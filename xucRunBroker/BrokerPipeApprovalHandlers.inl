// Purpose: Internal request-handler implementation fragment for BrokerPipeServer.cpp.
//
// Inclusion contract:
// - This file is included exactly once, from inside BrokerPipeServer.cpp's anonymous namespace.
// - It deliberately uses the server's private identity, replay, challenge, tracking, and I/O helpers.
// - It is not a standalone translation unit and must not expose broker-internal state in public headers.
// - Handler behavior and protocol responses remain defined by the existing broker contracts.
// Returns only approvals for the authenticated client SID and current logon session.
	void HandleApprovalListRequest(HANDLE pipe, HANDLE stopEvent,
		const ClientImageIdentity& imageIdentity, const std::wstring& clientSid,
		const std::wstring& clientLogonSid,
		const xucrun::broker::ApprovalListRequest& request, DWORD& transferred)
	{
		xucrun::broker::ApprovalListResult result;
		result.requestId = request.requestId;
		DWORD listError = NO_ERROR;
		if (!IsSensitiveClientAuthorized(imageIdentity, listError))
		{
			result.status = xucrun::broker::ApprovalManagementStatus::InvalidRequest;
			result.windowsError = listError;
		}
		else if (!xucrun::approval_store::List(
			clientSid, clientLogonSid, result.entries, listError))
		{
			result.status = xucrun::broker::ApprovalManagementStatus::Failed;
			result.windowsError = listError;
		}
		else
		{
			result.status = xucrun::broker::ApprovalManagementStatus::Succeeded;
		}

		std::vector<std::uint8_t> response =
			xucrun::broker::SerializeApprovalListResult(result);
		RunOverlappedIo(pipe, stopEvent, true, response.data(),
			static_cast<DWORD>(response.size()), transferred);
	}

