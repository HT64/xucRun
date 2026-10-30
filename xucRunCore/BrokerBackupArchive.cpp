// Purpose: Serializes and validates the versioned binary archive shared by broker backup workflows.
// Approach: Exercises the behavior with focused assertions against the shared implementation and Windows contracts.
#include "BrokerBackupArchive.h"

#include <algorithm>
#include <set>

namespace
{
	constexpr std::array<std::uint8_t, 8> Magic = {
		'X', 'U', 'C', 'B', 'A', 'K', '1', 0
	};

	void AppendUInt32(std::vector<std::uint8_t>& bytes, std::uint32_t value)
	{
		for (unsigned int shift = 0; shift < 32; shift += 8)
			bytes.push_back(static_cast<std::uint8_t>(value >> shift));
	}

	void AppendUInt64(std::vector<std::uint8_t>& bytes, std::uint64_t value)
	{
		for (unsigned int shift = 0; shift < 64; shift += 8)
			bytes.push_back(static_cast<std::uint8_t>(value >> shift));
	}

	void AppendString(std::vector<std::uint8_t>& bytes, const std::wstring& value)
	{
		AppendUInt32(bytes, static_cast<std::uint32_t>(value.size()));
		for (const wchar_t character : value)
		{
			bytes.push_back(static_cast<std::uint8_t>(character));
			bytes.push_back(static_cast<std::uint8_t>(character >> 8));
		}
	}

	void AppendBytes(std::vector<std::uint8_t>& bytes,
		const std::vector<std::uint8_t>& value)
	{
		AppendUInt32(bytes, static_cast<std::uint32_t>(value.size()));
		bytes.insert(bytes.end(), value.begin(), value.end());
	}

	bool ReadUInt32(const std::vector<std::uint8_t>& bytes,
		std::size_t& offset, std::uint32_t& value)
	{
		if (offset > bytes.size() || bytes.size() - offset < 4)
			return false;
		value = 0;
		for (unsigned int shift = 0; shift < 32; shift += 8)
			value |= static_cast<std::uint32_t>(bytes[offset++]) << shift;
		return true;
	}

	bool ReadUInt64(const std::vector<std::uint8_t>& bytes,
		std::size_t& offset, std::uint64_t& value)
	{
		if (offset > bytes.size() || bytes.size() - offset < 8)
			return false;
		value = 0;
		for (unsigned int shift = 0; shift < 64; shift += 8)
			value |= static_cast<std::uint64_t>(bytes[offset++]) << shift;
		return true;
	}

	bool ReadString(const std::vector<std::uint8_t>& bytes, std::size_t& offset,
		std::size_t maximumLength, std::wstring& value, bool allowEmpty = false)
	{
		std::uint32_t length = 0;
		if (!ReadUInt32(bytes, offset, length) || (!allowEmpty && length == 0) ||
			length > maximumLength || offset > bytes.size() ||
			bytes.size() - offset < static_cast<std::size_t>(length) * 2)
		{
			return false;
		}
		value.resize(length);
		for (std::size_t index = 0; index < length; index++)
		{
			const std::uint16_t character = static_cast<std::uint16_t>(bytes[offset]) |
				(static_cast<std::uint16_t>(bytes[offset + 1]) << 8);
			if (character == 0)
				return false;
			value[index] = static_cast<wchar_t>(character);
			offset += 2;
		}
		return true;
	}

	bool ReadBytes(const std::vector<std::uint8_t>& bytes, std::size_t& offset,
		std::size_t maximumSize, std::vector<std::uint8_t>& value)
	{
		std::uint32_t size = 0;
		if (!ReadUInt32(bytes, offset, size) || size == 0 || size > maximumSize ||
			offset > bytes.size() || bytes.size() - offset < size)
		{
			return false;
		}
		value.assign(bytes.begin() + offset, bytes.begin() + offset + size);
		offset += size;
		return true;
	}

	bool IsValidMetadataName(const std::wstring& name)
	{
		if (name.size() != 69 || !name.ends_with(L".meta"))
			return false;
		return std::all_of(name.begin(), name.begin() + 64, [](wchar_t character) {
			return (character >= L'0' && character <= L'9') ||
				(character >= L'A' && character <= L'F');
		});
	}
}

