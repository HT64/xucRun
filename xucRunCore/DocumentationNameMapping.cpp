// Purpose: Implements the optional display-only name mapping used for documentation screenshots.
// Behavior: Activates only when a strictly valid UTF-8 sidecar file exists beside the executable.
#include "DocumentationNameMapping.h"

#include "FileIdentity.h"

#include <Windows.h>
#include <shlobj.h>
#include <wincred.h>

#include <algorithm>
#include <cstddef>
#include <string_view>
#include <vector>

namespace
{
	constexpr wchar_t ConfigurationFileName[] = L"xucRun.documentation-names.ini";
	constexpr DWORD MaximumConfigurationSize = 64 * 1024;
	constexpr wchar_t UnmappedAccount[] = L"[unmapped account]";
	constexpr wchar_t UnmappedComputer[] = L"[unmapped computer]";

	xucrun::documentation_names::NameMapping runtimeMapping;
	bool runtimeInitialized = false;
	bool runtimeInitializationSucceeded = false;
	std::wstring runtimeInitializationError;

	std::wstring Trim(std::wstring_view value)
	{
		const auto whitespace = [](wchar_t character) {
			return character == L' ' || character == L'\t' ||
				character == L'\r' || character == L'\n';
		};
		while (!value.empty() && whitespace(value.front()))
			value.remove_prefix(1);
		while (!value.empty() && whitespace(value.back()))
			value.remove_suffix(1);
		return std::wstring(value);
	}

	bool ContainsControlCharacter(const std::wstring& value)
	{
		return std::any_of(value.begin(), value.end(), [](wchar_t character) {
			return character < L' ' || character == 0x7f;
		});
	}

	bool ReadUtf8File(HANDLE file, std::wstring& contents, std::wstring& error)
	{
		LARGE_INTEGER size = {};
		if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 ||
			size.QuadPart > MaximumConfigurationSize)
		{
			const DWORD sizeError = GetLastError();
			error = size.QuadPart > MaximumConfigurationSize
				? L"The documentation name mapping file exceeds 64 KiB."
				: size.QuadPart == 0
					? L"The documentation name mapping file is empty."
					: L"Cannot determine the documentation name mapping file size. "
						L"Windows error: " + std::to_wstring(sizeError);
			return false;
		}

		std::string bytes(static_cast<std::size_t>(size.QuadPart), '\0');
		DWORD bytesRead = 0;
		const bool read = ReadFile(file, bytes.data(),
			static_cast<DWORD>(bytes.size()), &bytesRead, nullptr) != FALSE;
		const DWORD readError = read ? ERROR_SUCCESS : GetLastError();
		if (!read || bytesRead != bytes.size())
		{
			error = L"Cannot read the documentation name mapping file. Windows error: " +
				std::to_wstring(readError == ERROR_SUCCESS
					? ERROR_READ_FAULT : readError);
			return false;
		}

		std::size_t offset = 0;
		if (bytes.size() >= 3 &&
			static_cast<unsigned char>(bytes[0]) == 0xef &&
			static_cast<unsigned char>(bytes[1]) == 0xbb &&
			static_cast<unsigned char>(bytes[2]) == 0xbf)
		{
			offset = 3;
		}
		if (bytes.size() - offset >= 2 &&
			((static_cast<unsigned char>(bytes[offset]) == 0xff &&
				static_cast<unsigned char>(bytes[offset + 1]) == 0xfe) ||
				(static_cast<unsigned char>(bytes[offset]) == 0xfe &&
					static_cast<unsigned char>(bytes[offset + 1]) == 0xff)))
		{
			error = L"The documentation name mapping file must be saved as UTF-8, "
				L"not UTF-16.";
			return false;
		}

		const int characterCount = MultiByteToWideChar(CP_UTF8,
			MB_ERR_INVALID_CHARS, bytes.data() + offset,
			static_cast<int>(bytes.size() - offset), nullptr, 0);
		if (characterCount <= 0)
		{
			error = L"The documentation name mapping file is not valid UTF-8.";
			return false;
		}
		contents.resize(static_cast<std::size_t>(characterCount));
		if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
			bytes.data() + offset, static_cast<int>(bytes.size() - offset),
			contents.data(), characterCount) != characterCount)
		{
			error = L"Converting the documentation name mapping file from UTF-8 failed.";
			return false;
		}
		return true;
	}

	std::wstring GetConfigurationPath(std::wstring& error)
	{
		std::vector<wchar_t> executablePath(512);
		for (;;)
		{
			const DWORD length = GetModuleFileNameW(nullptr, executablePath.data(),
				static_cast<DWORD>(executablePath.size()));
			if (length == 0)
			{
				error = L"Cannot locate the executable for the documentation name "
					L"mapping file. Windows error: " + std::to_wstring(GetLastError());
				return {};
			}

			if (length < executablePath.size() - 1)
			{
				std::wstring path(executablePath.data(), length);
				const std::size_t separator = path.find_last_of(L"\\/");
				if (separator == std::wstring::npos)
				break;
				return path.substr(0, separator + 1) + ConfigurationFileName;
			}
			if (executablePath.size() >= 32768)
				break;
			executablePath.resize((std::min)(executablePath.size() * 2,
				static_cast<std::size_t>(32768)));
		}
		error = L"The executable path is too long for the documentation name mapping file.";
		return {};
	}

	bool IsSupportedConfigurationLocation(
		const std::wstring& configurationPath, std::wstring& error)
	{
		PWSTR programFilesPath = nullptr;
		const HRESULT result = SHGetKnownFolderPath(
			FOLDERID_ProgramFiles, KF_FLAG_DEFAULT, nullptr, &programFilesPath);
		if (FAILED(result) || !programFilesPath)
		{
			if (programFilesPath)
				CoTaskMemFree(programFilesPath);
			error = L"Cannot resolve the trusted Program Files directory for the "
				L"documentation name mapping file.";
			return false;
		}
		const std::wstring expectedDirectory =
			std::wstring(programFilesPath) + L"\\xucRun";
		CoTaskMemFree(programFilesPath);
		const std::size_t separator = configurationPath.find_last_of(L"\\/");
		const std::wstring actualDirectory = separator == std::wstring::npos
			? L"" : configurationPath.substr(0, separator);
		if (_wcsicmp(actualDirectory.c_str(), expectedDirectory.c_str()) != 0)
		{
			error = L"The documentation name mapping file is accepted only in the "
				L"installed xucRun directory: " + expectedDirectory;
			return false;
		}
		return true;
	}
}

