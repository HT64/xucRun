// Purpose: Defines client-side enumeration of enabled local Windows accounts.
// Approach: Exposes the declarations, constants, and types consumed by the components that provide this behavior.
// Behavior: Returns canonical computer account names for credential selection dialogs.
#pragma once

#include <string>
#include <vector>

namespace xucrun::local_account_enumeration
{
	std::vector<std::wstring> GetEnabledLocalAccounts();
}