namespace xucrun::backup
{
	bool Validate(const Archive& archive) noexcept
	{
		if (archive.createdAt == 0 ||
			archive.description.size() > MaximumDescriptionLength ||
			std::any_of(archive.description.begin(), archive.description.end(),
				[](wchar_t character) { return character < L' '; }) ||
			std::all_of(archive.machineBinding.begin(), archive.machineBinding.end(),
				[](std::uint8_t value) { return value == 0; }) ||
			archive.metadata.size() > MaximumMetadataEntries)
		{
			return false;
		}
		std::array<std::size_t, 3> counts = {};
		std::set<std::wstring> targets;
		for (const auto& entry : archive.credentials)
		{
			const auto kind = static_cast<std::uint8_t>(entry.kind);
			if (kind < static_cast<std::uint8_t>(EntryKind::Credential) ||
				kind > static_cast<std::uint8_t>(EntryKind::AuthenticityMarker) ||
				++counts[kind - 1] > MaximumEntriesPerKind || entry.target.empty() ||
				entry.target.size() > MaximumTargetLength ||
				entry.target.find(L'\0') != std::wstring::npos ||
				entry.protectedBlob.empty() ||
				entry.protectedBlob.size() > MaximumCredentialBlobSize ||
				!targets.insert(entry.target).second)
			{
				return false;
			}
		}
		std::set<std::wstring> names;
		for (const auto& entry : archive.metadata)
		{
			if (!IsValidMetadataName(entry.name) || entry.contents.empty() ||
				entry.contents.size() > MaximumMetadataSize ||
				!names.insert(entry.name).second)
			{
				return false;
			}
		}
		return true;
	}

	std::vector<std::uint8_t> Serialize(const Archive& archive)
	{
		if (!Validate(archive))
			return {};
		std::vector<std::uint8_t> bytes;
		bytes.reserve(64 + archive.credentials.size() * 128);
		bytes.insert(bytes.end(), Magic.begin(), Magic.end());
		AppendUInt32(bytes, ArchiveVersion);
		AppendUInt64(bytes, archive.createdAt);
		AppendString(bytes, archive.description);
		bytes.insert(bytes.end(), archive.machineBinding.begin(), archive.machineBinding.end());
		AppendUInt32(bytes, static_cast<std::uint32_t>(archive.credentials.size()));
		AppendUInt32(bytes, static_cast<std::uint32_t>(archive.metadata.size()));
		for (const auto& entry : archive.credentials)
		{
			bytes.push_back(static_cast<std::uint8_t>(entry.kind));
			AppendString(bytes, entry.target);
			AppendBytes(bytes, entry.protectedBlob);
		}
		for (const auto& entry : archive.metadata)
		{
			AppendString(bytes, entry.name);
			AppendBytes(bytes, entry.contents);
		}
		return bytes.size() <= MaximumArchiveSize ? bytes : std::vector<std::uint8_t>{};
	}

	bool Deserialize(const std::vector<std::uint8_t>& bytes, Archive& archive)
	{
		archive = {};
		if (bytes.size() < Magic.size() + 4 + 8 + 4 + MachineBindingSize + 8 ||
			bytes.size() > MaximumArchiveSize ||
			!std::equal(Magic.begin(), Magic.end(), bytes.begin()))
		{
			return false;
		}
		std::size_t offset = Magic.size();
		std::uint32_t version = 0;
		std::uint32_t credentialCount = 0;
		std::uint32_t metadataCount = 0;
		if (!ReadUInt32(bytes, offset, version) || version != ArchiveVersion ||
			!ReadUInt64(bytes, offset, archive.createdAt) ||
			!ReadString(bytes, offset, MaximumDescriptionLength,
				archive.description, true) ||
			offset > bytes.size() || bytes.size() - offset < MachineBindingSize)
		{
			return false;
		}
		std::copy_n(bytes.begin() + offset, MachineBindingSize,
			archive.machineBinding.begin());
		offset += MachineBindingSize;
		if (!ReadUInt32(bytes, offset, credentialCount) ||
			credentialCount > MaximumEntriesPerKind * 3 ||
			!ReadUInt32(bytes, offset, metadataCount) ||
			metadataCount > MaximumMetadataEntries)
		{
			return false;
		}
		archive.credentials.reserve(credentialCount);
		for (std::uint32_t index = 0; index < credentialCount; index++)
		{
			if (offset >= bytes.size())
				return false;
			CredentialEntry entry;
			entry.kind = static_cast<EntryKind>(bytes[offset++]);
			if (!ReadString(bytes, offset, MaximumTargetLength, entry.target) ||
				!ReadBytes(bytes, offset, MaximumCredentialBlobSize, entry.protectedBlob))
			{
				return false;
			}
			archive.credentials.push_back(std::move(entry));
		}
		archive.metadata.reserve(metadataCount);
		for (std::uint32_t index = 0; index < metadataCount; index++)
		{
			MetadataEntry entry;
			if (!ReadString(bytes, offset, MaximumMetadataNameLength, entry.name) ||
				!ReadBytes(bytes, offset, MaximumMetadataSize, entry.contents))
			{
				return false;
			}
			archive.metadata.push_back(std::move(entry));
		}
		return offset == bytes.size() && Validate(archive);
	}
}
