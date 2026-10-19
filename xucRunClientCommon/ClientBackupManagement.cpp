// Purpose: Implements client commands for broker-backed backup, restore, listing, and deletion.
#include "ClientBackupManagement.h"

#include <Windows.h>
#include <CommCtrl.h>
#include <sddl.h>

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include "BrokerContracts.h"
#include "BrokerPipeClient.h"
#include "ClientAccountIdentity.h"
#include "ClientBrokerCredentialSelection.h"
#include "ClientBrokerLaunch.h"
#include "ClientConsoleOutput.h"
#include "ClientCredentialFormatting.h"
#include "ClientUiCommon.h"
#include "ClientWindowsError.h"
#include "Localization.h"

namespace
{
	constexpr int ListId = 1800;
	constexpr int CreateId = 1801;
	constexpr int RestoreId = 1802;
	constexpr int DeleteAllId = 1803;
	constexpr int DeleteId = 1804;
	constexpr int RestoreConfirmationCheckId = 1810;
	constexpr int RestoreConfirmationProceedId = 1811;
	constexpr int RestoreConfirmationCancelId = 1812;
	constexpr int DescriptionEditId = 1820;
	constexpr int DescriptionCreateId = 1821;
	constexpr int DescriptionCancelId = 1822;
	constexpr DWORD CommandPipeTimeoutMilliseconds = 135000;
	constexpr DWORD CommandResponseMagic = 0x42435558;
	constexpr DWORD MaximumCommandCharacters = 4 * 1024 * 1024;
	constexpr std::size_t DisplayedBackupIdLength = 12;
	constexpr wchar_t CommandPipeNamePrefix[] =
		L"\\\\.\\pipe\\xucRun.BackupCommand.";

	struct CommandResponseHeader
	{
		DWORD magic = CommandResponseMagic;
		DWORD windowsError = NO_ERROR;
		DWORD exitCode = 1;
		DWORD characterCount = 0;
	};

	struct CommandResponse
	{
		DWORD windowsError = NO_ERROR;
		DWORD exitCode = 1;
		std::wstring content;
	};

	enum class DialogAction { Close, Create, Restore, Delete, DeleteAll };
	enum class DialogMode { Manage, RestoreOnly };

	struct DialogContext
	{
		const std::vector<xucrun::broker::BackupEntry>* entries;
		DialogMode mode = DialogMode::Manage;
		DialogAction action = DialogAction::Close;
		int selectedIndex = -1;
		int sortColumn = -1;
		bool sortAscending = true;
	};

	struct RestoreConfirmationContext
	{
		std::wstring content;
		bool confirmed = false;
		bool createFallbackBackup = true;
		bool fallbackAvailable = true;
	};

	struct DescriptionDialogContext
	{
		std::wstring description;
		bool accepted = false;
	};

	std::wstring DisplayBackupId(const std::wstring& id)
	{
		return id.size() <= DisplayedBackupIdLength
			? id : id.substr(0, DisplayedBackupIdLength);
	}

	void UpdateBackupSortIndicator(HWND list, int sortColumn, bool ascending)
	{
		HWND header = ListView_GetHeader(list);
		const int columnCount = Header_GetItemCount(header);
		for (int column = 0; column < columnCount; column++)
		{
			HDITEMW item = {};
			item.mask = HDI_FORMAT;
			if (!Header_GetItem(header, column, &item))
				continue;
			item.fmt &= ~(HDF_SORTUP | HDF_SORTDOWN);
			if (column == sortColumn)
				item.fmt |= ascending ? HDF_SORTUP : HDF_SORTDOWN;
			Header_SetItem(header, column, &item);
		}
	}

	int CALLBACK CompareBackupRows(
		LPARAM leftValue, LPARAM rightValue, LPARAM contextValue)
	{
		const auto* context = reinterpret_cast<const DialogContext*>(contextValue);
		const std::size_t leftRow = static_cast<std::size_t>(leftValue);
		const std::size_t rightRow = static_cast<std::size_t>(rightValue);
		if (!context || context->sortColumn < 0 ||
			leftRow >= context->entries->size() || rightRow >= context->entries->size())
		{
			return 0;
		}
		const auto& left = (*context->entries)[leftRow];
		const auto& right = (*context->entries)[rightRow];
		const auto compareNumber = [](auto leftNumber, auto rightNumber) {
			return leftNumber < rightNumber ? -1 : leftNumber > rightNumber ? 1 : 0;
		};
		int comparison = 0;
		switch (context->sortColumn)
		{
		case 0: comparison = compareNumber(left.createdAt, right.createdAt); break;
		case 1: comparison = _wcsicmp(left.id.c_str(), right.id.c_str()); break;
		case 2: comparison = compareNumber(
			left.credentialCount, right.credentialCount); break;
		case 3: comparison = compareNumber(left.approvalCount, right.approvalCount); break;
		case 4: comparison = compareNumber(
			left.authenticityMarkerCount, right.authenticityMarkerCount); break;
		case 5: comparison = compareNumber(left.metadataCount, right.metadataCount); break;
		case 6:
			comparison = _wcsicmp(left.description.c_str(), right.description.c_str());
			break;
		default: break;
		}
		if (comparison == 0)
			comparison = _wcsicmp(left.id.c_str(), right.id.c_str());
		if (comparison == 0)
			comparison = leftRow < rightRow ? -1 : leftRow > rightRow ? 1 : 0;
		return context->sortAscending ? comparison : -comparison;
	}

