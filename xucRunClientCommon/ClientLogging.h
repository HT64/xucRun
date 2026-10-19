// Purpose: Defines initialization and writing of the shared client log.
// Behavior: Hides log path, enabled state, and output format behind a compact API.
#pragma once

#include <string>
#include <vector>

namespace xucrun::client_logging
{
	bool Initialize(std::wstring& configuredValue, std::wstring& failedDirectory,
		std::wstring& configurationPath, unsigned long& error);
	bool TryGetExistingLogFilePath(std::wstring& path);
	bool GetExistingLogFiles(std::wstring& directory,
		std::vector<std::wstring>& paths, unsigned long& error);
	void Write(const std::wstring& level, const std::wstring& message);
}