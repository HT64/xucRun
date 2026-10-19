// Purpose: Declares the isolated round-trip test of the broker-internal credential store.
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