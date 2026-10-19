// Purpose: Defines the elevated, read-only status view across Windows user profiles.
#pragma once

#include <string>

namespace xucrun::administrative_status
{
	int LaunchView();
	int ShowView();
	int WriteConsoleView(const std::wstring& pipeToken);
}