	void SortBackupTable(HWND dialog, int column)
	{
		auto* context = reinterpret_cast<DialogContext*>(
			GetWindowLongPtrW(dialog, DWLP_USER));
		if (!context || column < 0 || column >= 7)
			return;
		context->sortAscending = context->sortColumn != column ||
			!context->sortAscending;
		context->sortColumn = column;
		HWND list = GetDlgItem(dialog, ListId);
		ListView_SortItems(list, CompareBackupRows, context);
		UpdateBackupSortIndicator(list, column, context->sortAscending);
	}

	int GetSelectedBackupIndex(HWND dialog)
	{
		HWND list = GetDlgItem(dialog, ListId);
		const int selectedItem = ListView_GetNextItem(list, -1, LVNI_SELECTED);
		if (selectedItem < 0)
			return -1;
		LVITEMW item = {};
		item.mask = LVIF_PARAM;
		item.iItem = selectedItem;
		return ListView_GetItem(list, &item) ? static_cast<int>(item.lParam) : -1;
	}

	std::wstring Describe(const xucrun::broker::BackupEntry& entry,
		bool abbreviatedId = false)
	{
		std::wstring result =
			xucrun::credential_formatting::FormatCredentialTimestamp(entry.createdAt) +
			L"   " + (abbreviatedId ? DisplayBackupId(entry.id) : entry.id) + L"   " +
			xucrun::localization::Format(xucrun::localization::TextId::BackupCounts,
				{ std::to_wstring(entry.credentialCount),
				  std::to_wstring(entry.approvalCount),
				  std::to_wstring(entry.authenticityMarkerCount),
				  std::to_wstring(entry.metadataCount) });
		if (!entry.description.empty())
		{
			result += L"   " + xucrun::localization::Format(
				xucrun::localization::TextId::BackupDescriptionDisplay,
				{ entry.description });
		}
		return result;
	}

	INT_PTR CALLBACK DescriptionDialogProc(HWND dialog, UINT message,
		WPARAM wParam, LPARAM lParam)
	{
		auto* context = reinterpret_cast<DescriptionDialogContext*>(
			GetWindowLongPtrW(dialog, DWLP_USER));
		if (message == WM_INITDIALOG)
		{
			context = reinterpret_cast<DescriptionDialogContext*>(lParam);
			SetWindowLongPtrW(dialog, DWLP_USER, reinterpret_cast<LONG_PTR>(context));
			xucrun::ui::ApplyApplicationIcon(dialog);
			SetWindowTextW(dialog, xucrun::ui::GetWindowTitle(
				xucrun::localization::GetText(
					xucrun::localization::TextId::BackupTitle)).c_str());
			xucrun::ui::CreateDialogControl(dialog, 0, L"STATIC",
				xucrun::localization::GetText(
					xucrun::localization::TextId::BackupDescriptionPrompt).c_str(),
				SS_LEFT, 12, 12, 316, 16);
			HWND edit = xucrun::ui::CreateDialogControl(dialog, WS_EX_CLIENTEDGE,
				L"EDIT", nullptr, ES_AUTOHSCROLL | WS_TABSTOP, 12, 32, 316, 22,
				DescriptionEditId);
			SendMessageW(edit, EM_LIMITTEXT,
				xucrun::backup::MaximumDescriptionLength, 0);
			xucrun::ui::CreateDialogControl(dialog, 0, L"BUTTON",
				xucrun::localization::GetText(
					xucrun::localization::TextId::BackupCreate).c_str(),
				BS_DEFPUSHBUTTON | WS_TABSTOP, 170, 66, 76, 26,
				DescriptionCreateId);
			xucrun::ui::CreateDialogControl(dialog, 0, L"BUTTON",
				xucrun::localization::GetText(
					xucrun::localization::TextId::ButtonCancel).c_str(),
				BS_PUSHBUTTON | WS_TABSTOP, 252, 66, 76, 26,
				DescriptionCancelId);
			SetFocus(edit);
			return FALSE;
		}
		if (message == WM_COMMAND && context)
		{
			switch (LOWORD(wParam))
			{
			case DescriptionCreateId:
			{
				const int length = GetWindowTextLengthW(
					GetDlgItem(dialog, DescriptionEditId));
				std::vector<wchar_t> text(static_cast<std::size_t>(length) + 1);
				if (length != 0)
				{
					GetDlgItemTextW(dialog, DescriptionEditId,
						text.data(), length + 1);
				}
				context->description.assign(text.data(), length);
				context->accepted = true;
				EndDialog(dialog, IDOK);
				return TRUE;
			}
			case DescriptionCancelId:
			case IDCANCEL:
				EndDialog(dialog, IDCANCEL);
				return TRUE;
			}
		}
		if (message == WM_CLOSE)
		{
			EndDialog(dialog, IDCANCEL);
			return TRUE;
		}
		return FALSE;
	}

	bool PromptForDescription(std::wstring& description)
	{
		struct Header { WORD version, signature; DWORD help, exStyle, style; WORD count;
			short x, y, cx, cy; };
		BYTE buffer[512] = {};
		auto* header = reinterpret_cast<Header*>(buffer);
		header->version = 1;
		header->signature = 0xffff;
		header->style = DS_SETFONT | DS_CENTER | WS_POPUP | WS_CAPTION | WS_SYSMENU;
		header->cx = 340;
		header->cy = 104;
		WORD* data = reinterpret_cast<WORD*>(buffer + sizeof(Header));
		*data++ = 0; *data++ = 0; *data++ = 0;
		xucrun::ui::WriteDefaultDialogFont(data, 50);
		DescriptionDialogContext context;
		DialogBoxIndirectParamW(GetModuleHandleW(nullptr),
			reinterpret_cast<LPCDLGTEMPLATEW>(buffer), xucrun::ui::GetDialogOwnerWindow(),
			DescriptionDialogProc, reinterpret_cast<LPARAM>(&context));
		description = std::move(context.description);
		return context.accepted;
	}

