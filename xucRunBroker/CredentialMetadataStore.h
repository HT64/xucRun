// Purpose: Declares the protected metadata store for broker-managed account credentials.
// Approach: Exposes the declarations, constants, and types consumed by the components that provide this behavior.
// Manages the account SID and modification time separately from the password itself.
#pragma once

#include <Windows.h>

#include <cstdint>
#include <string>
#include <vector>

#include "BrokerBackupArchive.h"

struct StoredCredentialMetadata
{
	std::wstring accountSid;
	std::wstring canonicalAccountName;
	std::uint64_t lastWritten = 0;
	std::wstring ownerUserSid;
};

struct BrokerStoreSecurityInspection
{
	bool isDirectory = false;
	bool reparsePoint = false;
	bool ownerIsSystem = false;
	bool daclPresent = false;
	bool daclProtected = false;
	bool aclEntriesMatch = false;
};

bool ResolveCredentialTarget(
	const std::wstring& clientSid,
	const std::wstring& accountSid,
	std::wstring& credentialTarget,
	DWORD& error);

bool EnsureCredentialMetadataBinding(
	const std::wstring& clientSid,
	const std::wstring& accountSid,
	std::wstring& recordPath,
	std::wstring& credentialTarget,
	DWORD& error);

bool DeleteCredentialMetadataBinding(
	const std::wstring& clientSid,
	const std::wstring& accountSid,
	DWORD& error);

bool DeleteOrphanedCredentialMetadata(
	std::uint32_t& deletedCount,
	DWORD& error);

bool ListStoredCredentialMetadata(
	const std::wstring& clientSid,
	std::vector<StoredCredentialMetadata>& entries,
	DWORD& error);

bool ListAllStoredCredentialMetadata(
	std::vector<StoredCredentialMetadata>& entries,
	DWORD& error);

bool DeleteAllCredentialMetadata(DWORD& error);

bool ReadAllCredentialMetadataFiles(
	std::vector<xucrun::backup::MetadataEntry>& entries, DWORD& error);

bool ValidateCredentialMetadataFiles(
	const std::vector<xucrun::backup::MetadataEntry>& entries, DWORD& error);

bool ResolveCredentialTargetsFromMetadataFiles(
	const std::vector<xucrun::backup::MetadataEntry>& entries,
	std::vector<std::wstring>& targets, DWORD& error);

bool ReplaceAllCredentialMetadataFiles(
	const std::vector<xucrun::backup::MetadataEntry>& entries, DWORD& error);

bool InspectBrokerStoreDirectorySecurity(
	const std::wstring& path,
	BrokerStoreSecurityInspection& inspection,
	DWORD& error);

bool RunCredentialMetadataStoreSecuritySelfTest(DWORD& error);