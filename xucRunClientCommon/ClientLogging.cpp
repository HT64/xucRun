// Purpose: Implements dated, process-safe client file logging.
// Behavior: Chooses the log path and adds time, level, process ID, and message.
#include "ClientLogging.h"

#include "BrokerPipeClient.h"

#include <Windows.h>
#include <ShlObj.h>

#include <chrono>
#include <algorithm>
#include <fstream>
#include <vector>

namespace
{
	std::wstring logFilePath;
	bool loggingEnabled = false;

	std::wstring GetTimestamp()
	{
		const auto now = std::chrono::system_clock::now();
		const auto time = std::chrono::system_clock::to_time_t(now);
		struct tm localTime = {};
		localtime_s(&localTime, &time);

		wchar_t buffer[64] = {};
		wcsftime(buffer, std::size(buffer), L"%Y-%m-%d %H:%M:%S", &localTime);
		return buffer;
	}

	bool IsClientLogFileName(const std::wstring& name)
	{
		const std::wstring prefix = std::wstring(xucrun::broker::ProductName) + L"_";
		constexpr wchar_t suffix[] = L".log";
		if (name.size() != prefix.size() + 8 + std::size(suffix) - 1 ||
			!name.starts_with(prefix) || !name.ends_with(suffix))
		{
			return false;
		}
		return std::all_of(name.begin() + prefix.size(),
			name.begin() + prefix.size() + 8,
			[](wchar_t character) {
				return character >= L'0' && character <= L'9';
			});
	}
}

