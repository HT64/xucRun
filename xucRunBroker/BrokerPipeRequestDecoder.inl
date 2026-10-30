// Purpose: Internal request decoding for BrokerPipeServer.cpp.
// Approach: Provides implementation helpers included by the owning translation unit to keep the related flow grouped.
//
// Inclusion contract:
// - Included exactly once after the broker's private replay and authorization helpers.
// - The order below is the protocol precedence. Earlier contracts must keep priority.
// - Diagnostic contracts remain unavailable in release builds.
// - The decoded model owns every request object so dispatch never reparses a frame.

struct DecodedBrokerRequest
{
	BrokerRequestKind kind = BrokerRequestKind::Invalid;
	xucrun::broker::CredentialCaptureCommitRequest credentialCaptureCommit;
	xucrun::broker::HealthRequest health;
	xucrun::broker::LaunchRequest launch;
	xucrun::broker::SystemLaunchRequest systemLaunch;
	xucrun::broker::CredentialMetadataRequest credentialMetadata;
	xucrun::broker::SessionTestRequest sessionTest;
	xucrun::broker::CredentialStoreTestRequest credentialStoreTest;
	xucrun::broker::CredentialSaveRequest credentialSave;
	xucrun::broker::CredentialDeleteRequest credentialDelete;
	xucrun::broker::CredentialListRequest credentialList;
	xucrun::broker::CredentialUseTestRequest credentialUseTest;
	xucrun::broker::ApprovalChallengeRequest approvalChallenge;
	xucrun::broker::ApprovalConfirmationRequest approvalConfirmation;
	xucrun::broker::ApprovalHelperRequest approvalHelper;
	xucrun::broker::ApprovalHelperDecisionRequest approvalHelperDecision;
	xucrun::broker::ApprovalHelperRequest approvalHelperLease;
	xucrun::broker::ApprovalListRequest approvalList;
	xucrun::broker::ApprovalResetRequest approvalReset;
	xucrun::broker::SecurityAuditRequest securityAudit;
	xucrun::broker::TrackedProcessListRequest trackedProcessList;
	xucrun::broker::TrackedProcessRegistrationRequest trackedProcessRegistration;
	xucrun::broker::StatusIconListRequest statusIconList;
	xucrun::broker::BrokerStopRequest brokerStop;
	xucrun::broker::CredentialCaptureChallengeRequest credentialCaptureChallenge;
	xucrun::broker::CredentialCaptureHelperRequest credentialCaptureHelper;
	xucrun::broker::CredentialCaptureSubmitRequest credentialCaptureSubmit;
	xucrun::broker::AdministrativeInventoryRequest administrativeInventory;
	xucrun::broker::AdministrativeDeleteRequest administrativeDelete;
	xucrun::broker::BackupRequest backup;
};

