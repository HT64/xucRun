// Purpose: Internal request-handler implementation fragment for BrokerPipeServer.cpp.
//
// Inclusion contract:
// - This file is included exactly once, from inside BrokerPipeServer.cpp's anonymous namespace.
// - It deliberately uses the server's private identity, replay, challenge, tracking, and I/O helpers.
// - It is not a standalone translation unit and must not expose broker-internal state in public headers.
// - Handler behavior and protocol responses remain defined by the existing broker contracts.
	void PruneStatusIcons(std::vector<RegisteredStatusIcon>& icons)
	{
		icons.erase(std::remove_if(icons.begin(), icons.end(),
			[](const RegisteredStatusIcon& icon) {
				if (WaitForSingleObject(icon.process, 0) == WAIT_TIMEOUT)
					return false;
				CloseHandle(icon.process);
				return true;
			}), icons.end());
	}

	bool IsProcessElevated(HANDLE process, bool& elevated, DWORD& error)
	{
		HANDLE token = nullptr;
		if (!OpenProcessToken(process, TOKEN_QUERY, &token))
		{
			error = GetLastError();
			return false;
		}
		TOKEN_ELEVATION tokenElevation = {};
		DWORD bytesNeeded = 0;
		const bool read = GetTokenInformation(token, TokenElevation,
			&tokenElevation, sizeof(tokenElevation), &bytesNeeded) != FALSE;
		error = read ? NO_ERROR : GetLastError();
		CloseHandle(token);
		elevated = read && tokenElevation.TokenIsElevated != 0;
		return read;
	}

	void HandleStatusIconListRequest(HANDLE pipe, HANDLE stopEvent,
		HANDLE clientProcess, DWORD clientProcessId, DWORD clientSessionId,
		const std::wstring& clientSid, const ClientImageIdentity& imageIdentity,
		const xucrun::broker::StatusIconListRequest& request,
		std::vector<RegisteredStatusIcon>& icons, BrokerLogCallback log,
		DWORD& transferred)
	{
		xucrun::broker::StatusIconListResult result;
		result.requestId = request.requestId;
		DWORD error = NO_ERROR;
		if (!IsSensitiveClientAuthorized(imageIdentity, error))
		{
			result.status = xucrun::broker::CredentialListStatus::InvalidRequest;
			result.windowsError = error;
		}
		else
		{
			PruneStatusIcons(icons);
			DWORD coreAuthorizationError = NO_ERROR;
			if (request.includeAllSessions &&
				!IsCoreClientAuthorized(imageIdentity, coreAuthorizationError))
			{
				result.status =
					xucrun::broker::CredentialListStatus::InvalidRequest;
				result.windowsError = ERROR_ACCESS_DENIED;
			}
			DWORD trayAuthorizationError = NO_ERROR;
			if (result.windowsError == NO_ERROR &&
				IsTrayCompanionAuthorized(
				imageIdentity, trayAuthorizationError))
			{
				auto existing = std::find_if(icons.begin(), icons.end(),
					[clientProcessId](const RegisteredStatusIcon& icon) {
						return icon.processId == clientProcessId;
					});
				if (existing != icons.end())
				{
					existing->clientVersion = request.clientVersion;
				}
				else if (icons.size() >= xucrun::broker::MaximumStatusIconEntries)
				{
					result.status = xucrun::broker::CredentialListStatus::Failed;
					result.windowsError = ERROR_TOO_MANY_SESS;
				}
				else
				{
					AccountIdentity identity;
					bool elevated = false;
					HANDLE retainedProcess = nullptr;
					if (!ResolveUserAccountIdentity(clientSid, identity, error) ||
						!IsProcessElevated(clientProcess, elevated, error) ||
						!DuplicateHandle(GetCurrentProcess(), clientProcess,
							GetCurrentProcess(), &retainedProcess, SYNCHRONIZE,
							FALSE, 0))
					{
						if (error == NO_ERROR)
							error = GetLastError();
						result.status =
							xucrun::broker::CredentialListStatus::Failed;
						result.windowsError = error;
					}
					else
					{
						icons.push_back({ retainedProcess, clientProcessId,
							clientSessionId, request.clientVersion, clientSid,
							identity.canonicalName, elevated });
						if (log)
						{
							log(EVENTLOG_INFORMATION_TYPE,
								L"Status icon registered. Account " +
								SanitizeLogValue(identity.canonicalName) +
								L", session " +
								std::to_wstring(clientSessionId) + L", PID " +
								std::to_wstring(clientProcessId) + L", elevated " +
								(elevated ? L"true" : L"false") + L".");
						}
					}
				}
			}
			if (result.windowsError == NO_ERROR)
			{
				result.status = xucrun::broker::CredentialListStatus::Succeeded;
				for (const RegisteredStatusIcon& icon : icons)
				{
					if (request.includeAllSessions ||
						icon.sessionId == clientSessionId)
					{
						result.entries.push_back({ icon.processId, icon.elevated,
							icon.clientVersion, icon.accountSid,
							icon.canonicalAccountName });
					}
				}
				std::sort(result.entries.begin(), result.entries.end(),
					[](const auto& left, const auto& right) {
						if (left.canonicalAccountName != right.canonicalAccountName)
							return left.canonicalAccountName < right.canonicalAccountName;
						return left.processId < right.processId;
					});
			}
		}
		std::vector<std::uint8_t> response =
			xucrun::broker::SerializeStatusIconListResult(result);
		RunOverlappedIo(pipe, stopEvent, true, response.data(),
			static_cast<DWORD>(response.size()), transferred);
	}

