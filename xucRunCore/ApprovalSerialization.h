// Purpose: Defines the versioned binary format for stored program approvals.
// Provides types, bounds, and serialization and validation operations.
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace xucrun::approval
{
	inline constexpr std::uint8_t GeneralApprovalVersion = 1;
	inline constexpr std::size_t GeneralApprovalSize = 10;
	inline constexpr std::uint8_t ScopedApprovalVersion = 5;
	inline constexpr std::uint8_t GlobalScopedApprovalVersion = 6;
	inline constexpr std::size_t ScopedApprovalHeaderSize = 110;
	inline constexpr std::uint8_t BrokerApprovalVersion = 9;

	enum class ApprovalMode : std::uint8_t
	{
		AskAt = 1,
		Forever = 2
	};

	enum class ScopedApprovalKind : std::uint8_t
	{
		CurrentAccount = ScopedApprovalVersion,
		AllAccounts = GlobalScopedApprovalVersion
	};

	using Sha256Hash = std::array<std::uint8_t, 32>;

	enum class BrokerApprovalProgramScope : std::uint8_t
	{
		General = 1,
		TargetProgram = 2,
		ExactInvocation = 3
	};

	enum class BrokerApprovalAccountScope : std::uint8_t
	{
		CurrentAccount = 1,
		AllAccounts = 2
	};

	enum class BrokerApprovalLifetime : std::uint8_t
	{
		Until = 1,
		LogonSession = 2,
		Forever = 3
	};

	struct BrokerApprovalValue
	{
		BrokerApprovalProgramScope programScope = BrokerApprovalProgramScope::General;
		BrokerApprovalAccountScope accountScope = BrokerApprovalAccountScope::CurrentAccount;
		BrokerApprovalLifetime lifetime = BrokerApprovalLifetime::Until;
		std::uint8_t elevationMode = 0;
		std::uint8_t visibility = 0;
		std::uint64_t createdAt = 0;
		std::uint64_t expiresAt = 0;
		Sha256Hash callerHash = {};
		Sha256Hash targetHash = {};
		Sha256Hash argumentHash = {};
		Sha256Hash scriptHash = {};
		std::wstring ownerUserSid;
		std::wstring targetAccountSid;
		std::wstring sessionLogonSid;
		std::wstring callerPath;
		std::wstring targetPath;
		std::wstring workingDirectory;
		std::wstring scriptPath;
	};

	struct ScopedApprovalValue
	{
		ScopedApprovalKind kind = ScopedApprovalKind::CurrentAccount;
		ApprovalMode mode = ApprovalMode::AskAt;
		std::uint64_t approvalTime = 0;
		Sha256Hash callerHash = {};
		Sha256Hash targetHash = {};
		Sha256Hash ownerHash = {};
		std::wstring callerPath;
		std::wstring targetPath;
	};

	std::vector<std::uint8_t> SerializeGeneralApproval(
		ApprovalMode mode, std::uint64_t approvalTime);
	bool DeserializeGeneralApproval(std::span<const std::uint8_t> value,
		ApprovalMode& mode, std::uint64_t& approvalTime) noexcept;
	bool DeserializeScopedApprovalPaths(std::span<const std::uint8_t> value,
		std::wstring& callerPath, std::wstring& targetPath);
	std::vector<std::uint8_t> SerializeScopedApproval(
		const ScopedApprovalValue& approval, std::size_t maximumSize);
	bool DeserializeScopedApproval(std::span<const std::uint8_t> value,
		ScopedApprovalValue& approval);
	std::vector<std::uint8_t> SerializeBrokerApproval(
		const BrokerApprovalValue& approval, std::size_t maximumSize);
	bool DeserializeBrokerApproval(std::span<const std::uint8_t> value,
		BrokerApprovalValue& approval);
	bool IsBrokerApprovalExpired(const BrokerApprovalValue& approval,
		std::uint64_t now, const std::wstring& currentLogonSid) noexcept;
}