// Deletes an approval only if its stored owner SID matches the authenticated client.
	void HandleApprovalResetRequest(HANDLE pipe, HANDLE stopEvent,
		const ClientImageIdentity& imageIdentity, const std::wstring& clientSid,
		const xucrun::broker::ApprovalResetRequest& request, DWORD& transferred)
	{
		xucrun::broker::ApprovalResetResult result;
		result.requestId = request.requestId;
		DWORD resetError = NO_ERROR;
		if (!IsSensitiveClientAuthorized(imageIdentity, resetError))
		{
			result.status = xucrun::broker::ApprovalManagementStatus::InvalidRequest;
			result.windowsError = resetError;
		}
		else if (_wcsicmp(request.approval.ownerUserSid.c_str(), clientSid.c_str()) != 0)
		{
			result.status = xucrun::broker::ApprovalManagementStatus::InvalidRequest;
			result.windowsError = ERROR_ACCESS_DENIED;
		}
		bool deleted = false;
		if (result.windowsError == NO_ERROR &&
			!xucrun::approval_store::DeleteListed(
				clientSid, request.approval, deleted, resetError))
		{
			result.status = xucrun::broker::ApprovalManagementStatus::Failed;
			result.windowsError = resetError;
		}
		else if (result.windowsError == NO_ERROR)
		{
			result.status = xucrun::broker::ApprovalManagementStatus::Succeeded;
			result.deletedCount = deleted ? 1 : 0;
		}

		std::vector<std::uint8_t> response =
			xucrun::broker::SerializeApprovalResetResult(result);
		RunOverlappedIo(pipe, stopEvent, true, response.data(),
			static_cast<DWORD>(response.size()), transferred);
	}

	// Returns challenge details only to the helper identity and process bound to that challenge.
	void HandleApprovalHelperRequest(HANDLE pipe, HANDLE stopEvent,
		DWORD helperProcessId, DWORD helperSessionId, DWORD helperIntegrityRid,
		const ClientImageIdentity& helperIdentity, const std::wstring& helperSid,
		const xucrun::broker::ApprovalHelperRequest& request,
		std::deque<PendingApprovalChallenge>& pendingChallenges,
		DWORD& transferred)
	{
		xucrun::broker::ApprovalChallengeResult result;
		result.requestId = request.requestId;
		DWORD error = NO_ERROR;
		auto pending = std::find_if(pendingChallenges.begin(), pendingChallenges.end(),
			[&request](const PendingApprovalChallenge& value) {
				return value.challengeId == request.challengeId;
			});
		if (!IsApprovalHelperAuthorized(helperIdentity, helperIntegrityRid, error))
		{
			result.status = xucrun::broker::ApprovalChallengeStatus::Denied;
			result.windowsError = error;
		}
		else if (pending == pendingChallenges.end())
		{
			result.status = xucrun::broker::ApprovalChallengeStatus::InvalidRequest;
			result.windowsError = ERROR_NOT_FOUND;
		}
		else if (pending->expiresAt <= GetTickCount64())
		{
			CancelSecureHost(pending->secureHostCancellationSignal);
			pendingChallenges.erase(pending);
			result.status = xucrun::broker::ApprovalChallengeStatus::Failed;
			result.windowsError = ERROR_TIMEOUT;
		}
		else if (pending->helperStartMode ==
			xucrun::broker::ApprovalHelperStartMode::ClientRunAsRequired &&
			!IsClientStartedApprovalHelperAccountAuthorized(helperSid, error))
		{
			result.status = xucrun::broker::ApprovalChallengeStatus::Denied;
			result.windowsError = error == NO_ERROR ? ERROR_ACCESS_DENIED : error;
		}
		else if (!pending->approvalAccountSid.empty() &&
			_wcsicmp(pending->approvalAccountSid.c_str(), helperSid.c_str()) != 0)
		{
			result.status = xucrun::broker::ApprovalChallengeStatus::Denied;
			result.windowsError = ERROR_ACCESS_DENIED;
		}
		else if (pending->clientAccountPresenceConfirmation &&
			_wcsicmp(pending->clientSid.c_str(), helperSid.c_str()) != 0)
		{
			result.status = xucrun::broker::ApprovalChallengeStatus::Denied;
			result.windowsError = ERROR_ACCESS_DENIED;
		}
		else if (!pending->userConfirmationRequired || pending->helperDataRetrieved ||
			pending->helperStartMode ==
				xucrun::broker::ApprovalHelperStartMode::None ||
			pending->clientSessionId != helperSessionId ||
			(pending->helperStartMode ==
				xucrun::broker::ApprovalHelperStartMode::BrokerStarted &&
			 ((pending->helperProcessId != 0 &&
			   pending->helperProcessId != helperProcessId) ||
			  (pending->helperProcessId == 0 &&
			   !IsRunningSecureHostChild(pending->secureHostProcess,
				pending->secureHostProcessId, helperProcessId)))) ||
			(pending->helperStartMode ==
				xucrun::broker::ApprovalHelperStartMode::ClientRunAsRequired &&
			 pending->helperProcessId != 0 &&
			 pending->helperProcessId != helperProcessId) ||
			std::any_of(pendingChallenges.begin(), pendingChallenges.end(),
				[&pending, helperProcessId](const PendingApprovalChallenge& value) {
					return &value != &*pending &&
						value.helperProcessId == helperProcessId;
				}))
		{
			result.status = xucrun::broker::ApprovalChallengeStatus::Denied;
			result.windowsError = ERROR_ACCESS_DENIED;
		}
		else
		{
			AccountIdentity clientAccount;
			AccountIdentity account;
			if (!ResolveUserAccountIdentity(pending->clientSid, clientAccount, error) ||
				!ResolveUserAccountIdentity(pending->accountSid, account, error))
			{
				result.status = xucrun::broker::ApprovalChallengeStatus::Failed;
				result.windowsError = error;
			}
			else
			{
				if (!xucrun::authenticity_marker_store::GetOrCreate(
					pending->clientSid, result.authenticityMarker, error))
				{
					result.status = xucrun::broker::ApprovalChallengeStatus::Failed;
					result.windowsError = error == NO_ERROR ? ERROR_GEN_FAILURE : error;
				}
				else
				{
					const ULONGLONG now = GetTickCount64();
					pending->helperProcessId = helperProcessId;
					pending->helperDataRetrieved = true;
					pending->helperAbsoluteExpiresAt = now +
						xucrun::broker::ApprovalHelperMaximumLifetimeMilliseconds;
					pending->expiresAt = now +
						xucrun::broker::ApprovalHelperDecisionLifetimeMilliseconds;
					result.status = xucrun::broker::ApprovalChallengeStatus::Issued;
					result.userConfirmationRequired = false;
					result.challengeId = pending->challengeId;
					result.canonicalClientName = clientAccount.canonicalName;
					result.canonicalAccountName = account.canonicalName;
					result.clientPath = pending->clientPath;
					std::copy_n(pending->clientSha256.begin(), result.clientSha256.size(),
						result.clientSha256.begin());
					result.clientTrustStatus = pending->clientTrustStatus;
					result.clientSignatureTrusted = pending->clientSignatureTrusted;
					result.finalExecutablePath = pending->executablePath;
					result.executableSha256 = pending->executableSha256;
					result.executableTrustStatus = pending->executableTrustStatus;
					result.executableSignatureTrusted = pending->executableSignatureTrusted;
					result.argumentTail = pending->argumentTail;
					result.workingDirectory = pending->workingDirectory;
					PopulateApprovalHelperDirectScriptBinding(*pending, result);
					result.elevationMode = pending->elevationMode;
					result.visibility = pending->visibility;
					result.helperStartMode = pending->helperStartMode;
					result.approvalHelperView = true;
					result.storedApprovalPresenceConfirmation =
						pending->storedApprovalPresenceConfirmation;
					result.writableElevatedTargetReason =
						pending->writableElevatedTargetReason;
					result.programUpdateAvailable = pending->programUpdate.available;
					if (pending->programUpdate.available)
					{
						result.changedProgramPath = pending->programUpdate.changedPath;
						result.oldProgramSha256 = pending->programUpdate.oldHash;
						result.newProgramSha256 = pending->programUpdate.newHash;
						result.affectedApprovalCount =
							pending->programUpdate.affectedApprovalCount;
						result.affectedAccountCount =
							pending->programUpdate.affectedAccountCount;
					}
				}
			}
		}
		const std::vector<std::uint8_t> response =
			xucrun::broker::SerializeApprovalChallengeResult(result);
		RunOverlappedIo(pipe, stopEvent, true, const_cast<std::uint8_t*>(response.data()),
			static_cast<DWORD>(response.size()), transferred);
	}

	// Renews a helper lease without extending the absolute lifetime of the approval challenge.
	void HandleApprovalHelperLeaseRequest(HANDLE pipe, HANDLE stopEvent,
		DWORD helperProcessId, DWORD helperSessionId, DWORD helperIntegrityRid,
		const ClientImageIdentity& helperIdentity,
		const xucrun::broker::ApprovalHelperRequest& request,
		std::deque<PendingApprovalChallenge>& pendingChallenges,
		DWORD& transferred)
	{
		xucrun::broker::ApprovalHelperDecisionResult result;
		result.requestId = request.requestId;
		DWORD error = NO_ERROR;
		auto pending = std::find_if(pendingChallenges.begin(), pendingChallenges.end(),
			[&request](const PendingApprovalChallenge& value) {
				return value.challengeId == request.challengeId;
			});
		const ULONGLONG now = GetTickCount64();
		if (!IsApprovalHelperAuthorized(helperIdentity, helperIntegrityRid, error))
		{
			result.status = xucrun::broker::ApprovalConfirmationStatus::Denied;
			result.windowsError = error;
		}
		else if (pending == pendingChallenges.end())
		{
			result.status = xucrun::broker::ApprovalConfirmationStatus::InvalidRequest;
			result.windowsError = ERROR_NOT_FOUND;
		}
		else if (pending->expiresAt <= now || pending->helperAbsoluteExpiresAt <= now)
		{
			CancelSecureHost(pending->secureHostCancellationSignal);
			pendingChallenges.erase(pending);
			result.status = xucrun::broker::ApprovalConfirmationStatus::Expired;
			result.windowsError = ERROR_TIMEOUT;
		}
		else if (!pending->helperDataRetrieved || pending->helperDecisionRecorded ||
			pending->helperProcessId != helperProcessId ||
			pending->clientSessionId != helperSessionId)
		{
			result.status = xucrun::broker::ApprovalConfirmationStatus::Denied;
			result.windowsError = ERROR_ACCESS_DENIED;
		}
		else
		{
			pending->expiresAt = (std::min)(
				now + xucrun::broker::ApprovalHelperDecisionLifetimeMilliseconds,
				pending->helperAbsoluteExpiresAt);
			result.status = xucrun::broker::ApprovalConfirmationStatus::Confirmed;
			result.windowsError = NO_ERROR;
		}
		const std::vector<std::uint8_t> response =
			xucrun::broker::SerializeApprovalHelperLeaseResult(result);
		RunOverlappedIo(pipe, stopEvent, true, const_cast<std::uint8_t*>(response.data()),
			static_cast<DWORD>(response.size()), transferred);
	}

	// Records exactly one decision from the helper process associated with the challenge.
	void HandleApprovalHelperDecisionRequest(HANDLE pipe, HANDLE stopEvent,
		DWORD helperProcessId, DWORD helperSessionId, DWORD helperIntegrityRid,
		const ClientImageIdentity& helperIdentity,
		const xucrun::broker::ApprovalHelperDecisionRequest& request,
		std::deque<PendingApprovalChallenge>& pendingChallenges,
		DWORD& transferred)
	{
		xucrun::broker::ApprovalHelperDecisionResult result;
		result.requestId = request.requestId;
		DWORD error = NO_ERROR;
		auto pending = std::find_if(pendingChallenges.begin(), pendingChallenges.end(),
			[&request](const PendingApprovalChallenge& value) {
				return value.challengeId == request.challengeId;
			});
		if (!IsApprovalHelperAuthorized(helperIdentity, helperIntegrityRid, error))
		{
			result.status = xucrun::broker::ApprovalConfirmationStatus::Denied;
			result.windowsError = error;
		}
		else if (pending == pendingChallenges.end())
		{
			result.status = xucrun::broker::ApprovalConfirmationStatus::InvalidRequest;
			result.windowsError = ERROR_NOT_FOUND;
		}
		else if (pending->expiresAt <= GetTickCount64())
		{
			pendingChallenges.erase(pending);
			result.status = xucrun::broker::ApprovalConfirmationStatus::Expired;
			result.windowsError = ERROR_TIMEOUT;
		}
		else if (!pending->userConfirmationRequired ||
			pending->clientSessionId != helperSessionId ||
			pending->helperProcessId != helperProcessId ||
			pending->helperDecisionRecorded ||
			(pending->storedApprovalPresenceConfirmation &&
			 !IsSingleUseApprovalDecision(request)) ||
			(pending->writableElevatedTargetReason !=
				xucrun::broker::ApprovalFailureReason::None &&
			 !IsSingleUseApprovalDecision(request)) ||
			(request.updateScope != xucrun::broker::ApprovalUpdateScope::None &&
			 !pending->programUpdate.available) ||
			request.broadInterpreterApprovalConfirmed !=
				RequiresBroadInterpreterApprovalConfirmation(*pending, request))
		{
			result.status = xucrun::broker::ApprovalConfirmationStatus::Denied;
			result.windowsError = ERROR_ACCESS_DENIED;
		}
		else
		{
			pending->helperDecision = request;
			pending->helperDecisionRecorded = true;
			const bool denied = request.decision == xucrun::broker::ApprovalDecision::Deny;
			result.status = denied
				? xucrun::broker::ApprovalConfirmationStatus::Denied
				: xucrun::broker::ApprovalConfirmationStatus::Confirmed;
			result.windowsError = denied ? ERROR_CANCELLED : NO_ERROR;
			if (denied)
				pendingChallenges.erase(pending);
		}
		const std::vector<std::uint8_t> response =
			xucrun::broker::SerializeApprovalHelperDecisionResult(result);
		RunOverlappedIo(pipe, stopEvent, true, const_cast<std::uint8_t*>(response.data()),
			static_cast<DWORD>(response.size()), transferred);
	}

