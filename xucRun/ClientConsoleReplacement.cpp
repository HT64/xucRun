// Purpose: Implements console replacement for invocations without a target program.
// Approach: Implements the behavior with focused helpers built on shared xucRun interfaces and Windows APIs.
// Behavior: Chooses current token or broker credentials and replaces the parent shell.
#include "ClientConsoleReplacement.h"

#include <Windows.h>

#include <string>

#include "ClientAccountIdentity.h"
#include "ClientBrokerCredentialManagement.h"
#include "ClientBrokerCredentialSelection.h"
#include "ClientBrokerLaunch.h"
#include "ClientConsoleSession.h"
#include "ClientCredentialInfo.h"
#include "ClientLogging.h"
#include "ClientSameUserLaunch.h"
#include "ClientUiCommon.h"
#include "ClientWindowsError.h"
#include "Localization.h"

namespace xucrun::console_replacement
{
	ConsoleReplacementResult Run(const ConsoleReplacementRequest& request)
	{
		const console_session::ParentShell parentShell =
			console_session::DetectParentShell();
		client_logging::Write(L"Info", L"Parent shell for console replacement: " +
			parentShell.description);

		// Console replacement requires a known shell type; generic
		// text output detects consoles only by their handles.
		if (parentShell.type == process_context::ParentShellType::Unknown)
		{
			return { ConsoleReplacementCompletion::UnknownParent, 0, true };
		}

		std::wstring consolePath =
			console_session::GetExecutablePath(parentShell.type);
		if (request.onlyUserParameter)
		{
			client_logging::Write(L"Info", request.requestElevation
				? L"/user-elevated parameter - launching elevated console of type: " + parentShell.description + L" with user: " + request.username
				: L"/user parameter - launching console without requested elevation of type: " + parentShell.description + L" with user: " + request.username);
		}
		else
		{
			client_logging::Write(L"Info", L"No parameters - determining automatic launch strategy for parent shell: " + parentShell.description);
		}

		std::wstring currentUser, currentDomain;
		if (!account_identity::GetCurrentUserInfo(currentUser, currentDomain))
		{
			ui::ReportError(localization::GetText(
				localization::TextId::CurrentUserQueryFailed),
				L"Determining the current account for console replacement failed.");
			return { ConsoleReplacementCompletion::Failed, 1, false };
		}

		bool useBrokerCredential = false;
		same_user_launch::SameUserLaunchMode sameUserLaunchMode =
			same_user_launch::SameUserLaunchMode::Credentials;
		CredentialInfo selectedCredential;
		broker::ElevationMode brokerElevationMode =
			broker::ElevationMode::PreserveToken;
		DWORD selectionError = NO_ERROR;
		broker_credential_selection::BrokerCredentialSelectionOutcome selection =
			broker_credential_selection::BrokerCredentialSelectionOutcome::NotFound;

		if (!request.username.empty())
		{
			sameUserLaunchMode = same_user_launch::GetSameUserLaunchMode(
				request.username, request.requestElevation);

			if (sameUserLaunchMode != same_user_launch::SameUserLaunchMode::Credentials)
			{
				client_logging::Write(L"Info", L"User switch refers to current account - launching console without credentials: " + request.username);
			}
			else
			{
				client_logging::Write(L"Info", L"Explicit user switch -> searching broker credential for: " + request.username);
				selection = broker_credential_selection::SelectBrokerCredential(
					request.username, request.requestElevation,
					selectedCredential, selectionError);
				brokerElevationMode = request.requestElevation
					? broker::ElevationMode::RequireElevated
					: broker::ElevationMode::PreserveToken;
				useBrokerCredential = selection ==
					broker_credential_selection::BrokerCredentialSelectionOutcome::Selected;
			}
		}
		else if (request.requestElevation)
		{
			client_logging::Write(L"Info", L"/user-elevated without user -> searching broker admin accounts");
			selection = broker_credential_selection::SelectBrokerCredential(
				L"", true, selectedCredential, selectionError);
			brokerElevationMode = broker::ElevationMode::RequireElevated;
			useBrokerCredential = selection ==
				broker_credential_selection::BrokerCredentialSelectionOutcome::Selected;
		}
		else if (request.selectCredentialWithoutElevation ||
			!account_identity::IsUserInAdminGroup())
		{
			if (request.selectCredentialWithoutElevation)
			{
				client_logging::Write(L"Info", L"/user without user -> searching all broker credentials");
				selection = broker_credential_selection::SelectBrokerCredential(
					L"", false, selectedCredential, selectionError);
			}
			else
			{
				client_logging::Write(L"Info", L"No admin account -> prefer broker admin accounts");
				selection = broker_credential_selection::SelectBrokerCredential(
					L"", true, selectedCredential, selectionError);
				if (selection == broker_credential_selection::BrokerCredentialSelectionOutcome::Selected)
				{
					brokerElevationMode = broker::ElevationMode::RequireElevated;
					client_logging::Write(L"Info", L"Broker admin account selected - requesting elevation automatically");
				}
				else if (selection == broker_credential_selection::BrokerCredentialSelectionOutcome::NotFound)
				{
					client_logging::Write(L"Info", L"No broker admin account found - using all broker credentials");
					selection = broker_credential_selection::SelectBrokerCredential(
						L"", false, selectedCredential, selectionError);
				}
			}
			useBrokerCredential = selection ==
				broker_credential_selection::BrokerCredentialSelectionOutcome::Selected;
		}
		else
		{
			sameUserLaunchMode = account_identity::IsRunningAsAdministrator()
				? same_user_launch::SameUserLaunchMode::CurrentToken
				: same_user_launch::SameUserLaunchMode::ElevateCurrentToken;
			client_logging::Write(L"Info", sameUserLaunchMode == same_user_launch::SameUserLaunchMode::ElevateCurrentToken
				? L"Non-elevated admin account without user switch -> request UAC elevation"
				: L"Already elevated admin account without user switch -> use current token");
		}

		if ((!request.username.empty() || request.requestElevation) &&
			sameUserLaunchMode == same_user_launch::SameUserLaunchMode::Credentials &&
			selection == broker_credential_selection::BrokerCredentialSelectionOutcome::NotFound)
		{
			client_logging::Write(L"Info", request.username.empty()
				? L"No administrator broker credential stored for elevated console replacement - starting administrator credential creation"
				: L"Explicit broker credential for console replacement missing - starting credential prompt for: " +
					request.username);
			if (!broker_credential_management::PromptAndSaveBrokerCredentials(
				request.username, ui::GetDialogOwnerWindow(),
				request.requestElevation))
			{
				client_logging::Write(L"Info",
					L"Credential creation for explicit console replacement cancelled or failed");
				return { ConsoleReplacementCompletion::Failed, 1, false };
			}
			selection = broker_credential_selection::SelectBrokerCredential(
				request.username, request.requestElevation,
				selectedCredential, selectionError);
			useBrokerCredential = selection ==
				broker_credential_selection::BrokerCredentialSelectionOutcome::Selected;
		}

		const bool automaticCredentialSelection = request.username.empty() &&
			!request.requestElevation && !request.selectCredentialWithoutElevation &&
			sameUserLaunchMode == same_user_launch::SameUserLaunchMode::Credentials;
		if (automaticCredentialSelection &&
			selection == broker_credential_selection::BrokerCredentialSelectionOutcome::NotFound)
		{
			client_logging::Write(L"Info", L"No broker credentials available for console replacement - starting credential creation");
			if (!broker_credential_management::PromptAndSaveBrokerCredentials(
				L"", ui::GetDialogOwnerWindow()))
			{
				client_logging::Write(L"Info", L"Automatic credential creation for console replacement cancelled or failed");
				return { ConsoleReplacementCompletion::Failed, 1, false };
			}

			selection = broker_credential_selection::SelectBrokerCredential(
				L"", true, selectedCredential, selectionError);
			if (selection == broker_credential_selection::BrokerCredentialSelectionOutcome::Selected)
			{
				brokerElevationMode = broker::ElevationMode::RequireElevated;
			}
			else if (selection == broker_credential_selection::BrokerCredentialSelectionOutcome::NotFound)
			{
				selection = broker_credential_selection::SelectBrokerCredential(
					L"", false, selectedCredential, selectionError);
				brokerElevationMode = broker::ElevationMode::PreserveToken;
			}
			useBrokerCredential = selection ==
				broker_credential_selection::BrokerCredentialSelectionOutcome::Selected;
		}

		if (selection == broker_credential_selection::BrokerCredentialSelectionOutcome::Cancelled)
		{
			client_logging::Write(L"Info", L"Broker account selection for console replacement cancelled");
			return { ConsoleReplacementCompletion::Cancelled, 0, false };
		}
		if (sameUserLaunchMode == same_user_launch::SameUserLaunchMode::Credentials &&
			!useBrokerCredential)
		{
			const std::wstring message = selection ==
				broker_credential_selection::BrokerCredentialSelectionOutcome::NotAdministrator
				? localization::GetText(
					localization::TextId::ElevationAccountNotAdmin)
				: selection ==
					broker_credential_selection::BrokerCredentialSelectionOutcome::Failed
				? localization::Format(
					localization::TextId::BrokerCredentialsQueryFailed,
					{ windows_error::FormatWindowsError(selectionError) })
				: localization::Format(
					localization::TextId::BrokerCredentialNotFound,
					{ request.programExecutableName });
			ui::ReportError(message,
				selection == broker_credential_selection::
					BrokerCredentialSelectionOutcome::NotAdministrator
				? L"The selected broker credential account is not an administrator."
				: selection == broker_credential_selection::
					BrokerCredentialSelectionOutcome::Failed
				? L"Broker credential selection failed. Windows error: " +
					std::to_wstring(selectionError)
				: L"No matching broker credential was found for console replacement.");
			return { ConsoleReplacementCompletion::Failed, 1, false };
		}

		bool launchSuccess;
		if (useBrokerCredential)
		{
			if (consolePath.empty())
			{
				ui::ReportError(localization::GetText(
					localization::TextId::ConsolePathFailed),
					L"Determining the console executable path failed.");
				return { ConsoleReplacementCompletion::Failed, 1, false };
			}

			wchar_t currentDir[MAX_PATH];
			DWORD dirLen = GetCurrentDirectoryW(MAX_PATH, currentDir);
			std::wstring workDir;
			if (dirLen > 0 && dirLen < MAX_PATH)
			{
				workDir = currentDir;
			}

			const std::wstring consoleArgs =
				console_session::BuildWorkingDirectoryArguments(
					parentShell.type, workDir);

			client_logging::Write(L"Info",
				brokerElevationMode == broker::ElevationMode::RequireElevated
				? L"Launching console with credentials and requested elevation"
				: L"Launching console with credentials without additional elevation");
			if (!consoleArgs.empty())
			{
				client_logging::Write(L"Info", L"Console parameters for working directory: " + consoleArgs);
			}

			const broker_launch::BrokerLaunchOutcome launchOutcome =
				broker_launch::LaunchWithBrokerCredential(
				selectedCredential, consolePath, consoleArgs,
				brokerElevationMode, broker::LaunchVisibility::Normal);
			if (launchOutcome == broker_launch::BrokerLaunchOutcome::Cancelled)
				return { ConsoleReplacementCompletion::Cancelled, 0, false };
			launchSuccess = launchOutcome == broker_launch::BrokerLaunchOutcome::Succeeded;
		}
		else
		{
			wchar_t currentDir[MAX_PATH];
			DWORD dirLen = GetCurrentDirectoryW(MAX_PATH, currentDir);
			std::wstring workDir;
			if (dirLen > 0 && dirLen < MAX_PATH)
			{
				workDir = currentDir;
			}

			const std::wstring consoleArgs =
				console_session::BuildWorkingDirectoryArguments(
					parentShell.type, workDir);

			client_logging::Write(L"Info", sameUserLaunchMode == same_user_launch::SameUserLaunchMode::ElevateCurrentToken
				? L"Launching console under current account with requested elevation"
				: (!request.username.empty()
					? L"Launching console under explicitly selected current account with unchanged token state"
					: L"Launching console under current account with existing elevated token"));
			if (!consoleArgs.empty())
			{
				client_logging::Write(L"Info", L"Console parameters for working directory: " + consoleArgs);
			}
			launchSuccess = console_session::Launch(
				parentShell.type, consoleArgs,
				sameUserLaunchMode == same_user_launch::SameUserLaunchMode::ElevateCurrentToken);
		}

		if (!launchSuccess)
		{
			return { ConsoleReplacementCompletion::LaunchFailed, 1, false };
		}

		Sleep(500);
		console_session::CloseParentConsole();
		return { ConsoleReplacementCompletion::Succeeded, 0, false };
	}
}