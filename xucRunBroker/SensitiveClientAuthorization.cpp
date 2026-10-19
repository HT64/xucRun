// Purpose: Loads and validates the installed policy for sensitive broker clients.
// Validates the path and signature binding.
#include "SensitiveClientAuthorization.h"

#include "BrokerPipeClient.h"
#include "FileIdentity.h"

#include <aclapi.h>
#include <sddl.h>

#include <algorithm>

namespace
{
	constexpr wchar_t ImagePathValue[] = L"SensitiveClientImagePath";
	constexpr wchar_t SignerValue[] = L"SensitiveClientSignerSha256";

	// Reads the configured client path as a complete, non-empty registry value.
	bool ReadConfiguredPath(HKEY key, std::wstring& path, DWORD& error)
	{
		DWORD type = 0;
		DWORD size = 0;
		LONG status = RegGetValueW(key, nullptr, ImagePathValue,
			RRF_RT_REG_SZ, &type, nullptr, &size);
		if (status != ERROR_SUCCESS || size < sizeof(wchar_t) ||
			(size % sizeof(wchar_t)) != 0)
		{
			error = status == ERROR_SUCCESS ? ERROR_INVALID_DATA : status;
			return false;
		}
		std::vector<wchar_t> value(size / sizeof(wchar_t));
		status = RegGetValueW(key, nullptr, ImagePathValue,
			RRF_RT_REG_SZ, &type, value.data(), &size);
		if (status != ERROR_SUCCESS || value.empty() || value.back() != L'\0' ||
			value.front() == L'\0')
		{
			error = status == ERROR_SUCCESS ? ERROR_INVALID_DATA : status;
			return false;
		}
		path.assign(value.data());
		return true;
	}

	// Reads exactly one SHA-256 thumbprint as the signer binding of the client policy.
	bool ReadConfiguredSigner(HKEY key, std::vector<std::uint8_t>& signer,
		DWORD& error)
	{
		DWORD type = 0;
		DWORD size = 0;
		LONG status = RegGetValueW(key, nullptr, SignerValue,
			RRF_RT_REG_BINARY, &type, nullptr, &size);
		if (status != ERROR_SUCCESS || size != 32)
		{
			error = status == ERROR_SUCCESS ? ERROR_INVALID_DATA : status;
			return false;
		}
		signer.resize(size);
		status = RegGetValueW(key, nullptr, SignerValue,
			RRF_RT_REG_BINARY, &type, signer.data(), &size);
		if (status != ERROR_SUCCESS || size != signer.size())
		{
			signer.clear();
			error = status == ERROR_SUCCESS ? ERROR_INVALID_DATA : status;
			return false;
		}
		return true;
	}

