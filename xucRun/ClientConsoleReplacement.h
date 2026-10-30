// Purpose: Defines console replacement for invocations without a target program.
// Approach: Exposes the declarations, constants, and types consumed by the components that provide this behavior.
// Behavior: Detects the parent shell and launches its replacement with the matching account strategy.
#pragma once

#include <string>

namespace xucrun::console_replacement
{
	enum class ConsoleReplacementCompletion
	{
		Succeeded,
		Cancelled,
		Failed,
		LaunchFailed,
		UnknownParent
	};

	struct ConsoleReplacementRequest
	{
		bool onlyUserParameter = false;
		bool requestElevation = false;
		bool selectCredentialWithoutElevation = false;
		std::wstring username;
		std::wstring programExecutableName;
	};

	struct ConsoleReplacementResult
	{
		ConsoleReplacementCompletion completion =
			ConsoleReplacementCompletion::Failed;
		int exitCode = 1;
		bool showHelp = false;
	};

	ConsoleReplacementResult Run(const ConsoleReplacementRequest& request);
}