// Purpose: Defines stateless detection rules for special client process launches.
// Approach: Exposes the declarations, constants, and types consumed by the components that provide this behavior.
// Behavior: Evaluates target path and argument sequence without starting processes or changing UI state.
#pragma once

#include <string>

namespace xucrun::launch_detection
{
	bool IsExplicitlyHiddenPowerShellLaunch(
		const std::wstring& executablePath, const std::wstring& argumentTail);
}