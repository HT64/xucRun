// Purpose: Implements canonical generation of opaque broker target names.
// Approach: Implements the behavior with focused helpers built on shared xucRun interfaces and Windows APIs.
// Encodes bound identity values deterministically for the store keys.
#include "TargetNaming.h"

#include <iomanip>
#include <sstream>

namespace xucrun::targets
{
	// Encodes bytes canonically as uppercase hexadecimal text.
	std::wstring BytesToUpperHex(std::span<const std::uint8_t> value)
	{
		static constexpr wchar_t HexDigits[] = L"0123456789ABCDEF";
		std::wstring result;
		result.reserve(value.size() * 2);
		for (std::uint8_t byte : value)
		{
			result += HexDigits[byte >> 4];
			result += HexDigits[byte & 0x0F];
		}
		return result;
	}

	// Derives a canonical decimal target name from the first 128 hash bits.
	std::wstring HashToDecimalTargetName(std::span<const std::uint8_t> hash)
	{
		if (hash.size() < 16)
			return L"";

		std::uint64_t firstPart = 0;
		std::uint64_t secondPart = 0;
		for (std::size_t index = 0; index < 8; index++)
		{
			firstPart = (firstPart << 8) | hash[index];
			secondPart = (secondPart << 8) | hash[index + 8];
		}

		std::wostringstream targetStream;
		targetStream << std::setfill(L'0') << std::setw(20) << firstPart
			<< std::setw(20) << secondPart;
		return targetStream.str();
	}

	// Creates a length-unambiguous HMAC input for scope, paths, and both SHA-256 identities.
	std::wstring BuildScopedApprovalHmacInput(
		xucrun::approval::ScopedApprovalKind kind,
		const std::wstring& credentialTarget,
		const std::wstring& callerPath,
		const std::wstring& targetPath,
		std::span<const std::uint8_t> callerHash,
		std::span<const std::uint8_t> targetHash)
	{
		if (callerHash.size() != 32 || targetHash.size() != 32 ||
			callerPath.empty() || targetPath.empty())
		{
			return L"";
		}

		std::wstring input = kind == xucrun::approval::ScopedApprovalKind::CurrentAccount
			? L"scoped-approval-v5|" + credentialTarget + L"|"
			: L"scoped-approval-v6|all-accounts|";
		input += std::to_wstring(callerPath.size()) + L":" + callerPath + L"|" +
			std::to_wstring(targetPath.size()) + L":" + targetPath + L"|" +
			BytesToUpperHex(callerHash) + L"|" + BytesToUpperHex(targetHash);
		return input;
	}

	// Canonically binds the owner HMAC to the approval scope and, if applicable, the logon target.
	std::wstring BuildScopedOwnerHmacInput(
		xucrun::approval::ScopedApprovalKind kind,
		const std::wstring& credentialTarget)
	{
		return kind == xucrun::approval::ScopedApprovalKind::CurrentAccount
			? L"scoped-approval-owner-v5|" + credentialTarget
			: L"scoped-approval-owner-v6|all-accounts";
	}
}