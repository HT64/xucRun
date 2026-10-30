// Purpose: Defines a unified display path for longer read-only client text.
// Approach: Exposes the declarations, constants, and types consumed by the components that provide this behavior.
// Behavior: Uses an existing console when possible, otherwise a scrollable Win32 dialog.
#pragma once

#include <Windows.h>

#include <string>

namespace xucrun::text_dialog
{
	void Show(const std::wstring& windowTitle, const std::wstring& heading,
		const std::wstring& content, HWND owner);
}