// Sends status and version and accepts only the client's nonce-bound acknowledgement.
	void HandleHealthRequest(HANDLE pipe, HANDLE stopEvent,
		const xucrun::broker::HealthRequest& request,
		std::vector<TrackedElevatedProcess>& trackedProcesses,
		std::vector<TrackedElevatedProcessJob>& trackedJobs,
		DWORD clientSessionId, BrokerLogCallback log, DWORD& transferred)
	{
		xucrun::broker::HealthResult health;
		health.requestId = request.requestId;
		health.nonce = request.nonce;
		health.brokerProcessId = GetCurrentProcessId();
		health.elevatedProcessCount = CountTrackedElevatedProcesses(
			trackedProcesses, trackedJobs, clientSessionId);
		health.brokerVersion = {
			VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH, BUILD_NUMBER
		};
		std::vector<std::uint8_t> response =
			xucrun::broker::SerializeHealthResult(health);
		const bool responseWritten = RunOverlappedIo(pipe, stopEvent, true,
			response.data(), static_cast<DWORD>(response.size()), transferred) &&
			transferred == response.size();
		std::vector<std::uint8_t> acknowledgementFrame(
			xucrun::broker::HealthAcknowledgementSize);
		xucrun::broker::HealthAcknowledgement acknowledgement;
		const bool acknowledged = responseWritten &&
			RunOverlappedIo(pipe, stopEvent, false, acknowledgementFrame.data(),
				static_cast<DWORD>(acknowledgementFrame.size()), transferred) &&
			transferred == acknowledgementFrame.size() &&
			xucrun::broker::DeserializeHealthAcknowledgement(
				acknowledgementFrame, acknowledgement) &&
			acknowledgement.requestId == request.requestId &&
			acknowledgement.nonce == request.nonce;
		if (!acknowledged && log)
			log(EVENTLOG_WARNING_TYPE, L"Health response was not acknowledged correctly.");
	}

