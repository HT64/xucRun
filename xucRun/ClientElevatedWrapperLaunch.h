// Purpose: Declares launching of a validated broker-bound elevated wrapper process.
// Approach: Exposes the declarations, constants, and types consumed by the components that provide this behavior.
#pragma once

#include <string>

#include "ClientElevatedWrapperPreparation.h"
#include "ClientElevatedWrapperReporter.h"

namespace xucrun::elevated_wrapper
{
	bool LaunchPreparedElevatedWrapperProcess(
		const ElevatedWrapperLaunchPreparation& preparation,
		const std::wstring& originalTargetProgram,
		bool shouldElevate,
		bool hidden,
		bool monitorHiddenProcess,
		BrokerElevatedResultReporter& brokerResult);
}