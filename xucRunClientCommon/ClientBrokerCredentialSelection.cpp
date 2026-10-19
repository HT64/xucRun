// Purpose: Implements retrieval and selection of stored broker credentials.
// Behavior: Queries metadata from the broker and filters by account and admin status.
#include "ClientBrokerCredentialSelection.h"

#include <algorithm>
#include <utility>

#include "BrokerContracts.h"
#include "BrokerPipeClient.h"
#include "ClientAccountIdentity.h"
#include "ClientCredentialFormatting.h"
#include "ClientCredentialSelectionDialog.h"

namespace xucrun::broker_credential_selection
{
	namespace
	{
		BrokerCredentialSelectionOutcome SelectCredential(
			const std::wstring& specificUser, bool requireAdmin,
			credential_formatting::CredentialSelectionMode selectionMode,
			CredentialInfo& selectedCredential, DWORD& error);
	}

	bool GetAllBrokerCredentials(std::vector<CredentialInfo>& credentials, DWORD& error)
	{
		credentials.clear();
		error = NO_ERROR;
		broker::CredentialListRequest request;
		request.requestId = broker::GenerateBrokerRequestId();
		if (request.requestId == 0)
		{
			error = ERROR_GEN_FAILURE;
			return false;
		}
		const broker::PipeCredentialListResponse response =
			broker::SubmitCredentialListRequest(request);
		if (response.status != broker::PipeCredentialListStatus::Succeeded)
		{
			error = response.windowsError;
			return false;
		}
		if (response.result.status != broker::CredentialListStatus::Succeeded)
		{
			error = response.result.windowsError == NO_ERROR
				? ERROR_INVALID_DATA
				: response.result.windowsError;
			return false;
		}
		credentials.reserve(response.result.entries.size());
		for (const broker::CredentialListEntry& entry : response.result.entries)
		{
			CredentialInfo credential;
			credential.username = entry.canonicalAccountName;
			credential.accountSid = entry.accountSid;
			credential.lastWritten = entry.lastWritten;
			credentials.push_back(std::move(credential));
		}
		return true;
	}

	BrokerCredentialSelectionOutcome SelectBrokerCredential(
		const std::wstring& specificUser, bool requireAdmin,
		CredentialInfo& selectedCredential, DWORD& error)
	{
		return SelectCredential(specificUser, requireAdmin,
			requireAdmin
				? credential_formatting::CredentialSelectionMode::ElevatedLaunch
				: credential_formatting::CredentialSelectionMode::Launch,
			selectedCredential, error);
	}

	BrokerCredentialSelectionOutcome SelectBrokerApprovalCredential(
		CredentialInfo& selectedCredential, DWORD& error)
	{
		return SelectCredential(L"", true,
			credential_formatting::CredentialSelectionMode::ApprovalConfirmation,
			selectedCredential, error);
	}

	namespace
	{
		BrokerCredentialSelectionOutcome SelectCredential(
			const std::wstring& specificUser, bool requireAdmin,
			credential_formatting::CredentialSelectionMode selectionMode,
			CredentialInfo& selectedCredential, DWORD& error)
		{
		std::vector<CredentialInfo> credentials;
		if (!GetAllBrokerCredentials(credentials, error))
			return BrokerCredentialSelectionOutcome::Failed;

		std::wstring requestedSid;
		if (!specificUser.empty())
		{
			std::wstring canonicalName;
			if (!account_identity::ResolveAccountIdentity(
				specificUser, canonicalName, requestedSid))
			{
				error = ERROR_NONE_MAPPED;
				return BrokerCredentialSelectionOutcome::NotFound;
			}

			// Validate the account before looking for a stored credential. An explicit
			// standard account can never satisfy /user-elevated, regardless of whether
			// its password is already stored. Reporting that distinction prevents both
			// an unnecessary credential prompt and a misleading "not found" result.
			if (requireAdmin &&
				!account_identity::IsUserInAdminGroup(canonicalName))
			{
				error = ERROR_ACCESS_DENIED;
				return BrokerCredentialSelectionOutcome::NotAdministrator;
			}
			const auto storedCredential = std::find_if(
				credentials.begin(), credentials.end(),
				[&requestedSid](const CredentialInfo& credential) {
					return credential.accountSid == requestedSid;
				});
			if (storedCredential == credentials.end())
				return BrokerCredentialSelectionOutcome::NotFound;
		}
		if (requireAdmin)
		{
			std::erase_if(credentials, [](const CredentialInfo& credential) {
				return !account_identity::IsUserInAdminGroup(credential.username);
			});
		}
		if (credentials.empty())
			return BrokerCredentialSelectionOutcome::NotFound;

		int selectedIndex = -1;
		if (!specificUser.empty())
		{
			const auto found = std::find_if(credentials.begin(), credentials.end(),
				[&requestedSid](const CredentialInfo& credential) {
					return credential.accountSid == requestedSid;
				});
			if (found == credentials.end())
				return BrokerCredentialSelectionOutcome::NotFound;
			selectedIndex = static_cast<int>(std::distance(credentials.begin(), found));
		}
		else if (credentials.size() == 1)
		{
			selectedIndex = 0;
		}
		else
		{
			selectedIndex = credential_selection_dialog::ShowCredentialSelectionDialog(
				credentials, selectionMode);
			if (selectedIndex < 0)
				return BrokerCredentialSelectionOutcome::Cancelled;
		}

		selectedCredential = std::move(
			credentials[static_cast<std::size_t>(selectedIndex)]);
		error = NO_ERROR;
		return BrokerCredentialSelectionOutcome::Selected;
		}
	}
}