	INT_PTR CALLBACK RestoreConfirmationProc(HWND dialog, UINT message,
		WPARAM wParam, LPARAM lParam)
	{
		auto* context = reinterpret_cast<RestoreConfirmationContext*>(
			GetWindowLongPtrW(dialog, DWLP_USER));
		if (message == WM_INITDIALOG)
		{
			context = reinterpret_cast<RestoreConfirmationContext*>(lParam);
			SetWindowLongPtrW(dialog, DWLP_USER, reinterpret_cast<LONG_PTR>(context));
			xucrun::ui::ApplyApplicationIcon(dialog);
			SetWindowTextW(dialog, xucrun::ui::GetWindowTitle(
				xucrun::localization::GetText(
					xucrun::localization::TextId::BackupTitle)).c_str());
			xucrun::ui::CreateDialogControl(dialog, 0, L"STATIC",
				context->content.c_str(), SS_LEFT, 12, 12, 316, 46);
			HWND checkbox = xucrun::ui::CreateDialogControl(dialog, 0, L"BUTTON",
				xucrun::localization::GetText(
					xucrun::localization::TextId::BackupCreateFallback).c_str(),
				BS_AUTOCHECKBOX | WS_TABSTOP, 12, 65, 316, 28,
				RestoreConfirmationCheckId);
			SendMessageW(checkbox, BM_SETCHECK,
				context->fallbackAvailable ? BST_CHECKED : BST_UNCHECKED, 0);
			EnableWindow(checkbox, context->fallbackAvailable);
			xucrun::ui::CreateDialogControl(dialog, 0, L"BUTTON",
				xucrun::localization::GetText(
					xucrun::localization::TextId::BackupRestoreProceed).c_str(),
				BS_PUSHBUTTON | WS_TABSTOP, 170, 103, 76, 26,
				RestoreConfirmationProceedId);
			HWND cancel = xucrun::ui::CreateDialogControl(dialog, 0, L"BUTTON",
				xucrun::localization::GetText(
					xucrun::localization::TextId::ButtonCancel).c_str(),
				BS_DEFPUSHBUTTON | WS_TABSTOP, 252, 103, 76, 26,
				RestoreConfirmationCancelId);
			SetFocus(cancel);
			return FALSE;
		}
		if (message == WM_COMMAND && context)
		{
			switch (LOWORD(wParam))
			{
			case RestoreConfirmationProceedId:
				context->confirmed = true;
				context->createFallbackBackup = SendDlgItemMessageW(dialog,
					RestoreConfirmationCheckId, BM_GETCHECK, 0, 0) == BST_CHECKED;
				EndDialog(dialog, IDOK);
				return TRUE;
			case RestoreConfirmationCancelId:
			case IDCANCEL:
				EndDialog(dialog, IDCANCEL);
				return TRUE;
			}
		}
		if (message == WM_CLOSE)
		{
			EndDialog(dialog, IDCANCEL);
			return TRUE;
		}
		return FALSE;
	}

	bool ConfirmRestore(const xucrun::broker::BackupEntry& entry,
		bool fallbackAvailable, bool& createFallbackBackup)
	{
		struct Header { WORD version, signature; DWORD help, exStyle, style; WORD count;
			short x, y, cx, cy; };
		BYTE buffer[512] = {};
		auto* header = reinterpret_cast<Header*>(buffer);
		header->version = 1;
		header->signature = 0xffff;
		header->style = DS_SETFONT | DS_CENTER | WS_POPUP | WS_CAPTION | WS_SYSMENU;
		header->cx = 340;
		header->cy = 140;
		WORD* data = reinterpret_cast<WORD*>(buffer + sizeof(Header));
		*data++ = 0; *data++ = 0; *data++ = 0;
		xucrun::ui::WriteDefaultDialogFont(data, 50);
		RestoreConfirmationContext context;
		context.createFallbackBackup = fallbackAvailable;
		context.fallbackAvailable = fallbackAvailable;
		context.content = xucrun::localization::Format(
			xucrun::localization::TextId::BackupRestoreConfirm,
			{ xucrun::credential_formatting::FormatCredentialTimestamp(entry.createdAt),
				DisplayBackupId(entry.id) });
		DialogBoxIndirectParamW(GetModuleHandleW(nullptr),
			reinterpret_cast<LPCDLGTEMPLATEW>(buffer), xucrun::ui::GetDialogOwnerWindow(),
			RestoreConfirmationProc, reinterpret_cast<LPARAM>(&context));
		createFallbackBackup = context.createFallbackBackup;
		return context.confirmed;
	}

	bool CurrentStoreHasRestorableData()
	{
		xucrun::broker::AdministrativeInventoryRequest request;
		request.requestId = xucrun::broker::GenerateBrokerRequestId();
		const auto response =
			xucrun::broker::SubmitAdministrativeInventoryRequest(request);
		if (response.status !=
			xucrun::broker::PipeAdministrativeInventoryStatus::Succeeded ||
			response.result.status !=
			xucrun::broker::AdministrativeInventoryStatus::Succeeded)
		{
			return true;
		}
		return !response.result.credentials.empty() ||
			!response.result.approvals.empty() ||
			response.result.authenticityMarkerCount != 0;
	}

