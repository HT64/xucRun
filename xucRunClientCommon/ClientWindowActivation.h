// Purpose: Defines activation of visible windows for launched or broker-tracked processes.
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