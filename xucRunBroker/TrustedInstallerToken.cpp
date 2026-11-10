// Purpose: Obtains and validates an authentic TrustedInstaller service token and tests its use.
// Approach: Binds the token source to SCM state, enforces deterministic token rules, and supports a fixed child verifier.
#include "TrustedInstallerToken.h"

#include <Windows.h>
#include <sddl.h>
#include <userenv.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace
{
	constexpr wchar_t TrustedInstallerServiceName[] = L"TrustedInstaller";
	constexpr wchar_t TrustedInstallerSid[] =
		L"S-1-5-80-956008885-3418522649-1831038044-1853292631-2271478464";
	constexpr DWORD ServiceStartTimeoutMilliseconds = 30000;
	constexpr DWORD ChildTimeoutMilliseconds = 30000;

	class Handle
	{
	public:
		explicit Handle(HANDLE value = nullptr) noexcept : value_(value) {}
		~Handle()
		{
			if (value_)
				CloseHandle(value_);
		}
		Handle(const Handle&) = delete;
		Handle& operator=(const Handle&) = delete;
		HANDLE get() const noexcept { return value_; }
		HANDLE* put() noexcept { return &value_; }
		HANDLE release() noexcept
		{
			HANDLE value = value_;
			value_ = nullptr;
			return value;
		}
		explicit operator bool() const noexcept { return value_ != nullptr; }

	private:
		HANDLE value_;
	};

	class ServiceHandle
	{
	public:
		explicit ServiceHandle(SC_HANDLE value = nullptr) noexcept : value_(value) {}
		~ServiceHandle()
		{
			if (value_)
				CloseServiceHandle(value_);
		}
		ServiceHandle(const ServiceHandle&) = delete;
		ServiceHandle& operator=(const ServiceHandle&) = delete;
		SC_HANDLE get() const noexcept { return value_; }
		explicit operator bool() const noexcept { return value_ != nullptr; }

	private:
		SC_HANDLE value_;
	};

	bool Fail(const wchar_t* stage, DWORD failure,
		DWORD& error, std::wstring& failedStage)
	{
		error = failure == NO_ERROR ? ERROR_GEN_FAILURE : failure;
		failedStage = stage;
		return false;
	}

	bool ReadTokenInformation(HANDLE token, TOKEN_INFORMATION_CLASS informationClass,
		std::vector<std::uint8_t>& buffer, DWORD& error)
	{
		DWORD size = 0;
		GetTokenInformation(token, informationClass, nullptr, 0, &size);
		if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || size == 0)
		{
			error = GetLastError();
			if (error == NO_ERROR)
				error = ERROR_INVALID_DATA;
			return false;
		}
		buffer.resize(size);
		if (!GetTokenInformation(
			token, informationClass, buffer.data(), size, &size))
		{
			error = GetLastError();
			return false;
		}
		return true;
	}

	bool EnableProcessPrivilege(const wchar_t* privilegeName,
		DWORD& error, std::wstring& failedStage)
	{
		Handle token;
		if (!OpenProcessToken(GetCurrentProcess(),
			TOKEN_QUERY | TOKEN_ADJUST_PRIVILEGES, token.put()))
			return Fail(L"open broker token for privilege", GetLastError(),
				error, failedStage);

		LUID privilege = {};
		if (!LookupPrivilegeValueW(nullptr, privilegeName, &privilege))
			return Fail(L"resolve broker privilege", GetLastError(),
				error, failedStage);

		TOKEN_PRIVILEGES privileges = {};
		privileges.PrivilegeCount = 1;
		privileges.Privileges[0].Luid = privilege;
		privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
		SetLastError(NO_ERROR);
		if (!AdjustTokenPrivileges(
			token.get(), FALSE, &privileges, 0, nullptr, nullptr))
			return Fail(L"enable broker privilege", GetLastError(),
				error, failedStage);
		const DWORD adjustmentError = GetLastError();
		if (adjustmentError != NO_ERROR)
			return Fail(privilegeName, adjustmentError, error, failedStage);
		return true;
	}

	bool ValidateTrustedInstallerToken(HANDLE token, const DWORD* expectedSessionId,
		LUID* authenticationId, DWORD& error, std::wstring& failedStage)
	{
		TrustedInstallerTokenRuleInput rules;
		std::vector<std::uint8_t> userBuffer;
		if (!ReadTokenInformation(token, TokenUser, userBuffer, error))
			return Fail(L"read token user", error, error, failedStage);
		BYTE systemSid[SECURITY_MAX_SID_SIZE] = {};
		DWORD systemSidSize = sizeof(systemSid);
		if (!CreateWellKnownSid(
			WinLocalSystemSid, nullptr, systemSid, &systemSidSize))
			return Fail(L"create LocalSystem SID", GetLastError(),
				error, failedStage);
		const auto* user = reinterpret_cast<const TOKEN_USER*>(userBuffer.data());
		rules.userIsLocalSystem =
			IsValidSid(user->User.Sid) && EqualSid(user->User.Sid, systemSid);

		PSID trustedInstallerSid = nullptr;
		if (!ConvertStringSidToSidW(TrustedInstallerSid, &trustedInstallerSid))
			return Fail(L"create TrustedInstaller SID", GetLastError(),
				error, failedStage);
		std::vector<std::uint8_t> groupsBuffer;
		const bool groupsRead =
			ReadTokenInformation(token, TokenGroups, groupsBuffer, error);
		if (groupsRead)
		{
			const auto* groups =
				reinterpret_cast<const TOKEN_GROUPS*>(groupsBuffer.data());
			for (DWORD index = 0; index < groups->GroupCount; ++index)
			{
				const SID_AND_ATTRIBUTES& group = groups->Groups[index];
				if (IsValidSid(group.Sid) &&
					EqualSid(group.Sid, trustedInstallerSid))
				{
					rules.serviceSidPresent = true;
					rules.serviceSidAttributes = group.Attributes;
					break;
				}
			}
		}
		LocalFree(trustedInstallerSid);
		if (!groupsRead)
			return Fail(L"read token groups", error, error, failedStage);
		DWORD returned = 0;
		if (!GetTokenInformation(token, TokenType,
			&rules.tokenType, sizeof(rules.tokenType), &returned))
			return Fail(L"read token type", GetLastError(), error, failedStage);

		std::vector<std::uint8_t> integrityBuffer;
		if (!ReadTokenInformation(
			token, TokenIntegrityLevel, integrityBuffer, error))
			return Fail(L"read token integrity", error, error, failedStage);
		const auto* integrity =
			reinterpret_cast<const TOKEN_MANDATORY_LABEL*>(integrityBuffer.data());
		if (!IsValidSid(integrity->Label.Sid) ||
			*GetSidSubAuthorityCount(integrity->Label.Sid) == 0)
			return Fail(L"validate token integrity SID", ERROR_INVALID_SID,
				error, failedStage);
		const UCHAR lastSubAuthority =
			*GetSidSubAuthorityCount(integrity->Label.Sid) - 1;
		rules.integrityRid =
			*GetSidSubAuthority(integrity->Label.Sid, lastSubAuthority);

		returned = 0;
		if (!GetTokenInformation(token, TokenSessionId,
			&rules.sessionId, sizeof(rules.sessionId), &returned))
			return Fail(L"read token session", GetLastError(), error, failedStage);

		TOKEN_STATISTICS statistics = {};
		returned = 0;
		if (!GetTokenInformation(token, TokenStatistics,
			&statistics, sizeof(statistics), &returned))
			return Fail(L"read token statistics", GetLastError(),
				error, failedStage);
		if (authenticationId)
			*authenticationId = statistics.AuthenticationId;
		return ValidateTrustedInstallerTokenRules(
			rules, expectedSessionId, error, failedStage);
	}

	bool QueryServiceStatus(SC_HANDLE service, SERVICE_STATUS_PROCESS& status,
		DWORD& error, std::wstring& failedStage)
	{
		DWORD bytesNeeded = 0;
		if (!QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO,
			reinterpret_cast<BYTE*>(&status), sizeof(status), &bytesNeeded))
			return Fail(L"query TrustedInstaller service status",
				GetLastError(), error, failedStage);
		return true;
	}

	bool ReadServiceConfiguration(SC_HANDLE service,
		DWORD& serviceType, bool& runsAsLocalSystem,
		DWORD& error, std::wstring& failedStage)
	{
		serviceType = 0;
		runsAsLocalSystem = false;
		DWORD size = 0;
		QueryServiceConfigW(service, nullptr, 0, &size);
		if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || size == 0)
			return Fail(L"size TrustedInstaller service configuration",
				GetLastError(), error, failedStage);
		std::vector<std::uint8_t> buffer(size);
		auto* configuration =
			reinterpret_cast<QUERY_SERVICE_CONFIGW*>(buffer.data());
		if (!QueryServiceConfigW(service, configuration, size, &size))
			return Fail(L"query TrustedInstaller service configuration",
				GetLastError(), error, failedStage);
		serviceType = configuration->dwServiceType;
		runsAsLocalSystem = configuration->lpServiceStartName &&
			_wcsicmp(configuration->lpServiceStartName, L"LocalSystem") == 0;
		return true;
	}

	bool EnsureServiceRunning(SC_HANDLE service, SERVICE_STATUS_PROCESS& status,
		DWORD& error, std::wstring& failedStage)
	{
		const ULONGLONG deadline =
			GetTickCount64() + ServiceStartTimeoutMilliseconds;
		if (!QueryServiceStatus(service, status, error, failedStage))
			return false;
		if (status.dwCurrentState == SERVICE_STOPPED &&
			!StartServiceW(service, 0, nullptr))
		{
			const DWORD startError = GetLastError();
			if (startError != ERROR_SERVICE_ALREADY_RUNNING)
				return Fail(L"start TrustedInstaller service",
					startError, error, failedStage);
		}
		if (status.dwCurrentState == SERVICE_STOPPED &&
			!QueryServiceStatus(service, status, error, failedStage))
			return false;
		while (status.dwCurrentState != SERVICE_RUNNING)
		{
			if (status.dwCurrentState == SERVICE_STOPPED ||
				GetTickCount64() >= deadline)
				return Fail(L"wait for TrustedInstaller service",
					status.dwWin32ExitCode != NO_ERROR
						? status.dwWin32ExitCode
						: ERROR_SERVICE_REQUEST_TIMEOUT,
					error, failedStage);
			Sleep(std::clamp(status.dwWaitHint / 10, 50UL, 1000UL));
			if (!QueryServiceStatus(service, status, error, failedStage))
				return false;
		}
		if (status.dwProcessId == 0)
			return Fail(L"validate TrustedInstaller service PID",
				ERROR_INVALID_DATA, error, failedStage);
		return true;
	}

	bool ConfirmServiceProcess(SC_HANDLE service, DWORD expectedProcessId,
		DWORD& error, std::wstring& failedStage)
	{
		SERVICE_STATUS_PROCESS status = {};
		if (!QueryServiceStatus(service, status, error, failedStage))
			return false;
		if (status.dwCurrentState != SERVICE_RUNNING ||
			status.dwProcessId != expectedProcessId)
			return Fail(L"revalidate TrustedInstaller service PID",
				ERROR_PROCESS_ABORTED, error, failedStage);
		return true;
	}

	bool GetModulePath(std::wstring& path,
		DWORD& error, std::wstring& failedStage)
	{
		std::vector<wchar_t> buffer(32768);
		const DWORD length = GetModuleFileNameW(
			nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
		if (length == 0 || length >= buffer.size())
			return Fail(L"resolve broker image path",
				length == 0 ? GetLastError() : ERROR_INSUFFICIENT_BUFFER,
				error, failedStage);
		path.assign(buffer.data(), length);
		return true;
	}

	bool LaunchChildVerifier(HANDLE token, DWORD expectedSessionId,
		DWORD& error, std::wstring& failedStage)
	{
		std::wstring executablePath;
		if (!GetModulePath(executablePath, error, failedStage))
			return false;
		std::wstring command = L"\"" + executablePath +
			L"\" /trustedinstaller-token-child-test " +
			std::to_wstring(expectedSessionId);
		std::vector<wchar_t> commandLine(command.begin(), command.end());
		commandLine.push_back(L'\0');

		void* environment = nullptr;
		if (!CreateEnvironmentBlock(&environment, token, FALSE))
			return Fail(L"create TrustedInstaller environment",
				GetLastError(), error, failedStage);

		wchar_t desktop[] = L"winsta0\\default";
		STARTUPINFOW startup = {};
		startup.cb = sizeof(startup);
		startup.lpDesktop = desktop;
		PROCESS_INFORMATION process = {};
		const BOOL started = CreateProcessAsUserW(token, executablePath.c_str(),
			commandLine.data(), nullptr, nullptr, FALSE,
			CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT,
			environment, nullptr, &startup, &process);
		const DWORD startError = started ? NO_ERROR : GetLastError();
		DestroyEnvironmentBlock(environment);
		if (!started)
			return Fail(L"start TrustedInstaller child verifier",
				startError, error, failedStage);

		Handle childProcess(process.hProcess);
		Handle childThread(process.hThread);
		Handle childToken;
		if (!OpenProcessToken(childProcess.get(), TOKEN_QUERY, childToken.put()))
		{
			TerminateProcess(childProcess.get(), ERROR_ACCESS_DENIED);
			return Fail(L"open TrustedInstaller child token",
				GetLastError(), error, failedStage);
		}
		if (!ValidateTrustedInstallerToken(childToken.get(), &expectedSessionId,
			nullptr, error, failedStage))
		{
			TerminateProcess(childProcess.get(), error);
			return false;
		}
		if (ResumeThread(childThread.get()) == static_cast<DWORD>(-1))
		{
			const DWORD resumeError = GetLastError();
			TerminateProcess(childProcess.get(), resumeError);
			return Fail(L"resume TrustedInstaller child verifier",
				resumeError, error, failedStage);
		}
		const DWORD wait =
			WaitForSingleObject(childProcess.get(), ChildTimeoutMilliseconds);
		if (wait != WAIT_OBJECT_0)
		{
			const DWORD waitError =
				wait == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError();
			TerminateProcess(childProcess.get(), waitError);
			WaitForSingleObject(childProcess.get(), 5000);
			return Fail(L"wait for TrustedInstaller child verifier",
				waitError, error, failedStage);
		}
		DWORD exitCode = ERROR_GEN_FAILURE;
		if (!GetExitCodeProcess(childProcess.get(), &exitCode))
			return Fail(L"read TrustedInstaller child result",
				GetLastError(), error, failedStage);
		if (exitCode != NO_ERROR)
			return Fail(L"validate TrustedInstaller child result",
				exitCode, error, failedStage);
		return true;
	}
}

