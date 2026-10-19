// Purpose: Defines process queries and classification of supported parent shells.
// Operation: Provides process ID, name, path, and a stable ParentShellType without UI dependencies.
#pragma once

#include <Windows.h>

#include <string>

namespace xucrun::process_context
{
	enum class ParentShellType
	{
		Unknown,
		Cmd,
		PowerShell,
		PowerShellCore,
		TCC
	};

	DWORD GetParentProcessId(DWORD processId);
	std::wstring GetProcessName(DWORD processId);
	std::wstring GetProcessPath(DWORD processId);
	ParentShellType DetectParentShellType(const std::wstring& processName);
	std::wstring ParentShellTypeToString(ParentShellType type);
}