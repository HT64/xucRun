// Purpose: Defines the client-side selection dialog for local Windows accounts.
// Approach: Exposes the declarations, constants, and types consumed by the components that provide this behavior.
// Behavior: Shows prepared account display strings and returns the selected canonical account.
#pragma once

#include <Windows.h>

#include <cstddef>
#include <string>
#include <vector>

namespace xucrun::local_account_selection_dialog
{
	bool Show(const std::vector<std::wstring>& accounts,
		const std::vector<std::wstring>& displayAccounts,
		const std::vector<bool>& storedAccounts,
		const std::wstring& currentAccount, std::size_t currentAccountIndex,
		bool noStoredCredentials, std::wstring& selectedAccount, HWND owner);
}