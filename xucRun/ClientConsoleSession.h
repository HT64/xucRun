// Purpose: Defines detection, replacement, and closure of the invoking console session.
// Approach: Exposes the declarations, constants, and types consumed by the components that provide this behavior.
// Behavior: Encapsulates shell type, executable shell path, working-directory arguments, and launch parameters.
#pragma once

#include <string>

#include "ClientProcessContext.h"

namespace xucrun::console_session
{
	struct ParentShell
	{
		process_context::ParentShellType type =
			process_context::ParentShellType::Unknown;
		std::wstring description = L"Unknown";
	};

	ParentShell DetectParentShell();
	std::wstring GetExecutablePath(process_context::ParentShellType type);
	std::wstring BuildWorkingDirectoryArguments(
		process_context::ParentShellType type,
		const std::wstring& workingDirectory);
	bool Launch(process_context::ParentShellType type,
		const std::wstring& parameters = L"", bool requestElevation = false);
	bool CloseParentConsole();
}