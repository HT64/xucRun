// Purpose: Defines display and reset of stored broker approvals.
// Approach: Exposes the declarations, constants, and types consumed by the components that provide this behavior.
// Behavior: Connects broker queries, account selection, and localized client dialogs.
#pragma once

#include <string>

namespace xucrun::approval_management
{
	int ResetBrokerApprovals(const std::wstring& specificUser);
	int ShowBrokerApprovals();
}