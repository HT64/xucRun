// Purpose: Declares handle-based resolution and hashing for file identities.
// Provides final paths and SHA-256 values without trusting the input path.
#pragma once

#include <Windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace xucrun::files
{
	bool ResolveExecutablePath(const std::wstring& input, std::wstring& resolvedPath);
	bool ResolveFinalFilePath(const std::wstring& input, std::wstring& resolvedPath);
	bool ResolveFinalFilePathFromHandle(HANDLE file, std::wstring& resolvedPath);
	bool ComputeFileSHA256(const std::wstring& path, std::vector<std::uint8_t>& hash);
	bool ComputeFileSHA256FromHandle(HANDLE file, std::vector<std::uint8_t>& hash);
}