	// Resolves the configured client path canonically through a file handle.
	bool ResolveConfiguredPath(const std::wstring& configuredPath,
		std::wstring& finalPath, DWORD& error)
	{
		HANDLE image = CreateFileW(configuredPath.c_str(), GENERIC_READ,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
		if (image == INVALID_HANDLE_VALUE)
		{
			error = GetLastError();
			return false;
		}
		const bool resolved = xucrun::files::ResolveFinalFilePathFromHandle(
			image, finalPath);
		error = resolved ? NO_ERROR : GetLastError();
		CloseHandle(image);
		return resolved;
	}

	bool RunUnprotectedImageBindingSelfTest()
	{
		wchar_t temporaryDirectory[MAX_PATH] = {};
		wchar_t temporaryFile[MAX_PATH] = {};
		if (!GetTempPathW(static_cast<DWORD>(std::size(temporaryDirectory)),
			temporaryDirectory) ||
			!GetTempFileNameW(temporaryDirectory, L"xui", 0, temporaryFile))
		{
			return false;
		}
		PSECURITY_DESCRIPTOR descriptor = nullptr;
		PACL dacl = nullptr;
		BOOL daclPresent = FALSE;
		BOOL daclDefaulted = FALSE;
		const bool descriptorReady =
			ConvertStringSecurityDescriptorToSecurityDescriptorW(
				L"D:P(A;;GA;;;WD)", SDDL_REVISION_1, &descriptor, nullptr) == TRUE &&
			GetSecurityDescriptorDacl(descriptor, &daclPresent,
				&dacl, &daclDefaulted) == TRUE && daclPresent && dacl;
		const DWORD aclStatus = descriptorReady
			? SetNamedSecurityInfoW(temporaryFile, SE_FILE_OBJECT,
				DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
				nullptr, nullptr, dacl, nullptr)
			: ERROR_INVALID_SECURITY_DESCR;
		if (descriptor)
			LocalFree(descriptor);
		if (aclStatus != ERROR_SUCCESS)
		{
			DeleteFileW(temporaryFile);
			return false;
		}

		ClientImageIdentity identity;
		DWORD error = NO_ERROR;
		const bool identityRead = ReadFileImageIdentity(
			temporaryFile, identity, error, true);
		SetLastError(NO_ERROR);
		const bool deleteBlocked = !DeleteFileW(temporaryFile) &&
			GetLastError() == ERROR_SHARING_VIOLATION;
		const bool rejected = identityRead && !identity.protectedPath &&
			identity.pathProtectionError == ERROR_ACCESS_DENIED && deleteBlocked;
		identity = {};
		const bool deleted = DeleteFileW(temporaryFile) == TRUE;
		return rejected && deleted;
	}

	bool EvaluateTrayCompanionPolicy(const ClientImageIdentity& identity,
		const SensitiveClientPolicy& policy, DWORD& error)
	{
		const std::size_t separator = policy.imagePath.find_last_of(L"\\/");
		if (separator == std::wstring::npos)
		{
			error = ERROR_INVALID_DATA;
			return false;
		}
		const std::wstring trayPath =
			policy.imagePath.substr(0, separator + 1) + L"xucRunTray.exe";
		error = ERROR_ACCESS_DENIED;
		if (!identity.signatureTrusted || !identity.fileIdentityRead ||
			!identity.protectedPath ||
			_wcsicmp(identity.path.c_str(), trayPath.c_str()) != 0 ||
			identity.signerCertificateSha256.size() !=
				policy.signerCertificateSha256.size() ||
			!std::equal(identity.signerCertificateSha256.begin(),
				identity.signerCertificateSha256.end(),
				policy.signerCertificateSha256.begin()))
		{
			return false;
		}
		error = NO_ERROR;
		return true;
	}
}

// Loads the protected client policy and binds it to the final image path.
bool LoadSensitiveClientPolicy(SensitiveClientPolicy& policy, DWORD& error)
{
	policy = {};
	error = NO_ERROR;
	HKEY key = nullptr;
	const LONG status = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
		xucrun::broker::BrokerServiceParametersRegistryPath.c_str(), 0,
		KEY_QUERY_VALUE, &key);
	if (status != ERROR_SUCCESS)
	{
		error = status;
		return false;
	}
	std::wstring configuredPath;
	const bool loaded = ReadConfiguredPath(key, configuredPath, error) &&
		ReadConfiguredSigner(key, policy.signerCertificateSha256, error);
	RegCloseKey(key);
	if (!loaded)
		return false;
	if (!ResolveConfiguredPath(configuredPath, policy.imagePath, error))
	{
		policy = {};
		return false;
	}
	return true;
}

// Authorizes only a trusted signed image with an exactly matching path and signer hash.
bool EvaluateSensitiveClientPolicy(const ClientImageIdentity& identity,
	const SensitiveClientPolicy& policy, DWORD& error)
{
	error = ERROR_ACCESS_DENIED;
	if (!identity.signatureTrusted || !identity.fileIdentityRead ||
		!identity.protectedPath || identity.path.empty() ||
		policy.imagePath.empty() || identity.signerCertificateSha256.size() != 32 ||
		policy.signerCertificateSha256.size() != 32 ||
		_wcsicmp(identity.path.c_str(), policy.imagePath.c_str()) != 0 ||
		!std::equal(identity.signerCertificateSha256.begin(),
			identity.signerCertificateSha256.end(),
			policy.signerCertificateSha256.begin()))
	{
		return false;
	}
	error = NO_ERROR;
	return true;
}

