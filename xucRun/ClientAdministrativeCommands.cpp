// Purpose: Dispatches administrative client commands to status, credential, approval, backup, language, and audit workflows.
#include "ClientAdministrativeCommands.h"

#include <Windows.h>

#include <string>

#include "BrokerContracts.h"
#include "ClientApprovalManagement.h"
#include "ClientAdministrativeStatus.h"
#include "ClientBackupManagement.h"
#include "ClientBrokerCredentialManagement.h"
#include "ClientInfoCommand.h"
#include "ClientLanguageChange.h"
#include "ClientLogging.h"
#include "ClientSecurityAudit.h"
#include "ClientUiCommon.h"
#include "ClientWindowsError.h"
#include "Localization.h"

namespace
{
	DWORD SetConfiguredLanguage(const wchar_t* language)
	{
		HANDLE token = nullptr;
		TOKEN_ELEVATION elevation = {};
		DWORD returnLength = 0;
		const bool elevationKnown = OpenProcessToken(
			GetCurrentProcess(), TOKEN_QUERY, &token) &&
			GetTokenInformation(
				token, TokenElevation, &elevation, sizeof(elevation), &returnLength);
		DWORD error = elevationKnown ? ERROR_SUCCESS : GetLastError();
		if (token)
			CloseHandle(token);
		if (elevationKnown && !elevation.TokenIsElevated)
			error = ERROR_ELEVATION_REQUIRED;

		wchar_t programFiles[MAX_PATH] = {};
		if (error == ERROR_SUCCESS)
		{
			const DWORD pathLength = GetEnvironmentVariableW(
				L"ProgramFiles", programFiles, ARRAYSIZE(programFiles));
			if (pathLength == 0 || pathLength >= ARRAYSIZE(programFiles))
			{
				error = pathLength == 0 ? GetLastError() : ERROR_INSUFFICIENT_BUFFER;
				if (error == ERROR_SUCCESS)
					error = ERROR_PATH_NOT_FOUND;
			}
		}

		if (error == ERROR_SUCCESS)
		{
			DWORD validationError = ERROR_SUCCESS;
			if (!xucrun::localization::ValidateLanguageResourceForSwitch(
				language, validationError))
			{
				error = validationError;
			}
		}

		if (error == ERROR_SUCCESS)
		{
			const std::wstring configurationPath =
				std::wstring(programFiles) + L"\\xucRun\\xucRun.ini";
			SetLastError(ERROR_SUCCESS);
			if (!WritePrivateProfileStringW(
				L"xucRun", L"Language", language, configurationPath.c_str()))
			{
				error = GetLastError();
				if (error == ERROR_SUCCESS)
					error = ERROR_WRITE_FAULT;
			}
		}

		return error;
	}

	bool SendLanguageChangeResult(
		const std::wstring& pipeToken, DWORD result)
	{
		const std::wstring pipeName =
			std::wstring(xucrun::broker::SetLanguagePipeNamePrefix) + pipeToken;
		if (!WaitNamedPipeW(pipeName.c_str(), 15000))
			return false;
		HANDLE pipe = CreateFileW(pipeName.c_str(), GENERIC_WRITE, 0, nullptr,
			OPEN_EXISTING, 0, nullptr);
		if (pipe == INVALID_HANDLE_VALUE)
			return false;
		DWORD transferred = 0;
		const bool sent = WriteFile(
			pipe, &result, sizeof(result), &transferred, nullptr) != FALSE &&
			transferred == sizeof(result);
		CloseHandle(pipe);
		return sent;
	}
}