// Filters, sorts, and limits previously validated process list candidates.
	std::vector<xucrun::broker::TrackedProcessListEntry> BuildTrackedProcessListEntries(
		const std::vector<TrackedProcessListCandidate>& source, DWORD clientSessionId)
	{
		std::vector<TrackedProcessListCandidate> candidates;
		for (const TrackedProcessListCandidate& candidate : source)
		{
			if (candidate.sessionId == clientSessionId)
				candidates.push_back(candidate);
		}
		std::sort(candidates.begin(), candidates.end(),
			[](const auto& left, const auto& right) {
				if (left.creationTime != right.creationTime)
					return left.creationTime > right.creationTime;
				return left.entry.processId > right.entry.processId;
			});
		std::vector<xucrun::broker::TrackedProcessListEntry> entries;
		entries.reserve((std::min)(candidates.size(),
			xucrun::broker::MaximumTrackedProcessEntries));
		for (TrackedProcessListCandidate& candidate : candidates)
		{
			if (entries.size() >= xucrun::broker::MaximumTrackedProcessEntries)
				break;
			entries.push_back(std::move(candidate.entry));
		}
		return entries;
	}

// Updates and returns the most recent valid elevated processes in the client session.
	void HandleTrackedProcessListRequest(HANDLE pipe, HANDLE stopEvent,
		const ClientImageIdentity& imageIdentity, DWORD clientSessionId,
		const xucrun::broker::TrackedProcessListRequest& request,
		std::vector<TrackedElevatedProcess>& trackedProcesses,
		std::vector<TrackedElevatedProcessJob>& trackedJobs, DWORD& transferred)
	{
		xucrun::broker::TrackedProcessListResult result;
		result.requestId = request.requestId;
		DWORD authorizationError = NO_ERROR;
		if (!IsSensitiveClientAuthorized(imageIdentity, authorizationError))
		{
			result.status = xucrun::broker::CredentialListStatus::InvalidRequest;
			result.windowsError = authorizationError;
		}
		else
		{
			RefreshTrackedElevatedProcesses(trackedProcesses, trackedJobs);
			DiscoverElevatedUserProcesses(trackedProcesses, clientSessionId);
			result.status = xucrun::broker::CredentialListStatus::Succeeded;
			std::vector<TrackedProcessListCandidate> candidates;
			for (const TrackedElevatedProcess& process : trackedProcesses)
			{
				if (process.sessionId != clientSessionId)
					continue;
				ULONGLONG creationTime = 0;
				if (GetProcessCreationTimeValue(process.process, creationTime))
					candidates.push_back({ process.sessionId, creationTime,
						{ process.trackingId, process.processId, process.processBaseName } });
			}
			result.entries = BuildTrackedProcessListEntries(candidates, clientSessionId);
		}

		std::vector<std::uint8_t> response =
			xucrun::broker::SerializeTrackedProcessListResult(result);
		RunOverlappedIo(pipe, stopEvent, true, response.data(),
			static_cast<DWORD>(response.size()), transferred);
	}

