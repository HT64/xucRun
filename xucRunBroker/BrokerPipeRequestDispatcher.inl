// Purpose: Internal typed request dispatch for BrokerPipeServer.cpp.
//
// Inclusion contract:
// - Included after BrokerPipeRequestDecoder.inl and all request handlers.
// - Dispatch performs no frame parsing and never owns client or pipe handles.
// - The connection loop remains solely responsible for closing and disconnecting.
// - A true result means that an authenticated broker-stop request was accepted.

bool DispatchBrokerRequest(DecodedBrokerRequest& decoded,
	HANDLE pipe, HANDLE stopEvent, HANDLE clientProcess,
	ULONG clientProcessId, DWORD clientSessionId, DWORD clientIntegrityRid,
	const ClientImageIdentity& imageIdentity,
	const std::wstring& clientSid, const std::wstring& clientLogonSid,
	std::deque<PendingApprovalChallenge>& pendingApprovalChallenges,
	std::deque<PendingCredentialCaptureChallenge>& pendingCredentialCaptureChallenges,
	std::vector<TrackedElevatedProcess>& trackedElevatedProcesses,
	std::vector<TrackedElevatedProcessJob>& trackedElevatedProcessJobs,
	std::vector<RegisteredStatusIcon>& registeredStatusIcons,
	BrokerLogCallback log, DWORD& transferred)
{
	switch (decoded.kind)
	{
	case BrokerRequestKind::CredentialCaptureCommit:
		HandleCredentialCaptureCommitRequest(pipe, stopEvent, clientProcessId,
			clientSessionId, decoded.credentialCaptureCommit,
			pendingCredentialCaptureChallenges, log, transferred);
		break;
	case BrokerRequestKind::AdministrativeInventory:
		HandleAdministrativeInventoryRequest(pipe, stopEvent, clientProcess,
			imageIdentity, decoded.administrativeInventory, transferred);
		break;
	case BrokerRequestKind::AdministrativeDelete:
		HandleAdministrativeDeleteRequest(pipe, stopEvent, clientProcess,
			imageIdentity, decoded.administrativeDelete, transferred);
		break;
	case BrokerRequestKind::Backup:
		HandleBackupRequest(pipe, stopEvent, clientProcess,
			imageIdentity, decoded.backup, transferred);
		break;
	case BrokerRequestKind::Health:
		HandleHealthRequest(pipe, stopEvent, decoded.health,
			trackedElevatedProcesses, trackedElevatedProcessJobs,
			clientSessionId, log, transferred);
		break;
	case BrokerRequestKind::CredentialCaptureSubmit:
		HandleCredentialCaptureSubmitRequest(pipe, stopEvent, clientProcessId,
			clientSessionId, clientIntegrityRid, imageIdentity,
			decoded.credentialCaptureSubmit, pendingCredentialCaptureChallenges,
			log, transferred);
		break;
	case BrokerRequestKind::CredentialCaptureHelper:
		HandleCredentialCaptureHelperRequest(pipe, stopEvent, clientProcessId,
			clientSessionId, clientIntegrityRid, imageIdentity, clientSid,
			decoded.credentialCaptureHelper, pendingCredentialCaptureChallenges,
			transferred);
		break;
	case BrokerRequestKind::CredentialCaptureChallenge:
		HandleCredentialCaptureChallengeRequest(pipe, stopEvent, clientProcess,
			clientProcessId, imageIdentity, clientSid, clientLogonSid,
			clientSessionId, decoded.credentialCaptureChallenge,
			pendingCredentialCaptureChallenges, pendingApprovalChallenges,
			log, transferred);
		break;
	case BrokerRequestKind::BrokerStop:
		return HandleBrokerStopRequest(
			pipe, stopEvent, imageIdentity, decoded.brokerStop, transferred);
	case BrokerRequestKind::TrackedProcessRegistration:
		HandleTrackedProcessRegistrationRequest(pipe, stopEvent,
			clientProcess, imageIdentity, clientSessionId, clientLogonSid,
			decoded.trackedProcessRegistration, trackedElevatedProcesses,
			trackedElevatedProcessJobs, log, transferred);
		break;
	case BrokerRequestKind::TrackedProcessList:
		HandleTrackedProcessListRequest(pipe, stopEvent, imageIdentity,
			clientSessionId, decoded.trackedProcessList,
			trackedElevatedProcesses, trackedElevatedProcessJobs, transferred);
		break;
	case BrokerRequestKind::StatusIconList:
		HandleStatusIconListRequest(pipe, stopEvent, clientProcess,
			clientProcessId, clientSessionId, clientSid, imageIdentity,
			decoded.statusIconList, registeredStatusIcons, log, transferred);
		break;
	case BrokerRequestKind::ApprovalList:
		HandleApprovalListRequest(pipe, stopEvent, imageIdentity,
			clientSid, clientLogonSid, decoded.approvalList, transferred);
		break;
	case BrokerRequestKind::ApprovalReset:
		HandleApprovalResetRequest(pipe, stopEvent, imageIdentity,
			clientSid, decoded.approvalReset, transferred);
		break;
	case BrokerRequestKind::ApprovalConfirmation:
		HandleApprovalConfirmationRequest(pipe, stopEvent, clientProcess,
			imageIdentity, clientSid, clientLogonSid, clientSessionId,
			decoded.approvalConfirmation, pendingApprovalChallenges,
			trackedElevatedProcesses, trackedElevatedProcessJobs, log, transferred);
		break;
	case BrokerRequestKind::ApprovalHelperDecision:
		HandleApprovalHelperDecisionRequest(pipe, stopEvent, clientProcessId,
			clientSessionId, clientIntegrityRid, imageIdentity,
			decoded.approvalHelperDecision, pendingApprovalChallenges, transferred);
		break;
	case BrokerRequestKind::ApprovalHelperLease:
		HandleApprovalHelperLeaseRequest(pipe, stopEvent, clientProcessId,
			clientSessionId, clientIntegrityRid, imageIdentity,
			decoded.approvalHelperLease, pendingApprovalChallenges, transferred);
		break;
	case BrokerRequestKind::ApprovalHelper:
		HandleApprovalHelperRequest(pipe, stopEvent, clientProcessId,
			clientSessionId, clientIntegrityRid, imageIdentity, clientSid,
			decoded.approvalHelper, pendingApprovalChallenges, transferred);
		break;
	case BrokerRequestKind::ApprovalChallenge:
		HandleApprovalChallengeRequest(pipe, stopEvent, clientProcess,
			clientProcessId, imageIdentity, clientSid, clientLogonSid,
			clientSessionId, decoded.approvalChallenge, pendingApprovalChallenges,
			pendingCredentialCaptureChallenges, log, transferred);
		break;
	case BrokerRequestKind::CredentialDelete:
		HandleCredentialDeleteRequest(pipe, stopEvent, imageIdentity,
			clientSid, decoded.credentialDelete, transferred);
		break;
	case BrokerRequestKind::CredentialList:
		HandleCredentialListRequest(pipe, stopEvent, imageIdentity,
			clientSid, decoded.credentialList, transferred);
		break;
	case BrokerRequestKind::SecurityAudit:
		HandleSecurityAuditRequest(pipe, stopEvent, imageIdentity,
			clientSid, decoded.securityAudit, log, transferred);
		break;
	case BrokerRequestKind::CredentialUseTest:
		HandleCredentialUseTestRequest(pipe, stopEvent, clientProcess,
			imageIdentity, clientSid, clientSessionId,
			decoded.credentialUseTest, log, transferred);
		break;
	case BrokerRequestKind::CredentialSave:
		HandleCredentialSaveRequest(pipe, stopEvent, imageIdentity,
			clientSid, decoded.credentialSave, log, transferred);
		break;
	case BrokerRequestKind::CredentialStoreTest:
		HandleCredentialStoreTestRequest(pipe, stopEvent, clientProcess,
			decoded.credentialStoreTest, log, transferred);
		break;
	case BrokerRequestKind::SessionTest:
		HandleSessionTestRequest(pipe, stopEvent, clientProcess,
			clientSessionId, clientSid, decoded.sessionTest, log, transferred);
		break;
	case BrokerRequestKind::CredentialMetadata:
		HandleCredentialMetadataRequest(pipe, stopEvent, clientSid,
			decoded.credentialMetadata, log, transferred);
		break;
	case BrokerRequestKind::Launch:
		HandleLaunchRequest(pipe, stopEvent, decoded.launch, log, transferred);
		break;
	default:
		break;
	}
	return false;
}
