// Purpose: Declares the installed authorization policy for sensitive broker clients.
// Binds access to the final path and trusted signing certificate.
#pragma once

#include "ClientImageIdentity.h"

#include <Windows.h>

#include <cstdint>
#include <string>
#include <vector>

struct SensitiveClientPolicy
{
	std::wstring imagePath;
	std::vector<std::uint8_t> signerCertificateSha256;
};

bool LoadSensitiveClientPolicy(SensitiveClientPolicy& policy, DWORD& error);
bool EvaluateSensitiveClientPolicy(const ClientImageIdentity& identity,
	const SensitiveClientPolicy& policy, DWORD& error);
bool IsCoreClientAuthorized(const ClientImageIdentity& identity, DWORD& error);
bool IsTrayCompanionAuthorized(const ClientImageIdentity& identity, DWORD& error);
// Handler-level authorization for requests allowed to either trusted image.
// The connection dispatcher separately limits the tray request kinds.
bool IsSensitiveClientAuthorized(const ClientImageIdentity& identity, DWORD& error);
bool IsApprovalHelperAuthorized(const ClientImageIdentity& identity,
	DWORD integrityRid, DWORD& error);
bool RunSensitiveClientPolicySelfTest();
bool RunSensitiveClientPolicyConfigurationTest(
	DWORD& error, std::wstring* failedProtectionPath = nullptr);