// Purpose: Internal request-handler implementation fragment for BrokerPipeServer.cpp.
//
// Inclusion contract:
// - This file is included exactly once, from inside BrokerPipeServer.cpp's anonymous namespace.
// - It deliberately uses the server's private identity, replay, challenge, tracking, and I/O helpers.
// - It is not a standalone translation unit and must not expose broker-internal state in public headers.
// - Handler behavior and protocol responses remain defined by the existing broker contracts.
// Returns exclusively account-related credential metadata to the authorized client.
	void HandleCredentialListRequest(HANDLE pipe, HANDLE stopEvent,
		const ClientImageIdentity& imageIdentity, const std::wstring& clientSid,
		const xucrun::broker::CredentialListRequest& request, DWORD& transferred)
	{
		xucrun::broker::CredentialListResult result;
		result.requestId = request.requestId;
		DWORD authorizationError = NO_ERROR;
		if (!IsSensitiveClientAuthorized(imageIdentity, authorizationError))
		{
			result.status = xucrun::broker::CredentialListStatus::InvalidRequest;
			result.windowsError = authorizationError;
		}
		else
		{
			std::vector<StoredCredentialMetadata> storedEntries;
			DWORD listError = NO_ERROR;
			if (!ListStoredCredentialMetadata(clientSid, storedEntries, listError))
			{
				result.status = xucrun::broker::CredentialListStatus::Failed;
				result.windowsError = listError;
			}
			else
			{
				result.status = xucrun::broker::CredentialListStatus::Succeeded;
				result.entries.reserve(storedEntries.size());
				for (StoredCredentialMetadata& entry : storedEntries)
				{
					result.entries.push_back({ std::move(entry.accountSid),
						std::move(entry.canonicalAccountName), entry.lastWritten });
				}
			}
		}

		std::vector<std::uint8_t> response =
			xucrun::broker::SerializeCredentialListResult(result);
		RunOverlappedIo(pipe, stopEvent, true, response.data(),
			static_cast<DWORD>(response.size()), transferred);
	}

// Deletes a credential only for a server-side resolved target account SID of the client.
	void HandleCredentialDeleteRequest(HANDLE pipe, HANDLE stopEvent,
		const ClientImageIdentity& imageIdentity, const std::wstring& clientSid,
		const xucrun::broker::CredentialDeleteRequest& request, DWORD& transferred)
	{
		xucrun::broker::CredentialDeleteResult result;
		result.requestId = request.requestId;
		DWORD authorizationError = NO_ERROR;
		if (!IsSensitiveClientAuthorized(imageIdentity, authorizationError))
		{
			result.status = xucrun::broker::CredentialDeleteStatus::InvalidRequest;
			result.windowsError = authorizationError;
		}
		else
		{
			AccountIdentity accountIdentity;
			DWORD deleteError = NO_ERROR;
			std::wstring credentialTarget;
			if (!ResolveUserAccountIdentity(request.requestedAccountSid,
				accountIdentity, deleteError) ||
				!ResolveCredentialTarget(
					clientSid, accountIdentity.sid, credentialTarget, deleteError))
			{
				result.status = xucrun::broker::CredentialDeleteStatus::InvalidRequest;
				result.windowsError = deleteError;
			}
			else
			{
				std::lock_guard storeLock(xucrun::broker_store::TransactionMutex());
				std::uint32_t deletedApprovalCount = 0;
				if (!xucrun::approval_store::DeleteForAccount(clientSid,
					accountIdentity.sid, deletedApprovalCount, deleteError))
				{
					result.status = xucrun::broker::CredentialDeleteStatus::Failed;
					result.windowsError = deleteError;
				}
				else
				{
					const bool credentialDeleted =
						xucrun::broker_store::Delete(credentialTarget, deleteError);
					const bool credentialMissing =
						!credentialDeleted && deleteError == ERROR_NOT_FOUND;
					if ((credentialDeleted || credentialMissing) &&
						DeleteCredentialMetadataBinding(
							clientSid, accountIdentity.sid, deleteError))
					{
						result.status = credentialDeleted
							? xucrun::broker::CredentialDeleteStatus::Succeeded
							: xucrun::broker::CredentialDeleteStatus::NotFound;
						result.windowsError = credentialDeleted ? NO_ERROR : ERROR_NOT_FOUND;
					}
					else
					{
						result.status = xucrun::broker::CredentialDeleteStatus::Failed;
						result.windowsError = deleteError;
					}
				}
			}
		}

		std::vector<std::uint8_t> response =
			xucrun::broker::SerializeCredentialDeleteResult(result);
		RunOverlappedIo(pipe, stopEvent, true, response.data(),
			static_cast<DWORD>(response.size()), transferred);
	}

