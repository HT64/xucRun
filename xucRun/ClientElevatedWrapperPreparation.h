// Purpose: Defines launch preparation for internal elevated wrapper mode.
// Behavior: Prepares program, arguments, and working directory for ShellExecuteEx.
#pragma once

#include <string>

namespace xucrun::elevated_wrapper
{
	struct ElevatedWrapperLaunchPreparation
	{
		std::wstring finalProgram;
		std::wstring finalArguments;
		std::wstring workingDirectory;
	};

	ElevatedWrapperLaunchPreparation PrepareElevatedWrapperLaunch(
		const std::wstring& targetProgram,
		const std::wstring& targetArguments,
		const std::wstring& workingDirectory,
		bool shouldElevate);
}