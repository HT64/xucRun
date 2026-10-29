// Purpose: Authenticates pipe clients, enforces broker request policy, and dispatches privileged operations.
#include "BrokerBackupStore.h"
#include "BrokerTargetNaming.h"
// Processes authenticated broker requests over a hardened named pipe.
// Authorizes clients and controls credential, approval, launch, and process operations.
#include "BrokerPipeServer.h"
#include "BrokerRequestPolicy.h"

#include "../xucRunSharedResources/BuildVersion.h"

#include "AccountIdentity.h"
#include "ApprovalStore.h"
#include "AuthenticityMarkerStore.h"
#include "BrokerPipeClient.h"
#include "BrokerSerialization.h"
#include "ClientImageIdentity.h"
#include "CredentialMetadataStore.h"
#include "CredentialStore.h"
#include "CredentialStoreSelfTest.h"
#include "CredentialValidator.h"
#include "FileIdentity.h"
#include "SessionTestLauncher.h"
#include "SensitiveClientAuthorization.h"
#include "SecurityAclAudit.h"
#include "TargetNaming.h"
#include "TargetImageIdentity.h"
#include "../xucRunCore/ClientProcessContext.h"

#include <aclapi.h>
#include <sddl.h>
#include <bcrypt.h>
#include <shellapi.h>
#include <tlhelp32.h>

#include <algorithm>
#include <deque>
#include <filesystem>
#include <limits>
#include <memory>
#include <vector>

#pragma comment(lib, "Shell32.lib")

namespace
{
	constexpr wchar_t PipeSecurity[] =
		L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;0x00100183;;;AU)S:(ML;;NW;;;ME)";
	constexpr DWORD ClientIoTimeoutMilliseconds = 5000;
	constexpr std::size_t MaximumPendingApprovalChallenges = 64;
	constexpr std::size_t MaximumPendingApprovalChallengesPerSid = 8;
	constexpr std::size_t MaximumPendingApprovalChallengesPerSession = 16;
	constexpr std::size_t MaximumPendingApprovalChallengesPerProcess = 4;
#ifdef _DEBUG
	constexpr bool DiagnosticPipeContractsEnabled = true;
#else
	constexpr bool DiagnosticPipeContractsEnabled = false;
