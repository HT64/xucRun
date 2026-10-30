// Purpose: Declares machine-bound, opaque target names for broker stores.
// Approach: Exposes the declarations, constants, and types consumed by the components that provide this behavior.
// Separates credential, approval, and authenticity-marker keys through distinct purposes.
#pragma once

#include <Windows.h>

#include <string>

namespace xucrun::broker_targets
{
	enum class TargetKind
	{
		Credential,
		Approval,
		AuthenticityMarker
	};

	bool GetMachineScopedPrefix(TargetKind kind, std::wstring& prefix, DWORD& error);
	bool GetCurrentApprovalPrefix(std::wstring& prefix, DWORD& error);
}