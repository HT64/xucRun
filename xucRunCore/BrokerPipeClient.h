// Purpose: Provides the client API for authenticated requests to the broker service.
// Approach: Exposes the declarations, constants, and types consumed by the components that provide this behavior.
// Encapsulates health, start, credential, approval, and process contracts.
#pragma once

#include "BrokerContracts.h"

#include <cstdint>
#include <string>

namespace xucrun::broker
{
	inline constexpr wchar_t ProductName[] = L"xucRun";
	inline constexpr wchar_t BrokerServiceName[] = L"xucRunBroker";
	inline const std::wstring BrokerServiceRegistryPath =
		std::wstring(L"SYSTEM\\CurrentControlSet\\Services\\") + BrokerServiceName;
	inline const std::wstring BrokerServiceParametersRegistryPath =
		BrokerServiceRegistryPath + L"\\Parameters";
	inline constexpr wchar_t BrokerPipeName[] = L"\\\\.\\pipe\\xucRunBroker.v1";

	enum class PipeHealthStatus : std::uint8_t
	{
		Succeeded,
		Unavailable,
		IoError,
		InvalidResponse
	};

	struct PipeHealthCheck
	{
		PipeHealthStatus status = PipeHealthStatus::Unavailable;
		std::uint32_t windowsError = 0;
		std::uint32_t brokerProcessId = 0;
		std::uint32_t elevatedProcessCount = 0;
		ProductVersion brokerVersion;
	};

	enum class PipeLaunchStatus : std::uint8_t
	{
		Succeeded,
		Unavailable,
		IoError,
		InvalidResponse
	};

	struct PipeLaunchResponse
	{
		PipeLaunchStatus status = PipeLaunchStatus::Unavailable;
		std::uint32_t windowsError = 0;
		LaunchResult result;
	};

	struct PipeSystemLaunchResponse
	{
		PipeLaunchStatus status = PipeLaunchStatus::Unavailable;
		std::uint32_t windowsError = 0;
		SystemLaunchResult result;
	};

	enum class PipeCredentialMetadataStatus : std::uint8_t
	{
		Succeeded,
		Unavailable,
		IoError,
		InvalidResponse
	};

	struct PipeCredentialMetadataResponse
	{
		PipeCredentialMetadataStatus status = PipeCredentialMetadataStatus::Unavailable;
		std::uint32_t windowsError = 0;
		CredentialMetadataResult result;
	};

	enum class PipeSessionTestStatus : std::uint8_t
	{
		Succeeded,
		Unavailable,
		IoError,
		InvalidResponse
	};

	struct PipeSessionTestResponse
	{
		PipeSessionTestStatus status = PipeSessionTestStatus::Unavailable;
		std::uint32_t windowsError = 0;
		SessionTestResult result;
	};

	enum class PipeCredentialStoreTestStatus : std::uint8_t
	{
		Succeeded,
		Unavailable,
		IoError,
		InvalidResponse
	};

	struct PipeCredentialStoreTestResponse
	{
		PipeCredentialStoreTestStatus status =
			PipeCredentialStoreTestStatus::Unavailable;
		std::uint32_t windowsError = 0;
		CredentialStoreTestResult result;
	};

	enum class PipeCredentialSaveStatus : std::uint8_t
	{
		Succeeded,
		PolicyDenied,
		Unavailable,
		IoError,
		InvalidResponse
	};

	struct PipeCredentialSaveResponse
	{
		PipeCredentialSaveStatus status = PipeCredentialSaveStatus::Unavailable;
		std::uint32_t windowsError = 0;
		CredentialSaveResult result;
	};

	struct PipeCredentialCaptureChallengeResponse
	{
		PipeCredentialSaveStatus status = PipeCredentialSaveStatus::Unavailable;
		std::uint32_t windowsError = 0;
		CredentialCaptureChallengeResult result;
	};

	struct PipeCredentialCaptureHelperResponse
	{
		PipeCredentialSaveStatus status = PipeCredentialSaveStatus::Unavailable;
		std::uint32_t windowsError = 0;
		CredentialCaptureHelperResult result;
	};

