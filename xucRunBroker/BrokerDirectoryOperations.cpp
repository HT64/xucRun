// Purpose: Implements broker-owned directory validation, creation, traversal, and deletion helpers.
// Approach: Implements the behavior with focused helpers built on shared xucRun interfaces and Windows APIs.
#include "BrokerDirectoryOperations.h"

#include <winternl.h>

#include <array>
#include <cstddef>
#include <cstring>
#include <limits>
#include <memory>
#include <string_view>

namespace
{
	using NtCreateFileFunction = NTSTATUS(NTAPI*)(PHANDLE, ACCESS_MASK,
		POBJECT_ATTRIBUTES, PIO_STATUS_BLOCK, PLARGE_INTEGER, ULONG, ULONG,
		ULONG, ULONG, PVOID, ULONG);
	using NtSetInformationFileFunction = NTSTATUS(NTAPI*)(HANDLE,
		PIO_STATUS_BLOCK, PVOID, ULONG, FILE_INFORMATION_CLASS);
	using RtlNtStatusToDosErrorFunction = ULONG(WINAPI*)(NTSTATUS);
	// FileRenameInformation is native class 10; the public SDK omits its name in some versions.
	constexpr FILE_INFORMATION_CLASS NativeFileRenameInformationClass =
		static_cast<FILE_INFORMATION_CLASS>(10);

	struct NativeFileRenameInformation
	{
		BOOLEAN replaceIfExists;
		HANDLE rootDirectory;
		ULONG fileNameLength;
		WCHAR fileName[1];
	};

	struct NativeFunctions
	{
		NtCreateFileFunction createFile = nullptr;
		NtSetInformationFileFunction setInformationFile = nullptr;
		RtlNtStatusToDosErrorFunction toDosError = nullptr;
	};

	const NativeFunctions& GetNativeFunctions()
	{
		static const NativeFunctions functions = []
		{
			NativeFunctions result;
			const HMODULE module = GetModuleHandleW(L"ntdll.dll");
			if (module)
			{
				result.createFile = reinterpret_cast<NtCreateFileFunction>(
					GetProcAddress(module, "NtCreateFile"));
				result.setInformationFile = reinterpret_cast<NtSetInformationFileFunction>(
					GetProcAddress(module, "NtSetInformationFile"));
				result.toDosError = reinterpret_cast<RtlNtStatusToDosErrorFunction>(
					GetProcAddress(module, "RtlNtStatusToDosError"));
			}
			return result;
		}();
		return functions;
	}

	bool IsValidChildName(std::wstring_view name)
	{
		if (name.empty() || name == L"." || name == L".." ||
			name.size() > (std::numeric_limits<USHORT>::max)() / sizeof(wchar_t))
		{
			return false;
		}
		return name.find_first_of(L"\\/:") == std::wstring_view::npos &&
			name.find(L'\0') == std::wstring_view::npos;
	}
}

