// Purpose: Internal implementation fragment for SessionTestLauncher.cpp.
//
// Inclusion contract:
// - This file is included exactly once by SessionTestLauncher.cpp.
// - It shares that translation unit's private token, desktop, profile, and process helpers.
// - It is not a standalone translation unit and must not expose internal security state.
// - Token validation, handle ownership, cleanup order, and failure-stage reporting are preserved.

// Launches the session test only with a client token matching the SID and session ID.
bool LaunchSessionTestProcess(
	HANDLE clientProcess,
	DWORD sessionId,
	const std::wstring& expectedUserSid,
	DWORD& processId,
	DWORD& error,
	std::wstring& failedStage)
{
	processId = 0;
	error = NO_ERROR;
	failedStage = L"privilege";
	if (!EnablePrivilege(SE_ASSIGNPRIMARYTOKEN_NAME, error) ||
		!EnablePrivilege(SE_INCREASE_QUOTA_NAME, error))
	{
		return false;
	}
	HANDLE sourceToken = nullptr;
	failedStage = L"OpenProcessToken";
	if (!OpenProcessToken(clientProcess, TOKEN_QUERY | TOKEN_DUPLICATE, &sourceToken))
	{
		error = GetLastError();
		return false;
	}
	HANDLE token = nullptr;
	failedStage = L"DuplicateTokenEx";
	if (!DuplicateTokenEx(sourceToken,
		TOKEN_ASSIGN_PRIMARY | TOKEN_DUPLICATE | TOKEN_QUERY |
			TOKEN_ADJUST_DEFAULT | TOKEN_ADJUST_SESSIONID,
		nullptr, SecurityImpersonation, TokenPrimary, &token))
	{
		error = GetLastError();
		CloseHandle(sourceToken);
		return false;
	}
	CloseHandle(sourceToken);

	std::wstring tokenSid;
	DWORD tokenSessionId = 0;
	failedStage = L"token identity";
	if (!ReadTokenIdentity(token, tokenSid, tokenSessionId, error) ||
		tokenSessionId != sessionId ||
		_wcsicmp(tokenSid.c_str(), expectedUserSid.c_str()) != 0)
	{
		if (error == NO_ERROR)
			error = ERROR_ACCESS_DENIED;
		CloseHandle(token);
		return false;
	}
	if (!GrantInteractiveDesktopAccessInSession(token, sessionId, error, failedStage))
	{
		CloseHandle(token);
		return false;
	}

	const bool started = LaunchFixedTestConsole(token, processId, error, failedStage);
	CloseHandle(token);
	return started;
}

// Logs on the target account and launches only after SID, session, and object access validation.
bool LaunchCredentialTestProcess(
	HANDLE clientProcess,
	const AccountIdentity& identity,
	const xucrun::SensitiveString& password,
	DWORD sessionId,
	DWORD& processId,
	DWORD& error,
	std::wstring& failedStage)
{
	processId = 0;
	error = NO_ERROR;
	failedStage = L"LogonUserW";
	HANDLE token = nullptr;
	if (LogonAccountWithPassword(identity, password, token, error) !=
		CredentialValidationStatus::Succeeded)
	{
		return false;
	}

	HANDLE clientIdentityToken = nullptr;
	failedStage = L"same-account client token";
	if (!OpenProcessToken(clientProcess, TOKEN_QUERY, &clientIdentityToken))
	{
		error = GetLastError();
		CloseHandle(token);
		return false;
	}
	std::wstring clientSid;
	DWORD clientSessionId = 0;
	const bool clientIdentityRead = ReadTokenIdentity(
		clientIdentityToken, clientSid, clientSessionId, error);
	CloseHandle(clientIdentityToken);
	if (!clientIdentityRead)
	{
		CloseHandle(token);
		return false;
	}
	if (_wcsicmp(clientSid.c_str(), identity.sid.c_str()) == 0)
	{
		CloseHandle(token);
		return LaunchSessionTestProcess(clientProcess, sessionId,
			identity.sid, processId, error, failedStage);
	}
	CloseHandle(token);
	return LaunchSecondaryLogonBrokerTest(clientProcess,
		identity, password, sessionId, processId, error, failedStage);
}

