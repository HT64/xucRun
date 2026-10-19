// Purpose: Creates, validates, restores, and removes encrypted broker backup archives.
#include "BrokerBackupStore.h"

#include "BrokerBackupArchive.h"
#include "BrokerDirectoryOperations.h"
#include "BrokerTargetNaming.h"
#include "CredentialMetadataStore.h"
#include "CredentialStore.h"
#include "ApprovalSerialization.h"

#include <aclapi.h>
#include <bcrypt.h>
#include <ncryptprotect.h>
#include <sddl.h>
#include <shlobj.h>

#include <algorithm>
#include <array>
#include <mutex>
#include <set>

namespace
{
	constexpr wchar_t StoreSecurity[] =
		L"O:SYD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)";
	constexpr std::size_t MaximumProtectedArchiveSize =
		xucrun::backup::MaximumArchiveSize + 64 * 1024;
	constexpr wchar_t RestoreJournalName[] = L"restore.journal";
	constexpr wchar_t RestoreJournalTemporaryName[] = L"restore.journal.tmp";

	void Erase(std::vector<std::uint8_t>& bytes)
	{
		if (!bytes.empty())
			SecureZeroMemory(bytes.data(), bytes.size());
		bytes.clear();
	}

	void LogTechnicalEvent(WORD eventType, const std::wstring& message)
	{
		OutputDebugStringW((L"xucRunBroker: " + message + L"\n").c_str());
		HANDLE source = RegisterEventSourceW(nullptr, L"xucRunBroker");
		if (!source)
			return;
		const wchar_t* strings[] = { message.c_str() };
		ReportEventW(source, eventType, 0, 1, nullptr, 1, 0, strings, nullptr);
		DeregisterEventSource(source);
	}

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