	struct PipeCredentialCaptureSubmitResponse
	{
		PipeCredentialSaveStatus status = PipeCredentialSaveStatus::Unavailable;
		std::uint32_t windowsError = 0;
		CredentialCaptureSubmitResult result;
	};

	struct PipeCredentialCaptureCommitResponse
	{
		PipeCredentialSaveStatus status = PipeCredentialSaveStatus::Unavailable;
		std::uint32_t windowsError = 0;
		CredentialCaptureCommitResult result;
	};

	using PipeCredentialDeleteStatus = PipeCredentialSaveStatus;

	struct PipeCredentialDeleteResponse
	{
		PipeCredentialDeleteStatus status = PipeCredentialDeleteStatus::Unavailable;
		std::uint32_t windowsError = 0;
		CredentialDeleteResult result;
	};

	using PipeCredentialListStatus = PipeCredentialSaveStatus;

	struct PipeCredentialListResponse
	{
		PipeCredentialListStatus status = PipeCredentialListStatus::Unavailable;
		std::uint32_t windowsError = 0;
		CredentialListResult result;
	};

	using PipeAdministrativeInventoryStatus = PipeCredentialSaveStatus;

	struct PipeAdministrativeInventoryResponse
	{
		PipeAdministrativeInventoryStatus status =
			PipeAdministrativeInventoryStatus::Unavailable;
		std::uint32_t windowsError = 0;
		AdministrativeInventoryResult result;
	};

	using PipeAdministrativeDeleteStatus = PipeCredentialSaveStatus;

	struct PipeAdministrativeDeleteResponse
	{
		PipeAdministrativeDeleteStatus status =
			PipeAdministrativeDeleteStatus::Unavailable;
		std::uint32_t windowsError = 0;
		AdministrativeDeleteResult result;
	};

	using PipeBackupStatus = PipeCredentialSaveStatus;

	struct PipeBackupResponse
	{
		PipeBackupStatus status = PipeBackupStatus::Unavailable;
		std::uint32_t windowsError = 0;
		BackupResult result;
	};

	using PipeTrackedProcessListStatus = PipeCredentialSaveStatus;

	struct PipeTrackedProcessListResponse
	{
		PipeTrackedProcessListStatus status = PipeTrackedProcessListStatus::Unavailable;
		std::uint32_t windowsError = 0;
		TrackedProcessListResult result;
	};

	using PipeStatusIconListStatus = PipeCredentialSaveStatus;

	struct PipeStatusIconListResponse
	{
		PipeStatusIconListStatus status = PipeStatusIconListStatus::Unavailable;
		std::uint32_t windowsError = 0;
		StatusIconListResult result;
	};

	using PipeTrackedProcessRegistrationStatus = PipeCredentialSaveStatus;

	struct PipeTrackedProcessRegistrationResponse
	{
		PipeTrackedProcessRegistrationStatus status =
			PipeTrackedProcessRegistrationStatus::Unavailable;
		std::uint32_t windowsError = 0;
		TrackedProcessRegistrationResult result;
	};

	using PipeSecurityAuditStatus = PipeCredentialSaveStatus;

	struct PipeSecurityAuditResponse
	{
		PipeSecurityAuditStatus status = PipeSecurityAuditStatus::Unavailable;
		std::uint32_t windowsError = 0;
		SecurityAuditResult result;
	};

	using PipeBrokerStopStatus = PipeCredentialSaveStatus;

	struct PipeBrokerStopResponse
	{
		PipeBrokerStopStatus status = PipeBrokerStopStatus::Unavailable;
		std::uint32_t windowsError = 0;
		BrokerStopResult result;
	};

	using PipeCredentialUseTestStatus = PipeCredentialSaveStatus;

	struct PipeCredentialUseTestResponse
	{
		PipeCredentialUseTestStatus status = PipeCredentialUseTestStatus::Unavailable;
		std::uint32_t windowsError = 0;
		CredentialUseTestResult result;
	};

	using PipeApprovalChallengeStatus = PipeCredentialSaveStatus;

	struct PipeApprovalChallengeResponse
	{
		PipeApprovalChallengeStatus status = PipeApprovalChallengeStatus::Unavailable;
		std::uint32_t windowsError = 0;
		ApprovalChallengeResult result;
	};

