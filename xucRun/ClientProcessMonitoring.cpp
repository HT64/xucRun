// Purpose: Implements startup and execution of the internal monitor for hidden target processes.
// Approach: Implements the behavior with focused helpers built on shared xucRun interfaces and Windows APIs.
// Behavior: Inherits only the target handle via attribute list, waits for it, and reports non-zero exit codes.
#include "ClientProcessMonitoring.h"

#include <cstdint>
#include <vector>

#include "ClientCommandLine.h"
#include "ClientUiCommon.h"
#include "Localization.h"

namespace xucrun::process_monitoring
{
	bool StartHiddenProcessMonitor(
		HANDLE process, const std::wstring& programName, DWORD& error)
	{
		error = NO_ERROR;
		HANDLE inheritedProcess = nullptr;
		if (!DuplicateHandle(GetCurrentProcess(), process, GetCurrentProcess(),
			&inheritedProcess, SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
			TRUE, 0))
		{
			error = GetLastError();
			return false;
		}

		wchar_t ownPath[MAX_PATH] = {};
		if (GetModuleFileNameW(nullptr, ownPath, MAX_PATH) == 0)
		{
			error = GetLastError();
			CloseHandle(inheritedProcess);
			return false;
		}
		const std::wstring command = command_line::EscapeArgument(ownPath) +
			L" /internal-monitor:" +
			std::to_wstring(reinterpret_cast<std::uintptr_t>(inheritedProcess)) +
			L" " + command_line::EscapeArgument(programName);
		std::vector<wchar_t> commandLine(command.begin(), command.end());
		commandLine.push_back(L'\0');

		SIZE_T attributeListSize = 0;
		InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeListSize);
		std::vector<std::uint8_t> attributeBuffer(attributeListSize);
		auto* attributeList = reinterpret_cast<PPROC_THREAD_ATTRIBUTE_LIST>(
			attributeBuffer.data());
		STARTUPINFOEXW startup = {};
		startup.StartupInfo.cb = sizeof(startup);
		startup.lpAttributeList = attributeList;
		PROCESS_INFORMATION monitor = {};
		const bool initialized = attributeListSize != 0 &&
			InitializeProcThreadAttributeList(
				attributeList, 1, 0, &attributeListSize) == TRUE;
		const bool attributesReady = initialized &&
			UpdateProcThreadAttribute(attributeList, 0,
				PROC_THREAD_ATTRIBUTE_HANDLE_LIST, &inheritedProcess,
				sizeof(inheritedProcess), nullptr, nullptr) == TRUE;
		const BOOL started = attributesReady && CreateProcessW(ownPath,
			commandLine.data(), nullptr, nullptr, TRUE,
			CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT,
			nullptr, nullptr, &startup.StartupInfo, &monitor);
		error = started ? NO_ERROR : GetLastError();
		if (initialized)
			DeleteProcThreadAttributeList(attributeList);
		CloseHandle(inheritedProcess);
		if (!started)
			return false;
		CloseHandle(monitor.hThread);
		CloseHandle(monitor.hProcess);
		return true;
	}

	int HiddenProcessMonitorMode(
		const std::wstring& option, const std::wstring& programName)
	{
		constexpr const wchar_t* prefix = L"/internal-monitor:";
		if (_wcsnicmp(option.c_str(), prefix, wcslen(prefix)) != 0)
			return 1;
		wchar_t* end = nullptr;
		const unsigned long long value = wcstoull(
			option.c_str() + wcslen(prefix), &end, 10);
		HANDLE process = value != 0 && end && *end == L'\0'
			? reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(value))
			: nullptr;
		if (!process || GetProcessId(process) == 0)
			return 1;
		if (WaitForSingleObject(process, INFINITE) != WAIT_OBJECT_0)
		{
			CloseHandle(process);
			return 1;
		}
		DWORD exitCode = 0;
		const bool exitCodeAvailable =
			GetExitCodeProcess(process, &exitCode) == TRUE;
		CloseHandle(process);
		if (!exitCodeAvailable)
			return 1;
		if (exitCode != 0)
		{
			ui::ReportError(localization::Format(
				localization::TextId::HiddenProcessFailed,
				{ programName, std::to_wstring(exitCode) }),
				L"The monitored hidden process exited with a failure code. Program: " +
					programName + L"; exit code: " + std::to_wstring(exitCode));
		}
		return 0;
	}
}