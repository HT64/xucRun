// Purpose: Defines dispatch for the client's public administrative commands.
// Behavior: Executes recognized commands and leaves argv ownership and cleanup to the caller.
#pragma once

#include <optional>
#include <string>

namespace xucrun::administrative_commands
{
	std::optional<int> HandleAdministrativeCommand(
		const std::wstring& firstArg, int argc, wchar_t* argv[],
		const std::wstring& specificUserParam, bool resetApprovalWithUser,
		std::wstring resetApprovalUser, const wchar_t* programExecutableName);
}