// Purpose: Coordinates a machine-wide language change through the xucRun client.
// Approach: Exposes the declarations, constants, and types consumed by the components that provide this behavior.
#pragma once

namespace xucrun::language_change
{
	bool Request(const wchar_t* elevatedArgument);
}
