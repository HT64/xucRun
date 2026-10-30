// Purpose: Defines the verified image identity of a connected client process.
// Approach: Exposes the declarations, constants, and types consumed by the components that provide this behavior.
// Retains file and directory locks for sensitive authorization decisions.
#pragma once

#include <Windows.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct ClientImageIdentity
{
	std::wstring path;
	std::vector<std::uint8_t> sha256;
	std::vector<std::uint8_t> signerCertificateSha256;
	std::uint64_t volumeSerialNumber = 0;
	std::array<std::uint8_t, 16> fileId = {};
	LONG trustStatus = TRUST_E_NOSIGNATURE;
	bool signatureTrusted = false;
	bool fileIdentityRead = false;
	bool protectedPath = false;
	DWORD pathProtectionError = NO_ERROR;
	std::wstring pathProtectionFailurePath;
	std::shared_ptr<void> imageLock;
	std::vector<std::shared_ptr<void>> directoryLocks;
};

bool ReadFileImageIdentity(
	const std::wstring& path,
	ClientImageIdentity& identity,
	DWORD& windowsError,
	bool bindProtectedPath);

bool ReadClientImageIdentity(
	HANDLE process,
	ClientImageIdentity& identity,
	DWORD& windowsError);

bool ReadCurrentProcessImageIdentity(
	ClientImageIdentity& identity,
	DWORD& windowsError);

bool HaveMatchingTrustedSigners(
	const ClientImageIdentity& first,
	const ClientImageIdentity& second);