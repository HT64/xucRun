// Purpose: Implements the client-side selection dialog for stored credentials.
// Operation: Manages the selection in the dialog context and maps the sorted result to the input vector.
#include "ClientCredentialSelectionDialog.h"

#include <Windows.h>

#include <algorithm>

#include "ClientAccountIdentity.h"
#include "ClientLogging.h"
#include "ClientUiCommon.h"
#include "Localization.h"

namespace
{
	using xucrun::CredentialInfo;
	using xucrun::credential_formatting::CredentialSelectionMode;
	constexpr int CredentialListId = 1000;
	constexpr int CredentialHintId = 1600;

	struct CredentialSelectionContext
	{
		const std::vector<CredentialInfo>* credentials;
		CredentialSelectionMode mode;
		const std::vector<std::size_t>* approvalCounts;
		int selectedIndex = -1;
	};

	INT_PTR CALLBACK CredentialSelectionDialogProc(
		HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
	{
		switch (message)
		{
		case WM_INITDIALOG:
		{
			auto* context = reinterpret_cast<CredentialSelectionContext*>(lParam);
			SetWindowLongPtrW(dialog, DWLP_USER, reinterpret_cast<LONG_PTR>(context));
			xucrun::ui::ApplyApplicationIcon(dialog);
			const bool deleteMode = context->mode == CredentialSelectionMode::Delete;
			const bool resetMode =
				context->mode == CredentialSelectionMode::ResetApproval;
			const bool approvalMode =
				context->mode == CredentialSelectionMode::ApprovalConfirmation;
			SetWindowTextW(dialog, xucrun::ui::GetWindowTitle(
				xucrun::credential_formatting::GetCredentialSelectionTitle(
					context->mode)).c_str());

			RECT dialogRect = {};
			GetWindowRect(dialog, &dialogRect);
			const int x = (GetSystemMetrics(SM_CXSCREEN) -
				(dialogRect.right - dialogRect.left)) / 2;
			const int y = (GetSystemMetrics(SM_CYSCREEN) -
				(dialogRect.bottom - dialogRect.top)) / 2;
			SetWindowPos(dialog, HWND_TOPMOST, x, y, 0, 0,
				SWP_NOSIZE | SWP_SHOWWINDOW);
			SetForegroundWindow(dialog);
			SetFocus(dialog);
			if (GetForegroundWindow() != dialog)
			{
				FLASHWINFO flash = { sizeof(FLASHWINFO) };
				flash.hwnd = dialog;
				flash.dwFlags = FLASHW_ALL | FLASHW_TIMERNOFG;
				flash.uCount = 5;
				FlashWindowEx(&flash);
				MessageBeep(MB_ICONINFORMATION);
			}

			std::wstring username;
			std::wstring domain;
			const std::wstring invokingAccount =
				xucrun::account_identity::GetCurrentUserInfo(username, domain)
				? (domain.empty() ? username : domain + L"\\" + username)
				: xucrun::localization::GetText(
					xucrun::localization::TextId::ValueUnknown);
			const std::wstring header = xucrun::localization::Format(deleteMode
				? xucrun::localization::TextId::CredentialSelectionDeleteHeader
				: (resetMode
					? xucrun::localization::TextId::CredentialSelectionResetHeader
					: (approvalMode
						? xucrun::localization::TextId::CredentialSelectionApprovalHeader
						: (context->mode == CredentialSelectionMode::ElevatedLaunch
						? xucrun::localization::TextId::CredentialSelectionElevatedHeader
						: xucrun::localization::TextId::CredentialSelectionLaunchHeader))),
				{ invokingAccount });
			const std::wstring resetHint = resetMode && context->approvalCounts &&
				!context->approvalCounts->empty() && !context->credentials->empty()
				? xucrun::credential_formatting::GetResetApprovalSelectionHint(
					(*context->approvalCounts)[0], (*context->credentials)[0].username)
				: L"";
			const std::wstring footer = deleteMode
				? xucrun::localization::GetText(
					xucrun::localization::TextId::CredentialSelectionDeleteHint)
				: (resetMode ? resetHint : xucrun::localization::GetText(approvalMode
					? xucrun::localization::TextId::CredentialSelectionApprovalHint
					: xucrun::localization::TextId::CredentialSelectionLaunchHint));
			const int headerHeight = xucrun::ui::MeasureDialogTextHeight(
				dialog, header, 268, 18);
			const std::size_t visibleCredentialCount = (std::min)(
				context->credentials->size(), static_cast<std::size_t>(8));
			const int listHeight = 4 + static_cast<int>(visibleCredentialCount) * 12;
			const int listY = 10 + headerHeight;
			const int footerHeight = xucrun::ui::MeasureDialogTextHeight(
				dialog, footer, 268, 55);
			const int footerY = listY + listHeight + 4;
			const int buttonY = footerY + footerHeight + 7;
			xucrun::ui::ResizeDialogClientHeight(dialog, buttonY + 22);
			xucrun::ui::CreateDialogControl(dialog, 0, L"STATIC", header.c_str(),
				SS_LEFT, 6, 6, 268, headerHeight);
			HWND credentialList = xucrun::ui::CreateDialogControl(dialog,
				WS_EX_CLIENTEDGE, L"LISTBOX", nullptr,
				WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
				6, listY, 268, listHeight, CredentialListId);
			for (const auto& credential : *context->credentials)
			{
				SendMessageW(credentialList, LB_ADDSTRING, 0,
					reinterpret_cast<LPARAM>(credential.username.c_str()));
			}
			SendMessageW(credentialList, LB_SETCURSEL, 0, 0);
			xucrun::ui::CreateDialogControl(dialog, 0, L"STATIC", footer.c_str(),
				SS_LEFT, 6, footerY, 268, footerHeight, CredentialHintId);

			const std::wstring okText = deleteMode
				? xucrun::localization::GetText(xucrun::localization::TextId::ButtonDelete)
				: (resetMode && context->approvalCounts &&
					!context->approvalCounts->empty() && (*context->approvalCounts)[0] > 1
					? xucrun::localization::GetText(xucrun::localization::TextId::ButtonNext)
					: (resetMode ? xucrun::localization::GetText(
						xucrun::localization::TextId::ButtonReset) : L"OK"));
			HWND okButton = xucrun::ui::CreateDialogControl(dialog, 0, L"BUTTON",
				okText.c_str(), BS_DEFPUSHBUTTON | WS_TABSTOP,
				88, buttonY, 44, 14, IDOK);
			if (resetMode && context->approvalCounts &&
				!context->approvalCounts->empty() && (*context->approvalCounts)[0] == 0)
			{
				EnableWindow(okButton, FALSE);
			}
			xucrun::ui::CreateDialogControl(dialog, 0, L"BUTTON",
				xucrun::localization::GetText(
					xucrun::localization::TextId::ButtonCancel).c_str(),
				BS_PUSHBUTTON | WS_TABSTOP, 138, buttonY, 54, 14, IDCANCEL);
			SetFocus(credentialList);
			return FALSE;
		}

		case WM_COMMAND:
		{
			auto* context = reinterpret_cast<CredentialSelectionContext*>(
				GetWindowLongPtrW(dialog, DWLP_USER));
			if (!context)
				return FALSE;
			const int command = LOWORD(wParam);
			if (context->mode == CredentialSelectionMode::ResetApproval &&
				command == CredentialListId && HIWORD(wParam) == LBN_SELCHANGE &&
				context->approvalCounts)
			{
				const LRESULT selectedIndex = SendMessageW(
					GetDlgItem(dialog, CredentialListId), LB_GETCURSEL, 0, 0);
				const std::size_t index = static_cast<std::size_t>(selectedIndex);
				if (index < context->approvalCounts->size())
				{
					const std::size_t approvalCount = (*context->approvalCounts)[index];
					SetWindowTextW(GetDlgItem(dialog, CredentialHintId),
						xucrun::credential_formatting::GetResetApprovalSelectionHint(
							approvalCount,
							(*context->credentials)[index].username).c_str());
					SetWindowTextW(GetDlgItem(dialog, IDOK),
						xucrun::localization::GetText(
							approvalCount > 1
								? xucrun::localization::TextId::ButtonNext
								: xucrun::localization::TextId::ButtonReset).c_str());
					EnableWindow(GetDlgItem(dialog, IDOK), approvalCount > 0);
				}
			}

			if (command == IDOK)
			{
				context->selectedIndex = static_cast<int>(SendMessageW(
					GetDlgItem(dialog, CredentialListId), LB_GETCURSEL, 0, 0));
				EndDialog(dialog, IDOK);
				return TRUE;
			}
			if (command == IDCANCEL)
			{
				context->selectedIndex = -1;
				EndDialog(dialog, IDCANCEL);
				return TRUE;
			}
			break;
		}

		case WM_CLOSE:
		{
			auto* context = reinterpret_cast<CredentialSelectionContext*>(
				GetWindowLongPtrW(dialog, DWLP_USER));
			if (context)
				context->selectedIndex = -1;
			EndDialog(dialog, IDCANCEL);
			return TRUE;
		}
		}
		return FALSE;
	}
}

namespace xucrun::credential_selection_dialog
{
	int ShowCredentialSelectionDialog(
		const std::vector<CredentialInfo>& credentials,
		credential_formatting::CredentialSelectionMode mode,
		const std::vector<std::size_t>* approvalCounts)
	{
		if (credentials.empty())
			return -1;
		if (credentials.size() == 1 &&
			mode != credential_formatting::CredentialSelectionMode::Delete &&
			mode != credential_formatting::CredentialSelectionMode::ResetApproval)
		{
			return 0;
		}

		std::vector<CredentialInfo> sortedCredentials = credentials;
		std::sort(sortedCredentials.begin(), sortedCredentials.end(),
			[](const CredentialInfo& left, const CredentialInfo& right) {
				const bool leftIsAdministrator = left.username == L"Administrator";
				const bool rightIsAdministrator = right.username == L"Administrator";
				if (leftIsAdministrator != rightIsAdministrator)
					return leftIsAdministrator;
				return left.username < right.username;
			});

		std::vector<std::size_t> sortedApprovalCounts;
		if (approvalCounts && approvalCounts->size() == credentials.size())
		{
			for (const auto& sortedCredential : sortedCredentials)
			{
				const auto original = std::find_if(credentials.begin(), credentials.end(),
					[&sortedCredential](const CredentialInfo& credential) {
						return !sortedCredential.accountSid.empty()
							? credential.accountSid == sortedCredential.accountSid
							: credential.targetName == sortedCredential.targetName;
					});
				sortedApprovalCounts.push_back(original == credentials.end() ? 0
					: (*approvalCounts)[static_cast<std::size_t>(
						std::distance(credentials.begin(), original))]);
			}
		}

		struct DialogTemplateHeader
		{
			WORD dlgVer;
			WORD signature;
			DWORD helpID;
			DWORD exStyle;
			DWORD style;
			WORD itemCount;
			short x;
			short y;
			short cx;
			short cy;
		};
		BYTE buffer[1024] = {};
		auto* dialogTemplate = reinterpret_cast<DialogTemplateHeader*>(buffer);
		dialogTemplate->dlgVer = 1;
		dialogTemplate->signature = 0xFFFF;
		dialogTemplate->style = DS_SETFONT | DS_CENTER | WS_POPUP | WS_CAPTION |
			WS_SYSMENU | WS_BORDER;
		dialogTemplate->cx = 280;
		dialogTemplate->cy = 105;
		WORD* data = reinterpret_cast<WORD*>(buffer + sizeof(DialogTemplateHeader));
		*data++ = 0;
		*data++ = 0;
		const std::wstring title = ui::GetWindowTitle(
			credential_formatting::GetCredentialSelectionTitle(mode));
		wcscpy_s(reinterpret_cast<wchar_t*>(data), 100, title.c_str());
		data += title.size() + 1;
		ui::WriteDefaultDialogFont(data, 50);

		CredentialSelectionContext context = { &sortedCredentials, mode,
			sortedApprovalCounts.empty() ? nullptr : &sortedApprovalCounts };
		const INT_PTR result = DialogBoxIndirectParamW(GetModuleHandle(nullptr),
			reinterpret_cast<LPCDLGTEMPLATEW>(buffer), ui::GetDialogOwnerWindow(),
			CredentialSelectionDialogProc, reinterpret_cast<LPARAM>(&context));
		if (result == IDOK && context.selectedIndex >= 0)
		{
			const CredentialInfo& selected =
				sortedCredentials[static_cast<std::size_t>(context.selectedIndex)];
			for (std::size_t index = 0; index < credentials.size(); index++)
			{
				const bool sameCredential = !selected.accountSid.empty()
					? credentials[index].accountSid == selected.accountSid
					: credentials[index].targetName == selected.targetName;
				if (sameCredential)
				{
					client_logging::Write(L"Info",
						L"User selected credential: " +
						credentials[index].username);
					return static_cast<int>(index);
				}
			}
		}
		client_logging::Write(L"Info", L"User cancelled selection");
		return -1;
	}
}