// Decodes exactly one request using the historical first-match precedence.
bool DecodeBrokerRequest(std::span<const std::uint8_t> frame,
	DecodedBrokerRequest& decoded)
{
	if (xucrun::broker::DeserializeCredentialCaptureCommitRequest(
		frame, decoded.credentialCaptureCommit))
	{
		decoded.kind = BrokerRequestKind::CredentialCaptureCommit;
		return true;
	}
	if (xucrun::broker::DeserializeHealthRequest(frame, decoded.health))
	{
		decoded.kind = BrokerRequestKind::Health;
		return true;
	}
	if (DiagnosticPipeContractsEnabled &&
		xucrun::broker::DeserializeLaunchRequest(frame, decoded.launch))
	{
		decoded.kind = BrokerRequestKind::Launch;
		return true;
	}
	if (DiagnosticPipeContractsEnabled &&
		xucrun::broker::DeserializeCredentialMetadataRequest(
			frame, decoded.credentialMetadata))
	{
		decoded.kind = BrokerRequestKind::CredentialMetadata;
		return true;
	}
	if (DiagnosticPipeContractsEnabled &&
		xucrun::broker::DeserializeSessionTestRequest(frame, decoded.sessionTest))
	{
		decoded.kind = BrokerRequestKind::SessionTest;
		return true;
	}
	if (DiagnosticPipeContractsEnabled &&
		xucrun::broker::DeserializeCredentialStoreTestRequest(
			frame, decoded.credentialStoreTest))
	{
		decoded.kind = BrokerRequestKind::CredentialStoreTest;
		return true;
	}
	if (DiagnosticPipeContractsEnabled &&
		xucrun::broker::DeserializeCredentialSaveRequest(frame, decoded.credentialSave))
	{
		decoded.kind = BrokerRequestKind::CredentialSave;
		return true;
	}
	if (xucrun::broker::DeserializeCredentialDeleteRequest(
		frame, decoded.credentialDelete))
	{
		decoded.kind = BrokerRequestKind::CredentialDelete;
		return true;
	}
	if (xucrun::broker::DeserializeCredentialListRequest(frame, decoded.credentialList))
	{
		decoded.kind = BrokerRequestKind::CredentialList;
		return true;
	}
	if (xucrun::broker::DeserializeCredentialUseTestRequest(
		frame, decoded.credentialUseTest))
	{
		decoded.kind = BrokerRequestKind::CredentialUseTest;
		return true;
	}
	if (xucrun::broker::DeserializeApprovalChallengeRequest(
		frame, decoded.approvalChallenge))
	{
		decoded.kind = BrokerRequestKind::ApprovalChallenge;
		return true;
	}
	if (xucrun::broker::DeserializeApprovalConfirmationRequest(
		frame, decoded.approvalConfirmation))
	{
		decoded.kind = BrokerRequestKind::ApprovalConfirmation;
		return true;
	}
	if (xucrun::broker::DeserializeApprovalHelperRequest(frame, decoded.approvalHelper))
	{
		decoded.kind = BrokerRequestKind::ApprovalHelper;
		return true;
	}
	if (xucrun::broker::DeserializeApprovalHelperDecisionRequest(
		frame, decoded.approvalHelperDecision))
	{
		decoded.kind = BrokerRequestKind::ApprovalHelperDecision;
		return true;
	}
	if (xucrun::broker::DeserializeApprovalHelperLeaseRequest(
		frame, decoded.approvalHelperLease))
	{
		decoded.kind = BrokerRequestKind::ApprovalHelperLease;
		return true;
	}
	if (xucrun::broker::DeserializeApprovalListRequest(frame, decoded.approvalList))
	{
		decoded.kind = BrokerRequestKind::ApprovalList;
		return true;
	}
	if (xucrun::broker::DeserializeApprovalResetRequest(frame, decoded.approvalReset))
	{
		decoded.kind = BrokerRequestKind::ApprovalReset;
		return true;
	}
	if (xucrun::broker::DeserializeSecurityAuditRequest(frame, decoded.securityAudit))
	{
		decoded.kind = BrokerRequestKind::SecurityAudit;
		return true;
	}
	if (xucrun::broker::DeserializeTrackedProcessListRequest(
		frame, decoded.trackedProcessList))
	{
		decoded.kind = BrokerRequestKind::TrackedProcessList;
		return true;
	}
	if (xucrun::broker::DeserializeTrackedProcessRegistrationRequest(
		frame, decoded.trackedProcessRegistration))
	{
		decoded.kind = BrokerRequestKind::TrackedProcessRegistration;
		return true;
	}
	if (xucrun::broker::DeserializeStatusIconListRequest(frame, decoded.statusIconList))
	{
		decoded.kind = BrokerRequestKind::StatusIconList;
		return true;
	}
	if (xucrun::broker::DeserializeBrokerStopRequest(frame, decoded.brokerStop))
	{
		decoded.kind = BrokerRequestKind::BrokerStop;
		return true;
	}
	if (xucrun::broker::DeserializeCredentialCaptureChallengeRequest(
		frame, decoded.credentialCaptureChallenge))
	{
		decoded.kind = BrokerRequestKind::CredentialCaptureChallenge;
		return true;
	}
	if (xucrun::broker::DeserializeCredentialCaptureHelperRequest(
		frame, decoded.credentialCaptureHelper))
	{
		decoded.kind = BrokerRequestKind::CredentialCaptureHelper;
		return true;
	}
	if (xucrun::broker::DeserializeCredentialCaptureSubmitRequest(
		frame, decoded.credentialCaptureSubmit))
	{
		decoded.kind = BrokerRequestKind::CredentialCaptureSubmit;
		return true;
	}
	if (xucrun::broker::DeserializeAdministrativeInventoryRequest(
		frame, decoded.administrativeInventory))
	{
		decoded.kind = BrokerRequestKind::AdministrativeInventory;
		return true;
	}
	if (xucrun::broker::DeserializeAdministrativeDeleteRequest(
		frame, decoded.administrativeDelete))
	{
		decoded.kind = BrokerRequestKind::AdministrativeDelete;
		return true;
	}
	if (xucrun::broker::DeserializeBackupRequest(frame, decoded.backup))
	{
		decoded.kind = BrokerRequestKind::Backup;
		return true;
	}
	if (xucrun::broker::DeserializeSystemLaunchRequest(frame, decoded.systemLaunch))
	{
		decoded.kind = BrokerRequestKind::SystemLaunch;
		return true;
	}
	return false;
}