namespace xucrun::documentation_names
{
	bool CaseInsensitiveLess::operator()(
		const std::wstring& left, const std::wstring& right) const
	{
		return CompareStringOrdinal(left.c_str(), static_cast<int>(left.size()),
			right.c_str(), static_cast<int>(right.size()), TRUE) == CSTR_LESS_THAN;
	}

	bool NameMapping::LoadFromFile(const std::wstring& path, std::wstring& error)
	{
		computers_.clear();
		accounts_.clear();
		operationalAccounts_.clear();
		active_ = false;
		HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
			nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
		if (file == INVALID_HANDLE_VALUE)
		{
			error = L"Cannot open the documentation name mapping file. Windows error: " +
				std::to_wstring(GetLastError());
			return false;
		}
		std::wstring contents;
		const bool read = ReadUtf8File(file, contents, error);
		CloseHandle(file);
		return read && LoadFromContents(contents, error);
	}

	bool NameMapping::LoadFromTrustedFile(
		const std::wstring& path, std::wstring& error)
	{
		computers_.clear();
		accounts_.clear();
		operationalAccounts_.clear();
		active_ = false;
		HANDLE file = CreateFileW(path.c_str(), GENERIC_READ | READ_CONTROL,
			FILE_SHARE_READ, nullptr, OPEN_EXISTING,
			FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
		if (file == INVALID_HANDLE_VALUE)
		{
			error = L"Cannot securely open the documentation name mapping file. "
				L"Windows error: " + std::to_wstring(GetLastError());
			return false;
		}

		constexpr DWORD UnexpectedWriteMask = FILE_WRITE_DATA | FILE_APPEND_DATA |
			FILE_WRITE_EA | FILE_WRITE_ATTRIBUTES | DELETE | WRITE_DAC | WRITE_OWNER;
		DWORD protectionError = NO_ERROR;
		FILE_STANDARD_INFO standardInfo = {};
		std::wstring finalPath;
		const bool trusted =
			GetFileType(file) == FILE_TYPE_DISK &&
			GetFileInformationByHandleEx(
				file, FileStandardInfo, &standardInfo, sizeof(standardInfo)) &&
			!standardInfo.Directory &&
			xucrun::files::IsFileSystemObjectProtectedForPrivilegedWriters(
				file, UnexpectedWriteMask, protectionError) &&
			xucrun::files::ResolveFinalFilePathFromHandle(file, finalPath) &&
			_wcsicmp(finalPath.c_str(), path.c_str()) == 0;
		if (!trusted)
		{
			if (protectionError == NO_ERROR)
				protectionError = ERROR_ACCESS_DENIED;
			CloseHandle(file);
			error = L"The documentation name mapping file must be a regular, "
				L"non-reparse file at the installed path and writable only by "
				L"privileged Windows principals. Windows error: " +
				std::to_wstring(protectionError);
			return false;
		}

		std::wstring contents;
		const bool read = ReadUtf8File(file, contents, error);
		CloseHandle(file);
		return read && LoadFromContents(contents, error);
	}

	bool NameMapping::LoadFromContents(
		const std::wstring& contents, std::wstring& error)
	{
		error.clear();
		computers_.clear();
		accounts_.clear();
		operationalAccounts_.clear();
		active_ = false;

		enum class Section
		{
			None,
			Computers,
			Accounts
		};
		Section section = Section::None;
		std::size_t lineNumber = 0;
		std::size_t position = 0;
		while (position <= contents.size())
		{
			const std::size_t end = contents.find(L'\n', position);
			std::wstring line = Trim(std::wstring_view(contents).substr(
				position, end == std::wstring::npos
					? std::wstring::npos : end - position));
			lineNumber++;
			if (!line.empty() && line.front() != L';' && line.front() != L'#')
			{
				if (line.front() == L'[' && line.back() == L']')
				{
					const std::wstring name = Trim(
						std::wstring_view(line).substr(1, line.size() - 2));
					if (_wcsicmp(name.c_str(), L"Computers") == 0)
						section = Section::Computers;
					else if (_wcsicmp(name.c_str(), L"Accounts") == 0)
						section = Section::Accounts;
					else
					{
						error = L"Unknown section in the documentation name mapping "
							L"file at line " + std::to_wstring(lineNumber) + L".";
						return false;
					}
				}
				else
				{
					const std::size_t equals = line.find(L'=');
					if (section == Section::None || equals == std::wstring::npos)
					{
						error = L"Invalid entry in the documentation name mapping "
							L"file at line " + std::to_wstring(lineNumber) + L".";
						return false;
					}
					const std::wstring operationalName = Trim(
						std::wstring_view(line).substr(0, equals));
					const std::wstring displayName = Trim(
						std::wstring_view(line).substr(equals + 1));
					if (operationalName.empty() || displayName.empty() ||
						operationalName.size() > CREDUI_MAX_USERNAME_LENGTH ||
						displayName.size() > CREDUI_MAX_USERNAME_LENGTH ||
						ContainsControlCharacter(operationalName) ||
						ContainsControlCharacter(displayName))
					{
						error = L"Empty or invalid name in the documentation name "
							L"mapping file at line " + std::to_wstring(lineNumber) + L".";
						return false;
					}

					Mapping& mapping = section == Section::Computers
						? computers_ : accounts_;
					if (!mapping.emplace(operationalName, displayName).second)
					{
						error = L"Duplicate source name in the documentation name "
							L"mapping file at line " + std::to_wstring(lineNumber) + L".";
						return false;
					}
					if (section == Section::Accounts &&
						!operationalAccounts_.emplace(
							displayName, operationalName).second)
					{
						error = L"Duplicate displayed account name in the documentation "
							L"name mapping file at line " +
							std::to_wstring(lineNumber) + L".";
						return false;
					}
				}
			}
			if (end == std::wstring::npos)
				break;
			position = end + 1;
		}

		if (computers_.empty() || accounts_.empty())
		{
			error = L"The documentation name mapping file must contain at least one "
				L"entry in both [Computers] and [Accounts].";
			return false;
		}
		active_ = true;
		return true;
	}

	bool NameMapping::IsActive() const
	{
		return active_;
	}

	std::wstring NameMapping::DisplayAccount(const std::wstring& accountName) const
	{
		if (!active_)
			return accountName;
		const auto mapped = accounts_.find(accountName);
		return mapped == accounts_.end() ? UnmappedAccount : mapped->second;
	}

	std::wstring NameMapping::DisplayComputer(const std::wstring& computerName) const
	{
		if (!active_)
			return computerName;
		const auto mapped = computers_.find(computerName);
		return mapped == computers_.end() ? UnmappedComputer : mapped->second;
	}

	std::wstring NameMapping::OperationalAccount(
		const std::wstring& displayedAccountName) const
	{
		const auto mapped = operationalAccounts_.find(displayedAccountName);
		return mapped == operationalAccounts_.end()
			? displayedAccountName : mapped->second;
	}

	bool Initialize(std::wstring& error)
	{
		if (runtimeInitialized)
		{
			error = runtimeInitializationError;
			return runtimeInitializationSucceeded;
		}
		runtimeInitialized = true;

		std::wstring pathError;
		const std::wstring path = GetConfigurationPath(pathError);
		if (path.empty())
		{
			runtimeInitializationError = pathError;
			error = runtimeInitializationError;
			return false;
		}
		const DWORD attributes = GetFileAttributesW(path.c_str());
		if (attributes == INVALID_FILE_ATTRIBUTES &&
			GetLastError() == ERROR_FILE_NOT_FOUND)
		{
			runtimeInitializationSucceeded = true;
			error.clear();
			return true;
		}
		if (attributes == INVALID_FILE_ATTRIBUTES ||
			(attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
		{
			runtimeInitializationError =
				L"Cannot access the documentation name mapping file: " + path;
			error = runtimeInitializationError;
			return false;
		}
		if (!IsSupportedConfigurationLocation(path, runtimeInitializationError))
		{
			error = runtimeInitializationError;
			return false;
		}
		if (!runtimeMapping.LoadFromTrustedFile(path, runtimeInitializationError))
		{
			runtimeInitializationError += L"\n\nFile: " + path;
			error = runtimeInitializationError;
			return false;
		}
		runtimeInitializationSucceeded = true;
		error.clear();
		return true;
	}

	bool IsActive()
	{
		return runtimeMapping.IsActive();
	}

	std::wstring DisplayAccount(const std::wstring& accountName)
	{
		return runtimeMapping.DisplayAccount(accountName);
	}

	std::wstring DisplayComputer(const std::wstring& computerName)
	{
		return runtimeMapping.DisplayComputer(computerName);
	}

	std::wstring OperationalAccount(const std::wstring& displayedAccountName)
	{
		return runtimeMapping.OperationalAccount(displayedAccountName);
	}
}