// Consumes a challenge once, revalidates its bindings, and launches the confirmed target.
	void HandleApprovalConfirmationRequest(HANDLE pipe, HANDLE stopEvent,
		HANDLE clientProcess, const ClientImageIdentity& imageIdentity,
		const std::wstring& clientSid, const std::wstring& clientLogonSid,
		DWORD clientSessionId,
		const xucrun::broker::ApprovalConfirmationRequest& approvalConfirmationRequest,
		std::deque<PendingApprovalChallenge>& pendingApprovalChallenges,
		std::vector<TrackedElevatedProcess>& trackedElevatedProcesses,
		std::vector<TrackedElevatedProcessJob>& trackedElevatedProcessJobs,
		BrokerLogCallback log, DWORD& transferred)
	{
		xucrun::broker::ApprovalConfirmationResult confirmationResult;
		confirmationResult.requestId = approvalConfirmationRequest.requestId;
		DWORD confirmationError = NO_ERROR;
		if (!IsSensitiveClientAuthorized(imageIdentity, confirmationError))
		{
			confirmationResult.status = xucrun::broker::ApprovalConfirmationStatus::Denied;
			confirmationResult.windowsError = confirmationError;
		}
		else
		{
			const DWORD clientProcessId = GetProcessId(clientProcess);
			const auto pendingIterator = std::find_if(
				pendingApprovalChallenges.begin(), pendingApprovalChallenges.end(),
				[&approvalConfirmationRequest](const PendingApprovalChallenge& pending) {
					return pending.challengeId == approvalConfirmationRequest.challengeId;
				});
			if (pendingIterator == pendingApprovalChallenges.end())
			{
				confirmationResult.status =
					xucrun::broker::ApprovalConfirmationStatus::InvalidRequest;
				confirmationResult.windowsError = ERROR_NOT_FOUND;
			}
			else
			{
				PendingApprovalChallenge pending = std::move(*pendingIterator);
				pendingApprovalChallenges.erase(pendingIterator);
				xucrun::broker::ApprovalConfirmationRequest effectiveRequest =
					approvalConfirmationRequest;
				if (pending.helperDecisionRecorded)
				{
					effectiveRequest.decision = pending.helperDecision.decision;
					effectiveRequest.persistence = pending.helperDecision.persistence;
					effectiveRequest.programScope = pending.helperDecision.programScope;
					effectiveRequest.accountScope = pending.helperDecision.accountScope;
					effectiveRequest.updateScope = pending.helperDecision.updateScope;
				}
				if (pending.expiresAt <= GetTickCount64())
				{
					CancelSecureHost(pending.secureHostCancellationSignal);
					confirmationResult.status =
						xucrun::broker::ApprovalConfirmationStatus::Expired;
					confirmationResult.windowsError = ERROR_TIMEOUT;
				}
				else if (!MatchesPendingChallengeClient(pending,
					clientSid, clientLogonSid, clientProcessId,
					clientSessionId, imageIdentity))
				{
					confirmationResult.status =
						xucrun::broker::ApprovalConfirmationStatus::Denied;
					confirmationResult.windowsError = ERROR_ACCESS_DENIED;
				}
				else if (pending.userConfirmationRequired &&
					!pending.helperDecisionRecorded)
				{
					confirmationResult.status =
						xucrun::broker::ApprovalConfirmationStatus::Denied;
					confirmationResult.windowsError = ERROR_ACCESS_DENIED;
				}
				else if (effectiveRequest.decision ==
					xucrun::broker::ApprovalDecision::Deny)
				{
					confirmationResult.status =
						xucrun::broker::ApprovalConfirmationStatus::Denied;
					confirmationResult.windowsError = ERROR_CANCELLED;
				}
				else if (!pending.userConfirmationRequired &&
					effectiveRequest.persistence !=
						xucrun::broker::ApprovalPersistence::Once)
				{
					confirmationResult.status =
						xucrun::broker::ApprovalConfirmationStatus::Denied;
					confirmationResult.windowsError = ERROR_INVALID_DATA;
				}
				else if (effectiveRequest.updateScope !=
					xucrun::broker::ApprovalUpdateScope::None &&
					!pending.programUpdate.available)
				{
					confirmationResult.status =
						xucrun::broker::ApprovalConfirmationStatus::Denied;
					confirmationResult.windowsError = ERROR_INVALID_DATA;
				}
				else
				{
					AccountIdentity accountIdentity;
					LaunchObjectLocks launchObjectLocks;
					if (!ResolveUserAccountIdentity(
						pending.accountSid, accountIdentity, confirmationError) ||
						accountIdentity.sid != pending.accountSid ||
						(pending.elevationMode ==
							xucrun::broker::ElevationMode::RequireElevated &&
						 ((pending.writableElevatedTargetReason ==
							xucrun::broker::ApprovalFailureReason::None &&
						   !ValidateElevatedLaunchPaths(clientProcess, pending.executablePath,
							   confirmationResult.failureReason,
							   confirmationError)) ||
						  (pending.writableElevatedTargetReason !=
							xucrun::broker::ApprovalFailureReason::None &&
						   !ValidateElevatedLaunchDirectory(clientProcess,
							   pending.executablePath,
							   confirmationResult.failureReason,
							   confirmationError)))) ||
						!LockAndValidateDirectScript(
							pending, launchObjectLocks, confirmationError) ||
						!LockAndValidateLaunchObjects(
							pending, launchObjectLocks, confirmationResult.failureReason,
							confirmationError))
					{
						confirmationResult.status =
							xucrun::broker::ApprovalConfirmationStatus::Denied;
						confirmationResult.windowsError = confirmationError == NO_ERROR
							? ERROR_FILE_INVALID
							: confirmationError;
					}
					else
					{
						std::wstring credentialTarget;
						std::vector<std::uint8_t> storedPassword;
						xucrun::SensitiveString password;
						std::wstring failedStage;
						std::wstring effectiveWorkingDirectory;
						DWORD launchedProcessId = 0;
						xucrun::approval::BrokerApprovalValue storedApproval;
						bool approvalStored = false;
						if (!ResolveCredentialTarget(clientSid, accountIdentity.sid,
							credentialTarget, confirmationError) ||
							!xucrun::broker_store::Read(
								credentialTarget, storedPassword, confirmationError))
						{
							confirmationResult.status =
								xucrun::broker::ApprovalConfirmationStatus::Failed;
							confirmationResult.windowsError = confirmationError;
						}
						else if (!AssignStoredPassword(storedPassword, password))
						{
							confirmationResult.status =
								xucrun::broker::ApprovalConfirmationStatus::Failed;
							confirmationResult.windowsError = ERROR_INVALID_DATA;
						}
						else if (effectiveRequest.updateScope !=
							xucrun::broker::ApprovalUpdateScope::None)
						{
							std::uint32_t updatedCount = 0;
							xucrun::approval::BrokerApprovalValue updateContext;
							if (!BuildApprovalContext(pending, updateContext) ||
								!xucrun::approval_store::ApplyProgramUpdate(
									updateContext, pending.programUpdate,
									effectiveRequest.updateScope ==
										xucrun::broker::ApprovalUpdateScope::AllAccounts,
									updatedCount, confirmationError))
							{
								confirmationResult.status =
									xucrun::broker::ApprovalConfirmationStatus::Failed;
								confirmationResult.windowsError = confirmationError;
							}
							else if (log)
							{
								log(EVENTLOG_INFORMATION_TYPE,
									std::to_wstring(updatedCount) +
									L" approval(s) updated after confirmed program update: " +
									SanitizeLogValue(pending.programUpdate.changedPath));
							}
						}
						else if (!StoreApproval(pending, effectiveRequest,
							storedApproval, approvalStored, confirmationError))
						{
							confirmationResult.status =
								xucrun::broker::ApprovalConfirmationStatus::Failed;
							confirmationResult.windowsError = confirmationError;
						}
						if (confirmationResult.windowsError == NO_ERROR)
						{
							if (LaunchApprovedCredentialProcess(clientProcess,
								accountIdentity, password,
								clientSessionId, pending.clientPath, pending.executablePath,
								pending.argumentTail, pending.workingDirectory,
								pending.elevationMode, pending.visibility, launchedProcessId,
								effectiveWorkingDirectory,
								confirmationError, failedStage))
							{
								confirmationResult.status =
									xucrun::broker::ApprovalConfirmationStatus::Confirmed;
								confirmationResult.processId = launchedProcessId;
								if (pending.elevationMode ==
									xucrun::broker::ElevationMode::RequireElevated)
								{
									HANDLE trackedProcess = OpenProcess(
										PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE,
										FALSE, launchedProcessId);
									if (trackedProcess)
									{
										PruneTrackedElevatedProcesses(trackedElevatedProcesses);
										xucrun::broker::RequestNonce trackingId = {};
										if (GenerateTrackingId(trackedElevatedProcesses, trackingId))
										{
											std::wstring trackedLogonSid;
											DWORD jobError = NO_ERROR;
											const bool jobCreated = GetProcessLogonSid(
												trackedProcess, trackedLogonSid, jobError) &&
												CreateTrackedProcessJob(trackedProcess, launchedProcessId,
													clientSessionId, trackedLogonSid, true,
													trackedElevatedProcessJobs, jobError);
											if (!jobCreated && log)
											{
												log(EVENTLOG_WARNING_TYPE,
													L"Descendants of the elevated process cannot be monitored. PID " +
													std::to_wstring(launchedProcessId) +
													L", error " + std::to_wstring(jobError));
											}
											else if (jobCreated && log)
											{
												log(EVENTLOG_INFORMATION_TYPE,
													L"Descendant monitoring enabled for broker process. PID " +
													std::to_wstring(launchedProcessId));
											}
											trackedElevatedProcesses.push_back({ trackedProcess,
												launchedProcessId, clientSessionId, trackingId,
												GetProcessBaseName(pending.executablePath) });
										}
										else
										{
											CloseHandle(trackedProcess);
											if (log)
												log(EVENTLOG_WARNING_TYPE,
													L"Tracking ID could not be generated.");
										}
									}
									else if (log)
									{
										log(EVENTLOG_WARNING_TYPE,
											L"Elevated process could not be monitored. PID " +
											std::to_wstring(launchedProcessId) + L", error " +
											std::to_wstring(GetLastError()));
									}
								}
								if (log)
								{
									if (pending.writableElevatedTargetReason !=
										xucrun::broker::ApprovalFailureReason::None)
									{
										log(EVENTLOG_WARNING_TYPE,
											L"Writable elevated target approved through helper for client SID " +
											pending.clientSid + L" for target " +
											SanitizeLogValue(pending.executablePath) +
											L" (file writable). PID " +
											std::to_wstring(launchedProcessId));
									}
									if (_wcsicmp(effectiveWorkingDirectory.c_str(),
										pending.workingDirectory.c_str()) != 0)
									{
										log(EVENTLOG_INFORMATION_TYPE,
											L"Working directory fallback from " +
											SanitizeLogValue(pending.workingDirectory) + L" to " +
											SanitizeLogValue(effectiveWorkingDirectory) + L".");
									}
									log(EVENTLOG_INFORMATION_TYPE,
										L"One-time approval challenge confirmed and consumed; "
										L"bound process started. PID " +
										std::to_wstring(launchedProcessId));
								}
							}
							else
							{
								if (approvalStored)
								{
									DWORD rollbackError = NO_ERROR;
									if (!xucrun::approval_store::DeleteExact(
										storedApproval, rollbackError) && log)
									{
										log(EVENTLOG_ERROR_TYPE,
											L"Approval rollback after failed launch failed. Error " +
											std::to_wstring(rollbackError));
									}
								}
								confirmationResult.status =
									xucrun::broker::ApprovalConfirmationStatus::Failed;
								confirmationResult.windowsError = confirmationError;
								if (log)
									log(EVENTLOG_ERROR_TYPE,
										L"Confirmed credential launch failed at stage " +
										failedStage + L". Error " +
										std::to_wstring(confirmationError));
							}
						}
						password.clear();
						EraseSensitiveBytes(storedPassword);
					}
				}
			}
		}
		std::vector<std::uint8_t> response =
			xucrun::broker::SerializeApprovalConfirmationResult(confirmationResult);
		RunOverlappedIo(pipe, stopEvent, true, response.data(),
			static_cast<DWORD>(response.size()), transferred);
	}