// Validates a stored credential exclusively in the broker and does not return a password.
	void HandleCredentialUseTestRequest(HANDLE pipe, HANDLE stopEvent,
		HANDLE clientProcess, const ClientImageIdentity& imageIdentity,
		const std::wstring& clientSid, DWORD clientSessionId,
		const xucrun::broker::CredentialUseTestRequest& request,
		BrokerLogCallback log, DWORD& transferred)
	{
		xucrun::broker::CredentialUseTestResult result;
		result.requestId = request.requestId;
		DWORD authorizationError = NO_ERROR;
		if (!IsSensitiveClientAuthorized(imageIdentity, authorizationError))
		{
			result.status = xucrun::broker::CredentialUseTestStatus::InvalidRequest;
			result.windowsError = authorizationError;
		}
		else
		{
			AccountIdentity accountIdentity;
			DWORD useError = NO_ERROR;
			std::wstring credentialTarget;
			if (!ResolveUserAccountIdentity(request.requestedAccountSid,
				accountIdentity, useError) ||
				!ResolveCredentialTarget(clientSid, accountIdentity.sid,
					credentialTarget, useError))
			{
				result.status = xucrun::broker::CredentialUseTestStatus::InvalidRequest;
				result.windowsError = useError;
			}
			else
			{
				std::vector<std::uint8_t> storedPassword;
				xucrun::SensitiveString password;
				if (!xucrun::broker_store::Read(credentialTarget, storedPassword, useError))
				{
					result.status = useError == ERROR_NOT_FOUND
						? xucrun::broker::CredentialUseTestStatus::NotFound
						: xucrun::broker::CredentialUseTestStatus::Failed;
					result.windowsError = useError;
				}
				else if (!AssignStoredPassword(storedPassword, password))
				{
					result.status = xucrun::broker::CredentialUseTestStatus::Failed;
					result.windowsError = ERROR_INVALID_DATA;
				}
				else
				{
					std::wstring failedStage;
					DWORD launchedProcessId = 0;
					if (LaunchCredentialTestProcess(clientProcess, accountIdentity,
						password, clientSessionId, launchedProcessId, useError, failedStage))
					{
						result.processId = launchedProcessId;
						result.status = xucrun::broker::CredentialUseTestStatus::Succeeded;
					}
					else
					{
						result.status = IsCredentialRejectionError(useError)
							? xucrun::broker::CredentialUseTestStatus::InvalidCredentials
							: xucrun::broker::CredentialUseTestStatus::Failed;
						if (log)
							log(EVENTLOG_ERROR_TYPE,
								L"Credential test launch failed at stage " + failedStage +
								L". Error " + std::to_wstring(useError));
					}
					result.windowsError = useError;
				}
				password.clear();
				EraseSensitiveBytes(storedPassword);
			}
		}
		std::vector<std::uint8_t> response =
			xucrun::broker::SerializeCredentialUseTestResult(result);
		RunOverlappedIo(pipe, stopEvent, true, response.data(),
			static_cast<DWORD>(response.size()), transferred);
	}

