// Purpose: Declares the binary representation of all broker protocol messages.
// Defines fixed frame sizes and size-limited serialization functions.
#pragma once

#include "BrokerContracts.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace xucrun::broker
{
	inline constexpr std::size_t LaunchRequestHeaderSize = 36;
	inline constexpr std::size_t LaunchResultSize = 24;
	inline constexpr std::size_t SystemLaunchRequestHeaderSize = 32;
	inline constexpr std::size_t SystemLaunchResultSize = 28;
	inline constexpr std::size_t HealthRequestSize = 36;
	inline constexpr std::size_t HealthResultSize = 60;
	inline constexpr std::size_t HealthAcknowledgementSize = 36;
	inline constexpr std::size_t CredentialMetadataRequestHeaderSize = 24;
	inline constexpr std::size_t CredentialMetadataResultSize = 24;
	inline constexpr std::size_t SessionTestRequestSize = 20;
	inline constexpr std::size_t SessionTestResultSize = 28;
	inline constexpr std::size_t CredentialStoreTestRequestSize = 20;
	inline constexpr std::size_t CredentialStoreTestResultSize = 24;
	inline constexpr std::size_t CredentialSaveRequestHeaderSize = 28;
	inline constexpr std::size_t CredentialSaveResultSize = 24;
	inline constexpr std::size_t CredentialCaptureChallengeRequestHeaderSize = 28;
	inline constexpr std::size_t CredentialCaptureChallengeResultHeaderSize = 64;
	inline constexpr std::size_t CredentialCaptureHelperRequestSize = 36;
	inline constexpr std::size_t CredentialCaptureHelperResultHeaderSize = 36;
	inline constexpr std::size_t CredentialCaptureSubmitRequestHeaderSize = 44;
	inline constexpr std::size_t CredentialCaptureSubmitResultSize = 24;
	inline constexpr std::size_t CredentialCaptureCommitRequestSize = 36;
	inline constexpr std::size_t CredentialCaptureCommitResultSize = 24;
	inline constexpr std::size_t CredentialDeleteRequestHeaderSize = 24;
	inline constexpr std::size_t CredentialDeleteResultSize = 24;
	inline constexpr std::size_t CredentialListRequestSize = 20;
	inline constexpr std::size_t CredentialListResultHeaderSize = 28;
	inline constexpr std::size_t CredentialListEntryHeaderSize = 16;
	inline constexpr std::size_t AdministrativeInventoryRequestSize = 20;
	inline constexpr std::size_t AdministrativeInventoryResultHeaderSize = 36;
	inline constexpr std::size_t AdministrativeCredentialEntryHeaderSize = 20;
	inline constexpr std::size_t AdministrativeDeleteRequestHeaderSize = 36;
	inline constexpr std::size_t AdministrativeDeleteResultSize = 24;
	inline constexpr std::size_t BackupRequestHeaderSize = 28;
	inline constexpr std::size_t BackupResultHeaderSize = 36;
	inline constexpr std::size_t BackupEntryHeaderSize = 32;
	inline constexpr std::size_t TrackedProcessListRequestSize = 20;
	inline constexpr std::size_t TrackedProcessListResultHeaderSize = 28;
	inline constexpr std::size_t TrackedProcessListEntryHeaderSize = 24;
	inline constexpr std::size_t TrackedProcessRegistrationRequestSize = 32;
	inline constexpr std::size_t TrackedProcessRegistrationResultSize = 24;
	inline constexpr std::size_t StatusIconListRequestSize = 40;
	inline constexpr std::size_t StatusIconListResultHeaderSize = 28;
	inline constexpr std::size_t StatusIconListEntryHeaderSize = 32;
	inline constexpr std::size_t BrokerPolicyRejectionSize = 24;
	inline constexpr std::size_t SecurityAuditRequestSize = 20;
	inline constexpr std::size_t SecurityAuditResultHeaderSize = 32;
	inline constexpr std::size_t BrokerStopRequestSize = 20;
	inline constexpr std::size_t BrokerStopResultSize = 24;
	inline constexpr std::size_t CredentialUseTestRequestHeaderSize = 24;
	inline constexpr std::size_t CredentialUseTestResultSize = 28;
	inline constexpr std::size_t ApprovalChallengeRequestHeaderSize = 40;
	inline constexpr std::size_t ApprovalChallengeResultHeaderSize = 264;
	inline constexpr std::size_t ApprovalConfirmationRequestSize = 40;
	inline constexpr std::size_t ApprovalConfirmationResultSize = 28;
	inline constexpr std::size_t ApprovalHelperRequestSize = 36;
	inline constexpr std::size_t ApprovalHelperLeaseRequestSize = 36;
	inline constexpr std::size_t ApprovalHelperLeaseResultSize = 24;
	inline constexpr std::size_t ApprovalHelperDecisionRequestSize = 40;
	inline constexpr std::size_t ApprovalHelperDecisionResultSize = 24;
	inline constexpr std::size_t ApprovalListRequestSize = 20;
	inline constexpr std::size_t ApprovalListResultHeaderSize = 28;
	inline constexpr std::size_t ApprovalResetRequestHeaderSize = 24;
	inline constexpr std::size_t ApprovalResetResultSize = 28;
	inline constexpr std::size_t MaximumApprovalEntrySize = 4096;
	inline constexpr std::size_t MaximumApprovalEntries =
		MaximumAdministrativeApprovalEntries;
	inline constexpr std::size_t MaximumApprovalListResultSize =
		ApprovalListResultHeaderSize + MaximumApprovalEntries *
		(sizeof(std::uint32_t) + MaximumApprovalEntrySize);
	inline constexpr std::size_t MaximumApprovalResetRequestSize =
		ApprovalResetRequestHeaderSize + MaximumApprovalEntrySize;
	inline constexpr std::size_t MaximumCredentialCaptureChallengeResultSize =
		CredentialCaptureChallengeResultHeaderSize + 4 * MaximumCanonicalAccountNameLength;
	inline constexpr std::size_t MaximumCredentialCaptureHelperResultSize =
		CredentialCaptureHelperResultHeaderSize + 4 * MaximumCanonicalAccountNameLength +
		2 * MaximumAuthenticityMarkerLength;
	inline constexpr std::size_t MaximumCredentialCaptureSubmitRequestSize =
		CredentialCaptureSubmitRequestHeaderSize + 2 *
		(MaximumCanonicalAccountNameLength + MaximumPasswordLength);
	inline constexpr std::size_t MaximumLaunchRequestSize = LaunchRequestHeaderSize +
		2 * (MaximumAccountSidLength + MaximumPathLength +
			MaximumArgumentLength + MaximumPathLength);
	inline constexpr std::size_t MaximumLaunchResultSize = LaunchResultSize;
	inline constexpr std::size_t MaximumSystemLaunchRequestSize =
		SystemLaunchRequestHeaderSize + 2 *
		(MaximumPathLength + MaximumArgumentLength + MaximumPathLength);
	inline constexpr std::size_t MaximumSystemLaunchResultSize = SystemLaunchResultSize;
	inline constexpr std::size_t MaximumApprovalChallengeRequestSize =
		ApprovalChallengeRequestHeaderSize + 2 * (2 * MaximumAccountSidLength +
			MaximumPathLength + MaximumArgumentLength + MaximumPathLength);
	inline constexpr std::size_t MaximumApprovalChallengeResultSize =
		ApprovalChallengeResultHeaderSize + 2 * (MaximumCanonicalAccountNameLength +
			MaximumCanonicalAccountNameLength + MaximumPathLength + MaximumPathLength +
			MaximumArgumentLength + MaximumPathLength + MaximumPathLength +
			MaximumAuthenticityMarkerLength);
	inline constexpr std::size_t MaximumCredentialListResultSize =
		CredentialListResultHeaderSize + MaximumCredentialListEntries *
		(CredentialListEntryHeaderSize + 2 *
			(MaximumAccountSidLength + MaximumCanonicalAccountNameLength));
	inline constexpr std::size_t MaximumAdministrativeInventoryResultSize =
		AdministrativeInventoryResultHeaderSize +
		MaximumAdministrativeCredentialEntries *
		(AdministrativeCredentialEntryHeaderSize + 2 *
			(2 * MaximumAccountSidLength + MaximumCanonicalAccountNameLength)) +
		MaximumApprovalEntries * (sizeof(std::uint32_t) + MaximumApprovalEntrySize);
	inline constexpr std::size_t MaximumAdministrativeDeleteRequestSize =
		AdministrativeDeleteRequestHeaderSize +
		2 * (2 * MaximumAccountSidLength) + MaximumApprovalEntrySize;
	inline constexpr std::size_t MaximumBackupRequestSize =
		BackupRequestHeaderSize + 2 *
		(BackupIdLength + xucrun::backup::MaximumDescriptionLength);
	inline constexpr std::size_t MaximumBackupResultSize = BackupResultHeaderSize +
		2 * BackupIdLength + MaximumBackupEntries *
		(BackupEntryHeaderSize + 2 *
			(BackupIdLength + xucrun::backup::MaximumDescriptionLength));
	inline constexpr std::size_t MaximumTrackedProcessListResultSize =
		TrackedProcessListResultHeaderSize + MaximumTrackedProcessEntries *
		(TrackedProcessListEntryHeaderSize + 2 * MaximumProcessBaseNameLength);
	inline constexpr std::size_t MaximumStatusIconListResultSize =
		StatusIconListResultHeaderSize + MaximumStatusIconEntries *
		(StatusIconListEntryHeaderSize + 2 *
			(MaximumAccountSidLength + MaximumCanonicalAccountNameLength));
	inline constexpr std::size_t MaximumSecurityAuditResultSize =
		SecurityAuditResultHeaderSize + 2 * MaximumSecurityAuditReportLength;

	std::vector<std::uint8_t> SerializeLaunchRequest(const LaunchRequest& request);
	bool DeserializeLaunchRequest(
		std::span<const std::uint8_t> frame,
		LaunchRequest& request);
	std::vector<std::uint8_t> SerializeLaunchResult(const LaunchResult& result);
	bool DeserializeLaunchResult(
		std::span<const std::uint8_t> frame,
		LaunchResult& result);
	std::vector<std::uint8_t> SerializeSystemLaunchRequest(
		const SystemLaunchRequest& request);
	bool DeserializeSystemLaunchRequest(
		std::span<const std::uint8_t> frame,
		SystemLaunchRequest& request);
	std::vector<std::uint8_t> SerializeSystemLaunchResult(
		const SystemLaunchResult& result);
	bool DeserializeSystemLaunchResult(
		std::span<const std::uint8_t> frame,
		SystemLaunchResult& result);
	std::vector<std::uint8_t> SerializeHealthRequest(const HealthRequest& request);
	bool DeserializeHealthRequest(
		std::span<const std::uint8_t> frame,
		HealthRequest& request);
	std::vector<std::uint8_t> SerializeHealthResult(const HealthResult& result);
	bool DeserializeHealthResult(
		std::span<const std::uint8_t> frame,
		HealthResult& result);
	std::vector<std::uint8_t> SerializeHealthAcknowledgement(
		const HealthAcknowledgement& acknowledgement);
	bool DeserializeHealthAcknowledgement(
		std::span<const std::uint8_t> frame,
		HealthAcknowledgement& acknowledgement);
	std::vector<std::uint8_t> SerializeCredentialMetadataRequest(
		const CredentialMetadataRequest& request);
	bool DeserializeCredentialMetadataRequest(
		std::span<const std::uint8_t> frame,
		CredentialMetadataRequest& request);
	std::vector<std::uint8_t> SerializeCredentialMetadataResult(
		const CredentialMetadataResult& result);
	bool DeserializeCredentialMetadataResult(
		std::span<const std::uint8_t> frame,
		CredentialMetadataResult& result);
	std::vector<std::uint8_t> SerializeSessionTestRequest(
		const SessionTestRequest& request);
	bool DeserializeSessionTestRequest(
		std::span<const std::uint8_t> frame,
		SessionTestRequest& request);
	std::vector<std::uint8_t> SerializeSessionTestResult(
		const SessionTestResult& result);
	bool DeserializeSessionTestResult(
		std::span<const std::uint8_t> frame,
		SessionTestResult& result);
	std::vector<std::uint8_t> SerializeCredentialStoreTestRequest(
		const CredentialStoreTestRequest& request);
	bool DeserializeCredentialStoreTestRequest(
		std::span<const std::uint8_t> frame,
		CredentialStoreTestRequest& request);
	std::vector<std::uint8_t> SerializeCredentialStoreTestResult(
		const CredentialStoreTestResult& result);
	bool DeserializeCredentialStoreTestResult(
		std::span<const std::uint8_t> frame,
		CredentialStoreTestResult& result);
	std::vector<std::uint8_t> SerializeCredentialSaveRequest(
		const CredentialSaveRequest& request);
	bool DeserializeCredentialSaveRequest(
		std::span<const std::uint8_t> frame,
		CredentialSaveRequest& request);
	bool IsCredentialSaveRequestFrame(
		std::span<const std::uint8_t> frame) noexcept;
	std::vector<std::uint8_t> SerializeCredentialSaveResult(
		const CredentialSaveResult& result);
	bool DeserializeCredentialSaveResult(
		std::span<const std::uint8_t> frame,
		CredentialSaveResult& result);
	std::vector<std::uint8_t> SerializeCredentialCaptureChallengeRequest(
		const CredentialCaptureChallengeRequest& request);
	bool DeserializeCredentialCaptureChallengeRequest(
		std::span<const std::uint8_t> frame,
		CredentialCaptureChallengeRequest& request);
	bool IsCredentialCaptureChallengeRequestFrame(
		std::span<const std::uint8_t> frame) noexcept;
	std::vector<std::uint8_t> SerializeCredentialCaptureChallengeResult(
		const CredentialCaptureChallengeResult& result);
	bool DeserializeCredentialCaptureChallengeResult(
		std::span<const std::uint8_t> frame,
		CredentialCaptureChallengeResult& result);
	std::vector<std::uint8_t> SerializeCredentialCaptureHelperRequest(
		const CredentialCaptureHelperRequest& request);
	bool DeserializeCredentialCaptureHelperRequest(
		std::span<const std::uint8_t> frame,
		CredentialCaptureHelperRequest& request);
	bool IsCredentialCaptureHelperRequestFrame(
		std::span<const std::uint8_t> frame) noexcept;
	std::vector<std::uint8_t> SerializeCredentialCaptureHelperResult(
		const CredentialCaptureHelperResult& result);
	bool DeserializeCredentialCaptureHelperResult(
		std::span<const std::uint8_t> frame,
		CredentialCaptureHelperResult& result);
	std::vector<std::uint8_t> SerializeCredentialCaptureSubmitRequest(
		const CredentialCaptureSubmitRequest& request);
	bool DeserializeCredentialCaptureSubmitRequest(
		std::span<const std::uint8_t> frame,
		CredentialCaptureSubmitRequest& request);
	bool IsCredentialCaptureSubmitRequestFrame(
		std::span<const std::uint8_t> frame) noexcept;
	std::vector<std::uint8_t> SerializeCredentialCaptureSubmitResult(
		const CredentialCaptureSubmitResult& result);
	bool DeserializeCredentialCaptureSubmitResult(
		std::span<const std::uint8_t> frame,
		CredentialCaptureSubmitResult& result);
	std::vector<std::uint8_t> SerializeCredentialCaptureCommitRequest(
		const CredentialCaptureCommitRequest& request);
	bool DeserializeCredentialCaptureCommitRequest(
		std::span<const std::uint8_t> frame,
		CredentialCaptureCommitRequest& request);
	std::vector<std::uint8_t> SerializeCredentialCaptureCommitResult(
		const CredentialCaptureCommitResult& result);
	bool DeserializeCredentialCaptureCommitResult(
		std::span<const std::uint8_t> frame,
		CredentialCaptureCommitResult& result);
	std::vector<std::uint8_t> SerializeCredentialDeleteRequest(
		const CredentialDeleteRequest& request);
	bool DeserializeCredentialDeleteRequest(
		std::span<const std::uint8_t> frame,
		CredentialDeleteRequest& request);
	std::vector<std::uint8_t> SerializeCredentialDeleteResult(
		const CredentialDeleteResult& result);
	bool DeserializeCredentialDeleteResult(
		std::span<const std::uint8_t> frame,
		CredentialDeleteResult& result);
	std::vector<std::uint8_t> SerializeCredentialListRequest(
		const CredentialListRequest& request);
	bool DeserializeCredentialListRequest(
		std::span<const std::uint8_t> frame,
		CredentialListRequest& request);
	std::vector<std::uint8_t> SerializeCredentialListResult(
		const CredentialListResult& result);
	bool DeserializeCredentialListResult(
		std::span<const std::uint8_t> frame,
		CredentialListResult& result);
	std::vector<std::uint8_t> SerializeAdministrativeInventoryRequest(
		const AdministrativeInventoryRequest& request);
	bool DeserializeAdministrativeInventoryRequest(
		std::span<const std::uint8_t> frame,
		AdministrativeInventoryRequest& request);
	std::vector<std::uint8_t> SerializeAdministrativeInventoryResult(
		const AdministrativeInventoryResult& result);
	bool DeserializeAdministrativeInventoryResult(
		std::span<const std::uint8_t> frame,
		AdministrativeInventoryResult& result);
	std::vector<std::uint8_t> SerializeAdministrativeDeleteRequest(
		const AdministrativeDeleteRequest& request);
	bool DeserializeAdministrativeDeleteRequest(
		std::span<const std::uint8_t> frame,
		AdministrativeDeleteRequest& request);
	std::vector<std::uint8_t> SerializeAdministrativeDeleteResult(
		const AdministrativeDeleteResult& result);
	bool DeserializeAdministrativeDeleteResult(
		std::span<const std::uint8_t> frame,
		AdministrativeDeleteResult& result);
	std::vector<std::uint8_t> SerializeBackupRequest(const BackupRequest& request);
	bool DeserializeBackupRequest(
		std::span<const std::uint8_t> frame, BackupRequest& request);
	std::vector<std::uint8_t> SerializeBackupResult(const BackupResult& result);
	bool DeserializeBackupResult(
		std::span<const std::uint8_t> frame, BackupResult& result);
	std::vector<std::uint8_t> SerializeTrackedProcessListRequest(
		const TrackedProcessListRequest& request);
	bool DeserializeTrackedProcessListRequest(
		std::span<const std::uint8_t> frame,
		TrackedProcessListRequest& request);
	std::vector<std::uint8_t> SerializeTrackedProcessListResult(
		const TrackedProcessListResult& result);
	bool DeserializeTrackedProcessListResult(
		std::span<const std::uint8_t> frame,
		TrackedProcessListResult& result);
	std::vector<std::uint8_t> SerializeStatusIconListRequest(
		const StatusIconListRequest& request);
	bool DeserializeStatusIconListRequest(
		std::span<const std::uint8_t> frame, StatusIconListRequest& request);
	std::vector<std::uint8_t> SerializeStatusIconListResult(
		const StatusIconListResult& result);
	bool DeserializeStatusIconListResult(
		std::span<const std::uint8_t> frame, StatusIconListResult& result);
	std::vector<std::uint8_t> SerializeBrokerPolicyRejection(
		const BrokerPolicyRejection& rejection);
	bool DeserializeBrokerPolicyRejection(
		std::span<const std::uint8_t> frame, BrokerPolicyRejection& rejection);
	std::vector<std::uint8_t> SerializeTrackedProcessRegistrationRequest(
		const TrackedProcessRegistrationRequest& request);
	bool DeserializeTrackedProcessRegistrationRequest(
		std::span<const std::uint8_t> frame,
		TrackedProcessRegistrationRequest& request);
	std::vector<std::uint8_t> SerializeTrackedProcessRegistrationResult(
		const TrackedProcessRegistrationResult& result);
	bool DeserializeTrackedProcessRegistrationResult(
		std::span<const std::uint8_t> frame,
		TrackedProcessRegistrationResult& result);
	std::vector<std::uint8_t> SerializeSecurityAuditRequest(
		const SecurityAuditRequest& request);
	bool DeserializeSecurityAuditRequest(
		std::span<const std::uint8_t> frame,
		SecurityAuditRequest& request);
	std::vector<std::uint8_t> SerializeSecurityAuditResult(
		const SecurityAuditResult& result);
	bool DeserializeSecurityAuditResult(
		std::span<const std::uint8_t> frame,
		SecurityAuditResult& result);
	std::vector<std::uint8_t> SerializeBrokerStopRequest(
		const BrokerStopRequest& request);
	bool DeserializeBrokerStopRequest(
		std::span<const std::uint8_t> frame,
		BrokerStopRequest& request);
	std::vector<std::uint8_t> SerializeBrokerStopResult(
		const BrokerStopResult& result);
	bool DeserializeBrokerStopResult(
		std::span<const std::uint8_t> frame,
		BrokerStopResult& result);
	std::vector<std::uint8_t> SerializeCredentialUseTestRequest(
		const CredentialUseTestRequest& request);
	bool DeserializeCredentialUseTestRequest(
		std::span<const std::uint8_t> frame,
		CredentialUseTestRequest& request);
	std::vector<std::uint8_t> SerializeCredentialUseTestResult(
		const CredentialUseTestResult& result);
	bool DeserializeCredentialUseTestResult(
		std::span<const std::uint8_t> frame,
		CredentialUseTestResult& result);
	std::vector<std::uint8_t> SerializeApprovalChallengeRequest(
		const ApprovalChallengeRequest& request);
	bool DeserializeApprovalChallengeRequest(
		std::span<const std::uint8_t> frame,
		ApprovalChallengeRequest& request);
	std::vector<std::uint8_t> SerializeApprovalChallengeResult(
		const ApprovalChallengeResult& result);
	bool DeserializeApprovalChallengeResult(
		std::span<const std::uint8_t> frame,
		ApprovalChallengeResult& result);
	std::vector<std::uint8_t> SerializeApprovalConfirmationRequest(
		const ApprovalConfirmationRequest& request);
	bool DeserializeApprovalConfirmationRequest(
		std::span<const std::uint8_t> frame,
		ApprovalConfirmationRequest& request);
	std::vector<std::uint8_t> SerializeApprovalConfirmationResult(
		const ApprovalConfirmationResult& result);
	bool DeserializeApprovalConfirmationResult(
		std::span<const std::uint8_t> frame,
		ApprovalConfirmationResult& result);
	std::vector<std::uint8_t> SerializeApprovalHelperRequest(
		const ApprovalHelperRequest& request);
	bool DeserializeApprovalHelperRequest(
		std::span<const std::uint8_t> frame,
		ApprovalHelperRequest& request);
	std::vector<std::uint8_t> SerializeApprovalHelperLeaseRequest(
		const ApprovalHelperRequest& request);
	bool DeserializeApprovalHelperLeaseRequest(
		std::span<const std::uint8_t> frame,
		ApprovalHelperRequest& request);
	std::vector<std::uint8_t> SerializeApprovalHelperLeaseResult(
		const ApprovalHelperDecisionResult& result);
	bool DeserializeApprovalHelperLeaseResult(
		std::span<const std::uint8_t> frame,
		ApprovalHelperDecisionResult& result);
	std::vector<std::uint8_t> SerializeApprovalHelperDecisionRequest(
		const ApprovalHelperDecisionRequest& request);
	bool DeserializeApprovalHelperDecisionRequest(
		std::span<const std::uint8_t> frame,
		ApprovalHelperDecisionRequest& request);
	std::vector<std::uint8_t> SerializeApprovalHelperDecisionResult(
		const ApprovalHelperDecisionResult& result);
	bool DeserializeApprovalHelperDecisionResult(
		std::span<const std::uint8_t> frame,
		ApprovalHelperDecisionResult& result);
	std::vector<std::uint8_t> SerializeApprovalListRequest(
		const ApprovalListRequest& request);
	bool DeserializeApprovalListRequest(std::span<const std::uint8_t> frame,
		ApprovalListRequest& request);
	std::vector<std::uint8_t> SerializeApprovalListResult(
		const ApprovalListResult& result);
	bool DeserializeApprovalListResult(std::span<const std::uint8_t> frame,
		ApprovalListResult& result);
	std::vector<std::uint8_t> SerializeApprovalResetRequest(
		const ApprovalResetRequest& request);
	bool DeserializeApprovalResetRequest(std::span<const std::uint8_t> frame,
		ApprovalResetRequest& request);
	std::vector<std::uint8_t> SerializeApprovalResetResult(
		const ApprovalResetResult& result);
	bool DeserializeApprovalResetResult(std::span<const std::uint8_t> frame,
		ApprovalResetResult& result);
}