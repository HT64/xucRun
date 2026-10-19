// Purpose: Defines the handle-based verified identity of a requested launch file.
// Declares final path, file, hash, and binary type resolution.
#pragma once

#include <Windows.h>

#include <cstdint>
#include <string>
#include <vector>

struct TargetImageIdentity
{
	std::wstring path;
	std::vector<std::uint8_t> sha256;
	DWORD binaryType = 0;
};

bool ReadTargetImageIdentity(
	const std::wstring& requestedPath,
	TargetImageIdentity& identity,
	DWORD& windowsError);