// Purpose: Defines client-side metadata for credentials reported by the broker.
// Behavior: Bundles account identity, target name, and approval timestamps for client workflows and dialogs.
#pragma once

#include <Windows.h>

#include <string>

#include "ApprovalSerialization.h"

namespace xucrun
{
	struct CredentialInfo
	{
		std::wstring targetName;
		std::wstring username;
		std::wstring accountSid;
		approval::ApprovalMode approvalMode = approval::ApprovalMode::AskAt;
		ULONGLONG approvalTime = 0;
		ULONGLONG lastWritten = 0;
	};
}