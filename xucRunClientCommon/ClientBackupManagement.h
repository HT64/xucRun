// Purpose: Declares client-side backup and restore command handling.
#pragma once

#include <string>

namespace xucrun::backup_management
{
	int Launch(const std::wstring& command, bool restoreOnly = false);
	int ShowView(bool restoreOnly = false);
	int ExecuteInternalCommand(const std::wstring& argumentTail);
}