// Issues a short-lived challenge and starts its fixed approval helper when required.
	void HandleApprovalChallengeRequest(HANDLE pipe, HANDLE stopEvent,
		HANDLE clientProcess, DWORD clientProcessId, const ClientImageIdentity& imageIdentity,
		const std::wstring& clientSid,
		const std::wstring& clientLogonSid, DWORD clientSessionId,
		const xucrun::broker::ApprovalChallengeRequest& request,
		std::deque<PendingApprovalChallenge>& pendingChallenges,
		std::deque<PendingCredentialCaptureChallenge>& pendingCredentialChallenges,
		BrokerLogCallback log, DWORD& transferred)
	{
		xucrun::broker::ApprovalChallengeResult result;
		result.requestId = request.requestId;
		result.elevationMode = request.elevationMode;
		result.visibility = request.visibility;
		DWORD challengeError = NO_ERROR;
		AccountIdentity clientAccountIdentity;
		AccountIdentity accountIdentity;
		AccountIdentity approvalAccountIdentity;
		TargetImageIdentity targetIdentity;
		ClientImageIdentity targetImageIdentity;
		std::wstring finalWorkingDirectory;
		std::wstring displayedScriptPath;
		std::wstring boundScriptPath;
		std::array<std::uint8_t, 32> boundScriptHash = {};
		bool diagnosticProfile = false;
		bool trustedAdministrativeClientLaunch = false;
		bool clientIsAdministrator = false;
		bool clientIsHighIntegrity = false;
		bool targetIsAdministrator = false;
		xucrun::broker::ApprovalFailureReason writableElevatedTargetReason =
			xucrun::broker::ApprovalFailureReason::None;
		PrunePendingApprovalChallenges(pendingChallenges);
		PrunePendingCredentialCaptureChallenges(pendingCredentialChallenges);
		if (!IsSensitiveClientAuthorized(imageIdentity, challengeError))
		{
			result.status = xucrun::broker::ApprovalChallengeStatus::Denied;
			result.windowsError = challengeError;
		}
		else if (!ResolveUserAccountIdentity(clientSid,
			clientAccountIdentity, challengeError) ||
			!ResolveUserAccountIdentity(
				request.requestedAccountSid, accountIdentity, challengeError))
		{
			result.status = xucrun::broker::ApprovalChallengeStatus::InvalidRequest;
			result.windowsError = challengeError == NO_ERROR
				? ERROR_INVALID_DATA
				: challengeError;
		}
		else if (
			!ReadTargetImageIdentity(request.executablePath, targetIdentity, challengeError) ||
			!ReadFileImageIdentity(
				targetIdentity.path, targetImageIdentity, challengeError, false) ||
			targetImageIdentity.sha256 != targetIdentity.sha256 ||
			targetIdentity.sha256.size() != result.executableSha256.size() ||
			!ResolveWorkingDirectory(request.workingDirectory,
				finalWorkingDirectory, challengeError))
		{
			result.status = xucrun::broker::ApprovalChallengeStatus::InvalidRequest;
			result.windowsError = challengeError == NO_ERROR
				? ERROR_INVALID_DATA
				: challengeError;
		}
		else if ([&]() {
			diagnosticProfile = IsApprovalDiagnosticRequestAllowed(
				request, targetIdentity, finalWorkingDirectory, challengeError);
			trustedAdministrativeClientLaunch = IsTrustedAdministrativeClientLaunch(
				request, imageIdentity, targetIdentity);
			challengeError = NO_ERROR;
			return request.elevationMode ==
					xucrun::broker::ElevationMode::RequireElevated &&
				!ValidateElevatedLaunchPathsForChallenge(clientProcess, targetIdentity.path,
					writableElevatedTargetReason, result.failureReason, challengeError);
		}())
		{
			result.status = xucrun::broker::ApprovalChallengeStatus::Denied;
			result.windowsError = challengeError == NO_ERROR
				? ERROR_ACCESS_DENIED : challengeError;
		}
		else if (trustedAdministrativeClientLaunch &&
			writableElevatedTargetReason !=
				xucrun::broker::ApprovalFailureReason::None)
		{
			result.status = xucrun::broker::ApprovalChallengeStatus::Denied;
			result.windowsError = ERROR_ACCESS_DENIED;
			result.failureReason = writableElevatedTargetReason;
		}
		else if (HasPendingSecureDesktopChallenge(pendingChallenges,
			pendingCredentialChallenges, clientSessionId))
		{
			result.status = xucrun::broker::ApprovalChallengeStatus::Failed;
			result.windowsError = ERROR_BUSY;
		}
		else if (!HasPendingApprovalChallengeCapacity(pendingChallenges,
			clientSid, clientSessionId, clientProcessId, result.failureReason))
		{
			result.status = xucrun::broker::ApprovalChallengeStatus::Failed;
			result.windowsError = ERROR_TOO_MANY_CMDS;
		}
		else if (!GenerateChallengeId(pendingChallenges, result.challengeId))
		{
			result.status = xucrun::broker::ApprovalChallengeStatus::Failed;
			result.windowsError = ERROR_GEN_FAILURE;
		}
		else
		{
			std::array<std::uint8_t, 32> displayedScriptHash = {};
			ResolveDirectScriptBinding(clientProcess, targetIdentity.path,
				request.argumentTail, finalWorkingDirectory, displayedScriptPath,
				displayedScriptHash,
				boundScriptPath, boundScriptHash);
			result.status = xucrun::broker::ApprovalChallengeStatus::Issued;
			result.canonicalClientName = clientAccountIdentity.canonicalName;
			result.canonicalAccountName = accountIdentity.canonicalName;
			result.clientPath = imageIdentity.path;
			std::copy_n(imageIdentity.sha256.begin(), result.clientSha256.size(),
				result.clientSha256.begin());
			result.clientTrustStatus = imageIdentity.trustStatus;
			result.clientSignatureTrusted = imageIdentity.signatureTrusted;
			result.finalExecutablePath = targetIdentity.path;
			std::copy(targetIdentity.sha256.begin(), targetIdentity.sha256.end(),
				result.executableSha256.begin());
			result.executableTrustStatus = targetImageIdentity.trustStatus;
			result.executableSignatureTrusted = targetImageIdentity.signatureTrusted;
			result.argumentTail = request.argumentTail;
			result.workingDirectory = finalWorkingDirectory;
			result.directScriptPath = displayedScriptPath;
			result.directScriptSha256 = boundScriptHash;
			result.writableElevatedTargetReason = writableElevatedTargetReason;

			PendingApprovalChallenge pending;
			pending.challengeId = result.challengeId;
			pending.expiresAt = GetTickCount64() +
				xucrun::broker::ApprovalChallengeLifetimeMilliseconds;
			pending.clientSid = clientSid;
			pending.clientLogonSid = clientLogonSid;
			pending.clientProcessId = clientProcessId;
			pending.clientSessionId = clientSessionId;
			pending.accountSid = accountIdentity.sid;
			pending.approvalAccountSid = request.approvalAccountSid;
			pending.clientPath = imageIdentity.path;
			pending.clientSha256 = imageIdentity.sha256;
			pending.clientTrustStatus = imageIdentity.trustStatus;
			pending.clientSignatureTrusted = imageIdentity.signatureTrusted;
			pending.executablePath = result.finalExecutablePath;
			pending.executableSha256 = result.executableSha256;
			pending.executableTrustStatus = targetImageIdentity.trustStatus;
			pending.executableSignatureTrusted = targetImageIdentity.signatureTrusted;
			pending.argumentTail = result.argumentTail;
			pending.workingDirectory = result.workingDirectory;
			pending.directScriptPath = displayedScriptPath;
			pending.directScriptSha256 = displayedScriptHash;
			pending.scriptPath = boundScriptPath;
			pending.scriptSha256 = boundScriptHash;
			pending.elevationMode = result.elevationMode;
			pending.visibility = result.visibility;
			pending.writableElevatedTargetReason = writableElevatedTargetReason;
			pending.diagnosticProfile = diagnosticProfile;
			pending.trustedAdministrativeClientLaunch =
				trustedAdministrativeClientLaunch;
			bool storedApprovalMatches = false;
			if (!ComputeUtf16Sha256(pending.argumentTail, pending.argumentHash))
			{
				result = {};
				result.requestId = request.requestId;
				result.status = xucrun::broker::ApprovalChallengeStatus::Failed;
				result.windowsError = ERROR_GEN_FAILURE;
			}
			else if (!HasStoredApproval(pending, storedApprovalMatches, challengeError))
			{
				result = {};
				result.requestId = request.requestId;
				result.status = xucrun::broker::ApprovalChallengeStatus::Failed;
				result.windowsError = challengeError;
			}
			else
			{
				bool clientMembershipResolved = false;
				bool clientIntegrityResolved = false;
				bool targetMembershipResolved = false;
				bool approvalAccountAvailable = false;
				DWORD membershipError = NO_ERROR;
				clientMembershipResolved = IsAccountMemberOfAdministrators(
					clientAccountIdentity, clientIsAdministrator, membershipError);
				DWORD integrityError = NO_ERROR;
				clientIsHighIntegrity = HasHighIntegrity(clientProcess, integrityError);
				clientIntegrityResolved = clientIsHighIntegrity ||
					integrityError == ERROR_ELEVATION_REQUIRED;
				targetMembershipResolved = IsAccountMemberOfAdministrators(
					accountIdentity, targetIsAdministrator, membershipError);
				if (trustedAdministrativeClientLaunch)
				{
					storedApprovalMatches = targetMembershipResolved &&
						targetIsAdministrator;
					if (!storedApprovalMatches)
					{
						result = {};
						result.requestId = request.requestId;
						result.status =
							xucrun::broker::ApprovalChallengeStatus::Denied;
						result.windowsError = targetMembershipResolved
							? ERROR_ACCESS_DENIED : membershipError;
					}
				}
				if (!request.approvalAccountSid.empty())
				{
					bool approvalIsAdministrator = false;
					DWORD approvalError = NO_ERROR;
					std::wstring credentialTarget;
					approvalAccountAvailable =
						ResolveUserAccountIdentity(request.approvalAccountSid,
							approvalAccountIdentity, approvalError) &&
						IsAccountMemberOfAdministrators(approvalAccountIdentity,
							approvalIsAdministrator, approvalError) &&
						approvalIsAdministrator &&
						_wcsicmp(approvalAccountIdentity.sid.c_str(),
							accountIdentity.sid.c_str()) != 0 &&
						ResolveCredentialTarget(clientSid, approvalAccountIdentity.sid,
							credentialTarget, approvalError);
				}
				if (storedApprovalMatches)
				{
					if (!clientMembershipResolved || !clientIntegrityResolved ||
						!targetMembershipResolved)
						storedApprovalMatches = false;
					const bool standardTargetPresenceRequired =
						clientMembershipResolved && !clientIsHighIntegrity &&
						targetMembershipResolved && !targetIsAdministrator &&
						pending.elevationMode ==
							xucrun::broker::ElevationMode::PreserveToken;
					const bool approvalAccountAllowed =
						standardTargetPresenceRequired && !clientIsAdministrator
						? approvalAccountAvailable &&
							_wcsicmp(approvalAccountIdentity.sid.c_str(),
								accountIdentity.sid.c_str()) != 0
						: request.approvalAccountSid.empty();
					if (!approvalAccountAllowed)
						storedApprovalMatches = false;
				}
				xucrun::approval::BrokerApprovalValue updateContext;
				FILETIME updateNow = {};
				GetSystemTimeAsFileTime(&updateNow);
				ULARGE_INTEGER updateTimestamp = {};
				updateTimestamp.LowPart = updateNow.dwLowDateTime;
				updateTimestamp.HighPart = updateNow.dwHighDateTime;
				if (!storedApprovalMatches &&
					(!BuildApprovalContext(pending, updateContext) ||
					 !xucrun::approval_store::FindProgramUpdate(updateContext,
						 updateTimestamp.QuadPart, pending.programUpdate, challengeError)))
				{
					result = {};
					result.requestId = request.requestId;
					result.status = xucrun::broker::ApprovalChallengeStatus::Failed;
					result.windowsError = challengeError;
				}
				else if (!storedApprovalMatches && pending.programUpdate.available)
				{
					if (pending.programUpdate.affectsAllAccounts)
					{
						std::vector<StoredCredentialMetadata> credentialEntries;
						DWORD metadataError = NO_ERROR;
						if (!ListStoredCredentialMetadata(
							pending.clientSid, credentialEntries, metadataError))
						{
							result = {};
							result.requestId = request.requestId;
							result.status =
								xucrun::broker::ApprovalChallengeStatus::Failed;
							result.windowsError = metadataError;
						}
						else
						{
							pending.programUpdate.affectedAccountCount =
								static_cast<std::uint32_t>(credentialEntries.size());
						}
					}
					if (result.status != xucrun::broker::ApprovalChallengeStatus::Issued)
					{
						pending.programUpdate = {};
					}
					else
					{
						result.programUpdateAvailable = true;
						result.changedProgramPath = pending.programUpdate.changedPath;
						result.oldProgramSha256 = pending.programUpdate.oldHash;
						result.newProgramSha256 = pending.programUpdate.newHash;
						result.affectedApprovalCount =
							pending.programUpdate.affectedApprovalCount;
						result.affectedAccountCount =
							pending.programUpdate.affectedAccountCount;
					}
				}
				pending.storedApprovalPresenceConfirmation =
					RequiresStoredApprovalPresenceConfirmation(storedApprovalMatches,
						clientIntegrityResolved, clientIsHighIntegrity,
						targetMembershipResolved, targetIsAdministrator,
						pending.elevationMode) ||
					RequiresStandardTargetPresenceConfirmation(storedApprovalMatches,
						clientMembershipResolved, clientIsAdministrator,
						clientIntegrityResolved, clientIsHighIntegrity,
						targetMembershipResolved, targetIsAdministrator,
						approvalAccountAvailable, pending.elevationMode);
				pending.clientAccountPresenceConfirmation =
					RequiresClientAccountPresenceConfirmation(
						pending.storedApprovalPresenceConfirmation,
						clientMembershipResolved, clientIsAdministrator,
						clientIntegrityResolved, clientIsHighIntegrity);
				pending.userConfirmationRequired = !storedApprovalMatches ||
					pending.storedApprovalPresenceConfirmation ||
					pending.writableElevatedTargetReason !=
						xucrun::broker::ApprovalFailureReason::None;
				result.userConfirmationRequired = pending.userConfirmationRequired;
				result.storedApprovalPresenceConfirmation =
					pending.storedApprovalPresenceConfirmation;
				if (result.status == xucrun::broker::ApprovalChallengeStatus::Issued &&
					pending.userConfirmationRequired)
				{
					std::wstring failedStage;
					const bool useSeparateApprovalAccount =
						ShouldUseSeparateApprovalAccountForHelper(
							clientMembershipResolved, clientIsAdministrator,
							clientIntegrityResolved, clientIsHighIntegrity,
							targetMembershipResolved, targetIsAdministrator,
							approvalAccountAvailable, pending.elevationMode);
					const AccountIdentity& helperAccount =
						useSeparateApprovalAccount
						? approvalAccountIdentity : accountIdentity;
					if (!LaunchApprovalHelperForChallenge(clientProcess, helperAccount,
						pending, result.approvalHelperProcessHandle,
						challengeError, failedStage))
					{
						result = {};
						result.requestId = request.requestId;
						result.status =
							xucrun::broker::ApprovalChallengeStatus::Failed;
						result.windowsError = challengeError == NO_ERROR
							? ERROR_GEN_FAILURE : challengeError;
						if (log)
							log(EVENTLOG_ERROR_TYPE,
								L"Approval helper launch failed at stage " + failedStage +
								L". Error " + std::to_wstring(result.windowsError));
					}
					else
					{
						result.helperStartMode = pending.helperStartMode;
					}
				}
				if (result.status == xucrun::broker::ApprovalChallengeStatus::Issued)
					pendingChallenges.push_back(std::move(pending));
			}
			if (log && result.status ==
				xucrun::broker::ApprovalChallengeStatus::Issued)
			{
				const std::wstring sanitizedTarget =
					SanitizeLogValue(targetIdentity.path);
				if (writableElevatedTargetReason !=
					xucrun::broker::ApprovalFailureReason::None)
				{
					log(EVENTLOG_WARNING_TYPE,
						L"Writable elevated target approval requested by client SID " +
						clientSid + L" for target " + sanitizedTarget +
						L" (file writable).");
				}
				log(EVENTLOG_INFORMATION_TYPE,
					L"Short-lived approval challenge issued for client SID " + clientSid +
					L", target account " + accountIdentity.canonicalName + L", and target " +
					sanitizedTarget + (diagnosticProfile
						? L" using the fixed diagnostic profile."
						: L" using the production profile."));
			}
		}
		std::vector<std::uint8_t> response =
			xucrun::broker::SerializeApprovalChallengeResult(result);
		RunOverlappedIo(pipe, stopEvent, true, response.data(),
			static_cast<DWORD>(response.size()), transferred);
	}
