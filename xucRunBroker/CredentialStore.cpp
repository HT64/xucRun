// Purpose: Stores passwords exclusively in the broker's LocalSystem credential store.
// Additionally protects payload data with DPAPI-NG and clears plaintext memory deterministically.
#include "CredentialStore.h"

#include <ncryptprotect.h>
#include <wincred.h>

#include <mutex>

namespace
{
	constexpr std::uint8_t EmptySecretMarker[] = {
		0x58, 0x55, 0x43, 0x52, 0x55, 0x4E, 0x2D, 0x45,
		0x4D, 0x50, 0x54, 0x59, 0x2D, 0x56, 0x31
	};

	// Overwrites sensitive bytes before releasing the buffer.
	void Erase(std::vector<std::uint8_t>& value) noexcept
	{
		if (!value.empty())
			SecureZeroMemory(value.data(), value.size());
		value.clear();
	}

	// Validates credential target names for permitted length and embedded null characters.
	bool ValidateTarget(const std::wstring& target, DWORD& error)
	{
		if (target.empty() || target.size() > CRED_MAX_GENERIC_TARGET_NAME_LENGTH ||
			target.find(L'\0') != std::wstring::npos)
		{
			error = ERROR_INVALID_PARAMETER;
			return false;
		}
		return true;
	}

	// Reads only the protected credential blob from Windows Credential Manager.
	bool ReadProtectedInternal(const std::wstring& target,
		std::vector<std::uint8_t>& protectedSecret, DWORD& error)
	{
		PCREDENTIALW credential = nullptr;
		if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &credential))
		{
			error = GetLastError();
			return false;
		}
		if (!credential->CredentialBlob || credential->CredentialBlobSize == 0)
		{
			CredFree(credential);
			error = ERROR_INVALID_DATA;
			return false;
		}
		protectedSecret.assign(credential->CredentialBlob,
			credential->CredentialBlob + credential->CredentialBlobSize);
		CredFree(credential);
		if (protectedSecret.empty())
		{
			error = ERROR_INVALID_DATA;
			return false;
		}
		return true;
	}

	// Decrypts a DPAPI-NG blob and clears the API output buffer immediately afterward.
	bool Unprotect(std::span<const std::uint8_t> protectedSecret,
		std::vector<std::uint8_t>& secret, DWORD& error)
	{
		NCRYPT_DESCRIPTOR_HANDLE descriptor = nullptr;
		PBYTE output = nullptr;
		ULONG outputSize = 0;
		const SECURITY_STATUS status = NCryptUnprotectSecret(&descriptor, 0,
			protectedSecret.data(), static_cast<ULONG>(protectedSecret.size()),
			nullptr, nullptr, &output, &outputSize);
		if (descriptor)
			NCryptCloseProtectionDescriptor(descriptor);
		if (status != ERROR_SUCCESS || !output || outputSize == 0)
		{
			if (output)
			{
				SecureZeroMemory(output, outputSize);
				LocalFree(output);
			}
			error = status == ERROR_SUCCESS
				? ERROR_INVALID_DATA
				: static_cast<DWORD>(status);
			return false;
		}
		secret.assign(output, output + outputSize);
		SecureZeroMemory(output, outputSize);
		LocalFree(output);
		return true;
	}
}

namespace xucrun::broker_store
{
	std::recursive_mutex& TransactionMutex()
	{
		static std::recursive_mutex mutex;
		return mutex;
	}
	// Validates the effective store context without reading credentials or other persistent data.
	bool InspectAccessContext(AccessContextInspection& inspection, DWORD& error)
	{
		inspection = {};
		error = NO_ERROR;
		HANDLE threadToken = nullptr;
		if (OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &threadToken))
		{
			CloseHandle(threadToken);
		}
		else if (GetLastError() == ERROR_NO_TOKEN)
		{
			inspection.noThreadImpersonation = true;
		}
		else
		{
			error = GetLastError();
			return false;
		}