	INT_PTR CALLBACK DialogProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
	{
		auto* context = reinterpret_cast<DialogContext*>(
			GetWindowLongPtrW(dialog, DWLP_USER));
		if (message == WM_INITDIALOG)
		{
			context = reinterpret_cast<DialogContext*>(lParam);
			SetWindowLongPtrW(dialog, DWLP_USER, reinterpret_cast<LONG_PTR>(context));
			xucrun::ui::ApplyApplicationIcon(dialog);
			SetWindowTextW(dialog, xucrun::ui::GetWindowTitle(
				xucrun::localization::GetText(
					xucrun::localization::TextId::BackupTitle)).c_str());
			xucrun::ui::CreateDialogControl(dialog, 0, L"STATIC",
				xucrun::localization::GetText(
					xucrun::localization::TextId::BackupSummary).c_str(),
				SS_LEFT, 10, 10, 580, 28);
			HWND list = xucrun::ui::CreateDialogControl(dialog, WS_EX_CLIENTEDGE,
				WC_LISTVIEWW, nullptr, WS_TABSTOP | WS_VSCROLL | WS_HSCROLL |
					LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
				10, 42, 580, 180, ListId);
			ListView_SetExtendedListViewStyle(list,
				LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
			const std::array<xucrun::localization::TextId, 7> columns = {
				xucrun::localization::TextId::BackupColumnCreated,
				xucrun::localization::TextId::BackupColumnId,
				xucrun::localization::TextId::BackupColumnCredentials,
				xucrun::localization::TextId::BackupColumnApprovals,
				xucrun::localization::TextId::BackupColumnMarkers,
				xucrun::localization::TextId::BackupColumnMetadata,
				xucrun::localization::TextId::BackupColumnDescription
			};
			for (int column = 0; column < static_cast<int>(columns.size()); column++)
			{
				std::wstring heading = xucrun::localization::GetText(columns[column]);
				LVCOLUMNW listColumn = {};
				listColumn.mask = LVCF_TEXT | LVCF_FMT | LVCF_WIDTH;
				listColumn.fmt = LVCFMT_LEFT;
				listColumn.cx = 80;
				listColumn.pszText = heading.data();
				ListView_InsertColumn(list, column, &listColumn);
			}
			for (int row = 0; row < static_cast<int>(context->entries->size()); row++)
			{
				const auto& entry = (*context->entries)[row];
				std::array<std::wstring, 7> values = {
					xucrun::credential_formatting::FormatCredentialTimestamp(entry.createdAt),
					DisplayBackupId(entry.id),
					std::to_wstring(entry.credentialCount),
					std::to_wstring(entry.approvalCount),
					std::to_wstring(entry.authenticityMarkerCount),
					std::to_wstring(entry.metadataCount),
					entry.description
				};
				LVITEMW item = {};
				item.mask = LVIF_TEXT | LVIF_PARAM;
				item.iItem = row;
				item.lParam = row;
				item.pszText = values[0].data();
				ListView_InsertItem(list, &item);
				for (int column = 1; column < static_cast<int>(values.size()); column++)
					ListView_SetItemText(list, row, column, values[column].data());
			}
			std::array<int, 7> columnWidths = {};
			int fixedColumnsWidth = 0;
			for (int column = 0; column < static_cast<int>(columns.size()); column++)
			{
				const bool isDescription =
					column == static_cast<int>(columns.size()) - 1;
				int contentWidth = 0;
				if (!isDescription)
				{
					ListView_SetColumnWidth(list, column, LVSCW_AUTOSIZE);
					contentWidth = ListView_GetColumnWidth(list, column);
				}
				ListView_SetColumnWidth(list, column, LVSCW_AUTOSIZE_USEHEADER);
				columnWidths[column] = (std::max)(
					contentWidth, ListView_GetColumnWidth(list, column));
				if (!isDescription)
					fixedColumnsWidth += columnWidths[column];
			}
			RECT listRect = {};
			GetClientRect(list, &listRect);
			const int availableWidth = (std::max)(1,
				static_cast<int>(listRect.right - listRect.left) -
				GetSystemMetrics(SM_CXVSCROLL) - 4);
			columnWidths.back() = (std::max)(columnWidths.back(),
				availableWidth - fixedColumnsWidth);
			for (int column = 0; column < static_cast<int>(columns.size()); column++)
				ListView_SetColumnWidth(list, column, columnWidths[column]);
			if (!context->entries->empty())
			{
				ListView_SetItemState(list, 0, LVIS_SELECTED | LVIS_FOCUSED,
					LVIS_SELECTED | LVIS_FOCUSED);
			}
			HWND create = xucrun::ui::CreateDialogControl(dialog, 0, L"BUTTON",
				xucrun::localization::GetText(xucrun::localization::TextId::BackupCreate).c_str(),
				BS_PUSHBUTTON | WS_TABSTOP, 48, 234, 90, 20, CreateId);
			HWND restore = xucrun::ui::CreateDialogControl(dialog, 0, L"BUTTON",
				xucrun::localization::GetText(xucrun::localization::TextId::BackupRestore).c_str(),
				BS_PUSHBUTTON | WS_TABSTOP, 146, 234, 110, 20, RestoreId);
			HWND removeSelected = xucrun::ui::CreateDialogControl(dialog, 0, L"BUTTON",
				xucrun::localization::GetText(
					xucrun::localization::TextId::BackupDeleteSelected).c_str(),
				BS_PUSHBUTTON | WS_TABSTOP, 264, 234, 90, 20, DeleteId);
			HWND removeAll = xucrun::ui::CreateDialogControl(dialog, 0, L"BUTTON",
				xucrun::localization::GetText(xucrun::localization::TextId::BackupDeleteAll).c_str(),
				BS_PUSHBUTTON | WS_TABSTOP, 362, 234, 112, 20, DeleteAllId);
			xucrun::ui::CreateDialogControl(dialog, 0, L"BUTTON",
				xucrun::localization::GetText(xucrun::localization::TextId::BackupClose).c_str(),
				BS_DEFPUSHBUTTON | WS_TABSTOP, 482, 234, 70, 20, IDCANCEL);
			const bool manage = context->mode == DialogMode::Manage;
			EnableWindow(create, manage);
			EnableWindow(restore, !context->entries->empty());
			EnableWindow(removeSelected, manage && !context->entries->empty());
			EnableWindow(removeAll, manage && !context->entries->empty());
			SetForegroundWindow(dialog);
			return TRUE;
		}
		if (message == WM_NOTIFY && context)
		{
			const auto* header = reinterpret_cast<const NMHDR*>(lParam);
			if (header && header->idFrom == ListId &&
				header->code == LVN_COLUMNCLICK)
			{
				SortBackupTable(dialog,
					reinterpret_cast<const NMLISTVIEW*>(lParam)->iSubItem);
				return TRUE;
			}
		}
		if (message == WM_COMMAND && context)
		{
			const int id = LOWORD(wParam);
			if (id == CreateId || id == RestoreId || id == DeleteId || id == DeleteAllId)
			{
				context->action = id == CreateId ? DialogAction::Create
					: id == RestoreId ? DialogAction::Restore
					: id == DeleteId ? DialogAction::Delete : DialogAction::DeleteAll;
				context->selectedIndex = GetSelectedBackupIndex(dialog);
				EndDialog(dialog, id);
				return TRUE;
			}
			if (id == IDCANCEL) { EndDialog(dialog, IDCANCEL); return TRUE; }
		}
		if (message == WM_CLOSE) { EndDialog(dialog, IDCANCEL); return TRUE; }
		return FALSE;
	}

	DialogAction ShowDialog(const std::vector<xucrun::broker::BackupEntry>& entries,
		int& selectedIndex, DialogMode mode)
	{
		INITCOMMONCONTROLSEX controls = {
			sizeof(controls), ICC_LISTVIEW_CLASSES };
		InitCommonControlsEx(&controls);
		struct Header { WORD version, signature; DWORD help, exStyle, style; WORD count;
			short x, y, cx, cy; };
		BYTE buffer[512] = {};
		auto* header = reinterpret_cast<Header*>(buffer);
		header->version = 1;
		header->signature = 0xffff;
		header->style = DS_SETFONT | DS_CENTER | WS_POPUP | WS_CAPTION | WS_SYSMENU;
		header->cx = 600;
		header->cy = 270;
		WORD* data = reinterpret_cast<WORD*>(buffer + sizeof(Header));
		*data++ = 0; *data++ = 0; *data++ = 0;
		xucrun::ui::WriteDefaultDialogFont(data, 50);
		DialogContext context = { &entries, mode };
		DialogBoxIndirectParamW(GetModuleHandleW(nullptr),
			reinterpret_cast<LPCDLGTEMPLATEW>(buffer), xucrun::ui::GetDialogOwnerWindow(),
			DialogProc, reinterpret_cast<LPARAM>(&context));
		selectedIndex = context.selectedIndex;
		return context.action;
	}

	bool Submit(xucrun::broker::BackupAction action, const std::wstring& id,
		xucrun::broker::BackupResult& result, DWORD& error,
		bool createFallbackBackup = false,
		const std::wstring& description = {})
	{
		xucrun::broker::BackupRequest request;
		request.requestId = xucrun::broker::GenerateBrokerRequestId();
		request.action = action;
		request.createFallbackBackup = createFallbackBackup;
		request.backupId = id;
		request.description = description;
		const auto response = xucrun::broker::SubmitBackupRequest(request, 120000);
		if (response.status != xucrun::broker::PipeBackupStatus::Succeeded ||
			response.result.status != xucrun::broker::BackupStatus::Succeeded)
		{
			error = response.status == xucrun::broker::PipeBackupStatus::Succeeded
				? response.result.windowsError : response.windowsError;
			return false;
		}
		result = response.result;
		error = NO_ERROR;
		return true;
	}

	std::wstring GetSelfPath()
	{
		std::vector<wchar_t> path(512);
		for (;;)
		{
			const DWORD length = GetModuleFileNameW(nullptr, path.data(),
				static_cast<DWORD>(path.size()));
			if (length == 0) return L"";
			if (length < path.size() - 1) return std::wstring(path.data(), length);
			path.resize(path.size() * 2);
		}
	}

	void ReportCommandError(const std::wstring& message)
	{
		if (!xucrun::console_output::WriteErrorToAvailableTarget(message))
			xucrun::ui::ReportError(message,
				L"The backup description is invalid.");
	}

	void ReportCommandError(DWORD error)
	{
		ReportCommandError(xucrun::localization::Format(
			xucrun::localization::TextId::BackupFailed,
			{ xucrun::windows_error::FormatWindowsError(error) }));
	}

	std::wstring CreateCommandPipeToken()
	{
		const std::uint64_t first = xucrun::broker::GenerateBrokerRequestId();
		const std::uint64_t second = xucrun::broker::GenerateBrokerRequestId();
		if (first == 0 || second == 0)
			return L"";
		std::wostringstream token;
		token << std::hex << std::nouppercase << std::setfill(L'0') <<
			std::setw(16) << first << std::setw(16) << second;
		return token.str();
	}

	std::wstring BuildCommandPipeName(const std::wstring& token)
	{
		return std::wstring(CommandPipeNamePrefix) + token;
	}

	HANDLE CreateCommandPipe(const std::wstring& token, DWORD& error)
	{
		std::wstring currentUserSid;
		if (!xucrun::account_identity::GetCurrentUserSID(currentUserSid))
		{
			error = GetLastError();
			return INVALID_HANDLE_VALUE;
		}
		const std::wstring descriptorText =
			L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GA;;;" + currentUserSid + L")";
		PSECURITY_DESCRIPTOR descriptor = nullptr;
		if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
			descriptorText.c_str(), SDDL_REVISION_1, &descriptor, nullptr))
		{
			error = GetLastError();
			return INVALID_HANDLE_VALUE;
		}
		SECURITY_ATTRIBUTES security = { sizeof(security), descriptor, FALSE };
		HANDLE pipe = CreateNamedPipeW(BuildCommandPipeName(token).c_str(),
			PIPE_ACCESS_INBOUND | FILE_FLAG_FIRST_PIPE_INSTANCE | FILE_FLAG_OVERLAPPED,
			PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
			1, 0, 64 * 1024, 0, &security);
		error = pipe == INVALID_HANDLE_VALUE ? GetLastError() : NO_ERROR;
		LocalFree(descriptor);
		return pipe;
	}

	bool ReadPipeBytes(HANDLE pipe, void* buffer, DWORD byteCount, DWORD& error)
	{
		auto* destination = static_cast<std::uint8_t*>(buffer);
		DWORD position = 0;
		while (position < byteCount)
		{
			OVERLAPPED operation = {};
			operation.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
			if (!operation.hEvent)
			{
				error = GetLastError();
				return false;
			}
			DWORD transferred = 0;
			bool succeeded = ReadFile(pipe, destination + position,
				byteCount - position, &transferred, &operation) == TRUE;
			if (!succeeded && GetLastError() == ERROR_IO_PENDING)
			{
				if (WaitForSingleObject(operation.hEvent,
					CommandPipeTimeoutMilliseconds) == WAIT_OBJECT_0)
				{
					succeeded = GetOverlappedResult(
						pipe, &operation, &transferred, FALSE) == TRUE;
				}
				else
				{
					CancelIoEx(pipe, &operation);
					WaitForSingleObject(operation.hEvent, INFINITE);
					SetLastError(ERROR_TIMEOUT);
				}
			}
			if (!succeeded || transferred == 0)
			{
				error = succeeded ? ERROR_BROKEN_PIPE : GetLastError();
				CloseHandle(operation.hEvent);
				return false;
			}
			CloseHandle(operation.hEvent);
			position += transferred;
		}
		error = NO_ERROR;
		return true;
	}

	bool WritePipeBytes(HANDLE pipe, const void* buffer, DWORD byteCount)
	{
		const auto* source = static_cast<const std::uint8_t*>(buffer);
		DWORD position = 0;
		while (position < byteCount)
		{
			DWORD written = 0;
			if (!WriteFile(pipe, source + position, byteCount - position,
				&written, nullptr) || written == 0)
			{
				return false;
			}
			position += written;
		}
		return true;
	}

	bool SendCommandResponse(HANDLE pipe, DWORD windowsError, DWORD exitCode,
		const std::wstring& content)
	{
		CommandResponseHeader header;
		header.windowsError = windowsError;
		header.exitCode = exitCode;
		header.characterCount = content.size() <= MaximumCommandCharacters
			? static_cast<DWORD>(content.size()) : 0;
		if (content.size() > MaximumCommandCharacters)
		{
			header.windowsError = ERROR_INSUFFICIENT_BUFFER;
			header.exitCode = 1;
		}
		return WritePipeBytes(pipe, &header, sizeof(header)) &&
			(header.characterCount == 0 || WritePipeBytes(pipe, content.data(),
				header.characterCount * sizeof(wchar_t)));
	}

	bool ReceiveCommandResponse(HANDLE pipe, CommandResponse& response, DWORD& error)
	{
		CommandResponseHeader header;
		if (!ReadPipeBytes(pipe, &header, sizeof(header), error))
			return false;
		if (header.magic != CommandResponseMagic ||
			header.characterCount > MaximumCommandCharacters ||
			header.exitCode > static_cast<DWORD>((std::numeric_limits<int>::max)()))
		{
			error = ERROR_INVALID_DATA;
			return false;
		}
		response.windowsError = header.windowsError;
		response.exitCode = header.exitCode;
		response.content.resize(header.characterCount);
		return header.characterCount == 0 || ReadPipeBytes(pipe,
			response.content.data(), header.characterCount * sizeof(wchar_t), error);
	}

	CommandResponse RunCommand(const std::wstring& command)
	{
		xucrun::broker::BackupAction action;
		std::wstring id;
		std::wstring description;
		if (xucrun::broker::ParseAdministrativeBackupCreateCommand(
			command, description))
		{
			action = xucrun::broker::BackupAction::Create;
		}
		else if (command == L"list") action = xucrun::broker::BackupAction::List;
		else if (command == L"delete-all")
			action = xucrun::broker::BackupAction::DeleteAll;
		else
		{
			action = xucrun::broker::BackupAction::Restore;
			id = command.substr(8);
		}

		xucrun::broker::BackupResult result;
		DWORD error = NO_ERROR;
		if (!Submit(action, id, result, error,
			action == xucrun::broker::BackupAction::Restore, description))
		{
			return { error, 1, xucrun::localization::Format(
				xucrun::localization::TextId::BackupFailed,
				{ xucrun::windows_error::FormatWindowsError(error) }) };
		}
		std::wstring output;
		for (const auto& entry : result.entries)
			output += Describe(entry) + L"\n";
		if (!result.fallbackBackupId.empty())
		{
			output += xucrun::localization::Format(
				xucrun::localization::TextId::BackupRestored,
				{ result.fallbackBackupId }) + L"\n";
		}
		else if (action == xucrun::broker::BackupAction::Restore)
		{
			output += xucrun::localization::GetText(
				xucrun::localization::TextId::BackupRestoredWithoutFallback) + L"\n";
		}
		if (output.empty())
			output = xucrun::localization::GetText(
				xucrun::localization::TextId::BackupSucceeded);
		return { NO_ERROR, 0, std::move(output) };
	}

}

