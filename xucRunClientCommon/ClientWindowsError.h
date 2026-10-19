// Purpose: Defines a unified, localized representation of Windows error codes.
// Behavior: Converts a DWORD error into cleaned text understandable to users.
#pragma once

#include <Windows.h>

#include <string>

namespace xucrun::windows_error
{
	std::wstring FormatWindowsError(DWORD error);
}