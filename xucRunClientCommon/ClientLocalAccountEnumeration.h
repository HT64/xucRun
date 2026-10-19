// Purpose: Defines client-side enumeration of enabled local Windows accounts.
// Behavior: Returns canonical computer account names for credential selection dialogs.
#pragma once

#include <string>
#include <vector>

namespace xucrun::local_account_enumeration
{
	std::vector<std::wstring> GetEnabledLocalAccounts();
}