// Validates and stores a credential; all password buffers remain in the broker.
	void HandleCredentialSaveRequest(HANDLE pipe, HANDLE stopEvent,
		const ClientImageIdentity& imageIdentity, const std::wstring& clientSid,
		xucrun::broker::CredentialSaveRequest& request,
		BrokerLogCallback log, DWORD& transferred)
	{
		xucrun::broker::CredentialSaveResult result;
		result.requestId = request.requestId;
		DWORD authorizationError = NO_ERROR;
		if (!IsSensitiveClientAuthorized(imageIdentity, authorizationError))
		{
			result.status = xucrun::broker::CredentialSaveStatus::InvalidRequest;
			result.windowsError = authorizationError;
			if (log)
				log(EVENTLOG_WARNING_TYPE,
					L"Credential save request from unauthorized client discarded. Error " +
					std::to_wstring(authorizationError));
		}
		else
		{
			AccountIdentity accountIdentity;
			DWORD accountIdentityError = NO_ERROR;
			if (!ResolveUserAccountIdentity(request.requestedAccountSid,
				accountIdentity, accountIdentityError))
			{
				result.status = xucrun::broker::CredentialSaveStatus::InvalidRequest;
				result.windowsError = accountIdentityError;
			}
			else
			{
				DWORD validationError = NO_ERROR;
				const CredentialValidationStatus validationStatus =
					ValidateAccountPassword(accountIdentity, request.password, validationError);
				result.windowsError = validationError;
				if (validationStatus == CredentialValidationStatus::Rejected)
				{
					result.status = xucrun::broker::CredentialSaveStatus::InvalidCredentials;
				}
				else if (validationStatus == CredentialValidationStatus::Failed)
				{
					result.status = xucrun::broker::CredentialSaveStatus::Failed;
				}
				else
				{
					std::wstring metadataPath;
					std::wstring credentialTarget;
					DWORD storeError = NO_ERROR;
					const auto* passwordBytes = reinterpret_cast<const std::uint8_t*>(
						request.password.c_str());
					const std::span<const std::uint8_t> password(
						passwordBytes, request.password.size() * sizeof(wchar_t));
					std::vector<std::uint8_t> storedPassword;
					if (EnsureCredentialMetadataBinding(clientSid, accountIdentity.sid,
						metadataPath, credentialTarget, storeError) &&
						xucrun::broker_store::Write(credentialTarget, password, storeError) &&
						xucrun::broker_store::Read(credentialTarget, storedPassword, storeError) &&
						storedPassword.size() == password.size() &&
						std::equal(storedPassword.begin(), storedPassword.end(), password.begin()))
					{
						result.status = xucrun::broker::CredentialSaveStatus::Succeeded;
						result.windowsError = NO_ERROR;
					}
					else
					{
						result.status = xucrun::broker::CredentialSaveStatus::Failed;
						if (storeError == NO_ERROR)
							storeError = ERROR_INVALID_DATA;
						result.windowsError = storeError;
						DWORD deleteError = NO_ERROR;
						xucrun::broker_store::Delete(credentialTarget, deleteError);
					}
					EraseSensitiveBytes(storedPassword);
				}
				request.password.clear();
				if (log)
					log(validationStatus == CredentialValidationStatus::Failed
						? EVENTLOG_ERROR_TYPE
						: EVENTLOG_INFORMATION_TYPE,
						L"Credential validation for target account " +
						accountIdentity.canonicalName + L" completed. Status " +
						std::to_wstring(static_cast<int>(validationStatus)) + L", error " +
						std::to_wstring(result.windowsError));
			}
		}
		request.password.clear();
		std::vector<std::uint8_t> response =
			xucrun::broker::SerializeCredentialSaveResult(result);
		RunOverlappedIo(pipe, stopEvent, true, response.data(),
			static_cast<DWORD>(response.size()), transferred);
	}

// Performs the isolated LocalSystem credential store round trip.
	void HandleCredentialStoreTestRequest(HANDLE pipe, HANDLE stopEvent,
		HANDLE clientProcess, const xucrun::broker::CredentialStoreTestRequest& request,
		BrokerLogCallback log, DWORD& transferred)
	{
		xucrun::broker::CredentialStoreTestResult result;
		result.requestId = request.requestId;
		DWORD storeError = NO_ERROR;
		std::wstring failedStage;
		if (RunCredentialStoreSelfTest(clientProcess,
			request.requestId, storeError, failedStage))
		{
			result.status = xucrun::broker::CredentialStoreTestStatus::Succeeded;
			if (log)
				log(EVENTLOG_INFORMATION_TYPE,
					L"LocalSystem credential store self-test completed successfully.");
		}
		else
		{
			result.status = xucrun::broker::CredentialStoreTestStatus::Failed;
			result.windowsError = storeError;
			if (log)
				log(EVENTLOG_ERROR_TYPE,
					L"LocalSystem credential store self-test failed at stage " +
					failedStage + L". Error " +
					std::to_wstring(storeError));
		}
		std::vector<std::uint8_t> response =
			xucrun::broker::SerializeCredentialStoreTestResult(result);
		RunOverlappedIo(pipe, stopEvent, true, response.data(),
			static_cast<DWORD>(response.size()), transferred);
	}

