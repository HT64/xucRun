// Purpose: Defines the client-side selection dialog for stored credentials.
// Approach: Exposes the declarations, constants, and types consumed by the components that provide this behavior.
// Behavior: Sorts accounts for display and returns the index in the original input vector.
#pragma once

#include <cstddef>
#include <vector>

#include "ClientCredentialFormatting.h"
#include "ClientCredentialInfo.h"

namespace xucrun::credential_selection_dialog
{
	int ShowCredentialSelectionDialog(
		const std::vector<CredentialInfo>& credentials,
		credential_formatting::CredentialSelectionMode mode =
			credential_formatting::CredentialSelectionMode::Launch,
		const std::vector<std::size_t>* approvalCounts = nullptr);
}