	using PipeApprovalConfirmationStatus = PipeCredentialSaveStatus;

	struct PipeApprovalConfirmationResponse
	{
		PipeApprovalConfirmationStatus status =
			PipeApprovalConfirmationStatus::Unavailable;
		std::uint32_t windowsError = 0;
		ApprovalConfirmationResult result;
	};

	using PipeApprovalHelperStatus = PipeCredentialSaveStatus;

	struct PipeApprovalHelperResponse
	{
		PipeApprovalHelperStatus status = PipeApprovalHelperStatus::Unavailable;
		std::uint32_t windowsError = 0;
		ApprovalChallengeResult result;
	};

	struct PipeApprovalHelperDecisionResponse
	{
		PipeApprovalHelperStatus status = PipeApprovalHelperStatus::Unavailable;
		std::uint32_t windowsError = 0;
		ApprovalHelperDecisionResult result;
	};

	using PipeApprovalManagementStatus = PipeCredentialSaveStatus;

	struct PipeApprovalListResponse
	{
		PipeApprovalManagementStatus status = PipeApprovalManagementStatus::Unavailable;
		std::uint32_t windowsError = 0;
		ApprovalListResult result;
	};

	struct PipeApprovalResetResponse
	{
		PipeApprovalManagementStatus status = PipeApprovalManagementStatus::Unavailable;
		std::uint32_t windowsError = 0;
		ApprovalResetResult result;
	};