// Returns the ID used by the replay cache for every non-health contract.
std::uint64_t GetBrokerRequestId(const DecodedBrokerRequest& decoded)
{
	switch (decoded.kind)
	{
	case BrokerRequestKind::CredentialCaptureCommit: return decoded.credentialCaptureCommit.requestId;
	case BrokerRequestKind::Launch: return decoded.launch.requestId;
	case BrokerRequestKind::CredentialMetadata: return decoded.credentialMetadata.requestId;
	case BrokerRequestKind::SessionTest: return decoded.sessionTest.requestId;
	case BrokerRequestKind::CredentialStoreTest: return decoded.credentialStoreTest.requestId;
	case BrokerRequestKind::CredentialSave: return decoded.credentialSave.requestId;
	case BrokerRequestKind::CredentialDelete: return decoded.credentialDelete.requestId;
	case BrokerRequestKind::CredentialList: return decoded.credentialList.requestId;
	case BrokerRequestKind::CredentialUseTest: return decoded.credentialUseTest.requestId;
	case BrokerRequestKind::ApprovalChallenge: return decoded.approvalChallenge.requestId;
	case BrokerRequestKind::ApprovalConfirmation: return decoded.approvalConfirmation.requestId;
	case BrokerRequestKind::ApprovalHelper: return decoded.approvalHelper.requestId;
	case BrokerRequestKind::ApprovalHelperDecision: return decoded.approvalHelperDecision.requestId;
	case BrokerRequestKind::ApprovalHelperLease: return decoded.approvalHelperLease.requestId;
	case BrokerRequestKind::ApprovalList: return decoded.approvalList.requestId;
	case BrokerRequestKind::ApprovalReset: return decoded.approvalReset.requestId;
	case BrokerRequestKind::SecurityAudit: return decoded.securityAudit.requestId;
	case BrokerRequestKind::TrackedProcessList: return decoded.trackedProcessList.requestId;
	case BrokerRequestKind::TrackedProcessRegistration:
		return decoded.trackedProcessRegistration.requestId;
	case BrokerRequestKind::StatusIconList: return decoded.statusIconList.requestId;
	case BrokerRequestKind::BrokerStop: return decoded.brokerStop.requestId;
	case BrokerRequestKind::CredentialCaptureChallenge:
		return decoded.credentialCaptureChallenge.requestId;
	case BrokerRequestKind::CredentialCaptureHelper:
		return decoded.credentialCaptureHelper.requestId;
	case BrokerRequestKind::CredentialCaptureSubmit:
		return decoded.credentialCaptureSubmit.requestId;
	case BrokerRequestKind::AdministrativeInventory:
		return decoded.administrativeInventory.requestId;
	case BrokerRequestKind::AdministrativeDelete:
		return decoded.administrativeDelete.requestId;
	case BrokerRequestKind::Backup: return decoded.backup.requestId;
	case BrokerRequestKind::SystemLaunch: return decoded.systemLaunch.requestId;
	default: return 0;
	}
}

bool IsBrokerRequestReplay(const DecodedBrokerRequest& decoded,
	std::deque<ReplayEntry>& replayEntries)
{
	if (decoded.kind == BrokerRequestKind::Health)
		return IsReplay(decoded.health, replayEntries);

	const std::uint64_t requestId = GetBrokerRequestId(decoded);
	return requestId == 0 || std::any_of(replayEntries.begin(), replayEntries.end(),
		[requestId](const ReplayEntry& entry) { return entry.requestId == requestId; });
}

void RecordBrokerRequest(const DecodedBrokerRequest& decoded,
	std::deque<ReplayEntry>& replayEntries)
{
	if (decoded.kind == BrokerRequestKind::Health)
		return;
	replayEntries.push_back({ GetBrokerRequestId(decoded), {} });
	if (replayEntries.size() > 256)
		replayEntries.pop_front();
}

bool UsesGeneralClientAuditLog(BrokerRequestKind kind)
{
	switch (kind)
	{
	case BrokerRequestKind::Launch:
	case BrokerRequestKind::CredentialMetadata:
	case BrokerRequestKind::SessionTest:
	case BrokerRequestKind::CredentialStoreTest:
	case BrokerRequestKind::CredentialSave:
	case BrokerRequestKind::CredentialDelete:
	case BrokerRequestKind::CredentialList:
	case BrokerRequestKind::CredentialUseTest:
	case BrokerRequestKind::ApprovalChallenge:
	case BrokerRequestKind::ApprovalConfirmation:
	case BrokerRequestKind::ApprovalHelper:
	case BrokerRequestKind::ApprovalHelperDecision:
	case BrokerRequestKind::ApprovalHelperLease:
	case BrokerRequestKind::ApprovalList:
	case BrokerRequestKind::ApprovalReset:
	case BrokerRequestKind::SecurityAudit:
	case BrokerRequestKind::TrackedProcessList:
	case BrokerRequestKind::TrackedProcessRegistration:
	case BrokerRequestKind::BrokerStop:
	case BrokerRequestKind::SystemLaunch:
		return true;
	default:
		return false;
	}
}