namespace xucrun::broker_directory
{
	bool OpenFile(HANDLE directory, const std::wstring& name,
		DWORD desiredAccess, DWORD shareAccess, OpenMode mode,
		HANDLE& file, DWORD& error)
	{
		file = INVALID_HANDLE_VALUE;
		if (!directory || directory == INVALID_HANDLE_VALUE || !IsValidChildName(name))
		{
			error = ERROR_INVALID_NAME;
			return false;
		}
		const NativeFunctions& functions = GetNativeFunctions();
		if (!functions.createFile || !functions.toDosError)
		{
			error = ERROR_PROC_NOT_FOUND;
			return false;
		}

		UNICODE_STRING nativeName = {};
		nativeName.Length = static_cast<USHORT>(name.size() * sizeof(wchar_t));
		nativeName.MaximumLength = nativeName.Length;
		nativeName.Buffer = const_cast<PWSTR>(name.data());
		OBJECT_ATTRIBUTES attributes = {};
		InitializeObjectAttributes(&attributes, &nativeName,
			OBJ_CASE_INSENSITIVE | OBJ_DONT_REPARSE, directory, nullptr);
		IO_STATUS_BLOCK statusBlock = {};
		GENERIC_MAPPING mapping = {
			FILE_GENERIC_READ,
			FILE_GENERIC_WRITE,
			FILE_GENERIC_EXECUTE,
			FILE_ALL_ACCESS
		};
		ACCESS_MASK nativeAccess = desiredAccess;
		MapGenericMask(&nativeAccess, &mapping);
		const ULONG disposition = mode == OpenMode::CreateNew ? FILE_CREATE : FILE_OPEN;
		ULONG createOptions = FILE_NON_DIRECTORY_FILE | FILE_OPEN_REPARSE_POINT |
			FILE_SYNCHRONOUS_IO_NONALERT;
		if (mode == OpenMode::CreateNew)
			createOptions |= FILE_WRITE_THROUGH;
		// The reparse-point validation below also needs attributes on write-only handles.
		const NTSTATUS status = functions.createFile(&file,
			nativeAccess | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
			&attributes, &statusBlock, nullptr,
			FILE_ATTRIBUTE_NORMAL, shareAccess, disposition,
			createOptions, nullptr, 0);
		if (status < 0)
		{
			file = INVALID_HANDLE_VALUE;
			error = functions.toDosError(status);
			return false;
		}

		FILE_ATTRIBUTE_TAG_INFO information = {};
		if (!GetFileInformationByHandleEx(
			file, FileAttributeTagInfo, &information, sizeof(information)))
		{
			error = GetLastError();
			CloseHandle(file);
			file = INVALID_HANDLE_VALUE;
			return false;
		}
		if ((information.FileAttributes &
			(FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0)
		{
			error = ERROR_CANT_ACCESS_FILE;
			CloseHandle(file);
			file = INVALID_HANDLE_VALUE;
			return false;
		}
		error = NO_ERROR;
		return true;
	}

	bool RenameFile(HANDLE file, HANDLE directory,
		const std::wstring& newName, DWORD& error)
	{
		if (!file || file == INVALID_HANDLE_VALUE || !directory ||
			directory == INVALID_HANDLE_VALUE || !IsValidChildName(newName))
		{
			error = ERROR_INVALID_NAME;
			return false;
		}
		const NativeFunctions& functions = GetNativeFunctions();
		if (!functions.setInformationFile || !functions.toDosError)
		{
			error = ERROR_PROC_NOT_FOUND;
			return false;
		}
		const DWORD nameSize = static_cast<DWORD>(newName.size() * sizeof(wchar_t));
		const std::size_t informationSize =
			offsetof(NativeFileRenameInformation, fileName) + nameSize;
		auto storage = std::make_unique_for_overwrite<std::byte[]>(informationSize);
		std::memset(storage.get(), 0, informationSize);
		auto* information = reinterpret_cast<NativeFileRenameInformation*>(storage.get());
		information->replaceIfExists = FALSE;
		information->rootDirectory = directory;
		information->fileNameLength = nameSize;
		std::memcpy(information->fileName, newName.data(), nameSize);
		IO_STATUS_BLOCK statusBlock = {};
		const NTSTATUS status = functions.setInformationFile(file, &statusBlock,
			information, static_cast<ULONG>(informationSize),
			NativeFileRenameInformationClass);
		if (status < 0)
		{
			error = functions.toDosError(status);
			return false;
		}
		error = NO_ERROR;
		return true;
	}

	bool DeleteFile(HANDLE directory, const std::wstring& name,
		bool missingIsSuccess, DWORD& error)
	{
		HANDLE file = INVALID_HANDLE_VALUE;
		if (!OpenFile(directory, name, DELETE | FILE_READ_ATTRIBUTES,
			FILE_SHARE_READ | FILE_SHARE_WRITE, OpenMode::OpenExisting, file, error))
		{
			if (missingIsSuccess &&
				(error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND))
			{
				error = NO_ERROR;
				return true;
			}
			return false;
		}
		const bool success = MarkFileForDeletion(file, error);
		CloseHandle(file);
		return success;
	}

	bool MarkFileForDeletion(HANDLE file, DWORD& error)
	{
		if (!file || file == INVALID_HANDLE_VALUE)
		{
			error = ERROR_INVALID_HANDLE;
			return false;
		}
		FILE_DISPOSITION_INFO disposition = { TRUE };
		if (!SetFileInformationByHandle(
			file, FileDispositionInfo, &disposition, sizeof(disposition)))
		{
			error = GetLastError();
			return false;
		}
		error = NO_ERROR;
		return true;
	}

	bool Flush(HANDLE directory, DWORD& error)
	{
		if (!directory || directory == INVALID_HANDLE_VALUE)
		{
			error = ERROR_INVALID_HANDLE;
			return false;
		}
		if (!FlushFileBuffers(directory))
		{
			error = GetLastError();
			return false;
		}
		error = NO_ERROR;
		return true;
	}

	bool Enumerate(HANDLE directory, std::vector<Entry>& entries, DWORD& error)
	{
		entries.clear();
		if (!directory || directory == INVALID_HANDLE_VALUE)
		{
			error = ERROR_INVALID_HANDLE;
			return false;
		}
		alignas(FILE_ID_BOTH_DIR_INFO) std::array<std::byte, 64 * 1024> buffer = {};
		bool restart = true;
		for (;;)
		{
			const FILE_INFO_BY_HANDLE_CLASS informationClass = restart
				? FileIdBothDirectoryRestartInfo : FileIdBothDirectoryInfo;
			if (!GetFileInformationByHandleEx(directory, informationClass,
				buffer.data(), static_cast<DWORD>(buffer.size())))
			{
				error = GetLastError();
				if (error == ERROR_NO_MORE_FILES)
				{
					error = NO_ERROR;
					return true;
				}
				entries.clear();
				return false;
			}
			restart = false;
			std::size_t offset = 0;
			for (;;)
			{
				const auto* information = reinterpret_cast<const FILE_ID_BOTH_DIR_INFO*>(
					buffer.data() + offset);
				constexpr std::size_t headerSize = offsetof(FILE_ID_BOTH_DIR_INFO, FileName);
				if (offset + headerSize > buffer.size() ||
					information->FileNameLength % sizeof(wchar_t) != 0 ||
					information->FileNameLength > buffer.size() - offset - headerSize)
				{
					error = ERROR_INVALID_DATA;
					entries.clear();
					return false;
				}
				entries.push_back({ std::wstring(information->FileName,
					information->FileNameLength / sizeof(wchar_t)),
					information->FileAttributes });
				if (information->NextEntryOffset == 0)
					break;
				if (information->NextEntryOffset < headerSize ||
					information->NextEntryOffset > buffer.size() - offset)
				{
					error = ERROR_INVALID_DATA;
					entries.clear();
					return false;
				}
				offset += information->NextEntryOffset;
			}
		}
	}
}