	bool OpenDirectory(const std::wstring& path, DWORD access,
		HANDLE& handle, DWORD& error)
	{
		handle = CreateFileW(path.c_str(), access,
			FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
			OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
			nullptr);
		if (handle == INVALID_HANDLE_VALUE)
		{
			error = GetLastError();
			return false;
		}
		BY_HANDLE_FILE_INFORMATION information = {};
		if (!GetFileInformationByHandle(handle, &information))
		{
			error = GetLastError();
			CloseHandle(handle);
			handle = INVALID_HANDLE_VALUE;
			return false;
		}
		if ((information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
			(information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
		{
			error = ERROR_CANT_ACCESS_FILE;
			CloseHandle(handle);
			handle = INVALID_HANDLE_VALUE;
			return false;
		}
		return true;
	}

	bool EnsureDirectory(const std::wstring& path, SECURITY_ATTRIBUTES& security,
		HANDLE& handle, DWORD& error)
	{
		if (!CreateDirectoryW(path.c_str(), &security) &&
			GetLastError() != ERROR_ALREADY_EXISTS)
		{
			error = GetLastError();
			return false;
		}
		if (!OpenDirectory(path,
			FILE_LIST_DIRECTORY | FILE_ADD_FILE | FILE_TRAVERSE | FILE_DELETE_CHILD |
			GENERIC_WRITE | READ_CONTROL | WRITE_DAC | WRITE_OWNER,
			handle, error))
		{
			return false;
		}
		PSID owner = nullptr;
		PACL dacl = nullptr;
		BOOL defaulted = FALSE;
		BOOL present = FALSE;
		if (!GetSecurityDescriptorOwner(security.lpSecurityDescriptor,
			&owner, &defaulted) || !GetSecurityDescriptorDacl(
				security.lpSecurityDescriptor, &present, &dacl, &defaulted) || !present)
		{
			error = ERROR_INVALID_SECURITY_DESCR;
			return false;
		}
		error = SetSecurityInfo(handle, SE_FILE_OBJECT,
			OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION |
			PROTECTED_DACL_SECURITY_INFORMATION, owner, nullptr, dacl, nullptr);
		if (error != ERROR_SUCCESS)
			return false;
		BrokerStoreSecurityInspection inspection;
		if (!InspectBrokerStoreDirectorySecurity(path, inspection, error) ||
			!inspection.isDirectory || inspection.reparsePoint ||
			!inspection.ownerIsSystem || !inspection.daclPresent ||
			!inspection.daclProtected || !inspection.aclEntriesMatch)
		{
			if (error == NO_ERROR)
				error = ERROR_INVALID_SECURITY_DESCR;
			return false;
		}
		return true;
	}

	struct BackupDirectoryGuard
	{
		BackupDirectoryGuard() = default;
		~BackupDirectoryGuard()
		{
			if (version != INVALID_HANDLE_VALUE)
				CloseHandle(version);
			if (backups != INVALID_HANDLE_VALUE)
				CloseHandle(backups);
			if (application != INVALID_HANDLE_VALUE)
				CloseHandle(application);
		}
		BackupDirectoryGuard(const BackupDirectoryGuard&) = delete;
		BackupDirectoryGuard& operator=(const BackupDirectoryGuard&) = delete;

		std::wstring path;
		HANDLE application = INVALID_HANDLE_VALUE;
		HANDLE backups = INVALID_HANDLE_VALUE;
		HANDLE version = INVALID_HANDLE_VALUE;
	};

	bool GetBackupDirectory(BackupDirectoryGuard& directory, DWORD& error)
	{
		PWSTR programData = nullptr;
		const HRESULT result = SHGetKnownFolderPath(
			FOLDERID_ProgramData, KF_FLAG_DEFAULT, nullptr, &programData);
		if (FAILED(result))
		{
			error = HRESULT_CODE(result);
			return false;
		}
		const std::wstring application = std::wstring(programData) + L"\\xucRun";
		CoTaskMemFree(programData);
		const std::wstring backups = application + L"\\BrokerBackups";
		directory.path = backups + L"\\" +
			xucrun::backup::GetArchiveVersionDirectoryName();
		PSECURITY_DESCRIPTOR descriptor = nullptr;
		if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
			StoreSecurity, SDDL_REVISION_1, &descriptor, nullptr))
		{
			error = GetLastError();
			return false;
		}
		SECURITY_ATTRIBUTES security = { sizeof(security), descriptor, FALSE };
		const bool success = EnsureDirectory(
			application, security, directory.application, error) &&
			EnsureDirectory(backups, security, directory.backups, error) &&
			EnsureDirectory(directory.path, security, directory.version, error);
		LocalFree(descriptor);
		if (!success && IsStoreSecurityError(error))
		{
			const DWORD originalError = error;
			error = ERROR_INVALID_SECURITY_DESCR;
			LogTechnicalEvent(EVENTLOG_WARNING_TYPE,
				L"Broker backup operation blocked because the directory security "
				L"policy could not be applied or verified. Path: " + directory.path +
				L". Original Windows error: " + std::to_wstring(originalError) +
				L". Run xucRun /audit.");
		}
		return success;
	}

	bool GetMachineBinding(std::array<std::uint8_t, 32>& binding, DWORD& error)
	{
		std::wstring value;
		for (const auto kind : { xucrun::broker_targets::TargetKind::Credential,
			xucrun::broker_targets::TargetKind::Approval,
			xucrun::broker_targets::TargetKind::AuthenticityMarker })
		{
			std::wstring prefix;
			if (!xucrun::broker_targets::GetMachineScopedPrefix(kind, prefix, error))
				return false;
			value += prefix;
		}
		BCRYPT_ALG_HANDLE algorithm = nullptr;
		BCRYPT_HASH_HANDLE hash = nullptr;
		NTSTATUS status = BCryptOpenAlgorithmProvider(
			&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
		if (status == 0)
			status = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0);
		if (status == 0)
			status = BCryptHashData(hash, reinterpret_cast<PUCHAR>(value.data()),
				static_cast<ULONG>(value.size() * sizeof(wchar_t)), 0);
		if (status == 0)
			status = BCryptFinishHash(hash, binding.data(),
				static_cast<ULONG>(binding.size()), 0);
		if (hash) BCryptDestroyHash(hash);
		if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
		error = status == 0 ? NO_ERROR : ERROR_GEN_FAILURE;
		return status == 0;
	}

	bool Protect(const std::vector<std::uint8_t>& plaintext,
		std::vector<std::uint8_t>& protectedBytes, DWORD& error)
	{
		NCRYPT_DESCRIPTOR_HANDLE descriptor = nullptr;
		SECURITY_STATUS status = NCryptCreateProtectionDescriptor(
			L"LOCAL=user", 0, &descriptor);
		PBYTE output = nullptr;
		ULONG outputSize = 0;
		if (status == ERROR_SUCCESS)
			status = NCryptProtectSecret(descriptor, 0, plaintext.data(),
				static_cast<ULONG>(plaintext.size()), nullptr, nullptr, &output, &outputSize);
		if (descriptor) NCryptCloseProtectionDescriptor(descriptor);
		if (status != ERROR_SUCCESS || !output || outputSize == 0 ||
			outputSize > MaximumProtectedArchiveSize)
		{
			if (output) LocalFree(output);
			error = status == ERROR_SUCCESS ? ERROR_INVALID_DATA : static_cast<DWORD>(status);
			return false;
		}
		protectedBytes.assign(output, output + outputSize);
		SecureZeroMemory(output, outputSize);
		LocalFree(output);
		error = NO_ERROR;
		return true;
	}

	bool Unprotect(const std::vector<std::uint8_t>& protectedBytes,
		std::vector<std::uint8_t>& plaintext, DWORD& error)
	{
		NCRYPT_DESCRIPTOR_HANDLE descriptor = nullptr;
		PBYTE output = nullptr;
		ULONG outputSize = 0;
		const SECURITY_STATUS status = NCryptUnprotectSecret(&descriptor, 0,
			protectedBytes.data(), static_cast<ULONG>(protectedBytes.size()),
			nullptr, nullptr, &output, &outputSize);
		if (descriptor) NCryptCloseProtectionDescriptor(descriptor);
		if (status != ERROR_SUCCESS || !output || outputSize == 0 ||
			outputSize > xucrun::backup::MaximumArchiveSize)
		{
			if (output)
			{
				SecureZeroMemory(output, outputSize);
				LocalFree(output);
			}
			error = status == ERROR_SUCCESS ? ERROR_INVALID_DATA : static_cast<DWORD>(status);
			return false;
		}
		plaintext.assign(output, output + outputSize);
		SecureZeroMemory(output, outputSize);
		LocalFree(output);
		error = NO_ERROR;
		return true;
	}

	bool IsValidId(const std::wstring& id)
	{
		return id.size() == xucrun::broker::BackupIdLength &&
			std::all_of(id.begin(), id.end(), [](wchar_t character) {
				return (character >= L'0' && character <= L'9') ||
					(character >= L'A' && character <= L'F') ||
					(character >= L'a' && character <= L'f');
			});
	}

	bool IsStrictTarget(const std::wstring& target, const std::wstring& prefix)
	{
		if (target.size() != prefix.size() + 64 || !target.starts_with(prefix))
			return false;
		return std::all_of(target.begin() + prefix.size(), target.end(),
			[](wchar_t character) {
				return (character >= L'0' && character <= L'9') ||
					(character >= L'A' && character <= L'F');
			});
	}

	bool GetCurrentEntryPrefix(std::size_t index,
		std::wstring& prefix, DWORD& error)
	{
		const auto kind = static_cast<xucrun::broker_targets::TargetKind>(index);
		return kind == xucrun::broker_targets::TargetKind::Approval
			? xucrun::broker_targets::GetCurrentApprovalPrefix(prefix, error)
			: xucrun::broker_targets::GetMachineScopedPrefix(kind, prefix, error);
	}

	bool IsValidUtf16Password(const std::vector<std::uint8_t>& bytes)
	{
		if (bytes.size() % sizeof(wchar_t) != 0)
			return false;
		for (std::size_t offset = 0; offset < bytes.size(); offset += 2)
		{
			const std::uint16_t character = static_cast<std::uint16_t>(bytes[offset]) |
				(static_cast<std::uint16_t>(bytes[offset + 1]) << 8);
			if (character == 0 || (character >= 0xDC00 && character <= 0xDFFF))
				return false;
			if (character >= 0xD800 && character <= 0xDBFF)
			{
				offset += 2;
				if (offset >= bytes.size())
					return false;
				const std::uint16_t low = static_cast<std::uint16_t>(bytes[offset]) |
					(static_cast<std::uint16_t>(bytes[offset + 1]) << 8);
				if (low < 0xDC00 || low > 0xDFFF)
					return false;
			}
		}
		return true;
	}

	bool IsValidAuthenticityMarkerPayload(const std::vector<std::uint8_t>& bytes)
	{
		if (bytes.size() != xucrun::broker::MaximumAuthenticityMarkerLength * 2)
			return false;
		for (std::size_t index = 0;
			index < xucrun::broker::MaximumAuthenticityMarkerLength; index++)
		{
			const std::uint16_t character = static_cast<std::uint16_t>(bytes[index * 2]) |
				(static_cast<std::uint16_t>(bytes[index * 2 + 1]) << 8);
			if ((index + 1) % 5 == 0)
			{
				if (character != L'-') return false;
			}
			else if (!((character >= L'0' && character <= L'9') ||
				(character >= L'A' && character <= L'F')))
			{
				return false;
			}
		}
		return true;
	}

	bool ValidateProtectedPayload(const xucrun::backup::CredentialEntry& entry,
		DWORD& error)
	{
		std::vector<std::uint8_t> plaintext;
		if (!xucrun::broker_store::UnprotectForValidation(
			entry.protectedBlob, plaintext, error))
		{
			LogTechnicalEvent(EVENTLOG_ERROR_TYPE,
				L"Backup payload unprotect failed for entry kind " +
				std::to_wstring(static_cast<unsigned int>(entry.kind)) +
				L" with error " + std::to_wstring(error) + L".");
			return false;
		}
		bool valid = false;
		switch (entry.kind)
		{
		case xucrun::backup::EntryKind::Credential:
			valid = IsValidUtf16Password(plaintext);
			break;
		case xucrun::backup::EntryKind::Approval:
		{
			xucrun::approval::BrokerApprovalValue approval;
			valid = xucrun::approval::DeserializeBrokerApproval(plaintext, approval);
			break;
		}
		case xucrun::backup::EntryKind::AuthenticityMarker:
			valid = IsValidAuthenticityMarkerPayload(plaintext);
			break;
		}
		Erase(plaintext);
		if (!valid)
		{
			error = ERROR_INVALID_DATA;
			LogTechnicalEvent(EVENTLOG_ERROR_TYPE,
				L"Backup payload validation failed for entry kind " +
				std::to_wstring(static_cast<unsigned int>(entry.kind)) + L".");
		}
		return valid;
	}

	bool CreateId(std::wstring& id, DWORD& error)
	{
		std::array<std::uint8_t, 16> random = {};
		if (BCryptGenRandom(nullptr, random.data(), static_cast<ULONG>(random.size()),
			BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
		{
			error = ERROR_GEN_FAILURE;
			return false;
		}
		constexpr wchar_t hex[] = L"0123456789ABCDEF";
		id.clear();
		id.reserve(random.size() * 2);
		for (const auto byte : random)
		{
			id.push_back(hex[byte >> 4]);
			id.push_back(hex[byte & 0x0f]);
		}
		error = NO_ERROR;
		return true;
	}

	bool ReadFileBytes(HANDLE directory, const std::wstring& name,
		std::vector<std::uint8_t>& bytes, DWORD& error)
	{
		HANDLE file = INVALID_HANDLE_VALUE;
		if (!xucrun::broker_directory::OpenFile(directory, name, GENERIC_READ,
			FILE_SHARE_READ, xucrun::broker_directory::OpenMode::OpenExisting,
			file, error))
		{
			return false;
		}
		BY_HANDLE_FILE_INFORMATION information = {};
		LARGE_INTEGER size = {};
		bool success = GetFileInformationByHandle(file, &information) &&
			(information.dwFileAttributes &
				(FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) == 0 &&
			GetFileSizeEx(file, &size) && size.QuadPart > 0 &&
			size.QuadPart <= MaximumProtectedArchiveSize;
		if (success)
		{
			bytes.resize(static_cast<std::size_t>(size.QuadPart));
			DWORD read = 0;
			success = ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()),
				&read, nullptr) && read == bytes.size();
		}
		if (!success) error = ERROR_INVALID_DATA;
		CloseHandle(file);
		return success;
	}

	bool ValidateArchiveForMachine(const xucrun::backup::Archive& archive, DWORD& error)
	{
		std::array<std::uint8_t, 32> binding = {};
		if (!xucrun::backup::Validate(archive) ||
			!GetMachineBinding(binding, error) || binding != archive.machineBinding ||
			!ValidateCredentialMetadataFiles(archive.metadata, error))
		{
			if (error == NO_ERROR) error = ERROR_INVALID_DATA;
			return false;
		}
		std::array<std::wstring, 3> prefixes;
		for (std::size_t index = 0; index < prefixes.size(); index++)
		{
			if (!GetCurrentEntryPrefix(index, prefixes[index], error))
				return false;
		}
		std::vector<std::wstring> metadataTargets;
		if (!ResolveCredentialTargetsFromMetadataFiles(
			archive.metadata, metadataTargets, error))
		{
			return false;
		}
		std::vector<std::wstring> credentialTargets;
		for (const auto& entry : archive.credentials)
		{
			const std::size_t index = static_cast<std::size_t>(entry.kind) - 1;
			if (index >= prefixes.size() ||
				!IsStrictTarget(entry.target, prefixes[index]))
			{
				LogTechnicalEvent(EVENTLOG_ERROR_TYPE,
					L"Backup target validation failed for entry kind " +
					std::to_wstring(static_cast<unsigned int>(entry.kind)) + L".");
				if (error == NO_ERROR) error = ERROR_INVALID_DATA;
				return false;
			}
			if (!ValidateProtectedPayload(entry, error))
				return false;
			if (entry.kind == xucrun::backup::EntryKind::Credential)
				credentialTargets.push_back(entry.target);
		}
		std::sort(credentialTargets.begin(), credentialTargets.end());
		if (credentialTargets != metadataTargets)
		{
			LogTechnicalEvent(EVENTLOG_ERROR_TYPE,
				L"Backup credential metadata binding validation failed (credentials: " +
				std::to_wstring(credentialTargets.size()) + L", metadata: " +
				std::to_wstring(metadataTargets.size()) + L").");
			error = ERROR_INVALID_DATA;
			return false;
		}
		error = NO_ERROR;
		return true;
	}

	bool LoadArchive(HANDLE directory, const std::wstring& id,
		xucrun::backup::Archive& archive, DWORD& error)
	{
		if (!IsValidId(id))
		{
			error = ERROR_INVALID_PARAMETER;
			return false;
		}
		std::vector<std::uint8_t> protectedBytes;
		std::vector<std::uint8_t> plaintext;
		const bool success = ReadFileBytes(directory, id + L".bak",
			protectedBytes, error) && Unprotect(protectedBytes, plaintext, error) &&
			xucrun::backup::Deserialize(plaintext, archive) &&
			ValidateArchiveForMachine(archive, error);
		Erase(plaintext);
		if (!success && error == NO_ERROR) error = ERROR_INVALID_DATA;
		return success;
	}

	bool Capture(xucrun::backup::Archive& archive, DWORD& error)
	{
		archive = {};
		FILETIME now = {};
		GetSystemTimeAsFileTime(&now);
		ULARGE_INTEGER value = {};
		value.LowPart = now.dwLowDateTime;
		value.HighPart = now.dwHighDateTime;
		archive.createdAt = value.QuadPart;
		if (!GetMachineBinding(archive.machineBinding, error))
			return false;
		for (std::size_t index = 0; index < 3; index++)
		{
			std::wstring prefix;
			std::vector<std::wstring> targets;
			if (!GetCurrentEntryPrefix(index, prefix, error) ||
				!xucrun::broker_store::EnumerateTargets(prefix, targets, error))
				return false;
			for (const auto& target : targets)
			{
				xucrun::backup::CredentialEntry entry;
				entry.kind = static_cast<xucrun::backup::EntryKind>(index + 1);
				entry.target = target;
				if (!xucrun::broker_store::ReadProtected(
					target, entry.protectedBlob, error))
					return false;
				archive.credentials.push_back(std::move(entry));
			}
		}
		std::uint32_t deletedMetadataCount = 0;
		if (!DeleteOrphanedCredentialMetadata(deletedMetadataCount, error))
		{
			return false;
		}
		if (deletedMetadataCount != 0)
		{
			LogTechnicalEvent(EVENTLOG_WARNING_TYPE,
				L"Removed " + std::to_wstring(deletedMetadataCount) +
				L" orphaned credential metadata entries before backup capture.");
		}
		if (!ReadAllCredentialMetadataFiles(archive.metadata, error))
			return false;
		std::sort(archive.credentials.begin(), archive.credentials.end(),
			[](const auto& left, const auto& right) { return left.target < right.target; });
		std::sort(archive.metadata.begin(), archive.metadata.end(),
			[](const auto& left, const auto& right) { return left.name < right.name; });
		return ValidateArchiveForMachine(archive, error);
	}

	xucrun::broker::BackupEntry Describe(const std::wstring& id,
		const xucrun::backup::Archive& archive)
	{
		xucrun::broker::BackupEntry result;
		result.id = id;
		result.description = archive.description;
		result.createdAt = archive.createdAt;
		result.metadataCount = static_cast<std::uint32_t>(archive.metadata.size());
		for (const auto& entry : archive.credentials)
		{
			switch (entry.kind)
			{
			case xucrun::backup::EntryKind::Credential: result.credentialCount++; break;
			case xucrun::backup::EntryKind::Approval: result.approvalCount++; break;
			case xucrun::backup::EntryKind::AuthenticityMarker:
				result.authenticityMarkerCount++; break;
			}
		}
		return result;
	}

	bool DeleteOrphanedTemporaryArchives(HANDLE directory, DWORD& error)
	{
		std::vector<xucrun::broker_directory::Entry> files;
		if (!xucrun::broker_directory::Enumerate(directory, files, error))
			return false;
		std::uint32_t deleted = 0;
		for (const auto& file : files)
		{
			const std::wstring& name = file.name;
			if (name.size() != xucrun::broker::BackupIdLength + 4 ||
				!name.ends_with(L".tmp") ||
				!IsValidId(name.substr(0, xucrun::broker::BackupIdLength)))
			{
				continue;
			}
			if ((file.attributes &
				(FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0)
			{
				error = ERROR_INVALID_DATA;
				return false;
			}
			if (!xucrun::broker_directory::DeleteFile(directory, name, false, error))
				return false;
			deleted++;
		}
		if (deleted != 0)
		{
			LogTechnicalEvent(EVENTLOG_WARNING_TYPE,
				L"Removed " + std::to_wstring(deleted) +
				L" orphaned temporary backup archives.");
		}
		error = NO_ERROR;
		return true;
	}

	bool SaveArchive(HANDLE directory,
		const xucrun::backup::Archive& archive, std::wstring& id, DWORD& error)
	{
		std::vector<std::uint8_t> plaintext = xucrun::backup::Serialize(archive);
		std::vector<std::uint8_t> protectedBytes;
		if (plaintext.empty() || !Protect(plaintext, protectedBytes, error))
		{
			Erase(plaintext);
			return false;
		}
		Erase(plaintext);
		if (!CreateId(id, error))
			return false;
		const std::wstring temporaryName = id + L".tmp";
		const std::wstring finalName = id + L".bak";
		HANDLE file = INVALID_HANDLE_VALUE;
		if (!xucrun::broker_directory::OpenFile(directory, temporaryName,
			GENERIC_WRITE | DELETE, 0,
			xucrun::broker_directory::OpenMode::CreateNew, file, error))
		{
			return false;
		}
		DWORD written = 0;
		bool success = WriteFile(file, protectedBytes.data(),
			static_cast<DWORD>(protectedBytes.size()), &written, nullptr) == TRUE;
		if (!success)
			error = GetLastError();
		else if (written != protectedBytes.size())
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
				file, directory, finalName, error) && FlushFileBuffers(file) &&
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
		return success;
	}

	bool WriteRestoreJournal(HANDLE directory,
		const std::wstring& fallbackId, DWORD& error)
	{
		if (!xucrun::broker_directory::DeleteFile(
			directory, RestoreJournalTemporaryName, true, error))
		{
			return false;
		}
		HANDLE file = INVALID_HANDLE_VALUE;
		if (!xucrun::broker_directory::OpenFile(directory,
			RestoreJournalTemporaryName, GENERIC_WRITE | DELETE, 0,
			xucrun::broker_directory::OpenMode::CreateNew, file, error))
		{
			return false;
		}
		const auto* bytes = reinterpret_cast<const std::uint8_t*>(fallbackId.data());
		const DWORD size = static_cast<DWORD>(fallbackId.size() * sizeof(wchar_t));
		DWORD written = 0;
		bool success = WriteFile(file, bytes, size, &written, nullptr) == TRUE;
		if (!success)
			error = GetLastError();
		else if (written != size)
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
				file, directory, RestoreJournalName, error) && FlushFileBuffers(file) &&
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
		return success;
	}

	bool ReadRestoreJournal(HANDLE directory,
		std::wstring& fallbackId, bool& exists, DWORD& error)
	{
		fallbackId.clear();
		exists = false;
		HANDLE file = INVALID_HANDLE_VALUE;
		if (!xucrun::broker_directory::OpenFile(directory, RestoreJournalName,
			GENERIC_READ, FILE_SHARE_READ,
			xucrun::broker_directory::OpenMode::OpenExisting, file, error))
		{
			if (error == ERROR_FILE_NOT_FOUND)
			{
				error = NO_ERROR;
				return true;
			}
			return false;
		}
		exists = true;
		BY_HANDLE_FILE_INFORMATION information = {};
		LARGE_INTEGER fileSize = {};
		std::array<wchar_t, xucrun::broker::BackupIdLength> id = {};
		DWORD read = 0;
		const bool success = GetFileInformationByHandle(file, &information) &&
			(information.dwFileAttributes &
				(FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) == 0 &&
			GetFileSizeEx(file, &fileSize) && fileSize.QuadPart == sizeof(id) &&
			ReadFile(file, id.data(), static_cast<DWORD>(sizeof(id)), &read, nullptr) &&
			read == sizeof(id);
		CloseHandle(file);
		if (!success)
		{
			error = ERROR_INVALID_DATA;
			return false;
		}
		fallbackId.assign(id.begin(), id.end());
		if (!IsValidId(fallbackId))
		{
			error = ERROR_INVALID_DATA;
			return false;
		}
		error = NO_ERROR;
		return true;
	}

	bool DeleteRestoreJournal(HANDLE directory, DWORD& error)
	{
		return xucrun::broker_directory::DeleteFile(
			directory, RestoreJournalName, false, error) &&
			xucrun::broker_directory::Flush(directory, error);
	}

	bool Apply(const xucrun::backup::Archive& archive, DWORD& error)
	{
		if (!ValidateArchiveForMachine(archive, error))
			return false;
		for (std::size_t index = 0; index < 3; index++)
		{
			std::wstring prefix;
			if (!xucrun::broker_targets::GetMachineScopedPrefix(
				static_cast<xucrun::broker_targets::TargetKind>(index), prefix, error) ||
				!xucrun::broker_store::DeleteAllWithPrefix(prefix, error))
				return false;
		}
		for (const auto& entry : archive.credentials)
		{
			if (!xucrun::broker_store::WriteProtected(
				entry.target, entry.protectedBlob, error))
				return false;
		}
		return ReplaceAllCredentialMetadataFiles(archive.metadata, error);
	}

	bool Equivalent(const xucrun::backup::Archive& left,
		const xucrun::backup::Archive& right)
	{
		return left.machineBinding == right.machineBinding &&
			left.credentials.size() == right.credentials.size() &&
			left.metadata.size() == right.metadata.size() &&
			std::equal(left.credentials.begin(), left.credentials.end(),
				right.credentials.begin(), [](const auto& a, const auto& b) {
					return a.kind == b.kind && a.target == b.target &&
						a.protectedBlob == b.protectedBlob;
				}) &&
			std::equal(left.metadata.begin(), left.metadata.end(),
				right.metadata.begin(), [](const auto& a, const auto& b) {
					return a.name == b.name && a.contents == b.contents;
				});
	}
}

namespace xucrun::backup_store
{
	bool RunSecuritySelfTest(DWORD& error)
	{
		error = NO_ERROR;
		wchar_t temporaryPath[MAX_PATH] = {};
		wchar_t temporaryName[MAX_PATH] = {};
		if (GetTempPathW(ARRAYSIZE(temporaryPath), temporaryPath) == 0 ||
			GetTempFileNameW(temporaryPath, L"xub", 0, temporaryName) == 0 ||
			!DeleteFileW(temporaryName) || !CreateDirectoryW(temporaryName, nullptr))
		{
			error = GetLastError();
			return false;
		}

		const std::wstring directoryPath = temporaryName;
		const std::wstring movedPath = directoryPath + L"-moved";
		HANDLE directory = INVALID_HANDLE_VALUE;
		bool success = OpenDirectory(directoryPath,
			FILE_LIST_DIRECTORY | FILE_ADD_FILE | FILE_TRAVERSE |
			FILE_DELETE_CHILD | READ_CONTROL | FILE_READ_ATTRIBUTES,
			directory, error);
		bool directoryMoved = false;
		if (success)
		{
			directoryMoved = MoveFileExW(
				directoryPath.c_str(), movedPath.c_str(), 0) == TRUE;
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
			const std::wstring finalProbeName = L"bound-final.tmp";
			HANDLE probe = INVALID_HANDLE_VALUE;
			if (!CreateDirectoryW(directoryPath.c_str(), nullptr) ||
				!xucrun::broker_directory::OpenFile(directory, probeName,
					GENERIC_WRITE | DELETE, 0,
					xucrun::broker_directory::OpenMode::CreateNew, probe, error))
			{
				if (error == NO_ERROR)
					error = GetLastError();
				success = false;
			}
			const std::uint8_t probeValue = 0x5a;
			DWORD written = 0;
			if (success && (!WriteFile(probe, &probeValue, sizeof(probeValue),
				&written, nullptr) || written != sizeof(probeValue) ||
				!FlushFileBuffers(probe) ||
				!xucrun::broker_directory::RenameFile(
					probe, directory, finalProbeName, error) ||
				!xucrun::broker_directory::Flush(directory, error)))
			{
				if (error == NO_ERROR)
					error = GetLastError();
				success = false;
			}
			if (probe != INVALID_HANDLE_VALUE)
				CloseHandle(probe);
			if (success &&
				(GetFileAttributesW((movedPath + L"\\" + finalProbeName).c_str()) ==
					INVALID_FILE_ATTRIBUTES ||
				 GetFileAttributesW((directoryPath + L"\\" + finalProbeName).c_str()) !=
					INVALID_FILE_ATTRIBUTES))
			{
				error = ERROR_INVALID_REPARSE_DATA;
				success = false;
			}
			std::vector<xucrun::broker_directory::Entry> files;
			if (success && (!xucrun::broker_directory::Enumerate(directory, files, error) ||
				std::none_of(files.begin(), files.end(), [&](const auto& entry)
				{
					return entry.name == finalProbeName;
				})))
			{
				if (error == NO_ERROR)
					error = ERROR_FILE_NOT_FOUND;
				success = false;
			}
			HANDLE readProbe = INVALID_HANDLE_VALUE;
			if (success && !xucrun::broker_directory::OpenFile(directory,
				finalProbeName, GENERIC_READ, FILE_SHARE_READ,
				xucrun::broker_directory::OpenMode::OpenExisting, readProbe, error))
			{
				success = false;
			}
			std::uint8_t readValue = 0;
			DWORD read = 0;
			if (success && (!ReadFile(readProbe, &readValue, sizeof(readValue),
				&read, nullptr) || read != sizeof(readValue) || readValue != probeValue))
			{
				error = ERROR_INVALID_DATA;
				success = false;
			}
			if (readProbe != INVALID_HANDLE_VALUE)
				CloseHandle(readProbe);
			DWORD deleteError = NO_ERROR;
			xucrun::broker_directory::DeleteFile(
				directory, probeName, true, deleteError);
			if (!xucrun::broker_directory::DeleteFile(
				directory, finalProbeName, true, deleteError) && success)
			{
				error = deleteError;
				success = false;
			}
			const std::wstring orphanedArchiveName(
				xucrun::broker::BackupIdLength, L'A');
			const std::wstring orphanedTemporaryName = orphanedArchiveName + L".tmp";
			for (const std::wstring& name : {
				orphanedTemporaryName, std::wstring(RestoreJournalTemporaryName) })
			{
				HANDLE temporaryFile = INVALID_HANDLE_VALUE;
				if (success && !xucrun::broker_directory::OpenFile(directory, name,
					GENERIC_WRITE | DELETE, 0,
					xucrun::broker_directory::OpenMode::CreateNew,
					temporaryFile, error))
				{
					success = false;
				}
				if (temporaryFile != INVALID_HANDLE_VALUE)
					CloseHandle(temporaryFile);
			}
			if (success && !DeleteOrphanedTemporaryArchives(directory, error))
				success = false;
			files.clear();
			if (success && (!xucrun::broker_directory::Enumerate(directory, files, error) ||
				std::any_of(files.begin(), files.end(), [&](const auto& entry)
				{
					return entry.name == orphanedTemporaryName;
				}) ||
				std::none_of(files.begin(), files.end(), [&](const auto& entry)
				{
					return entry.name == RestoreJournalTemporaryName;
				})))
			{
				if (error == NO_ERROR)
					error = ERROR_INVALID_DATA;
				success = false;
			}
			xucrun::broker_directory::DeleteFile(
				directory, orphanedTemporaryName, true, deleteError);
			xucrun::broker_directory::DeleteFile(
				directory, RestoreJournalTemporaryName, true, deleteError);
		}
		if (directory != INVALID_HANDLE_VALUE)
		{
			CloseHandle(directory);
			directory = INVALID_HANDLE_VALUE;
		}
		if (directoryMoved)
		{
			RemoveDirectoryW(directoryPath.c_str());
			if (!MoveFileExW(movedPath.c_str(), directoryPath.c_str(), 0))
			{
				if (success)
					error = GetLastError();
				success = false;
			}
		}
		if (directory != INVALID_HANDLE_VALUE)
			CloseHandle(directory);
		RemoveDirectoryW(directoryPath.c_str());
		RemoveDirectoryW(movedPath.c_str());
		if (success)
			error = NO_ERROR;
		return success;
	}
	bool Create(const std::wstring& description,
		xucrun::broker::BackupEntry& entry, DWORD& error)
	{
		std::lock_guard lock(xucrun::broker_store::TransactionMutex());
		BackupDirectoryGuard directory;
		std::vector<xucrun::broker::BackupEntry> existing;
		if (!GetBackupDirectory(directory, error) ||
			!DeleteOrphanedTemporaryArchives(directory.version, error) ||
			!List(existing, error))
			return false;
		if (existing.size() >= xucrun::broker::MaximumBackupEntries)
		{
			error = ERROR_TOO_MANY_NAMES;
			return false;
		}
		xucrun::backup::Archive archive;
		std::wstring id;
		if (!Capture(archive, error))
			return false;
		archive.description = description;
		if (!xucrun::backup::Validate(archive) ||
			!SaveArchive(directory.version, archive, id, error))
		{
			if (error == NO_ERROR) error = ERROR_INVALID_DATA;
			return false;
		}
		entry = Describe(id, archive);
		return true;
	}

	bool List(std::vector<xucrun::broker::BackupEntry>& entries, DWORD& error)
	{
		std::lock_guard lock(xucrun::broker_store::TransactionMutex());
		entries.clear();
		BackupDirectoryGuard directory;
		if (!GetBackupDirectory(directory, error))
			return false;
		std::vector<xucrun::broker_directory::Entry> files;
		if (!xucrun::broker_directory::Enumerate(directory.version, files, error))
			return false;
		bool success = true;
		for (const auto& file : files)
		{
			const std::wstring& name = file.name;
			if (name == L"." || name == L".." || !name.ends_with(L".bak"))
				continue;
			if ((file.attributes &
				(FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0 ||
				name.size() != xucrun::broker::BackupIdLength + 4 ||
				entries.size() >= xucrun::broker::MaximumBackupEntries)
			{
				error = ERROR_INVALID_DATA;
				success = false;
				break;
			}
			const std::wstring id = name.substr(0, xucrun::broker::BackupIdLength);
			xucrun::backup::Archive archive;
			if (!LoadArchive(directory.version, id, archive, error))
			{
				success = false;
				break;
			}
			entries.push_back(Describe(id, archive));
		}
		if (!success) { entries.clear(); return false; }
		std::sort(entries.begin(), entries.end(), [](const auto& left, const auto& right) {
			return left.createdAt > right.createdAt;
		});
		error = NO_ERROR;
		return true;
	}

	bool Restore(const std::wstring& id, bool createFallbackBackup,
		std::wstring& fallbackBackupId, DWORD& error)
	{
		std::lock_guard lock(xucrun::broker_store::TransactionMutex());
		fallbackBackupId.clear();
		BackupDirectoryGuard directory;
		xucrun::backup::Archive selected;
		if (!GetBackupDirectory(directory, error) ||
			!LoadArchive(directory.version, id, selected, error))
			return false;
		std::wstring pendingFallbackId;
		bool journalExists = false;
		if (!ReadRestoreJournal(
			directory.version, pendingFallbackId, journalExists, error))
		{
			return false;
		}
		if (journalExists)
		{
			error = ERROR_BUSY;
			return false;
		}
		if (createFallbackBackup)
		{
			std::vector<xucrun::broker::BackupEntry> existing;
			if (!DeleteOrphanedTemporaryArchives(directory.version, error) ||
				!List(existing, error))
				return false;
			if (existing.size() >= xucrun::broker::MaximumBackupEntries)
			{
				error = ERROR_TOO_MANY_NAMES;
				return false;
			}
		}
		xucrun::backup::Archive fallback;
		if (!Capture(fallback, error))
			return false;
		if (createFallbackBackup)
		{
			if (!SaveArchive(directory.version, fallback, fallbackBackupId, error))
				return false;
			xucrun::backup::Archive persistedFallback;
			if (!LoadArchive(directory.version, fallbackBackupId, persistedFallback, error) ||
				!Equivalent(fallback, persistedFallback) ||
				!WriteRestoreJournal(directory.version, fallbackBackupId, error))
			{
				if (error == NO_ERROR) error = ERROR_INVALID_DATA;
				return false;
			}
			LogTechnicalEvent(EVENTLOG_INFORMATION_TYPE,
				L"Restore journal persisted for fallback backup " + fallbackBackupId + L".");
		}
		xucrun::backup::Archive verification;
		if (Apply(selected, error) && Capture(verification, error) &&
			Equivalent(selected, verification) &&
			(!createFallbackBackup || DeleteRestoreJournal(directory.version, error)))
		{
			LogTechnicalEvent(EVENTLOG_INFORMATION_TYPE,
				createFallbackBackup
					? L"Backup " + id + L" restored and verified; restore journal removed."
					: L"Backup " + id + L" restored and verified without fallback backup.");
			return true;
		}
		const DWORD restoreError = error == NO_ERROR ? ERROR_INVALID_DATA : error;
		verification = {};
		if (!Apply(fallback, error) || !Capture(verification, error) ||
			!Equivalent(fallback, verification) ||
			(createFallbackBackup && !DeleteRestoreJournal(directory.version, error)))
		{
			LogTechnicalEvent(EVENTLOG_ERROR_TYPE,
				(createFallbackBackup
					? L"Restore rollback failed; restore journal remains. Error "
					: L"Restore rollback failed without persisted fallback. Error ") +
				std::to_wstring(error) + L".");
			error = ERROR_WRITE_FAULT;
			return false;
		}
		LogTechnicalEvent(EVENTLOG_WARNING_TYPE,
			createFallbackBackup
				? L"Backup restore failed and was rolled back to fallback backup " +
					fallbackBackupId + L"."
				: L"Backup restore failed and was rolled back to the captured state.");
		error = restoreError;
		return false;
	}

	bool RecoverPendingRestore(bool& recovered, DWORD& error)
	{
		std::lock_guard lock(xucrun::broker_store::TransactionMutex());
		recovered = false;
		BackupDirectoryGuard directory;
		std::wstring fallbackId;
		bool journalExists = false;
		if (!GetBackupDirectory(directory, error) ||
			!ReadRestoreJournal(directory.version, fallbackId, journalExists, error))
		{
			return false;
		}
		if (!journalExists)
			return true;
		LogTechnicalEvent(EVENTLOG_WARNING_TYPE,
			L"Pending restore journal found; rolling back to fallback backup " +
			fallbackId + L".");
		xucrun::backup::Archive fallback;
		xucrun::backup::Archive verification;
		if (!LoadArchive(directory.version, fallbackId, fallback, error) ||
			!Apply(fallback, error) || !Capture(verification, error) ||
			!Equivalent(fallback, verification) ||
			!DeleteRestoreJournal(directory.version, error))
		{
			LogTechnicalEvent(EVENTLOG_ERROR_TYPE,
				L"Pending restore recovery failed. Error " +
				std::to_wstring(error) + L".");
			if (error == NO_ERROR) error = ERROR_WRITE_FAULT;
			return false;
		}
		recovered = true;
		LogTechnicalEvent(EVENTLOG_INFORMATION_TYPE,
			L"Pending restore rolled back and verified; restore journal removed.");
		error = NO_ERROR;
		return true;
	}

	bool Delete(const std::wstring& id, DWORD& error)
	{
		std::lock_guard lock(xucrun::broker_store::TransactionMutex());
		if (!IsValidId(id))
		{
			error = ERROR_INVALID_PARAMETER;
			return false;
		}
		BackupDirectoryGuard directory;
		if (!GetBackupDirectory(directory, error))
			return false;
		std::wstring pendingFallbackId;
		bool journalExists = false;
		if (!ReadRestoreJournal(
			directory.version, pendingFallbackId, journalExists, error))
		{
			return false;
		}
		if (journalExists)
		{
			error = ERROR_BUSY;
			return false;
		}
		return xucrun::broker_directory::DeleteFile(
			directory.version, id + L".bak", false, error);
	}

	bool DeleteAll(DWORD& error)
	{
		std::lock_guard lock(xucrun::broker_store::TransactionMutex());
		BackupDirectoryGuard directory;
		if (!GetBackupDirectory(directory, error))
			return false;
		std::wstring pendingFallbackId;
		bool journalExists = false;
		if (!ReadRestoreJournal(
			directory.version, pendingFallbackId, journalExists, error))
		{
			return false;
		}
		if (journalExists)
		{
			error = ERROR_BUSY;
			return false;
		}
		std::vector<xucrun::broker_directory::Entry> files;
		if (!xucrun::broker_directory::Enumerate(directory.version, files, error))
			return false;
		bool success = true;
		for (const auto& file : files)
		{
			const std::wstring& name = file.name;
			if (name == L"." || name == L"..") continue;
			if ((file.attributes &
				(FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0 ||
				(!name.ends_with(L".bak") && !name.ends_with(L".tmp")))
			{
				error = ERROR_INVALID_DATA;
				success = false;
				break;
			}
			if (!xucrun::broker_directory::DeleteFile(
				directory.version, name, false, error))
			{
				success = false;
				break;
			}
		}
		if (success) error = NO_ERROR;
		return success;
	}
}