#endif

	bool ComputeUtf16Sha256(const std::wstring& text,
		std::array<std::uint8_t, 32>& digest)
	{
		digest = {};
		BCRYPT_ALG_HANDLE algorithm = nullptr;
		BCRYPT_HASH_HANDLE hash = nullptr;
		NTSTATUS status = BCryptOpenAlgorithmProvider(
			&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
		if (status == 0)
			status = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0);
		if (status == 0 && !text.empty())
		{
			status = BCryptHashData(hash,
				reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(text.data())),
				static_cast<ULONG>(text.size() * sizeof(wchar_t)), 0);
		}
		if (status == 0)
		{
			status = BCryptFinishHash(
				hash, digest.data(), static_cast<ULONG>(digest.size()), 0);
		}
		if (hash)
			BCryptDestroyHash(hash);
		if (algorithm)
			BCryptCloseAlgorithmProvider(algorithm, 0);
		return status == 0;
	}

	std::wstring LowerFileName(const std::wstring& path)
	{
		std::wstring name = std::filesystem::path(path).filename().native();
		std::transform(name.begin(), name.end(), name.begin(), ::towlower);
		return name;
	}

	bool HasExtension(const std::wstring& path,
		std::initializer_list<const wchar_t*> extensions)
	{
		std::wstring extension = std::filesystem::path(path).extension().native();
		std::transform(extension.begin(), extension.end(), extension.begin(), ::towlower);
		return std::any_of(extensions.begin(), extensions.end(),
			[&extension](const wchar_t* candidate) {
				return extension == candidate;
			});
	}

	bool TryGetCommandScriptArgument(const std::wstring& argumentTail,
		std::initializer_list<const wchar_t*> extensions,
		std::wstring& scriptArgument)
	{
		for (std::size_t position = 0; position + 1 < argumentTail.size(); position++)
		{
			if (argumentTail[position] != L'/' ||
				(::towlower(argumentTail[position + 1]) != L'c' &&
				 ::towlower(argumentTail[position + 1]) != L'k') ||
				(position != 0 && !::iswspace(argumentTail[position - 1])) ||
				(position + 2 != argumentTail.size() &&
				 !::iswspace(argumentTail[position + 2])))
			{
				continue;
			}

			std::size_t pathStart =
				argumentTail.find_first_not_of(L" \t", position + 2);
			if (pathStart == std::wstring::npos)
				return false;
			if (argumentTail[pathStart] == L'"')
			{
				pathStart++;
				if (pathStart < argumentTail.size() &&
					argumentTail[pathStart] == L'"')
				{
					pathStart++;
				}
				const std::size_t pathEnd = argumentTail.find(L'"', pathStart);
				if (pathEnd == std::wstring::npos)
					return false;
				scriptArgument =
					argumentTail.substr(pathStart, pathEnd - pathStart);
			}
			else
			{
				const std::size_t pathEnd =
					argumentTail.find_first_of(L" \t", pathStart);
				scriptArgument = argumentTail.substr(pathStart,
					pathEnd == std::wstring::npos
						? std::wstring::npos : pathEnd - pathStart);
			}
			if (HasExtension(scriptArgument, extensions))
				return true;
			scriptArgument.clear();
			return false;
		}
		return false;
	}

	bool TryGetDirectScriptArgument(const std::wstring& executablePath,
		const std::wstring& argumentTail, std::wstring& scriptArgument)
	{
		scriptArgument.clear();
		if (argumentTail.empty())
			return false;
		const std::wstring commandLine = L"xucRunScriptBinding " + argumentTail;
		int argumentCount = 0;
		LPWSTR* arguments = CommandLineToArgvW(commandLine.c_str(), &argumentCount);
		if (!arguments || argumentCount < 2)
		{
			if (arguments)
				LocalFree(arguments);
			return false;
		}

		std::vector<std::wstring> values;
		values.reserve(static_cast<std::size_t>(argumentCount - 1));
		for (int index = 1; index < argumentCount; index++)
			values.emplace_back(arguments[index]);
		LocalFree(arguments);

		const std::wstring executable = LowerFileName(executablePath);
		if (executable == L"powershell.exe" || executable == L"pwsh.exe")
		{
			for (std::size_t index = 0; index + 1 < values.size(); index++)
			{
				const std::wstring& option = values[index];
				if (option.size() >= 2 && option.size() <= 5 &&
					_wcsnicmp(option.c_str(), L"-File", option.size()) == 0 &&
					HasExtension(values[index + 1], { L".ps1" }))
				{
					scriptArgument = values[index + 1];
					return true;
				}
			}
			return false;
		}

		if (executable == L"cmd.exe" || executable == L"tcc.exe" ||
			executable == L"tcmd.exe")
		{
			return TryGetCommandScriptArgument(argumentTail,
				executable == L"cmd.exe"
					? std::initializer_list<const wchar_t*>{ L".bat", L".cmd" }
					: std::initializer_list<const wchar_t*>{
						L".bat", L".cmd", L".btm" },
				scriptArgument);
		}
		if (executable == L"wscript.exe" || executable == L"cscript.exe")
		{
			for (const std::wstring& value : values)
			{
				if (value.starts_with(L"//"))
					continue;
				if (HasExtension(value,
					{ L".vbs", L".vbe", L".js", L".jse", L".wsf", L".wsh" }))
				{
					scriptArgument = value;
					return true;
				}
				return false;
			}
		}
		return false;
	}

	bool IsPowerShellInterpreter(const std::wstring& executablePath)
	{
		const std::wstring executable = LowerFileName(executablePath);
		return executable == L"powershell.exe" || executable == L"pwsh.exe";
	}

	bool RunDirectScriptArgumentSelfTest()
	{
		std::wstring script;
		return TryGetDirectScriptArgument(L"C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe",
				L"-NoProfile -File \"C:\\Scripts\\task.ps1\" -Name Test", script) &&
			script == L"C:\\Scripts\\task.ps1" &&
			TryGetDirectScriptArgument(L"C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe",
				L"-NoProfile -f \"C:\\Scripts\\short.ps1\"", script) &&
			script == L"C:\\Scripts\\short.ps1" &&
			TryGetDirectScriptArgument(L"C:\\Windows\\System32\\cmd.exe",
				L"/D /S /C \"C:\\Scripts\\task.cmd\" value", script) &&
			script == L"C:\\Scripts\\task.cmd" &&
			TryGetDirectScriptArgument(L"C:\\Windows\\System32\\cmd.exe",
				L"/D /S /C \"\"C:\\Scripts\\task.cmd\" value\"", script) &&
			script == L"C:\\Scripts\\task.cmd" &&
			TryGetDirectScriptArgument(L"C:\\Tools\\tcc.exe",
				L"/C \"C:\\Scripts\\task.btm\" value", script) &&
			script == L"C:\\Scripts\\task.btm" &&
			TryGetDirectScriptArgument(L"C:\\Windows\\System32\\cscript.exe",
				L"//NoLogo \"C:\\Scripts\\task.vbs\" value", script) &&
			script == L"C:\\Scripts\\task.vbs" &&
			!TryGetDirectScriptArgument(L"C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe",
				L"-Command \"Get-Process\"", script) &&
			!TryGetDirectScriptArgument(L"C:\\Windows\\System32\\cmd.exe",
				L"/C echo test", script);
	}

	struct ReplayEntry
	{
		std::uint64_t requestId;
		xucrun::broker::RequestNonce nonce;
	};

	struct PendingApprovalChallenge
	{
		xucrun::broker::RequestNonce challengeId;
		ULONGLONG expiresAt = 0;
		ULONGLONG helperAbsoluteExpiresAt = 0;
		std::wstring clientSid;
		std::wstring clientLogonSid;
		DWORD clientProcessId = 0;
		DWORD clientSessionId = 0;
		std::wstring accountSid;
		std::wstring approvalAccountSid;
		std::wstring clientPath;
		std::vector<std::uint8_t> clientSha256;
		LONG clientTrustStatus = TRUST_E_NOSIGNATURE;
		bool clientSignatureTrusted = false;
		std::wstring executablePath;
		std::array<std::uint8_t, 32> executableSha256 = {};
		LONG executableTrustStatus = TRUST_E_NOSIGNATURE;
		bool executableSignatureTrusted = false;
		std::wstring argumentTail;
		std::wstring workingDirectory;
		std::array<std::uint8_t, 32> argumentHash = {};
		std::wstring directScriptPath;
		std::array<std::uint8_t, 32> directScriptSha256 = {};
		std::wstring scriptPath;
		std::array<std::uint8_t, 32> scriptSha256 = {};
		xucrun::broker::ElevationMode elevationMode =
			xucrun::broker::ElevationMode::PreserveToken;
		xucrun::broker::LaunchVisibility visibility =
			xucrun::broker::LaunchVisibility::Normal;
		xucrun::broker::ApprovalFailureReason writableElevatedTargetReason =
			xucrun::broker::ApprovalFailureReason::None;
		bool diagnosticProfile = false;
		bool trustedAdministrativeClientLaunch = false;
		bool userConfirmationRequired = true;
		bool storedApprovalPresenceConfirmation = false;
		bool clientAccountPresenceConfirmation = false;
		xucrun::broker::ApprovalHelperStartMode helperStartMode =
			xucrun::broker::ApprovalHelperStartMode::None;
		std::shared_ptr<void> secureHostProcess;
		std::shared_ptr<void> secureHostCancellationSignal;
		DWORD secureHostProcessId = 0;
		DWORD helperProcessId = 0;
		bool helperDataRetrieved = false;
		bool helperDecisionRecorded = false;
		xucrun::broker::ApprovalHelperDecisionRequest helperDecision;
		xucrun::approval_store::ProgramUpdateInfo programUpdate;
	};

	struct PendingCredentialCaptureChallenge
	{
		xucrun::broker::RequestNonce challengeId;
		ULONGLONG expiresAt = 0;
		std::wstring clientSid;
		std::wstring clientLogonSid;
		DWORD clientProcessId = 0;
		DWORD clientSessionId = 0;
		std::wstring clientPath;
		std::vector<std::uint8_t> clientSha256;
		std::wstring accountSid;
		xucrun::broker::ApprovalHelperStartMode helperStartMode =
			xucrun::broker::ApprovalHelperStartMode::None;
		std::shared_ptr<void> secureHostProcess;
		std::shared_ptr<void> secureHostCancellationSignal;
		DWORD secureHostProcessId = 0;
		DWORD helperProcessId = 0;
		bool helperDataRetrieved = false;
		bool credentialStaged = false;
		xucrun::SensitiveString stagedPassword;
	};

	struct LaunchObjectLocks
	{
		HANDLE executable = INVALID_HANDLE_VALUE;
		HANDLE workingDirectory = INVALID_HANDLE_VALUE;
		HANDLE script = INVALID_HANDLE_VALUE;

		~LaunchObjectLocks()
		{
			if (script != INVALID_HANDLE_VALUE)
				CloseHandle(script);
			if (workingDirectory != INVALID_HANDLE_VALUE)
				CloseHandle(workingDirectory);
			if (executable != INVALID_HANDLE_VALUE)
				CloseHandle(executable);
		}
	};

	// Shows the observed path but exposes a hash only when reusable binding is safe.
	void PopulateApprovalHelperDirectScriptBinding(
		const PendingApprovalChallenge& pending,
		xucrun::broker::ApprovalChallengeResult& result)
	{
		result.directScriptPath = pending.directScriptPath;
		result.directScriptSha256 = pending.scriptSha256;
	}

	bool IsUniversalInterpreter(const std::wstring& executablePath)
	{
		const std::size_t separator = executablePath.find_last_of(L"\\/");
		const std::wstring fileName = separator == std::wstring::npos
			? executablePath : executablePath.substr(separator + 1);
		for (const wchar_t* candidate : {
			L"cmd.exe", L"powershell.exe", L"pwsh.exe", L"tcc.exe", L"tcmd.exe",
			L"wscript.exe", L"cscript.exe" })
		{
			if (CompareStringOrdinal(
				fileName.c_str(), -1, candidate, -1, TRUE) == CSTR_EQUAL)
			{
				return true;
			}
		}
		return false;
	}

	bool RequiresBroadInterpreterApprovalConfirmation(
		const PendingApprovalChallenge& pending,
		const xucrun::broker::ApprovalHelperDecisionRequest& request)
	{
		return request.decision == xucrun::broker::ApprovalDecision::ApproveOnce &&
			request.persistence != xucrun::broker::ApprovalPersistence::Once &&
			request.programScope !=
				xucrun::broker::ApprovalProgramScope::ExactInvocation &&
			IsUniversalInterpreter(pending.executablePath);
	}

	std::shared_ptr<void> OwnHandle(HANDLE handle)
	{
		return std::shared_ptr<void>(handle, [](void* value) {
			if (value) CloseHandle(static_cast<HANDLE>(value));
		});
	}

	void CancelSecureHost(const std::shared_ptr<void>& cancellationSignal)
	{
		if (cancellationSignal)
			SetEvent(static_cast<HANDLE>(cancellationSignal.get()));
	}

	bool HasHighIntegrity(HANDLE process, DWORD& error);

	bool IsRunningSecureHostChild(const std::shared_ptr<void>& hostProcess,
		DWORD hostProcessId, DWORD childProcessId)
	{
		HANDLE process = static_cast<HANDLE>(hostProcess.get());
		return process && hostProcessId != 0 && GetProcessId(process) == hostProcessId &&
			WaitForSingleObject(process, 0) == WAIT_TIMEOUT &&
			xucrun::process_context::GetParentProcessId(childProcessId) == hostProcessId;
	}

	bool BindClientStartedSecureHost(DWORD helperProcessId, DWORD expectedSessionId,
		PendingCredentialCaptureChallenge& pending, DWORD& error)
	{
		const DWORD hostProcessId =
			xucrun::process_context::GetParentProcessId(helperProcessId);
		HANDLE hostProcess = hostProcessId == 0 ? nullptr : OpenProcess(
			SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
			FALSE, hostProcessId);
		DWORD hostSessionId = 0;
		ClientImageIdentity hostIdentity;
		if (!hostProcess ||
			!ProcessIdToSessionId(hostProcessId, &hostSessionId) ||
			hostSessionId != expectedSessionId ||
			!HasHighIntegrity(hostProcess, error) ||
			!ReadClientImageIdentity(hostProcess, hostIdentity, error) ||
			!IsApprovalHelperAuthorized(
				hostIdentity, SECURITY_MANDATORY_HIGH_RID, error))
		{
			if (hostProcess)
				CloseHandle(hostProcess);
			if (error == NO_ERROR)
				error = ERROR_ACCESS_DENIED;
			return false;
		}
		pending.secureHostProcess = OwnHandle(hostProcess);
		pending.secureHostProcessId = hostProcessId;
		error = NO_ERROR;
		return true;
	}

	bool IsCredentialCommitHost(const PendingCredentialCaptureChallenge& pending,
		DWORD processId, DWORD sessionId)
	{
		HANDLE expectedHost = static_cast<HANDLE>(pending.secureHostProcess.get());
		// credentialStaged distinguishes a validated empty password from the absence
		// of a submitted credential; string emptiness cannot represent that state.
		return pending.credentialStaged &&
			pending.clientSessionId == sessionId && expectedHost &&
			pending.secureHostProcessId == processId &&
			GetProcessId(expectedHost) == processId &&
			WaitForSingleObject(expectedHost, 0) == WAIT_TIMEOUT;
	}

	bool IsBoundCredentialCommitHost(
		const std::deque<PendingCredentialCaptureChallenge>& challenges,
		DWORD processId, DWORD sessionId)
	{
		return std::any_of(challenges.begin(), challenges.end(),
			[processId, sessionId](const PendingCredentialCaptureChallenge& pending) {
				return IsCredentialCommitHost(pending, processId, sessionId);
			});
	}

	struct TrackedElevatedProcess
	{
		HANDLE process = nullptr;
		DWORD processId = 0;
		DWORD sessionId = 0;
		xucrun::broker::RequestNonce trackingId = {};
		std::wstring processBaseName;
	};

	struct TrackedProcessListCandidate
	{
		DWORD sessionId = 0;
		ULONGLONG creationTime = 0;
		xucrun::broker::TrackedProcessListEntry entry;
	};

	struct RegisteredStatusIcon
	{
		HANDLE process = nullptr;
		DWORD processId = 0;
		DWORD sessionId = 0;
		xucrun::broker::ProductVersion clientVersion;
		std::wstring accountSid;
		std::wstring canonicalAccountName;
		bool elevated = false;
	};

	struct TrackedElevatedProcessJob
	{
		HANDLE job = nullptr;
		DWORD sessionId = 0;
		std::wstring logonSid;
		ULONGLONG rootCreationTime = 0;
		bool discoverLogonSessionProcesses = false;
	};

	// Debug diagnostics intentionally trust an elevated broker or xucRun Debug image by name.
	bool IsDebugDiagnosticClientAuthorized(
		const ClientImageIdentity& identity, DWORD integrityRid)
	{
#ifdef _DEBUG
		if (integrityRid < SECURITY_MANDATORY_HIGH_RID || identity.path.empty())
			return false;
		static const std::wstring brokerPath = [] {
			std::vector<wchar_t> modulePath(32768);
			const DWORD length = GetModuleFileNameW(nullptr, modulePath.data(),
				static_cast<DWORD>(modulePath.size()));
			if (length == 0 || length >= modulePath.size())
				return std::wstring{};
			HANDLE image = CreateFileW(modulePath.data(), FILE_READ_ATTRIBUTES,
				FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
				nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
			if (image == INVALID_HANDLE_VALUE)
				return std::wstring{};
			std::wstring finalPath;
			xucrun::files::ResolveFinalFilePathFromHandle(image, finalPath);
			CloseHandle(image);
			return finalPath;
		}();
		const std::size_t separator = identity.path.find_last_of(L"\\/");
		const std::wstring fileName = separator == std::wstring::npos
			? identity.path : identity.path.substr(separator + 1);
		return (!brokerPath.empty() &&
			_wcsicmp(identity.path.c_str(), brokerPath.c_str()) == 0) ||
			_wcsicmp(fileName.c_str(), L"xucRun.exe") == 0;
#else
		UNREFERENCED_PARAMETER(identity);
		UNREFERENCED_PARAMETER(integrityRid);
		return false;
#endif
	}

	// Overwrites sensitive byte buffers before releasing them.
	void EraseSensitiveBytes(std::vector<std::uint8_t>& value) noexcept
	{
		if (!value.empty())
			SecureZeroMemory(value.data(), value.size());
		value.clear();
	}

	// Transfers stored password bytes once into the protected string and clears the source buffer.
	bool AssignStoredPassword(std::vector<std::uint8_t>& storedPassword,
		xucrun::SensitiveString& password)
	{
		const bool valid = (storedPassword.size() % sizeof(wchar_t)) == 0 &&
			storedPassword.size() / sizeof(wchar_t) <=
				xucrun::broker::MaximumPasswordLength &&
			password.assignUtf16(storedPassword,
				storedPassword.size() / sizeof(wchar_t));
		EraseSensitiveBytes(storedPassword);
		return valid;
	}

	std::wstring ChallengeToHex(const xucrun::broker::RequestNonce& challenge)
	{
		constexpr wchar_t digits[] = L"0123456789ABCDEF";
		std::wstring value;
		value.reserve(challenge.size() * 2);
		for (const std::uint8_t byte : challenge)
		{
			value.push_back(digits[byte >> 4]);
			value.push_back(digits[byte & 0x0F]);
		}
		return value;
	}

	// Grants SYSTEM signal rights and the host account only the right to wait.
	bool CreateSecureHostCancellationSignal(
		const xucrun::broker::RequestNonce& challenge,
		const std::wstring& waitingAccountSid,
		std::wstring& name,
		std::shared_ptr<void>& signal,
		DWORD& error)
	{
		name = L"Global\\xucRunSecureDesktopCancel-" + ChallengeToHex(challenge);
		const std::wstring sddl = L"D:P(A;;GA;;;SY)(A;;0x00100000;;;" +
			waitingAccountSid + L")";
		PSECURITY_DESCRIPTOR descriptor = nullptr;
		if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
			sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr))
		{
			error = GetLastError();
			return false;
		}
		SECURITY_ATTRIBUTES security = { sizeof(security), descriptor, FALSE };
		HANDLE event = CreateEventW(&security, TRUE, FALSE, name.c_str());
		error = event ? NO_ERROR : GetLastError();
		const bool created = event && GetLastError() != ERROR_ALREADY_EXISTS;
		LocalFree(descriptor);
		if (!created)
		{
			if (event) CloseHandle(event);
			if (error == NO_ERROR) error = ERROR_ALREADY_EXISTS;
			return false;
		}
		signal = OwnHandle(event);
		return true;
	}

	std::wstring SanitizeLogValue(const std::wstring& value)
	{
		std::wstring sanitized = value;
		for (wchar_t& character : sanitized)
		{
			if (character < L' ' || character == 0x7F)
				character = L'?';
		}
		return sanitized;
	}

	bool HasHighIntegrity(HANDLE process, DWORD& error)
	{
		HANDLE token = nullptr;
		if (!OpenProcessToken(process, TOKEN_QUERY, &token))
		{
			error = GetLastError();
			return false;
		}
		DWORD size = 0;
		GetTokenInformation(token, TokenIntegrityLevel, nullptr, 0, &size);
		std::vector<std::uint8_t> buffer(size);
		const bool read = size != 0 &&
			GetTokenInformation(token, TokenIntegrityLevel,
				buffer.data(), size, &size) == TRUE;
		if (!read)
		{
			error = GetLastError();
			CloseHandle(token);
			return false;
		}
		const auto* label = reinterpret_cast<const TOKEN_MANDATORY_LABEL*>(
			buffer.data());
		PSID sid = label->Label.Sid;
		const UCHAR count = IsValidSid(sid) ? *GetSidSubAuthorityCount(sid) : 0;
		const bool highIntegrity = count != 0 &&
			*GetSidSubAuthority(sid, count - 1) >= SECURITY_MANDATORY_HIGH_RID;
		CloseHandle(token);
		if (!highIntegrity)
			error = count == 0 ? ERROR_INVALID_SID : ERROR_ELEVATION_REQUIRED;
		return highIntegrity;
	}

	bool HasEnabledAdministratorsGroup(HANDLE process, DWORD& error)
	{
		HANDLE token = nullptr;
		if (!OpenProcessToken(process, TOKEN_QUERY, &token))
		{
			error = GetLastError();
			return false;
		}
		DWORD size = 0;
		GetTokenInformation(token, TokenGroups, nullptr, 0, &size);
		std::vector<std::uint8_t> buffer(size);
		const bool groupsRead = size != 0 && GetTokenInformation(
			token, TokenGroups, buffer.data(), size, &size) == TRUE;
		CloseHandle(token);
		if (!groupsRead)
		{
			error = GetLastError();
			return false;
		}
		BYTE administratorsSid[SECURITY_MAX_SID_SIZE] = {};
		DWORD administratorsSidSize = sizeof(administratorsSid);
		if (!CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr,
			administratorsSid, &administratorsSidSize))
		{
			error = GetLastError();
			return false;
		}
		const auto* groups = reinterpret_cast<const TOKEN_GROUPS*>(buffer.data());
		for (DWORD index = 0; index < groups->GroupCount; index++)
		{
			if (EqualSid(groups->Groups[index].Sid, administratorsSid) &&
				(groups->Groups[index].Attributes & SE_GROUP_ENABLED) != 0 &&
				(groups->Groups[index].Attributes & SE_GROUP_USE_FOR_DENY_ONLY) == 0)
			{
				error = NO_ERROR;
				return true;
			}
		}
		error = ERROR_ACCESS_DENIED;
		return false;
	}

	constexpr bool ShouldUseClientTokenForApprovalHelper(
		xucrun::broker::ElevationMode elevationMode,
		bool targetMembershipResolved,
		bool targetIsAdministrator,
		bool clientIsHighIntegrity) noexcept
	{
		return elevationMode == xucrun::broker::ElevationMode::PreserveToken &&
			targetMembershipResolved && !targetIsAdministrator && clientIsHighIntegrity;
	}

	// Selects the initial mode before an optional, validated administrator confirmation
	// account can switch a standard-to-standard approval helper to broker-started.
	constexpr xucrun::broker::ApprovalHelperStartMode SelectApprovalHelperStartMode(
		xucrun::broker::ElevationMode elevationMode,
		bool targetMembershipResolved,
		bool targetIsAdministrator,
		bool clientIsHighIntegrity) noexcept
	{
		return elevationMode == xucrun::broker::ElevationMode::PreserveToken &&
			targetMembershipResolved && !targetIsAdministrator && !clientIsHighIntegrity
			? xucrun::broker::ApprovalHelperStartMode::ClientRunAsRequired
			: xucrun::broker::ApprovalHelperStartMode::BrokerStarted;
	}

	constexpr bool RequiresStoredApprovalPresenceConfirmation(
		bool storedApprovalMatches,
		bool clientIntegrityResolved,
		bool clientIsHighIntegrity,
		bool targetMembershipResolved,
		bool targetIsAdministrator,
		xucrun::broker::ElevationMode elevationMode) noexcept
	{
		return storedApprovalMatches && clientIntegrityResolved &&
			!clientIsHighIntegrity && targetMembershipResolved &&
			targetIsAdministrator &&
			elevationMode == xucrun::broker::ElevationMode::PreserveToken;
	}

	constexpr bool RequiresStandardTargetPresenceConfirmation(
		bool storedApprovalMatches,
		bool clientMembershipResolved,
		bool clientIsAdministrator,
		bool clientIntegrityResolved,
		bool clientIsHighIntegrity,
		bool targetMembershipResolved,
		bool targetIsAdministrator,
		bool approvalAccountAvailable,
		xucrun::broker::ElevationMode elevationMode) noexcept
	{
		return storedApprovalMatches && clientMembershipResolved &&
			(clientIsAdministrator || approvalAccountAvailable) &&
			clientIntegrityResolved && !clientIsHighIntegrity &&
			targetMembershipResolved && !targetIsAdministrator &&
			elevationMode == xucrun::broker::ElevationMode::PreserveToken;
	}

	constexpr bool RequiresClientAccountPresenceConfirmation(
		bool storedApprovalPresenceConfirmation,
		bool clientMembershipResolved,
		bool clientIsAdministrator,
		bool clientIntegrityResolved,
		bool clientIsHighIntegrity) noexcept
	{
		return storedApprovalPresenceConfirmation && clientMembershipResolved &&
			clientIsAdministrator && clientIntegrityResolved && !clientIsHighIntegrity;
	}

	constexpr bool ShouldUseSeparateApprovalAccountForHelper(
		bool clientMembershipResolved,
		bool clientIsAdministrator,
		bool clientIntegrityResolved,
		bool clientIsHighIntegrity,
		bool targetMembershipResolved,
		bool targetIsAdministrator,
		bool approvalAccountAvailable,
		xucrun::broker::ElevationMode elevationMode) noexcept
	{
		return clientMembershipResolved && !clientIsAdministrator &&
			clientIntegrityResolved && !clientIsHighIntegrity &&
			targetMembershipResolved && !targetIsAdministrator &&
			approvalAccountAvailable &&
			elevationMode == xucrun::broker::ElevationMode::PreserveToken;
	}

	constexpr bool IsSingleUseApprovalDecision(
		const xucrun::broker::ApprovalHelperDecisionRequest& request) noexcept
	{
		return (request.decision == xucrun::broker::ApprovalDecision::ApproveOnce ||
			request.decision == xucrun::broker::ApprovalDecision::Deny) &&
			request.persistence == xucrun::broker::ApprovalPersistence::Once &&
			request.programScope ==
				xucrun::broker::ApprovalProgramScope::TargetProgram &&
			request.accountScope ==
				xucrun::broker::ApprovalAccountScope::CurrentAccount &&
			request.updateScope == xucrun::broker::ApprovalUpdateScope::None;
	}

	// Rejects LocalSystem and accepts only a resolved administrator for the client-started mode.
	bool IsClientStartedApprovalHelperAccountAuthorized(
		const std::wstring& helperSid, DWORD& error)
	{
		if (_wcsicmp(helperSid.c_str(), L"S-1-5-18") == 0)
		{
			error = ERROR_ACCESS_DENIED;
			return false;
		}
		AccountIdentity helperAccount;
		bool isAdministrator = false;
		if (!ResolveUserAccountIdentity(helperSid, helperAccount, error) ||
			!IsAccountMemberOfAdministrators(helperAccount, isAdministrator, error))
		{
			return false;
		}
		if (!isAdministrator)
			error = ERROR_ACCESS_DENIED;
		return isAdministrator;
	}

	bool LaunchApprovalHelperForChallenge(HANDLE clientProcess,
		const AccountIdentity& accountIdentity,
		PendingApprovalChallenge& pending,
		std::uint64_t& clientHelperProcessHandle,
		xucrun::broker::ApprovalFailureReason& failureReason,
		DWORD& error, std::wstring& failedStage)
	{
		clientHelperProcessHandle = 0;
		const std::filesystem::path clientPath(pending.clientPath);
		const std::filesystem::path helperPath =
			clientPath.parent_path() / L"xucRunApproval.exe";
		ClientImageIdentity helperIdentity;
		if (!ReadFileImageIdentity(helperPath.wstring(), helperIdentity, error, true) ||
			!IsApprovalHelperAuthorized(
				helperIdentity, SECURITY_MANDATORY_HIGH_RID, error))
		{
			failedStage = L"approval helper identity";
			return false;
		}

		DWORD helperProcessId = 0;
		DWORD secureHostProcessId = 0;
		HANDLE helperHostProcess = nullptr;
		HANDLE helperProcess = nullptr;
		if (pending.clientAccountPresenceConfirmation)
		{
			pending.helperStartMode =
				xucrun::broker::ApprovalHelperStartMode::ClientRunAsRequired;
			error = ERROR_ELEVATION_REQUIRED;
			return true;
		}
		bool targetIsAdministrator = false;
		DWORD membershipError = NO_ERROR;
		const bool targetMembershipResolved = IsAccountMemberOfAdministrators(
			accountIdentity, targetIsAdministrator, membershipError);
		const bool clientIsHighIntegrity = pending.elevationMode ==
			xucrun::broker::ElevationMode::PreserveToken && targetMembershipResolved &&
			!targetIsAdministrator && HasHighIntegrity(clientProcess, error);
		pending.helperStartMode = SelectApprovalHelperStartMode(pending.elevationMode,
			targetMembershipResolved, targetIsAdministrator, clientIsHighIntegrity);
		if (pending.helperStartMode ==
			xucrun::broker::ApprovalHelperStartMode::ClientRunAsRequired)
		{
			if (error != ERROR_ELEVATION_REQUIRED)
			{
				failedStage = L"approval helper client token";
				return false;
			}
			error = NO_ERROR;
			return true;
		}
		if (ShouldUseClientTokenForApprovalHelper(pending.elevationMode,
			targetMembershipResolved, targetIsAdministrator, clientIsHighIntegrity))
		{
			std::wstring cancellationEventName;
			if (!CreateSecureHostCancellationSignal(pending.challengeId,
				pending.clientSid, cancellationEventName,
				pending.secureHostCancellationSignal, error))
			{
				failedStage = L"secure desktop cancellation signal";
				return false;
			}
			if (!LaunchSecureDesktopClientApproval(clientProcess, pending.clientSid,
				pending.clientSessionId, helperPath.wstring(),
				L"/challenge:" + ChallengeToHex(pending.challengeId),
				cancellationEventName,
				static_cast<HANDLE>(pending.secureHostCancellationSignal.get()),
				helperHostProcess, helperProcess, secureHostProcessId,
				helperProcessId, error, failedStage))
			{
				return false;
			}
		}
		else
		{
			std::wstring cancellationEventName;
			if (!CreateSecureHostCancellationSignal(pending.challengeId,
				accountIdentity.sid, cancellationEventName,
				pending.secureHostCancellationSignal, error))
			{
				failedStage = L"secure desktop cancellation signal";
				return false;
			}
			std::wstring credentialTarget;
			std::vector<std::uint8_t> storedPassword;
			xucrun::SensitiveString password;
			if (!ResolveCredentialTarget(pending.clientSid, accountIdentity.sid,
				credentialTarget, error) ||
				!xucrun::broker_store::Read(credentialTarget, storedPassword, error) ||
				!AssignStoredPassword(storedPassword, password))
			{
				if (error == NO_ERROR)
					error = ERROR_INVALID_DATA;
				failedStage = L"approval helper credential";
				return false;
			}
			std::wstring effectiveWorkingDirectory;
			if (!LaunchApprovedCredentialProcess(clientProcess, accountIdentity, password,
				pending.clientSessionId, pending.clientPath, helperPath.wstring(),
				L"/secure-desktop-bootstrap /cancel-event:" + cancellationEventName +
				L" /challenge:" +
					ChallengeToHex(pending.challengeId),
				helperPath.parent_path().wstring(),
				xucrun::broker::ElevationMode::RequireElevated,
				xucrun::broker::LaunchVisibility::Normal,
				secureHostProcessId, effectiveWorkingDirectory, error, failedStage,
				&helperHostProcess, &failureReason))
			{
				return false;
			}
			if (!helperHostProcess)
			{
				error = GetLastError();
				failedStage = L"secure desktop bootstrap process";
				return false;
			}
		}

		ClientImageIdentity launchedIdentity;
		DWORD helperSessionId = 0;
		HANDLE validatedProcess = helperProcess ? helperProcess : helperHostProcess;
		const DWORD validatedProcessId = helperProcess ? helperProcessId : secureHostProcessId;
		if (!validatedProcess ||
			!ProcessIdToSessionId(validatedProcessId, &helperSessionId) ||
			helperSessionId != pending.clientSessionId ||
			!HasHighIntegrity(validatedProcess, error) ||
			!ReadClientImageIdentity(validatedProcess, launchedIdentity, error) ||
			!IsApprovalHelperAuthorized(
				launchedIdentity, SECURITY_MANDATORY_HIGH_RID, error))
		{
			if (error == NO_ERROR)
				error = ERROR_ACCESS_DENIED;
			failedStage = L"approval helper process validation";
			if (helperProcess)
			{
				TerminateProcess(helperProcess, ERROR_ACCESS_DENIED);
				CloseHandle(helperProcess);
			}
			if (helperHostProcess)
			{
				CancelSecureHost(pending.secureHostCancellationSignal);
				CloseHandle(helperHostProcess);
			}
			return false;
		}

		HANDLE clientHandle = nullptr;
		if (!helperHostProcess || !DuplicateHandle(GetCurrentProcess(), helperHostProcess,
			clientProcess,
			&clientHandle, SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
			FALSE, 0))
		{
			error = GetLastError();
			failedStage = L"approval helper process handle";
			CancelSecureHost(pending.secureHostCancellationSignal);
			CloseHandle(helperProcess);
			CloseHandle(helperHostProcess);
			return false;
		}
		const bool helperWasStartedDirectly = helperProcess != nullptr;
		if (helperProcess) CloseHandle(helperProcess);
		pending.secureHostProcess = OwnHandle(helperHostProcess);
		pending.secureHostProcessId = secureHostProcessId;
		pending.helperProcessId = helperWasStartedDirectly ? helperProcessId : 0;
		clientHelperProcessHandle = static_cast<std::uint64_t>(
			reinterpret_cast<ULONG_PTR>(clientHandle));
		return true;
	}

	bool LaunchCredentialCaptureHelper(HANDLE clientProcess,
		const AccountIdentity& approvalAccount,
		PendingCredentialCaptureChallenge& pending,
		std::uint64_t& clientHelperProcessHandle,
		DWORD& error, std::wstring& failedStage)
	{
		clientHelperProcessHandle = 0;
		const std::filesystem::path clientPath(pending.clientPath);
		const std::filesystem::path helperPath =
			clientPath.parent_path() / L"xucRunApproval.exe";
		ClientImageIdentity helperIdentity;
		if (!ReadFileImageIdentity(helperPath.wstring(), helperIdentity, error, true) ||
			!IsApprovalHelperAuthorized(
				helperIdentity, SECURITY_MANDATORY_HIGH_RID, error))
		{
			failedStage = L"credential capture helper identity";
			return false;
		}

		if (!HasHighIntegrity(clientProcess, error))
		{
			if (error != ERROR_ELEVATION_REQUIRED)
			{
				failedStage = L"credential capture client token";
				return false;
			}
			if (approvalAccount.sid.empty())
			{
				pending.helperStartMode =
					xucrun::broker::ApprovalHelperStartMode::ClientRunAsRequired;
				error = NO_ERROR;
				return true;
			}
		}

		DWORD helperProcessId = 0;
		DWORD secureHostProcessId = 0;
		HANDLE helperHostProcess = nullptr;
		HANDLE helperProcess = nullptr;
		pending.helperStartMode =
			xucrun::broker::ApprovalHelperStartMode::BrokerStarted;
		if (approvalAccount.sid.empty())
		{
			std::wstring cancellationEventName;
			if (!CreateSecureHostCancellationSignal(pending.challengeId,
				pending.clientSid, cancellationEventName,
				pending.secureHostCancellationSignal, error))
			{
				failedStage = L"secure desktop cancellation signal";
				return false;
			}
			if (!LaunchSecureDesktopClientApproval(clientProcess, pending.clientSid,
				pending.clientSessionId, helperPath.wstring(),
				L"/credentialchallenge:" + ChallengeToHex(pending.challengeId),
				cancellationEventName,
				static_cast<HANDLE>(pending.secureHostCancellationSignal.get()),
				helperHostProcess, helperProcess, secureHostProcessId,
				helperProcessId, error, failedStage))
			{
				return false;
			}
		}
		else
		{
			std::wstring cancellationEventName;
			if (!CreateSecureHostCancellationSignal(pending.challengeId,
				approvalAccount.sid, cancellationEventName,
				pending.secureHostCancellationSignal, error))
			{
				failedStage = L"secure desktop cancellation signal";
				return false;
			}
			std::wstring credentialTarget;
			std::vector<std::uint8_t> storedPassword;
			xucrun::SensitiveString password;
			if (!ResolveCredentialTarget(pending.clientSid, approvalAccount.sid,
				credentialTarget, error) ||
				!xucrun::broker_store::Read(credentialTarget, storedPassword, error) ||
				!AssignStoredPassword(storedPassword, password))
			{
				if (error == NO_ERROR)
					error = ERROR_INVALID_DATA;
				failedStage = L"credential capture approval credential";
				return false;
			}
			std::wstring effectiveWorkingDirectory;
			if (!LaunchApprovedCredentialProcess(clientProcess, approvalAccount, password,
				pending.clientSessionId, pending.clientPath, helperPath.wstring(),
				L"/secure-desktop-bootstrap /cancel-event:" + cancellationEventName +
				L" /credentialchallenge:" +
					ChallengeToHex(pending.challengeId),
				helperPath.parent_path().wstring(),
				xucrun::broker::ElevationMode::RequireElevated,
				xucrun::broker::LaunchVisibility::Normal,
				secureHostProcessId, effectiveWorkingDirectory, error, failedStage,
				&helperHostProcess))
			{
				return false;
			}
			if (!helperHostProcess)
			{
				error = GetLastError();
				failedStage = L"secure desktop credential bootstrap process";
				return false;
			}
		}

		ClientImageIdentity launchedIdentity;
		DWORD helperSessionId = 0;
		HANDLE validatedProcess = helperProcess ? helperProcess : helperHostProcess;
		const DWORD validatedProcessId = helperProcess ? helperProcessId : secureHostProcessId;
		if (!validatedProcess ||
			!ProcessIdToSessionId(validatedProcessId, &helperSessionId) ||
			helperSessionId != pending.clientSessionId ||
			!HasHighIntegrity(validatedProcess, error) ||
			!ReadClientImageIdentity(validatedProcess, launchedIdentity, error) ||
			!IsApprovalHelperAuthorized(
				launchedIdentity, SECURITY_MANDATORY_HIGH_RID, error))
		{
			if (error == NO_ERROR)
				error = ERROR_ACCESS_DENIED;
			failedStage = L"credential capture helper process validation";
			if (helperProcess)
			{
				TerminateProcess(helperProcess, ERROR_ACCESS_DENIED);
				CloseHandle(helperProcess);
			}
			if (helperHostProcess)
			{
				CancelSecureHost(pending.secureHostCancellationSignal);
				CloseHandle(helperHostProcess);
			}
			return false;
		}

		HANDLE clientHandle = nullptr;
		if (!helperHostProcess || !DuplicateHandle(GetCurrentProcess(), helperHostProcess,
			clientProcess,
			&clientHandle, SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
			FALSE, 0))
		{
			error = GetLastError();
			failedStage = L"credential capture helper process handle";
			CancelSecureHost(pending.secureHostCancellationSignal);
			CloseHandle(helperProcess);
			if (helperHostProcess) CloseHandle(helperHostProcess);
			return false;
		}
		const bool helperWasStartedDirectly = helperProcess != nullptr;
		if (helperProcess) CloseHandle(helperProcess);
		pending.secureHostProcess = OwnHandle(helperHostProcess);
		pending.secureHostProcessId = secureHostProcessId;
		pending.helperProcessId = helperWasStartedDirectly ? helperProcessId : 0;
		clientHelperProcessHandle = static_cast<std::uint64_t>(
			reinterpret_cast<ULONG_PTR>(clientHandle));
		return true;
	}

	// Retains only elevated processes reliably identified as running and closes all other handles.
	void PruneTrackedElevatedProcesses(
		std::vector<TrackedElevatedProcess>& processes)
	{
		processes.erase(std::remove_if(processes.begin(), processes.end(),
			[](const TrackedElevatedProcess& process) {
				if (WaitForSingleObject(process.process, 0) == WAIT_TIMEOUT)
					return false;
				CloseHandle(process.process);
				return true;
			}), processes.end());
	}

	// Generates a cryptographically random tracking ID distinct from all active entries.
	bool GenerateTrackingId(const std::vector<TrackedElevatedProcess>& processes,
		xucrun::broker::RequestNonce& trackingId)
	{
		for (int attempt = 0; attempt < 4; attempt++)
		{
			trackingId = {};
			if (BCryptGenRandom(nullptr, trackingId.data(),
				static_cast<ULONG>(trackingId.size()),
				BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
			{
				return false;
			}
			const bool nonzero = std::any_of(trackingId.begin(), trackingId.end(),
				[](std::uint8_t value) { return value != 0; });
			const bool duplicate = std::any_of(processes.begin(), processes.end(),
				[&trackingId](const TrackedElevatedProcess& process) {
					return process.trackingId == trackingId;
				});
			if (nonzero && !duplicate)
				return true;
		}
		trackingId = {};
		return false;
	}

	// Shortens a process path to a bounded display name.
	std::wstring GetProcessBaseName(const std::wstring& path)
	{
		const size_t separator = path.find_last_of(L"\\/");
		std::wstring name = separator == std::wstring::npos
			? path
			: path.substr(separator + 1);
		if (name.size() > xucrun::broker::MaximumProcessBaseNameLength)
			name.resize(xucrun::broker::MaximumProcessBaseNameLength);
		return name.empty() ? L"Unbekannter Prozess" : name;
	}

	// Identifies infrastructure processes that are not counted as launched payloads.
	bool IsInfrastructureProcess(const std::wstring& processBaseName)
	{
		return _wcsicmp(processBaseName.c_str(), L"conhost.exe") == 0;
	}

	// Reads a process creation time as a stable time value.
	bool GetProcessCreationTimeValue(HANDLE process, ULONGLONG& creationTime)
	{
		FILETIME created = {};
		FILETIME exited = {};
		FILETIME kernel = {};
		FILETIME user = {};
		if (!GetProcessTimes(process, &created, &exited, &kernel, &user))
			return false;
		ULARGE_INTEGER value = {};
		value.LowPart = created.dwLowDateTime;
		value.HighPart = created.dwHighDateTime;
		creationTime = value.QuadPart;
		return creationTime != 0;
	}

	// Reads the logon SID from the process token to bind it to the original logon session.
	bool GetProcessLogonSid(HANDLE process, std::wstring& logonSid, DWORD& error)
	{
		logonSid.clear();
		HANDLE token = nullptr;
		if (!OpenProcessToken(process, TOKEN_QUERY, &token))
		{
			error = GetLastError();
			return false;
		}
		DWORD size = 0;
		GetTokenInformation(token, TokenGroups, nullptr, 0, &size);
		std::vector<std::uint8_t> buffer(size);
		bool success = size != 0 && GetTokenInformation(
			token, TokenGroups, buffer.data(), size, &size) == TRUE;
		if (!success)
			error = GetLastError();
		if (success)
		{
			const auto* groups = reinterpret_cast<const TOKEN_GROUPS*>(buffer.data());
			success = false;
			for (DWORD index = 0; index < groups->GroupCount; index++)
			{
				if ((groups->Groups[index].Attributes & SE_GROUP_LOGON_ID) != SE_GROUP_LOGON_ID)
					continue;
				wchar_t* text = nullptr;
				if (ConvertSidToStringSidW(groups->Groups[index].Sid, &text))
				{
					logonSid = text;
					LocalFree(text);
					success = true;
				}
				else
				{
					error = GetLastError();
				}
				break;
			}
			if (!success && error == NO_ERROR)
				error = ERROR_NO_SUCH_LOGON_SESSION;
		}
		CloseHandle(token);
		return success;
	}

	// Accepts a tracked process only with a matching PID, session, logon SID, and elevated token.
	bool ValidateRegisteredProcess(HANDLE process, DWORD expectedProcessId,
		DWORD expectedSessionId, const std::wstring& expectedLogonSid,
		std::wstring& processBaseName, DWORD& error)
	{
		error = NO_ERROR;
		if (GetProcessId(process) != expectedProcessId ||
			WaitForSingleObject(process, 0) != WAIT_TIMEOUT)
		{
			error = ERROR_PROCESS_ABORTED;
			return false;
		}
		DWORD sessionId = 0;
		if (!ProcessIdToSessionId(expectedProcessId, &sessionId) ||
			sessionId != expectedSessionId)
		{
			error = GetLastError();
			if (error == NO_ERROR)
				error = ERROR_ACCESS_DENIED;
			return false;
		}
		std::wstring logonSid;
		if (!GetProcessLogonSid(process, logonSid, error) ||
			_wcsicmp(logonSid.c_str(), expectedLogonSid.c_str()) != 0)
		{
			if (error == NO_ERROR)
				error = ERROR_ACCESS_DENIED;
			return false;
		}
		HANDLE token = nullptr;
		if (!OpenProcessToken(process, TOKEN_QUERY, &token))
		{
			error = GetLastError();
			return false;
		}
		TOKEN_ELEVATION elevation = {};
		DWORD tokenSize = 0;
		const bool elevationRead = GetTokenInformation(token, TokenElevation,
			&elevation, sizeof(elevation), &tokenSize) == TRUE;
		const bool elevated = elevationRead && elevation.TokenIsElevated;
		if (!elevationRead)
			error = GetLastError();
		else if (!elevated)
			error = ERROR_ELEVATION_REQUIRED;
		CloseHandle(token);
		if (!elevated)
			return false;

		std::vector<wchar_t> imagePath(xucrun::broker::MaximumPathLength + 1);
		DWORD imagePathLength = static_cast<DWORD>(imagePath.size());
		if (!QueryFullProcessImageNameW(
			process, 0, imagePath.data(), &imagePathLength) || imagePathLength == 0)
		{
			error = GetLastError();
			return false;
		}
		processBaseName = GetProcessBaseName(
			std::wstring(imagePath.data(), imagePathLength));
		return true;
	}

	// Binds the root process to a job object so subsequent processes can be tracked in a controlled manner.
	bool CreateTrackedProcessJob(HANDLE process, DWORD processId, DWORD sessionId,
		const std::wstring& logonSid, bool discoverLogonSessionProcesses,
		std::vector<TrackedElevatedProcessJob>& jobs, DWORD& error)
	{
		HANDLE job = CreateJobObjectW(nullptr, nullptr);
		if (!job)
		{
			error = GetLastError();
			return false;
		}
		ULONGLONG rootCreationTime = 0;
		HANDLE assignmentProcess = OpenProcess(
			PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, processId);
		if (!GetProcessCreationTimeValue(process, rootCreationTime) ||
			!assignmentProcess || GetProcessId(process) != processId ||
			!AssignProcessToJobObject(job, assignmentProcess))
		{
			error = GetLastError();
			if (error == NO_ERROR)
				error = ERROR_INVALID_PARAMETER;
			if (assignmentProcess)
				CloseHandle(assignmentProcess);
			CloseHandle(job);
			return false;
		}
		CloseHandle(assignmentProcess);
		jobs.push_back({ job, sessionId, logonSid, rootCreationTime,
			discoverLogonSessionProcesses });
		return true;
	}

	// Discovers only processes created after the job started in the same validated logon session.
	void DiscoverTrackedLogonSessionProcesses(
		std::vector<TrackedElevatedProcess>& processes,
		const std::vector<TrackedElevatedProcessJob>& jobs)
	{
		HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
		if (snapshot == INVALID_HANDLE_VALUE)
			return;
		PROCESSENTRY32W candidate = { sizeof(candidate) };
		if (Process32FirstW(snapshot, &candidate))
		{
			do
			{
				const DWORD processId = candidate.th32ProcessID;
				if (processId == 0 ||
					std::any_of(processes.begin(), processes.end(),
						[processId](const auto& process) {
							return process.processId == processId;
						}))
				{
					candidate.dwSize = sizeof(candidate);
					continue;
				}
				for (const TrackedElevatedProcessJob& job : jobs)
				{
					if (!job.discoverLogonSessionProcesses)
						continue;
					HANDLE process = OpenProcess(
						PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, processId);
					ULONGLONG creationTime = 0;
					std::wstring processBaseName;
					DWORD validationError = NO_ERROR;
					if (!process || !GetProcessCreationTimeValue(process, creationTime) ||
						creationTime < job.rootCreationTime ||
						!ValidateRegisteredProcess(process, processId, job.sessionId,
							job.logonSid, processBaseName, validationError) ||
						IsInfrastructureProcess(processBaseName))
					{
						if (process)
							CloseHandle(process);
						continue;
					}
					xucrun::broker::RequestNonce trackingId = {};
					if (GenerateTrackingId(processes, trackingId))
					{
						processes.push_back({ process, processId, job.sessionId,
							trackingId, std::move(processBaseName) });
					}
					else
						CloseHandle(process);
					break;
				}
				candidate.dwSize = sizeof(candidate);
			} while (Process32NextW(snapshot, &candidate));
		}
		CloseHandle(snapshot);
	}

	// Captures elevated user processes exclusively in the requested session.
	void DiscoverElevatedUserProcesses(
		std::vector<TrackedElevatedProcess>& processes,
		DWORD sessionId)
	{
		HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
		if (snapshot == INVALID_HANDLE_VALUE)
			return;
		PROCESSENTRY32W candidate = { sizeof(candidate) };
		if (Process32FirstW(snapshot, &candidate))
		{
			do
			{
				const DWORD processId = candidate.th32ProcessID;
				DWORD processSessionId = 0;
				if (processId == 0 ||
					!ProcessIdToSessionId(processId, &processSessionId) ||
					processSessionId != sessionId ||
					std::any_of(processes.begin(), processes.end(),
						[processId](const auto& process) {
							return process.processId == processId;
						}))
				{
					candidate.dwSize = sizeof(candidate);
					continue;
				}
				HANDLE process = OpenProcess(
					PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, processId);
				std::wstring processLogonSid;
				std::wstring processBaseName;
				DWORD validationError = NO_ERROR;
				if (!process ||
					!GetProcessLogonSid(process, processLogonSid, validationError) ||
					!ValidateRegisteredProcess(process, processId, sessionId,
						processLogonSid, processBaseName, validationError) ||
					IsInfrastructureProcess(processBaseName))
				{
					if (process)
						CloseHandle(process);
					candidate.dwSize = sizeof(candidate);
					continue;
				}
				xucrun::broker::RequestNonce trackingId = {};
				if (GenerateTrackingId(processes, trackingId))
				{
					processes.push_back({ process, processId, sessionId,
						trackingId, std::move(processBaseName) });
				}
				else
				{
					CloseHandle(process);
				}
				candidate.dwSize = sizeof(candidate);
			} while (Process32NextW(snapshot, &candidate));
		}
		CloseHandle(snapshot);
	}

	// Synchronizes process and job tracking and revalidates every newly found process.
	void RefreshTrackedElevatedProcesses(
		std::vector<TrackedElevatedProcess>& processes,
		std::vector<TrackedElevatedProcessJob>& jobs)
	{
		PruneTrackedElevatedProcesses(processes);
		for (auto job = jobs.begin(); job != jobs.end();)
		{
			std::vector<std::uint8_t> buffer(sizeof(JOBOBJECT_BASIC_PROCESS_ID_LIST) +
				64 * sizeof(ULONG_PTR));
			auto* processList = reinterpret_cast<JOBOBJECT_BASIC_PROCESS_ID_LIST*>(
				buffer.data());
			if (!QueryInformationJobObject(job->job, JobObjectBasicProcessIdList,
				processList, static_cast<DWORD>(buffer.size()), nullptr))
			{
				CloseHandle(job->job);
				job = jobs.erase(job);
				continue;
			}
			if (processList->NumberOfAssignedProcesses == 0)
			{
				CloseHandle(job->job);
				job = jobs.erase(job);
				continue;
			}
			for (DWORD index = 0; index < processList->NumberOfProcessIdsInList; index++)
			{
				const DWORD processId = static_cast<DWORD>(processList->ProcessIdList[index]);
				if (processId == 0 || std::any_of(processes.begin(), processes.end(),
					[processId](const auto& process) { return process.processId == processId; }))
				{
					continue;
				}
				HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE,
					FALSE, processId);
				if (!process)
					continue;
				std::wstring processBaseName;
				DWORD validationError = NO_ERROR;
				if (!ValidateRegisteredProcess(process, processId, job->sessionId,
					job->logonSid, processBaseName, validationError) ||
					IsInfrastructureProcess(processBaseName))
				{
					CloseHandle(process);
					continue;
				}
				xucrun::broker::RequestNonce trackingId = {};
				if (!GenerateTrackingId(processes, trackingId))
				{
					CloseHandle(process);
					continue;
				}
				processes.push_back({ process, processId, job->sessionId,
					trackingId, std::move(processBaseName) });
			}
			++job;
		}
		DiscoverTrackedLogonSessionProcesses(processes, jobs);
	}

	// Counts only elevated processes in the specified session after a complete update.
	std::uint32_t CountTrackedElevatedProcesses(
		std::vector<TrackedElevatedProcess>& processes,
		std::vector<TrackedElevatedProcessJob>& jobs, DWORD sessionId)
	{
		RefreshTrackedElevatedProcesses(processes, jobs);
		DiscoverElevatedUserProcesses(processes, sessionId);
		return static_cast<std::uint32_t>(std::count_if(
			processes.begin(), processes.end(), [sessionId](const auto& process) {
				return process.sessionId == sessionId;
			}));
	}

	bool CanPersistApproval(
		const PendingApprovalChallenge& pending,
		const xucrun::broker::ApprovalConfirmationRequest& request)
	{
		if (request.persistence == xucrun::broker::ApprovalPersistence::Once ||
			request.programScope !=
				xucrun::broker::ApprovalProgramScope::ExactInvocation)
		{
			return true;
		}
		if (pending.directScriptPath.empty() &&
			IsPowerShellInterpreter(pending.executablePath))
		{
			return false;
		}
		const bool reusableScriptBinding =
			pending.scriptPath == pending.directScriptPath &&
			!pending.scriptPath.empty() &&
			std::any_of(pending.scriptSha256.begin(), pending.scriptSha256.end(),
				[](std::uint8_t value) { return value != 0; });
		return pending.directScriptPath.empty() || reusableScriptBinding;
	}

	// Persists a confirmed approval with owner, account, program, and lifetime bindings.
	bool StoreApproval(const PendingApprovalChallenge& pending,
		const xucrun::broker::ApprovalConfirmationRequest& request,
		xucrun::approval::BrokerApprovalValue& storedApproval,
		bool& approvalStored, DWORD& error)
	{
		approvalStored = false;
		if (request.persistence == xucrun::broker::ApprovalPersistence::Once)
			return true;
		if (!CanPersistApproval(pending, request))
		{
			error = ERROR_ACCESS_DENIED;
			return false;
		}
		xucrun::approval::BrokerApprovalValue approval;
		switch (request.programScope)
		{
		case xucrun::broker::ApprovalProgramScope::General:
			approval.programScope =
				xucrun::approval::BrokerApprovalProgramScope::General;
			break;
		case xucrun::broker::ApprovalProgramScope::TargetProgram:
			approval.programScope =
				xucrun::approval::BrokerApprovalProgramScope::TargetProgram;
			break;
		case xucrun::broker::ApprovalProgramScope::ExactInvocation:
			approval.programScope =
				xucrun::approval::BrokerApprovalProgramScope::ExactInvocation;
			break;
		default:
			error = ERROR_INVALID_DATA;
			return false;
		}
		approval.accountScope = request.accountScope ==
			xucrun::broker::ApprovalAccountScope::CurrentAccount
			? xucrun::approval::BrokerApprovalAccountScope::CurrentAccount
			: xucrun::approval::BrokerApprovalAccountScope::AllAccounts;
		approval.elevationMode = static_cast<std::uint8_t>(pending.elevationMode);
		FILETIME now = {};
		GetSystemTimeAsFileTime(&now);
		ULARGE_INTEGER timestamp = {};
		timestamp.LowPart = now.dwLowDateTime;
		timestamp.HighPart = now.dwHighDateTime;
		approval.createdAt = timestamp.QuadPart;
		if (request.persistence == xucrun::broker::ApprovalPersistence::TenMinutes ||
			request.persistence == xucrun::broker::ApprovalPersistence::OneDay)
		{
			approval.lifetime = xucrun::approval::BrokerApprovalLifetime::Until;
			const std::uint64_t seconds = request.persistence ==
				xucrun::broker::ApprovalPersistence::TenMinutes ? 600 : 86400;
			approval.expiresAt = approval.createdAt + seconds * 10000000ULL;
		}
		else if (request.persistence == xucrun::broker::ApprovalPersistence::LogonSession)
		{
			approval.lifetime = xucrun::approval::BrokerApprovalLifetime::LogonSession;
			approval.sessionLogonSid = pending.clientLogonSid;
		}
		else
		{
			approval.lifetime = xucrun::approval::BrokerApprovalLifetime::Forever;
		}
		approval.ownerUserSid = pending.clientSid;
		if (approval.accountScope == xucrun::approval::BrokerApprovalAccountScope::CurrentAccount)
			approval.targetAccountSid = pending.accountSid;
		if (approval.programScope !=
			xucrun::approval::BrokerApprovalProgramScope::General)
		{
			approval.callerPath = pending.clientPath;
			approval.targetPath = pending.executablePath;
			std::copy_n(pending.clientSha256.begin(), approval.callerHash.size(),
				approval.callerHash.begin());
			approval.targetHash = pending.executableSha256;
			if (approval.programScope ==
				xucrun::approval::BrokerApprovalProgramScope::ExactInvocation)
			{
				approval.visibility = static_cast<std::uint8_t>(pending.visibility);
				approval.argumentHash = pending.argumentHash;
				approval.workingDirectory = pending.workingDirectory;
				approval.scriptPath = pending.scriptPath;
				approval.scriptHash = pending.scriptSha256;
			}
		}
		if (!xucrun::approval_store::Write(approval, error))
			return false;
		storedApproval = std::move(approval);
		approvalStored = true;
		return true;
	}

	// Creates the complete security context for matching stored approvals.
	bool BuildApprovalContext(const PendingApprovalChallenge& pending,
		xucrun::approval::BrokerApprovalValue& context)
	{
		context = {};
		context.programScope =
			xucrun::approval::BrokerApprovalProgramScope::TargetProgram;
		context.accountScope = xucrun::approval::BrokerApprovalAccountScope::CurrentAccount;
		context.lifetime = xucrun::approval::BrokerApprovalLifetime::Forever;
		context.createdAt = 1;
		context.elevationMode = static_cast<std::uint8_t>(pending.elevationMode);
		context.ownerUserSid = pending.clientSid;
		context.targetAccountSid = pending.accountSid;
		context.sessionLogonSid = pending.clientLogonSid;
		context.callerPath = pending.clientPath;
		context.targetPath = pending.executablePath;
		std::copy_n(pending.clientSha256.begin(), context.callerHash.size(),
			context.callerHash.begin());
		context.targetHash = pending.executableSha256;
		context.visibility = static_cast<std::uint8_t>(pending.visibility);
		context.argumentHash = pending.argumentHash;
		context.workingDirectory = pending.workingDirectory;
		context.scriptPath = pending.scriptPath;
		context.scriptHash = pending.scriptSha256;
		return true;
	}

	// Checks the current challenge context against stored approvals that are still valid.
	bool HasStoredApproval(const PendingApprovalChallenge& pending,
		bool& matches, DWORD& error)
	{
		xucrun::approval::BrokerApprovalValue context;
		if (!BuildApprovalContext(pending, context))
			return false;
		FILETIME now = {};
		GetSystemTimeAsFileTime(&now);
		ULARGE_INTEGER timestamp = {};
		timestamp.LowPart = now.dwLowDateTime;
		timestamp.HighPart = now.dwHighDateTime;
		return xucrun::approval_store::HasMatching(
			context, timestamp.QuadPart, matches, error);
	}

	// Generates a cryptographically random, non-reused challenge ID.
	bool GenerateChallengeId(
		const std::deque<PendingApprovalChallenge>& challenges,
		xucrun::broker::RequestNonce& challengeId)
	{
		for (int attempt = 0; attempt < 4; attempt++)
		{
			challengeId = {};
			if (BCryptGenRandom(nullptr, challengeId.data(),
				static_cast<ULONG>(challengeId.size()),
				BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
			{
				return false;
			}
			const bool nonzero = std::any_of(challengeId.begin(), challengeId.end(),
				[](std::uint8_t value) { return value != 0; });
			const bool duplicate = std::any_of(challenges.begin(), challenges.end(),
				[&challengeId](const PendingApprovalChallenge& challenge) {
					return challenge.challengeId == challengeId;
				});
			if (nonzero && !duplicate)
				return true;
		}
		challengeId = {};
		return false;
	}

	bool GenerateCredentialCaptureChallengeId(
		const std::deque<PendingCredentialCaptureChallenge>& challenges,
		xucrun::broker::RequestNonce& challengeId)
	{
		for (int attempt = 0; attempt < 4; attempt++)
		{
			challengeId = {};
			if (BCryptGenRandom(nullptr, challengeId.data(),
				static_cast<ULONG>(challengeId.size()),
				BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
			{
				return false;
			}
			const bool nonzero = std::any_of(challengeId.begin(), challengeId.end(),
				[](std::uint8_t value) { return value != 0; });
			const bool duplicate = std::any_of(challenges.begin(), challenges.end(),
				[&challengeId](const PendingCredentialCaptureChallenge& challenge) {
					return challenge.challengeId == challengeId;
				});
			if (nonzero && !duplicate)
				return true;
		}
		challengeId = {};
		return false;
	}

	void PrunePendingCredentialCaptureChallenges(
		std::deque<PendingCredentialCaptureChallenge>& challenges)
	{
		const ULONGLONG now = GetTickCount64();
		std::erase_if(challenges,
			[now](const PendingCredentialCaptureChallenge& challenge) {
				const bool expired = challenge.expiresAt <= now;
				if (expired) CancelSecureHost(challenge.secureHostCancellationSignal);
				return expired;
			});
	}

	bool HasPendingCredentialCaptureCapacity(
		const std::deque<PendingCredentialCaptureChallenge>& challenges,
		const std::wstring& clientSid, DWORD clientSessionId, DWORD clientProcessId)
	{
		if (challenges.size() >= MaximumPendingApprovalChallenges)
			return false;
		const auto count = [&challenges](const auto& predicate) {
			return static_cast<std::size_t>(std::count_if(
				challenges.begin(), challenges.end(), predicate));
		};
		return count([&clientSid](const PendingCredentialCaptureChallenge& challenge) {
				return challenge.clientSid == clientSid;
			}) < MaximumPendingApprovalChallengesPerSid &&
			count([clientSessionId](const PendingCredentialCaptureChallenge& challenge) {
				return challenge.clientSessionId == clientSessionId;
			}) < MaximumPendingApprovalChallengesPerSession &&
			count([clientProcessId](const PendingCredentialCaptureChallenge& challenge) {
				return challenge.clientProcessId == clientProcessId;
			}) < MaximumPendingApprovalChallengesPerProcess;
	}

	// Removes expired challenges without displacing a still-active request.
	void PrunePendingApprovalChallenges(
		std::deque<PendingApprovalChallenge>& challenges)
	{
		const ULONGLONG now = GetTickCount64();
		std::erase_if(challenges, [now](const PendingApprovalChallenge& challenge) {
			const bool expired = challenge.expiresAt <= now;
			if (expired) CancelSecureHost(challenge.secureHostCancellationSignal);
			return expired;
		});
	}

	DWORD GetPendingChallengeWaitMilliseconds(ULONGLONG now,
		const std::deque<PendingApprovalChallenge>& approvalChallenges,
		const std::deque<PendingCredentialCaptureChallenge>& credentialChallenges)
	{
		ULONGLONG nextExpiration = (std::numeric_limits<ULONGLONG>::max)();
		for (const PendingApprovalChallenge& challenge : approvalChallenges)
			nextExpiration = (std::min)(nextExpiration, challenge.expiresAt);
		for (const PendingCredentialCaptureChallenge& challenge : credentialChallenges)
			nextExpiration = (std::min)(nextExpiration, challenge.expiresAt);
		if (nextExpiration == (std::numeric_limits<ULONGLONG>::max)())
			return INFINITE;
		if (nextExpiration <= now)
			return 0;
		return static_cast<DWORD>((std::min)(
			nextExpiration - now, static_cast<ULONGLONG>(INFINITE - 1)));
	}

	bool HasPendingSecureDesktopChallenge(
		const std::deque<PendingApprovalChallenge>& approvalChallenges,
		const std::deque<PendingCredentialCaptureChallenge>& credentialChallenges,
		DWORD clientSessionId)
	{
		return std::any_of(approvalChallenges.begin(), approvalChallenges.end(),
			[clientSessionId](const PendingApprovalChallenge& challenge) {
				return challenge.clientSessionId == clientSessionId &&
					(challenge.secureHostProcess || challenge.helperProcessId != 0);
			}) ||
			std::any_of(credentialChallenges.begin(), credentialChallenges.end(),
				[clientSessionId](const PendingCredentialCaptureChallenge& challenge) {
					return challenge.clientSessionId == clientSessionId &&
						(challenge.secureHostProcess || challenge.helperProcessId != 0);
				});
	}

	bool HasPendingApprovalChallengeCapacity(
		const std::deque<PendingApprovalChallenge>& challenges,
		const std::wstring& clientSid, DWORD clientSessionId, DWORD clientProcessId,
		xucrun::broker::ApprovalFailureReason& failureReason)
	{
		failureReason = xucrun::broker::ApprovalFailureReason::None;
		if (challenges.size() >= MaximumPendingApprovalChallenges)
		{
			failureReason = xucrun::broker::ApprovalFailureReason::ChallengeGlobalLimitReached;
			return false;
		}
		const auto count = [&challenges](const auto& predicate) {
			return static_cast<std::size_t>(std::count_if(
				challenges.begin(), challenges.end(), predicate));
		};
		if (count([&clientSid](const PendingApprovalChallenge& challenge) {
			return challenge.clientSid == clientSid;
			}) >= MaximumPendingApprovalChallengesPerSid)
		{
			failureReason = xucrun::broker::ApprovalFailureReason::ChallengeSidLimitReached;
			return false;
		}
		if (count([clientSessionId](const PendingApprovalChallenge& challenge) {
			return challenge.clientSessionId == clientSessionId;
			}) >= MaximumPendingApprovalChallengesPerSession)
		{
			failureReason = xucrun::broker::ApprovalFailureReason::ChallengeSessionLimitReached;
			return false;
		}
		if (count([clientProcessId](const PendingApprovalChallenge& challenge) {
			return challenge.clientProcessId == clientProcessId;
			}) >= MaximumPendingApprovalChallengesPerProcess)
		{
			failureReason = xucrun::broker::ApprovalFailureReason::ChallengeProcessLimitReached;
			return false;
		}
		return true;
	}

	bool GetClientMaximumAllowedAccess(HANDLE clientProcess,
		const std::wstring& path, DWORD& grantedAccess, DWORD& error)
	{
		grantedAccess = 0;
		PSECURITY_DESCRIPTOR descriptor = nullptr;
		PSID owner = nullptr;
		PSID group = nullptr;
		PACL dacl = nullptr;
		const DWORD securityError = GetNamedSecurityInfoW(
			const_cast<LPWSTR>(path.c_str()), SE_FILE_OBJECT,
			OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION |
			DACL_SECURITY_INFORMATION,
			&owner, &group, &dacl, nullptr, &descriptor);
		if (securityError != ERROR_SUCCESS)
		{
			error = securityError;
			return false;
		}

		HANDLE primaryToken = nullptr;
		HANDLE impersonationToken = nullptr;
		bool success = OpenProcessToken(clientProcess,
			TOKEN_QUERY | TOKEN_DUPLICATE, &primaryToken) == TRUE &&
			DuplicateToken(primaryToken, SecurityImpersonation,
				&impersonationToken) == TRUE;
		if (!success)
			error = GetLastError();
		GENERIC_MAPPING mapping = {
			FILE_GENERIC_READ, FILE_GENERIC_WRITE, FILE_GENERIC_EXECUTE, FILE_ALL_ACCESS
		};
		std::vector<std::uint8_t> privilegeBuffer(sizeof(PRIVILEGE_SET) +
			16 * sizeof(LUID_AND_ATTRIBUTES));
		DWORD privilegeSize = static_cast<DWORD>(privilegeBuffer.size());
		BOOL accessGranted = FALSE;
		if (success)
		{
			success = AccessCheck(descriptor, impersonationToken, MAXIMUM_ALLOWED,
				&mapping, reinterpret_cast<PRIVILEGE_SET*>(privilegeBuffer.data()),
				&privilegeSize, &grantedAccess, &accessGranted) == TRUE &&
				accessGranted == TRUE;
			if (!success)
				error = GetLastError();
		}
		if (impersonationToken)
			CloseHandle(impersonationToken);
		if (primaryToken)
			CloseHandle(primaryToken);
		LocalFree(descriptor);
		return success;
	}

	bool IsDirectScriptPathProtected(HANDLE clientProcess,
		const std::wstring& scriptPath)
	{
		DWORD grantedAccess = 0;
		DWORD accessError = NO_ERROR;
		std::filesystem::path directory =
			std::filesystem::path(scriptPath).parent_path();
		bool immediateParent = true;
		while (!directory.empty())
		{
			grantedAccess = 0;
			const DWORD directoryWriteMask =
				FILE_DELETE_CHILD | DELETE | WRITE_DAC | WRITE_OWNER |
				(immediateParent ? FILE_ADD_FILE : 0);
			if (!GetClientMaximumAllowedAccess(clientProcess,
					directory.native(), grantedAccess, accessError) ||
				(grantedAccess & directoryWriteMask) != 0)
			{
				return false;
			}
			const std::filesystem::path parent = directory.parent_path();
			if (parent == directory)
				break;
			directory = parent;
			immediateParent = false;
		}
		return true;
	}

	void ResolveDirectScriptBinding(HANDLE clientProcess,
		const std::wstring& executablePath, const std::wstring& argumentTail,
		const std::wstring& workingDirectory, std::wstring& displayedScriptPath,
		std::array<std::uint8_t, 32>& displayedScriptHash,
		std::wstring& boundScriptPath,
		std::array<std::uint8_t, 32>& boundScriptHash)
	{
		displayedScriptPath.clear();
		displayedScriptHash = {};
		boundScriptPath.clear();
		boundScriptHash = {};
		std::wstring scriptArgument;
		if (!TryGetDirectScriptArgument(
			executablePath, argumentTail, scriptArgument))
		{
			return;
		}

		std::filesystem::path requested(scriptArgument);
		if (requested.is_relative())
			requested = std::filesystem::path(workingDirectory) / requested;
		requested = requested.lexically_normal();
		displayedScriptPath = requested.native();

		HANDLE script = CreateFileW(displayedScriptPath.c_str(), GENERIC_READ,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
		if (script == INVALID_HANDLE_VALUE)
			return;
		std::wstring finalPath;
		std::vector<std::uint8_t> hash;
		BY_HANDLE_FILE_INFORMATION information = {};
		const bool identityRead =
			GetFileInformationByHandle(script, &information) == TRUE &&
			(information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 &&
			xucrun::files::ResolveFinalFilePathFromHandle(script, finalPath) &&
			xucrun::files::ComputeFileSHA256FromHandle(script, hash) &&
			hash.size() == boundScriptHash.size();
		CloseHandle(script);
		if (!identityRead)
			return;
		displayedScriptPath = finalPath;
		std::copy(hash.begin(), hash.end(), displayedScriptHash.begin());

		if (!IsDirectScriptPathProtected(clientProcess, finalPath))
			return;

		boundScriptPath = finalPath;
		boundScriptHash = displayedScriptHash;
	}

	bool LockAndValidateDirectScript(const PendingApprovalChallenge& pending,
		LaunchObjectLocks& locks, DWORD& error)
	{
		if (pending.directScriptPath.empty())
			return true;
		if (std::all_of(pending.directScriptSha256.begin(),
			pending.directScriptSha256.end(),
			[](std::uint8_t value) { return value == 0; }))
		{
			error = ERROR_FILE_INVALID;
			return false;
		}
		locks.script = CreateFileW(pending.directScriptPath.c_str(), GENERIC_READ,
			FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
		if (locks.script == INVALID_HANDLE_VALUE)
		{
			error = GetLastError();
			return false;
		}
		std::wstring finalPath;
		std::vector<std::uint8_t> hash;
		BY_HANDLE_FILE_INFORMATION information = {};
		if (GetFileInformationByHandle(locks.script, &information) != TRUE ||
			(information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ||
			!xucrun::files::ResolveFinalFilePathFromHandle(locks.script, finalPath) ||
			!xucrun::files::ComputeFileSHA256FromHandle(locks.script, hash) ||
			finalPath != pending.directScriptPath ||
			hash.size() != pending.directScriptSha256.size() ||
			!std::equal(hash.begin(), hash.end(),
				pending.directScriptSha256.begin()))
		{
			error = ERROR_FILE_INVALID;
			return false;
		}
		return true;
	}

	bool IsElevatedLaunchPathProtected(HANDLE clientProcess,
		const std::wstring& path, bool directory,
		xucrun::broker::ApprovalFailureReason writableReason,
		xucrun::broker::ApprovalFailureReason& failureReason, DWORD& error)
	{
		DWORD grantedAccess = 0;
		if (!GetClientMaximumAllowedAccess(clientProcess, path, grantedAccess, error))
		{
			failureReason = xucrun::broker::ApprovalFailureReason::ElevatedPathValidationFailed;
			return false;
		}
		const DWORD objectWriteMask = directory
			? FILE_ADD_FILE | FILE_ADD_SUBDIRECTORY | FILE_DELETE_CHILD |
				FILE_WRITE_ATTRIBUTES | FILE_WRITE_EA | DELETE | WRITE_DAC | WRITE_OWNER
			: FILE_WRITE_DATA | FILE_APPEND_DATA | FILE_WRITE_ATTRIBUTES |
				FILE_WRITE_EA | DELETE | WRITE_DAC | WRITE_OWNER;
		if ((grantedAccess & objectWriteMask) != 0)
		{
			error = ERROR_ACCESS_DENIED;
			failureReason = writableReason;
			return false;
		}
		return true;
	}

	bool ValidateElevatedLaunchPaths(HANDLE clientProcess,
		const std::wstring& executablePath,
		xucrun::broker::ApprovalFailureReason& failureReason, DWORD& error)
	{
		failureReason = xucrun::broker::ApprovalFailureReason::None;
		const std::filesystem::path executable(executablePath);
		if (!IsElevatedLaunchPathProtected(clientProcess, executablePath, false,
			xucrun::broker::ApprovalFailureReason::ElevatedTargetWritable,
			failureReason, error))
		{
			return false;
		}
		return IsElevatedLaunchPathProtected(
			clientProcess, executable.parent_path().native(), true,
			xucrun::broker::ApprovalFailureReason::ElevatedTargetDirectoryWritable,
			failureReason, error);
	}

	bool ValidateElevatedLaunchDirectory(HANDLE clientProcess,
		const std::wstring& executablePath,
		xucrun::broker::ApprovalFailureReason& failureReason, DWORD& error)
	{
		const std::filesystem::path executable(executablePath);
		return IsElevatedLaunchPathProtected(
			clientProcess, executable.parent_path().native(), true,
			xucrun::broker::ApprovalFailureReason::ElevatedTargetDirectoryWritable,
			failureReason, error);
	}

	bool ValidateElevatedLaunchPathsForChallenge(HANDLE clientProcess,
		const std::wstring& executablePath,
		xucrun::broker::ApprovalFailureReason& writableReason,
		xucrun::broker::ApprovalFailureReason& failureReason, DWORD& error)
	{
		writableReason = xucrun::broker::ApprovalFailureReason::None;
		failureReason = xucrun::broker::ApprovalFailureReason::None;
		if (!IsElevatedLaunchPathProtected(clientProcess, executablePath, false,
			xucrun::broker::ApprovalFailureReason::ElevatedTargetWritable,
			failureReason, error))
		{
			if (failureReason !=
				xucrun::broker::ApprovalFailureReason::ElevatedTargetWritable)
			{
				return false;
			}
			writableReason = failureReason;
			failureReason = xucrun::broker::ApprovalFailureReason::None;
			error = NO_ERROR;
		}
		return ValidateElevatedLaunchDirectory(
			clientProcess, executablePath, failureReason, error);
	}

	bool LockAndValidateLaunchObjects(const PendingApprovalChallenge& pending,
		LaunchObjectLocks& locks,
		xucrun::broker::ApprovalFailureReason& failureReason, DWORD& error)
	{
		failureReason = xucrun::broker::ApprovalFailureReason::None;
		locks.executable = CreateFileW(pending.executablePath.c_str(), GENERIC_READ,
			FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
		if (locks.executable == INVALID_HANDLE_VALUE)
		{
			error = GetLastError();
			failureReason = xucrun::broker::ApprovalFailureReason::LaunchObjectLockFailed;
			return false;
		}
		std::wstring finalExecutablePath;
		std::vector<std::uint8_t> executableSha256;
		if (!xucrun::files::ResolveFinalFilePathFromHandle(
				locks.executable, finalExecutablePath) ||
			!xucrun::files::ComputeFileSHA256FromHandle(
				locks.executable, executableSha256))
		{
			error = GetLastError() == NO_ERROR ? ERROR_FILE_INVALID : GetLastError();
			failureReason = xucrun::broker::ApprovalFailureReason::LaunchObjectLockFailed;
			return false;
		}
		if (_wcsicmp(finalExecutablePath.c_str(), pending.executablePath.c_str()) != 0 ||
			executableSha256.size() != pending.executableSha256.size() ||
			!std::equal(executableSha256.begin(), executableSha256.end(),
				pending.executableSha256.begin()))
		{
			error = ERROR_FILE_INVALID;
			failureReason = xucrun::broker::ApprovalFailureReason::LaunchObjectChanged;
			return false;
		}
		if (pending.workingDirectory.empty())
			return true;
		locks.workingDirectory = CreateFileW(pending.workingDirectory.c_str(),
			FILE_READ_ATTRIBUTES, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
			FILE_FLAG_BACKUP_SEMANTICS, nullptr);
		if (locks.workingDirectory == INVALID_HANDLE_VALUE)
		{
			error = GetLastError();
			failureReason = xucrun::broker::ApprovalFailureReason::LaunchObjectLockFailed;
			return false;
		}
		std::wstring finalWorkingDirectory;
		if (!xucrun::files::ResolveFinalFilePathFromHandle(
				locks.workingDirectory, finalWorkingDirectory))
		{
			error = GetLastError() == NO_ERROR ? ERROR_DIRECTORY : GetLastError();
			failureReason = xucrun::broker::ApprovalFailureReason::LaunchObjectLockFailed;
			return false;
		}
		if (finalWorkingDirectory != pending.workingDirectory)
		{
			error = ERROR_DIRECTORY;
			failureReason = xucrun::broker::ApprovalFailureReason::LaunchObjectChanged;
			return false;
		}
		return true;
	}

	// Binds a working directory to its final directory path through a handle.
	bool ResolveWorkingDirectory(const std::wstring& requestedPath,
		std::wstring& finalPath, DWORD& error)
	{
		finalPath.clear();
		if (requestedPath.empty())
			return true;
		HANDLE directory = CreateFileW(requestedPath.c_str(), FILE_READ_ATTRIBUTES,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
			FILE_FLAG_BACKUP_SEMANTICS, nullptr);
		if (directory == INVALID_HANDLE_VALUE)
		{
			error = GetLastError();
			return false;
		}
		BY_HANDLE_FILE_INFORMATION information = {};
		const bool success = GetFileInformationByHandle(directory, &information) == TRUE &&
			(information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
			xucrun::files::ResolveFinalFilePathFromHandle(directory, finalPath);
		if (!success)
		{
			error = GetLastError();
			if (error == NO_ERROR)
				error = ERROR_DIRECTORY;
		}
		CloseHandle(directory);
		return success;
	}

	// Allows the diagnostic profile only for the precisely specified system target, argument, and working directory.
	bool IsApprovalDiagnosticRequestAllowed(
		const xucrun::broker::ApprovalChallengeRequest& request,
		const TargetImageIdentity& targetIdentity,
		const std::wstring& finalWorkingDirectory,
		DWORD& error)
	{
		if (request.elevationMode != xucrun::broker::ElevationMode::PreserveToken ||
			request.argumentTail != xucrun::broker::ApprovalDiagnosticArgumentTail)
		{
			error = ERROR_NOT_SUPPORTED;
			return false;
		}

		wchar_t systemDirectory[MAX_PATH] = {};
		const UINT directoryLength = GetSystemDirectoryW(
			systemDirectory, static_cast<UINT>(std::size(systemDirectory)));
		if (directoryLength == 0 || directoryLength >= std::size(systemDirectory))
		{
			error = GetLastError();
			return false;
		}

		TargetImageIdentity expectedTarget;
		std::wstring expectedWorkingDirectory;
		if (!ReadTargetImageIdentity(std::wstring(systemDirectory) + L"\\cmd.exe",
			expectedTarget, error) ||
			!ResolveWorkingDirectory(systemDirectory, expectedWorkingDirectory, error))
		{
			return false;
		}
		if (_wcsicmp(targetIdentity.path.c_str(), expectedTarget.path.c_str()) != 0 ||
			targetIdentity.sha256 != expectedTarget.sha256 ||
			_wcsicmp(finalWorkingDirectory.c_str(), expectedWorkingDirectory.c_str()) != 0)
		{
			error = ERROR_NOT_SUPPORTED;
			return false;
		}
		return true;
	}

	bool IsTrustedAdministrativeClientLaunch(
		const xucrun::broker::ApprovalChallengeRequest& request,
		const ClientImageIdentity& clientIdentity,
		const TargetImageIdentity& targetIdentity)
	{
		const std::wstring consolePrefix =
			xucrun::broker::AdministrativeInventoryConsoleArgumentPrefix;
		const bool consoleArgument =
			request.argumentTail.starts_with(consolePrefix) &&
			request.argumentTail.size() == consolePrefix.size() +
				xucrun::broker::AdministrativeInventoryConsoleTokenLength &&
			std::all_of(request.argumentTail.begin() + consolePrefix.size(),
				request.argumentTail.end(), [](wchar_t character) {
					return (character >= L'0' && character <= L'9') ||
						(character >= L'a' && character <= L'f') ||
						(character >= L'A' && character <= L'F');
				});
		std::wstring backupPipeToken;
		std::wstring backupCommand;
		const bool backupCommandArgument =
			xucrun::broker::ParseAdministrativeBackupCommandArgument(
				request.argumentTail, backupPipeToken, backupCommand);
		std::wstring language;
		std::wstring languagePipeToken;
		const bool languageChangeArgument =
			xucrun::broker::ParseSetLanguageCommandArgument(
				request.argumentTail, language, languagePipeToken);
		const bool validPresentation =
			(request.visibility == xucrun::broker::LaunchVisibility::Normal &&
				(request.argumentTail ==
					xucrun::broker::AdministrativeInventoryViewerArgumentTail ||
				 request.argumentTail ==
					xucrun::broker::AdministrativeBackupViewerArgumentTail ||
				 request.argumentTail ==
					xucrun::broker::AdministrativeBackupRestoreViewerArgumentTail)) ||
			(request.visibility == xucrun::broker::LaunchVisibility::Hidden &&
				(consoleArgument || backupCommandArgument || languageChangeArgument));
		return request.elevationMode ==
				xucrun::broker::ElevationMode::RequireElevated &&
			request.approvalAccountSid.empty() &&
			validPresentation &&
			_wcsicmp(clientIdentity.path.c_str(), targetIdentity.path.c_str()) == 0 &&
			clientIdentity.sha256 == targetIdentity.sha256;
	}

	// Performs pipe I/O with cancellation and time limits so no client can block the broker.
	bool RunOverlappedIo(HANDLE pipe, HANDLE stopEvent, bool write,
		void* buffer, DWORD bufferSize, DWORD& transferred)
	{
		HANDLE operationEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		if (!operationEvent)
			return false;
		OVERLAPPED operation = {};
		operation.hEvent = operationEvent;
		const BOOL started = write
			? WriteFile(pipe, buffer, bufferSize, nullptr, &operation)
			: ReadFile(pipe, buffer, bufferSize, nullptr, &operation);
		if (started)
		{
			const BOOL completed = GetOverlappedResult(pipe, &operation, &transferred, TRUE);
			CloseHandle(operationEvent);
			return completed == TRUE;
		}

		if (GetLastError() != ERROR_IO_PENDING)
		{
			CloseHandle(operationEvent);
			return false;
		}

		HANDLE waits[] = { stopEvent, operationEvent };
		const DWORD wait = WaitForMultipleObjects(2, waits, FALSE, ClientIoTimeoutMilliseconds);
		if (wait != WAIT_OBJECT_0 + 1)
		{
			CancelIoEx(pipe, &operation);
			GetOverlappedResult(pipe, &operation, &transferred, TRUE);
			CloseHandle(operationEvent);
			return false;
		}
		const BOOL completed = GetOverlappedResult(pipe, &operation, &transferred, FALSE);
		CloseHandle(operationEvent);
		return completed == TRUE;
	}

	// Waits for a client, service stop, or the next pending challenge expiration.
	bool ConnectClient(HANDLE pipe, HANDLE stopEvent, DWORD timeout)
	{
		HANDLE connectEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		if (!connectEvent)
			return false;
		OVERLAPPED operation = {};
		operation.hEvent = connectEvent;
		bool alreadyConnected = false;
		if (!ConnectNamedPipe(pipe, &operation))
		{
			const DWORD error = GetLastError();
			if (error == ERROR_PIPE_CONNECTED)
			{
				alreadyConnected = true;
				SetEvent(connectEvent);
			}
			else if (error != ERROR_IO_PENDING)
			{
				CloseHandle(connectEvent);
				return false;
			}
		}
		else
		{
			SetEvent(connectEvent);
		}

		HANDLE waits[] = { stopEvent, connectEvent };
		const DWORD wait = WaitForMultipleObjects(2, waits, FALSE, timeout);
		if (wait != WAIT_OBJECT_0 + 1)
		{
			CancelIoEx(pipe, &operation);
			DWORD transferred = 0;
			GetOverlappedResult(pipe, &operation, &transferred, TRUE);
			CloseHandle(connectEvent);
			if (wait == WAIT_TIMEOUT)
				SetLastError(ERROR_TIMEOUT);
			return false;
		}
		DWORD transferred = 0;
		const BOOL connected = alreadyConnected ||
			GetOverlappedResult(pipe, &operation, &transferred, FALSE);
		CloseHandle(connectEvent);
		return connected;
	}

	// Determines the pipe client identity under impersonation and reverts to SYSTEM before privileged work.
	bool GetClientIdentity(HANDLE pipe, HANDLE& process, ULONG& processId, DWORD& sessionId,
		std::wstring& sid, DWORD& integrityRid, DWORD& error)
	{
		process = nullptr;
		integrityRid = 0;
		if (!GetNamedPipeClientProcessId(pipe, &processId))
		{
			error = GetLastError();
			return false;
		}
		process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION |
			PROCESS_DUP_HANDLE | SYNCHRONIZE, FALSE, processId);
		if (!process)
		{
			error = GetLastError();
			return false;
		}
		DWORD processSessionId = 0;
		if (GetProcessId(process) != processId ||
			WaitForSingleObject(process, 0) != WAIT_TIMEOUT ||
			!ProcessIdToSessionId(processId, &processSessionId))
		{
			error = GetLastError();
			if (error == NO_ERROR)
				error = ERROR_PROCESS_ABORTED;
			CloseHandle(process);
			process = nullptr;
			return false;
		}
		if (!ImpersonateNamedPipeClient(pipe))
		{
			error = GetLastError();
			CloseHandle(process);
			process = nullptr;
			return false;
		}

		HANDLE token = nullptr;
		bool success = OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &token) == TRUE;
		if (!success)
			error = GetLastError();
		DWORD requiredSize = 0;
		if (success)
		{
			GetTokenInformation(token, TokenUser, nullptr, 0, &requiredSize);
			success = GetLastError() == ERROR_INSUFFICIENT_BUFFER && requiredSize != 0;
			if (!success)
				error = GetLastError();
		}
		std::vector<std::uint8_t> tokenUser(requiredSize);
		if (success)
		{
			success = GetTokenInformation(token, TokenUser, tokenUser.data(), requiredSize, &requiredSize) == TRUE;
			if (!success)
				error = GetLastError();
		}
		DWORD sessionSize = sizeof(sessionId);
		if (success)
		{
			success = GetTokenInformation(token, TokenSessionId, &sessionId, sessionSize, &sessionSize) == TRUE;
			if (!success)
				error = GetLastError();
			else if (sessionId != processSessionId)
			{
				success = false;
				error = ERROR_ACCESS_DENIED;
			}
		}
		DWORD integritySize = 0;
		if (success)
		{
			GetTokenInformation(token, TokenIntegrityLevel, nullptr, 0, &integritySize);
			success = GetLastError() == ERROR_INSUFFICIENT_BUFFER && integritySize != 0;
			if (!success)
				error = GetLastError();
		}
		std::vector<std::uint8_t> integrityBuffer(integritySize);
		if (success)
		{
			success = GetTokenInformation(token, TokenIntegrityLevel,
				integrityBuffer.data(), integritySize, &integritySize) == TRUE;
			if (!success)
				error = GetLastError();
		}
		if (success)
		{
			const auto* label = reinterpret_cast<const TOKEN_MANDATORY_LABEL*>(
				integrityBuffer.data());
			PSID integritySid = label->Label.Sid;
			const UCHAR subAuthorityCount = IsValidSid(integritySid)
				? *GetSidSubAuthorityCount(integritySid)
				: 0;
			if (subAuthorityCount == 0)
			{
				success = false;
				error = ERROR_INVALID_SID;
			}
			else
			{
				integrityRid = *GetSidSubAuthority(
					integritySid, subAuthorityCount - 1);
				if (integrityRid < SECURITY_MANDATORY_MEDIUM_RID)
				{
					success = false;
					error = ERROR_ACCESS_DENIED;
				}
			}
		}
		wchar_t* stringSid = nullptr;
		if (success)
		{
			const auto* user = reinterpret_cast<const TOKEN_USER*>(tokenUser.data());
			success = ConvertSidToStringSidW(user->User.Sid, &stringSid) == TRUE;
			if (!success)
				error = GetLastError();
		}
		if (success)
			sid = stringSid;
		if (stringSid)
			LocalFree(stringSid);
		if (token)
			CloseHandle(token);
		if (!RevertToSelf())
		{
			error = GetLastError();
			success = false;
		}

		HANDLE processToken = nullptr;
		if (success)
		{
			success = OpenProcessToken(process, TOKEN_QUERY, &processToken) == TRUE;
			if (!success)
				error = GetLastError();
		}
		DWORD processUserSize = 0;
		if (success)
		{
			GetTokenInformation(processToken, TokenUser, nullptr, 0, &processUserSize);
			success = GetLastError() == ERROR_INSUFFICIENT_BUFFER && processUserSize != 0;
			if (!success)
				error = GetLastError();
		}
		std::vector<std::uint8_t> processUser(processUserSize);
		if (success)
		{
			success = GetTokenInformation(processToken, TokenUser,
				processUser.data(), processUserSize, &processUserSize) == TRUE;
			if (!success)
				error = GetLastError();
		}
		DWORD processTokenSessionId = 0;
		DWORD processTokenSessionSize = sizeof(processTokenSessionId);
		if (success)
		{
			success = GetTokenInformation(processToken, TokenSessionId,
				&processTokenSessionId, processTokenSessionSize,
				&processTokenSessionSize) == TRUE;
			if (!success)
				error = GetLastError();
		}
		if (success)
		{
			const auto* pipeUser = reinterpret_cast<const TOKEN_USER*>(tokenUser.data());
			const auto* clientProcessUser = reinterpret_cast<const TOKEN_USER*>(
				processUser.data());
			if (!EqualSid(pipeUser->User.Sid, clientProcessUser->User.Sid) ||
				processTokenSessionId != processSessionId)
			{
				success = false;
				error = ERROR_ACCESS_DENIED;
			}
		}
		if (processToken)
			CloseHandle(processToken);
		if (!success)
		{
			CloseHandle(process);
			process = nullptr;
		}
		return success;
	}

	// Detects and records reused health requests by ID and nonce.
	bool IsReplay(const xucrun::broker::HealthRequest& request,
		std::deque<ReplayEntry>& replayEntries)
	{
		const bool found = std::any_of(replayEntries.begin(), replayEntries.end(),
			[&request](const ReplayEntry& entry) {
				return entry.requestId == request.requestId || entry.nonce == request.nonce;
			});
		if (found)
			return true;
		replayEntries.push_back({ request.requestId, request.nonce });
		if (replayEntries.size() > 256)
			replayEntries.pop_front();
		return false;
	}

	// Detects reuse of a request ID for all other pipe contracts.
	template<typename Request>
	bool IsReplay(const Request& request,
		const std::deque<ReplayEntry>& replayEntries)
	{
		return std::any_of(replayEntries.begin(), replayEntries.end(),
			[&request](const ReplayEntry& entry) {
				return entry.requestId == request.requestId;
			});
	}

	// Binds a confirmation to the SID, logon SID, session, and unchanged client image identity of the challenge.
	bool MatchesPendingChallengeClient(const PendingApprovalChallenge& pending,
		const std::wstring& clientSid, const std::wstring& clientLogonSid,
		DWORD clientProcessId, DWORD clientSessionId,
		const ClientImageIdentity& imageIdentity)
	{
		return pending.clientSid == clientSid &&
			pending.clientLogonSid == clientLogonSid &&
			pending.clientProcessId == clientProcessId &&
			pending.clientSessionId == clientSessionId &&
			_wcsicmp(pending.clientPath.c_str(), imageIdentity.path.c_str()) == 0 &&
			pending.clientSha256 == imageIdentity.sha256;
	}

		// Keep handlers in this translation unit so private authorization state cannot leak
		// through public headers. The fragments are grouped by request domain only.
#include "BrokerPipeControlHandlers.inl"
#include "BrokerPipeCredentialHandlers.inl"
#include "BrokerPipeApprovalHandlers.inl"
#include "BrokerPipeProcessHandlers.inl"
#include "BrokerPipeRequestDecoder.inl"

#include "BrokerPipeRequestDispatcher.inl"


	bool RunWritableTargetDirectorySelfTest()
	{
		wchar_t temporaryPath[MAX_PATH] = {};
		const DWORD temporaryPathLength = GetTempPathW(MAX_PATH, temporaryPath);
		if (temporaryPathLength == 0 || temporaryPathLength >= MAX_PATH)
			return false;

		const std::wstring directoryPath = std::wstring(temporaryPath) +
			L"xucRunBrokerWritableTargetTest-" +
			std::to_wstring(GetCurrentProcessId()) + L"-" +
			std::to_wstring(GetTickCount64());
		const std::wstring executablePath = directoryPath + L"\\target.exe";
		if (!CreateDirectoryW(directoryPath.c_str(), nullptr))
			return false;

		HANDLE executable = CreateFileW(executablePath.c_str(), GENERIC_WRITE,
			0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (executable == INVALID_HANDLE_VALUE)
		{
			RemoveDirectoryW(directoryPath.c_str());
			return false;
		}
		CloseHandle(executable);

		xucrun::broker::ApprovalFailureReason writableReason =
			xucrun::broker::ApprovalFailureReason::None;
		xucrun::broker::ApprovalFailureReason failureReason =
			xucrun::broker::ApprovalFailureReason::None;
		DWORD error = NO_ERROR;
		const bool accepted = ValidateElevatedLaunchPathsForChallenge(
			GetCurrentProcess(), executablePath, writableReason, failureReason, error);
		const bool passed = !accepted &&
			writableReason == xucrun::broker::ApprovalFailureReason::ElevatedTargetWritable &&
			failureReason ==
				xucrun::broker::ApprovalFailureReason::ElevatedTargetDirectoryWritable &&
			error == ERROR_ACCESS_DENIED;

		DeleteFileW(executablePath.c_str());
		RemoveDirectoryW(directoryPath.c_str());
		return passed;
	}

	// Verifies that an explicitly staged empty password remains committable while an
	// otherwise identical challenge without a completed submission is rejected.
	bool RunEmptyPasswordCommitHostSelfTest()
	{
		const DWORD processId = GetCurrentProcessId();
		DWORD sessionId = 0;
		if (!ProcessIdToSessionId(processId, &sessionId))
			return false;
		HANDLE process = OpenProcess(
			SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
		if (!process)
			return false;

		PendingCredentialCaptureChallenge pending;
		pending.clientSessionId = sessionId;
		pending.secureHostProcess = OwnHandle(process);
		pending.secureHostProcessId = processId;
		if (IsCredentialCommitHost(pending, processId, sessionId))
			return false;
		pending.credentialStaged = true;
		return pending.stagedPassword.empty() &&
			IsCredentialCommitHost(pending, processId, sessionId);
	}

	bool RunUnboundScriptPersistenceSelfTest()
	{
		PendingApprovalChallenge pending;
		pending.directScriptPath = L"C:\\Scripts\\mutable.ps1";
		xucrun::broker::ApprovalConfirmationRequest request;
		request.persistence = xucrun::broker::ApprovalPersistence::TenMinutes;
		request.programScope =
			xucrun::broker::ApprovalProgramScope::ExactInvocation;
		xucrun::approval::BrokerApprovalValue storedApproval;
		bool approvalStored = false;
		DWORD error = NO_ERROR;
		if (StoreApproval(pending, request, storedApproval, approvalStored, error) ||
			approvalStored || error != ERROR_ACCESS_DENIED)
		{
			return false;
		}
		request.programScope =
			xucrun::broker::ApprovalProgramScope::TargetProgram;
		if (!CanPersistApproval(pending, request))
			return false;
		request.programScope = xucrun::broker::ApprovalProgramScope::General;
		if (!CanPersistApproval(pending, request))
			return false;
		request.programScope =
			xucrun::broker::ApprovalProgramScope::ExactInvocation;
		pending.scriptPath = pending.directScriptPath;
		if (CanPersistApproval(pending, request))
			return false;
		pending.scriptSha256[0] = 1;
		if (!CanPersistApproval(pending, request))
			return false;
		pending.scriptPath = L"C:\\Scripts\\different.ps1";
		if (CanPersistApproval(pending, request))
			return false;
		pending = {};
		pending.executablePath = L"C:\\Tools\\application.exe";
		if (!CanPersistApproval(pending, request))
			return false;
		pending.executablePath =
			L"C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe";
		if (CanPersistApproval(pending, request))
			return false;
		pending.executablePath = L"C:\\Program Files\\PowerShell\\7\\pwsh.exe";
		if (CanPersistApproval(pending, request))
			return false;
		request.programScope =
			xucrun::broker::ApprovalProgramScope::TargetProgram;
		if (!CanPersistApproval(pending, request))
			return false;
		request.programScope =
			xucrun::broker::ApprovalProgramScope::ExactInvocation;
		request.persistence = xucrun::broker::ApprovalPersistence::Once;
		error = NO_ERROR;
		return StoreApproval(
			pending, request, storedApproval, approvalStored, error) &&
			!approvalStored && error == NO_ERROR;
	}

	bool RunDirectScriptLockSelfTest()
	{
		wchar_t temporaryPath[MAX_PATH + 1] = {};
		if (GetTempPathW(MAX_PATH, temporaryPath) == 0)
			return false;
		wchar_t scriptPath[MAX_PATH + 1] = {};
		if (GetTempFileNameW(temporaryPath, L"xrs", 0, scriptPath) == 0)
			return false;
		const std::uint8_t content[] = { 'W', 'r', 'i', 't', 'e', '-', 'O', 'u', 't', 'p', 'u', 't' };
		HANDLE script = CreateFileW(scriptPath, GENERIC_WRITE, 0, nullptr,
			TRUNCATE_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		DWORD written = 0;
		const bool contentWritten = script != INVALID_HANDLE_VALUE &&
			WriteFile(script, content, static_cast<DWORD>(std::size(content)),
				&written, nullptr) == TRUE &&
			written == std::size(content);
		if (script != INVALID_HANDLE_VALUE)
			CloseHandle(script);
		if (!contentWritten)
		{
			DeleteFileW(scriptPath);
			return false;
		}

		script = CreateFileW(scriptPath, GENERIC_READ,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
		std::wstring finalPath;
		std::vector<std::uint8_t> hash;
		const bool identityRead = script != INVALID_HANDLE_VALUE &&
			xucrun::files::ResolveFinalFilePathFromHandle(script, finalPath) &&
			xucrun::files::ComputeFileSHA256FromHandle(script, hash) &&
			hash.size() == 32;
		if (script != INVALID_HANDLE_VALUE)
			CloseHandle(script);
		if (!identityRead)
		{
			DeleteFileW(scriptPath);
			return false;
		}

		PendingApprovalChallenge pending;
		pending.directScriptPath = finalPath;
		std::copy(hash.begin(), hash.end(), pending.directScriptSha256.begin());
		DWORD error = NO_ERROR;
		bool passed = false;
		{
			LaunchObjectLocks locks;
			if (LockAndValidateDirectScript(pending, locks, error))
			{
				HANDLE writer = CreateFileW(finalPath.c_str(), GENERIC_WRITE,
					FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
					nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
				passed = writer == INVALID_HANDLE_VALUE &&
					GetLastError() == ERROR_SHARING_VIOLATION;
				if (writer != INVALID_HANDLE_VALUE)
					CloseHandle(writer);
			}
		}
		DeleteFileW(scriptPath);
		return passed;
	}

	bool RunApprovalHelperDirectScriptBindingSelfTest()
	{
		PendingApprovalChallenge pending;
		pending.directScriptPath = L"C:\\Scripts\\task.cmd";
		pending.directScriptSha256[0] = 0x11;
		xucrun::broker::ApprovalChallengeResult result;
		PopulateApprovalHelperDirectScriptBinding(pending, result);
		if (result.directScriptPath != pending.directScriptPath ||
			std::any_of(result.directScriptSha256.begin(),
				result.directScriptSha256.end(),
				[](std::uint8_t value) { return value != 0; }))
		{
			return false;
		}

		pending.scriptPath = pending.directScriptPath;
		pending.scriptSha256[0] = 0x22;
		PopulateApprovalHelperDirectScriptBinding(pending, result);
		return result.directScriptPath == pending.directScriptPath &&
			result.directScriptSha256 == pending.scriptSha256;
	}

	bool RunWindowsCommandTrustSelfTest()
	{
		wchar_t systemDirectory[MAX_PATH + 1] = {};
		const UINT length = GetSystemDirectoryW(
			systemDirectory, static_cast<UINT>(std::size(systemDirectory)));
		if (length == 0 || length >= std::size(systemDirectory))
			return false;
		const std::wstring commandPath =
			std::wstring(systemDirectory, length) + L"\\cmd.exe";
		ClientImageIdentity identity;
		DWORD error = NO_ERROR;
		return ReadFileImageIdentity(commandPath, identity, error, false) &&
			identity.signatureTrusted && identity.trustStatus == ERROR_SUCCESS &&
			identity.signerCertificateSha256.size() == 32;
	}

	bool RunBroadInterpreterApprovalConfirmationSelfTest()
	{
		PendingApprovalChallenge pending;
		pending.executablePath = L"C:\\Windows\\System32\\cmd.exe";
		xucrun::broker::ApprovalHelperDecisionRequest request;
		request.decision = xucrun::broker::ApprovalDecision::ApproveOnce;
		request.persistence = xucrun::broker::ApprovalPersistence::TenMinutes;
		request.programScope = xucrun::broker::ApprovalProgramScope::TargetProgram;
		if (!RequiresBroadInterpreterApprovalConfirmation(pending, request))
			return false;
		request.programScope = xucrun::broker::ApprovalProgramScope::General;
		if (!RequiresBroadInterpreterApprovalConfirmation(pending, request))
			return false;
		request.programScope =
			xucrun::broker::ApprovalProgramScope::ExactInvocation;
		if (RequiresBroadInterpreterApprovalConfirmation(pending, request))
			return false;
		request.programScope = xucrun::broker::ApprovalProgramScope::TargetProgram;
		request.persistence = xucrun::broker::ApprovalPersistence::Once;
		if (RequiresBroadInterpreterApprovalConfirmation(pending, request))
			return false;
		request.persistence = xucrun::broker::ApprovalPersistence::Forever;
		pending.executablePath = L"C:\\Windows\\System32\\notepad.exe";
		return !RequiresBroadInterpreterApprovalConfirmation(pending, request);
	}
}

// Tests that approval challenges cannot be transferred between clients, sessions, or image identities.
bool RunApprovalChallengeIdentitySelfTest()
{
	if (!RunEmptyPasswordCommitHostSelfTest() ||
		!RunDirectScriptArgumentSelfTest() ||
		!RunUnboundScriptPersistenceSelfTest() ||
		!RunDirectScriptLockSelfTest() ||
		!RunApprovalHelperDirectScriptBindingSelfTest() ||
		!RunWindowsCommandTrustSelfTest() ||
		!RunBroadInterpreterApprovalConfirmationSelfTest() ||
		!ShouldRejectPreElevatedCrossAccountLaunch(
			xucrun::broker::ElevationMode::RequireElevated, true, true) ||
		ShouldRejectPreElevatedCrossAccountLaunch(
			xucrun::broker::ElevationMode::PreserveToken, true, true) ||
		ShouldRejectPreElevatedCrossAccountLaunch(
			xucrun::broker::ElevationMode::RequireElevated, false, true) ||
		ShouldRejectPreElevatedCrossAccountLaunch(
			xucrun::broker::ElevationMode::RequireElevated, true, false))
		return false;

	xucrun::broker::ApprovalChallengeRequest inventoryRequest;
	inventoryRequest.elevationMode = xucrun::broker::ElevationMode::RequireElevated;
	inventoryRequest.visibility = xucrun::broker::LaunchVisibility::Normal;
	inventoryRequest.argumentTail =
		xucrun::broker::AdministrativeInventoryViewerArgumentTail;
	ClientImageIdentity inventoryClient;
	inventoryClient.path = L"C:\\Program Files\\xucRun\\xucRun.exe";
	inventoryClient.sha256 = { 1, 2, 3 };
	TargetImageIdentity inventoryTarget;
	inventoryTarget.path = L"c:\\program files\\xucRun\\xucRun.exe";
	inventoryTarget.sha256 = inventoryClient.sha256;
	if (!IsTrustedAdministrativeClientLaunch(
		inventoryRequest, inventoryClient, inventoryTarget))
	{
		return false;
	}
	inventoryRequest.visibility = xucrun::broker::LaunchVisibility::Hidden;
	inventoryRequest.argumentTail =
		std::wstring(xucrun::broker::AdministrativeInventoryConsoleArgumentPrefix) +
		L"0123456789abcdef0123456789ABCDEF";
	if (!IsTrustedAdministrativeClientLaunch(
		inventoryRequest, inventoryClient, inventoryTarget))
	{
		return false;
	}
	inventoryRequest.argumentTail.pop_back();
	if (IsTrustedAdministrativeClientLaunch(
		inventoryRequest, inventoryClient, inventoryTarget))
	{
		return false;
	}
	inventoryRequest.argumentTail += L"Z";
	if (IsTrustedAdministrativeClientLaunch(
		inventoryRequest, inventoryClient, inventoryTarget))
	{
		return false;
	}
	inventoryRequest.visibility = xucrun::broker::LaunchVisibility::Normal;
	inventoryRequest.argumentTail = L"/internal-admin-status-view extra";
	if (IsTrustedAdministrativeClientLaunch(
		inventoryRequest, inventoryClient, inventoryTarget))
	{
		return false;
	}
	inventoryRequest.argumentTail =
		xucrun::broker::AdministrativeBackupViewerArgumentTail;
	if (!IsTrustedAdministrativeClientLaunch(
		inventoryRequest, inventoryClient, inventoryTarget))
	{
		return false;
	}
	inventoryRequest.visibility = xucrun::broker::LaunchVisibility::Hidden;
	if (IsTrustedAdministrativeClientLaunch(
		inventoryRequest, inventoryClient, inventoryTarget))
	{
		return false;
	}
	inventoryRequest.visibility = xucrun::broker::LaunchVisibility::Normal;
	inventoryRequest.argumentTail =
		xucrun::broker::AdministrativeBackupRestoreViewerArgumentTail;
	if (!IsTrustedAdministrativeClientLaunch(
		inventoryRequest, inventoryClient, inventoryTarget))
	{
		return false;
	}
	inventoryRequest.visibility = xucrun::broker::LaunchVisibility::Hidden;
	if (IsTrustedAdministrativeClientLaunch(
		inventoryRequest, inventoryClient, inventoryTarget))
	{
		return false;
	}
	const std::wstring backupCommandPrefix =
		std::wstring(xucrun::broker::AdministrativeBackupCommandArgumentPrefix) +
		L"0123456789abcdef0123456789ABCDEF:";
	inventoryRequest.argumentTail = backupCommandPrefix + L"create";
	if (!IsTrustedAdministrativeClientLaunch(
		inventoryRequest, inventoryClient, inventoryTarget))
	{
		return false;
	}
	inventoryRequest.argumentTail = backupCommandPrefix + L"create:0054006500730074";
	if (!IsTrustedAdministrativeClientLaunch(
		inventoryRequest, inventoryClient, inventoryTarget))
	{
		return false;
	}
	inventoryRequest.visibility = xucrun::broker::LaunchVisibility::Normal;
	if (IsTrustedAdministrativeClientLaunch(
		inventoryRequest, inventoryClient, inventoryTarget))
	{
		return false;
	}
	inventoryRequest.visibility = xucrun::broker::LaunchVisibility::Hidden;
	for (const std::wstring invalidArgument : {
		backupCommandPrefix + L"create-extra",
		backupCommandPrefix + L"create:",
		backupCommandPrefix + L"create:000A",
		backupCommandPrefix + L"create:00XZ",
		backupCommandPrefix + L"restore:",
		backupCommandPrefix + L"restore:0123456789ABCDEF0123456789ABCDEZ",
		std::wstring(xucrun::broker::AdministrativeBackupCommandArgumentPrefix) +
			L"0123456789abcdef0123456789ABCDE:list" })
	{
		inventoryRequest.argumentTail = invalidArgument;
		if (IsTrustedAdministrativeClientLaunch(
			inventoryRequest, inventoryClient, inventoryTarget))
		{
			return false;
		}
	}
	const std::wstring languagePipeToken =
		L"0123456789abcdef0123456789ABCDEF";
	inventoryRequest.argumentTail =
		std::wstring(xucrun::broker::SetLanguageGermanArgumentTail) +
		L":" + languagePipeToken;
	if (!IsTrustedAdministrativeClientLaunch(
		inventoryRequest, inventoryClient, inventoryTarget))
	{
		return false;
	}
	inventoryRequest.argumentTail =
		std::wstring(xucrun::broker::SetLanguageEnglishArgumentTail) +
		L":" + languagePipeToken;
	if (!IsTrustedAdministrativeClientLaunch(
		inventoryRequest, inventoryClient, inventoryTarget))
	{
		return false;
	}
	inventoryRequest.argumentTail =
		std::wstring(xucrun::broker::SetLanguageAutomaticArgumentTail) +
		L":" + languagePipeToken;
	if (!IsTrustedAdministrativeClientLaunch(
		inventoryRequest, inventoryClient, inventoryTarget))
	{
		return false;
	}
	inventoryRequest.argumentTail =
		L"/internal-set-language:fr:" + languagePipeToken;
	if (IsTrustedAdministrativeClientLaunch(
		inventoryRequest, inventoryClient, inventoryTarget))
	{
		return false;
	}
	inventoryRequest.visibility = xucrun::broker::LaunchVisibility::Normal;
	inventoryRequest.argumentTail =
		std::wstring(xucrun::broker::SetLanguageGermanArgumentTail) +
		L":" + languagePipeToken;
	if (IsTrustedAdministrativeClientLaunch(
		inventoryRequest, inventoryClient, inventoryTarget))
	{
		return false;
	}
	inventoryRequest.visibility = xucrun::broker::LaunchVisibility::Hidden;
	inventoryRequest.argumentTail =
		xucrun::broker::AdministrativeInventoryViewerArgumentTail;
	inventoryTarget.sha256[0] = 9;
	if (IsTrustedAdministrativeClientLaunch(
		inventoryRequest, inventoryClient, inventoryTarget))
	{
		return false;
	}

	if (!RunAccountIdentitySelfTest() ||
		!RunWritableTargetDirectorySelfTest() ||
		!RequiresStandardTargetPresenceConfirmation(true, true, false, true, false,
		true, false, true, xucrun::broker::ElevationMode::PreserveToken) ||
		!RequiresStandardTargetPresenceConfirmation(true, true, true, true, false,
			true, false, false, xucrun::broker::ElevationMode::PreserveToken) ||
		RequiresStandardTargetPresenceConfirmation(false, true, false, true, false,
			true, false, true, xucrun::broker::ElevationMode::PreserveToken) ||
		RequiresStandardTargetPresenceConfirmation(true, true, false, true, true,
			true, false, true, xucrun::broker::ElevationMode::PreserveToken) ||
		RequiresStandardTargetPresenceConfirmation(true, true, false, true, false,
			true, false, false, xucrun::broker::ElevationMode::PreserveToken) ||
		RequiresStandardTargetPresenceConfirmation(true, true, false, false, false,
			true, false, true, xucrun::broker::ElevationMode::PreserveToken) ||
		RequiresStandardTargetPresenceConfirmation(true, true, false, true, false,
			true, true, true, xucrun::broker::ElevationMode::PreserveToken) ||
		RequiresStandardTargetPresenceConfirmation(true, true, false, true, false,
			true, false, true, xucrun::broker::ElevationMode::RequireElevated))
	{
		return false;
	}
	if (!RequiresClientAccountPresenceConfirmation(true, true, true, true, false) ||
		RequiresClientAccountPresenceConfirmation(false, true, true, true, false) ||
		RequiresClientAccountPresenceConfirmation(true, false, true, true, false) ||
		RequiresClientAccountPresenceConfirmation(true, true, false, true, false) ||
		RequiresClientAccountPresenceConfirmation(true, true, true, false, false) ||
		RequiresClientAccountPresenceConfirmation(true, true, true, true, true))
	{
		return false;
	}
	if (!ShouldUseSeparateApprovalAccountForHelper(
		true, false, true, false, true, false, true,
		xucrun::broker::ElevationMode::PreserveToken) ||
		ShouldUseSeparateApprovalAccountForHelper(
		true, true, true, false, true, false, true,
		xucrun::broker::ElevationMode::PreserveToken) ||
		ShouldUseSeparateApprovalAccountForHelper(
		true, false, true, true, true, false, true,
		xucrun::broker::ElevationMode::PreserveToken) ||
		ShouldUseSeparateApprovalAccountForHelper(
		true, false, true, false, true, true, true,
		xucrun::broker::ElevationMode::PreserveToken) ||
		ShouldUseSeparateApprovalAccountForHelper(
		true, false, true, false, true, false, false,
		xucrun::broker::ElevationMode::PreserveToken) ||
		ShouldUseSeparateApprovalAccountForHelper(
		true, false, true, false, true, false, true,
		xucrun::broker::ElevationMode::RequireElevated))
	{
		return false;
	}
	xucrun::broker::ApprovalHelperDecisionRequest presenceDecision;
	presenceDecision.decision = xucrun::broker::ApprovalDecision::ApproveOnce;
	presenceDecision.persistence = xucrun::broker::ApprovalPersistence::Once;
	presenceDecision.programScope =
		xucrun::broker::ApprovalProgramScope::TargetProgram;
	presenceDecision.accountScope =
		xucrun::broker::ApprovalAccountScope::CurrentAccount;
	presenceDecision.updateScope = xucrun::broker::ApprovalUpdateScope::None;
	if (!IsSingleUseApprovalDecision(presenceDecision))
		return false;
	presenceDecision.decision = xucrun::broker::ApprovalDecision::Deny;
	if (!IsSingleUseApprovalDecision(presenceDecision))
		return false;
	presenceDecision.persistence = xucrun::broker::ApprovalPersistence::Forever;
	if (IsSingleUseApprovalDecision(presenceDecision))
		return false;
	presenceDecision.decision = xucrun::broker::ApprovalDecision::ApproveOnce;
	presenceDecision.persistence = xucrun::broker::ApprovalPersistence::Once;
	presenceDecision.programScope = xucrun::broker::ApprovalProgramScope::General;
	if (IsSingleUseApprovalDecision(presenceDecision))
		return false;
	presenceDecision.programScope =
		xucrun::broker::ApprovalProgramScope::TargetProgram;
	presenceDecision.accountScope = xucrun::broker::ApprovalAccountScope::AllAccounts;
	if (IsSingleUseApprovalDecision(presenceDecision))
		return false;
	presenceDecision.accountScope =
		xucrun::broker::ApprovalAccountScope::CurrentAccount;
	presenceDecision.updateScope = xucrun::broker::ApprovalUpdateScope::CurrentAccount;
	if (IsSingleUseApprovalDecision(presenceDecision))
		return false;
	if (SanitizeLogValue(L"C:\\safe\r\npath\x7F.exe") !=
		L"C:\\safe??path?.exe")
	{
		return false;
	}

	if (!RequiresStoredApprovalPresenceConfirmation(true, true, false, true, true,
		xucrun::broker::ElevationMode::PreserveToken) ||
		RequiresStoredApprovalPresenceConfirmation(false, true, false, true, true,
			xucrun::broker::ElevationMode::PreserveToken) ||
		RequiresStoredApprovalPresenceConfirmation(true, true, true, true, true,
			xucrun::broker::ElevationMode::PreserveToken) ||
		RequiresStoredApprovalPresenceConfirmation(true, true, false, true, false,
			xucrun::broker::ElevationMode::PreserveToken) ||
		RequiresStoredApprovalPresenceConfirmation(true, true, false, true, true,
			xucrun::broker::ElevationMode::RequireElevated) ||
		RequiresStoredApprovalPresenceConfirmation(true, false, false, true, true,
			xucrun::broker::ElevationMode::PreserveToken) ||
		RequiresStoredApprovalPresenceConfirmation(true, true, false, false, true,
			xucrun::broker::ElevationMode::PreserveToken))
	{
		return false;
	}
	if (!ShouldUseClientTokenForApprovalHelper(
			xucrun::broker::ElevationMode::PreserveToken, true, false, true) ||
		ShouldUseClientTokenForApprovalHelper(
			xucrun::broker::ElevationMode::PreserveToken, true, true, true) ||
		ShouldUseClientTokenForApprovalHelper(
			xucrun::broker::ElevationMode::PreserveToken, true, false, false) ||
		ShouldUseClientTokenForApprovalHelper(
			xucrun::broker::ElevationMode::PreserveToken, false, false, true) ||
		ShouldUseClientTokenForApprovalHelper(
			xucrun::broker::ElevationMode::RequireElevated, true, false, true))
	{
		return false;
	}
	if (SelectApprovalHelperStartMode(
			xucrun::broker::ElevationMode::PreserveToken, true, false, false) !=
			xucrun::broker::ApprovalHelperStartMode::ClientRunAsRequired ||
		SelectApprovalHelperStartMode(
			xucrun::broker::ElevationMode::PreserveToken, true, false, true) !=
			xucrun::broker::ApprovalHelperStartMode::BrokerStarted ||
		SelectApprovalHelperStartMode(
			xucrun::broker::ElevationMode::PreserveToken, true, true, false) !=
			xucrun::broker::ApprovalHelperStartMode::BrokerStarted ||
		SelectApprovalHelperStartMode(
			xucrun::broker::ElevationMode::RequireElevated, true, false, false) !=
			xucrun::broker::ApprovalHelperStartMode::BrokerStarted)
	{
		return false;
	}

	PendingApprovalChallenge pending;
	pending.clientSid = L"S-1-5-21-1";
	pending.clientLogonSid = L"S-1-5-5-1-2";
	pending.clientProcessId = 1234;
	pending.clientSessionId = 1;
	pending.clientPath = L"C:\\Program Files\\cmdTools\\xucRun.exe";
	pending.clientSha256 = { 1, 2, 3 };
	ClientImageIdentity identity;
	identity.path = pending.clientPath;
	identity.sha256 = pending.clientSha256;
	if (!MatchesPendingChallengeClient(pending, pending.clientSid,
		pending.clientLogonSid, pending.clientProcessId,
		pending.clientSessionId, identity))
	{
		return false;
	}
	const ULONGLONG now = 1000;
	std::deque<PendingApprovalChallenge> approvalChallenges;
	std::deque<PendingCredentialCaptureChallenge> credentialChallenges;
	if (GetPendingChallengeWaitMilliseconds(
		now, approvalChallenges, credentialChallenges) != INFINITE)
	{
		return false;
	}
	PendingApprovalChallenge approvalWait;
	approvalWait.expiresAt = now + 250;
	approvalChallenges.push_back(approvalWait);
	PendingCredentialCaptureChallenge credentialWait;
	credentialWait.expiresAt = now + 100;
	HANDLE currentProcess = nullptr;
	if (!DuplicateHandle(GetCurrentProcess(), GetCurrentProcess(), GetCurrentProcess(),
		&currentProcess, SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
		FALSE, 0))
	{
		return false;
	}
	DWORD currentSessionId = 0;
	if (!ProcessIdToSessionId(GetCurrentProcessId(), &currentSessionId))
	{
		CloseHandle(currentProcess);
		return false;
	}
	credentialWait.clientSessionId = currentSessionId;
	credentialWait.secureHostProcessId = GetCurrentProcessId();
	credentialWait.secureHostProcess = OwnHandle(currentProcess);
	if (IsCredentialCommitHost(credentialWait,
		GetCurrentProcessId(), currentSessionId) ||
		!credentialWait.stagedPassword.assign(L"test", 4))
	{
		return false;
	}
	credentialWait.credentialStaged = true;
	if (!IsCredentialCommitHost(credentialWait,
			GetCurrentProcessId(), currentSessionId) ||
		IsCredentialCommitHost(credentialWait,
			GetCurrentProcessId() + 1, currentSessionId) ||
		IsCredentialCommitHost(credentialWait,
			GetCurrentProcessId(), currentSessionId + 1))
	{
		return false;
	}
	credentialChallenges.push_back(std::move(credentialWait));
	if (GetPendingChallengeWaitMilliseconds(
		now, approvalChallenges, credentialChallenges) != 100)
	{
		return false;
	}
	credentialChallenges.front().expiresAt = now;
	if (GetPendingChallengeWaitMilliseconds(
		now, approvalChallenges, credentialChallenges) != 0)
	{
		return false;
	}
	HANDLE cancellationEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	HANDLE pendingCancellationEvent = nullptr;
	if (!cancellationEvent || !DuplicateHandle(GetCurrentProcess(), cancellationEvent,
		GetCurrentProcess(), &pendingCancellationEvent, 0, FALSE,
		DUPLICATE_SAME_ACCESS))
	{
		if (cancellationEvent) CloseHandle(cancellationEvent);
		return false;
	}
	credentialChallenges.front().secureHostCancellationSignal =
		OwnHandle(pendingCancellationEvent);
	PrunePendingCredentialCaptureChallenges(credentialChallenges);
	const bool cancellationSignalled = credentialChallenges.empty() &&
		WaitForSingleObject(cancellationEvent, 0) == WAIT_OBJECT_0;
	CloseHandle(cancellationEvent);
	if (!cancellationSignalled)
		return false;
	return !MatchesPendingChallengeClient(pending, pending.clientSid,
		pending.clientLogonSid, pending.clientProcessId + 1,
		pending.clientSessionId, identity) &&
		!MatchesPendingChallengeClient(pending, pending.clientSid,
			L"S-1-5-5-3-4", pending.clientProcessId,
			pending.clientSessionId, identity);
}

// Tests session filtering, sorting, data assignment, and limits without real processes.
bool RunTrackedProcessListSelectionSelfTest()
{
	const xucrun::broker::RequestNonce olderId = { 1 };
	const xucrun::broker::RequestNonce tiedLowerId = { 2 };
	const xucrun::broker::RequestNonce tiedHigherId = { 3 };
	const xucrun::broker::RequestNonce foreignId = { 4 };
	const std::vector<TrackedProcessListCandidate> mixed = {
		{ 7, 10, { olderId, 10, L"older.exe" } },
		{ 7, 20, { tiedLowerId, 20, L"tied-lower.exe" } },
		{ 8, 30, { foreignId, 30, L"foreign.exe" } },
		{ 7, 20, { tiedHigherId, 21, L"tied-higher.exe" } }
	};
	const auto selected = BuildTrackedProcessListEntries(mixed, 7);
	if (selected.size() != 3 ||
		selected[0].processId != 21 || selected[0].trackingId != tiedHigherId ||
		selected[0].processBaseName != L"tied-higher.exe" ||
		selected[1].processId != 20 || selected[1].trackingId != tiedLowerId ||
		selected[1].processBaseName != L"tied-lower.exe" ||
		selected[2].processId != 10 || selected[2].trackingId != olderId ||
		selected[2].processBaseName != L"older.exe")
	{
		return false;
	}

	std::vector<TrackedProcessListCandidate> many;
	for (std::uint32_t index = 0;
		index < xucrun::broker::MaximumTrackedProcessEntries + 2; ++index)
	{
		xucrun::broker::RequestNonce trackingId = {};
		trackingId[0] = static_cast<std::uint8_t>(index);
		many.push_back({ 7, index, { trackingId, 1000 + index,
			L"process-" + std::to_wstring(index) } });
	}
	const auto limited = BuildTrackedProcessListEntries(many, 7);
	return limited.size() == xucrun::broker::MaximumTrackedProcessEntries &&
		limited.front().processId == 1129 &&
		limited.front().trackingId[0] == 129 &&
		limited.front().processBaseName == L"process-129" &&
		limited.back().processId == 1002 &&
		limited.back().trackingId[0] == 2 &&
		limited.back().processBaseName == L"process-2";
}

// Serves one hardened pipe connection at a time and authorizes each contract before privileged actions.
DWORD RunBrokerPipeServer(HANDLE stopEvent,
	const ClientImageIdentity& brokerImageIdentity,
	BrokerLogCallback log)
{
	PSECURITY_DESCRIPTOR descriptor = nullptr;
	if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
		PipeSecurity, SDDL_REVISION_1, &descriptor, nullptr))
	{
		return GetLastError();
	}
	SECURITY_ATTRIBUTES security = { sizeof(security), descriptor, FALSE };
	std::deque<ReplayEntry> replayEntries;
	std::deque<PendingApprovalChallenge> pendingApprovalChallenges;
	std::deque<PendingCredentialCaptureChallenge> pendingCredentialCaptureChallenges;
	std::vector<TrackedElevatedProcess> trackedElevatedProcesses;
	std::vector<TrackedElevatedProcessJob> trackedElevatedProcessJobs;
	std::vector<RegisteredStatusIcon> registeredStatusIcons;
	DWORD result = NO_ERROR;
	HANDLE pipe = CreateNamedPipeW(
		xucrun::broker::BrokerPipeName,
		PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
		PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
		4, static_cast<DWORD>((std::max)(xucrun::broker::MaximumLaunchResultSize,
			(std::max)(xucrun::broker::MaximumApprovalChallengeResultSize,
				(std::max)(xucrun::broker::MaximumCredentialListResultSize,
					(std::max)(xucrun::broker::MaximumApprovalListResultSize,
						(std::max)(xucrun::broker::MaximumSecurityAuditResultSize,
							(std::max)(xucrun::broker::MaximumTrackedProcessListResultSize,
								(std::max)(xucrun::broker::MaximumStatusIconListResultSize,
									xucrun::broker::MaximumAdministrativeInventoryResultSize)))))))),
		static_cast<DWORD>((std::max)(xucrun::broker::MaximumLaunchRequestSize,
			(std::max)(xucrun::broker::MaximumApprovalChallengeRequestSize,
				xucrun::broker::MaximumCredentialCaptureSubmitRequestSize))),
		3000, &security);
	if (pipe == INVALID_HANDLE_VALUE)
	{
		result = GetLastError();
		LocalFree(descriptor);
		return result;
	}

	while (WaitForSingleObject(stopEvent, 0) == WAIT_TIMEOUT)
	{
		PrunePendingApprovalChallenges(pendingApprovalChallenges);
		PrunePendingCredentialCaptureChallenges(pendingCredentialCaptureChallenges);
		const DWORD challengeWait = GetPendingChallengeWaitMilliseconds(
			GetTickCount64(), pendingApprovalChallenges,
			pendingCredentialCaptureChallenges);
		if (!ConnectClient(pipe, stopEvent, challengeWait))
		{
			if (WaitForSingleObject(stopEvent, 0) == WAIT_OBJECT_0)
				break;
			DisconnectNamedPipe(pipe);
			continue;
		}

		ULONG clientProcessId = 0;
		HANDLE clientProcess = nullptr;
		DWORD clientSessionId = 0;
		DWORD clientIntegrityRid = 0;
		DWORD identityError = NO_ERROR;
		std::wstring clientSid;
		if (!GetClientIdentity(pipe, clientProcess, clientProcessId, clientSessionId, clientSid,
			clientIntegrityRid, identityError))
		{
			if (log)
				log(EVENTLOG_WARNING_TYPE,
					L"Pipe client identity could not be determined. Error " +
					std::to_wstring(identityError));
			DisconnectNamedPipe(pipe);
			continue;
		}
		ClientImageIdentity imageIdentity;
		DWORD imageIdentityError = NO_ERROR;
		DWORD authorizationError = NO_ERROR;
		const bool imageIdentityRead = ReadClientImageIdentity(
			clientProcess, imageIdentity, imageIdentityError);
#ifndef _DEBUG
		const bool brokerSignerMatches = imageIdentityRead &&
			HaveMatchingTrustedSigners(imageIdentity, brokerImageIdentity);
		if (imageIdentityRead && !brokerSignerMatches)
			authorizationError = ERROR_ACCESS_DENIED;
#else
		const bool brokerSignerMatches = true;
#endif
		const bool coreClientAuthorized = imageIdentityRead &&
			IsCoreClientAuthorized(imageIdentity, authorizationError);
		const bool trayClientAuthorized = imageIdentityRead &&
			IsTrayCompanionAuthorized(imageIdentity, authorizationError);
		const bool sensitiveClientAuthorized =
			coreClientAuthorized || trayClientAuthorized;
		const bool approvalHelperAuthorized = imageIdentityRead && brokerSignerMatches &&
			IsApprovalHelperAuthorized(
				imageIdentity, clientIntegrityRid, authorizationError);
		const bool boundCredentialCommitHost = imageIdentityRead &&
			IsBoundCredentialCommitHost(pendingCredentialCaptureChallenges,
				clientProcessId, clientSessionId)
#ifndef _DEBUG
			&& imageIdentity.path == brokerImageIdentity.path &&
			imageIdentity.sha256 == brokerImageIdentity.sha256
#endif
			;
		if (!imageIdentityRead || !brokerSignerMatches ||
			(!sensitiveClientAuthorized && !approvalHelperAuthorized &&
				!boundCredentialCommitHost &&
				!IsDebugDiagnosticClientAuthorized(imageIdentity, clientIntegrityRid)))
		{
			if (log)
				log(EVENTLOG_WARNING_TYPE,
					L"Unauthorized pipe client discarded before reading request. PID " +
					std::to_wstring(clientProcessId) + L", error " +
					std::to_wstring(imageIdentityError != NO_ERROR
						? imageIdentityError : authorizationError));
			CloseHandle(clientProcess);
			DisconnectNamedPipe(pipe);
			continue;
		}
		std::wstring clientLogonSid;
		if (!GetProcessLogonSid(clientProcess, clientLogonSid, identityError))
		{
			if (log)
				log(EVENTLOG_WARNING_TYPE,
					L"Client logon SID could not be determined. Error " +
					std::to_wstring(identityError));
			CloseHandle(clientProcess);
			DisconnectNamedPipe(pipe);
			continue;
		}

		std::vector<std::uint8_t> requestFrame((std::max)(
			(std::max)(xucrun::broker::MaximumLaunchRequestSize,
				xucrun::broker::MaximumApprovalChallengeRequestSize),
			xucrun::broker::MaximumApprovalResetRequestSize));
		DWORD transferred = 0;
		if (!RunOverlappedIo(pipe, stopEvent, false, requestFrame.data(),
			static_cast<DWORD>(requestFrame.size()), transferred))
		{
			if (log)
				log(EVENTLOG_WARNING_TYPE, L"Pipe request could not be read.");
			CloseHandle(clientProcess);
			DisconnectNamedPipe(pipe);
			continue;
		}
		requestFrame.resize(transferred);

		DecodedBrokerRequest decoded;
		const bool isCredentialCaptureCommit =
			xucrun::broker::DeserializeCredentialCaptureCommitRequest(
				requestFrame, decoded.credentialCaptureCommit);
		if (isCredentialCaptureCommit)
		{
			decoded.kind = BrokerRequestKind::CredentialCaptureCommit;
		}
		else if (boundCredentialCommitHost)
		{
			// A process bound as a commit host has one purpose and must not reach
			// any other decoder or handler, even if it sends a valid contract frame.
			if (log)
				log(EVENTLOG_WARNING_TYPE,
					L"Bound credential host sent a non-commit pipe request.");
			CloseHandle(clientProcess);
			DisconnectNamedPipe(pipe);
			continue;
		}

		const bool decodedSuccessfully = isCredentialCaptureCommit ||
			DecodeBrokerRequest(requestFrame, decoded);
		if (xucrun::broker::IsCredentialSaveRequestFrame(requestFrame) ||
			xucrun::broker::IsCredentialCaptureSubmitRequestFrame(requestFrame))
		{
			SecureZeroMemory(requestFrame.data(), requestFrame.size());
		}

		const bool replayed = decodedSuccessfully &&
			IsBrokerRequestReplay(decoded, replayEntries);
		if (!decodedSuccessfully || replayed)
		{
			if (log)
			{
				if (replayed && decoded.kind == BrokerRequestKind::CredentialCaptureCommit)
				{
					log(EVENTLOG_WARNING_TYPE,
						L"Replayed credential capture commit request discarded.");
				}
				else
				{
					log(EVENTLOG_WARNING_TYPE,
						L"Invalid or replayed pipe request discarded.");
				}
			}
			CloseHandle(clientProcess);
			DisconnectNamedPipe(pipe);
			continue;
		}
		if (sensitiveClientAuthorized &&
			!IsSensitiveClientRequestAllowed(decoded.kind,
				coreClientAuthorized, trayClientAuthorized))
		{
			if (log)
				log(EVENTLOG_WARNING_TYPE,
					L"Sensitive client request rejected by policy. Request " +
					std::wstring(GetBrokerRequestKindName(decoded.kind)) +
					L", component xucRunTray.exe, PID " +
					std::to_wstring(clientProcessId) + L".");
			xucrun::broker::BrokerPolicyRejection rejection;
			rejection.requestId = GetBrokerRequestId(decoded);
			std::vector<std::uint8_t> rejectionFrame =
				xucrun::broker::SerializeBrokerPolicyRejection(rejection);
			if (!rejectionFrame.empty())
			{
				RunOverlappedIo(pipe, stopEvent, true,
					rejectionFrame.data(),
					static_cast<DWORD>(rejectionFrame.size()), transferred);
			}
			CloseHandle(clientProcess);
			DisconnectNamedPipe(pipe);
			continue;
		}
		RecordBrokerRequest(decoded, replayEntries);

		if (log && UsesGeneralClientAuditLog(decoded.kind))
		{
			log(EVENTLOG_INFORMATION_TYPE,
				L"Client image: " + SanitizeLogValue(imageIdentity.path) + L", SHA-256 " +
				xucrun::targets::BytesToUpperHex(imageIdentity.sha256) +
				L", signature " + (imageIdentity.signatureTrusted
					? L"trusted"
					: L"not trusted (status " +
						std::to_wstring(imageIdentity.trustStatus) + L")"));
		}

		const bool stopAccepted = DispatchBrokerRequest(decoded,
			pipe, stopEvent, clientProcess, clientProcessId, clientSessionId,
			clientIntegrityRid, imageIdentity, clientSid, clientLogonSid,
			pendingApprovalChallenges, pendingCredentialCaptureChallenges,
			trackedElevatedProcesses, trackedElevatedProcessJobs,
			registeredStatusIcons, log, transferred);
		CloseHandle(clientProcess);
		DisconnectNamedPipe(pipe);
		if (stopAccepted)
		{
			if (log)
				log(EVENTLOG_INFORMATION_TYPE,
					L"Authenticated request to stop the service accepted.");
			SetEvent(stopEvent);
		}
	}

	for (const TrackedElevatedProcess& process : trackedElevatedProcesses)
		CloseHandle(process.process);
	for (const TrackedElevatedProcessJob& job : trackedElevatedProcessJobs)
		CloseHandle(job.job);
	for (const RegisteredStatusIcon& icon : registeredStatusIcons)
		CloseHandle(icon.process);
	CloseHandle(pipe);
	LocalFree(descriptor);
	return result;
}
