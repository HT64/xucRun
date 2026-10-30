// Purpose: Declares parsing of hidden and monitored-hidden launch modifiers.
// Approach: Exposes the declarations, constants, and types consumed by the components that provide this behavior.
#pragma once

namespace xucrun::hidden_launch
{
	bool ProcessHiddenLaunchModifier(
		int& argc, wchar_t* argv[], bool& hiddenLaunch, bool& monitorHiddenLaunch);
}