bool IsCoreClientAuthorized(const ClientImageIdentity& identity, DWORD& error)
{
	SensitiveClientPolicy policy;
	if (!LoadSensitiveClientPolicy(policy, error))
		return false;
	return EvaluateSensitiveClientPolicy(identity, policy, error);
}

bool IsTrayCompanionAuthorized(const ClientImageIdentity& identity, DWORD& error)
{
	SensitiveClientPolicy policy;
	if (!LoadSensitiveClientPolicy(policy, error))
		return false;
	return EvaluateTrayCompanionPolicy(identity, policy, error);
}

// Handler-level validation; the dispatcher enforces the tray request allowlist.
bool IsSensitiveClientAuthorized(const ClientImageIdentity& identity, DWORD& error)
{
	if (IsCoreClientAuthorized(identity, error))
		return true;
	return IsTrayCompanionAuthorized(identity, error);
}

// Authorizes only the elevated, installed helper beside the configured client.
bool IsApprovalHelperAuthorized(const ClientImageIdentity& identity,
	DWORD integrityRid, DWORD& error)
{
	error = ERROR_ACCESS_DENIED;
	if (integrityRid < SECURITY_MANDATORY_HIGH_RID)
		return false;
#ifdef _DEBUG
	// Debug diagnostics intentionally allow a locally built elevated helper by name.
	const std::size_t debugSeparator = identity.path.find_last_of(L"\\/");
	const std::wstring debugFileName = debugSeparator == std::wstring::npos
		? identity.path : identity.path.substr(debugSeparator + 1);
	if (_wcsicmp(debugFileName.c_str(), L"xucRunApproval.exe") == 0)
	{
		error = NO_ERROR;
		return true;
	}
#endif
	SensitiveClientPolicy clientPolicy;
	if (!LoadSensitiveClientPolicy(clientPolicy, error))
		return false;
	const std::size_t separator = clientPolicy.imagePath.find_last_of(L"\\/");
	if (separator == std::wstring::npos)
	{
		error = ERROR_INVALID_DATA;
		return false;
	}
	const std::wstring configuredHelperPath =
		clientPolicy.imagePath.substr(0, separator + 1) + L"xucRunApproval.exe";
	ClientImageIdentity installedHelper;
	if (!ReadFileImageIdentity(configuredHelperPath, installedHelper, error, true))
		return false;
	if (!identity.fileIdentityRead || !identity.protectedPath ||
		!installedHelper.fileIdentityRead || !installedHelper.protectedPath ||
		identity.volumeSerialNumber != installedHelper.volumeSerialNumber ||
		identity.fileId != installedHelper.fileId ||
		_wcsicmp(identity.path.c_str(), installedHelper.path.c_str()) != 0 ||
		identity.sha256 != installedHelper.sha256 ||
		!identity.signatureTrusted ||
		identity.signerCertificateSha256 != clientPolicy.signerCertificateSha256)
	{
		error = ERROR_ACCESS_DENIED;
		return false;
	}
	error = NO_ERROR;
	return true;
}