bool ValidateTrustedInstallerTokenRules(
	const TrustedInstallerTokenRuleInput& input,
	const DWORD* expectedSessionId, DWORD& error, std::wstring& failedStage)
{
	error = NO_ERROR;
	failedStage.clear();
	if (!input.userIsLocalSystem)
		return Fail(L"validate token user", ERROR_ACCESS_DENIED,
			error, failedStage);
	if (!input.serviceSidPresent)
		return Fail(L"validate TrustedInstaller SID presence",
			ERROR_ACCESS_DENIED, error, failedStage);
	if ((input.serviceSidAttributes & SE_GROUP_ENABLED) == 0)
		return Fail(L"validate enabled TrustedInstaller SID",
			ERROR_ACCESS_DENIED, error, failedStage);
	if ((input.serviceSidAttributes & SE_GROUP_USE_FOR_DENY_ONLY) != 0)
		return Fail(L"reject deny-only TrustedInstaller SID",
			ERROR_ACCESS_DENIED, error, failedStage);
	if (input.tokenType != TokenPrimary)
		return Fail(L"validate primary token", ERROR_BAD_TOKEN_TYPE,
			error, failedStage);
	if (input.integrityRid < SECURITY_MANDATORY_SYSTEM_RID)
		return Fail(L"validate System integrity", ERROR_ACCESS_DENIED,
			error, failedStage);
	if (expectedSessionId && input.sessionId != *expectedSessionId)
		return Fail(L"validate token session", ERROR_NO_SUCH_LOGON_SESSION,
			error, failedStage);
	return true;
}

