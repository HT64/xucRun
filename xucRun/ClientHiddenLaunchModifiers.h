// Purpose: Declares parsing of hidden and monitored-hidden launch modifiers.
#pragma once

namespace xucrun::hidden_launch
{
	bool ProcessHiddenLaunchModifier(
		int& argc, wchar_t* argv[], bool& hiddenLaunch, bool& monitorHiddenLaunch);
}