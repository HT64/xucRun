// Purpose: Defines parsing of the internal command line for the elevated wrapper.
// Behavior: Splits wrapper options, target program, and broker result channel without starting a process.

#pragma once

#include <string>

namespace xucrun::elevated_wrapper
{
	struct ElevatedWrapperArguments
	{
		bool requestElevation = false;
		bool hidden = false;
		bool monitorHiddenProcess = false;
		std::wstring workingDirectory;
		std::wstring targetProgram;
		std::wstring targetArguments;
	};

	enum class ElevatedWrapperParseStatus
	{
		Succeeded,
		ProgramMissing,
		ElevationSwitchMissing,
		InvalidBrokerResultMarker
	};

	ElevatedWrapperParseStatus ParseElevatedWrapperCommandLine(
		int argc, const std::wstring& rawCommandLine, ElevatedWrapperArguments& arguments);
}