bool ValidateTrustedInstallerServiceRules(
	const TrustedInstallerServiceRuleInput& input,
	DWORD& error, std::wstring& failedStage)
{
	error = NO_ERROR;
	failedStage.clear();
	if ((input.serviceType & SERVICE_WIN32_OWN_PROCESS) == 0 ||
		(input.serviceType & SERVICE_WIN32_SHARE_PROCESS) != 0 ||
		!input.runsAsLocalSystem)
		return Fail(L"validate TrustedInstaller service configuration",
			ERROR_SERVICE_LOGON_FAILED, error, failedStage);
	if (input.stateBefore != SERVICE_RUNNING ||
		input.processIdBefore == 0)
		return Fail(L"validate TrustedInstaller service state",
			ERROR_SERVICE_NOT_ACTIVE, error, failedStage);
	if (!input.processHandleAlive ||
		input.openedProcessId != input.processIdBefore)
		return Fail(L"validate TrustedInstaller service process",
			ERROR_PROCESS_ABORTED, error, failedStage);
	if (input.stateAfter != SERVICE_RUNNING ||
		input.processIdAfter != input.processIdBefore)
		return Fail(L"revalidate TrustedInstaller service PID",
			ERROR_PROCESS_ABORTED, error, failedStage);
	return true;
}

