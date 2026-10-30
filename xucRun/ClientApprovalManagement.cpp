// Purpose: Implements displaying and resetting stored broker approvals.
// Approach: Implements the behavior with focused helpers built on shared xucRun interfaces and Windows APIs.
// Behavior: Queries approvals and credential metadata from the broker and shows client dialogs.
#include "ClientApprovalManagement.h"

#include <Windows.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "ApprovalSerialization.h"
#include "BrokerContracts.h"
#include "BrokerPipeClient.h"
#include "ClientAccountIdentity.h"
#include "ClientApprovalSelectionDialog.h"
#include "ClientBrokerCredentialSelection.h"
#include "ClientConsoleOutput.h"
#include "ClientCredentialFormatting.h"
#include "ClientCredentialInfo.h"
#include "ClientCredentialSelectionDialog.h"
#include "ClientLogging.h"
#include "ClientTableDialog.h"
#include "ClientTextDialog.h"
#include "ClientUiCommon.h"
#include "ClientWindowsError.h"
#include "DocumentationNameMapping.h"
#include "Localization.h"
#include "TargetNaming.h"

namespace xucrun::approval_management
{
	namespace
	{
		std::wstring DisplayAccount(const std::wstring& accountName)
		{
			return documentation_names::DisplayAccount(accountName);
		}
	}

