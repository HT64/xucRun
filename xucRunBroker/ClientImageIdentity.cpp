// Purpose: Determines and verifies the path, SHA-256 hash, and signature of a broker client.
// Locks the process-reported file and its protected path during authorization.
#include "ClientImageIdentity.h"

#include "FileIdentity.h"
#include "SecurityAclAudit.h"

#include <Softpub.h>
#include <wintrust.h>

#include <algorithm>
#include <filesystem>
#include <vector>

namespace
{
	std::shared_ptr<void> OwnHandle(HANDLE handle)
	{
		return std::shared_ptr<void>(handle, [](void* value) {
			if (value && value != INVALID_HANDLE_VALUE)
				CloseHandle(static_cast<HANDLE>(value));
		});
	}

	bool BindProtectedPath(ClientImageIdentity& identity, DWORD& error)
	{
		constexpr DWORD FileWriteMask = FILE_WRITE_DATA | FILE_APPEND_DATA |
			FILE_WRITE_EA | FILE_WRITE_ATTRIBUTES | DELETE | WRITE_DAC |
			WRITE_OWNER;
		constexpr DWORD InstallationDirectoryWriteMask = FILE_ADD_FILE |
			FILE_ADD_SUBDIRECTORY | FILE_WRITE_EA | FILE_WRITE_ATTRIBUTES |
			FILE_DELETE_CHILD | DELETE | WRITE_DAC | WRITE_OWNER;
		constexpr DWORD AncestorReplacementMask = FILE_DELETE_CHILD |
			WRITE_DAC | WRITE_OWNER;
		if (!IsFileSystemObjectProtectedForBrokerAuthorization(
			static_cast<HANDLE>(identity.imageLock.get()),
			FileWriteMask, error))
		{
			identity.pathProtectionFailurePath = identity.path;
			return false;
		}

		std::filesystem::path directory =
			std::filesystem::path(identity.path).parent_path();
		bool installationDirectory = true;
		while (!directory.empty())
		{
			HANDLE handle = CreateFileW(directory.c_str(),
				FILE_READ_ATTRIBUTES | READ_CONTROL, FILE_SHARE_READ,
				nullptr, OPEN_EXISTING,
				FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
			if (handle == INVALID_HANDLE_VALUE)
			{
				error = GetLastError();
				identity.pathProtectionFailurePath = directory.wstring();
				return false;
			}
			std::shared_ptr<void> lock = OwnHandle(handle);
			std::wstring finalDirectory;
			if (!xucrun::files::ResolveFinalFilePathFromHandle(handle, finalDirectory) ||
				_wcsicmp(finalDirectory.c_str(), directory.c_str()) != 0 ||
				!IsFileSystemObjectProtectedForBrokerAuthorization(handle,
					installationDirectory
					? InstallationDirectoryWriteMask : AncestorReplacementMask, error))
			{
				if (error == NO_ERROR)
					error = ERROR_ACCESS_DENIED;
				identity.pathProtectionFailurePath = directory.wstring();
				return false;
			}
			identity.directoryLocks.push_back(std::move(lock));
			const std::filesystem::path parent = directory.parent_path();
			if (parent == directory)
				break;
			directory = parent;
			installationDirectory = false;
		}
		return true;
	}

	bool QueryProcessImagePath(HANDLE process, std::wstring& path, DWORD& error)
	{
		std::vector<wchar_t> buffer(32768);
		DWORD length = static_cast<DWORD>(buffer.size());
		if (!QueryFullProcessImageNameW(process, 0, buffer.data(), &length) || length == 0)
		{
			error = GetLastError();
			return false;
		}
		path.assign(buffer.data(), length);
		return true;
	}
}

// Validates an open image by final path, hash, and Authenticode signature.
bool ReadFileImageIdentity(
	const std::wstring& path,
	ClientImageIdentity& identity,
	DWORD& windowsError,
	bool bindProtectedPath)
{
	identity = {};
	windowsError = NO_ERROR;
	HANDLE image = CreateFileW(path.c_str(),
		GENERIC_READ | (bindProtectedPath ? READ_CONTROL : 0),
		bindProtectedPath
			? FILE_SHARE_READ
			: FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN |
			(bindProtectedPath ? FILE_FLAG_OPEN_REPARSE_POINT : 0), nullptr);
	if (image == INVALID_HANDLE_VALUE)
	{
		windowsError = GetLastError();
		return false;
	}

	identity.imageLock = OwnHandle(image);
	FILE_ID_INFO fileId = {};
	bool success = xucrun::files::ResolveFinalFilePathFromHandle(image, identity.path) &&
		xucrun::files::ComputeFileSHA256FromHandle(image, identity.sha256);
	if (success)
	{
		if (GetFileInformationByHandleEx(image, FileIdInfo, &fileId, sizeof(fileId)))
		{
			identity.volumeSerialNumber = fileId.VolumeSerialNumber;
			std::copy(std::begin(fileId.FileId.Identifier),
				std::end(fileId.FileId.Identifier), identity.fileId.begin());
			identity.fileIdentityRead = true;
		}
		if (bindProtectedPath)
		{
			identity.protectedPath = BindProtectedPath(
				identity, identity.pathProtectionError);
			if (!identity.protectedPath)
				identity.directoryLocks.clear();
		}
	}
	if (!success)
		windowsError = GetLastError();

	if (success)
	{
		WINTRUST_FILE_INFO fileInfo = {};
		fileInfo.cbStruct = sizeof(fileInfo);
		fileInfo.pcwszFilePath = identity.path.c_str();
		fileInfo.hFile = image;

		WINTRUST_DATA trustData = {};
		trustData.cbStruct = sizeof(trustData);
		trustData.dwUIChoice = WTD_UI_NONE;
		trustData.fdwRevocationChecks = WTD_REVOKE_NONE;
		trustData.dwUnionChoice = WTD_CHOICE_FILE;
		trustData.pFile = &fileInfo;
		trustData.dwStateAction = WTD_STATEACTION_VERIFY;
		trustData.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL | WTD_SAFER_FLAG;
		GUID policy = WINTRUST_ACTION_GENERIC_VERIFY_V2;
		identity.trustStatus = WinVerifyTrust(nullptr, &policy, &trustData);
		identity.signatureTrusted = identity.trustStatus == ERROR_SUCCESS;
		if (identity.signatureTrusted)
		{
			const CRYPT_PROVIDER_DATA* providerData =
				WTHelperProvDataFromStateData(trustData.hWVTStateData);
			const CRYPT_PROVIDER_SGNR* signer = providerData
				? WTHelperGetProvSignerFromChain(
					const_cast<CRYPT_PROVIDER_DATA*>(providerData), 0, FALSE, 0)
				: nullptr;
			if (signer && signer->csCertChain > 0 &&
				signer->pasCertChain[0].pCert)
			{
				DWORD fingerprintSize = 0;
				if (CertGetCertificateContextProperty(
					signer->pasCertChain[0].pCert, CERT_SHA256_HASH_PROP_ID,
					nullptr, &fingerprintSize) && fingerprintSize == 32)
				{
					identity.signerCertificateSha256.resize(fingerprintSize);
					if (!CertGetCertificateContextProperty(
						signer->pasCertChain[0].pCert, CERT_SHA256_HASH_PROP_ID,
						identity.signerCertificateSha256.data(), &fingerprintSize))
					{
						identity.signerCertificateSha256.clear();
					}
				}
			}
		}
		trustData.dwStateAction = WTD_STATEACTION_CLOSE;
		WinVerifyTrust(nullptr, &policy, &trustData);
	}

	if (!bindProtectedPath)
		identity.imageLock.reset();
	return success;
}

// Determines the process path and validates the subsequently opened image.
bool ReadClientImageIdentity(
	HANDLE process,
	ClientImageIdentity& identity,
	DWORD& windowsError)
{
	identity = {};
	windowsError = NO_ERROR;
	if (!process || process == INVALID_HANDLE_VALUE)
		return false;
	if (WaitForSingleObject(process, 0) != WAIT_TIMEOUT)
	{
		windowsError = ERROR_PROCESS_ABORTED;
		return false;
	}

	std::wstring processPathBefore;
	std::wstring processPathAfter;
	if (!QueryProcessImagePath(process, processPathBefore, windowsError) ||
		!ReadFileImageIdentity(processPathBefore, identity, windowsError, true) ||
		!QueryProcessImagePath(process, processPathAfter, windowsError))
	{
		return false;
	}
	std::wstring finalPathAfter;
	if (WaitForSingleObject(process, 0) != WAIT_TIMEOUT ||
		_wcsicmp(processPathBefore.c_str(), processPathAfter.c_str()) != 0 ||
		_wcsicmp(processPathAfter.c_str(), identity.path.c_str()) != 0 ||
		!xucrun::files::ResolveFinalFilePathFromHandle(
			static_cast<HANDLE>(identity.imageLock.get()), finalPathAfter) ||
		_wcsicmp(identity.path.c_str(), finalPathAfter.c_str()) != 0)
	{
		identity = {};
		windowsError = ERROR_ACCESS_DENIED;
		return false;
	}
	return true;
}

// Opens the current process with the minimum rights required for the same identity validation.
bool ReadCurrentProcessImageIdentity(
	ClientImageIdentity& identity,
	DWORD& windowsError)
{
	HANDLE process = OpenProcess(
		PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE,
		FALSE, GetCurrentProcessId());
	if (!process)
	{
		windowsError = GetLastError();
		return false;
	}
	const bool read = ReadClientImageIdentity(process, identity, windowsError);
	CloseHandle(process);
	return read;
}

// Compares two previously verified image identities by SHA-256 certificate thumbprint.
bool HaveMatchingTrustedSigners(
	const ClientImageIdentity& first,
	const ClientImageIdentity& second)
{
	return first.signatureTrusted && second.signatureTrusted &&
		first.signerCertificateSha256.size() == 32 &&
		second.signerCertificateSha256.size() == 32 &&
		std::equal(first.signerCertificateSha256.begin(),
			first.signerCertificateSha256.end(),
			second.signerCertificateSha256.begin());
}