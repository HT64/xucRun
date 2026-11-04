// Purpose: Defines the client-side selection dialog for local Windows accounts.
// Approach: Exposes the declarations, constants, and types consumed by the components that provide this behavior.
// Behavior: Shows prepared account display strings and returns the selected canonical account.
#pragma once

#include <Windows.h>

#include <cstddef>
#include <string>
#include <vector>

#include "ClientAccountIdentity.h"

namespace xucrun::local_account_selection_dialog
{
	enum class SelectionMode
	{
		CredentialManagement,
		ApprovalDialogHostCredentialManagement,
		Launch
	};

	enum class SelectionPresentation
	{
		TargetAccountCredentialManagement,
		ApprovalDialogHostCredentialManagement,
		Launch
	};

	constexpr SelectionPresentation GetSelectionPresentation(SelectionMode mode)
	{
		switch (mode)
		{
		case SelectionMode::CredentialManagement:
			return SelectionPresentation::TargetAccountCredentialManagement;
		case SelectionMode::ApprovalDialogHostCredentialManagement:
			return SelectionPresentation::ApprovalDialogHostCredentialManagement;
		case SelectionMode::Launch:
			return SelectionPresentation::Launch;
		}
		return SelectionPresentation::Launch;
	}

	bool Show(const std::vector<std::wstring>& accounts,
		const std::vector<std::wstring>& displayAccounts,
		const std::vector<bool>& storedAccounts,
		const std::vector<account_identity::AdministratorMembership>&
			administratorMemberships,
		const std::wstring& currentAccount, std::size_t currentAccountIndex,
		const std::wstring& uacWarning, bool noStoredCredentials, SelectionMode mode,
		std::wstring& selectedAccount, HWND owner);
}