// Launches an approved target only with a validated token bound to the account and session.
bool LaunchApprovedCredentialProcess(
	HANDLE clientProcess,
	const AccountIdentity& identity,
	const xucrun::SensitiveString& password,
	DWORD sessionId,
	const std::wstring& clientPath,
	const std::wstring& executablePath,
	const std::wstring& argumentTail,
	const std::wstring& workingDirectory,
	xucrun::broker::ElevationMode elevationMode,
	xucrun::broker::LaunchVisibility visibility,
	DWORD& processId,
	std::wstring& effectiveWorkingDirectory,
	DWORD& error,
	std::wstring& failedStage,
	HANDLE* processHandle)
{
	if (processHandle) *processHandle = nullptr;
	processId = 0;
	effectiveWorkingDirectory.clear();
	error = NO_ERROR;
	failedStage = L"LogonUserW";
	HANDLE token = nullptr;
	if (LogonAccountWithPassword(identity, password, token, error) !=
		CredentialValidationStatus::Succeeded)
	{
		return false;
	}

	HANDLE clientToken = nullptr;
	failedStage = L"same-account client token";
	if (!OpenProcessToken(clientProcess, TOKEN_QUERY | TOKEN_DUPLICATE, &clientToken))
	{
		error = GetLastError();
		CloseHandle(token);
		return false;
	}
	std::wstring clientSid;
	DWORD clientTokenSessionId = 0;
	if (!ReadTokenIdentity(clientToken, clientSid, clientTokenSessionId, error))
	{
		CloseHandle(clientToken);
		CloseHandle(token);
		return false;
	}
	if (_wcsicmp(clientSid.c_str(), identity.sid.c_str()) == 0 &&
		clientTokenSessionId == sessionId)
	{
		CloseHandle(token);
		token = clientToken;
		clientToken = nullptr;
	}
	const bool crossAccount = clientToken != nullptr;
	if (clientToken)
		CloseHandle(clientToken);
	if (crossAccount)
	{
		CloseHandle(token);
		token = nullptr;
		if (!LogonAccountWithSessionGroups(clientProcess,
			identity, password, token, error, failedStage))
		{
			return false;
		}
	}

	HANDLE launchToken = nullptr;
	failedStage = L"DuplicateTokenEx";
	if (!DuplicatePrimaryLaunchToken(token, launchToken, error))
	{
		CloseHandle(token);
		return false;
	}
	CloseHandle(token);
	token = launchToken;
	failedStage = L"privilege";
	if (!EnablePrivilege(SE_ASSIGNPRIMARYTOKEN_NAME, error) ||
		!EnablePrivilege(SE_INCREASE_QUOTA_NAME, error) ||
		!EnablePrivilege(SE_TCB_NAME, error))
	{
		CloseHandle(token);
		return false;
	}
	failedStage = L"TokenSessionId";
	if (!SetTokenInformation(token, TokenSessionId, &sessionId, sizeof(sessionId)) ||
		!GrantInteractiveDesktopAccessInSession(token, sessionId, error, failedStage))
	{
		if (error == NO_ERROR)
			error = GetLastError();
		CloseHandle(token);
		return false;
	}

	std::wstring tokenSid;
	DWORD tokenSessionId = 0;
	failedStage = L"token identity";
	if (!ReadTokenIdentity(token, tokenSid, tokenSessionId, error) ||
		tokenSessionId != sessionId ||
		_wcsicmp(tokenSid.c_str(), identity.sid.c_str()) != 0)
	{
		if (error == NO_ERROR)
			error = ERROR_ACCESS_DENIED;
		CloseHandle(token);
		return false;
	}
	failedStage = L"LoadUserProfileW";
	if (!GetUserProfileCache().EnsureLoaded(token, identity, error))
	{
		CloseHandle(token);
		return false;
	}

	bool started = false;
	// HiddenMonitor requires the interactive wrapper so its monitor process displays
	// the error dialog in the session and under the target user's account.
	if (elevationMode == xucrun::broker::ElevationMode::RequireElevated ||
		visibility == xucrun::broker::LaunchVisibility::HiddenMonitor ||
		IsBatchFile(executablePath))
	{
		started = LaunchShellWrapper(token, clientPath, executablePath, argumentTail,
			workingDirectory,
			elevationMode == xucrun::broker::ElevationMode::RequireElevated,
			visibility,
			processId, &effectiveWorkingDirectory,
			error, failedStage, processHandle);
	}
	else
	{
		started = LaunchBoundProcess(token, executablePath, argumentTail,
			workingDirectory, visibility, processId, &effectiveWorkingDirectory, error, failedStage);
		if (!started && error == ERROR_ACCESS_DENIED && failedStage == L"CreateProcessAsUserW")
		{
			started = LaunchShellWrapper(token, clientPath, executablePath, argumentTail,
				workingDirectory, false, visibility, processId, &effectiveWorkingDirectory,
				error, failedStage);
		}
	}
	CloseHandle(token);
	return started;
}