bool InspectTrustedInstallerToken(HANDLE token,
	const DWORD* expectedSessionId, LUID* authenticationId,
	DWORD& error, std::wstring& failedStage)
{
	if (!token)
		return Fail(L"validate TrustedInstaller token handle",
			ERROR_INVALID_HANDLE, error, failedStage);
	return ValidateTrustedInstallerToken(
		token, expectedSessionId, authenticationId, error, failedStage);
}

bool RunTrustedInstallerTokenRuleSelfTest()
{
	TrustedInstallerTokenRuleInput validToken;
	validToken.userIsLocalSystem = true;
	validToken.serviceSidPresent = true;
	validToken.serviceSidAttributes = SE_GROUP_ENABLED |
		SE_GROUP_ENABLED_BY_DEFAULT;
	validToken.tokenType = TokenPrimary;
	validToken.integrityRid = SECURITY_MANDATORY_SYSTEM_RID;
	validToken.sessionId = 7;
	DWORD expectedSessionId = validToken.sessionId;
	DWORD error = NO_ERROR;
	std::wstring stage;
	if (!ValidateTrustedInstallerTokenRules(
		validToken, &expectedSessionId, error, stage))
		return false;

	const auto tokenRejected =
		[&](const TrustedInstallerTokenRuleInput& candidate) {
			DWORD candidateError = NO_ERROR;
			std::wstring candidateStage;
			return !ValidateTrustedInstallerTokenRules(candidate,
				&expectedSessionId, candidateError, candidateStage) &&
				candidateError != NO_ERROR && !candidateStage.empty();
		};
	TrustedInstallerTokenRuleInput invalidToken = validToken;
	invalidToken.userIsLocalSystem = false;
	if (!tokenRejected(invalidToken))
		return false;
	invalidToken = validToken;
	invalidToken.serviceSidPresent = false;
	if (!tokenRejected(invalidToken))
		return false;
	invalidToken = validToken;
	invalidToken.serviceSidAttributes = 0;
	if (!tokenRejected(invalidToken))
		return false;
	invalidToken = validToken;
	invalidToken.serviceSidAttributes =
		SE_GROUP_ENABLED | SE_GROUP_USE_FOR_DENY_ONLY;
	if (!tokenRejected(invalidToken))
		return false;
	invalidToken = validToken;
	invalidToken.tokenType = TokenImpersonation;
	if (!tokenRejected(invalidToken))
		return false;
	invalidToken = validToken;
	invalidToken.integrityRid = SECURITY_MANDATORY_HIGH_RID;
	if (!tokenRejected(invalidToken))
		return false;
	invalidToken = validToken;
	invalidToken.sessionId++;
	if (!tokenRejected(invalidToken))
		return false;

	TrustedInstallerServiceRuleInput validService;
	validService.serviceType = SERVICE_WIN32_OWN_PROCESS;
	validService.runsAsLocalSystem = true;
	validService.stateBefore = SERVICE_RUNNING;
	validService.processIdBefore = 1234;
	validService.stateAfter = SERVICE_RUNNING;
	validService.processIdAfter = validService.processIdBefore;
	validService.processHandleAlive = true;
	validService.openedProcessId = validService.processIdBefore;
	if (!ValidateTrustedInstallerServiceRules(validService, error, stage))
		return false;
	const auto serviceRejected =
		[&](const TrustedInstallerServiceRuleInput& candidate) {
			DWORD candidateError = NO_ERROR;
			std::wstring candidateStage;
			return !ValidateTrustedInstallerServiceRules(candidate,
				candidateError, candidateStage) &&
				candidateError != NO_ERROR && !candidateStage.empty();
		};
	TrustedInstallerServiceRuleInput invalidService = validService;
	invalidService.serviceType = SERVICE_WIN32_SHARE_PROCESS;
	if (!serviceRejected(invalidService))
		return false;
	invalidService = validService;
	invalidService.runsAsLocalSystem = false;
	if (!serviceRejected(invalidService))
		return false;
	invalidService = validService;
	invalidService.processHandleAlive = false;
	if (!serviceRejected(invalidService))
		return false;
	invalidService = validService;
	invalidService.openedProcessId++;
	if (!serviceRejected(invalidService))
		return false;
	invalidService = validService;
	invalidService.processIdAfter++;
	if (!serviceRejected(invalidService))
		return false;
	invalidService = validService;
	invalidService.stateAfter = SERVICE_STOPPED;
	return serviceRejected(invalidService);
}

