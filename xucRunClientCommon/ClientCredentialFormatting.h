// Purpose: Defines localized display text for credentials, approvals, and broker errors.
// Behavior: Formats broker contract types and timestamps without dialog or storage access.
#pragma once

#include <Windows.h>

#include <cstddef>
#include <string>

#include "ApprovalSerialization.h"
#include "BrokerContracts.h"

namespace xucrun::credential_formatting
{
	enum class CredentialSelectionMode
	{
		Launch,
		ElevatedLaunch,
		ApprovalConfirmation,
		Delete,
		ResetApproval
	};

	std::wstring GetBrokerChallengeFailureMessage(
		const broker::ApprovalChallengeResult& result);
	std::wstring GetBrokerLaunchFailureMessage(
		const broker::ApprovalConfirmationResult& result);
	std::wstring FormatCredentialTimestamp(ULONGLONG timestamp);
	std::wstring GetCredentialSelectionTitle(CredentialSelectionMode mode);
	std::wstring GetResetApprovalSelectionHint(std::size_t approvalCount,
		const std::wstring& accountName);
	std::wstring FormatBrokerApprovalForReset(
		const approval::BrokerApprovalValue& approval);
	std::wstring FormatBrokerApprovalScope(
		const approval::BrokerApprovalValue& approval);
	std::wstring FormatExactInvocationBinding(
		const approval::BrokerApprovalValue& approval,
		const std::wstring& separator);
}