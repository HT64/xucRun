// Purpose: Defines monitoring of hidden-started target processes via an xucRun helper process.
// Behavior: Passes only a restricted process handle and visibly reports failing exit codes.
#pragma once

#include <Windows.h>

#include <string>

namespace xucrun::process_monitoring
{
	bool StartHiddenProcessMonitor(
		HANDLE process, const std::wstring& programName, DWORD& error);
	int HiddenProcessMonitorMode(
		const std::wstring& option, const std::wstring& programName);
}