// Tests positive and negative cases of path, trust, and signer binding.
bool RunSensitiveClientPolicySelfTest()
{
	if (!RunUnprotectedImageBindingSelfTest())
		return false;
	SensitiveClientPolicy policy;
	policy.imagePath = L"C:\\Program Files\\cmdTools\\xucRun.exe";
	policy.signerCertificateSha256.assign(32, 0xA5);
	ClientImageIdentity identity;
	identity.path = L"c:\\program files\\cmdtools\\XUCRUN.EXE";
	identity.signatureTrusted = true;
	identity.protectedPath = true;
	identity.fileIdentityRead = true;
	identity.volumeSerialNumber = 1;
	identity.signerCertificateSha256 = policy.signerCertificateSha256;
	DWORD error = NO_ERROR;
	if (!EvaluateSensitiveClientPolicy(identity, policy, error) || error != NO_ERROR)
		return false;
	identity.path = L"C:\\Program Files\\cmdTools\\xucRunTray.exe";
	if (!EvaluateTrayCompanionPolicy(identity, policy, error) || error != NO_ERROR)
		return false;
	identity.path = L"C:\\Temp\\xucRunTray.exe";
	if (EvaluateTrayCompanionPolicy(identity, policy, error) ||
		error != ERROR_ACCESS_DENIED)
		return false;
	identity.path = policy.imagePath;
	identity.signatureTrusted = false;
	if (EvaluateSensitiveClientPolicy(identity, policy, error) || error != ERROR_ACCESS_DENIED)
		return false;
	identity.signatureTrusted = true;
	identity.protectedPath = false;
	if (EvaluateSensitiveClientPolicy(identity, policy, error) || error != ERROR_ACCESS_DENIED)
		return false;
	identity.protectedPath = true;
	identity.path = L"C:\\Temp\\xucRun.exe";
	if (EvaluateSensitiveClientPolicy(identity, policy, error))
		return false;
	identity.path = policy.imagePath;
	ClientImageIdentity brokerIdentity = identity;
	if (!HaveMatchingTrustedSigners(identity, brokerIdentity))
		return false;
	brokerIdentity.signerCertificateSha256[0] ^= 0xFF;
	if (HaveMatchingTrustedSigners(identity, brokerIdentity))
		return false;
	identity.signerCertificateSha256[0] ^= 0xFF;
	return !EvaluateSensitiveClientPolicy(identity, policy, error) &&
		error == ERROR_ACCESS_DENIED;
}

// Starts the configured client suspended and validates the file determined from its process path.
bool RunSensitiveClientPolicyConfigurationTest(
	DWORD& error, std::wstring* failedProtectionPath)
{
	if (failedProtectionPath)
		failedProtectionPath->clear();
	SensitiveClientPolicy policy;
	if (!LoadSensitiveClientPolicy(policy, error))
		return false;

	STARTUPINFOW startup = {};
	startup.cb = sizeof(startup);
	PROCESS_INFORMATION process = {};
	if (!CreateProcessW(policy.imagePath.c_str(), nullptr, nullptr, nullptr, FALSE,
		CREATE_SUSPENDED, nullptr, nullptr, &startup, &process))
	{
		error = GetLastError();
		return false;
	}

	ClientImageIdentity identity;
	DWORD identityError = NO_ERROR;
	const bool identityRead = ReadClientImageIdentity(
		process.hProcess, identity, identityError);
	TerminateProcess(process.hProcess, ERROR_CANCELLED);
	WaitForSingleObject(process.hProcess, INFINITE);
	CloseHandle(process.hThread);
	CloseHandle(process.hProcess);
	if (!identityRead)
	{
		error = identityError == NO_ERROR ? ERROR_INVALID_DATA : identityError;
		return false;
	}
	if (!identity.protectedPath && failedProtectionPath)
		*failedProtectionPath = identity.pathProtectionFailurePath;
#ifndef _DEBUG
	ClientImageIdentity brokerIdentity;
	DWORD brokerIdentityError = NO_ERROR;
	const bool brokerIdentityRead = ReadCurrentProcessImageIdentity(
		brokerIdentity, brokerIdentityError);
	if (!brokerIdentityRead || !HaveMatchingTrustedSigners(identity, brokerIdentity))
	{
		error = brokerIdentityRead
			? ERROR_ACCESS_DENIED
			: (brokerIdentityError == NO_ERROR ? ERROR_INVALID_DATA : brokerIdentityError);
		return false;
	}
#endif
	return EvaluateSensitiveClientPolicy(identity, policy, error);
}