namespace xucrun::client_logging
{
	bool Initialize(std::wstring& configuredValue, std::wstring& failedDirectory,
		std::wstring& configurationPath, unsigned long& error)
	{
		loggingEnabled = false;
		logFilePath.clear();
		configuredValue.clear();
		failedDirectory.clear();
		configurationPath.clear();
		error = NO_ERROR;

		wchar_t programFiles[MAX_PATH] = {};
		const DWORD programFilesLength = GetEnvironmentVariableW(
			L"ProgramFiles", programFiles, ARRAYSIZE(programFiles));
		if (programFilesLength == 0 || programFilesLength >= ARRAYSIZE(programFiles))
			return true;

		configurationPath = std::wstring(programFiles) + L"\\xucRun\\xucRun.ini";
		std::vector<wchar_t> configuredDirectory(32768);
		const DWORD configuredLength = GetPrivateProfileStringW(
			L"Logging", L"ClientLogDirectory", L"", configuredDirectory.data(),
			static_cast<DWORD>(configuredDirectory.size()), configurationPath.c_str());
		if (configuredLength == 0)
			return true;
		configuredValue.assign(configuredDirectory.data(), configuredLength);
		if (configuredLength >= configuredDirectory.size() - 1)
		{
			failedDirectory = configuredValue;
			error = ERROR_INSUFFICIENT_BUFFER;
			return false;
		}

		const DWORD expandedLength = ExpandEnvironmentStringsW(
			configuredDirectory.data(), nullptr, 0);
		if (expandedLength == 0)
		{
			failedDirectory = configuredValue;
			error = GetLastError();
			return false;
		}
		std::vector<wchar_t> expandedDirectory(expandedLength);
		if (ExpandEnvironmentStringsW(configuredDirectory.data(),
			expandedDirectory.data(), expandedLength) == 0)
		{
			failedDirectory = configuredValue;
			error = GetLastError();
			return false;
		}
		std::wstring logDirectory(expandedDirectory.data());
		failedDirectory = logDirectory;
		DWORD attributes = GetFileAttributesW(logDirectory.c_str());
		if (attributes == INVALID_FILE_ATTRIBUTES)
		{
			const DWORD attributeError = GetLastError();
			if (attributeError != ERROR_FILE_NOT_FOUND &&
				attributeError != ERROR_PATH_NOT_FOUND)
			{
				error = attributeError;
				return false;
			}

			const int createResult = SHCreateDirectoryExW(nullptr,
				logDirectory.c_str(), nullptr);
			if (createResult != ERROR_SUCCESS && createResult != ERROR_ALREADY_EXISTS &&
				createResult != ERROR_FILE_EXISTS)
			{
				error = static_cast<DWORD>(createResult);
				return false;
			}
			attributes = GetFileAttributesW(logDirectory.c_str());
		}
		if (attributes == INVALID_FILE_ATTRIBUTES ||
			(attributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
		{
			error = attributes == INVALID_FILE_ATTRIBUTES ? GetLastError() : ERROR_DIRECTORY;
			return false;
		}

		const auto now = std::chrono::system_clock::now();
		const auto time = std::chrono::system_clock::to_time_t(now);
		struct tm localTime = {};
		localtime_s(&localTime, &time);
		wchar_t date[32] = {};
		wcsftime(date, std::size(date), L"%Y%m%d", &localTime);

		if (!logDirectory.empty() && logDirectory.back() != L'\\' &&
			logDirectory.back() != L'/')
		{
			logDirectory += L'\\';
		}
		logFilePath = logDirectory + xucrun::broker::ProductName + L"_" + date + L".log";
		HANDLE logFile = CreateFileW(logFilePath.c_str(), FILE_APPEND_DATA,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS,
			FILE_ATTRIBUTE_NORMAL, nullptr);
		if (logFile == INVALID_HANDLE_VALUE)
		{
			error = GetLastError();
			logFilePath.clear();
			return false;
		}
		CloseHandle(logFile);
		loggingEnabled = true;
		return true;
	}

	bool TryGetExistingLogFilePath(std::wstring& path)
	{
		path.clear();
		if (!loggingEnabled || logFilePath.empty())
			return false;
		const DWORD attributes = GetFileAttributesW(logFilePath.c_str());
		if (attributes == INVALID_FILE_ATTRIBUTES ||
			(attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
		{
			return false;
		}
		path = logFilePath;
		return true;
	}

	bool GetExistingLogFiles(std::wstring& directory,
		std::vector<std::wstring>& paths, unsigned long& error)
	{
		directory.clear();
		paths.clear();
		error = ERROR_SUCCESS;
		if (!loggingEnabled || logFilePath.empty())
			return true;
		const size_t separator = logFilePath.find_last_of(L"\\/");
		if (separator == std::wstring::npos)
		{
			error = ERROR_INVALID_DATA;
			return false;
		}
		directory = logFilePath.substr(0, separator);
		const std::wstring searchPattern = directory + L"\\" +
			broker::ProductName + L"_????????.log";
		WIN32_FIND_DATAW entry = {};
		HANDLE search = FindFirstFileW(searchPattern.c_str(), &entry);
		if (search == INVALID_HANDLE_VALUE)
		{
			error = GetLastError();
			if (error == ERROR_FILE_NOT_FOUND)
			{
				error = ERROR_SUCCESS;
				return true;
			}
			return false;
		}
		do
		{
			if ((entry.dwFileAttributes &
					(FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) == 0 &&
				IsClientLogFileName(entry.cFileName))
			{
				paths.push_back(directory + L"\\" + entry.cFileName);
			}
		} while (FindNextFileW(search, &entry));
		error = GetLastError();
		FindClose(search);
		if (error != ERROR_NO_MORE_FILES)
			return false;
		error = ERROR_SUCCESS;
		std::sort(paths.begin(), paths.end());
		return true;
	}

	void Write(const std::wstring& level, const std::wstring& message)
	{
		if (!loggingEnabled || logFilePath.empty())
			return;

		try
		{
			std::wofstream logFile(logFilePath, std::ios::app);
			if (logFile.is_open())
				logFile << GetTimestamp() << L" [" << level << L"] [PID " <<
					GetCurrentProcessId() << L"] " << message << std::endl;
		}
		catch (...)
		{
		}
	}
}