	int ResetBrokerApprovals(const std::wstring& specificUser)
	{
		std::wstring currentUser;
		std::wstring currentDomain;
		const std::wstring invokingAccount =
			account_identity::GetCurrentUserInfo(currentUser, currentDomain)
			? (currentDomain.empty() ? currentUser : currentDomain + L"\\" + currentUser)
			: localization::GetText(localization::TextId::ValueUnknown);
		std::vector<CredentialInfo> credentials;
		DWORD credentialError = NO_ERROR;
		if (!broker_credential_selection::GetAllBrokerCredentials(credentials, credentialError))
		{
			ui::ReportError(localization::Format(
				localization::TextId::CredentialListFailed,
				{ windows_error::FormatWindowsError(credentialError) }),
				L"Retrieving broker credentials for approval management failed. Windows error: " +
					std::to_wstring(credentialError));
			return 1;
		}
		if (credentials.empty())
		{
			ui::ShowInfo(localization::GetText(
				localization::TextId::CredentialNoneStored));
			return 0;
		}

		while (true)
		{
			broker::ApprovalListRequest listRequest;
			listRequest.requestId = broker::GenerateBrokerRequestId();
			const auto listResponse = broker::SubmitApprovalListRequest(listRequest);
			if (listResponse.status != broker::PipeApprovalManagementStatus::Succeeded ||
				listResponse.result.status != broker::ApprovalManagementStatus::Succeeded)
			{
				const DWORD listError = listResponse.status ==
					broker::PipeApprovalManagementStatus::Succeeded
					? listResponse.result.windowsError : listResponse.windowsError;
				ui::ReportError(localization::Format(
					localization::TextId::BrokerApprovalListFailed,
					{ windows_error::FormatWindowsError(listError) }),
					L"Retrieving broker approvals for reset failed. Windows error: " +
						std::to_wstring(listError));
				return 1;
			}

			std::vector<size_t> approvalCounts(credentials.size(), 0);
			for (size_t credentialIndex = 0; credentialIndex < credentials.size(); credentialIndex++)
			{
				approvalCounts[credentialIndex] = std::count_if(
					listResponse.result.entries.begin(), listResponse.result.entries.end(),
					[&credential = credentials[credentialIndex]](const auto& approvalValue) {
						return approvalValue.accountScope ==
							approval::BrokerApprovalAccountScope::AllAccounts ||
							_wcsicmp(approvalValue.targetAccountSid.c_str(),
								credential.accountSid.c_str()) == 0;
					});
			}

			int selectedCredentialIndex = -1;
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
						L"The requested approval account could not be resolved: " +
							specificUser);
					return 1;
				}
				const auto found = std::find_if(credentials.begin(), credentials.end(),
					[&requestedSid](const CredentialInfo& credential) {
						return _wcsicmp(credential.accountSid.c_str(), requestedSid.c_str()) == 0;
					});
				if (found == credentials.end())
				{
					ui::ReportError(localization::Format(
						localization::TextId::BrokerCredentialUserNotFound,
						{ DisplayAccount(specificUser) }),
						L"No stored broker credential exists for the requested approval account: " +
							specificUser);
					return 1;
				}
				selectedCredentialIndex = static_cast<int>(
					std::distance(credentials.begin(), found));
			}
			else
			{
				selectedCredentialIndex =
					credential_selection_dialog::ShowCredentialSelectionDialog(
						credentials, credential_formatting::CredentialSelectionMode::ResetApproval,
						&approvalCounts);
				if (selectedCredentialIndex < 0)
					return 0;
			}

			const CredentialInfo& credential =
				credentials[static_cast<size_t>(selectedCredentialIndex)];
			std::vector<approval::BrokerApprovalValue> matchingApprovals;
			std::vector<approval_selection_dialog::ResetApprovalOption> options;
			for (const auto& approvalValue : listResponse.result.entries)
			{
				if (approvalValue.accountScope !=
					approval::BrokerApprovalAccountScope::AllAccounts &&
					_wcsicmp(approvalValue.targetAccountSid.c_str(),
						credential.accountSid.c_str()) != 0)
				{
					continue;
				}
				approval_selection_dialog::ResetApprovalOption option;
				option.general = approvalValue.programScope ==
					approval::BrokerApprovalProgramScope::General;
				option.allAccounts = approvalValue.accountScope ==
					approval::BrokerApprovalAccountScope::AllAccounts;
				option.displayText =
					credential_formatting::FormatBrokerApprovalForReset(approvalValue);
				options.push_back(std::move(option));
				matchingApprovals.push_back(approvalValue);
			}
			if (options.empty())
			{
				ui::ShowInfo(localization::Format(
					localization::TextId::BrokerApprovalNone,
					{ DisplayAccount(invokingAccount),
						DisplayAccount(credential.username) }));
				return 0;
			}

			bool cancelled = false;
			const std::vector<int> selectedApprovals =
				approval_selection_dialog::ShowResetApprovalSelectionDialog(
					invokingAccount, credential.username, options,
					specificUser.empty(), cancelled);
			if (selectedApprovals.empty())
			{
				if (cancelled && specificUser.empty())
					continue;
				return 0;
			}

			std::uint32_t deletedCount = 0;
			for (const int approvalIndex : selectedApprovals)
			{
				if (approvalIndex < 0 ||
					static_cast<size_t>(approvalIndex) >= matchingApprovals.size())
				{
					continue;
				}
				broker::ApprovalResetRequest resetRequest;
				resetRequest.requestId = broker::GenerateBrokerRequestId();
				resetRequest.approval = matchingApprovals[static_cast<size_t>(approvalIndex)];
				const auto resetResponse = broker::SubmitApprovalResetRequest(resetRequest);
				if (resetResponse.status != broker::PipeApprovalManagementStatus::Succeeded ||
					resetResponse.result.status != broker::ApprovalManagementStatus::Succeeded)
				{
					const DWORD resetError = resetResponse.status ==
						broker::PipeApprovalManagementStatus::Succeeded
						? resetResponse.result.windowsError
						: resetResponse.windowsError;
					ui::ReportError(localization::Format(
						localization::TextId::BrokerApprovalDeleteFailed,
						{ std::to_wstring(deletedCount),
							DisplayAccount(invokingAccount),
							DisplayAccount(credential.username),
							windows_error::FormatWindowsError(resetError) }),
						L"Resetting a broker approval failed after " +
							std::to_wstring(deletedCount) +
							L" successful deletions. Windows error: " +
							std::to_wstring(resetError));
					return 1;
				}
				deletedCount += resetResponse.result.deletedCount;
			}
			ui::ReportInfo(localization::Format(
				localization::TextId::BrokerApprovalDeleteSucceeded,
				{ std::to_wstring(deletedCount),
					DisplayAccount(invokingAccount),
					DisplayAccount(credential.username) }),
				L"Broker approvals reset successfully. Deleted count: " +
					std::to_wstring(deletedCount) + L"; target account: " +
					credential.username);
			if (!specificUser.empty())
				return 0;
			client_logging::Write(
				L"Info", L"Reopening credential selection after successful approval reset");
		}
	}

	int ShowBrokerApprovals()
	{
		std::wstring currentUser;
		std::wstring currentDomain;
		if (!account_identity::GetCurrentUserInfo(currentUser, currentDomain))
		{
			ui::ReportError(localization::GetText(
				localization::TextId::CurrentUserQueryFailed),
				L"Determining the current account for the approval view failed.");
			return 1;
		}
		const std::wstring fullCurrentUser = currentDomain.empty()
			? currentUser : currentDomain + L"\\" + currentUser;
		for (;;)
		{
		broker::ApprovalListRequest request;
		request.requestId = broker::GenerateBrokerRequestId();
		const auto response = broker::SubmitApprovalListRequest(request);
		if (response.status != broker::PipeApprovalManagementStatus::Succeeded ||
			response.result.status != broker::ApprovalManagementStatus::Succeeded)
		{
			const DWORD approvalError = response.status ==
				broker::PipeApprovalManagementStatus::Succeeded
				? response.result.windowsError : response.windowsError;
			ui::ReportError(localization::Format(
				localization::TextId::BrokerApprovalsQueryFailed,
				{ windows_error::FormatWindowsError(approvalError) }),
				L"Retrieving broker approvals failed. Windows error: " +
					std::to_wstring(approvalError));
			return 1;
		}
		std::vector<CredentialInfo> brokerCredentials;
		DWORD credentialListError = NO_ERROR;
		if (!broker_credential_selection::GetAllBrokerCredentials(
			brokerCredentials, credentialListError))
		{
			ui::ReportError(localization::Format(
				localization::TextId::BrokerCredentialsQueryFailed,
				{ windows_error::FormatWindowsError(credentialListError) }),
				L"Retrieving broker credentials for the approval view failed. Windows error: " +
					std::to_wstring(credentialListError));
			return 1;
		}
		std::wstring output = localization::Format(
			localization::TextId::ShowApprovalsIntro,
			{ DisplayAccount(fullCurrentUser) });
		if (response.result.entries.empty())
		{
			output += localization::GetText(localization::TextId::ShowApprovalsEmpty);
		}
		else
		{
			for (size_t index = 0; index < response.result.entries.size(); index++)
			{
				const auto& approvalValue = response.result.entries[index];
				output += localization::Format(
					localization::TextId::ShowApprovalsEntry,
					{ std::to_wstring(index + 1) });
				if (approvalValue.accountScope ==
					approval::BrokerApprovalAccountScope::AllAccounts)
				{
					output += localization::GetText(
						localization::TextId::ShowApprovalsAllAccounts);
				}
				else
				{
					const auto credential = std::find_if(
						brokerCredentials.begin(), brokerCredentials.end(),
						[&approvalValue](const CredentialInfo& candidate) {
							return _wcsicmp(candidate.accountSid.c_str(),
								approvalValue.targetAccountSid.c_str()) == 0;
						});
					output += localization::Format(
						localization::TextId::ShowApprovalsAccount,
						{ credential == brokerCredentials.end()
							? localization::GetText(
								localization::TextId::ValueNotAvailable)
							: DisplayAccount(credential->username) });
				}
				const bool general = approvalValue.programScope ==
					approval::BrokerApprovalProgramScope::General;
				const bool exact = approvalValue.programScope ==
					approval::BrokerApprovalProgramScope::ExactInvocation;
				output += localization::Format(
					localization::TextId::ShowApprovalsScope,
					{ credential_formatting::FormatBrokerApprovalScope(approvalValue) });
				if (!general)
				{
					output += localization::Format(
						localization::TextId::ShowApprovalsTarget,
						{ approvalValue.targetPath });
				}
				if (exact)
				{
					output += localization::Format(
						localization::TextId::ShowApprovalsInvocationBinding,
						{ L"        " +
							credential_formatting::FormatExactInvocationBinding(
								approvalValue, L"\n        ") });
				}
				if (approvalValue.lifetime == approval::BrokerApprovalLifetime::Forever)
					output += localization::GetText(
						localization::TextId::ShowApprovalsLifetimeForever);
				else if (approvalValue.lifetime ==
					approval::BrokerApprovalLifetime::LogonSession)
				{
					output += localization::GetText(
						localization::TextId::ShowApprovalsLifetimeSession);
				}
				else
				{
					output += localization::Format(
						localization::TextId::ShowApprovalsLifetimeUntil,
						{ credential_formatting::FormatCredentialTimestamp(
							approvalValue.expiresAt) });
				}
				output += localization::GetText(approvalValue.elevationMode == 0
					? localization::TextId::ShowApprovalsTokenExisting
					: localization::TextId::ShowApprovalsTokenElevated);
				if (!general)
				{
					output += localization::Format(
						localization::TextId::ShowApprovalsTargetHash,
						{ targets::BytesToUpperHex(approvalValue.targetHash) });
				}
				if (index + 1 < response.result.entries.size())
					output += L"\n";
			}
		}

		const std::wstring title = localization::GetText(
			localization::TextId::ShowApprovalsTitle);
		const std::wstring heading = localization::GetText(
			localization::TextId::ShowApprovalsHeading);
		if (response.result.entries.empty())
		{
			text_dialog::Show(
				ui::GetWindowTitle(title), heading, output, ui::GetDialogOwnerWindow());
			return 0;
		}
		else if (console_output::WriteToAvailableTarget(heading, output) !=
			console_output::TextOutputResult::Complete)
		{
			const std::vector<table_dialog::Column> columns = {
				{ localization::GetText(
					localization::TextId::ShowCredentialsColumnAccount), 140 },
				{ localization::GetText(
					localization::TextId::ShowApprovalsColumnScope), 150 },
				{ localization::GetText(
					localization::TextId::ShowApprovalsColumnTarget), 260 },
				{ localization::GetText(
					localization::TextId::ShowApprovalsColumnInvocationBinding), 320 },
				{ localization::GetText(
					localization::TextId::ShowApprovalsColumnLifetime), 130 },
				{ localization::GetText(
					localization::TextId::ShowApprovalsColumnPrivilegeMode), 110 },
				{ localization::GetText(
					localization::TextId::ShowApprovalsColumnTargetHash), 180, true }
			};
			std::vector<table_dialog::Row> rows;
			rows.reserve(response.result.entries.size());
			for (const auto& approvalValue : response.result.entries)
			{
				const bool allAccounts = approvalValue.accountScope ==
					approval::BrokerApprovalAccountScope::AllAccounts;
				const auto credential = std::find_if(
					brokerCredentials.begin(), brokerCredentials.end(),
					[&approvalValue](const CredentialInfo& candidate) {
						return _wcsicmp(candidate.accountSid.c_str(),
							approvalValue.targetAccountSid.c_str()) == 0;
					});
				const bool general = approvalValue.programScope ==
					approval::BrokerApprovalProgramScope::General;
				const bool exact = approvalValue.programScope ==
					approval::BrokerApprovalProgramScope::ExactInvocation;
				std::wstring lifetime;
				if (approvalValue.lifetime == approval::BrokerApprovalLifetime::Forever)
				{
					lifetime = localization::GetText(
						localization::TextId::ShowApprovalsLifetimeForeverValue);
				}
				else if (approvalValue.lifetime ==
					approval::BrokerApprovalLifetime::LogonSession)
				{
					lifetime = localization::GetText(
						localization::TextId::ShowApprovalsLifetimeSessionValue);
				}
				else
				{
					lifetime = credential_formatting::FormatCredentialTimestamp(
						approvalValue.expiresAt);
				}
				rows.push_back({
					allAccounts
						? localization::GetText(
							localization::TextId::ShowApprovalsAllAccountsValue)
						: (credential == brokerCredentials.end()
							? localization::GetText(localization::TextId::ValueNotAvailable)
							: DisplayAccount(credential->username)),
					credential_formatting::FormatBrokerApprovalScope(approvalValue),
					general ? L"" : approvalValue.targetPath,
					exact
						? credential_formatting::FormatExactInvocationBinding(
							approvalValue, L"\n")
						: L"",
					std::move(lifetime),
					localization::GetText(approvalValue.elevationMode == 0
						? localization::TextId::ShowApprovalsPrivilegeUnchangedValue
						: localization::TextId::ShowApprovalsPrivilegeElevatedValue),
					general ? L"" : targets::BytesToUpperHex(approvalValue.targetHash)
				});
			}
			const auto dialogResult = table_dialog::Show(ui::GetWindowTitle(title),
				localization::Format(localization::TextId::ShowApprovalsIntro,
					{ DisplayAccount(fullCurrentUser) }), L"", columns, rows,
				ui::GetDialogOwnerWindow(),
				localization::GetText(
					localization::TextId::ShowApprovalsDeleteAll),
				L"",
				localization::GetText(
					localization::TextId::ShowApprovalsDeleteSelected));
			if (dialogResult.action == table_dialog::TableDialogAction::Closed)
				return 0;

			std::vector<std::size_t> selectedEntries;
			if (dialogResult.action == table_dialog::TableDialogAction::Action)
			{
				const std::wstring confirmation = localization::Format(
					localization::TextId::ShowApprovalsDeleteAllConfirm,
					{ std::to_wstring(response.result.entries.size()) });
				if (ui::ShowMessageBox(ui::GetDialogOwnerWindow(), confirmation,
					ui::GetWindowTitle(localization::GetText(
						localization::TextId::WarningTitle)),
					MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
				{
					continue;
				}
				selectedEntries.resize(response.result.entries.size());
				for (std::size_t index = 0; index < selectedEntries.size(); index++)
					selectedEntries[index] = index;
			}
			else
			{
				if (dialogResult.selectedRow < 0 ||
					static_cast<std::size_t>(dialogResult.selectedRow) >=
						response.result.entries.size())
				{
					continue;
				}
				const std::size_t selected =
					static_cast<std::size_t>(dialogResult.selectedRow);
				const auto& row = rows[selected];
				const std::wstring confirmation = localization::Format(
					localization::TextId::ShowApprovalsDeleteSelectedConfirm,
					{ row[0], row[1], row[2].empty()
						? localization::GetText(
							localization::TextId::ValueNotAvailable)
						: row[2] });
				if (ui::ShowMessageBox(ui::GetDialogOwnerWindow(), confirmation,
					ui::GetWindowTitle(localization::GetText(
						localization::TextId::WarningTitle)),
					MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
				{
					continue;
				}
				selectedEntries.push_back(selected);
			}

			bool deletionFailed = false;
			for (const std::size_t selected : selectedEntries)
			{
				broker::ApprovalResetRequest resetRequest;
				resetRequest.requestId = broker::GenerateBrokerRequestId();
				resetRequest.approval = response.result.entries[selected];
				const auto resetResponse =
					broker::SubmitApprovalResetRequest(resetRequest);
				if (resetResponse.status !=
						broker::PipeApprovalManagementStatus::Succeeded ||
					resetResponse.result.status !=
						broker::ApprovalManagementStatus::Succeeded)
				{
					const DWORD resetError = resetResponse.status ==
						broker::PipeApprovalManagementStatus::Succeeded
						? resetResponse.result.windowsError
						: resetResponse.windowsError;
					ui::ReportError(localization::Format(
						localization::TextId::ShowApprovalsDeleteFailed,
						{ windows_error::FormatWindowsError(resetError) }),
						L"Deleting an approval from the approval view failed. "
						L"Windows error: " + std::to_wstring(resetError));
					deletionFailed = true;
					break;
				}
			}
			if (deletionFailed)
				return 1;
			continue;
		}
		return 0;
		}
	}
}