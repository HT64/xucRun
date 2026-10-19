// Purpose: Defines display and reset of stored broker approvals.
// Behavior: Connects broker queries, account selection, and localized client dialogs.
#pragma once

#include <string>

namespace xucrun::approval_management
{
	int ResetBrokerApprovals(const std::wstring& specificUser);
	int ShowBrokerApprovals();
}