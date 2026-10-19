// Purpose: Persists protected credential metadata in the broker context.
// Serializes account SID and timestamps with strict format and size validation.
#include "CredentialMetadataStore.h"

#include "AccountIdentity.h"
#include "BrokerDirectoryOperations.h"
#include "BrokerTargetNaming.h"
#include "CredentialStore.h"
#include "TargetNaming.h"

#include <aclapi.h>
#include <bcrypt.h>
#include <sddl.h>
#include <shlobj.h>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace
{
	constexpr wchar_t StoreSecurity[] =
		L"O:SYD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)";
	constexpr std::uint32_t RecordMagic = 0x4D435558;
	constexpr std::uint32_t RecordVersion = 1;
	constexpr std::size_t MaximumStoredCredentialCount = 64;
	constexpr std::size_t MaximumAdministrativeStoredCredentialCount = 256;
	constexpr std::size_t MaximumSidLength = 184;

	bool IsStoreSecurityError(DWORD error)
	{
		return error == ERROR_ACCESS_DENIED ||
			error == ERROR_CANT_ACCESS_FILE ||
			error == ERROR_INVALID_ACL ||
			error == ERROR_INVALID_OWNER ||
			error == ERROR_INVALID_SECURITY_DESCR ||
			error == ERROR_INVALID_SID ||
			error == ERROR_PRIVILEGE_NOT_HELD;
	}

	void LogStoreSecurityBlock(const std::wstring& path, DWORD originalError)
	{
		const std::wstring message =
			L"Credential metadata store operation blocked because the directory "
			L"security policy could not be applied or verified. Path: " + path +
			L". Original Windows error: " + std::to_wstring(originalError) +
			L". Run xucRun /audit.";
		OutputDebugStringW((L"xucRunBroker: " + message + L"\n").c_str());
		HANDLE source = RegisterEventSourceW(nullptr, L"xucRunBroker");
		if (!source)
			return;
		const wchar_t* strings[] = { message.c_str() };
		ReportEventW(source, EVENTLOG_WARNING_TYPE, 0, 1,
			nullptr, 1, 0, strings, nullptr);
		DeregisterEventSource(source);
	}

	// Writes a 32-bit number to the canonical metadata format.
	void AppendUInt32(std::vector<std::uint8_t>& value, std::uint32_t number)
	{
		for (unsigned int shift = 0; shift < 32; shift += 8)
			value.push_back(static_cast<std::uint8_t>(number >> shift));
	}

	// Writes a length-encoded UTF-16 string to the metadata format.
	void AppendString(std::vector<std::uint8_t>& value, const std::wstring& text)
	{
		AppendUInt32(value, static_cast<std::uint32_t>(text.size()));
		const auto* bytes = reinterpret_cast<const std::uint8_t*>(text.data());
		value.insert(value.end(), bytes, bytes + text.size() * sizeof(wchar_t));
	}

	// Creates a record that firmly binds the client and account SIDs after syntactic validation.
	bool BuildRecord(const std::wstring& clientSid, const std::wstring& accountSid,
		std::vector<std::uint8_t>& record, DWORD& error)
	{
		PSID parsedClientSid = nullptr;
		PSID parsedAccountSid = nullptr;
		if (!ConvertStringSidToSidW(clientSid.c_str(), &parsedClientSid) ||
			!ConvertStringSidToSidW(accountSid.c_str(), &parsedAccountSid))
		{
			error = GetLastError();
			if (parsedClientSid)
				LocalFree(parsedClientSid);
			if (parsedAccountSid)
				LocalFree(parsedAccountSid);
			return false;
		}
		LocalFree(parsedClientSid);
		LocalFree(parsedAccountSid);

		record.clear();
		AppendUInt32(record, RecordMagic);
		AppendUInt32(record, RecordVersion);
		AppendString(record, clientSid);
		AppendString(record, accountSid);
		return true;
	}

	// Derives a collision-resistant file name from the complete binding record.
	bool ComputeRecordName(const std::vector<std::uint8_t>& record,
		std::wstring& name, DWORD& error)
	{
		BCRYPT_ALG_HANDLE algorithm = nullptr;
		BCRYPT_HASH_HANDLE hash = nullptr;
		std::vector<std::uint8_t> digest(32);
		NTSTATUS status = BCryptOpenAlgorithmProvider(
			&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
		if (status == 0)
			status = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0);
		if (status == 0)
			status = BCryptHashData(hash, const_cast<PUCHAR>(record.data()),
				static_cast<ULONG>(record.size()), 0);
		if (status == 0)
			status = BCryptFinishHash(hash, digest.data(),
				static_cast<ULONG>(digest.size()), 0);
		if (hash)
			BCryptDestroyHash(hash);
		if (algorithm)
			BCryptCloseAlgorithmProvider(algorithm, 0);
		if (status != 0)
		{
			error = ERROR_GEN_FAILURE;
			return false;
		}
		name = xucrun::targets::BytesToUpperHex(digest) + L".meta";
		return true;
	}

	// Opens exactly the named directory without resolving a final reparse point.
	bool OpenDirectoryWithoutReparsePoint(const std::wstring& path,
		DWORD desiredAccess, HANDLE& directory, DWORD& error)
	{
		directory = CreateFileW(path.c_str(), desiredAccess,
			FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
			FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
		if (directory == INVALID_HANDLE_VALUE)
		{
			error = GetLastError();
			return false;
		}
		BY_HANDLE_FILE_INFORMATION information = {};
		if (!GetFileInformationByHandle(directory, &information))
		{
			error = GetLastError();
			CloseHandle(directory);
			directory = INVALID_HANDLE_VALUE;
			return false;
		}
		if ((information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
			(information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
		{
			error = ERROR_CANT_ACCESS_FILE;
			CloseHandle(directory);
			directory = INVALID_HANDLE_VALUE;
			return false;
		}
		return true;
	}

	// Creates a storage directory already protected and opens it without reparse resolution.
	bool EnsureDirectory(const std::wstring& path, SECURITY_ATTRIBUTES* security,
		HANDLE& directory, DWORD& error)
	{
		if (!CreateDirectoryW(path.c_str(), security))
		{
			error = GetLastError();
			if (error != ERROR_ALREADY_EXISTS)
				return false;
		}
		return OpenDirectoryWithoutReparsePoint(path,
			FILE_LIST_DIRECTORY | FILE_ADD_FILE | FILE_TRAVERSE | FILE_DELETE_CHILD |
			GENERIC_WRITE | READ_CONTROL | WRITE_DAC | WRITE_OWNER,
			directory, error);
	}

	// Compares two allow ACEs including SID, mask, and inheritance flags.
	bool EqualAllowedAce(const void* left, const void* right)
	{
		const auto* leftAce = static_cast<const ACCESS_ALLOWED_ACE*>(left);
		const auto* rightAce = static_cast<const ACCESS_ALLOWED_ACE*>(right);
		return leftAce->Header.AceType == ACCESS_ALLOWED_ACE_TYPE &&
			rightAce->Header.AceType == ACCESS_ALLOWED_ACE_TYPE &&
			leftAce->Header.AceFlags == rightAce->Header.AceFlags &&
			leftAce->Mask == rightAce->Mask &&
			EqualSid(const_cast<DWORD*>(&leftAce->SidStart),
				const_cast<DWORD*>(&rightAce->SidStart));
	}

	// Compares ACL entries independently of order without assuming binary layout details.
	bool EqualAclEntries(PACL expected, PACL actual)
	{
		if (!expected || !actual || expected->AceCount != actual->AceCount)
			return false;
		std::vector<bool> matched(actual->AceCount, false);
		for (DWORD expectedIndex = 0; expectedIndex < expected->AceCount; expectedIndex++)
		{
			void* expectedAce = nullptr;
			if (!GetAce(expected, expectedIndex, &expectedAce))
				return false;
			bool found = false;
			for (DWORD actualIndex = 0; actualIndex < actual->AceCount; actualIndex++)
			{
				void* actualAce = nullptr;
				if (!matched[actualIndex] && GetAce(actual, actualIndex, &actualAce) &&
					EqualAllowedAce(expectedAce, actualAce))
				{
					matched[actualIndex] = true;
					found = true;
					break;
				}
			}
			if (!found)
				return false;
		}
		return true;
	}

	// Compares the owner, DACL protection, and allow ACEs of two security descriptors.
	bool EqualStoreSecurityDescriptors(PSECURITY_DESCRIPTOR expectedDescriptor,
		PSECURITY_DESCRIPTOR actualDescriptor)
	{
		PSID expectedOwner = nullptr;
		PSID actualOwner = nullptr;
		BOOL ownerDefaulted = FALSE;
		BOOL expectedDaclPresent = FALSE;
		BOOL actualDaclPresent = FALSE;
		BOOL daclDefaulted = FALSE;
		PACL expectedDacl = nullptr;
		PACL actualDacl = nullptr;
		SECURITY_DESCRIPTOR_CONTROL control = 0;
		DWORD revision = 0;
		return GetSecurityDescriptorOwner(
			expectedDescriptor, &expectedOwner, &ownerDefaulted) && expectedOwner &&
			GetSecurityDescriptorOwner(
				actualDescriptor, &actualOwner, &ownerDefaulted) && actualOwner &&
			EqualSid(expectedOwner, actualOwner) &&
			GetSecurityDescriptorDacl(expectedDescriptor, &expectedDaclPresent,
				&expectedDacl, &daclDefaulted) && expectedDaclPresent && expectedDacl &&
			GetSecurityDescriptorDacl(actualDescriptor, &actualDaclPresent,
				&actualDacl, &daclDefaulted) && actualDaclPresent && actualDacl &&
			GetSecurityDescriptorControl(actualDescriptor, &control, &revision) &&
			(control & SE_DACL_PROTECTED) != 0 && EqualAclEntries(expectedDacl, actualDacl);
	}

	// Reads the individual security attributes of an open store directory.
	bool InspectStoreSecurity(HANDLE directory, PSECURITY_DESCRIPTOR expectedDescriptor,
		BrokerStoreSecurityInspection& inspection, DWORD& error)
	{
		inspection = {};
		BY_HANDLE_FILE_INFORMATION information = {};
		if (!GetFileInformationByHandle(directory, &information))
		{
			error = GetLastError();
			return false;
		}
		inspection.isDirectory =
			(information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
		inspection.reparsePoint =
			(information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
		if (!inspection.isDirectory || inspection.reparsePoint)
		{
			error = NO_ERROR;
			return true;
		}

		PSID expectedOwner = nullptr;
		PSID actualOwner = nullptr;
		BOOL ownerDefaulted = FALSE;
		BOOL expectedDaclPresent = FALSE;
		BOOL actualDaclPresent = FALSE;
		BOOL daclDefaulted = FALSE;
		PACL expectedDacl = nullptr;
		PACL actualDacl = nullptr;
		if (!GetSecurityDescriptorOwner(
			expectedDescriptor, &expectedOwner, &ownerDefaulted) || !expectedOwner ||
			!GetSecurityDescriptorDacl(expectedDescriptor, &expectedDaclPresent,
				&expectedDacl, &daclDefaulted) || !expectedDaclPresent || !expectedDacl)
		{
			error = ERROR_INVALID_SECURITY_DESCR;
			return false;
		}

		PSECURITY_DESCRIPTOR actualDescriptor = nullptr;
		error = GetSecurityInfo(directory, SE_FILE_OBJECT,
			OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
			&actualOwner, nullptr, &actualDacl, nullptr, &actualDescriptor);
		if (error != ERROR_SUCCESS)
			return false;
		GetSecurityDescriptorDacl(actualDescriptor, &actualDaclPresent,
			&actualDacl, &daclDefaulted);
		SECURITY_DESCRIPTOR_CONTROL control = 0;
		DWORD revision = 0;
		inspection.ownerIsSystem = actualOwner && EqualSid(actualOwner, expectedOwner);
		inspection.daclPresent = actualDaclPresent && actualDacl;
		inspection.daclProtected = GetSecurityDescriptorControl(
			actualDescriptor, &control, &revision) && (control & SE_DACL_PROTECTED) != 0;
		inspection.aclEntriesMatch = inspection.daclPresent &&
			EqualAclEntries(expectedDacl, actualDacl);
		LocalFree(actualDescriptor);
		error = NO_ERROR;
		return true;
	}

	// Validates the owner, DACL protection, and semantic content of an applied store ACL.
	bool VerifyStoreSecurity(HANDLE directory,
		PSECURITY_DESCRIPTOR expectedDescriptor, DWORD& error)
	{
		BrokerStoreSecurityInspection inspection;
		const bool inspected = InspectStoreSecurity(
			directory, expectedDescriptor, inspection, error);
		const bool valid = inspected && inspection.isDirectory &&
			!inspection.reparsePoint && inspection.ownerIsSystem &&
			inspection.daclPresent && inspection.daclProtected &&
			inspection.aclEntriesMatch;
		if (!valid)
			error = ERROR_INVALID_SECURITY_DESCR;
		return valid;
	}

	// Enforces and verifies the owner and protected DACL of the broker store.
	bool ApplyStoreSecurity(HANDLE directory,
		PSECURITY_DESCRIPTOR descriptor, DWORD& error)
	{
		PSID owner = nullptr;
		BOOL ownerDefaulted = FALSE;
		BOOL daclPresent = FALSE;
		BOOL daclDefaulted = FALSE;
		PACL dacl = nullptr;
		if (!GetSecurityDescriptorOwner(descriptor, &owner, &ownerDefaulted) || !owner ||
			!GetSecurityDescriptorDacl(
			descriptor, &daclPresent, &dacl, &daclDefaulted) || !daclPresent)
		{
			error = ERROR_INVALID_SECURITY_DESCR;
			return false;
		}
		error = SetSecurityInfo(directory, SE_FILE_OBJECT,
			OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION |
			PROTECTED_DACL_SECURITY_INFORMATION, owner, nullptr, dacl, nullptr);
		return error == ERROR_SUCCESS && VerifyStoreSecurity(directory, descriptor, error);
	}

	struct StoreDirectoryGuard
	{
		StoreDirectoryGuard() = default;
		~StoreDirectoryGuard()
		{
			if (version != INVALID_HANDLE_VALUE)
				CloseHandle(version);
			if (store != INVALID_HANDLE_VALUE)
				CloseHandle(store);
			if (application != INVALID_HANDLE_VALUE)
				CloseHandle(application);
		}
		StoreDirectoryGuard(const StoreDirectoryGuard&) = delete;
		StoreDirectoryGuard& operator=(const StoreDirectoryGuard&) = delete;

		std::wstring path;
		HANDLE application = INVALID_HANDLE_VALUE;
		HANDLE store = INVALID_HANDLE_VALUE;
		HANDLE version = INVALID_HANDLE_VALUE;
	};

	// Determines and hardens the versioned broker store under ProgramData.
	bool GetStoreDirectory(StoreDirectoryGuard& directory, DWORD& error)
	{
		PWSTR programData = nullptr;
		const HRESULT result = SHGetKnownFolderPath(
			FOLDERID_ProgramData, KF_FLAG_DEFAULT, nullptr, &programData);
		if (FAILED(result))
		{
			error = HRESULT_CODE(result);
			return false;
		}
		const std::wstring applicationDirectory =
			std::wstring(programData) + L"\\xucRun";
		CoTaskMemFree(programData);

		PSECURITY_DESCRIPTOR descriptor = nullptr;
		if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
			StoreSecurity, SDDL_REVISION_1, &descriptor, nullptr))
		{
			error = GetLastError();
			return false;
		}
		SECURITY_ATTRIBUTES security = { sizeof(security), descriptor, FALSE };
		const std::wstring storeDirectory = applicationDirectory + L"\\BrokerStore";
		directory.path = storeDirectory + L"\\v1";
		const bool created = EnsureDirectory(
			applicationDirectory, &security, directory.application, error) &&
			ApplyStoreSecurity(directory.application, descriptor, error) &&
			EnsureDirectory(storeDirectory, &security, directory.store, error) &&
			ApplyStoreSecurity(directory.store, descriptor, error) &&
			EnsureDirectory(directory.path, &security, directory.version, error) &&
			ApplyStoreSecurity(directory.version, descriptor, error);
		LocalFree(descriptor);
		if (!created && IsStoreSecurityError(error))
		{
			const DWORD originalError = error;
			error = ERROR_INVALID_SECURITY_DESCR;
			LogStoreSecurityBlock(directory.path, originalError);
		}
		return created;
	}

	// Confirms that an existing record matches the expected SID binding byte for byte.
	bool ReadExistingRecord(HANDLE directory, const std::wstring& name,
		const std::vector<std::uint8_t>& expected, DWORD& error)
	{
		HANDLE file = INVALID_HANDLE_VALUE;
		if (!xucrun::broker_directory::OpenFile(directory, name, GENERIC_READ,
			FILE_SHARE_READ, xucrun::broker_directory::OpenMode::OpenExisting,
			file, error))
		{
			return false;
		}
		LARGE_INTEGER size = {};
		std::vector<std::uint8_t> actual;
		bool success = GetFileSizeEx(file, &size) && size.QuadPart >= 0 &&
			size.QuadPart <= 4096;
		if (success)
		{
			actual.resize(static_cast<std::size_t>(size.QuadPart));
			DWORD read = 0;
			success = ReadFile(file, actual.data(), static_cast<DWORD>(actual.size()),
				&read, nullptr) && read == actual.size() && actual == expected;
		}
		if (!success)
			error = ERROR_INVALID_DATA;
		CloseHandle(file);
		return success;
	}

	bool WriteRecordAtomically(HANDLE directory, const std::wstring& name,
		const std::vector<std::uint8_t>& record, DWORD& error)
	{
		if (ReadExistingRecord(directory, name, record, error))
			return true;
		if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND)
			return false;

		const std::wstring temporaryName = name + L".tmp";
		if (!xucrun::broker_directory::DeleteFile(
			directory, temporaryName, true, error))
		{
			return false;
		}
		HANDLE file = INVALID_HANDLE_VALUE;
		if (!xucrun::broker_directory::OpenFile(directory, temporaryName,
			GENERIC_WRITE | DELETE, 0, xucrun::broker_directory::OpenMode::CreateNew,
			file, error))
		{
			return false;
		}
		DWORD written = 0;
		bool success = WriteFile(file, record.data(),
			static_cast<DWORD>(record.size()), &written, nullptr) == TRUE;
		if (!success)
			error = GetLastError();
		else if (written != record.size())
		{
			success = false;
			error = ERROR_WRITE_FAULT;
		}
		else if (!FlushFileBuffers(file))
		{
			success = false;
			error = GetLastError();
		}
		if (success)
		{
			success = xucrun::broker_directory::RenameFile(
				file, directory, name, error) && FlushFileBuffers(file) &&
				xucrun::broker_directory::Flush(directory, error);
			if (!success && error == NO_ERROR)
				error = GetLastError();
		}
		if (!success)
		{
			DWORD deleteError = NO_ERROR;
			xucrun::broker_directory::MarkFileForDeletion(file, deleteError);
		}
		CloseHandle(file);
		if (!success && (error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS))
			return ReadExistingRecord(directory, name, record, error);
		return success;
	}

	// Reads a bounded regular metadata file without reparse point resolution.
	bool ReadRecordFile(HANDLE directory, const std::wstring& name,
		std::vector<std::uint8_t>& record, DWORD& error)
	{
		HANDLE file = INVALID_HANDLE_VALUE;
		if (!xucrun::broker_directory::OpenFile(directory, name, GENERIC_READ,
			FILE_SHARE_READ, xucrun::broker_directory::OpenMode::OpenExisting,
			file, error))
		{
			return false;
		}
		LARGE_INTEGER size = {};
		bool success = GetFileSizeEx(file, &size) && size.QuadPart >= 16 &&
			size.QuadPart <= 4096;
		if (success)
		{
			record.resize(static_cast<std::size_t>(size.QuadPart));
			DWORD read = 0;
			success = ReadFile(file, record.data(), static_cast<DWORD>(record.size()),
				&read, nullptr) && read == record.size();
		}
		CloseHandle(file);
		if (!success)
		{
			record.clear();
			error = ERROR_INVALID_DATA;
		}
		return success;
	}

	// Reads a 32-bit number from the metadata format with bounds checking.
	bool ReadUInt32(const std::vector<std::uint8_t>& record,
		std::size_t& offset, std::uint32_t& value)
	{
		if (offset > record.size() || record.size() - offset < sizeof(value))
			return false;
		value = 0;
		for (std::size_t index = 0; index < sizeof(value); index++)
			value |= static_cast<std::uint32_t>(record[offset++]) << (index * 8);
		return true;
	}

	// Reads a length-encoded string with size and bounds validation.
	bool ReadRecordString(const std::vector<std::uint8_t>& record,
		std::size_t& offset, std::wstring& value)
	{
		std::uint32_t length = 0;
		if (!ReadUInt32(record, offset, length) || length == 0 ||
			length > MaximumSidLength || offset > record.size() ||
			record.size() - offset < static_cast<std::size_t>(length) * sizeof(wchar_t))
		{
			return false;
		}
		value.resize(length);
		for (std::size_t index = 0; index < length; index++)
		{
			const std::uint16_t character = static_cast<std::uint16_t>(record[offset]) |
				(static_cast<std::uint16_t>(record[offset + 1]) << 8);
			if (character == 0)
				return false;
			value[index] = static_cast<wchar_t>(character);
			offset += sizeof(wchar_t);
		}
		return true;
	}

	// Fully validates the structure, version, and both SIDs of a metadata record.
	bool ParseRecord(const std::vector<std::uint8_t>& record,
		std::wstring& clientSid, std::wstring& accountSid)
	{
		std::size_t offset = 0;
		std::uint32_t magic = 0;
		std::uint32_t version = 0;
		return ReadUInt32(record, offset, magic) && magic == RecordMagic &&
			ReadUInt32(record, offset, version) && version == RecordVersion &&
			ReadRecordString(record, offset, clientSid) &&
			ReadRecordString(record, offset, accountSid) && offset == record.size();
	}

	bool DeleteAllMetadataFiles(HANDLE directory, DWORD& error)
	{
		std::vector<xucrun::broker_directory::Entry> files;
		if (!xucrun::broker_directory::Enumerate(directory, files, error))
			return false;
		for (const auto& file : files)
		{
			if (!file.name.ends_with(L".meta"))
				continue;
			if ((file.attributes &
				(FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0)
			{
				error = ERROR_INVALID_DATA;
				return false;
			}
			if (!xucrun::broker_directory::DeleteFile(
				directory, file.name, false, error))
			{
				return false;
			}
		}
		error = NO_ERROR;
		return true;
	}

	bool ReadAllMetadataFiles(HANDLE directory,
		std::vector<xucrun::backup::MetadataEntry>& entries, DWORD& error)
	{
		entries.clear();
		std::vector<xucrun::broker_directory::Entry> files;
		if (!xucrun::broker_directory::Enumerate(directory, files, error))
			return false;
		for (const auto& file : files)
		{
			if (!file.name.ends_with(L".meta"))
				continue;
			if ((file.attributes &
				(FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0 ||
				entries.size() >= xucrun::backup::MaximumMetadataEntries)
			{
				error = ERROR_INVALID_DATA;
				entries.clear();
				return false;
			}
			std::vector<std::uint8_t> record;
			std::wstring ownerSid;
			std::wstring accountSid;
			std::wstring expectedName;
			if (!ReadRecordFile(directory, file.name, record, error) ||
				!ParseRecord(record, ownerSid, accountSid) ||
				!ComputeRecordName(record, expectedName, error) ||
				_wcsicmp(expectedName.c_str(), file.name.c_str()) != 0)
			{
				error = ERROR_INVALID_DATA;
				entries.clear();
				return false;
			}
			entries.push_back({ file.name, std::move(record) });
		}
		error = NO_ERROR;
		return true;
	}
}

	// Derives the credential target exclusively from the validated client/account binding.
bool ResolveCredentialTarget(
	const std::wstring& clientSid,
	const std::wstring& accountSid,
	std::wstring& credentialTarget,
	DWORD& error)
{
	error = ERROR_SUCCESS;
	credentialTarget.clear();
	std::vector<std::uint8_t> record;
	std::wstring recordName;
	std::wstring prefix;
	if (!BuildRecord(clientSid, accountSid, record, error) ||
		!ComputeRecordName(record, recordName, error) ||
		!xucrun::broker_targets::GetMachineScopedPrefix(
			xucrun::broker_targets::TargetKind::Credential, prefix, error))
	{
		return false;
	}

	credentialTarget = prefix +
		recordName.substr(0, recordName.size() - 5);
	return true;
}

	// Creates the SID binding collision-safely or confirms an already identical record.
bool EnsureCredentialMetadataBinding(
	const std::wstring& clientSid,
	const std::wstring& accountSid,
	std::wstring& recordPath,
	std::wstring& credentialTarget,
	DWORD& error)
{
	std::lock_guard lock(xucrun::broker_store::TransactionMutex());
	error = ERROR_SUCCESS;
	recordPath.clear();
	credentialTarget.clear();
	std::vector<std::uint8_t> record;
	std::wstring recordName;
	StoreDirectoryGuard storeDirectory;
	if (!BuildRecord(clientSid, accountSid, record, error) ||
		!ComputeRecordName(record, recordName, error) ||
		!GetStoreDirectory(storeDirectory, error) ||
		!ResolveCredentialTarget(clientSid, accountSid, credentialTarget, error))
	{
		return false;
	}

	recordPath = storeDirectory.path + L"\\" + recordName;
	return WriteRecordAtomically(
		storeDirectory.version, recordName, record, error);
}

bool DeleteCredentialMetadataBinding(
	const std::wstring& clientSid,
	const std::wstring& accountSid,
	DWORD& error)
{
	std::lock_guard lock(xucrun::broker_store::TransactionMutex());
	std::vector<std::uint8_t> expectedRecord;
	std::vector<std::uint8_t> actualRecord;
	std::wstring recordName;
	StoreDirectoryGuard storeDirectory;
	if (!BuildRecord(clientSid, accountSid, expectedRecord, error) ||
		!ComputeRecordName(expectedRecord, recordName, error) ||
		!GetStoreDirectory(storeDirectory, error))
	{
		return false;
	}
	if (!ReadRecordFile(storeDirectory.version, recordName, actualRecord, error))
	{
		if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
		{
			error = NO_ERROR;
			return true;
		}
		return false;
	}
	if (actualRecord != expectedRecord)
	{
		error = ERROR_INVALID_DATA;
		return false;
	}
	return xucrun::broker_directory::DeleteFile(
		storeDirectory.version, recordName, true, error);
}

bool DeleteOrphanedCredentialMetadata(
	std::uint32_t& deletedCount,
	DWORD& error)
{
	std::lock_guard lock(xucrun::broker_store::TransactionMutex());
	deletedCount = 0;
	std::wstring prefix;
	std::vector<std::wstring> targets;
	if (!xucrun::broker_targets::GetMachineScopedPrefix(
		xucrun::broker_targets::TargetKind::Credential, prefix, error) ||
		!xucrun::broker_store::EnumerateTargets(prefix, targets, error))
	{
		return false;
	}
	std::sort(targets.begin(), targets.end());
	if (std::adjacent_find(targets.begin(), targets.end()) != targets.end())
	{
		error = ERROR_INVALID_DATA;
		return false;
	}
	StoreDirectoryGuard storeDirectory;
	if (!GetStoreDirectory(storeDirectory, error))
		return false;
	std::vector<xucrun::backup::MetadataEntry> entries;
	if (!ReadAllMetadataFiles(storeDirectory.version, entries, error))
		return false;
	std::vector<std::wstring> orphanNames;
	for (const auto& entry : entries)
	{
		std::vector<xucrun::backup::MetadataEntry> singleEntry = { entry };
		std::vector<std::wstring> metadataTargets;
		if (!ResolveCredentialTargetsFromMetadataFiles(
			singleEntry, metadataTargets, error) || metadataTargets.size() != 1)
		{
			error = ERROR_INVALID_DATA;
			return false;
		}
		if (!std::binary_search(targets.begin(), targets.end(), metadataTargets.front()))
			orphanNames.push_back(entry.name);
	}
	for (const auto& name : orphanNames)
	{
		if (!xucrun::broker_directory::DeleteFile(
			storeDirectory.version, name, false, error))
		{
			return false;
		}
		deletedCount++;
	}
	error = NO_ERROR;
	return true;
}

// Tests store hardening and reparse defenses in isolation in a temporary directory.
bool RunCredentialMetadataStoreSecuritySelfTest(DWORD& error)
{
	error = NO_ERROR;
	HANDLE token = nullptr;
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
	{
		error = GetLastError();
		return false;
	}
	DWORD tokenSize = 0;
	GetTokenInformation(token, TokenUser, nullptr, 0, &tokenSize);
	std::vector<BYTE> tokenBuffer(tokenSize);
	if (tokenSize == 0 || !GetTokenInformation(
		token, TokenUser, tokenBuffer.data(), tokenSize, &tokenSize))
	{
		error = GetLastError();
		CloseHandle(token);
		return false;
	}
	CloseHandle(token);
	const auto* tokenUser = reinterpret_cast<const TOKEN_USER*>(tokenBuffer.data());
	wchar_t* sidText = nullptr;
	if (!ConvertSidToStringSidW(tokenUser->User.Sid, &sidText))
	{
		error = GetLastError();
		return false;
	}
	const std::wstring ownerSid = sidText;
	LocalFree(sidText);

	PSECURITY_DESCRIPTOR expectedDescriptor = nullptr;
	PSECURITY_DESCRIPTOR wrongOwnerDescriptor = nullptr;
	PSECURITY_DESCRIPTOR extraAceDescriptor = nullptr;
	PSECURITY_DESCRIPTOR unprotectedDescriptor = nullptr;
	const std::wstring expectedSddl = L"O:" + ownerSid +
		L"D:P(A;OICI;FA;;;" + ownerSid + L")";
	const std::wstring wrongOwnerSddl = L"O:BAD:P(A;OICI;FA;;;" + ownerSid + L")";
	const std::wstring extraAceSddl = expectedSddl + L"(A;OICI;FA;;;BU)";
	const std::wstring unprotectedSddl = L"O:" + ownerSid +
		L"D:(A;OICI;FA;;;" + ownerSid + L")";
	const bool descriptorsCreated =
		ConvertStringSecurityDescriptorToSecurityDescriptorW(expectedSddl.c_str(),
			SDDL_REVISION_1, &expectedDescriptor, nullptr) &&
		ConvertStringSecurityDescriptorToSecurityDescriptorW(wrongOwnerSddl.c_str(),
			SDDL_REVISION_1, &wrongOwnerDescriptor, nullptr) &&
		ConvertStringSecurityDescriptorToSecurityDescriptorW(extraAceSddl.c_str(),
			SDDL_REVISION_1, &extraAceDescriptor, nullptr) &&
		ConvertStringSecurityDescriptorToSecurityDescriptorW(unprotectedSddl.c_str(),
			SDDL_REVISION_1, &unprotectedDescriptor, nullptr);
	if (!descriptorsCreated)
	{
		error = GetLastError();
		if (unprotectedDescriptor) LocalFree(unprotectedDescriptor);
		if (extraAceDescriptor) LocalFree(extraAceDescriptor);
		if (wrongOwnerDescriptor) LocalFree(wrongOwnerDescriptor);
		if (expectedDescriptor) LocalFree(expectedDescriptor);
		return false;
	}

	bool success = true;
	if (EqualStoreSecurityDescriptors(expectedDescriptor, wrongOwnerDescriptor))
	{
		error = ERROR_INVALID_OWNER;
		success = false;
	}
	else if (EqualStoreSecurityDescriptors(expectedDescriptor, extraAceDescriptor))
	{
		error = ERROR_INVALID_ACL;
		success = false;
	}
	else if (EqualStoreSecurityDescriptors(expectedDescriptor, unprotectedDescriptor))
	{
		error = ERROR_BAD_INHERITANCE_ACL;
		success = false;
	}
	wchar_t temporaryPath[MAX_PATH] = {};
	wchar_t temporaryName[MAX_PATH] = {};
	std::wstring root;
	std::wstring movedRoot;
	std::wstring target;
	std::wstring link;
	HANDLE directory = INVALID_HANDLE_VALUE;
	bool directoryMoved = false;
	if (success && GetTempPathW(ARRAYSIZE(temporaryPath), temporaryPath) > 0 &&
		GetTempFileNameW(temporaryPath, L"xus", 0, temporaryName) != 0 &&
		DeleteFileW(temporaryName))
	{
		root = temporaryName;
		SECURITY_ATTRIBUTES security = { sizeof(security), expectedDescriptor, FALSE };
		if (!EnsureDirectory(root, &security, directory, error))
			success = false;
		BOOL daclPresent = FALSE;
		BOOL daclDefaulted = FALSE;
		PACL testDacl = nullptr;
		if (success && (!GetSecurityDescriptorDacl(unprotectedDescriptor,
			&daclPresent, &testDacl, &daclDefaulted) || !daclPresent || !testDacl))
		{
			error = ERROR_INVALID_SECURITY_DESCR;
			success = false;
		}
		if (success)
		{
			error = SetSecurityInfo(directory, SE_FILE_OBJECT,
				DACL_SECURITY_INFORMATION | UNPROTECTED_DACL_SECURITY_INFORMATION,
				nullptr, nullptr, testDacl, nullptr);
			success = error == ERROR_SUCCESS;
		}
		if (success && (VerifyStoreSecurity(directory, expectedDescriptor, error) ||
			error != ERROR_INVALID_SECURITY_DESCR))
			success = false;
		testDacl = nullptr;
		if (success && (!GetSecurityDescriptorDacl(extraAceDescriptor,
			&daclPresent, &testDacl, &daclDefaulted) || !daclPresent || !testDacl))
		{
			error = ERROR_INVALID_SECURITY_DESCR;
			success = false;
		}
		if (success)
		{
			error = SetSecurityInfo(directory, SE_FILE_OBJECT,
				DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
				nullptr, nullptr, testDacl, nullptr);
			success = error == ERROR_SUCCESS;
		}
		if (success && (VerifyStoreSecurity(directory, expectedDescriptor, error) ||
			error != ERROR_INVALID_SECURITY_DESCR))
			success = false;
		if (success && !ApplyStoreSecurity(directory, expectedDescriptor, error))
			success = false;
		const std::wstring atomicName = L"atomic.meta";
		const std::wstring atomicTemporaryName = atomicName + L".tmp";
		const std::vector<std::uint8_t> atomicRecord = {
			0x58, 0x55, 0x43, 0x52, 0x75, 0x6e };
		if (success)
		{
			HANDLE partial = INVALID_HANDLE_VALUE;
			if (!xucrun::broker_directory::OpenFile(directory, atomicTemporaryName,
				GENERIC_WRITE | DELETE, 0,
				xucrun::broker_directory::OpenMode::CreateNew, partial, error))
			{
				success = false;
			}
			else
			{
				const std::uint8_t partialByte = 0;
				DWORD written = 0;
				success = WriteFile(partial, &partialByte, sizeof(partialByte),
					&written, nullptr) == TRUE && written == sizeof(partialByte);
				if (!success)
					error = GetLastError();
				CloseHandle(partial);
			}
		}
		if (success && (!WriteRecordAtomically(
			directory, atomicName, atomicRecord, error) ||
			!ReadExistingRecord(directory, atomicName, atomicRecord, error) ||
			!WriteRecordAtomically(directory, atomicName, atomicRecord, error)))
		{
			success = false;
		}
		std::vector<xucrun::broker_directory::Entry> atomicEntries;
		if (success && (!xucrun::broker_directory::Enumerate(
			directory, atomicEntries, error) || std::any_of(
				atomicEntries.begin(), atomicEntries.end(),
				[&atomicTemporaryName](const xucrun::broker_directory::Entry& entry)
				{
					return _wcsicmp(
						entry.name.c_str(), atomicTemporaryName.c_str()) == 0;
				})))
		{
			error = ERROR_INVALID_DATA;
			success = false;
		}
		if (success)
		{
			const std::vector<std::uint8_t> wrongRecord = { 0 };
			if (WriteRecordAtomically(directory, atomicName, wrongRecord, error) ||
				error != ERROR_INVALID_DATA)
			{
				error = ERROR_INVALID_DATA;
				success = false;
			}
		}
		DWORD atomicDeleteError = NO_ERROR;
		if (!xucrun::broker_directory::DeleteFile(
			directory, atomicName, true, atomicDeleteError) && success)
		{
			error = atomicDeleteError;
			success = false;
		}
		atomicDeleteError = NO_ERROR;
		if (!xucrun::broker_directory::DeleteFile(
			directory, atomicTemporaryName, true, atomicDeleteError) && success)
		{
			error = atomicDeleteError;
			success = false;
		}
		movedRoot = root + L"-moved";
		if (success)
		{
			directoryMoved = MoveFileExW(root.c_str(), movedRoot.c_str(), 0) == TRUE;
			const DWORD moveError = directoryMoved ? NO_ERROR : GetLastError();
			if (!directoryMoved && moveError != ERROR_SHARING_VIOLATION &&
				moveError != ERROR_ACCESS_DENIED)
			{
				error = moveError;
				success = false;
			}
		}
		if (success && directoryMoved)
		{
			const std::wstring probeName = L"bound.tmp";
			HANDLE probe = INVALID_HANDLE_VALUE;
			if (!CreateDirectoryW(root.c_str(), nullptr) ||
				!xucrun::broker_directory::OpenFile(directory, probeName,
					GENERIC_WRITE | DELETE, 0,
					xucrun::broker_directory::OpenMode::CreateNew, probe, error))
			{
				if (error == NO_ERROR)
					error = GetLastError();
				success = false;
			}
			if (probe != INVALID_HANDLE_VALUE)
				CloseHandle(probe);
			if (success &&
				(GetFileAttributesW((movedRoot + L"\\" + probeName).c_str()) ==
					INVALID_FILE_ATTRIBUTES ||
				 GetFileAttributesW((root + L"\\" + probeName).c_str()) !=
					INVALID_FILE_ATTRIBUTES))
			{
				error = ERROR_INVALID_REPARSE_DATA;
				success = false;
			}
			DWORD deleteError = NO_ERROR;
			if (!xucrun::broker_directory::DeleteFile(
				directory, probeName, true, deleteError) && success)
			{
				error = deleteError;
				success = false;
			}
		}
		if (directory != INVALID_HANDLE_VALUE)
		{
			CloseHandle(directory);
			directory = INVALID_HANDLE_VALUE;
		}
		if (directoryMoved)
		{
			RemoveDirectoryW(root.c_str());
			if (!MoveFileExW(movedRoot.c_str(), root.c_str(), 0))
			{
				if (success)
					error = GetLastError();
				success = false;
			}
		}
		target = root + L"\\target";
		link = root + L"\\link";
		if (success && !CreateDirectoryW(target.c_str(), nullptr))
		{
			error = GetLastError();
			success = false;
		}
		constexpr DWORD AllowUnprivilegedCreate = 0x2;
		if (success && !CreateSymbolicLinkW(link.c_str(), target.c_str(),
			SYMBOLIC_LINK_FLAG_DIRECTORY | AllowUnprivilegedCreate) &&
			!CreateSymbolicLinkW(link.c_str(), target.c_str(), SYMBOLIC_LINK_FLAG_DIRECTORY))
		{
			error = GetLastError();
			success = false;
		}
		if (success && (OpenDirectoryWithoutReparsePoint(link,
			READ_CONTROL | FILE_READ_ATTRIBUTES, directory, error) ||
			error != ERROR_CANT_ACCESS_FILE))
			success = false;
	}
	else if (success)
	{
		error = GetLastError();
		success = false;
	}
	if (directory != INVALID_HANDLE_VALUE)
		CloseHandle(directory);
	if (!link.empty())
		RemoveDirectoryW(link.c_str());
	if (!target.empty())
		RemoveDirectoryW(target.c_str());
	if (!root.empty())
		RemoveDirectoryW(root.c_str());
	if (!movedRoot.empty())
		RemoveDirectoryW(movedRoot.c_str());
	LocalFree(unprotectedDescriptor);
	LocalFree(extraAceDescriptor);
	LocalFree(wrongOwnerDescriptor);
	LocalFree(expectedDescriptor);
	if (success)
		error = NO_ERROR;
	return success;
}

// Validates the exact broker store policy read-only through a non-resolving handle.
bool InspectBrokerStoreDirectorySecurity(
	const std::wstring& path,
	BrokerStoreSecurityInspection& inspection,
	DWORD& error)
{
	inspection = {};
	HANDLE directory = CreateFileW(path.c_str(), READ_CONTROL | FILE_READ_ATTRIBUTES,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
		FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
	if (directory == INVALID_HANDLE_VALUE)
	{
		error = GetLastError();
		return false;
	}
	PSECURITY_DESCRIPTOR descriptor = nullptr;
	if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
		StoreSecurity, SDDL_REVISION_1, &descriptor, nullptr))
	{
		error = GetLastError();
		CloseHandle(directory);
		return false;
	}
	const bool success = InspectStoreSecurity(directory, descriptor, inspection, error);
	LocalFree(descriptor);
	CloseHandle(directory);
	return success;
}

	// Lists only valid credential metadata belonging to the requesting client.
bool ListStoredCredentialMetadata(
	const std::wstring& clientSid,
	std::vector<StoredCredentialMetadata>& entries,
	DWORD& error)
{
	std::lock_guard lock(xucrun::broker_store::TransactionMutex());
	entries.clear();
	error = NO_ERROR;
	std::vector<std::uint8_t> validatedClientRecord;
	if (!BuildRecord(clientSid, clientSid, validatedClientRecord, error))
		return false;

	StoreDirectoryGuard storeDirectory;
	if (!GetStoreDirectory(storeDirectory, error))
		return false;
	std::vector<xucrun::broker_directory::Entry> files;
	if (!xucrun::broker_directory::Enumerate(storeDirectory.version, files, error))
		return false;

	bool success = true;
	for (const auto& file : files)
	{
		if (!file.name.ends_with(L".meta"))
			continue;
		if ((file.attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
			continue;
		std::vector<std::uint8_t> record;
		std::wstring recordClientSid;
		std::wstring accountSid;
		std::wstring expectedName;
		if (!ReadRecordFile(storeDirectory.version, file.name, record, error) ||
			!ParseRecord(record, recordClientSid, accountSid) ||
			!ComputeRecordName(record, expectedName, error) ||
			_wcsicmp(expectedName.c_str(), file.name.c_str()) != 0)
		{
			error = ERROR_INVALID_DATA;
			success = false;
			break;
		}
		if (_wcsicmp(recordClientSid.c_str(), clientSid.c_str()) != 0)
			continue;
		if (entries.size() >= MaximumStoredCredentialCount)
		{
			error = ERROR_BUFFER_OVERFLOW;
			success = false;
			break;
		}

		AccountIdentity identity;
		std::wstring credentialTarget;
		std::uint64_t lastWritten = 0;
		DWORD entryError = NO_ERROR;
		if (!ResolveUserAccountIdentity(accountSid, identity, entryError) ||
			!ResolveCredentialTarget(clientSid, identity.sid, credentialTarget, entryError))
		{
			error = entryError;
			success = false;
			break;
		}
		if (!xucrun::broker_store::ReadLastWritten(
			credentialTarget, lastWritten, entryError))
		{
			if (entryError == ERROR_NOT_FOUND)
				continue;
			error = entryError;
			success = false;
			break;
		}
		entries.push_back({ identity.sid, identity.canonicalName, lastWritten,
			recordClientSid });
	}
	if (!success)
	{
		entries.clear();
		return false;
	}
	std::sort(entries.begin(), entries.end(),
		[](const StoredCredentialMetadata& left, const StoredCredentialMetadata& right)
		{
			return _wcsicmp(left.canonicalAccountName.c_str(),
				right.canonicalAccountName.c_str()) < 0;
		});
	error = NO_ERROR;
	return true;
}

bool ListAllStoredCredentialMetadata(
	std::vector<StoredCredentialMetadata>& entries,
	DWORD& error)
{
	std::lock_guard lock(xucrun::broker_store::TransactionMutex());
	entries.clear();
	error = NO_ERROR;
	StoreDirectoryGuard storeDirectory;
	if (!GetStoreDirectory(storeDirectory, error))
		return false;
	std::vector<xucrun::broker_directory::Entry> files;
	if (!xucrun::broker_directory::Enumerate(storeDirectory.version, files, error))
		return false;

	bool success = true;
	for (const auto& file : files)
	{
		if (!file.name.ends_with(L".meta"))
			continue;
		if ((file.attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
			continue;
		std::vector<std::uint8_t> record;
		std::wstring ownerUserSid;
		std::wstring accountSid;
		std::wstring expectedName;
		if (!ReadRecordFile(storeDirectory.version, file.name, record, error) ||
			!ParseRecord(record, ownerUserSid, accountSid) ||
			!ComputeRecordName(record, expectedName, error) ||
			_wcsicmp(expectedName.c_str(), file.name.c_str()) != 0)
		{
			error = ERROR_INVALID_DATA;
			success = false;
			break;
		}
		if (entries.size() >= MaximumAdministrativeStoredCredentialCount)
		{
			error = ERROR_BUFFER_OVERFLOW;
			success = false;
			break;
		}

		AccountIdentity identity;
		std::wstring credentialTarget;
		std::uint64_t lastWritten = 0;
		DWORD entryError = NO_ERROR;
		if (!ResolveUserAccountIdentity(accountSid, identity, entryError) ||
			!ResolveCredentialTarget(
				ownerUserSid, identity.sid, credentialTarget, entryError))
		{
			error = entryError;
			success = false;
			break;
		}
		if (!xucrun::broker_store::ReadLastWritten(
			credentialTarget, lastWritten, entryError))
		{
			if (entryError == ERROR_NOT_FOUND)
				continue;
			error = entryError;
			success = false;
			break;
		}
		entries.push_back({ identity.sid, identity.canonicalName, lastWritten,
			ownerUserSid });
	}
	if (!success)
	{
		entries.clear();
		return false;
	}
	std::sort(entries.begin(), entries.end(),
		[](const StoredCredentialMetadata& left,
			const StoredCredentialMetadata& right)
		{
			const int ownerComparison = _wcsicmp(
				left.ownerUserSid.c_str(), right.ownerUserSid.c_str());
			return ownerComparison != 0
				? ownerComparison < 0
				: _wcsicmp(left.canonicalAccountName.c_str(),
					right.canonicalAccountName.c_str()) < 0;
		});
	error = NO_ERROR;
	return true;
}

bool DeleteAllCredentialMetadata(DWORD& error)
{
	std::lock_guard lock(xucrun::broker_store::TransactionMutex());
	error = NO_ERROR;
	StoreDirectoryGuard storeDirectory;
	if (!GetStoreDirectory(storeDirectory, error))
		return false;
	return DeleteAllMetadataFiles(storeDirectory.version, error);
}

bool ReadAllCredentialMetadataFiles(
	std::vector<xucrun::backup::MetadataEntry>& entries, DWORD& error)
{
	std::lock_guard lock(xucrun::broker_store::TransactionMutex());
	entries.clear();
	StoreDirectoryGuard storeDirectory;
	if (!GetStoreDirectory(storeDirectory, error))
		return false;
	return ReadAllMetadataFiles(storeDirectory.version, entries, error);
}

bool ReplaceAllCredentialMetadataFiles(
	const std::vector<xucrun::backup::MetadataEntry>& entries, DWORD& error)
{
	std::lock_guard lock(xucrun::broker_store::TransactionMutex());
	if (!ValidateCredentialMetadataFiles(entries, error))
		return false;
	StoreDirectoryGuard storeDirectory;
	if (!GetStoreDirectory(storeDirectory, error))
		return false;
	if (!DeleteAllMetadataFiles(storeDirectory.version, error))
		return false;
	for (const auto& entry : entries)
	{
		HANDLE file = INVALID_HANDLE_VALUE;
		if (!xucrun::broker_directory::OpenFile(storeDirectory.version, entry.name,
			GENERIC_WRITE | DELETE, 0,
			xucrun::broker_directory::OpenMode::CreateNew, file, error))
		{
			return false;
		}
		DWORD written = 0;
		bool success = WriteFile(file, entry.contents.data(),
			static_cast<DWORD>(entry.contents.size()), &written, nullptr) == TRUE;
		if (!success)
			error = GetLastError();
		else if (written != entry.contents.size())
		{
			success = false;
			error = ERROR_WRITE_FAULT;
		}
		else if (!FlushFileBuffers(file))
		{
			success = false;
			error = GetLastError();
		}
		if (!success)
		{
			DWORD deleteError = NO_ERROR;
			xucrun::broker_directory::MarkFileForDeletion(file, deleteError);
		}
		CloseHandle(file);
		if (!success)
			return false;
	}
	error = NO_ERROR;
	return true;
}

bool ValidateCredentialMetadataFiles(
	const std::vector<xucrun::backup::MetadataEntry>& entries, DWORD& error)
{
	for (const auto& entry : entries)
	{
		std::wstring ownerSid;
		std::wstring accountSid;
		std::wstring expectedName;
		if (!ParseRecord(entry.contents, ownerSid, accountSid) ||
			!ComputeRecordName(entry.contents, expectedName, error) ||
			_wcsicmp(expectedName.c_str(), entry.name.c_str()) != 0)
		{
			error = ERROR_INVALID_DATA;
			return false;
		}
	}
	error = NO_ERROR;
	return true;
}

bool ResolveCredentialTargetsFromMetadataFiles(
	const std::vector<xucrun::backup::MetadataEntry>& entries,
	std::vector<std::wstring>& targets, DWORD& error)
{
	targets.clear();
	for (const auto& entry : entries)
	{
		std::wstring ownerSid;
		std::wstring accountSid;
		std::wstring expectedName;
		std::wstring target;
		if (!ParseRecord(entry.contents, ownerSid, accountSid) ||
			!ComputeRecordName(entry.contents, expectedName, error) ||
			_wcsicmp(expectedName.c_str(), entry.name.c_str()) != 0 ||
			!ResolveCredentialTarget(ownerSid, accountSid, target, error))
		{
			targets.clear();
			error = ERROR_INVALID_DATA;
			return false;
		}
		targets.push_back(std::move(target));
	}
	std::sort(targets.begin(), targets.end());
	if (std::adjacent_find(targets.begin(), targets.end()) != targets.end())
	{
		targets.clear();
		error = ERROR_INVALID_DATA;
		return false;
	}
	error = NO_ERROR;
	return true;
}