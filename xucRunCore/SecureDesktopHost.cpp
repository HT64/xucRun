// Purpose: Hosts sensitive UI on a private desktop isolated from lower-integrity processes.
// Approach: Implements the behavior with focused helpers built on shared xucRun interfaces and Windows APIs.
#include "SecureDesktopHost.h"

#include "BrokerContracts.h"

#include <bcrypt.h>
#include <sddl.h>
#include <userenv.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#pragma comment(lib, "Userenv.lib")

namespace xucrun::secure_desktop
{
	namespace
	{
		enum class HostWaitOutcome
		{
			ChildExited,
			Cancelled,
			TimedOut,
			Failed
		};

		HostWaitOutcome WaitForHelperOrCancellation(HANDLE childProcess,
			HANDLE cancellationEvent, DWORD timeout)
		{
			const HANDLE waitHandles[] = { childProcess, cancellationEvent };
			const DWORD wait = WaitForMultipleObjects(
				cancellationEvent ? 2 : 1, waitHandles, FALSE, timeout);
			if (wait == WAIT_OBJECT_0)
				return HostWaitOutcome::ChildExited;
			if (cancellationEvent && wait == WAIT_OBJECT_0 + 1)
				return HostWaitOutcome::Cancelled;
			return wait == WAIT_TIMEOUT
				? HostWaitOutcome::TimedOut : HostWaitOutcome::Failed;
		}

		bool ReadTokenUser(HANDLE token, std::vector<std::uint8_t>& buffer)
		{
			DWORD size = 0;
			GetTokenInformation(token, TokenUser, nullptr, 0, &size);
			if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || size == 0)
				return false;
			buffer.resize(size);
			return GetTokenInformation(
				token, TokenUser, buffer.data(), size, &size) == TRUE;
		}

