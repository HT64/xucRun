// Purpose: Defines multi-selection of broker approvals to reset.
// Behavior: Shows prepared approval texts and returns their selected indices.
#pragma once

#include <string>
#include <vector>

namespace xucrun::approval_selection_dialog
{
	struct ResetApprovalOption
	{
		bool general = false;
		bool allAccounts = false;
		std::wstring targetName;
		std::wstring displayText;
	};

	std::vector<int> ShowResetApprovalSelectionDialog(
		const std::wstring& invokingAccount, const std::wstring& targetAccount,
		const std::vector<ResetApprovalOption>& options, bool canGoBack,
		bool& cancelled);
}