// Launches a fixed approved helper only with the authenticated high-integrity client token.
bool LaunchApprovedClientProcess(
	HANDLE clientProcess,
	const std::wstring& expectedUserSid,
	DWORD sessionId,
	const std::wstring& executablePath,
	const std::wstring& argumentTail,
	const std::wstring& workingDirectory,
	DWORD& processId,
	std::wstring& effectiveWorkingDirectory,
	DWORD& error,
	std::wstring& failedStage)
{
	processId = 0;
	effectiveWorkingDirectory.clear();
	error = NO_ERROR;
	failedStage = L"approved client token";
	HANDLE clientToken = nullptr;
	if (!OpenProcessToken(clientProcess, TOKEN_QUERY | TOKEN_DUPLICATE, &clientToken))
	{
		error = GetLastError();
		return false;
	}

	std::wstring clientSid;
	DWORD clientSessionId = 0;
	if (!ReadTokenIdentity(clientToken, clientSid, clientSessionId, error) ||
		_wcsicmp(clientSid.c_str(), expectedUserSid.c_str()) != 0 ||
		clientSessionId != sessionId ||
		!IsHighIntegrityToken(clientToken, error))
	{
		if (error == NO_ERROR)
			error = ERROR_ACCESS_DENIED;
		CloseHandle(clientToken);
		return false;
	}

	HANDLE launchToken = nullptr;
	failedStage = L"approved client DuplicateTokenEx";
	if (!DuplicatePrimaryLaunchToken(clientToken, launchToken, error))
	{
		CloseHandle(clientToken);
		return false;
	}
	CloseHandle(clientToken);
	failedStage = L"approved client privilege";
	if (!EnablePrivilege(SE_ASSIGNPRIMARYTOKEN_NAME, error) ||
		!EnablePrivilege(SE_INCREASE_QUOTA_NAME, error))
	{
		CloseHandle(launchToken);
		return false;
	}
	const bool started = LaunchBoundProcess(launchToken, executablePath, argumentTail,
		workingDirectory, xucrun::broker::LaunchVisibility::Normal,
		processId, &effectiveWorkingDirectory, error, failedStage);
	CloseHandle(launchToken);
	return started;
}

