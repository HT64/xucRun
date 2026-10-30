// Purpose: Defines the elevated, read-only status view across Windows user profiles.
// Approach: Exposes the declarations, constants, and types consumed by the components that provide this behavior.
#pragma once

#include <string>

namespace xucrun::administrative_status
{
	int LaunchView();
	int ShowView();
	int WriteConsoleView(const std::wstring& pipeToken);
}