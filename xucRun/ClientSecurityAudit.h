// Purpose: Defines the explicit client environment audit invoked by /audit.
// Behavior: Collects localized findings and returns a consolidated trust decision.
#pragma once

#include <string>
#include <vector>

namespace xucrun::security_audit
{
	int RunAuditCommand();

	bool IsCredentialAccessEnvironmentTrusted(std::wstring& reason,
		std::wstring& signatureWarnings,
		std::vector<std::wstring>* checkedImages = nullptr);
}