		bool IsHighIntegrity(HANDLE token)
		{
			DWORD size = 0;
			GetTokenInformation(token, TokenIntegrityLevel, nullptr, 0, &size);
			if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || size == 0)
				return false;
			std::vector<std::uint8_t> buffer(size);
			if (!GetTokenInformation(token, TokenIntegrityLevel,
				buffer.data(), size, &size))
			{
				return false;
			}
			PSID sid = reinterpret_cast<TOKEN_MANDATORY_LABEL*>(buffer.data())->Label.Sid;
			const UCHAR count = IsValidSid(sid) ? *GetSidSubAuthorityCount(sid) : 0;
			return count != 0 &&
				*GetSidSubAuthority(sid, count - 1) >= SECURITY_MANDATORY_HIGH_RID;
		}

		bool IsAuthorizedHost(HANDLE helperToken, bool requireLocalSystemHost)
		{
			HANDLE currentToken = nullptr;
			if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &currentToken))
				return false;
			std::vector<std::uint8_t> currentUser;
			std::vector<std::uint8_t> helperUser;
			const bool usersRead = ReadTokenUser(currentToken, currentUser) &&
				ReadTokenUser(helperToken, helperUser);
			const bool currentIsSystem = usersRead && IsWellKnownSid(
				reinterpret_cast<TOKEN_USER*>(currentUser.data())->User.Sid,
				WinLocalSystemSid) == TRUE;
			const bool sameUser = usersRead && EqualSid(
				reinterpret_cast<TOKEN_USER*>(currentUser.data())->User.Sid,
				reinterpret_cast<TOKEN_USER*>(helperUser.data())->User.Sid) == TRUE;
			const bool authorized = IsHighIntegrity(currentToken) &&
				IsHighIntegrity(helperToken) &&
				(requireLocalSystemHost ? currentIsSystem : (currentIsSystem || sameUser));
			CloseHandle(currentToken);
			return authorized;
		}

		bool ReadTokenSessionAndLogonSid(HANDLE token, DWORD& sessionId,
			std::wstring& logonSid)
		{
			TOKEN_TYPE tokenType = TokenImpersonation;
			DWORD size = sizeof(tokenType);
			if (!GetTokenInformation(token, TokenType, &tokenType, size, &size) ||
				tokenType != TokenPrimary)
			{
				return false;
			}
			size = sizeof(sessionId);
			if (!GetTokenInformation(token, TokenSessionId,
				&sessionId, size, &size))
			{
				return false;
			}
			GetTokenInformation(token, TokenGroups, nullptr, 0, &size);
			if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || size == 0)
				return false;
			std::vector<std::uint8_t> buffer(size);
			if (!GetTokenInformation(token, TokenGroups, buffer.data(), size, &size))
				return false;
			const auto* groups = reinterpret_cast<const TOKEN_GROUPS*>(buffer.data());
			for (DWORD index = 0; index < groups->GroupCount; index++)
			{
				if ((groups->Groups[index].Attributes & SE_GROUP_LOGON_ID) != SE_GROUP_LOGON_ID)
					continue;
				wchar_t* value = nullptr;
				if (!ConvertSidToStringSidW(groups->Groups[index].Sid, &value))
					return false;
				logonSid = value;
				LocalFree(value);
				return true;
			}
			SetLastError(ERROR_NO_SUCH_LOGON_SESSION);
			return false;
		}

		std::wstring CreateDesktopName()
		{
			std::array<std::uint8_t, 16> random = {};
			if (BCryptGenRandom(nullptr, random.data(), static_cast<ULONG>(random.size()),
				BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
			{
				return {};
			}
			constexpr wchar_t digits[] = L"0123456789ABCDEF";
			std::wstring name = L"xucRunSecure-";
			for (const std::uint8_t byte : random)
			{
				name.push_back(digits[byte >> 4]);
				name.push_back(digits[byte & 0x0F]);
			}
			return name;
		}

		bool BuildDesktopSecurity(const std::wstring& logonSid,
			PSECURITY_DESCRIPTOR& descriptor)
		{
			descriptor = nullptr;
			const std::wstring sddl = L"D:P(A;;GA;;;SY)(A;;GA;;;" + logonSid +
				L")S:(ML;;NRNWNX;;;HI)";
			return ConvertStringSecurityDescriptorToSecurityDescriptorW(
				sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr) == TRUE;
		}

		bool ConfigureKillOnCloseJob(HANDLE job)
		{
			JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {};
			limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
			return SetInformationJobObject(job, JobObjectExtendedLimitInformation,
				&limits, sizeof(limits)) == TRUE;
		}

		bool ReportChildProcessHandle(HANDLE report, HANDLE childProcess, DWORD& error)
		{
			DWORD serverProcessId = 0;
			if (!GetNamedPipeServerProcessId(report, &serverProcessId) ||
				serverProcessId == 0)
			{
				error = GetLastError();
				return false;
			}
			HANDLE serverProcess = OpenProcess(PROCESS_DUP_HANDLE, FALSE, serverProcessId);
			HANDLE remoteHandle = nullptr;
			if (!serverProcess || !DuplicateHandle(GetCurrentProcess(), childProcess,
				serverProcess, &remoteHandle,
				PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE | SYNCHRONIZE,
				FALSE, 0))
			{
				error = GetLastError();
				if (serverProcess) CloseHandle(serverProcess);
				return false;
			}
			const std::uint64_t value = static_cast<std::uint64_t>(
				reinterpret_cast<std::uintptr_t>(remoteHandle));
			DWORD written = 0;
			const bool reported = WriteFile(
				report, &value, sizeof(value), &written, nullptr) == TRUE &&
				written == sizeof(value);
			if (!reported)
			{
				error = GetLastError();
				if (error == NO_ERROR) error = ERROR_WRITE_FAULT;
				HANDLE localCleanup = nullptr;
				DuplicateHandle(serverProcess, remoteHandle, GetCurrentProcess(),
					&localCleanup, 0, FALSE,
					DUPLICATE_SAME_ACCESS | DUPLICATE_CLOSE_SOURCE);
				if (localCleanup) CloseHandle(localCleanup);
			}
			CloseHandle(serverProcess);
			return reported;
		}
	}

	bool RunSecurityDescriptorSelfTest()
	{
		constexpr wchar_t TestLogonSid[] = L"S-1-5-5-1-2";
		PSECURITY_DESCRIPTOR descriptor = nullptr;
		if (!BuildDesktopSecurity(TestLogonSid, descriptor))
			return false;
		PSID logonSid = nullptr;
		BYTE systemSid[SECURITY_MAX_SID_SIZE] = {};
		DWORD systemSidSize = sizeof(systemSid);
		PACL dacl = nullptr;
		PACL sacl = nullptr;
		BOOL present = FALSE;
		BOOL defaulted = FALSE;
		bool systemAllowed = false;
		bool logonAllowed = false;
		bool highMandatoryLabel = false;
		const bool parsed = ConvertStringSidToSidW(TestLogonSid, &logonSid) == TRUE &&
			CreateWellKnownSid(WinLocalSystemSid, nullptr,
				systemSid, &systemSidSize) == TRUE &&
			GetSecurityDescriptorDacl(
				descriptor, &present, &dacl, &defaulted) == TRUE && present && dacl;
		if (parsed)
		{
			for (DWORD index = 0; index < dacl->AceCount; index++)
			{
				void* rawAce = nullptr;
				if (!GetAce(dacl, index, &rawAce))
					continue;
				const auto* header = static_cast<const ACE_HEADER*>(rawAce);
				if (header->AceType != ACCESS_ALLOWED_ACE_TYPE)
					continue;
				const auto* ace = static_cast<const ACCESS_ALLOWED_ACE*>(rawAce);
				PSID sid = const_cast<DWORD*>(&ace->SidStart);
				if ((ace->Mask & GENERIC_ALL) == GENERIC_ALL)
				{
					systemAllowed = systemAllowed || EqualSid(sid, systemSid) == TRUE;
					logonAllowed = logonAllowed || EqualSid(sid, logonSid) == TRUE;
				}
			}
			present = FALSE;
			if (GetSecurityDescriptorSacl(
				descriptor, &present, &sacl, &defaulted) && present && sacl)
			{
				for (DWORD index = 0; index < sacl->AceCount; index++)
				{
					void* rawAce = nullptr;
					if (!GetAce(sacl, index, &rawAce))
						continue;
					const auto* header = static_cast<const ACE_HEADER*>(rawAce);
					if (header->AceType != SYSTEM_MANDATORY_LABEL_ACE_TYPE)
						continue;
					const auto* ace = static_cast<const SYSTEM_MANDATORY_LABEL_ACE*>(rawAce);
					PSID sid = const_cast<DWORD*>(&ace->SidStart);
					const UCHAR count = IsValidSid(sid)
						? *GetSidSubAuthorityCount(sid) : 0;
					const DWORD requiredPolicy = SYSTEM_MANDATORY_LABEL_NO_READ_UP |
						SYSTEM_MANDATORY_LABEL_NO_WRITE_UP |
						SYSTEM_MANDATORY_LABEL_NO_EXECUTE_UP;
					highMandatoryLabel = count != 0 &&
						*GetSidSubAuthority(sid, count - 1) == SECURITY_MANDATORY_HIGH_RID &&
						(ace->Mask & requiredPolicy) == requiredPolicy;
				}
			}
		}
		if (logonSid) LocalFree(logonSid);
		LocalFree(descriptor);
		return parsed && systemAllowed && logonAllowed && highMandatoryLabel;
	}

	bool RunCancellationWaitSelfTest()
	{
		HANDLE child = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		HANDLE cancellation = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		if (!child || !cancellation)
		{
			if (child) CloseHandle(child);
			if (cancellation) CloseHandle(cancellation);
			return false;
		}
		SetEvent(cancellation);
		const bool cancelled = WaitForHelperOrCancellation(
			child, cancellation, 0) == HostWaitOutcome::Cancelled;
		ResetEvent(cancellation);
		SetEvent(child);
		const bool childExited = WaitForHelperOrCancellation(
			child, cancellation, 0) == HostWaitOutcome::ChildExited;
		ResetEvent(child);
		const bool timedOut = WaitForHelperOrCancellation(
			child, cancellation, 0) == HostWaitOutcome::TimedOut;
		CloseHandle(cancellation);
		CloseHandle(child);
		return cancelled && childExited && timedOut;
	}

	int RunHost(HANDLE helperToken, const wchar_t* helperPath,
		const wchar_t* helperArgument, const wchar_t* workingDirectory,
		HANDLE childProcessHandleReport, HANDLE cancellationEvent,
		bool requireLocalSystemHost)
	{
		if (!helperToken || !helperPath || !*helperPath || !helperArgument ||
			!IsAuthorizedHost(helperToken, requireLocalSystemHost))
		{
			return ERROR_ACCESS_DENIED;
		}
		DWORD tokenSessionId = 0;
		DWORD processSessionId = 0;
		std::wstring logonSid;
		if (!ProcessIdToSessionId(GetCurrentProcessId(), &processSessionId) ||
			!ReadTokenSessionAndLogonSid(helperToken, tokenSessionId, logonSid) ||
			tokenSessionId != processSessionId)
		{
			return ERROR_ACCESS_DENIED;
		}
		PSECURITY_DESCRIPTOR mutexDescriptor = nullptr;
		if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
			L"D:P(A;;GA;;;SY)(A;;GA;;;BA)", SDDL_REVISION_1,
			&mutexDescriptor, nullptr))
		{
			return static_cast<int>(GetLastError());
		}
		SECURITY_ATTRIBUTES mutexSecurity = {
			sizeof(mutexSecurity), mutexDescriptor, FALSE
		};
		HANDLE desktopMutex = CreateMutexW(
			&mutexSecurity, FALSE, L"Local\\xucRunSecureDesktop");
		LocalFree(mutexDescriptor);
		if (!desktopMutex)
			return static_cast<int>(GetLastError());
		const DWORD mutexWait = WaitForSingleObject(desktopMutex, 0);
		if (mutexWait != WAIT_OBJECT_0 && mutexWait != WAIT_ABANDONED)
		{
			const DWORD error = mutexWait == WAIT_TIMEOUT ? ERROR_BUSY : GetLastError();
			CloseHandle(desktopMutex);
			return static_cast<int>(error);
		}

		PSECURITY_DESCRIPTOR descriptor = nullptr;
		if (!BuildDesktopSecurity(logonSid, descriptor))
		{
			const DWORD error = GetLastError();
			ReleaseMutex(desktopMutex);
			CloseHandle(desktopMutex);
			return static_cast<int>(error);
		}
		SECURITY_ATTRIBUTES security = { sizeof(security), descriptor, FALSE };
		HWINSTA originalWindowStation = GetProcessWindowStation();
		HWINSTA interactiveWindowStation = OpenWindowStationW(L"WinSta0", FALSE,
			WINSTA_CREATEDESKTOP | WINSTA_ENUMDESKTOPS | WINSTA_READATTRIBUTES |
			WINSTA_WRITEATTRIBUTES | READ_CONTROL);
		if (!interactiveWindowStation ||
			!SetProcessWindowStation(interactiveWindowStation))
		{
			const DWORD error = GetLastError();
			if (interactiveWindowStation)
				CloseWindowStation(interactiveWindowStation);
			LocalFree(descriptor);
			ReleaseMutex(desktopMutex);
			CloseHandle(desktopMutex);
			return static_cast<int>(error);
		}

		HDESK originalDesktop = OpenInputDesktop(0, FALSE,
			DESKTOP_SWITCHDESKTOP);
		const std::wstring desktopName = CreateDesktopName();
		HDESK secureDesktop = desktopName.empty() ? nullptr : CreateDesktopExW(
			desktopName.c_str(), nullptr, nullptr, 0, GENERIC_ALL, &security, 0, nullptr);
		LocalFree(descriptor);
		if (!originalDesktop || !secureDesktop)
		{
			const DWORD error = GetLastError();
			if (secureDesktop) CloseDesktop(secureDesktop);
			if (originalDesktop) CloseDesktop(originalDesktop);
			SetProcessWindowStation(originalWindowStation);
			CloseWindowStation(interactiveWindowStation);
			ReleaseMutex(desktopMutex);
			CloseHandle(desktopMutex);
			return static_cast<int>(error);
		}

		void* environment = nullptr;
		std::wstring command = L"\"" + std::wstring(helperPath) + L"\" " + helperArgument;
		std::vector<wchar_t> commandLine(command.begin(), command.end());
		commandLine.push_back(L'\0');
		std::wstring derivedWorkingDirectory;
		if (!workingDirectory || !*workingDirectory)
		{
			derivedWorkingDirectory = helperPath;
			const std::size_t separator = derivedWorkingDirectory.find_last_of(L"\\/");
			derivedWorkingDirectory = separator == std::wstring::npos
				? std::wstring() : derivedWorkingDirectory.substr(0, separator);
			workingDirectory = derivedWorkingDirectory.c_str();
		}
		const std::wstring desktopPath = L"WinSta0\\" + desktopName;
		STARTUPINFOW startup = {};
		startup.cb = sizeof(startup);
		startup.lpDesktop = const_cast<LPWSTR>(desktopPath.c_str());
		PROCESS_INFORMATION child = {};
		HANDLE job = CreateJobObjectW(nullptr, nullptr);
		const bool childStarted = CreateEnvironmentBlock(&environment,
			helperToken, FALSE) == TRUE && job && ConfigureKillOnCloseJob(job) &&
			CreateProcessAsUserW(helperToken, helperPath, commandLine.data(), nullptr, nullptr,
				FALSE, CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT, environment,
				*workingDirectory ? workingDirectory : nullptr,
				&startup, &child) == TRUE;
		DWORD error = childStarted ? NO_ERROR : GetLastError();
		if (environment)
			DestroyEnvironmentBlock(environment);
		bool running = childStarted;
		if (running && !AssignProcessToJobObject(job, child.hProcess))
		{
			error = GetLastError();
			TerminateProcess(child.hProcess, error);
			running = false;
		}
		if (running && childProcessHandleReport &&
			!ReportChildProcessHandle(childProcessHandleReport, child.hProcess, error))
		{
			TerminateProcess(child.hProcess, error);
			running = false;
		}
		if (running && !SwitchDesktop(secureDesktop))
		{
			error = GetLastError();
			TerminateProcess(child.hProcess, error);
			running = false;
		}
		if (running && ResumeThread(child.hThread) == static_cast<DWORD>(-1))
		{
			error = GetLastError();
			TerminateProcess(child.hProcess, error);
			running = false;
		}
		if (childProcessHandleReport) CloseHandle(childProcessHandleReport);

		DWORD exitCode = error == NO_ERROR ? ERROR_GEN_FAILURE : error;
		if (running)
		{
			const HostWaitOutcome wait = WaitForHelperOrCancellation(
				child.hProcess, cancellationEvent,
				xucrun::broker::ApprovalHelperHostTimeoutMilliseconds);
			if (wait == HostWaitOutcome::ChildExited)
			{
				if (!GetExitCodeProcess(child.hProcess, &exitCode))
					exitCode = GetLastError();
			}
			else
			{
				exitCode = wait == HostWaitOutcome::TimedOut ||
					wait == HostWaitOutcome::Cancelled
					? ERROR_TIMEOUT : GetLastError();
				TerminateProcess(child.hProcess, exitCode);
				WaitForSingleObject(child.hProcess,
					xucrun::broker::BrokerIpcTimeoutReserveMilliseconds);
			}
		}
		if (!SwitchDesktop(originalDesktop) && exitCode == NO_ERROR)
		{
			exitCode = GetLastError();
			if (exitCode == NO_ERROR) exitCode = ERROR_GEN_FAILURE;
		}
		if (child.hThread) CloseHandle(child.hThread);
		if (child.hProcess) CloseHandle(child.hProcess);
		if (job) CloseHandle(job);
		CloseDesktop(secureDesktop);
		CloseDesktop(originalDesktop);
		SetProcessWindowStation(originalWindowStation);
		CloseWindowStation(interactiveWindowStation);
		ReleaseMutex(desktopMutex);
		CloseHandle(desktopMutex);
		return static_cast<int>(exitCode);
	}
}
