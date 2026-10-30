// Purpose: Runs a helper on an isolated high-integrity desktop and restores the user desktop.
// Approach: Exposes the declarations, constants, and types consumed by the components that provide this behavior.
#pragma once

#include <Windows.h>

namespace xucrun::secure_desktop
{
	bool RunSecurityDescriptorSelfTest();
	bool RunCancellationWaitSelfTest();

	int RunHost(HANDLE helperToken,
		const wchar_t* helperPath,
		const wchar_t* helperArgument,
		const wchar_t* workingDirectory,
		HANDLE childProcessHandleReport,
		HANDLE cancellationEvent,
		bool requireLocalSystemHost);
}