// Accepts only an elevated process handle duplicated from the client and revalidated.
	void HandleTrackedProcessRegistrationRequest(HANDLE pipe, HANDLE stopEvent,
		HANDLE clientProcess, const ClientImageIdentity& imageIdentity,
		DWORD clientSessionId, const std::wstring& clientLogonSid,
		const xucrun::broker::TrackedProcessRegistrationRequest& request,
		std::vector<TrackedElevatedProcess>& trackedProcesses,
		std::vector<TrackedElevatedProcessJob>& trackedJobs,
		BrokerLogCallback log, DWORD& transferred)
	{
		xucrun::broker::TrackedProcessRegistrationResult result;
		result.requestId = request.requestId;
		DWORD registrationError = NO_ERROR;
		HANDLE trackedProcess = nullptr;
		if (!IsSensitiveClientAuthorized(imageIdentity, registrationError))
		{
			result.status = xucrun::broker::TrackedProcessRegistrationStatus::InvalidRequest;
		}
		else if (!DuplicateHandle(clientProcess,
			reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(request.processHandle)),
			GetCurrentProcess(), &trackedProcess, 0, FALSE, DUPLICATE_SAME_ACCESS))
		{
			registrationError = GetLastError();
			result.status = xucrun::broker::TrackedProcessRegistrationStatus::InvalidRequest;
		}
		else
		{
			std::wstring processBaseName;
			if (!ValidateRegisteredProcess(trackedProcess, request.processId,
				clientSessionId, clientLogonSid, processBaseName, registrationError))
			{
				result.status = xucrun::broker::TrackedProcessRegistrationStatus::InvalidRequest;
				CloseHandle(trackedProcess);
				trackedProcess = nullptr;
			}
			else
			{
				PruneTrackedElevatedProcesses(trackedProcesses);
				const bool alreadyTracked = std::any_of(
					trackedProcesses.begin(), trackedProcesses.end(),
					[&request](const auto& process) {
						return process.processId == request.processId;
					});
				xucrun::broker::RequestNonce trackingId = {};
				if (alreadyTracked)
				{
					CloseHandle(trackedProcess);
					trackedProcess = nullptr;
					result.status =
						xucrun::broker::TrackedProcessRegistrationStatus::Succeeded;
				}
				else if (!GenerateTrackingId(trackedProcesses, trackingId))
				{
					registrationError = ERROR_GEN_FAILURE;
					result.status = xucrun::broker::TrackedProcessRegistrationStatus::Failed;
					CloseHandle(trackedProcess);
					trackedProcess = nullptr;
				}
				else if (!CreateTrackedProcessJob(trackedProcess, request.processId,
					clientSessionId, clientLogonSid, false, trackedJobs, registrationError))
				{
					result.status = xucrun::broker::TrackedProcessRegistrationStatus::Failed;
					CloseHandle(trackedProcess);
					trackedProcess = nullptr;
				}
				else
				{
					trackedProcesses.push_back({ trackedProcess, request.processId,
						clientSessionId, trackingId, std::move(processBaseName) });
					if (log)
						log(EVENTLOG_INFORMATION_TYPE,
							L"Descendant monitoring enabled for registered elevated process. PID " +
							std::to_wstring(request.processId));
					trackedProcess = nullptr;
					result.status =
						xucrun::broker::TrackedProcessRegistrationStatus::Succeeded;
				}
			}
		}
		result.windowsError = registrationError;
		std::vector<std::uint8_t> response =
			xucrun::broker::SerializeTrackedProcessRegistrationResult(result);
		RunOverlappedIo(pipe, stopEvent, true, response.data(),
			static_cast<DWORD>(response.size()), transferred);
	}