namespace xucrun::backup_management
{
	int Launch(const std::wstring& command, bool restoreOnly)
	{
		if (!command.empty() && !broker::IsValidAdministrativeBackupCommand(command))
			return 1;
		CredentialInfo credential;
		DWORD error = NO_ERROR;
		const auto selection = account_identity::IsUserInAdminGroup()
			? broker_credential_selection::BrokerCredentialSelectionOutcome::NotFound
			: broker_credential_selection::SelectBrokerCredential(
				L"", true, credential, error);
		if (selection == broker_credential_selection::BrokerCredentialSelectionOutcome::Cancelled)
			return command.empty() ? 0 : 1;
		if (selection == broker_credential_selection::BrokerCredentialSelectionOutcome::Failed)
		{
			const std::wstring message = localization::Format(
				localization::TextId::BrokerCredentialsQueryFailed,
				{ windows_error::FormatWindowsError(error) });
			if (command.empty())
				ui::ReportError(message, L"The administrative backup command is invalid.");
			else ReportCommandError(message);
			return 1;
		}
		const bool useStoredCredential = selection ==
			broker_credential_selection::BrokerCredentialSelectionOutcome::Selected;
		const std::wstring path = GetSelfPath();
		if (path.empty())
		{
			if (!command.empty()) ReportCommandError(GetLastError());
			return 1;
		}
		if (command.empty())
		{
			const wchar_t* viewerArgument = restoreOnly
				? broker::AdministrativeBackupRestoreViewerArgumentTail
				: broker::AdministrativeBackupViewerArgumentTail;
			if (useStoredCredential)
			{
				return broker_launch::LaunchWithBrokerCredential(credential, path,
					viewerArgument,
					broker::ElevationMode::RequireElevated,
					broker::LaunchVisibility::Normal) ==
					broker_launch::BrokerLaunchOutcome::Succeeded ? 0 : 1;
			}
			if (broker_launch::LaunchWithUac(path, viewerArgument,
				broker::LaunchVisibility::Normal, error))
			{
				return 0;
			}
			if (error != ERROR_CANCELLED)
				ui::ReportError(windows_error::FormatWindowsError(error),
					L"Launching the administrative backup command failed. Windows error: " +
						std::to_wstring(error));
			return 1;
		}

		const std::wstring pipeToken = CreateCommandPipeToken();
		if (pipeToken.empty())
		{
			ReportCommandError(ERROR_GEN_FAILURE);
			return 1;
		}
		HANDLE pipe = CreateCommandPipe(pipeToken, error);
		if (pipe == INVALID_HANDLE_VALUE)
		{
			ReportCommandError(error);
			return 1;
		}
		OVERLAPPED connection = {};
		connection.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		bool connectionPending = false;
		bool connected = false;
		if (connection.hEvent)
		{
			connected = ConnectNamedPipe(pipe, &connection) == TRUE;
			if (!connected)
			{
				const DWORD connectError = GetLastError();
				connectionPending = connectError == ERROR_IO_PENDING;
				connected = connectError == ERROR_PIPE_CONNECTED;
				if (connected) SetEvent(connection.hEvent);
				else if (!connectionPending) error = connectError;
			}
		}
		else error = GetLastError();
		if (!connected && !connectionPending)
		{
			if (connection.hEvent) CloseHandle(connection.hEvent);
			CloseHandle(pipe);
			ReportCommandError(error);
			return 1;
		}

		const std::wstring argument =
			std::wstring(broker::AdministrativeBackupCommandArgumentPrefix) +
			pipeToken + L":" + command;
		const bool launched = useStoredCredential
			? broker_launch::LaunchWithBrokerCredential(
				credential, path, argument, broker::ElevationMode::RequireElevated,
				broker::LaunchVisibility::Hidden) ==
					broker_launch::BrokerLaunchOutcome::Succeeded
			: broker_launch::LaunchWithUac(path, argument,
				broker::LaunchVisibility::Hidden, error);
		if (!launched)
		{
			CancelIoEx(pipe, &connection);
			if (connectionPending)
				WaitForSingleObject(connection.hEvent, INFINITE);
			CloseHandle(connection.hEvent);
			CloseHandle(pipe);
			if (!useStoredCredential && error != ERROR_CANCELLED)
				ReportCommandError(error);
			return 1;
		}
		if (connectionPending)
		{
			const DWORD wait = WaitForSingleObject(
				connection.hEvent, CommandPipeTimeoutMilliseconds);
			DWORD transferred = 0;
			connected = wait == WAIT_OBJECT_0 &&
				GetOverlappedResult(pipe, &connection, &transferred, FALSE);
			if (!connected)
			{
				error = wait == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError();
				CancelIoEx(pipe, &connection);
				WaitForSingleObject(connection.hEvent, INFINITE);
			}
		}
		CloseHandle(connection.hEvent);

		CommandResponse response;
		if (connected)
			connected = ReceiveCommandResponse(pipe, response, error);
		DisconnectNamedPipe(pipe);
		CloseHandle(pipe);
		if (!connected)
		{
			ReportCommandError(error);
			return 1;
		}

		const auto outputResult = response.exitCode == 0
			? console_output::WriteToAvailableTarget(
				localization::GetText(localization::TextId::BackupTitle), response.content)
			: (console_output::WriteErrorToAvailableTarget(response.content)
				? console_output::TextOutputResult::Complete
				: console_output::TextOutputResult::NoOutputTarget);
		if (outputResult != console_output::TextOutputResult::Complete)
		{
			if (response.exitCode == 0)
				ui::ReportInfo(response.content,
					L"The administrative backup command completed successfully.");
			else
				ui::ReportError(response.content,
					L"The administrative backup command failed. Exit code: " +
						std::to_wstring(response.exitCode));
		}
		return static_cast<int>(response.exitCode);
	}