		HANDLE processToken = nullptr;
		if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &processToken))
		{
			error = GetLastError();
			return false;
		}
		DWORD size = 0;
		GetTokenInformation(processToken, TokenUser, nullptr, 0, &size);
		std::vector<std::uint8_t> tokenUser(size);
		const bool read = size != 0 && GetTokenInformation(processToken, TokenUser,
			tokenUser.data(), size, &size) == TRUE;
		error = read ? NO_ERROR : GetLastError();
		CloseHandle(processToken);
		if (!read)
			return false;

		const auto* user = reinterpret_cast<const TOKEN_USER*>(tokenUser.data());
		inspection.processIsLocalSystem = IsWellKnownSid(
			user->User.Sid, WinLocalSystemSid) == TRUE;
		inspection.accessPermitted = inspection.noThreadImpersonation &&
			inspection.processIsLocalSystem;
		return true;
	}

	// Enforces a direct LocalSystem context and rejects impersonated or foreign callers.
	bool EnsureLocalSystemContext(DWORD& error)
	{
		AccessContextInspection inspection;
		if (!InspectAccessContext(inspection, error))
			return false;
		if (!inspection.accessPermitted)
			error = ERROR_ACCESS_DENIED;
		return inspection.accessPermitted;
	}

	// Checks whether a validly named credential exists in the LocalSystem store.
	bool Exists(const std::wstring& target, bool& exists, DWORD& error)
	{
		std::lock_guard lock(TransactionMutex());
		exists = false;
		if (!ValidateTarget(target, error) || !EnsureLocalSystemContext(error))
			return false;
		PCREDENTIALW credential = nullptr;
		if (CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &credential))
		{
			CredFree(credential);
			exists = true;
			error = NO_ERROR;
			return true;
		}
		error = GetLastError();
		if (error != ERROR_NOT_FOUND)
			return false;
		error = NO_ERROR;
		return true;
	}

	// Encrypts a secret for LocalSystem and never stores plaintext in Credential Manager.
	bool Write(const std::wstring& target, std::span<const std::uint8_t> secret,
		DWORD& error)
	{
		std::lock_guard lock(TransactionMutex());
		if (!ValidateTarget(target, error) ||
			secret.size() > CRED_MAX_CREDENTIAL_BLOB_SIZE ||
			!EnsureLocalSystemContext(error))
		{
			if (secret.size() > CRED_MAX_CREDENTIAL_BLOB_SIZE)
				error = ERROR_INVALID_PARAMETER;
			return false;
		}
		const std::span<const std::uint8_t> protectedInput = secret.empty()
			? std::span<const std::uint8_t>(EmptySecretMarker)
			: secret;
		NCRYPT_DESCRIPTOR_HANDLE descriptor = nullptr;
		SECURITY_STATUS status = NCryptCreateProtectionDescriptor(
			L"LOCAL=user", 0, &descriptor);
		if (status != ERROR_SUCCESS)
		{
			error = static_cast<DWORD>(status);
			return false;
		}
		PBYTE output = nullptr;
		ULONG outputSize = 0;
		status = NCryptProtectSecret(descriptor, 0, protectedInput.data(),
			static_cast<ULONG>(protectedInput.size()), nullptr, nullptr, &output, &outputSize);
		NCryptCloseProtectionDescriptor(descriptor);
		if (status != ERROR_SUCCESS || !output || outputSize == 0 ||
			outputSize > CRED_MAX_CREDENTIAL_BLOB_SIZE)
		{
			if (output)
			{
				SecureZeroMemory(output, outputSize);
				LocalFree(output);
			}
			error = status == ERROR_SUCCESS
				? ERROR_BUFFER_OVERFLOW
				: static_cast<DWORD>(status);
			return false;
		}

		CREDENTIALW credential = {};
		credential.Type = CRED_TYPE_GENERIC;
		credential.TargetName = const_cast<LPWSTR>(target.c_str());
		credential.CredentialBlobSize = outputSize;
		credential.CredentialBlob = output;
		credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
		credential.UserName = const_cast<LPWSTR>(L"");
		const bool written = CredWriteW(&credential, 0) == TRUE;
		error = written ? NO_ERROR : GetLastError();
		SecureZeroMemory(output, outputSize);
		LocalFree(output);
		return written;
	}

	// Reads and decrypts a credential only in the LocalSystem context and clears intermediate buffers.
	bool Read(const std::wstring& target, std::vector<std::uint8_t>& secret,
		DWORD& error)
	{
		std::lock_guard lock(TransactionMutex());
		Erase(secret);
		if (!ValidateTarget(target, error) || !EnsureLocalSystemContext(error))
			return false;
		std::vector<std::uint8_t> protectedSecret;
		const bool success = ReadProtectedInternal(target, protectedSecret, error) &&
			Unprotect(protectedSecret, secret, error);
		Erase(protectedSecret);
		if (success && secret.size() == std::size(EmptySecretMarker) &&
			std::equal(secret.begin(), secret.end(), std::begin(EmptySecretMarker)))
		{
			Erase(secret);
		}
		return success;
	}

	bool ReadProtected(const std::wstring& target,
		std::vector<std::uint8_t>& protectedSecret, DWORD& error)
	{
		std::lock_guard lock(TransactionMutex());
		protectedSecret.clear();
		return ValidateTarget(target, error) && EnsureLocalSystemContext(error) &&
			ReadProtectedInternal(target, protectedSecret, error);
	}

	bool UnprotectForValidation(std::span<const std::uint8_t> protectedSecret,
		std::vector<std::uint8_t>& secret, DWORD& error)
	{
		std::lock_guard lock(TransactionMutex());
		Erase(secret);
		if (protectedSecret.empty() ||
			protectedSecret.size() > CRED_MAX_CREDENTIAL_BLOB_SIZE ||
			!EnsureLocalSystemContext(error))
		{
			if (protectedSecret.empty() ||
				protectedSecret.size() > CRED_MAX_CREDENTIAL_BLOB_SIZE)
				error = ERROR_INVALID_PARAMETER;
			return false;
		}
		if (!Unprotect(protectedSecret, secret, error))
			return false;
		if (secret.size() == std::size(EmptySecretMarker) &&
			std::equal(secret.begin(), secret.end(), std::begin(EmptySecretMarker)))
		{
			Erase(secret);
		}
		error = NO_ERROR;
		return true;
	}

	bool WriteProtected(const std::wstring& target,
		std::span<const std::uint8_t> protectedSecret, DWORD& error)
	{
		std::lock_guard lock(TransactionMutex());
		if (!ValidateTarget(target, error) || protectedSecret.empty() ||
			protectedSecret.size() > CRED_MAX_CREDENTIAL_BLOB_SIZE ||
			!EnsureLocalSystemContext(error))
		{
			if (protectedSecret.empty() ||
				protectedSecret.size() > CRED_MAX_CREDENTIAL_BLOB_SIZE)
				error = ERROR_INVALID_PARAMETER;
			return false;
		}
		CREDENTIALW credential = {};
		credential.Type = CRED_TYPE_GENERIC;
		credential.TargetName = const_cast<LPWSTR>(target.c_str());
		credential.CredentialBlobSize = static_cast<DWORD>(protectedSecret.size());
		credential.CredentialBlob = const_cast<LPBYTE>(protectedSecret.data());
		credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
		credential.UserName = const_cast<LPWSTR>(L"");
		const bool written = CredWriteW(&credential, 0) == TRUE;
		error = written ? NO_ERROR : GetLastError();
		return written;
	}

	// Reads a credential's modification time without exposing its secret.
	bool ReadLastWritten(const std::wstring& target, std::uint64_t& lastWritten,
		DWORD& error)
	{
		std::lock_guard lock(TransactionMutex());
		lastWritten = 0;
		if (!ValidateTarget(target, error) || !EnsureLocalSystemContext(error))
			return false;
		PCREDENTIALW credential = nullptr;
		if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &credential))
		{
			error = GetLastError();
			return false;
		}
		ULARGE_INTEGER value = {};
		value.LowPart = credential->LastWritten.dwLowDateTime;
		value.HighPart = credential->LastWritten.dwHighDateTime;
		lastWritten = value.QuadPart;
		CredFree(credential);
		error = NO_ERROR;
		return true;
	}

	// Deletes a validly named credential exclusively in the LocalSystem context.
	bool Delete(const std::wstring& target, DWORD& error)
	{
		std::lock_guard lock(TransactionMutex());
		if (!ValidateTarget(target, error) || !EnsureLocalSystemContext(error))
			return false;
		if (!CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0))
		{
			error = GetLastError();
			return false;
		}
		error = NO_ERROR;
		return true;
	}

	// Lists credential targets for a validated prefix exclusively in the LocalSystem context.
	bool EnumerateTargets(const std::wstring& prefix,
		std::vector<std::wstring>& targets, DWORD& error)
	{
		std::lock_guard lock(TransactionMutex());
		targets.clear();
		if (prefix.empty() || prefix.find(L'\0') != std::wstring::npos ||
			!EnsureLocalSystemContext(error))
		{
			if (prefix.empty() || prefix.find(L'\0') != std::wstring::npos)
				error = ERROR_INVALID_PARAMETER;
			return false;
		}
		DWORD count = 0;
		PCREDENTIALW* credentials = nullptr;
		const std::wstring filter = prefix + L"*";
		if (!CredEnumerateW(filter.c_str(), 0, &count, &credentials))
		{
			error = GetLastError();
			if (error == ERROR_NOT_FOUND)
			{
				error = NO_ERROR;
				return true;
			}
			return false;
		}
		for (DWORD index = 0; index < count; index++)
		{
			if (credentials[index]->TargetName)
				targets.emplace_back(credentials[index]->TargetName);
		}
		CredFree(credentials);
		error = NO_ERROR;
		return true;
	}

	// Deletes all entries for a previously derived machine-bound product prefix.
	bool DeleteAllWithPrefix(const std::wstring& prefix, DWORD& error)
	{
		std::lock_guard lock(TransactionMutex());
		std::vector<std::wstring> targets;
		if (!EnumerateTargets(prefix, targets, error))
			return false;
		for (const std::wstring& target : targets)
		{
			if (!Delete(target, error) && error != ERROR_NOT_FOUND)
				return false;
		}
		error = NO_ERROR;
		return true;
	}

	// Proves under client impersonation that neither store access nor decryption is possible.
	bool VerifyClientCannotDecrypt(HANDLE clientProcess, const std::wstring& target,
		DWORD& error)
	{
		if (!clientProcess || !ValidateTarget(target, error) ||
			!EnsureLocalSystemContext(error))
		{
			if (!clientProcess)
				error = ERROR_INVALID_HANDLE;
			return false;
		}
		std::vector<std::uint8_t> protectedSecret;
		if (!ReadProtectedInternal(target, protectedSecret, error))
			return false;

		HANDLE sourceToken = nullptr;
		HANDLE impersonationToken = nullptr;
		bool isolated = OpenProcessToken(clientProcess,
			TOKEN_QUERY | TOKEN_DUPLICATE, &sourceToken) == TRUE;
		if (!isolated)
			error = GetLastError();
		if (isolated)
		{
			isolated = DuplicateTokenEx(sourceToken, TOKEN_QUERY | TOKEN_IMPERSONATE,
				nullptr, SecurityImpersonation, TokenImpersonation,
				&impersonationToken) == TRUE;
			if (!isolated)
				error = GetLastError();
		}
		if (sourceToken)
			CloseHandle(sourceToken);
		if (isolated && !SetThreadToken(nullptr, impersonationToken))
		{
			isolated = false;
			error = GetLastError();
		}
		std::vector<std::uint8_t> plaintext;
		if (isolated)
		{
			std::vector<std::uint8_t> storePlaintext;
			DWORD storeError = NO_ERROR;
			const bool storeRead = xucrun::broker_store::Read(
				target, storePlaintext, storeError);
			DWORD unprotectError = NO_ERROR;
			const bool decrypted = Unprotect(protectedSecret, plaintext, unprotectError);
			const DWORD revertError = RevertToSelf() ? NO_ERROR : GetLastError();
			isolated = !storeRead && storeError == ERROR_ACCESS_DENIED &&
				!decrypted && revertError == NO_ERROR;
			error = isolated ? NO_ERROR
				: (revertError != NO_ERROR ? revertError : ERROR_ACCESS_DENIED);
			Erase(storePlaintext);
		}
		if (impersonationToken)
			CloseHandle(impersonationToken);
		Erase(plaintext);
		Erase(protectedSecret);
		return isolated;
	}
}