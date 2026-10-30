// Purpose: Internal implementation fragment for SessionTestLauncher.cpp.
// Approach: Provides implementation helpers included by the owning translation unit to keep the related flow grouped.
//
// Inclusion contract:
// - This file is included exactly once by SessionTestLauncher.cpp.
// - It shares that translation unit's private token, desktop, profile, and process helpers.
// - It is not a standalone translation unit and must not expose internal security state.
// - Token validation, handle ownership, cleanup order, and failure-stage reporting are preserved.

// Launches a process with the supplied token and the previously validated object paths.
	bool LaunchBoundProcess(HANDLE token,
		const std::wstring& executablePath,
		const std::wstring& argumentTail,
		const std::wstring& workingDirectory,
		xucrun::broker::LaunchVisibility visibility,
		DWORD& processId, std::wstring* launchedWorkingDirectory,
		DWORD& error, std::wstring& failedStage)
	{
		std::wstring effectiveWorkingDirectory;
		if (!ValidateLaunchObjectAccess(token, executablePath,
			workingDirectory, effectiveWorkingDirectory, error, failedStage))
		{
			return false;
		}
		BrokerTemporaryBatch temporaryBatch;
		if (!PrepareBrokerTemporaryBatch(token,
			executablePath, argumentTail, effectiveWorkingDirectory,
			temporaryBatch, error))
		{
			failedStage = L"broker temporary batch";
			return false;
		}
		const std::wstring& launchExecutablePath = temporaryBatch.executablePath;
		const std::wstring& launchArgumentTail = temporaryBatch.argumentTail;
		const std::wstring& launchWorkingDirectory = temporaryBatch.workingDirectory;
		if (temporaryBatch.active() && !ValidateLaunchObjectAccess(token,
			launchExecutablePath, launchWorkingDirectory,
			effectiveWorkingDirectory, error, failedStage))
		{
			return false;
		}
		std::wstring command = L"\"" + launchExecutablePath + L"\"";
		if (!launchArgumentTail.empty())
			command += L" " + launchArgumentTail;
		std::vector<wchar_t> commandLine(command.begin(), command.end());
		commandLine.push_back(L'\0');

		void* environment = nullptr;
		failedStage = L"CreateEnvironmentBlock";
		if (!CreateEnvironmentBlock(&environment, token, FALSE))
		{
			error = GetLastError();
			return false;
		}
		STARTUPINFOW startup = {};
		startup.cb = sizeof(startup);
		startup.lpDesktop = const_cast<LPWSTR>(L"winsta0\\default");
		startup.dwFlags = STARTF_USESHOWWINDOW;
		startup.wShowWindow = visibility != xucrun::broker::LaunchVisibility::Normal
			? SW_HIDE
			: SW_SHOWNORMAL;
		PROCESS_INFORMATION process = {};
		failedStage = L"CreateProcessAsUserW";
		const BOOL started = CreateProcessAsUserW(
			token, launchExecutablePath.c_str(), commandLine.data(), nullptr, nullptr, FALSE,
			CREATE_NEW_CONSOLE | CREATE_UNICODE_ENVIRONMENT, environment,
			effectiveWorkingDirectory.empty() ? nullptr : effectiveWorkingDirectory.c_str(),
			&startup, &process);
		error = started ? NO_ERROR : GetLastError();
		DestroyEnvironmentBlock(environment);
		if (!started)
			return false;
		if (!RetainBrokerTemporaryBatchUntilProcessExit(
			temporaryBatch, process.hProcess, error))
		{
			TerminateProcess(process.hProcess, error);
			WaitForSingleObject(process.hProcess, INFINITE);
			CloseHandle(process.hThread);
			CloseHandle(process.hProcess);
			failedStage = L"broker temporary batch retention";
			return false;
		}

		failedStage = L"launched process lifetime";
		const DWORD waitResult = WaitForSingleObject(process.hProcess, 1000);
		if (waitResult == WAIT_OBJECT_0)
		{
			DWORD exitCode = STILL_ACTIVE;
			const bool readExitCode = GetExitCodeProcess(process.hProcess, &exitCode) == TRUE;
			if (!readExitCode || exitCode == STILL_ACTIVE || exitCode != NO_ERROR)
			{
				error = readExitCode && exitCode != STILL_ACTIVE
					? exitCode
					: ERROR_PROCESS_ABORTED;
				CloseHandle(process.hThread);
				CloseHandle(process.hProcess);
				return false;
			}
		}
		else if (waitResult != WAIT_TIMEOUT)
		{
			error = GetLastError();
			CloseHandle(process.hThread);
			CloseHandle(process.hProcess);
			return false;
		}
		processId = process.dwProcessId;
		if (launchedWorkingDirectory)
			*launchedWorkingDirectory = effectiveWorkingDirectory;
		failedStage.clear();
		CloseHandle(process.hThread);
		CloseHandle(process.hProcess);
		return true;
	}

	// Quotes a single Windows command-line argument without changing its meaning.
	std::wstring QuoteCommandLineArgument(const std::wstring& value)
	{
		std::wstring quoted = L"\"";
		size_t backslashCount = 0;
		for (const wchar_t character : value)
		{
			if (character == L'\\')
			{
				backslashCount++;
				continue;
			}
			if (character == L'\"')
			{
				quoted.append(backslashCount * 2 + 1, L'\\');
				quoted += character;
				backslashCount = 0;
				continue;
			}
			quoted.append(backslashCount, L'\\');
			backslashCount = 0;
			quoted += character;
		}
		quoted.append(backslashCount * 2, L'\\');
		quoted += L'\"';
		return quoted;
	}

	// Starts the broker binary as a SYSTEM host in the helper session and exposes only
	// the validated primary helper token through the inherited handle list.
	bool LaunchSecureDesktopHostProcess(HANDLE helperToken, DWORD sessionId,
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
		wchar_t brokerPath[32768] = {};
		const DWORD brokerPathLength = GetModuleFileNameW(
			nullptr, brokerPath, static_cast<DWORD>(std::size(brokerPath)));
		if (brokerPathLength == 0 || brokerPathLength >= std::size(brokerPath))
		{
			error = brokerPathLength == 0 ? GetLastError() : ERROR_INSUFFICIENT_BUFFER;
			failedStage = L"secure desktop broker path";
			return false;
		}

		HANDLE inheritedHelperToken = nullptr;
		HANDLE processIdRead = nullptr;
		HANDLE processIdWrite = nullptr;
		SECURITY_ATTRIBUTES pipeSecurity = { sizeof(pipeSecurity), nullptr, TRUE };
		failedStage = L"secure desktop helper token handle";
		if (!DuplicateHandle(GetCurrentProcess(), helperToken, GetCurrentProcess(),
			&inheritedHelperToken, 0, TRUE, DUPLICATE_SAME_ACCESS) ||
			!CreatePipe(&processIdRead, &processIdWrite, &pipeSecurity,
				sizeof(std::uint64_t)) ||
			!SetHandleInformation(processIdRead, HANDLE_FLAG_INHERIT, 0))
		{
			error = GetLastError();
			if (inheritedHelperToken) CloseHandle(inheritedHelperToken);
			if (processIdRead) CloseHandle(processIdRead);
			if (processIdWrite) CloseHandle(processIdWrite);
			return false;
		}

		HANDLE processToken = nullptr;
		HANDLE sessionToken = nullptr;
		failedStage = L"secure desktop SYSTEM session token";
		if (!OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE | TOKEN_QUERY,
			&processToken) ||
			!DuplicateTokenEx(processToken,
				TOKEN_ASSIGN_PRIMARY | TOKEN_DUPLICATE | TOKEN_QUERY |
				TOKEN_ADJUST_DEFAULT | TOKEN_ADJUST_SESSIONID,
				nullptr, SecurityImpersonation, TokenPrimary, &sessionToken))
		{
			error = GetLastError();
			if (processToken) CloseHandle(processToken);
			CloseHandle(inheritedHelperToken);
			CloseHandle(processIdRead);
			CloseHandle(processIdWrite);
			return false;
		}
		CloseHandle(processToken);
		if (!SetTokenInformation(sessionToken, TokenSessionId,
			&sessionId, sizeof(sessionId)))
		{
			error = GetLastError();
			CloseHandle(sessionToken);
			CloseHandle(inheritedHelperToken);
			CloseHandle(processIdRead);
			CloseHandle(processIdWrite);
			return false;
		}

		std::wstring command = QuoteCommandLineArgument(brokerPath) +
			L" /secure-desktop-host " +
			std::to_wstring(reinterpret_cast<std::uintptr_t>(inheritedHelperToken)) +
			L" " + std::to_wstring(reinterpret_cast<std::uintptr_t>(processIdWrite)) +
			L" " + QuoteCommandLineArgument(cancellationEventName) +
			L" " + QuoteCommandLineArgument(helperPath) +
			L" " + QuoteCommandLineArgument(helperArgument);
		std::vector<wchar_t> commandLine(command.begin(), command.end());
		commandLine.push_back(L'\0');
		void* environment = nullptr;
		failedStage = L"secure desktop SYSTEM environment";
		if (!CreateEnvironmentBlock(&environment, sessionToken, FALSE))
		{
			error = GetLastError();
			CloseHandle(sessionToken);
			CloseHandle(inheritedHelperToken);
			CloseHandle(processIdRead);
			CloseHandle(processIdWrite);
			return false;
		}

		SIZE_T attributeListSize = 0;
		InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeListSize);
		std::vector<std::uint8_t> attributeBuffer(attributeListSize);
		auto* attributeList = reinterpret_cast<PPROC_THREAD_ATTRIBUTE_LIST>(
			attributeBuffer.data());
		STARTUPINFOEXW startup = {};
		startup.StartupInfo.cb = sizeof(startup);
		startup.StartupInfo.lpDesktop = const_cast<LPWSTR>(L"winsta0\\default");
		startup.lpAttributeList = attributeList;
		const bool initialized = attributeListSize != 0 &&
			InitializeProcThreadAttributeList(attributeList, 1, 0,
				&attributeListSize) == TRUE;
		HANDLE inheritedHandles[] = { inheritedHelperToken, processIdWrite };
		const bool attributesReady = initialized && UpdateProcThreadAttribute(
			attributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
			inheritedHandles, sizeof(inheritedHandles), nullptr, nullptr) == TRUE;
		PROCESS_INFORMATION host = {};
		failedStage = L"secure desktop host start";
		const BOOL started = attributesReady && CreateProcessAsUserW(
			sessionToken, brokerPath, commandLine.data(), nullptr, nullptr, TRUE,
			CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT,
			environment, nullptr, &startup.StartupInfo, &host);
		error = started ? NO_ERROR : GetLastError();
		if (initialized) DeleteProcThreadAttributeList(attributeList);
		DestroyEnvironmentBlock(environment);
		CloseHandle(sessionToken);
		CloseHandle(inheritedHelperToken);
		CloseHandle(processIdWrite);
		if (!started)
		{
			CloseHandle(processIdRead);
			return false;
		}
		CloseHandle(host.hThread);
		failedStage = L"secure desktop child process handle";
		std::uint64_t helperProcessValue = 0;
		DWORD bytesRead = 0;
		bool processHandleReadSucceeded = false;
		const ULONGLONG reportDeadline = GetTickCount64() +
			xucrun::broker::ElevatedLaunchHelperTimeoutMilliseconds;
		while (GetTickCount64() < reportDeadline)
		{
			DWORD available = 0;
			if (!PeekNamedPipe(processIdRead, nullptr, 0, nullptr, &available, nullptr))
				break;
			if (available >= sizeof(helperProcessValue))
			{
				processHandleReadSucceeded = ReadFile(processIdRead,
					&helperProcessValue, sizeof(helperProcessValue), &bytesRead, nullptr) == TRUE &&
					bytesRead == sizeof(helperProcessValue) && helperProcessValue != 0;
				break;
			}
			if (WaitForSingleObject(host.hProcess, 25) == WAIT_OBJECT_0)
				break;
		}
		CloseHandle(processIdRead);
		helperProcess = processHandleReadSucceeded
			? reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(helperProcessValue))
			: nullptr;
		helperProcessId = helperProcess ? GetProcessId(helperProcess) : 0;
		if (!helperProcess || helperProcessId == 0)
		{
			error = processHandleReadSucceeded ? GetLastError() : ERROR_TIMEOUT;
			if (error == NO_ERROR) error = ERROR_INVALID_DATA;
			if (cancellationEvent) SetEvent(cancellationEvent);
			CloseHandle(host.hProcess);
			if (helperProcess) CloseHandle(helperProcess);
			helperProcess = nullptr;
			return false;
		}
		hostProcess = host.hProcess;
		hostProcessId = host.dwProcessId;
		failedStage.clear();
		return true;
	}

	// Identifies batch files that must be launched through a trusted command interpreter.
	bool IsBatchFile(const std::wstring& path)
	{
		const size_t extension = path.find_last_of(L'.');
		return extension != std::wstring::npos &&
			(_wcsicmp(path.c_str() + extension, L".bat") == 0 ||
			 _wcsicmp(path.c_str() + extension, L".cmd") == 0);
	}

	// Creates a logon token and adopts only session groups from the validated client.
	bool LogonAccountWithSessionGroups(HANDLE clientProcess,
		const AccountIdentity& identity,
		const xucrun::SensitiveString& password,
		HANDLE& token,
		DWORD& error,
		std::wstring& failedStage)
	{
		token = nullptr;
		using LogonUserExExWFunction = BOOL(WINAPI*)(
			LPCWSTR, LPCWSTR, LPCWSTR, DWORD, DWORD, PTOKEN_GROUPS, PHANDLE,
			PSID*, PVOID*, LPDWORD, PQUOTA_LIMITS);

		failedStage = L"cross-account broker privilege";
		if (!EnablePrivilege(SE_TCB_NAME, error) ||
			!EnablePrivilege(SE_ASSIGNPRIMARYTOKEN_NAME, error) ||
			!EnablePrivilege(SE_INCREASE_QUOTA_NAME, error))
		{
			return false;
		}

		HANDLE clientToken = nullptr;
		failedStage = L"cross-account client logon SID";
		if (!OpenProcessToken(clientProcess, TOKEN_QUERY, &clientToken))
		{
			error = GetLastError();
			return false;
		}
		std::vector<std::uint8_t> clientLogonSid;
		const bool logonSidRead = ReadLogonSid(clientToken, clientLogonSid, error);
		CloseHandle(clientToken);
		if (!logonSidRead)
			return false;

		BYTE localSid[SECURITY_MAX_SID_SIZE] = {};
		DWORD localSidSize = sizeof(localSid);
		failedStage = L"cross-account LOCAL SID";
		if (!CreateWellKnownSid(WinLocalSid, nullptr, localSid, &localSidSize))
		{
			error = GetLastError();
			return false;
		}

		struct AdditionalTokenGroups
		{
			DWORD count;
			SID_AND_ATTRIBUTES groups[2];
		} additionalGroups = {};
		additionalGroups.count = 2;
		additionalGroups.groups[0] = {
			clientLogonSid.data(),
			SE_GROUP_MANDATORY | SE_GROUP_ENABLED_BY_DEFAULT |
			SE_GROUP_ENABLED | SE_GROUP_LOGON_ID };
		additionalGroups.groups[1] = {
			localSid,
			SE_GROUP_MANDATORY | SE_GROUP_ENABLED_BY_DEFAULT | SE_GROUP_ENABLED };

		const HMODULE advapi = GetModuleHandleW(L"advapi32.dll");
		const auto logonUserExEx = advapi
			? reinterpret_cast<LogonUserExExWFunction>(
				GetProcAddress(advapi, "LogonUserExExW"))
			: nullptr;
		if (!logonUserExEx)
		{
			error = ERROR_PROC_NOT_FOUND;
			failedStage = L"cross-account LogonUserExExW resolve";
			return false;
		}

		failedStage = L"cross-account broker LogonUserExExW";
		if (!logonUserExEx(identity.userName.c_str(),
			identity.domain.empty() ? nullptr : identity.domain.c_str(),
			password.c_str(), LOGON32_LOGON_INTERACTIVE, LOGON32_PROVIDER_DEFAULT,
			reinterpret_cast<PTOKEN_GROUPS>(&additionalGroups), &token,
			nullptr, nullptr, nullptr, nullptr))
		{
			error = GetLastError();
			return false;
		}
		return true;
	}

	// Tests the secondary logon path with a test process bound to the account and session.
	bool LaunchSecondaryLogonBrokerTest(HANDLE clientProcess,
		const AccountIdentity& identity,
		const xucrun::SensitiveString& password,
		DWORD sessionId,
		DWORD& processId,
		DWORD& error,
		std::wstring& failedStage)
	{
		processId = 0;
		HANDLE token = nullptr;
		if (!LogonAccountWithSessionGroups(clientProcess,
			identity, password, token, error, failedStage))
		{
			return false;
		}

		HANDLE launchToken = nullptr;
		failedStage = L"cross-account DuplicateTokenEx";
		if (!DuplicatePrimaryLaunchToken(token, launchToken, error))
		{
			CloseHandle(token);
			return false;
		}
		CloseHandle(token);
		token = launchToken;

		failedStage = L"cross-account TokenSessionId";
		if (!SetTokenInformation(token, TokenSessionId, &sessionId, sizeof(sessionId)))
		{
			error = GetLastError();
			CloseHandle(token);
			return false;
		}
		if (!GrantInteractiveDesktopAccessInSession(
			token, sessionId, error, failedStage))
		{
			CloseHandle(token);
			return false;
		}

		std::wstring tokenSid;
		DWORD tokenSessionId = 0;
		failedStage = L"cross-account token identity";
		if (!ReadTokenIdentity(token, tokenSid, tokenSessionId, error) ||
			tokenSessionId != sessionId ||
			_wcsicmp(tokenSid.c_str(), identity.sid.c_str()) != 0)
		{
			if (error == NO_ERROR)
				error = ERROR_ACCESS_DENIED;
			CloseHandle(token);
			return false;
		}

		failedStage = L"cross-account LoadUserProfileW";
		if (!GetUserProfileCache().EnsureLoaded(token, identity, error))
		{
			CloseHandle(token);
			return false;
		}
		const bool started = LaunchFixedTestConsole(
			token, processId, error, failedStage);
		CloseHandle(token);
		return started;
	}

	// Launches the shell wrapper with tightly restricted handle inheritance and validates its response.
	bool LaunchShellWrapper(HANDLE token,
		const std::wstring& clientPath,
		const std::wstring& executablePath,
		const std::wstring& argumentTail,
		const std::wstring& workingDirectory,
		bool requestElevation,
		xucrun::broker::LaunchVisibility visibility,
		DWORD& processId, std::wstring* launchedWorkingDirectory,
		DWORD& error, std::wstring& failedStage, HANDLE* launchedProcessHandle = nullptr)
	{
		if (launchedProcessHandle) *launchedProcessHandle = nullptr;
		std::wstring effectiveWorkingDirectory;
		if (!ValidateLaunchObjectAccess(token, executablePath,
			workingDirectory, effectiveWorkingDirectory, error, failedStage))
		{
			return false;
		}
		BrokerTemporaryBatch temporaryBatch;
		if (!PrepareBrokerTemporaryBatch(token,
			executablePath, argumentTail, effectiveWorkingDirectory,
			temporaryBatch, error))
		{
			failedStage = L"broker temporary batch";
			return false;
		}
		const std::wstring& launchExecutablePath = temporaryBatch.executablePath;
		const std::wstring& launchArgumentTail = temporaryBatch.argumentTail;
		const std::wstring& launchWorkingDirectory = temporaryBatch.workingDirectory;
		if (temporaryBatch.active() && !ValidateLaunchObjectAccess(token,
			launchExecutablePath, launchWorkingDirectory,
			effectiveWorkingDirectory, error, failedStage))
		{
			return false;
		}

		HANDLE resultRead = nullptr;
		HANDLE resultWrite = nullptr;
		SECURITY_ATTRIBUTES pipeSecurity = { sizeof(pipeSecurity), nullptr, TRUE };
		failedStage = L"elevation result pipe";
		if (!CreatePipe(&resultRead, &resultWrite, &pipeSecurity,
			sizeof(xucrun::broker::ElevatedLaunchHelperResult)) ||
			!SetHandleInformation(resultRead, HANDLE_FLAG_INHERIT, 0))
		{
			error = GetLastError();
			if (resultRead) CloseHandle(resultRead);
			if (resultWrite) CloseHandle(resultWrite);
			return false;
		}
		HANDLE resultAcknowledgement = CreateEventW(
			&pipeSecurity, FALSE, FALSE, nullptr);
		if (!resultAcknowledgement)
		{
			error = GetLastError();
			CloseHandle(resultRead);
			CloseHandle(resultWrite);
			return false;
		}

		std::wstring command = QuoteCommandLineArgument(clientPath) + L" /internal-elevate";
		if (requestElevation)
			command += L" /internal-elevated";
		if (visibility == xucrun::broker::LaunchVisibility::Hidden)
			command += L" /internal-hidden";
		else if (visibility == xucrun::broker::LaunchVisibility::HiddenMonitor)
			command += L" /internal-hidden-monitor";
		command += L" /internal-broker-result:" +
			std::to_wstring(reinterpret_cast<std::uintptr_t>(resultWrite));
		command += L" /internal-broker-ack:" +
			std::to_wstring(reinterpret_cast<std::uintptr_t>(resultAcknowledgement));
		if (!effectiveWorkingDirectory.empty())
			command += L" /workdir " + QuoteCommandLineArgument(effectiveWorkingDirectory);
		command += L" " + QuoteCommandLineArgument(launchExecutablePath);
		if (!launchArgumentTail.empty())
			command += L" " + launchArgumentTail;
		std::vector<wchar_t> commandLine(command.begin(), command.end());
		commandLine.push_back(L'\0');

		void* environment = nullptr;
		failedStage = L"CreateEnvironmentBlock";
		if (!CreateEnvironmentBlock(&environment, token, FALSE))
		{
			error = GetLastError();
			CloseHandle(resultRead);
			CloseHandle(resultWrite);
			CloseHandle(resultAcknowledgement);
			return false;
		}

		SIZE_T attributeListSize = 0;
		InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeListSize);
		std::vector<std::uint8_t> attributeBuffer(attributeListSize);
		auto* attributeList = reinterpret_cast<PPROC_THREAD_ATTRIBUTE_LIST>(
			attributeBuffer.data());
		STARTUPINFOEXW startup = {};
		startup.StartupInfo.cb = sizeof(startup);
		startup.StartupInfo.lpDesktop = const_cast<LPWSTR>(L"winsta0\\default");
		startup.lpAttributeList = attributeList;
		PROCESS_INFORMATION helper = {};
		HANDLE inheritedHandles[] = { resultWrite, resultAcknowledgement };
		failedStage = L"elevation helper handle list";
		const bool attributeListInitialized = attributeListSize != 0 &&
			InitializeProcThreadAttributeList(attributeList, 1, 0,
				&attributeListSize) == TRUE;
		const bool attributesReady = attributeListInitialized &&
			UpdateProcThreadAttribute(attributeList, 0,
				PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inheritedHandles,
				sizeof(inheritedHandles), nullptr, nullptr) == TRUE;
		if (!attributesReady)
		{
			error = GetLastError();
			if (attributeListInitialized)
				DeleteProcThreadAttributeList(startup.lpAttributeList);
			DestroyEnvironmentBlock(environment);
			CloseHandle(resultRead);
			CloseHandle(resultWrite);
			CloseHandle(resultAcknowledgement);
			return false;
		}

		failedStage = L"elevation helper start";
		const BOOL started = CreateProcessAsUserW(token, clientPath.c_str(),
			commandLine.data(), nullptr, nullptr, TRUE,
			CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT |
			EXTENDED_STARTUPINFO_PRESENT,
			environment, nullptr, &startup.StartupInfo, &helper);
		error = started ? NO_ERROR : GetLastError();
		DeleteProcThreadAttributeList(startup.lpAttributeList);
		DestroyEnvironmentBlock(environment);
		CloseHandle(resultWrite);
		if (!started)
		{
			CloseHandle(resultRead);
			CloseHandle(resultAcknowledgement);
			return false;
		}
		CloseHandle(helper.hThread);

		xucrun::broker::ElevatedLaunchHelperResult result;
		DWORD bytesRead = 0;
		failedStage = L"elevation helper result";
		bool resultReadSucceeded = false;
		const ULONGLONG resultDeadline = GetTickCount64() +
			xucrun::broker::ElevatedLaunchHelperTimeoutMilliseconds;
		while (GetTickCount64() < resultDeadline)
		{
			DWORD available = 0;
			if (!PeekNamedPipe(resultRead, nullptr, 0, nullptr, &available, nullptr))
				break;
			if (available >= sizeof(result))
			{
				resultReadSucceeded = ReadFile(resultRead, &result,
					sizeof(result), &bytesRead, nullptr) == TRUE;
				break;
			}
			WaitForSingleObject(helper.hProcess, 25);
		}
		CloseHandle(resultRead);
		HANDLE reportedProcessHandle = nullptr;
		DWORD processHandleError = NO_ERROR;
		if (resultReadSucceeded && bytesRead == sizeof(result) &&
			result.magic == xucrun::broker::ElevatedLaunchHelperResultMagic &&
			result.windowsError == NO_ERROR && result.processId != 0 &&
			result.processHandle != 0)
		{
			if (!DuplicateHandle(helper.hProcess,
				reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(result.processHandle)),
				GetCurrentProcess(), &reportedProcessHandle, 0, FALSE,
				DUPLICATE_SAME_ACCESS))
			{
				processHandleError = GetLastError();
			}
		}
		auto finishUnconfirmedLaunch = [&]()
		{
			CloseHandle(resultAcknowledgement);
			DWORD retentionError = NO_ERROR;
			if (temporaryBatch.active() &&
				RetainBrokerTemporaryBatchUntilProcessExit(
					temporaryBatch, helper.hProcess, retentionError))
			{
				CloseHandle(helper.hProcess);
				return;
			}
			if (!temporaryBatch.active())
				TerminateProcess(helper.hProcess, ERROR_TIMEOUT);
			WaitForSingleObject(helper.hProcess,
				temporaryBatch.active() ? INFINITE : 5000);
			CloseHandle(helper.hProcess);
		};
		if (!resultReadSucceeded || bytesRead != sizeof(result) ||
			result.magic != xucrun::broker::ElevatedLaunchHelperResultMagic ||
			result.windowsError != NO_ERROR || result.processId == 0 ||
			result.processHandle == 0 || !reportedProcessHandle)
		{
			error = resultReadSucceeded && bytesRead == sizeof(result) &&
				result.magic == xucrun::broker::ElevatedLaunchHelperResultMagic
				? (result.windowsError == NO_ERROR
					? (processHandleError == NO_ERROR
						? ERROR_INVALID_DATA : processHandleError)
					: result.windowsError)
				: ERROR_INVALID_DATA;
			if (reportedProcessHandle)
				CloseHandle(reportedProcessHandle);
			finishUnconfirmedLaunch();
			return false;
		}
		if (GetProcessId(reportedProcessHandle) != result.processId)
		{
			CloseHandle(reportedProcessHandle);
			finishUnconfirmedLaunch();
			error = ERROR_INVALID_DATA;
			return false;
		}
		if (!RetainBrokerTemporaryBatchUntilProcessExit(
			temporaryBatch, reportedProcessHandle, error))
		{
			CloseHandle(reportedProcessHandle);
			finishUnconfirmedLaunch();
			failedStage = L"broker temporary batch retention";
			return false;
		}
		if (!SetEvent(resultAcknowledgement))
		{
			error = GetLastError();
			CloseHandle(resultAcknowledgement);
			CloseHandle(helper.hProcess);
			CloseHandle(reportedProcessHandle);
			failedStage = L"elevation helper acknowledgement";
			return false;
		}
		CloseHandle(resultAcknowledgement);
		const DWORD wrapperWait = WaitForSingleObject(helper.hProcess, 5000);
		if (wrapperWait != WAIT_OBJECT_0)
			TerminateProcess(helper.hProcess, ERROR_TIMEOUT);
		CloseHandle(helper.hProcess);
		processId = result.processId;
		if (launchedProcessHandle)
			*launchedProcessHandle = reportedProcessHandle;
		else
			CloseHandle(reportedProcessHandle);
		if (launchedWorkingDirectory)
			*launchedWorkingDirectory = effectiveWorkingDirectory;
		error = NO_ERROR;
		failedStage.clear();
		return true;
	}

	// Launches a fixed test console with the validated token.
	bool LaunchFixedTestConsole(HANDLE token, DWORD& processId,
		DWORD& error, std::wstring& failedStage)
	{
		wchar_t systemDirectory[MAX_PATH] = {};
		failedStage = L"system directory";
		const UINT systemDirectoryLength = GetSystemDirectoryW(
			systemDirectory, static_cast<UINT>(std::size(systemDirectory)));
		if (systemDirectoryLength == 0 ||
			systemDirectoryLength >= std::size(systemDirectory))
		{
			error = GetLastError();
			if (error == NO_ERROR)
				error = ERROR_INSUFFICIENT_BUFFER;
			return false;
		}
		const std::wstring executablePath = std::wstring(systemDirectory) + L"\\cmd.exe";
		return LaunchBoundProcess(token, executablePath,
			L"/d /k \"title xucRun Credential Test & echo Target identity: & whoami "
			L"& echo. & echo Target groups: & whoami /groups\"",
			systemDirectory, xucrun::broker::LaunchVisibility::Normal,
			processId, nullptr, error, failedStage);
	}
