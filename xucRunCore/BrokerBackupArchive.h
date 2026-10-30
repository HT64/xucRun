// Purpose: Declares the bounded, versioned broker backup archive format and validation API.
// Approach: Exposes the declarations, constants, and types consumed by the components that provide this behavior.
#pragma once

#include "ApprovalSerialization.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace xucrun::backup
{
	inline constexpr std::uint32_t ArchiveVersion = 3;
	inline constexpr std::size_t MachineBindingSize = 32;
	inline constexpr std::size_t MaximumDescriptionLength = 256;
	inline constexpr std::size_t MaximumEntriesPerKind = 256;
	inline constexpr std::size_t MaximumMetadataEntries = 256;
	inline constexpr std::size_t MaximumTargetLength = 32767;
	inline constexpr std::size_t MaximumCredentialBlobSize = 2560;
	inline constexpr std::size_t MaximumMetadataNameLength = 69;
	inline constexpr std::size_t MaximumMetadataSize = 4096;
	inline constexpr std::size_t MaximumArchiveSize = 4 * 1024 * 1024;

	inline std::wstring GetArchiveVersionDirectoryName()
	{
		return L"v" + std::to_wstring(ArchiveVersion) + L"-a" +
			std::to_wstring(xucrun::approval::BrokerApprovalVersion);
	}

	enum class EntryKind : std::uint8_t
	{
		Credential = 1,
		Approval = 2,
		AuthenticityMarker = 3
	};

	struct CredentialEntry
	{
		EntryKind kind = EntryKind::Credential;
		std::wstring target;
		std::vector<std::uint8_t> protectedBlob;
	};

	struct MetadataEntry
	{
		std::wstring name;
		std::vector<std::uint8_t> contents;
	};

	struct Archive
	{
		std::uint64_t createdAt = 0;
		std::wstring description;
		std::array<std::uint8_t, MachineBindingSize> machineBinding = {};
		std::vector<CredentialEntry> credentials;
		std::vector<MetadataEntry> metadata;
	};

	bool Validate(const Archive& archive) noexcept;
	std::vector<std::uint8_t> Serialize(const Archive& archive);
	bool Deserialize(const std::vector<std::uint8_t>& bytes, Archive& archive);
}
