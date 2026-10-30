// Purpose: Creates and validates broker-owned temporary batch wrappers for approved command execution.
// Approach: Exercises the behavior with focused assertions against the shared implementation and Windows contracts.
#include "BrokerTemporaryBatch.h"

#include "../xucRunSharedResources/BuildVersion.h"
#include "FileIdentity.h"

#include <aclapi.h>
#include <bcrypt.h>
#include <sddl.h>
#include <userenv.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cwctype>
#include <ctime>
#include <mutex>
#include <new>

namespace
{
	constexpr std::size_t MaximumRetainedWrappers = 64;
	constexpr std::size_t MaximumWrapperFilesPerDirectory = 128;
	constexpr wchar_t WrapperPrefix[] = L"xucRun_wrapper_";

	struct RetainedWrapper
	{
		HANDLE process = nullptr;
		HANDLE file = INVALID_HANDLE_VALUE;
		HANDLE originalFile = INVALID_HANDLE_VALUE;
		std::vector<HANDLE> directories;
		std::wstring path;
	};
	std::atomic_size_t retainedWrapperCount = 0;
	std::mutex wrapperCreationMutex;

	void CloseDirectories(std::vector<HANDLE>& directories) noexcept
	{
		for (auto entry = directories.rbegin(); entry != directories.rend(); ++entry)
		{
			if (*entry && *entry != INVALID_HANDLE_VALUE)
				CloseHandle(*entry);
		}
		directories.clear();
	}

	void ReleaseRetainedWrapper(RetainedWrapper& wrapper) noexcept
	{
		if (wrapper.file && wrapper.file != INVALID_HANDLE_VALUE)
			CloseHandle(wrapper.file);
		wrapper.file = INVALID_HANDLE_VALUE;
		if (wrapper.originalFile && wrapper.originalFile != INVALID_HANDLE_VALUE)
			CloseHandle(wrapper.originalFile);
		wrapper.originalFile = INVALID_HANDLE_VALUE;
		if (!wrapper.path.empty())
			DeleteFileW(wrapper.path.c_str());
		CloseDirectories(wrapper.directories);
		if (wrapper.process)
			CloseHandle(wrapper.process);
		wrapper.process = nullptr;
	}

	DWORD WINAPI WaitForRetainedWrapper(void* context) noexcept
	{
		auto* wrapper = static_cast<RetainedWrapper*>(context);
		WaitForSingleObject(wrapper->process, INFINITE);
		ReleaseRetainedWrapper(*wrapper);
		delete wrapper;
		retainedWrapperCount.fetch_sub(1);
		return 0;
	}

	std::wstring GetFileName(const std::wstring& path)
	{
		const std::size_t separator = path.find_last_of(L"\\/");
		return separator == std::wstring::npos ? path : path.substr(separator + 1);
	}

	bool HasCmdMetacharacter(const std::wstring& value)
	{
		return value.find_first_of(L"&|()!%^") != std::wstring::npos;
	}

	bool IsBatchFile(const std::wstring& path)
	{
		std::wstring lower = path;
		std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
		return lower.ends_with(L".bat") || lower.ends_with(L".cmd");
	}

	bool IsCommandShell(const std::wstring& path)
	{
		std::wstring name = GetFileName(path);
		std::transform(name.begin(), name.end(), name.begin(), ::towlower);
		return name == L"cmd" || name == L"cmd.exe";
	}

	std::size_t FindCommandSwitch(const std::wstring& arguments)
	{
		for (std::size_t position = 0; position + 1 < arguments.size(); position++)
		{
			if (arguments[position] != L'/' ||
				(std::towlower(arguments[position + 1]) != L'c' &&
				 std::towlower(arguments[position + 1]) != L'k'))
			{
				continue;
			}
			if ((position == 0 || std::iswspace(arguments[position - 1])) &&
				(position + 2 == arguments.size() ||
				 std::iswspace(arguments[position + 2])))
			{
				return position;
			}
		}
		return std::wstring::npos;
	}

