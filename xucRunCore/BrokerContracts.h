// Purpose: Defines the shared request and result types of the broker protocol.
// Specifies the protocol version, field bounds, and permitted status values.
#pragma once

#include "ApprovalSerialization.h"
#include "BrokerBackupArchive.h"
#include "SensitiveString.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace xucrun::broker
{
	inline constexpr std::uint32_t ProtocolVersion = 40;
	inline constexpr std::uint32_t PolicyDeniedWindowsError = 5;
	inline constexpr std::size_t MaximumAccountSidLength = 184;
	inline constexpr std::size_t MaximumPathLength = 32767;
	inline constexpr std::size_t MaximumArgumentLength = 32767;
	inline constexpr std::size_t MaximumPasswordLength = 256;
	inline constexpr std::size_t MaximumCanonicalAccountNameLength = 512;
	inline constexpr std::size_t MaximumAuthenticityMarkerLength = 9;
	inline constexpr std::size_t MaximumCredentialListEntries = 64;
	inline constexpr std::size_t MaximumAdministrativeCredentialEntries = 256;
	inline constexpr std::size_t MaximumAdministrativeApprovalEntries = 256;
	inline constexpr std::size_t MaximumBackupEntries = 128;
	inline constexpr std::size_t BackupIdLength = 32;
	inline constexpr std::size_t MaximumTrackedProcessEntries = 128;
	inline constexpr std::size_t MaximumProcessBaseNameLength = 260;
	inline constexpr std::size_t MaximumStatusIconEntries = 16;
	inline constexpr std::size_t MaximumSecurityAuditFindings = 32;
	inline constexpr std::size_t MaximumSecurityAuditReportLength = 16384;
	inline constexpr std::uint32_t DefaultBrokerRequestTimeoutMilliseconds = 5000;
	inline constexpr std::uint32_t BrokerIpcTimeoutReserveMilliseconds = 5000;
	inline constexpr std::uint32_t ApprovalChallengeLifetimeMilliseconds = 120000;
	inline constexpr std::uint32_t ApprovalDialogTimeoutMilliseconds = 120000;
	inline constexpr std::uint32_t CredentialCaptureDialogTimeoutMilliseconds = 60000;
	inline constexpr std::uint32_t ApprovalHelperDecisionLifetimeMilliseconds = 130000;
	inline constexpr std::uint32_t ApprovalHelperMaximumLifetimeMilliseconds = 300000;
	inline constexpr std::uint32_t ApprovalHelperHostTimeoutMilliseconds =
		ApprovalHelperMaximumLifetimeMilliseconds + BrokerIpcTimeoutReserveMilliseconds;
	inline constexpr std::uint32_t ApprovalHelperProcessWaitTimeoutMilliseconds =
		ApprovalHelperHostTimeoutMilliseconds + BrokerIpcTimeoutReserveMilliseconds;
	inline constexpr std::uint32_t ElevatedLaunchHelperResultMagic = 0x52434C45;
	inline constexpr std::uint32_t ElevatedLaunchHelperTimeoutMilliseconds = 30000;
	inline constexpr std::uint32_t ApprovalChallengeRequestTimeoutMilliseconds =
		ElevatedLaunchHelperTimeoutMilliseconds + BrokerIpcTimeoutReserveMilliseconds;
	inline constexpr std::uint32_t ApprovalConfirmationRequestTimeoutMilliseconds =
		ElevatedLaunchHelperTimeoutMilliseconds + BrokerIpcTimeoutReserveMilliseconds;
	inline constexpr wchar_t ApprovalDiagnosticArgumentTail[] =
		L"/d /k \"title xucRun Broker Approval Test & echo Target identity: & whoami\"";
	inline constexpr wchar_t AdministrativeInventoryViewerArgumentTail[] =
		L"/internal-admin-status-view";
	inline constexpr wchar_t AdministrativeBackupViewerArgumentTail[] =
		L"/internal-backup-view";
	inline constexpr wchar_t AdministrativeBackupRestoreViewerArgumentTail[] =
		L"/internal-backup-restore-view";
	inline constexpr wchar_t AdministrativeBackupCommandArgumentPrefix[] =
		L"/internal-backup-command:";
	inline constexpr std::size_t AdministrativeBackupCommandTokenLength = 32;
	inline constexpr wchar_t AdministrativeInventoryConsoleArgumentPrefix[] =
		L"/internal-admin-status-console:";
	inline constexpr std::size_t AdministrativeInventoryConsoleTokenLength = 32;
	inline constexpr wchar_t SetLanguageGermanArgumentTail[] =
		L"/internal-set-language:de";
	inline constexpr wchar_t SetLanguageEnglishArgumentTail[] =
		L"/internal-set-language:en";
	inline constexpr wchar_t RequestLanguageGermanArgumentTail[] =
		L"/set-language:de";
	inline constexpr wchar_t RequestLanguageEnglishArgumentTail[] =
		L"/set-language:en";
	inline constexpr std::size_t SetLanguagePipeTokenLength = 32;
	inline constexpr wchar_t SetLanguagePipeNamePrefix[] =
		L"\\\\.\\pipe\\xucRun.LanguageChange.";
	inline constexpr wchar_t StatusIconLanguageChangedEventNamePrefix[] =
		L"Global\\xucRunBrokerStatus.LanguageChanged.";

	struct ElevatedLaunchHelperResult
	{
		std::uint32_t magic = ElevatedLaunchHelperResultMagic;
		std::uint32_t windowsError = 0;
		std::uint32_t processId = 0;
		std::uint64_t processHandle = 0;
	};

	enum class ElevationMode : std::uint8_t
	{
		PreserveToken,
		RequireElevated
	};

	enum class LaunchVisibility : std::uint8_t
	{
		Normal,
		Hidden,
		HiddenMonitor
	};

	enum class LaunchStatus : std::uint8_t
	{
		Succeeded,
		Denied,
		Cancelled,
		InvalidRequest,
		BrokerUnavailable,
		Failed
	};

	enum class CredentialMetadataStatus : std::uint8_t
	{
		Succeeded,
		InvalidRequest,
		Failed
	};

	enum class SessionTestStatus : std::uint8_t
	{
		Succeeded,
		InvalidSession,
		Failed
	};

	enum class CredentialStoreTestStatus : std::uint8_t
	{
		Succeeded,
		Failed
	};

	enum class CredentialSaveStatus : std::uint8_t
	{
		Succeeded,
		InvalidCredentials,
		InvalidRequest,
		Failed
	};

	enum class CredentialDeleteStatus : std::uint8_t
	{
		Succeeded,
		NotFound,
		InvalidRequest,
		Failed
	};

	enum class CredentialListStatus : std::uint8_t
	{
		Succeeded,
		InvalidRequest,
		Failed
	};

	enum class AdministrativeInventoryStatus : std::uint8_t
	{
		Succeeded,
		InvalidRequest,
		Failed
	};

	enum class AdministrativeDeleteKind : std::uint8_t
	{
		Credential = 1,
		Approval = 2
	};

	enum class AdministrativeDeleteStatus : std::uint8_t
	{
		Succeeded,
		NotFound,
		InvalidRequest,
		Failed
	};

	enum class BackupAction : std::uint8_t
	{
		Create = 1,
		List = 2,
		Restore = 3,
		DeleteAll = 4,
		Delete = 5
	};

	enum class BackupStatus : std::uint8_t
	{
		Succeeded,
		InvalidRequest,
		NotFound,
		Failed
	};

	enum class SecurityAuditStatus : std::uint8_t
	{
		Succeeded,
		InvalidRequest,
		Failed
	};

	enum class TrackedProcessRegistrationStatus : std::uint8_t
	{
		Succeeded,
		InvalidRequest,
		Failed
	};

	enum class BrokerStopStatus : std::uint8_t
	{
		Accepted,
		InvalidRequest,
		Failed
	};

	enum class BrokerServiceControlAction : std::uint8_t
	{
		Disable = 1
	};

	enum class CredentialUseTestStatus : std::uint8_t
	{
		Succeeded,
		NotFound,
		InvalidCredentials,
		InvalidRequest,
		Failed
	};

	enum class ApprovalChallengeStatus : std::uint8_t
	{
		Issued,
		InvalidRequest,
		Denied,
		Failed
	};

	enum class ApprovalDecision : std::uint8_t
	{
		ApproveOnce,
		Deny
	};

	enum class ApprovalHelperStartMode : std::uint8_t
	{
		None,
		BrokerStarted,
		ClientRunAsRequired
	};

	enum class ApprovalPersistence : std::uint8_t
	{
		Once,
		TenMinutes,
		OneDay,
		LogonSession,
		Forever
	};

	enum class ApprovalProgramScope : std::uint8_t
	{
		General,
		TargetProgram,
		ExactInvocation
	};

	enum class ApprovalAccountScope : std::uint8_t
	{
		CurrentAccount,
		AllAccounts
	};

	enum class ApprovalUpdateScope : std::uint8_t
	{
		None,
		CurrentAccount,
		AllAccounts
	};

	enum class ApprovalConfirmationStatus : std::uint8_t
	{
		Confirmed,
		Denied,
		Expired,
		InvalidRequest,
		Failed
	};

	enum class ApprovalFailureReason : std::uint8_t
	{
		None,
		ElevatedTargetWritable,
		ElevatedTargetDirectoryWritable,
		ElevatedPathValidationFailed,
		LaunchObjectChanged,
		LaunchObjectLockFailed,
		ChallengeGlobalLimitReached,
		ChallengeSidLimitReached,
		ChallengeSessionLimitReached,
		ChallengeProcessLimitReached,
		CrossAccountUacUnavailable
	};

	enum class ApprovalManagementStatus : std::uint8_t
	{
		Succeeded,
		InvalidRequest,
		Failed
	};

	enum class ContractValidationError : std::uint8_t
	{
		None,
		UnsupportedProtocolVersion,
		MissingRequestId,
		MissingAccountSid,
		AccountSidTooLong,
		MissingExecutablePath,
		ExecutablePathTooLong,
		ArgumentsTooLong,
		WorkingDirectoryTooLong,
		MissingPassword,
		PasswordTooLong,
		MissingChallengeId,
		InvalidApprovalDecision,
		InvalidElevationMode,
		InvalidLaunchVisibility,
		MissingProcessHandle,
		MissingProcessId,
		InvalidUserInterfaceLanguage,
		InvalidServiceControlAction,
		InvalidBackupRequest,
		InvalidAdministrativeDeleteKind,
		InvalidProductVersion
	};

	enum class ResultValidationError : std::uint8_t
	{
		None,
		UnsupportedProtocolVersion,
		MissingRequestId,
		InvalidStatus
	};

	using RequestNonce = std::array<std::uint8_t, 16>;

	struct HealthRequest
	{
		std::uint32_t protocolVersion = ProtocolVersion;
		std::uint64_t requestId = 0;
		RequestNonce nonce = {};
	};

	struct ProductVersion
	{
		std::uint32_t major = 0;
		std::uint32_t minor = 0;
		std::uint32_t patch = 0;
		std::uint32_t build = 0;
	};

	struct HealthResult
	{
		std::uint32_t protocolVersion = ProtocolVersion;
		std::uint64_t requestId = 0;
		RequestNonce nonce = {};
		std::uint32_t brokerProcessId = 0;
		std::uint32_t elevatedProcessCount = 0;
		ProductVersion brokerVersion;
	};

	struct HealthAcknowledgement
	{
		std::uint32_t protocolVersion = ProtocolVersion;
		std::uint64_t requestId = 0;
		RequestNonce nonce = {};
	};

	struct LaunchRequest
	{
		std::uint32_t protocolVersion = ProtocolVersion;
		std::uint64_t requestId = 0;
		std::wstring requestedAccountSid;
		std::wstring executablePath;
		std::wstring argumentTail;
		std::wstring workingDirectory;
		ElevationMode elevationMode = ElevationMode::PreserveToken;
		LaunchVisibility visibility = LaunchVisibility::Normal;
	};

	struct LaunchResult
	{
		std::uint32_t protocolVersion = ProtocolVersion;
		std::uint64_t requestId = 0;
		LaunchStatus status = LaunchStatus::Failed;
		std::uint32_t windowsError = 0;
	};

	struct CredentialMetadataRequest
	{
		std::uint32_t protocolVersion = ProtocolVersion;
		std::uint64_t requestId = 0;
		std::wstring requestedAccountSid;
	};

	struct CredentialMetadataResult
	{
		std::uint32_t protocolVersion = ProtocolVersion;
		std::uint64_t requestId = 0;
		CredentialMetadataStatus status = CredentialMetadataStatus::Failed;
		std::uint32_t windowsError = 0;
	};

	struct SessionTestRequest
	{
		std::uint32_t protocolVersion = ProtocolVersion;
		std::uint64_t requestId = 0;
	};

	struct SessionTestResult
	{
		std::uint32_t protocolVersion = ProtocolVersion;
		std::uint64_t requestId = 0;
		SessionTestStatus status = SessionTestStatus::Failed;
		std::uint32_t windowsError = 0;
		std::uint32_t processId = 0;
	};

	struct CredentialStoreTestRequest
	{
		std::uint32_t protocolVersion = ProtocolVersion;
		std::uint64_t requestId = 0;
	};

	struct CredentialStoreTestResult
	{
		std::uint32_t protocolVersion = ProtocolVersion;
		std::uint64_t requestId = 0;
		CredentialStoreTestStatus status = CredentialStoreTestStatus::Failed;
		std::uint32_t windowsError = 0;
	};

	struct CredentialSaveRequest
	{
		std::uint32_t protocolVersion = ProtocolVersion;
		std::uint64_t requestId = 0;
		std::wstring requestedAccountSid;
		xucrun::SensitiveString password;
	};

	struct CredentialSaveResult
	{
		std::uint32_t protocolVersion = ProtocolVersion;
		std::uint64_t requestId = 0;
		CredentialSaveStatus status = CredentialSaveStatus::Failed;
		std::uint32_t windowsError = 0;
	};

	struct CredentialCaptureChallengeRequest
	{
		std::uint32_t protocolVersion = ProtocolVersion;
		std::uint64_t requestId = 0;
		std::wstring requestedAccountSid;
		std::wstring approvalAccountSid;
	};

	struct CredentialCaptureChallengeResult
	{
		std::uint32_t protocolVersion = ProtocolVersion;
		std::uint64_t requestId = 0;
		ApprovalChallengeStatus status = ApprovalChallengeStatus::Failed;
		std::uint32_t windowsError = 0;
		RequestNonce challengeId = {};
		std::wstring canonicalClientName;
		std::wstring canonicalAccountName;
		ApprovalHelperStartMode helperStartMode = ApprovalHelperStartMode::None;
		std::uint64_t approvalHelperProcessHandle = 0;
	};

	struct CredentialCaptureHelperRequest
	{
		std::uint32_t protocolVersion = ProtocolVersion;
		std::uint64_t requestId = 0;
		RequestNonce challengeId = {};
	};

	struct CredentialCaptureHelperResult
	{
		std::uint32_t protocolVersion = ProtocolVersion;
		std::uint64_t requestId = 0;
		ApprovalChallengeStatus status = ApprovalChallengeStatus::Failed;
		std::uint32_t windowsError = 0;
		std::wstring canonicalClientName;
		std::wstring canonicalAccountName;
		std::wstring authenticityMarker;
	};

	struct CredentialCaptureSubmitRequest
	{
		std::uint32_t protocolVersion = ProtocolVersion;
		std::uint64_t requestId = 0;
		RequestNonce challengeId = {};
		bool cancelled = false;
		std::wstring enteredAccountName;
		xucrun::SensitiveString password;
	};

	struct CredentialCaptureSubmitResult
	{
		std::uint32_t protocolVersion = ProtocolVersion;
		std::uint64_t requestId = 0;
		CredentialSaveStatus status = CredentialSaveStatus::Failed;
		std::uint32_t windowsError = 0;
	};

	struct CredentialCaptureCommitRequest
	{
		std::uint32_t protocolVersion = ProtocolVersion;
		std::uint64_t requestId = 0;
		RequestNonce challengeId = {};
	};

	using CredentialCaptureCommitResult = CredentialCaptureSubmitResult;

	struct CredentialDeleteRequest
	{
		std::uint32_t protocolVersion = ProtocolVersion;
		std::uint64_t requestId = 0;
		std::wstring requestedAccountSid;
	};

	struct CredentialDeleteResult
	{
		std::uint32_t protocolVersion = ProtocolVersion;
		std::uint64_t requestId = 0;
		CredentialDeleteStatus status = CredentialDeleteStatus::Failed;
		std::uint32_t windowsError = 0;
	};

	struct CredentialListRequest
	{
		std::uint32_t protocolVersion = ProtocolVersion;
		std::uint64_t requestId = 0;
	};

	struct CredentialListEntry
	{
		std::wstring accountSid;
		std::wstring canonicalAccountName;
		std::uint64_t lastWritten = 0;
	};

	struct CredentialListResult
	{
		std::uint32_t protocolVersion = ProtocolVersion;
		std::uint64_t requestId = 0;
		CredentialListStatus status = CredentialListStatus::Failed;
		std::uint32_t windowsError = 0;
		std::vector<CredentialListEntry> entries;
	};

	struct AdministrativeCredentialListEntry
	{
		std::wstring ownerUserSid;
		std::wstring accountSid;
		std::wstring canonicalAccountName;
		std::uint64_t lastWritten = 0;
	};

	struct AdministrativeInventoryRequest
	{
		std::uint32_t protocolVersion = ProtocolVersion;
		std::uint64_t requestId = 0;
	};

	struct AdministrativeInventoryResult
	{
		std::uint32_t protocolVersion = ProtocolVersion;
		std::uint64_t requestId = 0;
		AdministrativeInventoryStatus status =
			AdministrativeInventoryStatus::Failed;
		std::uint32_t windowsError = 0;
		std::uint32_t authenticityMarkerCount = 0;
		std::vector<AdministrativeCredentialListEntry> credentials;
		std::vector<xucrun::approval::BrokerApprovalValue> approvals;
	};

	struct AdministrativeDeleteRequest
	{
		std::uint32_t protocolVersion = ProtocolVersion;
		std::uint64_t requestId = 0;
		AdministrativeDeleteKind kind = AdministrativeDeleteKind::Credential;
		std::wstring ownerUserSid;
		std::wstring accountSid;
		xucrun::approval::BrokerApprovalValue approval;
	};

	struct AdministrativeDeleteResult
	{
		std::uint32_t protocolVersion = ProtocolVersion;
		std::uint64_t requestId = 0;
		AdministrativeDeleteStatus status = AdministrativeDeleteStatus::Failed;
		std::uint32_t windowsError = 0;
	};

	struct BackupEntry
	{
		std::wstring id;
		std::wstring description;
		std::uint64_t createdAt = 0;
		std::uint32_t credentialCount = 0;
		std::uint32_t approvalCount = 0;
		std::uint32_t authenticityMarkerCount = 0;
		std::uint32_t metadataCount = 0;
	};

	struct BackupRequest
	{
		std::uint32_t protocolVersion = ProtocolVersion;
		std::uint64_t requestId = 0;
		BackupAction action = BackupAction::List;
		bool createFallbackBackup = false;
		std::wstring backupId;
		std::wstring description;
	};

	struct BackupResult
	{
		std::uint32_t protocolVersion = ProtocolVersion;
		std::uint64_t requestId = 0;
		BackupStatus status = BackupStatus::Failed;
		std::uint32_t windowsError = 0;
		std::wstring fallbackBackupId;
		std::vector<BackupEntry> entries;
	};

	bool IsValidBackupId(const std::wstring& id) noexcept;
	bool BuildAdministrativeBackupCreateCommand(const std::wstring& description,
		std::wstring& command);
	bool ParseAdministrativeBackupCreateCommand(const std::wstring& command,
		std::wstring& description);
	bool IsValidAdministrativeBackupCommand(
		const std::wstring& command) noexcept;
	bool ParseAdministrativeBackupCommandArgument(const std::wstring& argumentTail,
		std::wstring& pipeToken, std::wstring& command) noexcept;
	bool ParseSetLanguageCommandArgument(const std::wstring& argumentTail,
		std::wstring& language, std::wstring& pipeToken) noexcept;

	struct TrackedProcessListRequest
	{
		std::uint32_t protocolVersion = ProtocolVersion;
		std::uint64_t requestId = 0;
	};

	struct TrackedProcessRegistrationRequest
	{
		std::uint32_t protocolVersion = ProtocolVersion;
		std::uint64_t requestId = 0;
		std::uint64_t processHandle = 0;
		std::uint32_t processId = 0;
	};

	struct TrackedProcessRegistrationResult
	{
		std::uint32_t protocolVersion = ProtocolVersion;
		std::uint64_t requestId = 0;
		TrackedProcessRegistrationStatus status =
			TrackedProcessRegistrationStatus::Failed;
		std::uint32_t windowsError = 0;
	};

	struct TrackedProcessListEntry
	{
		RequestNonce trackingId = {};
		std::uint32_t processId = 0;
		std::wstring processBaseName;
	};

	struct TrackedProcessListResult
	{
		std::uint32_t protocolVersion = ProtocolVersion;
		std::uint64_t requestId = 0;
		CredentialListStatus status = CredentialListStatus::Failed;
		std::uint32_t windowsError = 0;
		std::vector<TrackedProcessListEntry> entries;
	};

	struct StatusIconListRequest
	{
		std::uint32_t protocolVersion = ProtocolVersion;
		std::uint64_t requestId = 0;
		ProductVersion clientVersion;
		bool includeAllSessions = false;
	};

	struct StatusIconListEntry
	{
		std::uint32_t processId = 0;
		bool elevated = false;
		ProductVersion clientVersion;
		std::wstring accountSid;
		std::wstring canonicalAccountName;
	};

	struct StatusIconListResult
	{
		std::uint32_t protocolVersion = ProtocolVersion;
		std::uint64_t requestId = 0;
		CredentialListStatus status = CredentialListStatus::Failed;
		std::uint32_t windowsError = 0;
		std::vector<StatusIconListEntry> entries;
	};

	struct BrokerPolicyRejection
	{
		std::uint32_t protocolVersion = ProtocolVersion;
		std::uint64_t requestId = 0;
		std::uint32_t windowsError = PolicyDeniedWindowsError;
	};

	struct SecurityAuditRequest
	{
		std::uint32_t protocolVersion = ProtocolVersion;
		std::uint64_t requestId = 0;
		enum class Language : std::uint8_t
		{
			English,
			German
		};
		Language language = Language::English;
	};

	struct SecurityAuditResult
	{
		std::uint32_t protocolVersion = ProtocolVersion;
		std::uint64_t requestId = 0;
		SecurityAuditStatus status = SecurityAuditStatus::Failed;
		std::uint32_t windowsError = 0;
		std::uint32_t findingCount = 0;
		std::wstring report;
	};

	struct BrokerStopRequest
	{
		std::uint32_t protocolVersion = ProtocolVersion;
		std::uint64_t requestId = 0;
		BrokerServiceControlAction action = BrokerServiceControlAction::Disable;
	};

	struct BrokerStopResult
	{
		std::uint32_t protocolVersion = ProtocolVersion;
		std::uint64_t requestId = 0;
		BrokerStopStatus status = BrokerStopStatus::Failed;
		std::uint32_t windowsError = 0;
	};

	struct CredentialUseTestRequest
	{
		std::uint32_t protocolVersion = ProtocolVersion;
		std::uint64_t requestId = 0;
		std::wstring requestedAccountSid;
	};

	struct CredentialUseTestResult
	{
		std::uint32_t protocolVersion = ProtocolVersion;
		std::uint64_t requestId = 0;
		CredentialUseTestStatus status = CredentialUseTestStatus::Failed;
		std::uint32_t windowsError = 0;
		std::uint32_t processId = 0;
	};

	struct ApprovalChallengeRequest
	{
		std::uint32_t protocolVersion = ProtocolVersion;
		std::uint64_t requestId = 0;
		std::wstring requestedAccountSid;
		std::wstring approvalAccountSid;
		std::wstring executablePath;
		std::wstring argumentTail;
		std::wstring workingDirectory;
		ElevationMode elevationMode = ElevationMode::PreserveToken;
		LaunchVisibility visibility = LaunchVisibility::Normal;
	};

	struct ApprovalChallengeResult
	{
		std::uint32_t protocolVersion = ProtocolVersion;
		std::uint64_t requestId = 0;
		ApprovalChallengeStatus status = ApprovalChallengeStatus::Failed;
		std::uint32_t windowsError = 0;
		ApprovalFailureReason failureReason = ApprovalFailureReason::None;
		RequestNonce challengeId = {};
		std::wstring canonicalClientName;
		std::wstring canonicalAccountName;
		std::wstring clientPath;
		std::array<std::uint8_t, 32> clientSha256 = {};
		std::int32_t clientTrustStatus = static_cast<std::int32_t>(0x800B0100u);
		bool clientSignatureTrusted = false;
		std::wstring finalExecutablePath;
		std::array<std::uint8_t, 32> executableSha256 = {};
		std::int32_t executableTrustStatus = static_cast<std::int32_t>(0x800B0100u);
		bool executableSignatureTrusted = false;
		std::wstring argumentTail;
		std::wstring workingDirectory;
		std::wstring directScriptPath;
		std::array<std::uint8_t, 32> directScriptSha256 = {};
		ElevationMode elevationMode = ElevationMode::PreserveToken;
		LaunchVisibility visibility = LaunchVisibility::Normal;
		bool userConfirmationRequired = true;
		bool programUpdateAvailable = false;
		ApprovalHelperStartMode helperStartMode = ApprovalHelperStartMode::None;
		bool approvalHelperView = false;
		bool storedApprovalPresenceConfirmation = false;
		ApprovalFailureReason writableElevatedTargetReason = ApprovalFailureReason::None;
		std::uint64_t approvalHelperProcessHandle = 0;
		std::wstring changedProgramPath;
		std::array<std::uint8_t, 32> oldProgramSha256 = {};
		std::array<std::uint8_t, 32> newProgramSha256 = {};
		std::uint32_t affectedApprovalCount = 0;
		std::uint32_t affectedAccountCount = 0;
		std::wstring authenticityMarker;
	};

	struct ApprovalConfirmationRequest
	{
		std::uint32_t protocolVersion = ProtocolVersion;
		std::uint64_t requestId = 0;
		RequestNonce challengeId = {};
		ApprovalDecision decision = ApprovalDecision::Deny;
		ApprovalPersistence persistence = ApprovalPersistence::Once;
		ApprovalProgramScope programScope = ApprovalProgramScope::General;
		ApprovalAccountScope accountScope = ApprovalAccountScope::CurrentAccount;
		ApprovalUpdateScope updateScope = ApprovalUpdateScope::None;
	};

	struct ApprovalConfirmationResult
	{
		std::uint32_t protocolVersion = ProtocolVersion;
		std::uint64_t requestId = 0;
		ApprovalConfirmationStatus status = ApprovalConfirmationStatus::Failed;
		std::uint32_t windowsError = 0;
		ApprovalFailureReason failureReason = ApprovalFailureReason::None;
		std::uint32_t processId = 0;
	};

	struct ApprovalHelperRequest
	{
		std::uint32_t protocolVersion = ProtocolVersion;
		std::uint64_t requestId = 0;
		RequestNonce challengeId = {};
	};

	struct ApprovalHelperDecisionRequest
	{
		std::uint32_t protocolVersion = ProtocolVersion;
		std::uint64_t requestId = 0;
		RequestNonce challengeId = {};
		ApprovalDecision decision = ApprovalDecision::Deny;
		ApprovalPersistence persistence = ApprovalPersistence::Once;
		ApprovalProgramScope programScope = ApprovalProgramScope::TargetProgram;
		ApprovalAccountScope accountScope = ApprovalAccountScope::CurrentAccount;
		ApprovalUpdateScope updateScope = ApprovalUpdateScope::None;
		bool broadInterpreterApprovalConfirmed = false;
	};

	struct ApprovalHelperDecisionResult
	{
		std::uint32_t protocolVersion = ProtocolVersion;
		std::uint64_t requestId = 0;
		ApprovalConfirmationStatus status = ApprovalConfirmationStatus::Failed;
		std::uint32_t windowsError = 0;
	};

	struct ApprovalListRequest
	{
		std::uint32_t protocolVersion = ProtocolVersion;
		std::uint64_t requestId = 0;
	};

	struct ApprovalListResult
	{
		std::uint32_t protocolVersion = ProtocolVersion;
		std::uint64_t requestId = 0;
		ApprovalManagementStatus status = ApprovalManagementStatus::Failed;
		std::uint32_t windowsError = 0;
		std::vector<xucrun::approval::BrokerApprovalValue> entries;
	};

	struct ApprovalResetRequest
	{
		std::uint32_t protocolVersion = ProtocolVersion;
		std::uint64_t requestId = 0;
		xucrun::approval::BrokerApprovalValue approval;
	};

	struct ApprovalResetResult
	{
		std::uint32_t protocolVersion = ProtocolVersion;
		std::uint64_t requestId = 0;
		ApprovalManagementStatus status = ApprovalManagementStatus::Failed;
		std::uint32_t windowsError = 0;
		std::uint32_t deletedCount = 0;
	};

	LaunchRequest BuildLaunchRequest(
		std::uint64_t requestId,
		const std::wstring& requestedAccountSid,
		const std::wstring& executablePath,
		const std::wstring& argumentTail,
		const std::wstring& workingDirectory,
		ElevationMode elevationMode);
	ContractValidationError ValidateLaunchRequest(const LaunchRequest& request) noexcept;
	ResultValidationError ValidateLaunchResult(const LaunchResult& result) noexcept;
	ContractValidationError ValidateCredentialMetadataRequest(
		const CredentialMetadataRequest& request) noexcept;
	ResultValidationError ValidateCredentialMetadataResult(
		const CredentialMetadataResult& result) noexcept;
	ContractValidationError ValidateCredentialSaveRequest(
		const CredentialSaveRequest& request) noexcept;
	ResultValidationError ValidateCredentialSaveResult(
		const CredentialSaveResult& result) noexcept;
	ContractValidationError ValidateCredentialCaptureChallengeRequest(
		const CredentialCaptureChallengeRequest& request) noexcept;
	ResultValidationError ValidateCredentialCaptureChallengeResult(
		const CredentialCaptureChallengeResult& result) noexcept;
	ContractValidationError ValidateCredentialCaptureHelperRequest(
		const CredentialCaptureHelperRequest& request) noexcept;
	ResultValidationError ValidateCredentialCaptureHelperResult(
		const CredentialCaptureHelperResult& result) noexcept;
	ContractValidationError ValidateCredentialCaptureSubmitRequest(
		const CredentialCaptureSubmitRequest& request) noexcept;
	ResultValidationError ValidateCredentialCaptureSubmitResult(
		const CredentialCaptureSubmitResult& result) noexcept;
	ContractValidationError ValidateCredentialCaptureCommitRequest(
		const CredentialCaptureCommitRequest& request) noexcept;
	ResultValidationError ValidateCredentialCaptureCommitResult(
		const CredentialCaptureCommitResult& result) noexcept;
	ContractValidationError ValidateCredentialDeleteRequest(
		const CredentialDeleteRequest& request) noexcept;
	ResultValidationError ValidateCredentialDeleteResult(
		const CredentialDeleteResult& result) noexcept;
	ContractValidationError ValidateCredentialListRequest(
		const CredentialListRequest& request) noexcept;
	ResultValidationError ValidateCredentialListResult(
		const CredentialListResult& result) noexcept;
	ContractValidationError ValidateAdministrativeInventoryRequest(
		const AdministrativeInventoryRequest& request) noexcept;
	ResultValidationError ValidateAdministrativeInventoryResult(
		const AdministrativeInventoryResult& result) noexcept;
	ContractValidationError ValidateAdministrativeDeleteRequest(
		const AdministrativeDeleteRequest& request) noexcept;
	ResultValidationError ValidateAdministrativeDeleteResult(
		const AdministrativeDeleteResult& result) noexcept;
	ContractValidationError ValidateBackupRequest(
		const BackupRequest& request) noexcept;
	ResultValidationError ValidateBackupResult(
		const BackupResult& result) noexcept;
	ContractValidationError ValidateTrackedProcessListRequest(
		const TrackedProcessListRequest& request) noexcept;
	ResultValidationError ValidateTrackedProcessListResult(
		const TrackedProcessListResult& result) noexcept;
	ContractValidationError ValidateStatusIconListRequest(
		const StatusIconListRequest& request) noexcept;
	ResultValidationError ValidateStatusIconListResult(
		const StatusIconListResult& result) noexcept;
	ContractValidationError ValidateTrackedProcessRegistrationRequest(
		const TrackedProcessRegistrationRequest& request) noexcept;
	ResultValidationError ValidateTrackedProcessRegistrationResult(
		const TrackedProcessRegistrationResult& result) noexcept;
	ContractValidationError ValidateSecurityAuditRequest(
		const SecurityAuditRequest& request) noexcept;
	ResultValidationError ValidateSecurityAuditResult(
		const SecurityAuditResult& result) noexcept;
	ContractValidationError ValidateBrokerStopRequest(
		const BrokerStopRequest& request) noexcept;
	ResultValidationError ValidateBrokerStopResult(
		const BrokerStopResult& result) noexcept;
	ContractValidationError ValidateCredentialUseTestRequest(
		const CredentialUseTestRequest& request) noexcept;
	ResultValidationError ValidateCredentialUseTestResult(
		const CredentialUseTestResult& result) noexcept;
	ContractValidationError ValidateApprovalChallengeRequest(
		const ApprovalChallengeRequest& request) noexcept;
	ResultValidationError ValidateApprovalChallengeResult(
		const ApprovalChallengeResult& result) noexcept;
	ContractValidationError ValidateApprovalConfirmationRequest(
		const ApprovalConfirmationRequest& request) noexcept;
	ResultValidationError ValidateApprovalConfirmationResult(
		const ApprovalConfirmationResult& result) noexcept;
	ContractValidationError ValidateApprovalHelperRequest(
		const ApprovalHelperRequest& request) noexcept;
	ContractValidationError ValidateApprovalHelperDecisionRequest(
		const ApprovalHelperDecisionRequest& request) noexcept;
	ResultValidationError ValidateApprovalHelperDecisionResult(
		const ApprovalHelperDecisionResult& result) noexcept;
	ContractValidationError ValidateApprovalListRequest(
		const ApprovalListRequest& request) noexcept;
	ResultValidationError ValidateApprovalListResult(
		const ApprovalListResult& result) noexcept;
	ContractValidationError ValidateApprovalResetRequest(
		const ApprovalResetRequest& request) noexcept;
	ResultValidationError ValidateApprovalResetResult(
		const ApprovalResetResult& result) noexcept;
}