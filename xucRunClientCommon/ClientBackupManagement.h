// Purpose: Declares client-side backup and restore command handling.
// Approach: Exposes the declarations, constants, and types consumed by the components that provide this behavior.
#pragma once

#include <string>

namespace xucrun::backup_management
{
	int Launch(const std::wstring& command, bool restoreOnly = false);
	int ShowView(bool restoreOnly = false);
	int ExecuteInternalCommand(const std::wstring& argumentTail);
}
