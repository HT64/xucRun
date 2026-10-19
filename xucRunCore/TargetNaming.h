// Purpose: Declares canonical inputs and target names for broker store entries.
// Keeps name generation consistent between credential and approval stores.
#pragma once

#include "ApprovalSerialization.h"

#include <cstdint>
#include <span>
#include <string>

namespace xucrun::targets
{
	std::wstring BytesToUpperHex(std::span<const std::uint8_t> value);
	std::wstring HashToDecimalTargetName(std::span<const std::uint8_t> hash);
	std::wstring BuildScopedApprovalHmacInput(
		xucrun::approval::ScopedApprovalKind kind,
		const std::wstring& credentialTarget,
		const std::wstring& callerPath,
		const std::wstring& targetPath,
		std::span<const std::uint8_t> callerHash,
		std::span<const std::uint8_t> targetHash);
	std::wstring BuildScopedOwnerHmacInput(
		xucrun::approval::ScopedApprovalKind kind,
		const std::wstring& credentialTarget);
}