bool LaunchSecureDesktopClientApproval(HANDLE clientProcess,
	const std::wstring& expectedUserSid, DWORD sessionId,
	const std::wstring& helperPath, const std::wstring& helperArgument,
	const std::wstring& cancellationEventName, HANDLE cancellationEvent,
	HANDLE& hostProcess, HANDLE& helperProcess, DWORD& hostProcessId,
	DWORD& helperProcessId, DWORD& error,
	std::wstring& failedStage)
{
	hostProcess = nullptr;
	helperProcess = nullptr;
	hostProcessId = 0;
	helperProcessId = 0;
	error = NO_ERROR;
	HANDLE clientToken = nullptr;
	failedStage = L"secure desktop client token";
	if (!OpenProcessToken(clientProcess, TOKEN_QUERY | TOKEN_DUPLICATE, &clientToken))
	{
		error = GetLastError();
		return false;
	}
	std::wstring clientSid;
	DWORD clientSessionId = 0;
	if (!ReadTokenIdentity(clientToken, clientSid, clientSessionId, error) ||
		_wcsicmp(clientSid.c_str(), expectedUserSid.c_str()) != 0 ||
		clientSessionId != sessionId || !IsHighIntegrityToken(clientToken, error))
	{
		if (error == NO_ERROR) error = ERROR_ACCESS_DENIED;
		CloseHandle(clientToken);
		return false;
	}
	HANDLE helperToken = nullptr;
	failedStage = L"secure desktop client DuplicateTokenEx";
	if (!DuplicatePrimaryLaunchToken(clientToken, helperToken, error))
	{
		CloseHandle(clientToken);
		return false;
	}
	CloseHandle(clientToken);
	failedStage = L"secure desktop client privilege";
	if (!EnablePrivilege(SE_ASSIGNPRIMARYTOKEN_NAME, error) ||
		!EnablePrivilege(SE_INCREASE_QUOTA_NAME, error) ||
		!EnablePrivilege(SE_TCB_NAME, error) ||
		!GrantInteractiveDesktopAccessInSession(
			helperToken, sessionId, error, failedStage))
	{
		CloseHandle(helperToken);
		return false;
	}
	const bool started = LaunchSecureDesktopHostProcess(helperToken, sessionId,
		helperPath, helperArgument, cancellationEventName, cancellationEvent,
		hostProcess, helperProcess,
		hostProcessId, helperProcessId, error, failedStage);
	CloseHandle(helperToken);
	return started;
}