	bool ReadTokenSid(HANDLE token, std::wstring& sid, DWORD& error)
	{
		DWORD size = 0;
		GetTokenInformation(token, TokenUser, nullptr, 0, &size);
		if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || size == 0)
		{
			error = GetLastError();
			return false;
		}
		std::vector<std::uint8_t> buffer(size);
		if (!GetTokenInformation(token, TokenUser, buffer.data(), size, &size))
		{
			error = GetLastError();
			return false;
		}
		wchar_t* text = nullptr;
		if (!ConvertSidToStringSidW(
			reinterpret_cast<TOKEN_USER*>(buffer.data())->User.Sid, &text))
		{
			error = GetLastError();
			return false;
		}
		sid = text;
		LocalFree(text);
		return true;
	}

	bool CanTokenTraverseDirectory(HANDLE token, const std::wstring& path)
	{
		if (!ImpersonateLoggedOnUser(token))
			return false;
		HANDLE directory = CreateFileW(path.c_str(),
			FILE_TRAVERSE | FILE_READ_ATTRIBUTES,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
		const bool accessible = directory != INVALID_HANDLE_VALUE;
		if (directory != INVALID_HANDLE_VALUE)
			CloseHandle(directory);
		const bool reverted = RevertToSelf() == TRUE;
		return accessible && reverted;
	}

	std::wstring FindRamDisk(HANDLE targetToken)
	{
		std::array<wchar_t, 512> drives = {};
		const DWORD length = GetLogicalDriveStringsW(
			static_cast<DWORD>(drives.size() - 1), drives.data());
		if (length == 0 || length >= drives.size())
			return {};
		for (const wchar_t* drive = drives.data(); *drive;
			drive += wcslen(drive) + 1)
		{
			if (GetDriveTypeW(drive) != DRIVE_FIXED &&
				GetDriveTypeW(drive) != DRIVE_RAMDISK)
			{
				continue;
			}
			std::array<wchar_t, MAX_PATH + 1> volumeName = {};
			DWORD fileSystemFlags = 0;
			if (GetVolumeInformationW(drive, volumeName.data(),
				static_cast<DWORD>(volumeName.size()), nullptr, nullptr,
				&fileSystemFlags, nullptr, 0) &&
				_wcsicmp(volumeName.data(), L"RamDisk") == 0 &&
				(fileSystemFlags & FS_PERSISTENT_ACLS) != 0 &&
				CanTokenTraverseDirectory(targetToken, drive))
			{
				return drive;
			}
		}
		return {};
	}

	bool ReadEnvironmentValue(HANDLE token, const wchar_t* name,
		std::wstring& value, DWORD& error)
	{
		void* environment = nullptr;
		if (!CreateEnvironmentBlock(&environment, token, FALSE))
		{
			error = GetLastError();
			return false;
		}
		const std::wstring prefix = std::wstring(name) + L"=";
		for (const wchar_t* entry = static_cast<const wchar_t*>(environment);
			*entry; entry += wcslen(entry) + 1)
		{
			if (_wcsnicmp(entry, prefix.c_str(), prefix.size()) == 0)
			{
				value = entry + prefix.size();
				break;
			}
		}
		DestroyEnvironmentBlock(environment);
		if (value.empty())
		{
			error = ERROR_ENVVAR_NOT_FOUND;
			return false;
		}
		return true;
	}

	bool ReadDirectoryInformation(HANDLE directory,
		BY_HANDLE_FILE_INFORMATION& information, DWORD& error)
	{
		if (!GetFileInformationByHandle(directory, &information))
		{
			error = GetLastError();
			return false;
		}
		if ((information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
			(information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
		{
			error = ERROR_CANT_ACCESS_FILE;
			return false;
		}
		return true;
	}

	bool SameFileObject(const BY_HANDLE_FILE_INFORMATION& left,
		const BY_HANDLE_FILE_INFORMATION& right)
	{
		return left.dwVolumeSerialNumber == right.dwVolumeSerialNumber &&
			left.nFileIndexHigh == right.nFileIndexHigh &&
			left.nFileIndexLow == right.nFileIndexLow;
	}

	bool SameDirectoryPath(std::wstring left, std::wstring right)
	{
		while (!left.empty() && (left.back() == L'\\' || left.back() == L'/'))
			left.pop_back();
		while (!right.empty() && (right.back() == L'\\' || right.back() == L'/'))
			right.pop_back();
		return _wcsicmp(left.c_str(), right.c_str()) == 0;
	}

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

	bool OpenDirectoryLocked(const std::wstring& path, HANDLE& handle,
		BY_HANDLE_FILE_INFORMATION& information, DWORD& error)
	{
		handle = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES | SYNCHRONIZE,
			FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
			FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
		if (handle == INVALID_HANDLE_VALUE)
		{
			error = GetLastError();
			return false;
		}
		if (!ReadDirectoryInformation(handle, information, error))
		{
			CloseHandle(handle);
			handle = INVALID_HANDLE_VALUE;
			return false;
		}
		return true;
	}

	bool LockDirectoryChain(const std::wstring& requestedPath,
		std::wstring& finalDirectory, std::vector<HANDLE>& handles, DWORD& error)
	{
		HANDLE requested = INVALID_HANDLE_VALUE;
		BY_HANDLE_FILE_INFORMATION requestedInformation = {};
		if (!OpenDirectoryLocked(
			requestedPath, requested, requestedInformation, error))
		{
			return false;
		}
		if (!xucrun::files::ResolveFinalFilePathFromHandle(requested, finalDirectory) ||
			finalDirectory.size() < 3 || finalDirectory[1] != L':' ||
			(finalDirectory[2] != L'\\' && finalDirectory[2] != L'/'))
		{
			error = GetLastError() == NO_ERROR ? ERROR_BAD_PATHNAME : GetLastError();
			CloseHandle(requested);
			return false;
		}
		while (finalDirectory.size() > 3 &&
			(finalDirectory.back() == L'\\' || finalDirectory.back() == L'/'))
		{
			finalDirectory.pop_back();
		}

		std::vector<std::wstring> components;
		components.push_back(finalDirectory.substr(0, 3));
		for (std::size_t end = finalDirectory.find(L'\\', 3);
			end != std::wstring::npos; end = finalDirectory.find(L'\\', end + 1))
		{
			components.push_back(finalDirectory.substr(0, end));
		}
		if (_wcsicmp(components.back().c_str(), finalDirectory.c_str()) != 0)
			components.push_back(finalDirectory);

		for (const std::wstring& component : components)
		{
			HANDLE handle = INVALID_HANDLE_VALUE;
			BY_HANDLE_FILE_INFORMATION information = {};
			if (!OpenDirectoryLocked(component, handle, information, error))
			{
				CloseHandle(requested);
				CloseDirectories(handles);
				return false;
			}
			handles.push_back(handle);
		}
		BY_HANDLE_FILE_INFORMATION finalInformation = {};
		const bool same = ReadDirectoryInformation(
			handles.back(), finalInformation, error) &&
			SameFileObject(requestedInformation, finalInformation);
		CloseHandle(requested);
		if (!same)
		{
			CloseDirectories(handles);
			if (error == NO_ERROR)
				error = ERROR_CANT_ACCESS_FILE;
			return false;
		}
		return true;
	}

	bool CountExistingWrappers(const std::wstring& directory,
		std::size_t& count, DWORD& error)
	{
		count = 0;
		WIN32_FIND_DATAW data = {};
		HANDLE search = FindFirstFileW(
			(directory + L"\\" + WrapperPrefix + L"*.bat").c_str(), &data);
		if (search == INVALID_HANDLE_VALUE)
		{
			error = GetLastError();
			if (error == ERROR_FILE_NOT_FOUND)
			{
				error = NO_ERROR;
				return true;
			}
			return false;
		}
		do
		{
			if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
				count++;
		} while (count < MaximumWrapperFilesPerDirectory &&
			FindNextFileW(search, &data));
		const DWORD findError = GetLastError();
		FindClose(search);
		if (count >= MaximumWrapperFilesPerDirectory)
		{
			error = ERROR_TOO_MANY_OPEN_FILES;
			return false;
		}
		error = findError == ERROR_NO_MORE_FILES ? NO_ERROR : findError;
		return error == NO_ERROR;
	}

	std::wstring GenerateWrapperName(DWORD& error)
	{
		std::array<std::uint8_t, 16> random = {};
		if (BCryptGenRandom(nullptr, random.data(), static_cast<ULONG>(random.size()),
			BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
		{
			error = ERROR_GEN_FAILURE;
			return {};
		}
		constexpr wchar_t digits[] = L"0123456789ABCDEF";
		std::wstring name = WrapperPrefix;
		for (const std::uint8_t byte : random)
		{
			name.push_back(digits[byte >> 4]);
			name.push_back(digits[byte & 0x0F]);
		}
		return name + L".bat";
	}

	void ReplaceAll(std::wstring& value, wchar_t character,
		const std::wstring& replacement)
	{
		for (std::size_t position = 0;
			(position = value.find(character, position)) != std::wstring::npos;
			position += replacement.size())
		{
			value.replace(position, 1, replacement);
		}
	}

	std::wstring BuildWrapperContent(const std::wstring& originalPath, bool usePushd)
	{
		const bool requiresDirectTransfer = originalPath.find(L'^') != std::wstring::npos;
		const std::size_t separator = originalPath.find_last_of(L"\\/");
		std::wstring directory = separator == std::wstring::npos
			? std::wstring() : originalPath.substr(0, separator);
		std::wstring fileName = separator == std::wstring::npos
			? originalPath : originalPath.substr(separator + 1);
		ReplaceAll(fileName, L'%', L"%%");
		if (usePushd)
			ReplaceAll(directory, L'%', L"%%");

		const time_t now = time(nullptr);
		wchar_t timeBuffer[100];
		struct tm timeInfo;
		localtime_s(&timeInfo, &now);
		wcsftime(timeBuffer, 100, L"%Y-%m-%d %H:%M:%S", &timeInfo);
		const std::string version(VERSION_STRING);
		const std::wstring wideVersion(version.begin(), version.end());

		std::wstring content =
			L"@echo off\r\n"
			L"REM ========================================================================\r\n"
			L"REM TEMPORARY WRAPPER BATCH FILE\r\n"
			L"REM ========================================================================\r\n"
			L"REM Created by:    xucRun v" + wideVersion + L"\r\n"
			L"REM Created on:    " + std::wstring(timeBuffer) + L"\r\n"
			L"REM\r\n"
			L"REM The xucRun broker protects this file until the launched process exits.\r\n"
			L"REM A remaining file can be removed by a local administrator.\r\n"
			L"REM\r\n"
			L"REM ========================================================================\r\n"
			L"\r\n"
			L"setlocal DisableDelayedExpansion\r\n";
		if (usePushd)
			content += L"pushd \"" + directory + L"\"\r\n";
		if (requiresDirectTransfer)
		{
			content += L"\"" + fileName + L"\" %*\r\n";
			return content;
		}
		content += L"call \"" + fileName + L"\" %*\r\n";
		if (usePushd)
			content += L"popd\r\n";
		content += L"endlocal\r\n";
		content += L"del \"%~f0\"\r\n";
		return content;
	}

	bool ParseNestedBatch(const std::wstring& executablePath,
		const std::wstring& argumentTail, const std::wstring& workingDirectory,
		std::wstring& batchPath, std::wstring& commandPrefix,
		std::wstring& trailingArguments)
	{
		if (!IsCommandShell(executablePath) || argumentTail.empty())
			return false;
		const std::size_t switchPosition = FindCommandSwitch(argumentTail);
		if (switchPosition == std::wstring::npos)
			return false;
		commandPrefix = argumentTail.substr(0, switchPosition + 2);
		std::size_t pathStart = argumentTail.find_first_not_of(
			L" \t", switchPosition + 3);
		if (pathStart == std::wstring::npos)
			return false;
		std::size_t pathEnd = std::wstring::npos;
		std::size_t closingQuoteLength = 0;
		if (argumentTail[pathStart] == L'"')
		{
			pathStart++;
			pathEnd = argumentTail.find(L'"', pathStart);
			closingQuoteLength = pathEnd == std::wstring::npos ? 0 : 1;
		}
		else
		{
			pathEnd = argumentTail.find_first_of(L" \t", pathStart);
		}
		if (pathEnd == std::wstring::npos)
			pathEnd = argumentTail.size();
		batchPath = argumentTail.substr(pathStart, pathEnd - pathStart);
		if (!IsBatchFile(batchPath) || !HasCmdMetacharacter(GetFileName(batchPath)))
			return false;
		const std::size_t trailingStart = argumentTail.find_first_not_of(
			L" \t", pathEnd + closingQuoteLength);
		if (trailingStart != std::wstring::npos)
			trailingArguments = argumentTail.substr(trailingStart);
		if (batchPath.size() < 3 || batchPath[1] != L':')
		{
			batchPath = workingDirectory +
				(workingDirectory.empty() || workingDirectory.back() == L'\\'
					? L"" : L"\\") + batchPath;
		}
		std::array<wchar_t, 32768> fullPath = {};
		const DWORD length = GetFullPathNameW(batchPath.c_str(),
			static_cast<DWORD>(fullPath.size()), fullPath.data(), nullptr);
		if (length == 0 || length >= fullPath.size())
			return false;
		batchPath.assign(fullPath.data(), length);
		return true;
	}

	bool VerifyCreatedFile(HANDLE file, const std::wstring& expectedDirectory,
		const std::wstring& targetSid, std::wstring& finalPath, DWORD& error)
	{
		BY_HANDLE_FILE_INFORMATION information = {};
		if (!GetFileInformationByHandle(file, &information) ||
			(information.dwFileAttributes &
				(FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0 ||
			!xucrun::files::ResolveFinalFilePathFromHandle(file, finalPath))
		{
			error = GetLastError() == NO_ERROR ? ERROR_INVALID_DATA : GetLastError();
			return false;
		}
		const std::size_t separator = finalPath.find_last_of(L"\\/");
		const std::wstring parent = separator == std::wstring::npos
			? std::wstring() : finalPath.substr(0, separator);
		if (!SameDirectoryPath(parent, expectedDirectory))
		{
			error = ERROR_CANT_ACCESS_FILE;
			return false;
		}
		PSID owner = nullptr;
		PACL dacl = nullptr;
		PSECURITY_DESCRIPTOR descriptor = nullptr;
		SECURITY_DESCRIPTOR_CONTROL control = 0;
		DWORD revision = 0;
		error = GetSecurityInfo(file, SE_FILE_OBJECT,
			OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
			&owner, nullptr, &dacl, nullptr, &descriptor);
		const bool protectedDacl = error == ERROR_SUCCESS && descriptor &&
			GetSecurityDescriptorControl(descriptor, &control, &revision) &&
			(control & SE_DACL_PROTECTED) != 0;
		const std::wstring expectedSddl =
			L"O:SYD:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;0x1200a9;;;" + targetSid + L")";
		PSECURITY_DESCRIPTOR expectedDescriptor = nullptr;
		PSID expectedOwner = nullptr;
		PACL expectedDacl = nullptr;
		BOOL ownerDefaulted = FALSE;
		BOOL daclPresent = FALSE;
		BOOL daclDefaulted = FALSE;
		const bool exactSecurity = protectedDacl &&
			ConvertStringSecurityDescriptorToSecurityDescriptorW(
				expectedSddl.c_str(), SDDL_REVISION_1,
				&expectedDescriptor, nullptr) &&
			GetSecurityDescriptorOwner(expectedDescriptor,
				&expectedOwner, &ownerDefaulted) &&
			GetSecurityDescriptorDacl(expectedDescriptor, &daclPresent,
				&expectedDacl, &daclDefaulted) && daclPresent && dacl &&
			EqualSid(owner, expectedOwner) &&
			EqualAclEntries(expectedDacl, dacl);
		if (expectedDescriptor)
			LocalFree(expectedDescriptor);
		if (descriptor)
			LocalFree(descriptor);
		if (!exactSecurity)
		{
			if (error == ERROR_SUCCESS)
				error = ERROR_INVALID_SECURITY_DESCR;
			return false;
		}
		return true;
	}
}

bool BrokerTemporaryBatch::active() const noexcept
{
	return fileHandle && fileHandle != INVALID_HANDLE_VALUE;
}

BrokerTemporaryBatch::~BrokerTemporaryBatch()
{
	ReleaseBrokerTemporaryBatch(*this);
}

BrokerTemporaryBatch::BrokerTemporaryBatch(BrokerTemporaryBatch&& other) noexcept
	: executablePath(std::move(other.executablePath)),
	argumentTail(std::move(other.argumentTail)),
	workingDirectory(std::move(other.workingDirectory)),
	temporaryPath(std::move(other.temporaryPath)),
	fileHandle(other.fileHandle), originalFileHandle(other.originalFileHandle),
	directoryHandles(std::move(other.directoryHandles))
{
	other.fileHandle = INVALID_HANDLE_VALUE;
	other.originalFileHandle = INVALID_HANDLE_VALUE;
	other.temporaryPath.clear();
}

BrokerTemporaryBatch& BrokerTemporaryBatch::operator=(
	BrokerTemporaryBatch&& other) noexcept
{
	if (this == &other)
		return *this;
	ReleaseBrokerTemporaryBatch(*this);
	executablePath = std::move(other.executablePath);
	argumentTail = std::move(other.argumentTail);
	workingDirectory = std::move(other.workingDirectory);
	temporaryPath = std::move(other.temporaryPath);
	fileHandle = other.fileHandle;
	originalFileHandle = other.originalFileHandle;
	directoryHandles = std::move(other.directoryHandles);
	other.fileHandle = INVALID_HANDLE_VALUE;
	other.originalFileHandle = INVALID_HANDLE_VALUE;
	other.temporaryPath.clear();
	return *this;
}

void ReleaseBrokerTemporaryBatch(BrokerTemporaryBatch& prepared) noexcept
{
	if (prepared.fileHandle && prepared.fileHandle != INVALID_HANDLE_VALUE)
		CloseHandle(prepared.fileHandle);
	prepared.fileHandle = INVALID_HANDLE_VALUE;
	if (prepared.originalFileHandle &&
		prepared.originalFileHandle != INVALID_HANDLE_VALUE)
	{
		CloseHandle(prepared.originalFileHandle);
	}
	prepared.originalFileHandle = INVALID_HANDLE_VALUE;
	if (!prepared.temporaryPath.empty())
		DeleteFileW(prepared.temporaryPath.c_str());
	CloseDirectories(prepared.directoryHandles);
	prepared.temporaryPath.clear();
}

bool PrepareBrokerTemporaryBatch(HANDLE targetToken,
	const std::wstring& executablePath, const std::wstring& argumentTail,
	const std::wstring& workingDirectory, BrokerTemporaryBatch& prepared,
	DWORD& error)
{
	ReleaseBrokerTemporaryBatch(prepared);
	prepared.executablePath = executablePath;
	prepared.argumentTail = argumentTail;
	prepared.workingDirectory = workingDirectory;

	const bool directWrapper = HasCmdMetacharacter(GetFileName(executablePath));
	std::wstring originalPath = executablePath;
	std::wstring commandPrefix;
	std::wstring trailingArguments;
	const bool nestedWrapper = !directWrapper && ParseNestedBatch(executablePath,
		argumentTail, workingDirectory, originalPath, commandPrefix, trailingArguments);
	if (!directWrapper && !nestedWrapper)
	{
		error = NO_ERROR;
		return true;
	}
	prepared.originalFileHandle = CreateFileW(originalPath.c_str(),
		GENERIC_READ | SYNCHRONIZE, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
		FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
	if (prepared.originalFileHandle == INVALID_HANDLE_VALUE)
	{
		error = GetLastError();
		return false;
	}
	BY_HANDLE_FILE_INFORMATION originalInformation = {};
	if (!GetFileInformationByHandle(
		prepared.originalFileHandle, &originalInformation))
	{
		error = GetLastError();
		return false;
	}
	if ((originalInformation.dwFileAttributes &
		(FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0)
	{
		error = ERROR_CANT_ACCESS_FILE;
		return false;
	}
	std::wstring finalOriginalPath;
	if (!xucrun::files::ResolveFinalFilePathFromHandle(
		prepared.originalFileHandle, finalOriginalPath) ||
		_wcsicmp(finalOriginalPath.c_str(), originalPath.c_str()) != 0)
	{
		error = GetLastError() == NO_ERROR ? ERROR_CANT_ACCESS_FILE : GetLastError();
		return false;
	}
	originalPath = finalOriginalPath;

	std::wstring targetSid;
	if (!ReadTokenSid(targetToken, targetSid, error))
		return false;
	std::wstring temporaryDirectory = FindRamDisk(targetToken);
	if (temporaryDirectory.empty() &&
		!ReadEnvironmentValue(targetToken, L"TEMP", temporaryDirectory, error))
	{
		return false;
	}
	std::lock_guard<std::mutex> creationLock(wrapperCreationMutex);
	std::wstring finalDirectory;
	if (!LockDirectoryChain(temporaryDirectory, finalDirectory,
		prepared.directoryHandles, error))
	{
		return false;
	}
	std::size_t wrapperCount = 0;
	if (!CountExistingWrappers(finalDirectory, wrapperCount, error))
	{
		ReleaseBrokerTemporaryBatch(prepared);
		return false;
	}

	const std::wstring sddl = L"O:SYD:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;GRGX;;;" +
		targetSid + L")";
	PSECURITY_DESCRIPTOR descriptor = nullptr;
	if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
		sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr))
	{
		error = GetLastError();
		ReleaseBrokerTemporaryBatch(prepared);
		return false;
	}
	SECURITY_ATTRIBUTES security = { sizeof(security), descriptor, FALSE };
	HANDLE writer = INVALID_HANDLE_VALUE;
	std::wstring requestedPath;
	for (int attempt = 0; attempt < 8 && writer == INVALID_HANDLE_VALUE; attempt++)
	{
		const std::wstring name = GenerateWrapperName(error);
		if (name.empty())
			break;
		requestedPath = finalDirectory +
			(finalDirectory.back() == L'\\' ? L"" : L"\\") + name;
		writer = CreateFileW(requestedPath.c_str(), GENERIC_READ | GENERIC_WRITE,
			FILE_SHARE_READ | FILE_SHARE_WRITE, &security, CREATE_NEW,
			FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
		if (writer == INVALID_HANDLE_VALUE)
		{
			error = GetLastError();
			if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS)
				break;
		}
	}
	LocalFree(descriptor);
	if (writer == INVALID_HANDLE_VALUE)
	{
		ReleaseBrokerTemporaryBatch(prepared);
		return false;
	}

	const std::wstring content = BuildWrapperContent(originalPath, directWrapper);
	const int byteCount = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS,
		content.c_str(), static_cast<int>(content.size()), nullptr, 0,
		nullptr, nullptr);
	std::vector<char> bytes(byteCount > 0 ? byteCount : 0);
	BOOL usedDefaultCharacter = FALSE;
	const bool encoded = byteCount > 0 && WideCharToMultiByte(
		CP_ACP, WC_NO_BEST_FIT_CHARS, content.c_str(), static_cast<int>(content.size()),
		bytes.data(), byteCount, nullptr, &usedDefaultCharacter) == byteCount &&
		!usedDefaultCharacter;
	DWORD written = 0;
	const bool stored = encoded && WriteFile(writer, bytes.data(),
		static_cast<DWORD>(bytes.size()), &written, nullptr) &&
		written == bytes.size() && FlushFileBuffers(writer);
	if (!stored)
	{
		error = GetLastError() == NO_ERROR ? ERROR_WRITE_FAULT : GetLastError();
		CloseHandle(writer);
		prepared.temporaryPath = requestedPath;
		ReleaseBrokerTemporaryBatch(prepared);
		return false;
	}

	HANDLE firstGuard = ReOpenFile(writer, GENERIC_READ | SYNCHRONIZE,
		FILE_SHARE_READ | FILE_SHARE_WRITE, 0);
	if (firstGuard == INVALID_HANDLE_VALUE)
	{
		error = GetLastError();
		CloseHandle(writer);
		prepared.temporaryPath = requestedPath;
		ReleaseBrokerTemporaryBatch(prepared);
		return false;
	}
	CloseHandle(writer);
	HANDLE finalGuard = ReOpenFile(firstGuard, GENERIC_READ | SYNCHRONIZE,
		FILE_SHARE_READ, 0);
	if (finalGuard == INVALID_HANDLE_VALUE)
	{
		error = GetLastError();
		CloseHandle(firstGuard);
		prepared.temporaryPath = requestedPath;
		ReleaseBrokerTemporaryBatch(prepared);
		return false;
	}
	CloseHandle(firstGuard);
	prepared.fileHandle = finalGuard;
	if (!VerifyCreatedFile(finalGuard, finalDirectory, targetSid,
		prepared.temporaryPath, error))
	{
		ReleaseBrokerTemporaryBatch(prepared);
		return false;
	}

	if (directWrapper)
	{
		prepared.executablePath = prepared.temporaryPath;
	}
	else
	{
		prepared.argumentTail = commandPrefix + L" \"" +
			prepared.temporaryPath + L"\"";
		if (!trailingArguments.empty())
			prepared.argumentTail += L" " + trailingArguments;
		const std::size_t separator = originalPath.find_last_of(L"\\/");
		if (separator != std::wstring::npos)
			prepared.workingDirectory = originalPath.substr(0, separator);
	}
	error = NO_ERROR;
	return true;
}

bool RetainBrokerTemporaryBatchUntilProcessExit(BrokerTemporaryBatch& prepared,
	HANDLE process, DWORD& error)
{
	if (!prepared.active())
	{
		error = NO_ERROR;
		return true;
	}
	std::size_t count = retainedWrapperCount.load();
	while (count < MaximumRetainedWrappers &&
		!retainedWrapperCount.compare_exchange_weak(count, count + 1))
	{
	}
	if (count >= MaximumRetainedWrappers)
	{
		error = ERROR_TOO_MANY_OPEN_FILES;
		return false;
	}
	auto* retained = new (std::nothrow) RetainedWrapper;
	if (!retained)
	{
		retainedWrapperCount.fetch_sub(1);
		error = ERROR_NOT_ENOUGH_MEMORY;
		return false;
	}
	if (!DuplicateHandle(GetCurrentProcess(), process, GetCurrentProcess(),
		&retained->process, SYNCHRONIZE, FALSE, 0))
	{
		error = GetLastError();
		delete retained;
		retainedWrapperCount.fetch_sub(1);
		return false;
	}
	HANDLE waitThread = CreateThread(
		nullptr, 0, WaitForRetainedWrapper, retained, CREATE_SUSPENDED, nullptr);
	if (!waitThread)
	{
		error = GetLastError();
		CloseHandle(retained->process);
		delete retained;
		retainedWrapperCount.fetch_sub(1);
		return false;
	}
	retained->file = prepared.fileHandle;
	retained->originalFile = prepared.originalFileHandle;
	retained->directories = std::move(prepared.directoryHandles);
	retained->path = std::move(prepared.temporaryPath);
	if (ResumeThread(waitThread) == static_cast<DWORD>(-1))
	{
		error = GetLastError();
		TerminateThread(waitThread, error);
		WaitForSingleObject(waitThread, INFINITE);
		CloseHandle(waitThread);
		prepared.fileHandle = retained->file;
		prepared.originalFileHandle = retained->originalFile;
		prepared.directoryHandles = std::move(retained->directories);
		prepared.temporaryPath = retained->path;
		CloseHandle(retained->process);
		delete retained;
		retainedWrapperCount.fetch_sub(1);
		return false;
	}
	CloseHandle(waitThread);
	prepared.fileHandle = INVALID_HANDLE_VALUE;
	prepared.originalFileHandle = INVALID_HANDLE_VALUE;
	prepared.temporaryPath.clear();
	error = NO_ERROR;
	return true;
}

bool RunBrokerTemporaryBatchSelfTest()
{
	if (!SameDirectoryPath(L"Z:", L"Z:\\") ||
		SameDirectoryPath(L"Z:\\Other", L"Z:\\"))
	{
		return false;
	}
	const std::wstring directPath = L"C:\\Work ^ Here\\test ^ demo.bat";
	const std::wstring directContent = BuildWrapperContent(directPath, true);
	if (!HasCmdMetacharacter(GetFileName(directPath)) ||
		directContent.find(L"pushd \"C:\\Work ^ Here\"") == std::wstring::npos ||
		directContent.ends_with(L"\"test ^ demo.bat\" %*\r\n") == false ||
		directContent.find(L"call ") != std::wstring::npos ||
		directContent.find(L"Original path:") != std::wstring::npos ||
		directContent.find(L"del \"%~f0\"") != std::wstring::npos)
	{
		return false;
	}
	const std::wstring provenPath =
		L"Z:\\2 & & !! (# )% #-\\$ !! !!! &&\\"
		L"test () notepad & && (hierhin ! kopiert!).bat";
	const std::wstring provenContent = BuildWrapperContent(provenPath, true);
	if (provenContent.find(
			L"pushd \"Z:\\2 & & !! (# )%% #-\\$ !! !!! &&\"") ==
			std::wstring::npos ||
		provenContent.find(
			L"call \"test () notepad & && (hierhin ! kopiert!).bat\" %*") ==
			std::wstring::npos ||
		provenContent.find(L"popd\r\nendlocal\r\ndel \"%~f0\"\r\n") ==
			std::wstring::npos)
	{
		return false;
	}
	std::wstring batchPath;
	std::wstring commandPrefix;
	std::wstring trailingArguments;
	if (!ParseNestedBatch(L"C:\\Windows\\System32\\cmd.exe",
		L"/D /Q /C \"test ^ demo.bat\" alpha /K value", L"C:\\Work ^ Here",
		batchPath, commandPrefix, trailingArguments) ||
		_wcsicmp(batchPath.c_str(), directPath.c_str()) != 0 ||
		commandPrefix != L"/D /Q /C" || trailingArguments != L"alpha /K value")
	{
		return false;
	}
	if (!ParseNestedBatch(L"C:\\Windows\\System32\\cmd.exe",
		L"/Q\t/C\t\"test & demo.bat\"", L"C:\\Work",
		batchPath, commandPrefix, trailingArguments) ||
		commandPrefix != L"/Q\t/C")
	{
		return false;
	}

	return !ParseNestedBatch(L"C:\\Windows\\System32\\cmd.exe",
		L"/d /c \"normal.bat\"", L"C:\\Work",
		batchPath, commandPrefix, trailingArguments) &&
		!HasCmdMetacharacter(L"normal.bat");
}
