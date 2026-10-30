// Purpose: Defines retrieval and selection of stored broker credentials.
// Approach: Exposes the declarations, constants, and types consumed by the components that provide this behavior.
// Behavior: Processes credential metadata only; passwords remain in the broker.
#pragma once

#include <Windows.h>

#include <string>
#include <vector>

#include "ClientCredentialInfo.h"

namespace xucrun::broker_credential_selection
{
	enum class BrokerCredentialSelectionOutcome
	{
		Selected,
		NotFound,
		NotAdministrator,
		Cancelled,
		Failed
	};

	bool GetAllBrokerCredentials(std::vector<CredentialInfo>& credentials, DWORD& error);

	BrokerCredentialSelectionOutcome SelectBrokerCredential(
		const std::wstring& specificUser,
		bool requireAdmin,
		CredentialInfo& selectedCredential,
		DWORD& error);

	BrokerCredentialSelectionOutcome SelectBrokerApprovalCredential(
		CredentialInfo& selectedCredential, DWORD& error);
}