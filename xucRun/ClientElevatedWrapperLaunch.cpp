// Purpose: Launches the broker-prepared elevated wrapper process and reports its final result securely.
#include "ClientElevatedWrapperLaunch.h"

#include <Windows.h>
#include <shellapi.h>

#include <string>

#include "BrokerContracts.h"
#include "BrokerPipeClient.h"
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

namespace xucrun::elevated_wrapper
{
	bool LaunchPreparedElevatedWrapperProcess(
		const ElevatedWrapperLaunchPreparation& preparation,
		const std::wstring& originalTargetProgram,
		bool shouldElevate,
		bool hidden,
		bool monitorHiddenProcess,
		BrokerElevatedResultReporter& brokerResult)
	{
		const std::wstring& finalProgram = preparation.finalProgram;
		const std::wstring& finalArguments = preparation.finalArguments;
		const std::wstring& workingDirectory = preparation.workingDirectory;

		SHELLEXECUTEINFOW sei = { sizeof(sei) };
		sei.lpVerb = shouldElevate ? L"runas" : NULL;
		sei.lpFile = finalProgram.c_str();
		sei.lpParameters = finalArguments.empty() ? NULL : finalArguments.c_str();
		sei.lpDirectory = workingDirectory.empty() ? NULL : workingDirectory.c_str();
		sei.nShow = hidden ? SW_HIDE : SW_SHOWNORMAL;
		sei.fMask = SEE_MASK_NOCLOSEPROCESS;

		client_logging::Write(L"Info", L"ShellExecuteEx is called with:");
		client_logging::Write(L"Info", std::wstring(L"  lpVerb: ") + (shouldElevate ? L"runas (elevation)" : L"NULL (normal launch)"));
		client_logging::Write(L"Info", L"  lpFile: " + finalProgram);
		if (finalProgram != originalTargetProgram)
		{
			client_logging::Write(L"Info", L"  (original program: " + originalTargetProgram + L")");
		}
		if (!finalArguments.empty())
		{
			client_logging::Write(L"Info", L"  lpParameters: <present>");
		}
		client_logging::Write(L"Info", std::wstring(L"  nShow: ") +
			(sei.nShow == SW_HIDE ? L"SW_HIDE" : L"SW_SHOWNORMAL"));

		if (!ShellExecuteExW(&sei))
		{
			DWORD error = GetLastError();
			brokerResult.complete(error);
			if (error == ERROR_CANCELLED)
			{
				client_logging::Write(L"Warn", L"UAC cancelled (user clicked 'No')");
			}
			else
			{
				ui::ReportError(localization::Format(
					localization::TextId::ProgramLaunchFailed,
					{ shouldElevate ? localization::GetText(
						localization::TextId::ProgramElevationSuffix) : L"",
					  windows_error::FormatWindowsError(error) }),
					L"Launching the target from the elevated wrapper failed. Windows error: " +
						std::to_wstring(error));
			}
			return false;
		}

		const DWORD launchedProcessId = sei.hProcess ? GetProcessId(sei.hProcess) : 0;
		if (brokerResult.active() && launchedProcessId == 0)
		{
			const DWORD error = GetLastError() == NO_ERROR ? ERROR_INVALID_HANDLE : GetLastError();
			brokerResult.complete(error);
			if (sei.hProcess)
				CloseHandle(sei.hProcess);
			return false;
		}
		brokerResult.complete(NO_ERROR, launchedProcessId, sei.hProcess);
		if (monitorHiddenProcess && sei.hProcess)
		{
			DWORD monitorError = NO_ERROR;
			if (!process_monitoring::StartHiddenProcessMonitor(sei.hProcess,
				GetProgramNameFromPath(originalTargetProgram), monitorError))
			{
				ui::ReportError(localization::Format(
					localization::TextId::HiddenMonitorSetupFailed,
					{ windows_error::FormatWindowsError(monitorError) }),
					L"Starting the hidden process monitor from the elevated wrapper failed. "
					L"Windows error: " + std::to_wstring(monitorError));
			}
		}
		if (sei.hProcess)
			CloseHandle(sei.hProcess);

		client_logging::Write(L"Ok", std::wstring(L"Process ") +
			(shouldElevate ? L"with elevation" : L"without additional elevation") +
			L" launched - " + std::wstring(broker::ProductName) + L" exits immediately");

		return true;
	}
}