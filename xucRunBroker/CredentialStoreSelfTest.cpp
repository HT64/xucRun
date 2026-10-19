// Purpose: Tests the credential store with temporary, uniquely named test entries.
// Validates round trip and protection boundaries and removes generated data afterward.
#include "CredentialStoreSelfTest.h"
#include "CredentialStore.h"

#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <vector>

namespace
{
	// Generates an isolated credential target name for the self-test from the request ID.
	std::wstring BuildTargetName(std::uint64_t requestId)
	{
		constexpr wchar_t Hex[] = L"0123456789ABCDEF";
		std::wstring target = L"xucRunBrokerCredentialStoreTest-";
		for (int shift = 60; shift >= 0; shift -= 4)
			target.push_back(Hex[(requestId >> shift) & 0xF]);
		return target;
	}

	// Compares a retrieved test secret in full with the expected value.
	bool Matches(const std::vector<std::uint8_t>& actual,
		const std::array<std::uint8_t, 32>& expected, DWORD& error)
	{
		const bool matches = actual.size() == expected.size() &&
			std::equal(expected.begin(), expected.end(), actual.begin());
		if (!matches)
			error = ERROR_INVALID_DATA;
		return matches;
	}
}

// Tests writing, isolation, overwriting, empty values, and deletion while always clearing test secrets.
bool RunCredentialStoreSelfTest(
	HANDLE clientProcess,
	std::uint64_t requestId,
	DWORD& error,
	std::wstring& failedStage)
{
	error = NO_ERROR;
	const std::wstring target = BuildTargetName(requestId);
	failedStage = L"unique target";
	bool exists = false;
	if (!xucrun::broker_store::Exists(target, exists, error))
		return false;
	if (exists)
	{
		error = ERROR_ALREADY_EXISTS;
		return false;
	}

	std::array<std::uint8_t, 32> firstSecret = {};
	std::array<std::uint8_t, 32> secondSecret = {};
	std::vector<std::uint8_t> storedSecret;
	bool created = false;
	bool success = false;
	if (BCryptGenRandom(nullptr, firstSecret.data(),
		static_cast<ULONG>(firstSecret.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0 ||
		BCryptGenRandom(nullptr, secondSecret.data(),
			static_cast<ULONG>(secondSecret.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
	{
		error = ERROR_GEN_FAILURE;
		failedStage = L"random secret";
	}
	else
	{
		failedStage = L"initial write";
		created = xucrun::broker_store::Write(target, firstSecret, error);
		if (created)
		{
			failedStage = L"client isolation";
			if (xucrun::broker_store::VerifyClientCannotDecrypt(
				clientProcess, target, error))
			{
				failedStage = L"initial read";
				if (xucrun::broker_store::Read(target, storedSecret, error) &&
					Matches(storedSecret, firstSecret, error))
				{
					failedStage = L"overwrite";
					if (xucrun::broker_store::Write(target, secondSecret, error) &&
						xucrun::broker_store::Read(target, storedSecret, error) &&
						Matches(storedSecret, secondSecret, error))
					{
						failedStage = L"empty write";
						if (xucrun::broker_store::Write(target, {}, error) &&
							xucrun::broker_store::Read(target, storedSecret, error) &&
							storedSecret.empty())
						{
							failedStage = L"delete";
							if (xucrun::broker_store::Delete(target, error))
							{
								created = false;
								failedStage = L"deleted target verification";
								if (xucrun::broker_store::Exists(target, exists, error) && !exists)
									success = true;
							}
						}
					}
				}
			}
		}
	}

	DWORD cleanupError = NO_ERROR;
	if (created && !xucrun::broker_store::Delete(target, cleanupError) &&
		error == NO_ERROR)
	{
		error = cleanupError;
		failedStage = L"cleanup";
	}
	SecureZeroMemory(firstSecret.data(), firstSecret.size());
	SecureZeroMemory(secondSecret.data(), secondSecret.size());
	if (!storedSecret.empty())
		SecureZeroMemory(storedSecret.data(), storedSecret.size());
	if (success)
		failedStage.clear();
	return success;
}