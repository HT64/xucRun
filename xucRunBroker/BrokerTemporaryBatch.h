// Purpose: Declares lifecycle management for broker-owned temporary batch wrappers.
// Approach: Exposes the declarations, constants, and types consumed by the components that provide this behavior.
#pragma once

#include <Windows.h>

#include <string>
#include <vector>

struct BrokerTemporaryBatch
{
	BrokerTemporaryBatch() = default;
	~BrokerTemporaryBatch();
	BrokerTemporaryBatch(const BrokerTemporaryBatch&) = delete;
	BrokerTemporaryBatch& operator=(const BrokerTemporaryBatch&) = delete;
	BrokerTemporaryBatch(BrokerTemporaryBatch&& other) noexcept;
	BrokerTemporaryBatch& operator=(BrokerTemporaryBatch&& other) noexcept;

	std::wstring executablePath;
	std::wstring argumentTail;
	std::wstring workingDirectory;
	std::wstring temporaryPath;
	HANDLE fileHandle = INVALID_HANDLE_VALUE;
	HANDLE originalFileHandle = INVALID_HANDLE_VALUE;
	std::vector<HANDLE> directoryHandles;

	bool active() const noexcept;
};

bool PrepareBrokerTemporaryBatch(
	HANDLE targetToken,
	const std::wstring& executablePath,
	const std::wstring& argumentTail,
	const std::wstring& workingDirectory,
	BrokerTemporaryBatch& prepared,
	DWORD& error);

bool RetainBrokerTemporaryBatchUntilProcessExit(
	BrokerTemporaryBatch& prepared,
	HANDLE process,
	DWORD& error);

void ReleaseBrokerTemporaryBatch(BrokerTemporaryBatch& prepared) noexcept;

bool RunBrokerTemporaryBatchSelfTest();
