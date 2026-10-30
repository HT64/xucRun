// Purpose: Implements creation and deletion of stored broker credentials.
// Approach: Implements the behavior with focused helpers built on shared xucRun interfaces and Windows APIs.
// Behavior: Combines account selection, secure prompt input, and broker management calls.
#include "ClientBrokerCredentialManagement.h"

#include <CommCtrl.h>

#include <algorithm>
#include <filesystem>
#include <shellapi.h>
#include <vector>

#include "BrokerContracts.h"
#include "BrokerPipeClient.h"
#include "ClientAccountIdentity.h"
#include "ClientBrokerCredentialSelection.h"
#include "ClientConsoleOutput.h"
#include "ClientCredentialFormatting.h"
#include "ClientCredentialInfo.h"
#include "ClientCredentialSelectionDialog.h"
#include "ClientLocalAccountEnumeration.h"
#include "ClientLocalAccountSelectionDialog.h"
#include "ClientLogging.h"
#include "ClientTextDialog.h"
#include "ClientUiCommon.h"
#include "ClientWindowsError.h"
#include "DocumentationNameMapping.h"
#include "Localization.h"
#include "TargetNaming.h"

#pragma comment(lib, "Comctl32.lib")

namespace xucrun::broker_credential_management
{
	namespace
	{
		constexpr int CredentialTableSummaryId = 1700;
		constexpr int CredentialTableListId = 1701;
		constexpr int CredentialTableNoteId = 1702;
		constexpr int CredentialTableDeleteAllId = 1703;
		constexpr int CredentialTableDeleteSelectedId = 1705;

		enum class CredentialTableAction
		{
			Closed,
			DeleteAll,
			DeleteSelected
		};

		struct CredentialTableResult
		{
			CredentialTableAction action = CredentialTableAction::Closed;
			int selectedRow = -1;
		};

		std::wstring DisplayAccount(const std::wstring& accountName)
		{
			return documentation_names::DisplayAccount(accountName);
		}

		struct CredentialTableContext
		{
			const std::wstring* title;
			const std::wstring* summary;
			const std::wstring* note;
			const std::vector<CredentialInfo>* credentials;
			const std::vector<std::size_t>* approvalCounts;
			int selectedRow = -1;
		};

		std::wstring TrimLineBreaks(std::wstring text)
		{
			const size_t first = text.find_first_not_of(L"\r\n");
			if (first == std::wstring::npos)
				return L"";
			const size_t last = text.find_last_not_of(L"\r\n");
			return text.substr(first, last - first + 1);
		}

