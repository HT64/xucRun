// Purpose: Defines a unified, localized representation of Windows error codes.
// Approach: Exposes the declarations, constants, and types consumed by the components that provide this behavior.
// Behavior: Converts a DWORD error into cleaned text understandable to users.
#pragma once

#include <Windows.h>

#include <string>

namespace xucrun::windows_error
{
	std::wstring FormatWindowsError(DWORD error);
}