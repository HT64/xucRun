// Purpose: Stores a random per-user marker that only the broker reveals to authorized helpers.
#include "AuthenticityMarkerStore.h"

#include "BrokerTargetNaming.h"
#include "CredentialStore.h"
#include "TargetNaming.h"

#include <bcrypt.h>
#include <sddl.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace
{
	constexpr std::size_t MarkerByteCount = 4;
	constexpr std::size_t MarkerCharacterCount = 9;

	void Erase(std::vector<std::uint8_t>& value) noexcept
	{
		if (!value.empty())
			SecureZeroMemory(value.data(), value.size());
		value.clear();
	}

	bool BuildTarget(const std::wstring& clientSid, std::wstring& target, DWORD& error)
	{
		PSID sid = nullptr;
		if (!ConvertStringSidToSidW(clientSid.c_str(), &sid))
		{
			error = GetLastError();
			return false;
		}
		LocalFree(sid);

		BCRYPT_ALG_HANDLE algorithm = nullptr;
		BCRYPT_HASH_HANDLE hash = nullptr;
		std::array<std::uint8_t, 32> digest = {};
		NTSTATUS status = BCryptOpenAlgorithmProvider(
			&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
		if (status == 0)
			status = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0);
		if (status == 0)
		{
			status = BCryptHashData(hash,
				reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(clientSid.data())),
				static_cast<ULONG>(clientSid.size() * sizeof(wchar_t)), 0);
		}
		if (status == 0)
			status = BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0);
		if (hash)
			BCryptDestroyHash(hash);
		if (algorithm)
			BCryptCloseAlgorithmProvider(algorithm, 0);
		if (status != 0)
		{
			error = ERROR_GEN_FAILURE;
			return false;
		}

		std::wstring prefix;
		if (!xucrun::broker_targets::GetMachineScopedPrefix(
			xucrun::broker_targets::TargetKind::AuthenticityMarker, prefix, error))
		{
			return false;
		}
		target = prefix + xucrun::targets::BytesToUpperHex(digest);
		error = NO_ERROR;
		return true;
	}

	bool IsValidMarker(const std::wstring& marker, std::size_t expectedLength)
	{
		if (marker.size() != expectedLength)
			return false;
		for (std::size_t index = 0; index < marker.size(); index++)
		{
			if ((index + 1) % 5 == 0)
			{
				if (marker[index] != L'-')
					return false;
			}
			else if (!((marker[index] >= L'0' && marker[index] <= L'9') ||
				(marker[index] >= L'A' && marker[index] <= L'F')))
			{
				return false;
			}
		}
		return true;
	}

	bool Decode(const std::vector<std::uint8_t>& bytes, std::wstring& marker)
	{
		const std::size_t characterCount = bytes.size() / sizeof(wchar_t);
		if (bytes.size() % sizeof(wchar_t) != 0 ||
			characterCount != MarkerCharacterCount)
		{
			return false;
		}
		marker.resize(characterCount);
		std::memcpy(marker.data(), bytes.data(), bytes.size());
		if (!IsValidMarker(marker, characterCount))
			return false;
		return true;
	}

	bool Generate(std::wstring& marker, DWORD& error)
	{
		std::array<std::uint8_t, MarkerByteCount> random = {};
		const NTSTATUS status = BCryptGenRandom(nullptr, random.data(),
			static_cast<ULONG>(random.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
		if (status != 0)
		{
			error = ERROR_GEN_FAILURE;
			return false;
		}
		constexpr wchar_t Digits[] = L"0123456789ABCDEF";
		marker.clear();
		marker.reserve(MarkerCharacterCount);
		for (std::size_t index = 0; index < random.size(); index++)
		{
			if (index != 0 && index % 2 == 0)
				marker.push_back(L'-');
			marker.push_back(Digits[random[index] >> 4]);
			marker.push_back(Digits[random[index] & 0x0F]);
		}
		error = NO_ERROR;
		return true;
	}
}

namespace xucrun::authenticity_marker_store
{
	bool GetOrCreate(const std::wstring& clientSid,
		std::wstring& marker, DWORD& error)
	{
		marker.clear();
		std::wstring target;
		if (!BuildTarget(clientSid, target, error))
			return false;

		bool exists = false;
		if (!broker_store::Exists(target, exists, error))
			return false;
		if (exists)
		{
			std::vector<std::uint8_t> stored;
			const bool read = broker_store::Read(target, stored, error);
			const bool valid = read && Decode(stored, marker);
			Erase(stored);
			if (!valid)
			{
				marker.clear();
				if (read)
					error = ERROR_INVALID_DATA;
			}
			return valid;
		}

		if (!Generate(marker, error))
			return false;
		const auto* markerBytes = reinterpret_cast<const std::uint8_t*>(marker.data());
		const std::span<const std::uint8_t> value(
			markerBytes, marker.size() * sizeof(wchar_t));
		if (!broker_store::Write(target, value, error))
		{
			marker.clear();
			return false;
		}
		std::vector<std::uint8_t> stored;
		std::wstring verified;
		const bool readBack = broker_store::Read(target, stored, error) &&
			Decode(stored, verified) && verified == marker;
		Erase(stored);
		if (!readBack)
		{
			const DWORD failureError = error == NO_ERROR ? ERROR_INVALID_DATA : error;
			DWORD cleanupError = NO_ERROR;
			broker_store::Delete(target, cleanupError);
			marker.clear();
			error = failureError;
			return false;
		}
		error = NO_ERROR;
		return true;
	}
}
