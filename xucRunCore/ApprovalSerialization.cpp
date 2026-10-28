// Purpose: Serializes and deserializes program approvals in a canonical binary format.
// Validates version, field bounds, and structural consistency when reading.
#include "ApprovalSerialization.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace xucrun::approval
{
	namespace
	{
		constexpr std::size_t BrokerApprovalFixedSize = 166;
		constexpr std::size_t BrokerApprovalLengthsOffset = 152;

		// Writes a 64-bit integer canonically in little-endian order.
		void WriteUint64(std::vector<std::uint8_t>& value,
			std::size_t offset, std::uint64_t number)
		{
			for (std::size_t index = 0; index < sizeof(number); index++)
				value[offset + index] = static_cast<std::uint8_t>(number >> (index * 8));
		}

		// Reads a 64-bit integer from the canonical little-endian representation.
		std::uint64_t ReadUint64(std::span<const std::uint8_t> value,
			std::size_t offset)
		{
			std::uint64_t number = 0;
			for (std::size_t index = 0; index < sizeof(number); index++)
				number |= static_cast<std::uint64_t>(value[offset + index]) << (index * 8);
			return number;
		}

		// Validates the consistent combination of all scopes, durations, and required fields.
		bool IsValidBrokerApproval(const BrokerApprovalValue& approval)
		{
			const bool validScope = approval.programScope == BrokerApprovalProgramScope::General ||
				approval.programScope == BrokerApprovalProgramScope::TargetProgram ||
				approval.programScope == BrokerApprovalProgramScope::ExactInvocation;
			const bool validAccountScope =
				approval.accountScope == BrokerApprovalAccountScope::CurrentAccount ||
				approval.accountScope == BrokerApprovalAccountScope::AllAccounts;
			const bool validLifetime = approval.lifetime == BrokerApprovalLifetime::Until ||
				approval.lifetime == BrokerApprovalLifetime::LogonSession ||
				approval.lifetime == BrokerApprovalLifetime::Forever;
			return validScope && validAccountScope && validLifetime &&
				approval.elevationMode <= 1 && approval.visibility <= 2 &&
				approval.createdAt != 0 &&
				!approval.ownerUserSid.empty() &&
				(approval.accountScope == BrokerApprovalAccountScope::AllAccounts
					? approval.targetAccountSid.empty()
					: !approval.targetAccountSid.empty()) &&
				(approval.lifetime == BrokerApprovalLifetime::Until
					? approval.expiresAt > approval.createdAt
					: approval.expiresAt == 0) &&
				(approval.lifetime == BrokerApprovalLifetime::LogonSession
					? !approval.sessionLogonSid.empty()
					: approval.sessionLogonSid.empty()) &&
				(approval.programScope == BrokerApprovalProgramScope::General
					? approval.callerPath.empty() && approval.targetPath.empty() &&
						approval.workingDirectory.empty() && approval.scriptPath.empty() &&
						approval.visibility == 0 &&
						std::all_of(approval.argumentHash.begin(), approval.argumentHash.end(),
							[](std::uint8_t value) { return value == 0; }) &&
						std::all_of(approval.scriptHash.begin(), approval.scriptHash.end(),
							[](std::uint8_t value) { return value == 0; })
					: !approval.callerPath.empty() && !approval.targetPath.empty() &&
						(approval.programScope == BrokerApprovalProgramScope::TargetProgram
							? approval.workingDirectory.empty() && approval.scriptPath.empty() &&
								approval.visibility == 0 &&
								std::all_of(approval.argumentHash.begin(),
									approval.argumentHash.end(),
									[](std::uint8_t value) { return value == 0; }) &&
								std::all_of(approval.scriptHash.begin(),
									approval.scriptHash.end(),
									[](std::uint8_t value) { return value == 0; })
							: !approval.workingDirectory.empty() &&
								std::any_of(approval.argumentHash.begin(),
									approval.argumentHash.end(),
									[](std::uint8_t value) { return value != 0; }) &&
								(approval.scriptPath.empty()
									? std::all_of(approval.scriptHash.begin(),
										approval.scriptHash.end(),
										[](std::uint8_t value) { return value == 0; })
									: std::any_of(approval.scriptHash.begin(),
										approval.scriptHash.end(),
										[](std::uint8_t value) { return value != 0; }))));
		}
	}

	// Serializes a general approval into the fixed, version-specific binary format.
	std::vector<std::uint8_t> SerializeGeneralApproval(
		ApprovalMode mode, std::uint64_t approvalTime)
	{
		std::vector<std::uint8_t> value(GeneralApprovalSize);
		value[0] = GeneralApprovalVersion;
		value[1] = static_cast<std::uint8_t>(mode);
		for (std::size_t index = 0; index < sizeof(approvalTime); index++)
		{
			value[2 + index] = static_cast<std::uint8_t>(
				(approvalTime >> (index * 8)) & 0xFF);
		}
		return value;
	}

	// Reads only general approvals with the exact size and valid version and mode.
	bool DeserializeGeneralApproval(std::span<const std::uint8_t> value,
		ApprovalMode& mode, std::uint64_t& approvalTime) noexcept
	{
		if (value.size() != GeneralApprovalSize ||
			value[0] != GeneralApprovalVersion ||
			(value[1] != static_cast<std::uint8_t>(ApprovalMode::AskAt) &&
			 value[1] != static_cast<std::uint8_t>(ApprovalMode::Forever)))
		{
			return false;
		}

		mode = static_cast<ApprovalMode>(value[1]);
		approvalTime = 0;
		for (std::size_t index = 0; index < sizeof(approvalTime); index++)
			approvalTime |= static_cast<std::uint64_t>(value[2 + index]) << (index * 8);
		return mode == ApprovalMode::Forever || approvalTime != 0;
	}

	// Reads both null-free paths only when their lengths match the payload exactly.
	bool DeserializeScopedApprovalPaths(std::span<const std::uint8_t> value,
		std::wstring& callerPath, std::wstring& targetPath)
	{
		callerPath.clear();
		targetPath.clear();
		if (value.size() < ScopedApprovalHeaderSize ||
			(value[0] != ScopedApprovalVersion &&
			 value[0] != GlobalScopedApprovalVersion))
		{
			return false;
		}

		const std::size_t callerLength = value[106] |
			(static_cast<std::size_t>(value[107]) << 8);
		const std::size_t targetLength = value[108] |
			(static_cast<std::size_t>(value[109]) << 8);
		if (callerLength == 0 || targetLength == 0 ||
			ScopedApprovalHeaderSize + (callerLength + targetLength) * 2 != value.size())
		{
			return false;
		}

		auto readPath = [&value](std::size_t offset, std::size_t length, std::wstring& path)
		{
			path.resize(length);
			for (std::size_t index = 0; index < length; index++)
			{
				path[index] = static_cast<wchar_t>(value[offset + index * 2] |
					(value[offset + index * 2 + 1] << 8));
				if (path[index] == L'\0')
					return false;
			}
			return true;
		};

		const std::size_t targetOffset = ScopedApprovalHeaderSize + callerLength * 2;
		return readPath(ScopedApprovalHeaderSize, callerLength, callerPath) &&
			readPath(targetOffset, targetLength, targetPath);
	}

	// Serializes a scoped approval canonically within the specified maximum size.
	std::vector<std::uint8_t> SerializeScopedApproval(
		const ScopedApprovalValue& approval, std::size_t maximumSize)
	{
		if (approval.callerPath.empty() || approval.targetPath.empty() ||
			approval.callerPath.size() > std::numeric_limits<std::uint16_t>::max() ||
			approval.targetPath.size() > std::numeric_limits<std::uint16_t>::max())
		{
			return {};
		}

		const std::size_t valueSize = ScopedApprovalHeaderSize +
			(approval.callerPath.size() + approval.targetPath.size()) * 2;
		if (valueSize > maximumSize)
			return {};

		std::vector<std::uint8_t> value(valueSize);
		value[0] = static_cast<std::uint8_t>(approval.kind);
		value[1] = static_cast<std::uint8_t>(approval.mode);
		for (std::size_t index = 0; index < sizeof(approval.approvalTime); index++)
		{
			value[2 + index] = static_cast<std::uint8_t>(
				(approval.approvalTime >> (index * 8)) & 0xFF);
		}
		std::copy(approval.callerHash.begin(), approval.callerHash.end(), value.begin() + 10);
		std::copy(approval.targetHash.begin(), approval.targetHash.end(), value.begin() + 42);
		std::copy(approval.ownerHash.begin(), approval.ownerHash.end(), value.begin() + 74);
		value[106] = static_cast<std::uint8_t>(approval.callerPath.size() & 0xFF);
		value[107] = static_cast<std::uint8_t>((approval.callerPath.size() >> 8) & 0xFF);
		value[108] = static_cast<std::uint8_t>(approval.targetPath.size() & 0xFF);
		value[109] = static_cast<std::uint8_t>((approval.targetPath.size() >> 8) & 0xFF);

		auto writePath = [&value](std::size_t offset, const std::wstring& path)
		{
			for (std::size_t index = 0; index < path.size(); index++)
			{
				value[offset + index * 2] = static_cast<std::uint8_t>(path[index] & 0xFF);
				value[offset + index * 2 + 1] = static_cast<std::uint8_t>((path[index] >> 8) & 0xFF);
			}
		};
		writePath(ScopedApprovalHeaderSize, approval.callerPath);
		writePath(ScopedApprovalHeaderSize + approval.callerPath.size() * 2, approval.targetPath);
		return value;
	}

	// Accepts a scoped approval only after complete structure and mode validation.
	bool DeserializeScopedApproval(std::span<const std::uint8_t> value,
		ScopedApprovalValue& approval)
	{
		ScopedApprovalValue parsed;
		if (!DeserializeScopedApprovalPaths(value, parsed.callerPath, parsed.targetPath) ||
			(value[1] != static_cast<std::uint8_t>(ApprovalMode::AskAt) &&
			 value[1] != static_cast<std::uint8_t>(ApprovalMode::Forever)))
		{
			return false;
		}

		parsed.kind = value[0] == ScopedApprovalVersion
			? ScopedApprovalKind::CurrentAccount
			: ScopedApprovalKind::AllAccounts;
		parsed.mode = static_cast<ApprovalMode>(value[1]);
		for (std::size_t index = 0; index < sizeof(parsed.approvalTime); index++)
			parsed.approvalTime |= static_cast<std::uint64_t>(value[2 + index]) << (index * 8);
		if (parsed.mode != ApprovalMode::Forever && parsed.approvalTime == 0)
			return false;

		std::copy_n(value.begin() + 10, parsed.callerHash.size(), parsed.callerHash.begin());
		std::copy_n(value.begin() + 42, parsed.targetHash.size(), parsed.targetHash.begin());
		std::copy_n(value.begin() + 74, parsed.ownerHash.size(), parsed.ownerHash.begin());
		approval = std::move(parsed);
		return true;
	}

	// Serializes a consistent broker approval canonically and within size limits.
	std::vector<std::uint8_t> SerializeBrokerApproval(
		const BrokerApprovalValue& approval, std::size_t maximumSize)
	{
		if (!IsValidBrokerApproval(approval))
			return {};
		const std::wstring* strings[] = {
			&approval.ownerUserSid, &approval.targetAccountSid,
			&approval.sessionLogonSid, &approval.callerPath,
			&approval.targetPath, &approval.workingDirectory,
			&approval.scriptPath
		};
		std::size_t characterCount = 0;
		for (const std::wstring* text : strings)
		{
			if (text->size() > std::numeric_limits<std::uint16_t>::max())
				return {};
			characterCount += text->size();
		}
		if (characterCount > (maximumSize - std::min(maximumSize, BrokerApprovalFixedSize)) / 2)
			return {};

		std::vector<std::uint8_t> value(BrokerApprovalFixedSize + characterCount * 2);
		value[0] = BrokerApprovalVersion;
		value[1] = static_cast<std::uint8_t>(approval.programScope);
		value[2] = static_cast<std::uint8_t>(approval.accountScope);
		value[3] = static_cast<std::uint8_t>(approval.lifetime);
		value[4] = approval.elevationMode;
		value[5] = approval.visibility;
		WriteUint64(value, 8, approval.createdAt);
		WriteUint64(value, 16, approval.expiresAt);
		std::copy(approval.callerHash.begin(), approval.callerHash.end(), value.begin() + 24);
		std::copy(approval.targetHash.begin(), approval.targetHash.end(), value.begin() + 56);
		std::copy(approval.argumentHash.begin(), approval.argumentHash.end(), value.begin() + 88);
		std::copy(approval.scriptHash.begin(), approval.scriptHash.end(), value.begin() + 120);

		std::size_t lengthOffset = BrokerApprovalLengthsOffset;
		std::size_t stringOffset = BrokerApprovalFixedSize;
		for (const std::wstring* text : strings)
		{
			value[lengthOffset++] = static_cast<std::uint8_t>(text->size() & 0xFF);
			value[lengthOffset++] = static_cast<std::uint8_t>(text->size() >> 8);
			for (wchar_t character : *text)
			{
				if (character == L'\0')
					return {};
				value[stringOffset++] = static_cast<std::uint8_t>(character & 0xFF);
				value[stringOffset++] = static_cast<std::uint8_t>(character >> 8);
			}
		}
		return value;
	}

	// Accepts a broker approval only after complete consumption and with a valid field combination.
	bool DeserializeBrokerApproval(std::span<const std::uint8_t> value,
		BrokerApprovalValue& approval)
	{
		if (value.size() < BrokerApprovalFixedSize ||
			value[0] != BrokerApprovalVersion ||
			value[6] != 0 || value[7] != 0)
		{
			return false;
		}

		BrokerApprovalValue parsed;
		parsed.programScope = static_cast<BrokerApprovalProgramScope>(value[1]);
		parsed.accountScope = static_cast<BrokerApprovalAccountScope>(value[2]);
		parsed.lifetime = static_cast<BrokerApprovalLifetime>(value[3]);
		parsed.elevationMode = value[4];
		parsed.visibility = value[5];
		parsed.createdAt = ReadUint64(value, 8);
		parsed.expiresAt = ReadUint64(value, 16);
		std::copy_n(value.begin() + 24, parsed.callerHash.size(), parsed.callerHash.begin());
		std::copy_n(value.begin() + 56, parsed.targetHash.size(), parsed.targetHash.begin());
		std::copy_n(value.begin() + 88, parsed.argumentHash.size(),
			parsed.argumentHash.begin());
		std::copy_n(value.begin() + 120, parsed.scriptHash.size(),
			parsed.scriptHash.begin());

		std::wstring* strings[] = {
			&parsed.ownerUserSid, &parsed.targetAccountSid,
			&parsed.sessionLogonSid, &parsed.callerPath,
			&parsed.targetPath, &parsed.workingDirectory,
			&parsed.scriptPath
		};
		std::size_t lengthOffset = BrokerApprovalLengthsOffset;
		std::size_t stringOffset = BrokerApprovalFixedSize;
		for (std::wstring* text : strings)
		{
			const std::size_t length = value[lengthOffset] |
				(static_cast<std::size_t>(value[lengthOffset + 1]) << 8);
			lengthOffset += 2;
			if (length > (value.size() - stringOffset) / 2)
				return false;
			text->resize(length);
			for (std::size_t index = 0; index < length; index++)
			{
				(*text)[index] = static_cast<wchar_t>(value[stringOffset] |
					(value[stringOffset + 1] << 8));
				if ((*text)[index] == L'\0')
					return false;
				stringOffset += 2;
			}
		}
		if (stringOffset != value.size() || !IsValidBrokerApproval(parsed))
			return false;
		approval = std::move(parsed);
		return true;
	}

	// Determines expiration from absolute time or an unchanged logon session SID.
	bool IsBrokerApprovalExpired(const BrokerApprovalValue& approval,
		std::uint64_t now, const std::wstring& currentLogonSid) noexcept
	{
		if (approval.lifetime == BrokerApprovalLifetime::Until)
			return approval.expiresAt <= now;
		return approval.lifetime == BrokerApprovalLifetime::LogonSession &&
			_wcsicmp(approval.sessionLogonSid.c_str(), currentLogonSid.c_str()) != 0;
	}
}