// Launches the fixed diagnostic process exclusively in the authenticated client session.
	void HandleSessionTestRequest(HANDLE pipe, HANDLE stopEvent,
		HANDLE clientProcess, DWORD clientSessionId, const std::wstring& clientSid,
		const xucrun::broker::SessionTestRequest& request,
		BrokerLogCallback log, DWORD& transferred)
	{
		xucrun::broker::SessionTestResult result;
		result.requestId = request.requestId;
		DWORD launchError = NO_ERROR;
		DWORD launchedProcessId = 0;
		std::wstring failedStage;
		if (LaunchSessionTestProcess(clientProcess, clientSessionId, clientSid,
			launchedProcessId, launchError, failedStage))
		{
			result.processId = launchedProcessId;
			result.status = xucrun::broker::SessionTestStatus::Succeeded;
			if (log)
				log(EVENTLOG_INFORMATION_TYPE,
					L"Fixed session test process for SID " + clientSid +
					L" started in session " + std::to_wstring(clientSessionId) +
					L". PID " + std::to_wstring(result.processId));
		}
		else
		{
			result.status = launchError == ERROR_ACCESS_DENIED ||
				launchError == ERROR_NO_TOKEN
				? xucrun::broker::SessionTestStatus::InvalidSession
				: xucrun::broker::SessionTestStatus::Failed;
			result.windowsError = launchError;
			if (log)
				log(EVENTLOG_ERROR_TYPE,
					L"Fixed session test process could not be started at stage " + failedStage +
					L". Error " +
					std::to_wstring(launchError));
		}
		std::vector<std::uint8_t> response =
			xucrun::broker::SerializeSessionTestResult(result);
		RunOverlappedIo(pipe, stopEvent, true, response.data(),
			static_cast<DWORD>(response.size()), transferred);
	}

	bool IsElevatedAdministratorProcess(HANDLE process, DWORD integrityRid,
		DWORD& error)
	{
		if (integrityRid < SECURITY_MANDATORY_HIGH_RID)
		{
			error = ERROR_ELEVATION_REQUIRED;
			return false;
		}
		HANDLE token = nullptr;
		if (!OpenProcessToken(process, TOKEN_QUERY | TOKEN_DUPLICATE, &token))
		{
			error = GetLastError();
			return false;
		}
		TOKEN_ELEVATION elevation = {};
		DWORD elevationSize = 0;
		BYTE administratorsSid[SECURITY_MAX_SID_SIZE] = {};
		DWORD administratorsSidSize = sizeof(administratorsSid);
		BOOL isAdministrator = FALSE;
		if (!GetTokenInformation(token, TokenElevation, &elevation,
			sizeof(elevation), &elevationSize) ||
			elevation.TokenIsElevated == 0)
		{
			error = GetLastError();
			if (error == NO_ERROR)
				error = ERROR_ELEVATION_REQUIRED;
			CloseHandle(token);
			return false;
		}
		if (!CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr,
			administratorsSid, &administratorsSidSize))
		{
			error = GetLastError();
			CloseHandle(token);
			return false;
		}
		HANDLE membershipToken = nullptr;
		if (!DuplicateToken(token, SecurityIdentification, &membershipToken))
		{
			error = GetLastError();
			CloseHandle(token);
			return false;
		}
		const bool membershipRead =
			CheckTokenMembership(
				membershipToken, administratorsSid, &isAdministrator) != FALSE;
		error = membershipRead
			? (isAdministrator != FALSE ? NO_ERROR : ERROR_ACCESS_DENIED)
			: GetLastError();
		CloseHandle(membershipToken);
		CloseHandle(token);
		return membershipRead && isAdministrator != FALSE;
	}

	void CloseSystemLaunchLocks(std::vector<HANDLE>& locks) noexcept
	{
		for (HANDLE handle : locks)
		{
			if (handle != nullptr && handle != INVALID_HANDLE_VALUE)
				CloseHandle(handle);
		}
		locks.clear();
	}

	bool LockAndRevalidateSystemTarget(
		const TargetImageIdentity& expected,
		std::vector<HANDLE>& locks,
		DWORD& error)
	{
		locks.clear();
		std::filesystem::path current =
			std::filesystem::path(expected.path).parent_path();
		while (!current.empty())
		{
			HANDLE directory = CreateFileW(current.c_str(), FILE_READ_ATTRIBUTES,
				FILE_SHARE_READ, nullptr, OPEN_EXISTING,
				FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
			if (directory == INVALID_HANDLE_VALUE)
			{
				error = GetLastError();
				CloseSystemLaunchLocks(locks);
				return false;
			}
			BY_HANDLE_FILE_INFORMATION information = {};
			if (!GetFileInformationByHandle(directory, &information) ||
				(information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
				(information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
			{
				error = GetLastError();
				if (error == NO_ERROR)
					error = ERROR_REPARSE_TAG_INVALID;
				CloseHandle(directory);
				CloseSystemLaunchLocks(locks);
				return false;
			}
			locks.push_back(directory);
			const std::filesystem::path parent = current.parent_path();
			if (parent == current)
				break;
			current = parent;
		}

		HANDLE executable = CreateFileW(expected.path.c_str(), GENERIC_READ,
			FILE_SHARE_READ, nullptr, OPEN_EXISTING,
			FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
		if (executable == INVALID_HANDLE_VALUE)
		{
			error = GetLastError();
			CloseSystemLaunchLocks(locks);
			return false;
		}
		locks.push_back(executable);
		BY_HANDLE_FILE_INFORMATION information = {};
		std::wstring finalPath;
		std::vector<std::uint8_t> sha256;
		if (!GetFileInformationByHandle(executable, &information) ||
			(information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ||
			(information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
			!xucrun::files::ResolveFinalFilePathFromHandle(executable, finalPath) ||
			!xucrun::files::ComputeFileSHA256FromHandle(executable, sha256) ||
			_wcsicmp(finalPath.c_str(), expected.path.c_str()) != 0 ||
			sha256 != expected.sha256)
		{
			error = GetLastError();
			if (error == NO_ERROR)
				error = ERROR_FILE_INVALID;
			CloseSystemLaunchLocks(locks);
			return false;
		}
		error = NO_ERROR;
		return true;
	}

	xucrun::broker::SystemLaunchResult BuildLegacySystemLaunchRejection(
		std::uint64_t requestId)
	{
		xucrun::broker::SystemLaunchResult result;
		result.requestId = requestId;
		result.status = xucrun::broker::SystemLaunchStatus::AccessDenied;
		result.windowsError = ERROR_ACCESS_DENIED;
		return result;
	}

	void HandleSystemLaunchRequest(HANDLE pipe, HANDLE stopEvent,
		HANDLE, DWORD, DWORD, const ClientImageIdentity&, const std::wstring&,
		const xucrun::broker::SystemLaunchRequest& request,
		BrokerLogCallback log, DWORD& transferred)
	{
		const xucrun::broker::SystemLaunchResult result =
			BuildLegacySystemLaunchRejection(request.requestId);
		if (log)
			log(EVENTLOG_WARNING_TYPE,
				L"Legacy direct SYSTEM launch request rejected; a protected "
				L"single-use challenge is required.");
		std::vector<std::uint8_t> challengeRequiredResponse =
			xucrun::broker::SerializeSystemLaunchResult(result);
		RunOverlappedIo(pipe, stopEvent, true, challengeRequiredResponse.data(),
			static_cast<DWORD>(challengeRequiredResponse.size()), transferred);
	}

// Validates the account and target image of the diagnostic contract without launching a process.
	void HandleLaunchRequest(HANDLE pipe, HANDLE stopEvent,
		const xucrun::broker::LaunchRequest& request,
		BrokerLogCallback log, DWORD& transferred)
	{
		xucrun::broker::LaunchResult result;
		result.requestId = request.requestId;
		AccountIdentity accountIdentity;
		DWORD accountIdentityError = NO_ERROR;
		if (!ResolveUserAccountIdentity(
			request.requestedAccountSid, accountIdentity, accountIdentityError))
		{
			result.status = xucrun::broker::LaunchStatus::InvalidRequest;
			result.windowsError = accountIdentityError;
			if (log)
				log(EVENTLOG_WARNING_TYPE,
					L"Launch request " + std::to_wstring(request.requestId) +
					L" rejected due to invalid target account SID. Error " +
					std::to_wstring(accountIdentityError));
		}
		else
		{
			TargetImageIdentity targetIdentity;
			DWORD targetIdentityError = NO_ERROR;
			if (!ReadTargetImageIdentity(
				request.executablePath, targetIdentity, targetIdentityError))
			{
				result.status = xucrun::broker::LaunchStatus::InvalidRequest;
				result.windowsError = targetIdentityError;
				if (log)
					log(EVENTLOG_WARNING_TYPE,
						L"Launch request " + std::to_wstring(request.requestId) +
						L" rejected due to invalid target. Error " +
						std::to_wstring(targetIdentityError));
			}
			else
			{
				result.status = xucrun::broker::LaunchStatus::Denied;
				result.windowsError = ERROR_NOT_SUPPORTED;
				if (log)
					log(EVENTLOG_INFORMATION_TYPE,
						L"Launch target account: " + accountIdentity.canonicalName + L" (" +
						accountIdentity.sid + L"); target: " +
						SanitizeLogValue(targetIdentity.path) +
						L", SHA-256 " +
						xucrun::targets::BytesToUpperHex(targetIdentity.sha256) +
						L"; process launch is not enabled yet.");
			}
		}
		std::vector<std::uint8_t> response =
			xucrun::broker::SerializeLaunchResult(result);
		RunOverlappedIo(pipe, stopEvent, true, response.data(),
			static_cast<DWORD>(response.size()), transferred);
	}