namespace xucrun::administrative_commands
{
	std::optional<int> HandleAdministrativeCommand(
		const std::wstring& firstArg, int argc, wchar_t* argv[],
		const std::wstring& specificUserParam, bool resetApprovalWithUser,
		std::wstring resetApprovalUser, const wchar_t* programExecutableName)
	{
		if (firstArg == L"/info")
		{
			client_logging::Write(L"Info", L"=== /info: Runtime information ===");
			const int result = info_command::ShowInfo();
			client_logging::Write(L"Info", L"Program exit (/info)");
			return result;
		}

		if (firstArg == L"/audit")
		{
			client_logging::Write(L"Info", L"=== /audit: Security checks ===");
			const int result = security_audit::RunAuditCommand();
			client_logging::Write(result != 0 ? L"Warning" : L"Info",
				result != 0 ? L"Program exit (/audit), exit code 1"
					: L"Program exit (/audit), exit code 0");
			return result;
		}

		if (firstArg == L"/admin-status")
		{
			if (argc != 2)
				return 1;
			return administrative_status::LaunchView();
		}

		if (firstArg == L"/internal-admin-status-view")
		{
			if (argc != 2)
				return 1;
			return administrative_status::ShowView();
		}
		if (firstArg == L"/internal-backup-view")
		{
			if (argc != 2)
				return 1;
			return backup_management::ShowView();
		}
		if (firstArg == L"/internal-backup-restore-view")
		{
			if (argc != 2)
				return 1;
			return backup_management::ShowView(true);
		}
		if (firstArg == broker::SetLanguageGermanArgumentTail)
		{
			if (argc != 2)
				return 1;
			const DWORD error = SetConfiguredLanguage(L"de");
			if (error != ERROR_SUCCESS)
			{
				ui::ReportError(localization::Format(
					localization::TextId::LanguageChangeFailed,
					{ windows_error::FormatWindowsError(error) }),
					L"Changing the UI language to German failed. Windows error: " +
						std::to_wstring(error));
			}
			return error == ERROR_SUCCESS ? 0 : 1;
		}
		if (firstArg == broker::SetLanguageEnglishArgumentTail)
		{
			if (argc != 2)
				return 1;
			const DWORD error = SetConfiguredLanguage(L"en");
			if (error != ERROR_SUCCESS)
			{
				ui::ReportError(localization::Format(
					localization::TextId::LanguageChangeFailed,
					{ windows_error::FormatWindowsError(error) }),
					L"Changing the UI language to English failed. Windows error: " +
						std::to_wstring(error));
			}
			return error == ERROR_SUCCESS ? 0 : 1;
		}
		if (firstArg == broker::RequestLanguageGermanArgumentTail)
		{
			if (argc != 2)
				return 1;
			return language_change::Request(
				broker::SetLanguageGermanArgumentTail) ? 0 : 1;
		}
		if (firstArg == broker::RequestLanguageEnglishArgumentTail)
		{
			if (argc != 2)
				return 1;
			return language_change::Request(
				broker::SetLanguageEnglishArgumentTail) ? 0 : 1;
		}
		std::wstring language;
		std::wstring languagePipeToken;
		if (broker::ParseSetLanguageCommandArgument(
			firstArg, language, languagePipeToken))
		{
			if (argc != 2)
				return 1;
			const DWORD error = SetConfiguredLanguage(language.c_str());
			return SendLanguageChangeResult(languagePipeToken, error) &&
				error == ERROR_SUCCESS ? 0 : 1;
		}
		const std::wstring backupCommandPrefix =
			broker::AdministrativeBackupCommandArgumentPrefix;
		if (firstArg.starts_with(backupCommandPrefix))
		{
			if (argc != 2)
				return 1;
			return backup_management::ExecuteInternalCommand(firstArg);
		}
		if (firstArg == L"/backup")
		{
			return argc == 2 ? backup_management::Launch(L"") : 1;
		}
		if (firstArg == L"/backup-create")
		{
			if (argc < 2 || argc > 3) return 1;
			std::wstring command;
			if (!broker::BuildAdministrativeBackupCreateCommand(
				argc == 3 ? argv[2] : L"", command))
			{
				return 1;
			}
			return backup_management::Launch(command);
		}
		if (firstArg == L"/backup-list")
			return argc == 2 ? backup_management::Launch(L"list") : 1;
		if (firstArg == L"/backup-delete-all")
			return argc == 2 ? backup_management::Launch(L"delete-all") : 1;
		if (firstArg == L"/backup-restore")
			return argc == 2 ? backup_management::Launch(L"", true) : 1;
		if (firstArg.starts_with(L"/backup-restore:"))
		{
			const std::wstring backupId = firstArg.substr(16);
			return argc == 2 && broker::IsValidBackupId(backupId)
				? backup_management::Launch(L"restore:" + backupId) : 1;
		}
		const std::wstring administrativeConsolePrefix =
			broker::AdministrativeInventoryConsoleArgumentPrefix;
		if (firstArg.starts_with(administrativeConsolePrefix))
		{
			if (argc != 2)
				return 1;
			return administrative_status::WriteConsoleView(
				firstArg.substr(administrativeConsolePrefix.size()));
		}

		if (firstArg == L"/addcred")
		{
			client_logging::Write(L"Info", L"=== /addcred: Secure credential storage via CredUI dialog ===");

			std::wstring prefilledUsername;
			if (!specificUserParam.empty())
			{
				prefilledUsername = specificUserParam;
				client_logging::Write(L"Info", L"Username from /user parameter: " + prefilledUsername);
			}
			else if (argc >= 3)
			{
				prefilledUsername = argv[2];
				client_logging::Write(L"Info", L"Username provided (classic format): " + prefilledUsername);
			}

			HWND dialogOwner = ui::GetDialogOwnerWindow();
			const bool result = broker_credential_management::PromptAndSaveBrokerCredentials(
				prefilledUsername, dialogOwner);
			client_logging::Write(L"Info", L"Program exit (/addcred)");
			return result ? 0 : 1;
		}

		if (firstArg == L"/resetapprovals" || resetApprovalWithUser)
		{
			if (resetApprovalUser.empty() && argc >= 3)
				resetApprovalUser = argv[2];
			const int result = approval_management::ResetBrokerApprovals(resetApprovalUser);
			client_logging::Write(L"Info", L"Program exit (/resetapprovals)");
			return result;
		}

		if (firstArg == L"/deletecred")
		{
			std::wstring usernameToDelete = specificUserParam;
			if (usernameToDelete.empty() && argc >= 3)
			{
				usernameToDelete = argv[2];
			}

			client_logging::Write(L"Info", L"=== /deletecred: Delete credential ===");
			const int result = usernameToDelete.empty()
				? broker_credential_management::DeleteStoredCredential()
				: broker_credential_management::DeleteBrokerCredential(
					usernameToDelete, ui::GetDialogOwnerWindow());
			client_logging::Write(L"Info", L"Program exit (/deletecred)");
			return result;
		}

		if (firstArg == L"/showcred")
		{
			client_logging::Write(L"Info", L"=== /showcred: Show stored credentials ===");
			const int result = broker_credential_management::ShowBrokerCredentials(programExecutableName);
			client_logging::Write(result == 0 ? L"Info" : L"Error",
				result == 0 ? L"Program exit (/showcred)"
					: L"Program exit (/showcred - display failed)");
			return result;
		}

		if (firstArg == L"/showapprovals")
		{
			client_logging::Write(L"Info", L"=== /showapprovals: Show stored broker approvals ===");
			const int result = approval_management::ShowBrokerApprovals();
			if (result == 0)
				client_logging::Write(L"Info", L"Program exit (/showapprovals)");
			return result;
		}

		return std::nullopt;
	}
}