	int ShowView(bool restoreOnly)
	{
		for (;;)
		{
			broker::BackupResult list;
			DWORD error = NO_ERROR;
			if (!Submit(broker::BackupAction::List, L"", list, error))
			{
				ui::ReportError(localization::Format(localization::TextId::BackupFailed,
					{ windows_error::FormatWindowsError(error) }),
					L"Loading the broker backup list failed. Windows error: " +
						std::to_wstring(error));
				return 1;
			}
			int selected = -1;
			const DialogAction action = ShowDialog(list.entries, selected,
				restoreOnly ? DialogMode::RestoreOnly : DialogMode::Manage);
			if (action == DialogAction::Close) return 0;
			broker::BackupResult operation;
			if (action == DialogAction::Create)
			{
				std::wstring description;
				if (!PromptForDescription(description))
					continue;
				if (!Submit(broker::BackupAction::Create, L"", operation, error,
					false, description))
				{
					ui::ReportError(localization::Format(localization::TextId::BackupFailed,
						{ windows_error::FormatWindowsError(error) }),
						L"Creating a broker backup failed. Windows error: " +
							std::to_wstring(error));
					continue;
				}
				ui::ReportInfo(
					localization::GetText(localization::TextId::BackupCreated),
					L"Broker backup created successfully.");
			}
			else if (action == DialogAction::Restore && selected >= 0 &&
				selected < static_cast<int>(list.entries.size()))
			{
				const auto& entry = list.entries[selected];
				const bool fallbackAvailable = CurrentStoreHasRestorableData();
				bool createFallbackBackup = fallbackAvailable;
				if (!ConfirmRestore(entry, fallbackAvailable, createFallbackBackup))
					continue;
				if (!Submit(broker::BackupAction::Restore, entry.id, operation, error,
					createFallbackBackup))
					ui::ReportError(localization::Format(localization::TextId::BackupFailed,
						{ windows_error::FormatWindowsError(error) }),
						L"Restoring the broker backup failed. Windows error: " +
							std::to_wstring(error));
				else if (!fallbackAvailable)
					ui::ReportInfo(localization::GetText(
						localization::TextId::BackupRestoredEmptyState),
						L"Broker backup restored successfully into an empty store.");
				else if (!createFallbackBackup)
					ui::ReportInfo(localization::GetText(
						localization::TextId::BackupRestoredWithoutFallback),
						L"Broker backup restored successfully without a fallback backup.");
				else
					ui::ReportInfo(localization::Format(
						localization::TextId::BackupRestored,
						{ operation.fallbackBackupId }),
						L"Broker backup restored successfully. Fallback backup ID: " +
							operation.fallbackBackupId);
				if (restoreOnly && operation.status == broker::BackupStatus::Succeeded)
					return 0;
			}
			else if (action == DialogAction::Delete && selected >= 0 &&
				selected < static_cast<int>(list.entries.size()))
			{
				const auto& entry = list.entries[selected];
				if (ui::ShowMessageBox(ui::GetDialogOwnerWindow(),
					localization::Format(localization::TextId::BackupDeleteSelectedConfirm,
						{ credential_formatting::FormatCredentialTimestamp(entry.createdAt),
							DisplayBackupId(entry.id) }),
					ui::GetWindowTitle(localization::GetText(localization::TextId::BackupTitle)),
					MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
				{
					continue;
				}
				if (!Submit(broker::BackupAction::Delete, entry.id, operation, error))
					ui::ReportError(localization::Format(localization::TextId::BackupFailed,
						{ windows_error::FormatWindowsError(error) }),
						L"Deleting the selected broker backup failed. Windows error: " +
							std::to_wstring(error));
			}
			else if (action == DialogAction::DeleteAll)
			{
				if (ui::ShowMessageBox(ui::GetDialogOwnerWindow(),
					localization::GetText(localization::TextId::BackupDeleteConfirm),
					ui::GetWindowTitle(localization::GetText(localization::TextId::BackupTitle)),
					MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
					continue;
				if (!Submit(broker::BackupAction::DeleteAll, L"", operation, error))
					ui::ReportError(localization::Format(localization::TextId::BackupFailed,
						{ windows_error::FormatWindowsError(error) }),
						L"Deleting all broker backups failed. Windows error: " +
							std::to_wstring(error));
			}
		}
	}

	int ExecuteInternalCommand(const std::wstring& argumentTail)
	{
		std::wstring pipeToken;
		std::wstring command;
		if (!broker::ParseAdministrativeBackupCommandArgument(
			argumentTail, pipeToken, command))
		{
			return 1;
		}
		const std::wstring pipeName = BuildCommandPipeName(pipeToken);
		if (!WaitNamedPipeW(pipeName.c_str(), CommandPipeTimeoutMilliseconds))
			return 1;
		HANDLE pipe = CreateFileW(pipeName.c_str(), GENERIC_WRITE, 0, nullptr,
			OPEN_EXISTING, 0, nullptr);
		if (pipe == INVALID_HANDLE_VALUE)
			return 1;

		const CommandResponse response = RunCommand(command);
		const bool sent = SendCommandResponse(pipe, response.windowsError,
			response.exitCode, response.content);
		CloseHandle(pipe);
		return sent ? static_cast<int>(response.exitCode) : 1;
	}
}
