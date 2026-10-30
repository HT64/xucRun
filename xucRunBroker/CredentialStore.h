// Purpose: Declares the LocalSystem store for client- and account-bound passwords.
// Approach: Exposes the declarations, constants, and types consumed by the components that provide this behavior.
// Provides secure storage, short-term reading, listing, and deletion.
#pragma once

#include <Windows.h>

#include <cstdint>
#include <mutex>
#include <span>
#include <string>
#include <vector>

namespace xucrun::broker_store
{
	std::recursive_mutex& TransactionMutex();
	struct AccessContextInspection
	{
		bool processIsLocalSystem = false;
		bool noThreadImpersonation = false;
		bool accessPermitted = false;
	};

	bool InspectAccessContext(AccessContextInspection& inspection, DWORD& error);
	bool Exists(const std::wstring& target, bool& exists, DWORD& error);
	bool Write(const std::wstring& target, std::span<const std::uint8_t> secret,
		DWORD& error);
	bool Read(const std::wstring& target, std::vector<std::uint8_t>& secret,
		DWORD& error);
	bool ReadProtected(const std::wstring& target,
		std::vector<std::uint8_t>& protectedSecret, DWORD& error);
	bool UnprotectForValidation(std::span<const std::uint8_t> protectedSecret,
		std::vector<std::uint8_t>& secret, DWORD& error);
	bool WriteProtected(const std::wstring& target,
		std::span<const std::uint8_t> protectedSecret, DWORD& error);
	bool ReadLastWritten(const std::wstring& target, std::uint64_t& lastWritten,
		DWORD& error);
	bool Delete(const std::wstring& target, DWORD& error);
	bool EnumerateTargets(const std::wstring& prefix,
		std::vector<std::wstring>& targets, DWORD& error);
	bool DeleteAllWithPrefix(const std::wstring& prefix, DWORD& error);
	bool VerifyClientCannotDecrypt(HANDLE clientProcess, const std::wstring& target,
		DWORD& error);
}