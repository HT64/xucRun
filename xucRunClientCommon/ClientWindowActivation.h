// Purpose: Defines activation of visible windows for launched or broker-tracked processes.
// Approach: Exposes the declarations, constants, and types consumed by the components that provide this behavior.
// Behavior: Finds main windows by process ID and revalidates tracked windows before activation.
#pragma once

#include <Windows.h>

#include "BrokerContracts.h"

namespace xucrun::window_activation
{
	void ActivateTrackedProcessWindow(
		const broker::TrackedProcessListEntry& process, HWND targetWindow);
	void ActivateBrokerProcessWindow(DWORD processId);
}