bool RunTrustedInstallerTokenChildSelfTest(
	DWORD expectedSessionId, DWORD& error, std::wstring& failedStage)
{
	error = NO_ERROR;
	failedStage.clear();
	Handle token;
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, token.put()))
		return Fail(L"open child process token", GetLastError(),
			error, failedStage);
	return ValidateTrustedInstallerToken(
		token.get(), &expectedSessionId, nullptr, error, failedStage);
}

bool AcquireTrustedInstallerLaunchToken(DWORD sessionId, HANDLE& token,
	DWORD& error, std::wstring& failedStage)
{
	token = nullptr;
	error = NO_ERROR;
	failedStage.clear();

	Handle brokerToken;
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, brokerToken.put()))
		return Fail(L"open broker process token", GetLastError(),
			error, failedStage);
	std::vector<std::uint8_t> brokerUserBuffer;
	if (!ReadTokenInformation(
		brokerToken.get(), TokenUser, brokerUserBuffer, error))
		return Fail(L"read broker token user", error, error, failedStage);
	BYTE systemSid[SECURITY_MAX_SID_SIZE] = {};
	DWORD systemSidSize = sizeof(systemSid);
	if (!CreateWellKnownSid(
		WinLocalSystemSid, nullptr, systemSid, &systemSidSize))
		return Fail(L"create broker LocalSystem SID",
			GetLastError(), error, failedStage);
	const auto* brokerUser =
		reinterpret_cast<const TOKEN_USER*>(brokerUserBuffer.data());
	if (!IsValidSid(brokerUser->User.Sid) ||
		!EqualSid(brokerUser->User.Sid, systemSid))
		return Fail(L"require LocalSystem broker context",
			ERROR_ACCESS_DENIED, error, failedStage);

	if (!EnableProcessPrivilege(SE_DEBUG_NAME, error, failedStage))
		return false;

	ServiceHandle manager(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT));
	if (!manager)
		return Fail(L"open Service Control Manager",
			GetLastError(), error, failedStage);
	ServiceHandle service(OpenServiceW(manager.get(), TrustedInstallerServiceName,
		SERVICE_QUERY_CONFIG | SERVICE_QUERY_STATUS | SERVICE_START));
	if (!service)
		return Fail(L"open TrustedInstaller service",
			GetLastError(), error, failedStage);
	DWORD serviceType = 0;
	bool runsAsLocalSystem = false;
	if (!ReadServiceConfiguration(service.get(), serviceType,
		runsAsLocalSystem, error, failedStage))
		return false;

	SERVICE_STATUS_PROCESS serviceStatus = {};
	if (!EnsureServiceRunning(
		service.get(), serviceStatus, error, failedStage))
		return false;
	const DWORD serviceProcessId = serviceStatus.dwProcessId;
	Handle serviceProcess(OpenProcess(
		PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE,
		FALSE, serviceProcessId));
	if (!serviceProcess)
		return Fail(L"open TrustedInstaller service process",
			GetLastError(), error, failedStage);
	if (GetProcessId(serviceProcess.get()) != serviceProcessId ||
		WaitForSingleObject(serviceProcess.get(), 0) != WAIT_TIMEOUT)
		return Fail(L"validate TrustedInstaller service process",
			ERROR_PROCESS_ABORTED, error, failedStage);
	if (!ConfirmServiceProcess(
		service.get(), serviceProcessId, error, failedStage))
		return false;
	SERVICE_STATUS_PROCESS revalidatedStatus = {};
	if (!QueryServiceStatus(
		service.get(), revalidatedStatus, error, failedStage))
		return false;
	TrustedInstallerServiceRuleInput serviceRules;
	serviceRules.serviceType = serviceType;
	serviceRules.runsAsLocalSystem = runsAsLocalSystem;
	serviceRules.stateBefore = serviceStatus.dwCurrentState;
	serviceRules.processIdBefore = serviceStatus.dwProcessId;
	serviceRules.stateAfter = revalidatedStatus.dwCurrentState;
	serviceRules.processIdAfter = revalidatedStatus.dwProcessId;
	serviceRules.processHandleAlive =
		WaitForSingleObject(serviceProcess.get(), 0) == WAIT_TIMEOUT;
	serviceRules.openedProcessId = GetProcessId(serviceProcess.get());
	if (!ValidateTrustedInstallerServiceRules(
		serviceRules, error, failedStage))
		return false;

	Handle serviceToken;
	if (!OpenProcessToken(serviceProcess.get(),
		TOKEN_QUERY | TOKEN_DUPLICATE, serviceToken.put()))
		return Fail(L"open TrustedInstaller service token",
			GetLastError(), error, failedStage);
	LUID sourceAuthenticationId = {};
	if (!ValidateTrustedInstallerToken(serviceToken.get(), nullptr,
		&sourceAuthenticationId, error, failedStage))
		return false;
	if (!ConfirmServiceProcess(
		service.get(), serviceProcessId, error, failedStage))
		return false;

	Handle duplicatedToken;
	if (!DuplicateTokenEx(serviceToken.get(),
		TOKEN_ASSIGN_PRIMARY | TOKEN_DUPLICATE | TOKEN_QUERY |
		TOKEN_ADJUST_DEFAULT | TOKEN_ADJUST_SESSIONID,
		nullptr, SecurityImpersonation, TokenPrimary, duplicatedToken.put()))
		return Fail(L"duplicate TrustedInstaller service token",
			GetLastError(), error, failedStage);
	if (!ConfirmServiceProcess(
		service.get(), serviceProcessId, error, failedStage))
		return false;

	if (!EnableProcessPrivilege(SE_TCB_NAME, error, failedStage))
		return false;
	if (!SetTokenInformation(duplicatedToken.get(), TokenSessionId,
		&sessionId, sizeof(sessionId)))
		return Fail(L"set TrustedInstaller token session",
			GetLastError(), error, failedStage);

	LUID duplicatedAuthenticationId = {};
	if (!ValidateTrustedInstallerToken(duplicatedToken.get(), &sessionId,
		&duplicatedAuthenticationId, error, failedStage))
		return false;
	if (sourceAuthenticationId.LowPart != duplicatedAuthenticationId.LowPart ||
		sourceAuthenticationId.HighPart != duplicatedAuthenticationId.HighPart)
		return Fail(L"validate duplicated token authentication ID",
			ERROR_ACCESS_DENIED, error, failedStage);

	token = duplicatedToken.release();
	return true;
}

bool RunTrustedInstallerTokenSelfTest(DWORD& error, std::wstring& failedStage)
{
	error = NO_ERROR;
	failedStage.clear();
	if (!RunTrustedInstallerTokenRuleSelfTest())
		return Fail(L"TrustedInstaller deterministic rules",
			ERROR_INVALID_DATA, error, failedStage);
	DWORD targetSessionId = WTSGetActiveConsoleSessionId();
	if (targetSessionId == 0xFFFFFFFF)
		return Fail(L"resolve active console session",
			ERROR_NO_SUCH_LOGON_SESSION, error, failedStage);
	HANDLE acquiredToken = nullptr;
	if (!AcquireTrustedInstallerLaunchToken(
		targetSessionId, acquiredToken, error, failedStage))
		return false;
	Handle token(acquiredToken);
	if (!EnableProcessPrivilege(
		SE_ASSIGNPRIMARYTOKEN_NAME, error, failedStage) ||
		!EnableProcessPrivilege(
			SE_INCREASE_QUOTA_NAME, error, failedStage))
		return false;
	return LaunchChildVerifier(
			token.get(), targetSessionId, error, failedStage);
}
