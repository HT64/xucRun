// Purpose: Declares the ACL audit of security-critical broker resources.
// Approach: Exposes the declarations, constants, and types consumed by the components that provide this behavior.
// Provides structured findings with the object, problem, and concrete remediation.
#pragma once

#include <Windows.h>

#include <string>
#include <vector>

struct SecurityAclFinding
{
	std::wstring objectName;
	std::wstring problem;
	std::wstring remediation;
};

struct SecurityAclSignatureInspection
{
	std::wstring path;
	DWORD windowsError = NO_ERROR;
	LONG trustStatus = TRUST_E_NOSIGNATURE;
	bool identityRead = false;
	bool signatureTrusted = false;
	bool signerMatches = false;
	bool fileIdentityRead = false;
	bool protectedPath = false;
	DWORD pathProtectionError = NO_ERROR;
};

struct SecurityAclAuditCoverage
{
	struct CredentialStoreContext
	{
		std::wstring serviceAccount;
		DWORD contextError = NO_ERROR;
		DWORD serviceError = NO_ERROR;
		bool contextInspected = false;
		bool processIsLocalSystem = false;
		bool noThreadImpersonation = false;
		bool accessPermitted = false;
		bool serviceConfigurationRead = false;
		bool serviceAccountIsLocalSystem = false;
	} credentialStoreContext;

	std::vector<std::wstring> installationObjects;
	std::vector<std::wstring> storeDirectories;
	std::vector<SecurityAclSignatureInspection> signatureInspections;
};

std::vector<SecurityAclFinding> AuditBrokerSecurityAcls(bool german = true,
	SecurityAclAuditCoverage* coverage = nullptr,
	bool inspectCredentialStoreContext = false);
bool IsFileSystemObjectProtectedForBrokerAuthorization(
	HANDLE object, DWORD unexpectedWriteMask, DWORD& error);
bool RunSecurityAclAuditSelfTest();