// Creates or validates the server-side derived SID binding for credential metadata.
	void HandleCredentialMetadataRequest(HANDLE pipe, HANDLE stopEvent,
		const std::wstring& clientSid,
		const xucrun::broker::CredentialMetadataRequest& request,
		BrokerLogCallback log, DWORD& transferred)
	{
		xucrun::broker::CredentialMetadataResult result;
		result.requestId = request.requestId;
		AccountIdentity accountIdentity;
		DWORD accountIdentityError = NO_ERROR;
		if (!ResolveUserAccountIdentity(
			request.requestedAccountSid, accountIdentity, accountIdentityError))
		{
			result.status = xucrun::broker::CredentialMetadataStatus::InvalidRequest;
			result.windowsError = accountIdentityError;
		}
		else
		{
			std::wstring metadataPath;
			std::wstring credentialTarget;
			DWORD metadataError = NO_ERROR;
			if (EnsureCredentialMetadataBinding(clientSid, accountIdentity.sid,
				metadataPath, credentialTarget, metadataError))
			{
				result.status = xucrun::broker::CredentialMetadataStatus::Succeeded;
				if (log)
					log(EVENTLOG_INFORMATION_TYPE,
						L"Credential metadata binding for client SID " + clientSid +
						L" and target account " + accountIdentity.canonicalName +
						L" verified: " + metadataPath);
			}
			else
			{
				result.status = xucrun::broker::CredentialMetadataStatus::Failed;
				result.windowsError = metadataError;
				if (log)
					log(EVENTLOG_ERROR_TYPE,
						L"Credential metadata binding for client SID " + clientSid +
						L" and target account SID " + accountIdentity.sid +
						L" failed. Error " + std::to_wstring(metadataError));
			}
		}
		std::vector<std::uint8_t> response =
			xucrun::broker::SerializeCredentialMetadataResult(result);
		RunOverlappedIo(pipe, stopEvent, true, response.data(),
			static_cast<DWORD>(response.size()), transferred);
	}

	// Creates a short-lived credential-capture challenge bound to client identity and session.
	void HandleCredentialCaptureChallengeRequest(HANDLE pipe, HANDLE stopEvent,
		HANDLE clientProcess, DWORD clientProcessId,
		const ClientImageIdentity& imageIdentity, const std::wstring& clientSid,
		const std::wstring& clientLogonSid, DWORD clientSessionId,
		const xucrun::broker::CredentialCaptureChallengeRequest& request,
		std::deque<PendingCredentialCaptureChallenge>& pendingChallenges,
		std::deque<PendingApprovalChallenge>& pendingApprovalChallenges,
		BrokerLogCallback log, DWORD& transferred)
	{
		xucrun::broker::CredentialCaptureChallengeResult result;
		result.requestId = request.requestId;
		DWORD error = NO_ERROR;
		AccountIdentity clientAccount;
		AccountIdentity account;
		AccountIdentity approvalAccount;
		PrunePendingCredentialCaptureChallenges(pendingChallenges);
		PrunePendingApprovalChallenges(pendingApprovalChallenges);
		if (!IsSensitiveClientAuthorized(imageIdentity, error))
		{
			result.status = xucrun::broker::ApprovalChallengeStatus::Denied;
			result.windowsError = error;
		}
		else if (!ResolveUserAccountIdentity(clientSid, clientAccount, error) ||
			!ResolveUserAccountIdentity(request.requestedAccountSid, account, error))
		{
			result.status = xucrun::broker::ApprovalChallengeStatus::InvalidRequest;
			result.windowsError = error == NO_ERROR ? ERROR_INVALID_DATA : error;
		}
		else if (!request.approvalAccountSid.empty())
		{
			bool approvalIsAdministrator = false;
			if (!ResolveUserAccountIdentity(request.approvalAccountSid,
				approvalAccount, error) ||
				!IsAccountMemberOfAdministrators(
					approvalAccount, approvalIsAdministrator, error) ||
				!approvalIsAdministrator)
			{
				result.status = xucrun::broker::ApprovalChallengeStatus::InvalidRequest;
				result.windowsError = error == NO_ERROR ? ERROR_ACCESS_DENIED : error;
			}
		}
		if (result.status == xucrun::broker::ApprovalChallengeStatus::Failed &&
			HasPendingSecureDesktopChallenge(pendingApprovalChallenges,
				pendingChallenges, clientSessionId))
		{
			result.status = xucrun::broker::ApprovalChallengeStatus::Failed;
			result.windowsError = ERROR_BUSY;
		}
		else if (result.status == xucrun::broker::ApprovalChallengeStatus::Failed &&
			!HasPendingCredentialCaptureCapacity(pendingChallenges,
			clientSid, clientSessionId, clientProcessId))
		{
			result.status = xucrun::broker::ApprovalChallengeStatus::Failed;
			result.windowsError = ERROR_TOO_MANY_CMDS;
		}
		else if (result.status == xucrun::broker::ApprovalChallengeStatus::Failed &&
			!GenerateCredentialCaptureChallengeId(
			pendingChallenges, result.challengeId))
		{
			result.status = xucrun::broker::ApprovalChallengeStatus::Failed;
			result.windowsError = ERROR_GEN_FAILURE;
		}
		else if (result.status == xucrun::broker::ApprovalChallengeStatus::Failed)
		{
			PendingCredentialCaptureChallenge pending;
			pending.challengeId = result.challengeId;
			pending.expiresAt = GetTickCount64() +
				xucrun::broker::ApprovalChallengeLifetimeMilliseconds;
			pending.clientSid = clientSid;
			pending.clientLogonSid = clientLogonSid;
			pending.clientProcessId = clientProcessId;
			pending.clientSessionId = clientSessionId;
			pending.clientPath = imageIdentity.path;
			pending.clientSha256 = imageIdentity.sha256;
			pending.accountSid = account.sid;
			std::wstring failedStage;
			if (!LaunchCredentialCaptureHelper(clientProcess, approvalAccount, pending,
				result.approvalHelperProcessHandle, error, failedStage))
			{
				result = {};
				result.requestId = request.requestId;
				result.status = xucrun::broker::ApprovalChallengeStatus::Failed;
				result.windowsError = error == NO_ERROR ? ERROR_GEN_FAILURE : error;
				if (log)
					log(EVENTLOG_ERROR_TYPE,
						L"Credential capture helper launch failed at stage " + failedStage +
						L". Error " + std::to_wstring(result.windowsError));
			}
			else
			{
				result.status = xucrun::broker::ApprovalChallengeStatus::Issued;
				result.canonicalClientName = clientAccount.canonicalName;
				result.canonicalAccountName = account.canonicalName;
				result.helperStartMode = pending.helperStartMode;
				pendingChallenges.push_back(std::move(pending));
			}
		}
		const std::vector<std::uint8_t> response =
			xucrun::broker::SerializeCredentialCaptureChallengeResult(result);
		RunOverlappedIo(pipe, stopEvent, true,
			const_cast<std::uint8_t*>(response.data()),
			static_cast<DWORD>(response.size()), transferred);
	}

	// Leases challenge data only to the helper process bound to the secure host instance.
	void HandleCredentialCaptureHelperRequest(HANDLE pipe, HANDLE stopEvent,
		DWORD helperProcessId, DWORD helperSessionId, DWORD helperIntegrityRid,
		const ClientImageIdentity& helperIdentity, const std::wstring& helperSid,
		const xucrun::broker::CredentialCaptureHelperRequest& request,
		std::deque<PendingCredentialCaptureChallenge>& pendingChallenges,
		DWORD& transferred)
	{
		xucrun::broker::CredentialCaptureHelperResult result;
		result.requestId = request.requestId;
		DWORD error = NO_ERROR;
		auto pending = std::find_if(pendingChallenges.begin(), pendingChallenges.end(),
			[&request](const PendingCredentialCaptureChallenge& value) {
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
		else if (pending->clientSessionId != helperSessionId ||
			pending->helperDataRetrieved ||
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
				[&pending, helperProcessId](const PendingCredentialCaptureChallenge& value) {
					return &value != &*pending && value.helperProcessId == helperProcessId;
				}))
		{
			result.status = xucrun::broker::ApprovalChallengeStatus::Denied;
			result.windowsError = ERROR_ACCESS_DENIED;
		}
		else if (pending->helperStartMode ==
			xucrun::broker::ApprovalHelperStartMode::ClientRunAsRequired &&
			!BindClientStartedSecureHost(
				helperProcessId, helperSessionId, *pending, error))
		{
			result.status = xucrun::broker::ApprovalChallengeStatus::Denied;
			result.windowsError = error;
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
					pending->helperProcessId = helperProcessId;
					pending->helperDataRetrieved = true;
					pending->expiresAt = GetTickCount64() +
						xucrun::broker::ApprovalHelperDecisionLifetimeMilliseconds;
					result.status = xucrun::broker::ApprovalChallengeStatus::Issued;
					result.canonicalClientName = clientAccount.canonicalName;
					result.canonicalAccountName = account.canonicalName;
				}
			}
		}
		const std::vector<std::uint8_t> response =
			xucrun::broker::SerializeCredentialCaptureHelperResult(result);
		RunOverlappedIo(pipe, stopEvent, true,
			const_cast<std::uint8_t*>(response.data()),
			static_cast<DWORD>(response.size()), transferred);
	}

	// Accepts the sensitive credential payload only from the bound helper and stages it transiently.
	void HandleCredentialCaptureSubmitRequest(HANDLE pipe, HANDLE stopEvent,
		DWORD helperProcessId, DWORD helperSessionId, DWORD helperIntegrityRid,
		const ClientImageIdentity& helperIdentity,
		xucrun::broker::CredentialCaptureSubmitRequest& request,
		std::deque<PendingCredentialCaptureChallenge>& pendingChallenges,
		BrokerLogCallback log, DWORD& transferred)
	{
		xucrun::broker::CredentialCaptureSubmitResult result;
		result.requestId = request.requestId;
		DWORD error = NO_ERROR;
		auto pending = std::find_if(pendingChallenges.begin(), pendingChallenges.end(),
			[&request](const PendingCredentialCaptureChallenge& value) {
				return value.challengeId == request.challengeId;
			});
		if (!IsApprovalHelperAuthorized(helperIdentity, helperIntegrityRid, error))
		{
			result.status = xucrun::broker::CredentialSaveStatus::InvalidRequest;
			result.windowsError = error;
		}
		else if (pending == pendingChallenges.end())
		{
			result.status = xucrun::broker::CredentialSaveStatus::InvalidRequest;
			result.windowsError = ERROR_NOT_FOUND;
		}
		else if (pending->expiresAt <= GetTickCount64())
		{
			CancelSecureHost(pending->secureHostCancellationSignal);
			pendingChallenges.erase(pending);
			result.status = xucrun::broker::CredentialSaveStatus::Failed;
			result.windowsError = ERROR_TIMEOUT;
		}
		else if (!pending->helperDataRetrieved ||
			pending->helperProcessId != helperProcessId ||
			pending->clientSessionId != helperSessionId)
		{
			result.status = xucrun::broker::CredentialSaveStatus::InvalidRequest;
			result.windowsError = ERROR_ACCESS_DENIED;
		}
		else
		{
			if (request.cancelled)
			{
				pendingChallenges.erase(pending);
				result.status = xucrun::broker::CredentialSaveStatus::Failed;
				result.windowsError = ERROR_CANCELLED;
			}
			else
			{
				AccountIdentity account;
				if (!ResolveUserAccountNameIdentity(
					request.enteredAccountName, account, error) ||
					_wcsicmp(account.sid.c_str(), pending->accountSid.c_str()) != 0)
				{
					result.status = xucrun::broker::CredentialSaveStatus::InvalidCredentials;
					result.windowsError = error == NO_ERROR
						? ERROR_INVALID_ACCOUNT_NAME : error;
					pendingChallenges.erase(pending);
				}
				else
				{
					const CredentialValidationStatus validationStatus =
						ValidateAccountPassword(account, request.password, error);
					result.windowsError = error;
					if (validationStatus == CredentialValidationStatus::Succeeded)
					{
						pending->stagedPassword = std::move(request.password);
						pending->credentialStaged = true;
						pending->expiresAt = GetTickCount64() +
							xucrun::broker::ApprovalHelperDecisionLifetimeMilliseconds;
						result.status = xucrun::broker::CredentialSaveStatus::Succeeded;
						result.windowsError = NO_ERROR;
					}
					else
					{
						result.status = validationStatus ==
							CredentialValidationStatus::Rejected
							? xucrun::broker::CredentialSaveStatus::InvalidCredentials
							: xucrun::broker::CredentialSaveStatus::Failed;
						pendingChallenges.erase(pending);
					}
					if (log)
						log(validationStatus == CredentialValidationStatus::Failed
							? EVENTLOG_ERROR_TYPE : EVENTLOG_INFORMATION_TYPE,
							L"Secure credential capture validation for target account " +
							account.canonicalName + L" completed. Status " +
							std::to_wstring(static_cast<int>(validationStatus)) + L", error " +
							std::to_wstring(result.windowsError));
				}
			}
		}
		request.password.clear();
		const std::vector<std::uint8_t> response =
			xucrun::broker::SerializeCredentialCaptureSubmitResult(result);
		RunOverlappedIo(pipe, stopEvent, true,
			const_cast<std::uint8_t*>(response.data()),
			static_cast<DWORD>(response.size()), transferred);
	}

	// Commits staged credentials only from the broker-created host bound to the challenge.
	void HandleCredentialCaptureCommitRequest(HANDLE pipe, HANDLE stopEvent,
		DWORD hostProcessId, DWORD hostSessionId,
		const xucrun::broker::CredentialCaptureCommitRequest& request,
		std::deque<PendingCredentialCaptureChallenge>& pendingChallenges,
		BrokerLogCallback log, DWORD& transferred)
	{
		xucrun::broker::CredentialCaptureCommitResult result;
		result.requestId = request.requestId;
		DWORD error = NO_ERROR;
		auto pending = std::find_if(pendingChallenges.begin(), pendingChallenges.end(),
			[&request](const PendingCredentialCaptureChallenge& value) {
				return value.challengeId == request.challengeId;
			});
		if (pending == pendingChallenges.end())
		{
			result.status = xucrun::broker::CredentialSaveStatus::InvalidRequest;
			result.windowsError = ERROR_NOT_FOUND;
		}
		else
		{
			if (pending->expiresAt <= GetTickCount64())
			{
				pendingChallenges.erase(pending);
				result.status = xucrun::broker::CredentialSaveStatus::Failed;
				result.windowsError = ERROR_TIMEOUT;
			}
			else if (!IsCredentialCommitHost(*pending, hostProcessId, hostSessionId))
			{
				result.status = xucrun::broker::CredentialSaveStatus::InvalidRequest;
				result.windowsError = ERROR_ACCESS_DENIED;
			}
			else
			{
				PendingCredentialCaptureChallenge challenge = std::move(*pending);
				pendingChallenges.erase(pending);
				AccountIdentity account;
				std::wstring metadataPath;
				std::wstring credentialTarget;
				std::vector<std::uint8_t> storedPassword;
				const auto* passwordBytes = reinterpret_cast<const std::uint8_t*>(
					challenge.stagedPassword.c_str());
				const std::span<const std::uint8_t> password(passwordBytes,
					challenge.stagedPassword.size() * sizeof(wchar_t));
				if (ResolveUserAccountIdentity(challenge.accountSid, account, error) &&
					EnsureCredentialMetadataBinding(challenge.clientSid, account.sid,
						metadataPath, credentialTarget, error) &&
					xucrun::broker_store::Write(credentialTarget, password, error) &&
					xucrun::broker_store::Read(credentialTarget, storedPassword, error) &&
					storedPassword.size() == password.size() &&
					std::equal(storedPassword.begin(), storedPassword.end(), password.begin()))
				{
					result.status = xucrun::broker::CredentialSaveStatus::Succeeded;
					result.windowsError = NO_ERROR;
				}
				else
				{
					result.status = xucrun::broker::CredentialSaveStatus::Failed;
					result.windowsError = error == NO_ERROR ? ERROR_INVALID_DATA : error;
					DWORD deleteError = NO_ERROR;
					if (!credentialTarget.empty())
						xucrun::broker_store::Delete(credentialTarget, deleteError);
				}
				EraseSensitiveBytes(storedPassword);
				if (log)
					log(result.status == xucrun::broker::CredentialSaveStatus::Succeeded
						? EVENTLOG_INFORMATION_TYPE : EVENTLOG_ERROR_TYPE,
						L"Secure credential capture commit after desktop restore for target account " +
						account.canonicalName + L" completed. Error " +
						std::to_wstring(result.windowsError));
			}
		}
		const std::vector<std::uint8_t> response =
			xucrun::broker::SerializeCredentialCaptureCommitResult(result);
		RunOverlappedIo(pipe, stopEvent, true,
			const_cast<std::uint8_t*>(response.data()),
			static_cast<DWORD>(response.size()), transferred);
	}