	std::uint64_t GenerateBrokerRequestId() noexcept;
	bool StartBrokerService(
		std::uint32_t timeoutMilliseconds = DefaultBrokerRequestTimeoutMilliseconds,
		std::uint32_t* windowsError = nullptr);
	bool QueryRunningBrokerServiceProcessId(
		DWORD& processId, DWORD& windowsError) noexcept;
	PipeHealthCheck QueryBrokerPipeHealth(std::uint32_t timeoutMilliseconds = 3000);
	PipeLaunchResponse SubmitBrokerLaunchRequest(
		const LaunchRequest& request,
		std::uint32_t timeoutMilliseconds = DefaultBrokerRequestTimeoutMilliseconds);
	PipeSystemLaunchResponse SubmitSystemLaunchRequest(
		const SystemLaunchRequest& request,
		std::uint32_t timeoutMilliseconds = DefaultBrokerRequestTimeoutMilliseconds);
	PipeCredentialMetadataResponse SubmitCredentialMetadataRequest(
		const CredentialMetadataRequest& request,
		std::uint32_t timeoutMilliseconds = DefaultBrokerRequestTimeoutMilliseconds);
	PipeSessionTestResponse SubmitSessionTestRequest(
		const SessionTestRequest& request,
		std::uint32_t timeoutMilliseconds = DefaultBrokerRequestTimeoutMilliseconds);
	PipeCredentialStoreTestResponse SubmitCredentialStoreTestRequest(
		const CredentialStoreTestRequest& request,
		std::uint32_t timeoutMilliseconds = DefaultBrokerRequestTimeoutMilliseconds);
	PipeCredentialSaveResponse SubmitCredentialSaveRequest(
		const CredentialSaveRequest& request,
		std::uint32_t timeoutMilliseconds = DefaultBrokerRequestTimeoutMilliseconds);
	PipeCredentialCaptureChallengeResponse SubmitCredentialCaptureChallengeRequest(
		const CredentialCaptureChallengeRequest& request,
		std::uint32_t timeoutMilliseconds = DefaultBrokerRequestTimeoutMilliseconds);
	PipeCredentialCaptureHelperResponse SubmitCredentialCaptureHelperRequest(
		const CredentialCaptureHelperRequest& request,
		std::uint32_t timeoutMilliseconds = DefaultBrokerRequestTimeoutMilliseconds);
	PipeCredentialCaptureSubmitResponse SubmitCredentialCaptureSubmitRequest(
		const CredentialCaptureSubmitRequest& request,
		std::uint32_t timeoutMilliseconds = DefaultBrokerRequestTimeoutMilliseconds);
	PipeCredentialCaptureCommitResponse SubmitCredentialCaptureCommitRequest(
		const CredentialCaptureCommitRequest& request,
		std::uint32_t timeoutMilliseconds = DefaultBrokerRequestTimeoutMilliseconds);
	PipeCredentialDeleteResponse SubmitCredentialDeleteRequest(
		const CredentialDeleteRequest& request,
		std::uint32_t timeoutMilliseconds = DefaultBrokerRequestTimeoutMilliseconds);
	PipeCredentialListResponse SubmitCredentialListRequest(
		const CredentialListRequest& request,
		std::uint32_t timeoutMilliseconds = DefaultBrokerRequestTimeoutMilliseconds);
	PipeAdministrativeInventoryResponse SubmitAdministrativeInventoryRequest(
		const AdministrativeInventoryRequest& request,
		std::uint32_t timeoutMilliseconds = DefaultBrokerRequestTimeoutMilliseconds);
	PipeAdministrativeDeleteResponse SubmitAdministrativeDeleteRequest(
		const AdministrativeDeleteRequest& request,
		std::uint32_t timeoutMilliseconds = DefaultBrokerRequestTimeoutMilliseconds);
	PipeBackupResponse SubmitBackupRequest(const BackupRequest& request,
		std::uint32_t timeoutMilliseconds = DefaultBrokerRequestTimeoutMilliseconds);
	PipeTrackedProcessListResponse SubmitTrackedProcessListRequest(
		const TrackedProcessListRequest& request,
		std::uint32_t timeoutMilliseconds = DefaultBrokerRequestTimeoutMilliseconds);
	PipeStatusIconListResponse SubmitStatusIconListRequest(
		const StatusIconListRequest& request,
		std::uint32_t timeoutMilliseconds = DefaultBrokerRequestTimeoutMilliseconds);
	PipeTrackedProcessRegistrationResponse SubmitTrackedProcessRegistrationRequest(
		const TrackedProcessRegistrationRequest& request,
		std::uint32_t timeoutMilliseconds = DefaultBrokerRequestTimeoutMilliseconds);
	PipeSecurityAuditResponse SubmitSecurityAuditRequest(
		const SecurityAuditRequest& request,
		std::uint32_t timeoutMilliseconds = DefaultBrokerRequestTimeoutMilliseconds);
	PipeBrokerStopResponse SubmitBrokerStopRequest(
		const BrokerStopRequest& request,
		std::uint32_t timeoutMilliseconds = DefaultBrokerRequestTimeoutMilliseconds);
	PipeCredentialUseTestResponse SubmitCredentialUseTestRequest(
		const CredentialUseTestRequest& request,
		std::uint32_t timeoutMilliseconds = DefaultBrokerRequestTimeoutMilliseconds);
	PipeApprovalChallengeResponse SubmitApprovalChallengeRequest(
		const ApprovalChallengeRequest& request,
		std::uint32_t timeoutMilliseconds);
	PipeApprovalConfirmationResponse SubmitApprovalConfirmationRequest(
		const ApprovalConfirmationRequest& request,
		std::uint32_t timeoutMilliseconds);
	PipeApprovalHelperResponse SubmitApprovalHelperRequest(
		const ApprovalHelperRequest& request,
		std::uint32_t timeoutMilliseconds = DefaultBrokerRequestTimeoutMilliseconds);
	PipeApprovalHelperDecisionResponse SubmitApprovalHelperLeaseRequest(
		const ApprovalHelperRequest& request,
		std::uint32_t timeoutMilliseconds = DefaultBrokerRequestTimeoutMilliseconds);
	PipeApprovalHelperDecisionResponse SubmitApprovalHelperDecisionRequest(
		const ApprovalHelperDecisionRequest& request,
		std::uint32_t timeoutMilliseconds = DefaultBrokerRequestTimeoutMilliseconds);
	PipeApprovalListResponse SubmitApprovalListRequest(
		const ApprovalListRequest& request,
		std::uint32_t timeoutMilliseconds = DefaultBrokerRequestTimeoutMilliseconds);
	PipeApprovalResetResponse SubmitApprovalResetRequest(
		const ApprovalResetRequest& request,
		std::uint32_t timeoutMilliseconds = DefaultBrokerRequestTimeoutMilliseconds);
}