		void LayoutCredentialTable(HWND dialog)
		{
			RECT clientRect = {};
			GetClientRect(dialog, &clientRect);
			const int width = clientRect.right - clientRect.left;
			const int height = clientRect.bottom - clientRect.top;
			const UINT dpi = GetDpiForWindow(dialog);
			const int margin = MulDiv(12, dpi ? dpi : 96, 96);
			const int summaryHeight = MulDiv(54, dpi ? dpi : 96, 96);
			const int noteHeight = MulDiv(46, dpi ? dpi : 96, 96);
			const int buttonWidth = MulDiv(76, dpi ? dpi : 96, 96);
			const int actionWidth = MulDiv(170, dpi ? dpi : 96, 96);
			const int buttonHeight = MulDiv(26, dpi ? dpi : 96, 96);
			const int gap = MulDiv(8, dpi ? dpi : 96, 96);
			const int buttonY = height - margin - buttonHeight;
			const int noteY = buttonY - gap - noteHeight;
			const int listY = margin + summaryHeight;

			MoveWindow(GetDlgItem(dialog, CredentialTableSummaryId), margin, margin,
				(std::max)(1, width - 2 * margin), summaryHeight - gap, FALSE);
			HWND list = GetDlgItem(dialog, CredentialTableListId);
			MoveWindow(list, margin, listY, (std::max)(1, width - 2 * margin),
				(std::max)(1, noteY - gap - listY), FALSE);
			MoveWindow(GetDlgItem(dialog, CredentialTableNoteId), margin, noteY,
				(std::max)(1, width - 2 * margin), noteHeight, FALSE);
			const int buttonsWidth = 2 * actionWidth + 3 * gap + buttonWidth;
			const int buttonsX = (width - buttonsWidth) / 2;
			MoveWindow(GetDlgItem(dialog, CredentialTableDeleteAllId),
				buttonsX, buttonY, actionWidth, buttonHeight, FALSE);
			MoveWindow(GetDlgItem(dialog, CredentialTableDeleteSelectedId),
				buttonsX + actionWidth + gap, buttonY,
				actionWidth, buttonHeight, FALSE);
			MoveWindow(GetDlgItem(dialog, IDOK),
				buttonsX + 2 * (actionWidth + gap),
				buttonY, buttonWidth, buttonHeight, FALSE);

			RECT listRect = {};
			GetClientRect(list, &listRect);
			const int listWidth = (std::max)(1,
				static_cast<int>(listRect.right - listRect.left) -
				GetSystemMetrics(SM_CXVSCROLL) - 2);
			const int widths[] = {
				listWidth * 29 / 100, listWidth * 29 / 100,
				listWidth * 27 / 100,
				listWidth - listWidth * 85 / 100
			};
			for (int column = 0; column < 4; column++)
				ListView_SetColumnWidth(list, column, widths[column]);
			RedrawWindow(dialog, nullptr, nullptr,
				RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
		}

		void SetCredentialTableCell(
			HWND list, int row, int column, const std::wstring& text)
		{
			ListView_SetItemText(
				list, row, column, const_cast<LPWSTR>(text.c_str()));
		}

		INT_PTR CALLBACK CredentialTableDialogProc(
			HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
		{
			switch (message)
			{
			case WM_INITDIALOG:
			{
				const auto* context =
					reinterpret_cast<const CredentialTableContext*>(lParam);
				SetWindowLongPtrW(dialog, DWLP_USER, lParam);
				ui::ApplyApplicationIcon(dialog);
				SetWindowTextW(dialog, context->title->c_str());
				SetWindowPos(dialog, HWND_TOPMOST, 0, 0, 0, 0,
					SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
				SetForegroundWindow(dialog);

				ui::CreateDialogControl(dialog, 0, L"STATIC", context->summary->c_str(),
					SS_LEFT, 8, 8, 464, 34, CredentialTableSummaryId);
				HWND list = ui::CreateDialogControl(dialog, WS_EX_CLIENTEDGE,
					WC_LISTVIEWW, nullptr, WS_TABSTOP | WS_VSCROLL | LVS_REPORT |
					LVS_SINGLESEL | LVS_SHOWSELALWAYS, 8, 42, 464, 190,
					CredentialTableListId);
				ListView_SetExtendedListViewStyle(list,
					LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

				const localization::TextId columnTextIds[] = {
					localization::TextId::ShowCredentialsColumnAccount,
					localization::TextId::ShowCredentialsColumnStored,
					localization::TextId::ShowCredentialsColumnAccountType,
					localization::TextId::ShowCredentialsColumnApprovals
				};
				for (int column = 0; column < 4; column++)
				{
					const std::wstring text = localization::GetText(columnTextIds[column]);
					LVCOLUMNW listColumn = {};
					listColumn.mask = LVCF_TEXT | LVCF_FMT;
					listColumn.fmt = LVCFMT_LEFT;
					listColumn.pszText = const_cast<LPWSTR>(text.c_str());
					ListView_InsertColumn(list, column, &listColumn);
				}

				for (int row = 0; row < static_cast<int>(context->credentials->size()); row++)
				{
					const CredentialInfo& credential = (*context->credentials)[row];
					const bool isAdministrator =
						account_identity::IsUserInAdminGroup(credential.username);
					LVITEMW item = {};
					item.mask = LVIF_TEXT;
					item.iItem = row;
					const std::wstring displayedAccount =
						DisplayAccount(credential.username);
					item.pszText = const_cast<LPWSTR>(displayedAccount.c_str());
					ListView_InsertItem(list, &item);
					SetCredentialTableCell(list, row, 1,
						credential_formatting::FormatCredentialTimestamp(
							credential.lastWritten));
					SetCredentialTableCell(list, row, 2, localization::GetText(
						isAdministrator
							? localization::TextId::ShowCredentialsAdministratorAccount
							: localization::TextId::ShowCredentialsStandardAccount));
					SetCredentialTableCell(list, row, 3, std::to_wstring(
						(*context->approvalCounts)[static_cast<std::size_t>(row)]));
				}

				ui::CreateDialogControl(dialog, 0, L"STATIC", context->note->c_str(),
					SS_LEFT, 8, 238, 464, 30, CredentialTableNoteId);
				ui::CreateDialogControl(dialog, 0, L"BUTTON",
					localization::GetText(
						localization::TextId::ShowCredentialsDeleteAll).c_str(),
					BS_PUSHBUTTON | WS_TABSTOP, 20, 274, 140, 14,
					CredentialTableDeleteAllId);
				HWND deleteSelectedButton = ui::CreateDialogControl(dialog, 0, L"BUTTON",
					localization::GetText(
						localization::TextId::ShowCredentialsDeleteSelected).c_str(),
					BS_PUSHBUTTON | WS_TABSTOP, 168, 274, 140, 14,
					CredentialTableDeleteSelectedId);
				EnableWindow(deleteSelectedButton, FALSE);
				HWND okButton = ui::CreateDialogControl(dialog, 0, L"BUTTON", L"OK",
					BS_DEFPUSHBUTTON | WS_TABSTOP, 316, 274, 76, 14, IDOK);
				LayoutCredentialTable(dialog);
				SetFocus(okButton);
				return FALSE;
			}

			case WM_SIZE:
				LayoutCredentialTable(dialog);
				return TRUE;

			case WM_NOTIFY:
			{
				const auto* header = reinterpret_cast<const NMHDR*>(lParam);
				if (header && header->idFrom == CredentialTableListId &&
					header->code == LVN_ITEMCHANGED)
				{
					EnableWindow(GetDlgItem(dialog, CredentialTableDeleteSelectedId),
						ListView_GetNextItem(GetDlgItem(dialog, CredentialTableListId),
							-1, LVNI_SELECTED) >= 0);
					return TRUE;
				}
				break;
			}

			case WM_GETMINMAXINFO:
			{
				auto* sizeInfo = reinterpret_cast<MINMAXINFO*>(lParam);
				const UINT dpi = GetDpiForWindow(dialog);
				sizeInfo->ptMinTrackSize.x = MulDiv(680, dpi ? dpi : 96, 96);
				sizeInfo->ptMinTrackSize.y = MulDiv(360, dpi ? dpi : 96, 96);
				return TRUE;
			}

			case WM_COMMAND:
				if (LOWORD(wParam) == CredentialTableDeleteAllId ||
					LOWORD(wParam) == CredentialTableDeleteSelectedId)
				{
					auto* context = reinterpret_cast<CredentialTableContext*>(
						GetWindowLongPtrW(dialog, DWLP_USER));
					if (context && LOWORD(wParam) == CredentialTableDeleteSelectedId)
					{
						context->selectedRow = ListView_GetNextItem(
							GetDlgItem(dialog, CredentialTableListId),
							-1, LVNI_SELECTED);
					}
					EndDialog(dialog, LOWORD(wParam));
					return TRUE;
				}
				if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
				{
					EndDialog(dialog, LOWORD(wParam));
					return TRUE;
				}
				break;

			case WM_CLOSE:
				EndDialog(dialog, IDCANCEL);
				return TRUE;
			}
			return FALSE;
		}

		CredentialTableResult ShowCredentialTable(const std::wstring& title,
			const std::wstring& summary, const std::wstring& note,
			const std::vector<CredentialInfo>& credentials,
			const std::vector<std::size_t>& approvalCounts, HWND owner)
		{
			INITCOMMONCONTROLSEX controls = {
				sizeof(controls), ICC_LISTVIEW_CLASSES };
			InitCommonControlsEx(&controls);

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
				WS_SYSMENU | WS_SIZEBOX | WS_MAXIMIZEBOX;
			dialogTemplate->cx = 400;
			dialogTemplate->cy = 300;

			WORD* data = reinterpret_cast<WORD*>(buffer + sizeof(DialogTemplateHeader));
			*data++ = 0;
			*data++ = 0;
			wcscpy_s(reinterpret_cast<wchar_t*>(data), 100, title.c_str());
			data += title.length() + 1;
			ui::WriteDefaultDialogFont(data, 50);

			CredentialTableContext context = {
				&title, &summary, &note, &credentials, &approvalCounts };
			const INT_PTR result = DialogBoxIndirectParamW(GetModuleHandleW(nullptr),
				reinterpret_cast<LPCDLGTEMPLATEW>(buffer), owner,
				CredentialTableDialogProc, reinterpret_cast<LPARAM>(&context));
			if (result == CredentialTableDeleteAllId)
				return { CredentialTableAction::DeleteAll, -1 };
			if (result == CredentialTableDeleteSelectedId)
				return { CredentialTableAction::DeleteSelected, context.selectedRow };
			return {};
		}

		std::wstring GetInvokingAccount()
		{
			std::wstring username;
			std::wstring domain;
			if (!account_identity::GetCurrentUserInfo(username, domain))
				return localization::GetText(localization::TextId::ValueUnknown);
			return domain.empty() ? username : domain + L"\\" + username;
		}

		// Shows local account selection for credential creation without a fixed account name;
		// an empty successful selection stands for "Other account".
		bool SelectLocalAccountForCredentialPrompt(
			std::wstring& selectedAccount, HWND owner, bool requireAdministrator)
		{
			std::vector<std::wstring> accounts =
				local_account_enumeration::GetEnabledLocalAccounts();
			if (requireAdministrator)
			{
				std::erase_if(accounts, [](const std::wstring& account) {
					return !account_identity::IsUserInAdminGroup(account);
				});
			}
			std::wstring currentUsername;
			std::wstring currentDomain;
			std::wstring currentAccount;
			std::wstring currentAccountSid;
			if (account_identity::GetCurrentUserInfo(currentUsername, currentDomain))
			{
				currentAccount = currentDomain.empty()
					? currentUsername
					: currentDomain + L"\\" + currentUsername;
			}
			account_identity::GetCurrentUserSID(currentAccountSid);
			std::vector<std::wstring> displayAccounts;
			displayAccounts.reserve(accounts.size());
			for (const std::wstring& account : accounts)
				displayAccounts.push_back(DisplayAccount(account));
			std::vector<bool> storedAccounts(accounts.size(), false);
			size_t currentAccountIndex = static_cast<size_t>(-1);
			std::vector<CredentialInfo> storedCredentials;
			DWORD credentialListError = NO_ERROR;
			const bool credentialListAvailable =
				broker_credential_selection::GetAllBrokerCredentials(
					storedCredentials, credentialListError);
			for (size_t index = 0; index < accounts.size(); index++)
			{
				std::wstring canonicalName;
				std::wstring accountSid;
				if (!account_identity::ResolveAccountIdentity(
					accounts[index], canonicalName, accountSid))
				{
					continue;
				}
				if (!currentAccountSid.empty() && accountSid == currentAccountSid)
				{
					currentAccountIndex = index;
					displayAccounts[index] += localization::GetText(
						localization::TextId::AccountSelectionCurrent);
				}
				if (credentialListAvailable)
				{
					const auto credential = std::find_if(
						storedCredentials.begin(), storedCredentials.end(),
						[&accountSid](const CredentialInfo& credential) {
							return credential.accountSid == accountSid;
						});
					if (credential != storedCredentials.end())
					{
						storedAccounts[index] = true;
						displayAccounts[index] += localization::Format(
							localization::TextId::AccountSelectionStored,
							{ credential_formatting::FormatCredentialTimestamp(
								credential->lastWritten) });
					}
				}
			}

			return local_account_selection_dialog::Show(
				accounts, displayAccounts, storedAccounts,
				DisplayAccount(currentAccount), currentAccountIndex,
				credentialListAvailable && storedCredentials.empty(),
				selectedAccount, owner);
		}

		bool LaunchCredentialCaptureHelperWithRunAs(
			const broker::RequestNonce& challengeId, HANDLE& process, DWORD& error)
		{
			process = nullptr;
			std::vector<wchar_t> pathBuffer(512);
			DWORD pathLength = 0;
			for (;;)
			{
				SetLastError(NO_ERROR);
				pathLength = GetModuleFileNameW(nullptr, pathBuffer.data(),
					static_cast<DWORD>(pathBuffer.size()));
				if (pathLength == 0)
				{
					error = GetLastError();
					return false;
				}
				if (pathLength < pathBuffer.size() - 1)
					break;
				pathBuffer.resize(pathBuffer.size() * 2);
			}
			const std::filesystem::path helperPath =
				std::filesystem::path(std::wstring(pathBuffer.data(), pathLength))
					.parent_path() / L"xucRunApproval.exe";
			const std::wstring parameters =
				L"/secure-desktop-bootstrap /credentialchallenge:" +
				targets::BytesToUpperHex(challengeId);
			const std::wstring workingDirectory = helperPath.parent_path().wstring();
			SHELLEXECUTEINFOW executeInfo = { sizeof(executeInfo) };
			executeInfo.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
			executeInfo.lpVerb = L"runas";
			executeInfo.lpFile = helperPath.c_str();
			executeInfo.lpParameters = parameters.c_str();
			executeInfo.lpDirectory = workingDirectory.c_str();
			executeInfo.nShow = SW_SHOWNORMAL;
			if (!ShellExecuteExW(&executeInfo))
			{
				error = GetLastError();
				return false;
			}
			if (!executeInfo.hProcess)
			{
				error = ERROR_INVALID_HANDLE;
				return false;
			}
			process = executeInfo.hProcess;
			error = NO_ERROR;
			return true;
		}

		bool WaitForCredentialCaptureHelper(HANDLE process, DWORD& error)
		{
			if (!process)
			{
				error = ERROR_INVALID_HANDLE;
				return false;
			}
			const DWORD processId = GetProcessId(process);
			if (processId != 0)
				AllowSetForegroundWindow(processId);
			const DWORD wait = WaitForSingleObject(process,
				broker::ApprovalHelperProcessWaitTimeoutMilliseconds);
			if (wait != WAIT_OBJECT_0)
			{
				error = wait == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError();
				CloseHandle(process);
				return false;
			}
			DWORD exitCode = ERROR_GEN_FAILURE;
			const bool readExitCode = GetExitCodeProcess(process, &exitCode) == TRUE;
			CloseHandle(process);
			error = readExitCode ? exitCode : GetLastError();
			return readExitCode && exitCode == ERROR_SUCCESS;
		}

		// Deletes a broker credential using an already canonically resolved SID.
		int DeleteBrokerCredentialByIdentity(
			const std::wstring& canonicalUsername,
			const std::wstring& accountSid, const std::wstring& invokingAccount,
			HWND owner, bool showSuccess = true)
		{
			broker::CredentialDeleteRequest request;
			request.requestId = broker::GenerateBrokerRequestId();
			request.requestedAccountSid = accountSid;
			if (request.requestId == 0)
			{
				ui::ReportError(localization::GetText(
					localization::TextId::CredentialDeleteRequestFailed),
					L"Generating the broker credential deletion request identifier failed.",
					owner);
				return 1;
			}
			const broker::PipeCredentialDeleteResponse response =
				broker::SubmitCredentialDeleteRequest(request);
			if (response.status != broker::PipeCredentialDeleteStatus::Succeeded)
			{
				ui::ReportError(localization::Format(
					localization::TextId::CredentialBrokerUnreachable,
					{ windows_error::FormatWindowsError(response.windowsError) }),
					L"Submitting the broker credential deletion request failed. Windows error: " +
						std::to_wstring(response.windowsError), owner);
				return 1;
			}
			if (response.result.status == broker::CredentialDeleteStatus::NotFound)
			{
				ui::ReportError(localization::Format(
					localization::TextId::CredentialDeleteNotFound,
					{ DisplayAccount(invokingAccount),
						DisplayAccount(canonicalUsername) }),
					L"The broker credential selected for deletion was not found: " +
						canonicalUsername, owner);
				return 1;
			}
			if (response.result.status != broker::CredentialDeleteStatus::Succeeded)
			{
				ui::ReportError(localization::Format(
					localization::TextId::CredentialDeleteFailed,
					{ DisplayAccount(invokingAccount),
						DisplayAccount(canonicalUsername),
						windows_error::FormatWindowsError(response.result.windowsError) }),
					L"Deleting the broker credential failed. Target account: " +
						canonicalUsername + L"; Windows error: " +
						std::to_wstring(response.result.windowsError), owner);
				return 1;
			}

			client_logging::Write(
				L"Ok", L"Broker credential deleted: " + canonicalUsername);
			if (showSuccess)
			{
				ui::ShowInfo(localization::Format(
					localization::TextId::CredentialDeleteSucceeded,
					{ DisplayAccount(invokingAccount),
						DisplayAccount(canonicalUsername) }), owner);
			}
			return 0;
		}
	}

	// Selects the target identity and delegates secure credential capture to the elevated helper.
	bool PromptAndSaveBrokerCredentials(
		const std::wstring& prefilledUsername, HWND owner, bool requireAdministrator)
	{
		std::wstring selectedUsername = prefilledUsername;
		if (selectedUsername.empty() &&
			!SelectLocalAccountForCredentialPrompt(
				selectedUsername, owner, requireAdministrator))
		{
			client_logging::Write(L"Info", L"Local user account selection cancelled");
			return false;
		}

		std::wstring canonicalUsername;
		std::wstring accountSid;
		if (!account_identity::ResolveAccountIdentity(
			selectedUsername, canonicalUsername, accountSid))
		{
			ui::ReportError(localization::Format(
				localization::TextId::CredentialAccountUnresolved,
				{ DisplayAccount(selectedUsername) }),
				L"The selected credential account could not be resolved: " +
					selectedUsername, owner);
			return false;
		}
		if (requireAdministrator &&
			!account_identity::IsUserInAdminGroup(canonicalUsername))
		{
			ui::ReportError(localization::GetText(
				localization::TextId::ElevationAccountNotAdmin),
				L"The selected credential account is not an administrator: " +
					canonicalUsername, owner);
			return false;
		}
		const std::wstring currentAccount = GetInvokingAccount();
		CredentialInfo approvalCredential;
		DWORD approvalSelectionError = NO_ERROR;
		const auto approvalSelection = account_identity::IsRunningAsAdministrator()
			? broker_credential_selection::BrokerCredentialSelectionOutcome::NotFound
			: broker_credential_selection::SelectBrokerApprovalCredential(
				approvalCredential, approvalSelectionError);
		if (approvalSelection == broker_credential_selection::
			BrokerCredentialSelectionOutcome::Cancelled)
		{
			return false;
		}
		if (approvalSelection == broker_credential_selection::
			BrokerCredentialSelectionOutcome::Failed)
		{
			ui::ReportError(localization::Format(
				localization::TextId::BrokerCredentialsQueryFailed,
				{ windows_error::FormatWindowsError(approvalSelectionError) }),
				L"Selecting an administrator credential for credential capture failed. "
				L"Windows error: " + std::to_wstring(approvalSelectionError), owner);
			return false;
		}
		broker::CredentialCaptureChallengeRequest request;
		request.requestId = broker::GenerateBrokerRequestId();
		request.requestedAccountSid = accountSid;
		if (approvalSelection == broker_credential_selection::
			BrokerCredentialSelectionOutcome::Selected)
		{
			request.approvalAccountSid = approvalCredential.accountSid;
		}
		if (request.requestId == 0)
		{
			ui::ReportError(localization::GetText(
				localization::TextId::CredentialRequestIdFailed),
				L"Generating the credential capture request identifier failed.", owner);
			return false;
		}
		const broker::PipeCredentialCaptureChallengeResponse response =
			broker::SubmitCredentialCaptureChallengeRequest(request);
		if (response.status != broker::PipeCredentialSaveStatus::Succeeded)
		{
			ui::ReportError(localization::Format(
				localization::TextId::CredentialBrokerUnreachable,
				{ windows_error::FormatWindowsError(response.windowsError) }),
				L"Submitting the credential capture challenge failed. Windows error: " +
					std::to_wstring(response.windowsError), owner);
			return false;
		}
		if (response.result.status != broker::ApprovalChallengeStatus::Issued)
		{
			ui::ReportError(localization::GetText(
				localization::TextId::CredentialSaveFailed) + localization::Format(
				localization::TextId::CredentialErrorDetails,
				{ DisplayAccount(currentAccount),
					DisplayAccount(canonicalUsername),
					windows_error::FormatWindowsError(response.result.windowsError) }),
				L"The broker rejected the credential capture challenge. Status: " +
					std::to_wstring(static_cast<unsigned int>(response.result.status)) +
					L"; Windows error: " +
					std::to_wstring(response.result.windowsError), owner);
			return false;
		}

		HANDLE helperProcess = nullptr;
		DWORD helperError = NO_ERROR;
		if (response.result.helperStartMode ==
			broker::ApprovalHelperStartMode::BrokerStarted)
		{
			helperProcess = reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(
				response.result.approvalHelperProcessHandle));
		}
		else if (response.result.helperStartMode ==
			broker::ApprovalHelperStartMode::ClientRunAsRequired)
		{
			if (!LaunchCredentialCaptureHelperWithRunAs(
				response.result.challengeId, helperProcess, helperError))
			{
				if (helperError != ERROR_CANCELLED)
					ui::ReportError(windows_error::FormatWindowsError(helperError),
						L"Starting the credential capture helper failed. Windows error: " +
							std::to_wstring(helperError), owner);
				return false;
			}
		}
		else
		{
			ui::ReportError(localization::GetText(
				localization::TextId::CredentialSaveFailed),
				L"The broker returned an invalid credential capture helper start mode.",
				owner);
			return false;
		}
		if (!WaitForCredentialCaptureHelper(helperProcess, helperError))
		{
			if (helperError != ERROR_CANCELLED)
			{
				std::wstring message = helperError == ERROR_LOGON_FAILURE
					? localization::GetText(localization::TextId::CredentialSaveInvalid)
					: localization::GetText(localization::TextId::CredentialSaveFailed);
				message += localization::Format(
					localization::TextId::CredentialErrorDetails,
					{ DisplayAccount(currentAccount),
						DisplayAccount(canonicalUsername),
						windows_error::FormatWindowsError(helperError) });
				ui::ReportError(message,
					L"The credential capture helper failed. Target account: " +
						canonicalUsername + L"; Windows error: " +
						std::to_wstring(helperError), owner);
			}
			return false;
		}

		const std::wstring adminStatus = localization::GetText(
			account_identity::IsUserInAdminGroup(canonicalUsername)
				? localization::TextId::CredentialUserIsAdmin
				: localization::TextId::CredentialUserIsNotAdmin);
		const std::wstring message = localization::Format(
			localization::TextId::CredentialSaveSucceeded,
			{ DisplayAccount(currentAccount),
				DisplayAccount(canonicalUsername), adminStatus });
		client_logging::Write(
			L"Ok", L"Broker credential stored successfully for target account: " +
				canonicalUsername);
		ui::ShowInfo(message, owner);
		return true;
	}

	// Resolves an account identity and deletes its broker credential only after confirmation.
	int DeleteBrokerCredential(const std::wstring& accountName, HWND owner)
	{
		std::wstring canonicalUsername;
		std::wstring accountSid;
		if (!account_identity::ResolveAccountIdentity(
			accountName, canonicalUsername, accountSid))
		{
			ui::ReportError(localization::Format(
				localization::TextId::CredentialAccountUnresolved,
				{ DisplayAccount(accountName) }),
				L"The credential account requested for deletion could not be resolved: " +
					accountName, owner);
			return 1;
		}

		const std::wstring confirmation = localization::Format(
			localization::TextId::CredentialDeleteConfirmation,
			{ DisplayAccount(GetInvokingAccount()),
				DisplayAccount(canonicalUsername) });
		if (ui::ShowMessageBox(owner ? owner : ui::GetDialogOwnerWindow(), confirmation,
			ui::GetWindowTitle(localization::GetText(
				localization::TextId::CredentialDeleteTitle)),
			MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2 |
			MB_SETFOREGROUND | MB_TOPMOST) != IDYES)
		{
			client_logging::Write(
				L"Info", L"Broker credential deletion not confirmed: " + canonicalUsername);
			return 0;
		}
		return DeleteBrokerCredentialByIdentity(
			canonicalUsername, accountSid, GetInvokingAccount(), owner);
	}

	// Selects a stored broker credential when needed and deletes it after confirmation.
	int DeleteStoredCredential(const std::wstring& specificUser)
	{
		while (true)
		{
			std::vector<CredentialInfo> credentials;
			DWORD credentialListError = NO_ERROR;
			if (!broker_credential_selection::GetAllBrokerCredentials(
				credentials, credentialListError))
			{
				ui::ReportError(localization::Format(
					localization::TextId::CredentialListFailed,
					{ windows_error::FormatWindowsError(credentialListError) }),
					L"Retrieving broker credentials for deletion failed. Windows error: " +
						std::to_wstring(credentialListError));
				return 1;
			}
			if (credentials.empty())
			{
				ui::ShowInfo(localization::GetText(
					localization::TextId::CredentialNoneStored));
				return 0;
			}

			int selectedIndex = -1;
			if (!specificUser.empty())
			{
				std::wstring canonicalName;
				std::wstring requestedSid;
				if (!account_identity::ResolveAccountIdentity(
					specificUser, canonicalName, requestedSid))
				{
					ui::ReportError(localization::Format(
						localization::TextId::CredentialUserUnresolved,
						{ DisplayAccount(specificUser) }),
						L"The requested credential account could not be resolved: " +
							specificUser);
					return 1;
				}

				for (size_t index = 0; index < credentials.size(); index++)
				{
					if (!credentials[index].accountSid.empty() &&
						credentials[index].accountSid == requestedSid)
					{
						selectedIndex = static_cast<int>(index);
						break;
					}
				}

				if (selectedIndex == -1)
				{
					ui::ReportError(localization::Format(
						localization::TextId::CredentialUserNotStored,
						{ DisplayAccount(specificUser) }),
						L"No broker credential is stored for the requested account: " +
							specificUser);
					return 1;
				}
			}
			else
			{
				selectedIndex = credential_selection_dialog::ShowCredentialSelectionDialog(
					credentials, credential_formatting::CredentialSelectionMode::Delete);
				if (selectedIndex == -1)
				{
					client_logging::Write(
						L"Info", L"Credential deletion cancelled by user");
					return 0;
				}
			}

			const CredentialInfo& credential = credentials[selectedIndex];
			const std::wstring invokingAccount = GetInvokingAccount();
			const std::wstring confirmation = localization::Format(
				localization::TextId::CredentialDeleteConfirmation,
				{ DisplayAccount(invokingAccount),
					DisplayAccount(credential.username) });
			int answer = ui::ShowMessageBox(
				ui::GetDialogOwnerWindow(),
				confirmation,
				ui::GetWindowTitle(localization::GetText(
					localization::TextId::CredentialSelectionDeleteTitle)),
				MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2 |
				MB_SETFOREGROUND | MB_TOPMOST);
			if (answer != IDYES)
			{
				client_logging::Write(
					L"Info", L"Broker credential deletion not confirmed: " +
					credential.username);
				return 0;
			}

			if (DeleteBrokerCredentialByIdentity(
				credential.username, credential.accountSid, invokingAccount,
				ui::GetDialogOwnerWindow()) != 0)
			{
				return 1;
			}
			if (!specificUser.empty())
				return 0;
			std::vector<CredentialInfo> remainingCredentials;
			DWORD remainingCredentialsError = NO_ERROR;
			if (!broker_credential_selection::GetAllBrokerCredentials(
				remainingCredentials, remainingCredentialsError))
			{
				ui::ReportError(localization::Format(
					localization::TextId::CredentialRemainingListFailed,
					{ windows_error::FormatWindowsError(remainingCredentialsError) }),
					L"Retrieving remaining broker credentials after deletion failed. "
					L"Windows error: " + std::to_wstring(remainingCredentialsError));
				return 1;
			}
			if (remainingCredentials.empty())
				return 0;
			client_logging::Write(
				L"Info", L"Reopening credential selection after successful deletion");
		}
	}

	int ShowBrokerCredentials(const std::wstring& programExecutableName)
	{
		std::wstring currentUser;
		std::wstring currentDomain;
		if (!account_identity::GetCurrentUserInfo(currentUser, currentDomain))
		{
			ui::ReportError(localization::GetText(
				localization::TextId::CurrentUserQueryFailed),
				L"Determining the current account for the credential view failed.");
			return 1;
		}
		const std::wstring fullCurrentUser = currentDomain.empty()
			? currentUser : currentDomain + L"\\" + currentUser;

		for (;;)
		{
		std::vector<CredentialInfo> credentials;
		DWORD credentialListError = NO_ERROR;
		if (!broker_credential_selection::GetAllBrokerCredentials(
			credentials, credentialListError))
		{
			ui::ReportError(localization::Format(
				localization::TextId::BrokerCredentialsQueryFailed,
				{ windows_error::FormatWindowsError(credentialListError) }),
				L"Retrieving broker credentials for the credential view failed. Windows error: " +
					std::to_wstring(credentialListError));
			return 1;
		}
		std::vector<std::size_t> approvalCounts(credentials.size(), 0);
		if (!credentials.empty())
		{
			broker::ApprovalListRequest approvalRequest;
			approvalRequest.requestId = broker::GenerateBrokerRequestId();
			const auto approvalResponse =
				broker::SubmitApprovalListRequest(approvalRequest);
			if (approvalResponse.status !=
				broker::PipeApprovalManagementStatus::Succeeded ||
				approvalResponse.result.status !=
				broker::ApprovalManagementStatus::Succeeded)
			{
				const DWORD approvalError = approvalResponse.status ==
					broker::PipeApprovalManagementStatus::Succeeded
						? approvalResponse.result.windowsError
						: approvalResponse.windowsError;
				ui::ReportError(localization::Format(
					localization::TextId::BrokerApprovalsQueryFailed,
					{ windows_error::FormatWindowsError(approvalError) }),
					L"Retrieving approval counts for the credential view failed. Windows error: " +
						std::to_wstring(approvalError));
				return 1;
			}
			for (std::size_t credentialIndex = 0;
				credentialIndex < credentials.size(); credentialIndex++)
			{
				approvalCounts[credentialIndex] = std::count_if(
					approvalResponse.result.entries.begin(),
					approvalResponse.result.entries.end(),
					[&credential = credentials[credentialIndex]](const auto& approvalValue) {
						return approvalValue.accountScope ==
							approval::BrokerApprovalAccountScope::AllAccounts ||
							_wcsicmp(approvalValue.targetAccountSid.c_str(),
								credential.accountSid.c_str()) == 0;
					});
			}
		}

		std::wstring output = localization::Format(
			localization::TextId::ShowCredentialsIntro,
			{ DisplayAccount(fullCurrentUser) });
		if (credentials.empty())
		{
			output += localization::Format(
				localization::TextId::ShowCredentialsEmpty,
				{ programExecutableName });
		}
		else
		{
			output += localization::Format(
				localization::TextId::ShowCredentialsFound,
				{ std::to_wstring(credentials.size()) });
			output += localization::GetText(
				localization::TextId::ShowCredentialsApproval);
			for (size_t index = 0; index < credentials.size(); index++)
			{
				const CredentialInfo& credential = credentials[index];
				output += localization::Format(
					localization::TextId::ShowCredentialsEntry,
					{ std::to_wstring(index + 1),
						DisplayAccount(credential.username) });
				output += localization::Format(
					localization::TextId::ShowCredentialsStored,
					{ credential_formatting::FormatCredentialTimestamp(
						credential.lastWritten) });
				output += localization::GetText(
					account_identity::IsUserInAdminGroup(credential.username)
						? localization::TextId::ShowCredentialsAdmin
						: localization::TextId::ShowCredentialsNotAdmin);
				output += localization::Format(
					localization::TextId::ShowCredentialsApprovals,
					{ std::to_wstring(approvalCounts[index]) });
				if (index + 1 < credentials.size())
					output += L"\n";
			}
			output += localization::GetText(
				localization::TextId::ShowCredentialsNote);
		}

		const std::wstring title = ui::GetWindowTitle(localization::GetText(
			localization::TextId::ShowCredentialsTitle));
		const std::wstring heading = localization::GetText(
			localization::TextId::ShowCredentialsHeading);
		if (credentials.empty())
		{
			text_dialog::Show(title, heading, output, ui::GetDialogOwnerWindow());
			return 0;
		}
		else if (console_output::WriteToAvailableTarget(heading, output) !=
			console_output::TextOutputResult::Complete)
		{
			const std::wstring summary = TrimLineBreaks(localization::Format(
				localization::TextId::ShowCredentialsIntro,
				{ DisplayAccount(fullCurrentUser) })) + L"\n" +
				TrimLineBreaks(localization::Format(
					localization::TextId::ShowCredentialsFound,
					{ std::to_wstring(credentials.size()) })) + L"\n" +
				TrimLineBreaks(localization::GetText(
					localization::TextId::ShowCredentialsApproval));
			const CredentialTableResult dialogResult = ShowCredentialTable(title, summary,
				TrimLineBreaks(localization::GetText(
					localization::TextId::ShowCredentialsNote)),
				credentials, approvalCounts, ui::GetDialogOwnerWindow());
			if (dialogResult.action == CredentialTableAction::Closed)
				return 0;

			const std::wstring invokingAccount = GetInvokingAccount();
			if (dialogResult.action == CredentialTableAction::DeleteAll)
			{
				const std::wstring confirmation = localization::Format(
					localization::TextId::ShowCredentialsDeleteAllConfirm,
					{ std::to_wstring(credentials.size()) });
				if (ui::ShowMessageBox(ui::GetDialogOwnerWindow(), confirmation,
					ui::GetWindowTitle(localization::GetText(
						localization::TextId::WarningTitle)),
					MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2 |
					MB_SETFOREGROUND | MB_TOPMOST) != IDYES)
				{
					continue;
				}
				for (const CredentialInfo& credential : credentials)
				{
					if (DeleteBrokerCredentialByIdentity(
						credential.username, credential.accountSid,
						invokingAccount, ui::GetDialogOwnerWindow(), false) != 0)
					{
						return 1;
					}
				}
				continue;
			}

			if (dialogResult.selectedRow < 0 ||
				static_cast<std::size_t>(dialogResult.selectedRow) >= credentials.size())
			{
				continue;
			}
			const CredentialInfo& credential =
				credentials[static_cast<std::size_t>(dialogResult.selectedRow)];
			const std::wstring confirmation = localization::Format(
				localization::TextId::CredentialDeleteConfirmation,
				{ DisplayAccount(invokingAccount),
					DisplayAccount(credential.username) });
			if (ui::ShowMessageBox(ui::GetDialogOwnerWindow(), confirmation,
				ui::GetWindowTitle(localization::GetText(
					localization::TextId::CredentialDeleteTitle)),
				MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2 |
				MB_SETFOREGROUND | MB_TOPMOST) != IDYES)
			{
				continue;
			}
			if (DeleteBrokerCredentialByIdentity(
				credential.username, credential.accountSid, invokingAccount,
				ui::GetDialogOwnerWindow(), false) != 0)
			{
				return 1;
			}
			continue;
		}
		return 0;
		}
	}
}