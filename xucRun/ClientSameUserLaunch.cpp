// Purpose: Starts targets with the current user token while preserving requested elevation semantics.
#include "ClientSameUserLaunch.h"

#include <Windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cwctype>
#include <sstream>
#include <string>

#include "ClientAccountIdentity.h"
#include "ClientBrokerLaunch.h"
#include "ClientLogging.h"
#include "ClientProcessMonitoring.h"
#include "ClientUiCommon.h"
#include "ClientWindowsError.h"
#include "Localization.h"

namespace
{
	std::wstring GetProgramNameFromPath(const std::wstring& path)
	{
		size_t separator = path.find_last_of(L"\\/");
		return separator == std::wstring::npos ? path : path.substr(separator + 1);
	}
}

namespace xucrun::same_user_launch
{
	SameUserLaunchMode GetSameUserLaunchMode(
		const std::wstring& accountName,
		bool requestElevation)
	{
		if (!account_identity::IsCurrentUserAccount(accountName))
			return SameUserLaunchMode::Credentials;

		if (requestElevation)
		{
			if (!account_identity::IsUserInAdminGroup())
			{
				client_logging::Write(L"Info",
					L"Same-user account cannot be elevated directly because it is not an administrator account");
				return SameUserLaunchMode::Credentials;
			}
			return account_identity::IsRunningAsAdministrator()
				? SameUserLaunchMode::CurrentToken
				: SameUserLaunchMode::ElevateCurrentToken;
		}

		return SameUserLaunchMode::CurrentToken;
	}

	bool LaunchDirectly(
		const std::wstring& programPath,
		const std::wstring& arguments,
		bool elevateCurrentToken,
		bool hiddenLaunch,
		bool monitorHiddenLaunch,
		HANDLE* launchedProcess)
	{
		if (launchedProcess)
			*launchedProcess = nullptr;
		// Also start batch targets explicitly via cmd.exe in the direct same-user path.
		// A quoted first parameter ending with a trailing backslash can collapse
		// remaining arguments and is reconstructed first.
		std::wstring finalProgram = programPath;
		std::wstring finalArguments = arguments;
		std::wstring lowerProgramPath = programPath;
		std::transform(lowerProgramPath.begin(), lowerProgramPath.end(), lowerProgramPath.begin(), ::towlower);
		const bool isBatchTarget = lowerProgramPath.ends_with(L".bat") || lowerProgramPath.ends_with(L".cmd");
		if (isBatchTarget)
		{
			if (!finalArguments.empty() &&
				finalArguments.front() == L'"' &&
				finalArguments.back() == L'"' &&
				std::count(finalArguments.begin(), finalArguments.end(), L'"') == 3)
			{
				std::wstring inner = finalArguments.substr(1, finalArguments.length() - 2);
				size_t trailingBackslashQuote = inner.find(L"\\\"");
				if (trailingBackslashQuote != std::wstring::npos &&
					trailingBackslashQuote + 2 < inner.length() &&
					inner[trailingBackslashQuote + 2] == L' ')
				{
					std::wstring correctedArguments = L"\"" + inner.substr(0, trailingBackslashQuote + 1) + L"\"";
					std::wistringstream remainingArguments(inner.substr(trailingBackslashQuote + 3));
					std::wstring argument;
					while (remainingArguments >> argument)
					{
						correctedArguments += L" \"" + argument + L"\"";
					}
					finalArguments = correctedArguments;
					client_logging::Write(L"Info", L"Corrected batch arguments with trailing backslash: " + finalArguments);
				}
			}

			wchar_t systemDirectory[MAX_PATH];
			if (GetSystemDirectoryW(systemDirectory, MAX_PATH) > 0)
			{
				finalProgram = std::wstring(systemDirectory) + L"\\cmd.exe";
			}
			else
			{
				finalProgram = L"cmd.exe";
			}

			std::wstring batchArguments = finalArguments;
			finalArguments = L"/D /S /C \"\"" + programPath + L"\"";
			if (!batchArguments.empty())
			{
				finalArguments += L" " + batchArguments;
			}
			finalArguments += L"\"";
		}

		SHELLEXECUTEINFOW sei = { sizeof(sei) };
		sei.fMask = SEE_MASK_NOCLOSEPROCESS;
		sei.lpVerb = elevateCurrentToken ? L"runas" : NULL;
		sei.lpFile = finalProgram.c_str();
		sei.lpParameters = finalArguments.empty() ? NULL : finalArguments.c_str();
		sei.nShow = hiddenLaunch ? SW_HIDE : SW_SHOWNORMAL;

		client_logging::Write(L"Info", elevateCurrentToken
			? L"ShellExecuteEx with runas:"
			: L"ShellExecuteEx without additional elevation (lpVerb=NULL):");
		client_logging::Write(L"Info", L"  lpFile: " + finalProgram);
		if (!finalArguments.empty())
		{
			client_logging::Write(L"Info", L"  lpParameters: " + finalArguments);
		}
		client_logging::Write(L"Info", std::wstring(L"  nShow: ") +
			(sei.nShow == SW_HIDE ? L"SW_HIDE" : L"SW_SHOWNORMAL"));

		if (!ShellExecuteExW(&sei))
		{
			DWORD error = GetLastError();
			ui::ReportError(localization::Format(
				localization::TextId::ProgramLaunchFailed,
				{ L"", windows_error::FormatWindowsError(error) }),
				L"Launching the target in the current user context failed. Windows error: " +
					std::to_wstring(error));
			return false;
		}

		client_logging::Write(L"Ok", elevateCurrentToken
			? L"Program launched with requested elevation"
			: L"Program launched without additional elevation");

		if (sei.hProcess)
		{
			if (elevateCurrentToken || account_identity::IsRunningAsAdministrator())
			{
				broker_launch::RegisterElevatedProcessWithBroker(sei.hProcess);
			}
			if (monitorHiddenLaunch)
			{
				DWORD monitorError = NO_ERROR;
				if (!process_monitoring::StartHiddenProcessMonitor(
					sei.hProcess, GetProgramNameFromPath(programPath), monitorError))
				{
					ui::ReportError(localization::Format(
						localization::TextId::HiddenMonitorSetupFailed,
						{ windows_error::FormatWindowsError(monitorError) }),
						L"Starting the hidden process monitor in the current user context failed. "
						L"Windows error: " + std::to_wstring(monitorError));
				}
			}
			if (launchedProcess)
				*launchedProcess = sei.hProcess;
			else
				CloseHandle(sei.hProcess);
		}

		return true;
	}
}