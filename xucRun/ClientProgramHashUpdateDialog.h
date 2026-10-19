// Purpose: Defines the selection dialog for scope of a detected program hash update.
// Behavior: Shows signature-dependent program details and returns the chosen account scope.
#pragma once

#include <string>

namespace xucrun::program_hash_update_dialog
{
	enum class ProgramHashUpdateChoice
	{
		AllAccounts,
		CurrentAccount,
		Cancel
	};

	ProgramHashUpdateChoice ShowProgramHashUpdateDialog(
		const std::wstring& message, bool signatureValid);
}