// Purpose: Generates machine-bound target names for sensitive broker storage entries.
// Approach: Implements the behavior with focused helpers built on shared xucRun interfaces and Windows APIs.
// Derives them from MachineGuid, identity values, and the respective storage purpose.
#include "BrokerTargetNaming.h"

#include "ApprovalSerialization.h"
#include "TargetNaming.h"

#include <bcrypt.h>

#include <cstdint>
#include <vector>

namespace
{
	// Reads the stable machine identifier from the 64-bit system registry.
	bool ReadMachineGuid(std::wstring& machineGuid, DWORD& error)
	{
		machineGuid.clear();
		HKEY key = nullptr;
		const LSTATUS openStatus = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
			L"SOFTWARE\\Microsoft\\Cryptography", 0,
			KEY_READ | KEY_WOW64_64KEY, &key);
		if (openStatus != ERROR_SUCCESS)
		{
			error = static_cast<DWORD>(openStatus);
			return false;
		}

		wchar_t value[256] = {};
		DWORD type = 0;
		DWORD size = sizeof(value);
		const LSTATUS queryStatus = RegQueryValueExW(key, L"MachineGuid", nullptr,
			&type, reinterpret_cast<BYTE*>(value), &size);
		RegCloseKey(key);
		if (queryStatus != ERROR_SUCCESS ||
			(type != REG_SZ && type != REG_EXPAND_SZ) || size <= sizeof(wchar_t))
		{
			error = queryStatus == ERROR_SUCCESS
				? ERROR_INVALID_DATA
				: static_cast<DWORD>(queryStatus);
			return false;
		}

		value[std::size(value) - 1] = L'\0';
		machineGuid = value;
		if (machineGuid.empty())
		{
			error = ERROR_INVALID_DATA;
			return false;
		}
		error = NO_ERROR;
		return true;
	}

	// Computes the SHA-256 hash of a UTF-16 value for target name derivation.
	bool ComputeSha256(const std::wstring& value,
		std::vector<std::uint8_t>& digest, DWORD& error)
	{
		BCRYPT_ALG_HANDLE algorithm = nullptr;
		BCRYPT_HASH_HANDLE hash = nullptr;
		digest.assign(32, 0);
		NTSTATUS status = BCryptOpenAlgorithmProvider(
			&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
		if (status == 0)
			status = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0);
		if (status == 0)
		{
			status = BCryptHashData(hash,
				reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(value.data())),
				static_cast<ULONG>(value.size() * sizeof(wchar_t)), 0);
		}
		if (status == 0)
		{
			status = BCryptFinishHash(hash, digest.data(),
				static_cast<ULONG>(digest.size()), 0);
		}
		if (hash)
			BCryptDestroyHash(hash);
		if (algorithm)
			BCryptCloseAlgorithmProvider(algorithm, 0);
		if (status != 0)
		{
			digest.clear();
			error = ERROR_GEN_FAILURE;
			return false;
		}
		error = NO_ERROR;
		return true;
	}
}

namespace xucrun::broker_targets
{
	// Derives a machine- and storage-type-specific prefix to prevent targets from colliding across contexts.
	bool GetMachineScopedPrefix(TargetKind kind, std::wstring& prefix, DWORD& error)
	{
		std::wstring machineGuid;
		std::vector<std::uint8_t> digest;
		const wchar_t* scope = nullptr;
		switch (kind)
		{
		case TargetKind::Credential:
			scope = L"|1";
			break;
		case TargetKind::Approval:
			scope = L"|2";
			break;
		case TargetKind::AuthenticityMarker:
			scope = L"|3";
			break;
		default:
			prefix.clear();
			error = ERROR_INVALID_PARAMETER;
			return false;
		}
		if (!ReadMachineGuid(machineGuid, error) ||
			!ComputeSha256(machineGuid + scope, digest, error))
		{
			prefix.clear();
			return false;
		}
		prefix = xucrun::targets::HashToDecimalTargetName(digest) + L"-";
		if (prefix.size() != 41)
		{
			prefix.clear();
			error = ERROR_INVALID_DATA;
			return false;
		}
		error = NO_ERROR;
		return true;
	}

	bool GetCurrentApprovalPrefix(std::wstring& prefix, DWORD& error)
	{
		if (!GetMachineScopedPrefix(TargetKind::Approval, prefix, error))
			return false;
		prefix += std::to_wstring(xucrun::approval::BrokerApprovalVersion) + L"-";
		return true;
	}
}