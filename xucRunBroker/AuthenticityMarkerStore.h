// Purpose: Declares the per-user authenticity marker stored exclusively by the broker.
// Approach: Exposes the declarations, constants, and types consumed by the components that provide this behavior.
#pragma once

#include <Windows.h>

#include <string>

namespace xucrun::authenticity_marker_store
{
	bool GetOrCreate(const std::wstring& clientSid,
		std::wstring& marker, DWORD& error);
}