bool LaunchSecureDesktopCredentialApproval(HANDLE clientProcess,
	const AccountIdentity& identity, const xucrun::SensitiveString& password,
	DWORD sessionId, const std::wstring& helperPath,
	const std::wstring& helperArgument, const std::wstring& cancellationEventName,
	HANDLE cancellationEvent, HANDLE& hostProcess,
	HANDLE& helperProcess, DWORD& hostProcessId, DWORD& helperProcessId, DWORD& error,
	std::wstring& failedStage)
{
	hostProcess = nullptr;
	helperProcess = nullptr;
	hostProcessId = 0;
	helperProcessId = 0;
	error = NO_ERROR;
	failedStage = L"secure desktop LogonUserW";
	HANDLE token = nullptr;
	if (LogonAccountWithPassword(identity, password, token, error) !=
		CredentialValidationStatus::Succeeded)
	{
		return false;
	}

	HANDLE clientToken = nullptr;
	failedStage = L"secure desktop same-account client token";
	if (!OpenProcessToken(clientProcess, TOKEN_QUERY | TOKEN_DUPLICATE, &clientToken))
	{
		error = GetLastError();
		CloseHandle(token);
		return false;
	}
	std::wstring clientSid;
	DWORD clientTokenSessionId = 0;
	if (!ReadTokenIdentity(clientToken, clientSid, clientTokenSessionId, error))
	{
		CloseHandle(clientToken);
		CloseHandle(token);
		return false;
	}
	if (_wcsicmp(clientSid.c_str(), identity.sid.c_str()) == 0 &&
		clientTokenSessionId == sessionId)
	{
		CloseHandle(token);
		token = clientToken;
		clientToken = nullptr;
	}
	const bool crossAccount = clientToken != nullptr;
	if (clientToken) CloseHandle(clientToken);
	if (crossAccount)
	{
		CloseHandle(token);
		token = nullptr;
		if (!LogonAccountWithSessionGroups(clientProcess,
			identity, password, token, error, failedStage))
		{
			return false;
		}
	}

	HANDLE helperToken = nullptr;
	failedStage = L"secure desktop credential DuplicateTokenEx";
	if (!DuplicatePrimaryLaunchToken(token, helperToken, error))
	{
		CloseHandle(token);
		return false;
	}
	CloseHandle(token);
	failedStage = L"secure desktop credential privilege";
	if (!EnablePrivilege(SE_ASSIGNPRIMARYTOKEN_NAME, error) ||
		!EnablePrivilege(SE_INCREASE_QUOTA_NAME, error) ||
		!EnablePrivilege(SE_TCB_NAME, error))
	{
		CloseHandle(helperToken);
		return false;
	}
	failedStage = L"secure desktop credential session";
	if (!SetTokenInformation(helperToken, TokenSessionId, &sessionId, sizeof(sessionId)) ||
		!GrantInteractiveDesktopAccessInSession(
			helperToken, sessionId, error, failedStage))
	{
		if (error == NO_ERROR) error = GetLastError();
		CloseHandle(helperToken);
		return false;
	}
	std::wstring tokenSid;
	DWORD tokenSessionId = 0;
	failedStage = L"secure desktop credential identity";
	if (!ReadTokenIdentity(helperToken, tokenSid, tokenSessionId, error) ||
		tokenSessionId != sessionId ||
		_wcsicmp(tokenSid.c_str(), identity.sid.c_str()) != 0)
	{
		if (error == NO_ERROR) error = ERROR_ACCESS_DENIED;
		CloseHandle(helperToken);
		return false;
	}
	failedStage = L"secure desktop LoadUserProfileW";
	if (!GetUserProfileCache().EnsureLoaded(helperToken, identity, error))
	{
		CloseHandle(helperToken);
		return false;
	}
	const bool started = LaunchSecureDesktopHostProcess(helperToken, sessionId,
		helperPath, helperArgument, cancellationEventName, cancellationEvent,
		hostProcess, helperProcess,
		hostProcessId, helperProcessId, error, failedStage);
	CloseHandle(helperToken);
	return started;
}

// Applies ACL extensions only for a valid logon SID in the current helper session.
bool RunInteractiveDesktopAccessHelper(
	const std::wstring& logonSid,
	DWORD& error)
{
	error = NO_ERROR;
	std::wstring currentSid;
	HANDLE token = nullptr;
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
	{
		error = GetLastError();
		return false;
	}
	DWORD requiredSize = 0;
	GetTokenInformation(token, TokenUser, nullptr, 0, &requiredSize);
	std::vector<std::uint8_t> userBuffer(requiredSize);
	const bool readUser = requiredSize != 0 && GetTokenInformation(token,
		TokenUser, userBuffer.data(), requiredSize, &requiredSize) == TRUE;
	CloseHandle(token);
	if (!readUser || !IsWellKnownSid(
		reinterpret_cast<TOKEN_USER*>(userBuffer.data())->User.Sid,
		WinLocalSystemSid))
	{
		error = readUser ? ERROR_ACCESS_DENIED : GetLastError();
		return false;
	}

	PSID sid = nullptr;
	if (!ConvertStringSidToSidW(logonSid.c_str(), &sid))
	{
		error = GetLastError();
		return false;
	}
	if (!IsValidSid(sid))
	{
		LocalFree(sid);
		error = ERROR_INVALID_SID;
		return false;
	}
	const bool granted = GrantInteractiveDesktopAccess(sid, error) &&
		GrantBaseNamedObjectsAccess(sid, error);
	LocalFree(sid);
	return granted;
}
