// Purpose: Declares the isolated round-trip test of the broker-internal credential store.
// Approach: Exposes the declarations, constants, and types consumed by the components that provide this behavior.
// Tests storage, reading, protection binding, and final cleanup.
#pragma once

#include <Windows.h>

#include <cstdint>
#include <string>

bool RunCredentialStoreSelfTest(
	HANDLE clientProcess,
	std::uint64_t requestId,
	DWORD& error,
	std::wstring& failedStage);