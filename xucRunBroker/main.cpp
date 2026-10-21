// Purpose: Runs xucRunBroker as a hardened LocalSystem service and diagnostic program.
// Initializes service state, security checks, and the broker pipe server.
#include "ApprovalStore.h"
#include "BrokerBackupStore.h"
#include "BrokerTemporaryBatch.h"
#include "BrokerTargetNaming.h"
#include "BrokerContracts.h"
#include "BrokerPipeClient.h"
#include "BrokerPipeServer.h"
#include "BrokerRequestPolicy.h"
#include "CredentialMetadataStore.h"
#include "CredentialStore.h"
#include "CredentialValidator.h"
#include "SecureDesktopHost.h"
#include "SessionTestLauncher.h"
#include "SecurityAclAudit.h"
#include "SensitiveClientAuthorization.h"

#include <Windows.h>
#include <sddl.h>
#include <werapi.h>

#include <atomic>
#include <iostream>
#include <string>
#include <vector>

namespace
{
	constexpr wchar_t ServiceDisplayName[] = L"xucRun Privileged Broker";
	constexpr DWORD PurgeAndStopControl = 128;

	SERVICE_STATUS_HANDLE serviceStatusHandle = nullptr;
	HANDLE stopEvent = nullptr;
	std::atomic_bool purgeStoredProductData = false;
	std::vector<std::wstring> processHardeningWarnings;

	bool IsUninstallPurgeInvocation(DWORD argumentCount, wchar_t** arguments)
	{
		return argumentCount == 2 && arguments && arguments[1] &&
			_wcsicmp(arguments[1], L"uninstall-purge") == 0;
	}

	bool IsUninstallPurgeServiceAccessRestricted(DWORD& error)
	{
		error = NO_ERROR;
		SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
		SC_HANDLE service = manager ? OpenServiceW(manager,
			xucrun::broker::BrokerServiceName, READ_CONTROL) : nullptr;
		if (!manager || !service)
		{
			error = GetLastError();
			if (service) CloseServiceHandle(service);
			if (manager) CloseServiceHandle(manager);
			return false;
		}
		DWORD size = 0;
		QueryServiceObjectSecurity(service, DACL_SECURITY_INFORMATION,
			nullptr, 0, &size);
		if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || size == 0)
		{
			error = GetLastError();
			CloseServiceHandle(service);
			CloseServiceHandle(manager);
			return false;
		}
		std::vector<std::uint8_t> buffer(size);
		if (!QueryServiceObjectSecurity(service, DACL_SECURITY_INFORMATION,
			reinterpret_cast<PSECURITY_DESCRIPTOR>(buffer.data()), size, &size))
		{
			error = GetLastError();
			CloseServiceHandle(service);
			CloseServiceHandle(manager);
			return false;
		}
		CloseServiceHandle(service);
		CloseServiceHandle(manager);
		BOOL present = FALSE;
		BOOL defaulted = FALSE;
		PACL dacl = nullptr;
		if (!GetSecurityDescriptorDacl(
			reinterpret_cast<PSECURITY_DESCRIPTOR>(buffer.data()),
			&present, &dacl, &defaulted) || !present || !dacl)
		{
			error = ERROR_INVALID_SECURITY_DESCR;
			return false;
		}
		for (DWORD index = 0; index < dacl->AceCount; index++)
		{
			void* rawAce = nullptr;
			if (!GetAce(dacl, index, &rawAce))
			{
				error = GetLastError();
				return false;
			}
			const auto* header = static_cast<const ACE_HEADER*>(rawAce);
			if (header->AceType != ACCESS_ALLOWED_ACE_TYPE)
				continue;
			const auto* ace = static_cast<const ACCESS_ALLOWED_ACE*>(rawAce);
			PSID sid = const_cast<DWORD*>(&ace->SidStart);
			const bool trusted = IsWellKnownSid(sid, WinLocalSystemSid) ||
				IsWellKnownSid(sid, WinBuiltinAdministratorsSid);
			if (!trusted && (ace->Mask &
				(SERVICE_START | GENERIC_EXECUTE | GENERIC_ALL)) != 0)
			{
				error = ERROR_ACCESS_DENIED;
				return false;
			}
		}
		return true;
	}

	bool ParseCredentialChallengeArgument(const wchar_t* value,
		xucrun::broker::RequestNonce& challenge)
	{
		constexpr wchar_t Prefix[] = L"/credentialchallenge:";
		if (!value || _wcsnicmp(value, Prefix, std::size(Prefix) - 1) != 0)
			return false;
		const wchar_t* hex = value + std::size(Prefix) - 1;
		if (wcslen(hex) != challenge.size() * 2)
			return false;
		for (std::size_t index = 0; index < challenge.size(); index++)
		{
			wchar_t digits[3] = { hex[index * 2], hex[index * 2 + 1], L'\0' };
			wchar_t* end = nullptr;
			const unsigned long byte = wcstoul(digits, &end, 16);
			if (end != digits + 2 || byte > 0xFF)
				return false;
			challenge[index] = static_cast<std::uint8_t>(byte);
		}
		return true;
	}
	bool verboseBrokerLoggingEnabled = false;

	// Enables process mitigations before any further broker work and collects failed hardening measures.
	std::vector<std::wstring> ApplyEarlyProcessHardening()
	{
		std::vector<std::wstring> warnings;
		auto recordFailure = [&warnings](const wchar_t* mitigation, DWORD error)
		{
			warnings.push_back(std::wstring(mitigation) + L" could not be enabled (error " +
				std::to_wstring(error) + L")");
		};

		SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX |
			SEM_NOOPENFILEERRORBOX);
		const HRESULT werStatus = WerSetFlags(WER_FAULT_REPORTING_FLAG_NOHEAP);
		if (FAILED(werStatus))
			recordFailure(L"WER hardening without heap dumps", static_cast<DWORD>(werStatus));
		if (!SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32))
			recordFailure(L"Restricted DLL search", GetLastError());

		PROCESS_MITIGATION_DYNAMIC_CODE_POLICY dynamicCodePolicy = {};
		dynamicCodePolicy.ProhibitDynamicCode = 1;
		if (!SetProcessMitigationPolicy(
			ProcessDynamicCodePolicy, &dynamicCodePolicy, sizeof(dynamicCodePolicy)))
		{
			recordFailure(L"Dynamic code prohibition", GetLastError());
		}

		PROCESS_MITIGATION_EXTENSION_POINT_DISABLE_POLICY extensionPointPolicy = {};
		extensionPointPolicy.DisableExtensionPoints = 1;
		if (!SetProcessMitigationPolicy(ProcessExtensionPointDisablePolicy,
			&extensionPointPolicy, sizeof(extensionPointPolicy)))
		{
			recordFailure(L"Legacy extension point disablement", GetLastError());
		}

		PROCESS_MITIGATION_IMAGE_LOAD_POLICY imageLoadPolicy = {};
		imageLoadPolicy.NoRemoteImages = 1;
		imageLoadPolicy.NoLowMandatoryLabelImages = 1;
		imageLoadPolicy.PreferSystem32Images = 1;
		if (!SetProcessMitigationPolicy(
			ProcessImageLoadPolicy, &imageLoadPolicy, sizeof(imageLoadPolicy)))
		{
			recordFailure(L"Restricted image loading", GetLastError());
		}
		return warnings;
	}

	// Verifies that all required process mitigations are effective in the running broker.
	bool RunProcessMitigationSelfTest()
	{
		PROCESS_MITIGATION_DYNAMIC_CODE_POLICY dynamicCodePolicy = {};
		PROCESS_MITIGATION_EXTENSION_POINT_DISABLE_POLICY extensionPointPolicy = {};
		PROCESS_MITIGATION_IMAGE_LOAD_POLICY imageLoadPolicy = {};
		DWORD werFlags = 0;
		return processHardeningWarnings.empty() &&
			GetProcessMitigationPolicy(GetCurrentProcess(), ProcessDynamicCodePolicy,
				&dynamicCodePolicy, sizeof(dynamicCodePolicy)) &&
			GetProcessMitigationPolicy(GetCurrentProcess(), ProcessExtensionPointDisablePolicy,
				&extensionPointPolicy, sizeof(extensionPointPolicy)) &&
			GetProcessMitigationPolicy(GetCurrentProcess(), ProcessImageLoadPolicy,
				&imageLoadPolicy, sizeof(imageLoadPolicy)) &&
			SUCCEEDED(WerGetFlags(GetCurrentProcess(), &werFlags)) &&
			dynamicCodePolicy.ProhibitDynamicCode &&
			extensionPointPolicy.DisableExtensionPoints &&
			imageLoadPolicy.NoRemoteImages &&
			imageLoadPolicy.NoLowMandatoryLabelImages &&
			imageLoadPolicy.PreferSystem32Images &&
			(werFlags & WER_FAULT_REPORTING_FLAG_NOHEAP) != 0;
	}

	// Writes a technical English message to debug output and the Windows event log.
	void LogEvent(WORD eventType, const std::wstring& message)
	{
		OutputDebugStringW((L"xucRunBroker: " + message + L"\n").c_str());
		HANDLE source = RegisterEventSourceW(
			nullptr, xucrun::broker::BrokerServiceName);
		if (!source)
			return;
		const wchar_t* strings[] = { message.c_str() };
		ReportEventW(source, eventType, 0, 1, nullptr, 1, 0, strings, nullptr);
		DeregisterEventSource(source);
	}

	// Enables additional informational events through the machine-wide xucRun configuration.
	bool IsVerboseBrokerLoggingEnabled()
	{
		wchar_t programFiles[MAX_PATH] = {};
		const DWORD pathLength = GetEnvironmentVariableW(
			L"ProgramFiles", programFiles, ARRAYSIZE(programFiles));
		if (pathLength == 0 || pathLength >= ARRAYSIZE(programFiles))
			return false;

		const std::wstring configPath = std::wstring(programFiles) +
			L"\\xucRun\\xucRun.ini";
		wchar_t enabled[16] = {};
		GetPrivateProfileStringW(L"Logging", L"BrokerInformationEvents", L"false",
			enabled, ARRAYSIZE(enabled), configPath.c_str());
		return _wcsicmp(enabled, L"true") == 0;
	}

	// Filters optional informational messages unless the INI configuration enables them.
	void LogBrokerEvent(WORD eventType, const std::wstring& message)
	{
		if (eventType != EVENTLOG_INFORMATION_TYPE || verboseBrokerLoggingEnabled)
			LogEvent(eventType, message);
	}

	// Reports the current service state and progress data to the Service Control Manager.
	void SetServiceState(DWORD state, DWORD windowsError = NO_ERROR, DWORD waitHint = 0)
	{
		static DWORD checkpoint = 1;
		SERVICE_STATUS status = {};
		status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
		status.dwCurrentState = state;
		status.dwControlsAccepted = state == SERVICE_RUNNING
			? SERVICE_ACCEPT_STOP
			: 0;
		status.dwWin32ExitCode = windowsError;
		status.dwCheckPoint = state == SERVICE_START_PENDING || state == SERVICE_STOP_PENDING
			? checkpoint++
			: 0;
		status.dwWaitHint = waitHint;
		SetServiceStatus(serviceStatusHandle, &status);
	}

	// Handles service stop requests and signals controlled cancellation to the pipe server.
	DWORD WINAPI ServiceControlHandler(DWORD control, DWORD, void*, void*)
	{
		if ((control == SERVICE_CONTROL_STOP || control == PurgeAndStopControl) && stopEvent)
		{
			if (control == PurgeAndStopControl)
				purgeStoredProductData = true;
			SetServiceState(SERVICE_STOP_PENDING, NO_ERROR, 3000);
			SetEvent(stopEvent);
		}
		return NO_ERROR;
	}

	// Removes all machine-bound credential, approval, and authenticity marker targets of xucRun.
	DWORD PurgeBrokerCredentials()
	{
		for (const xucrun::broker_targets::TargetKind kind : {
			xucrun::broker_targets::TargetKind::Credential,
			xucrun::broker_targets::TargetKind::Approval,
			xucrun::broker_targets::TargetKind::AuthenticityMarker })
		{
			std::wstring prefix;
			DWORD error = NO_ERROR;
			if (!xucrun::broker_targets::GetMachineScopedPrefix(kind, prefix, error) ||
				!xucrun::broker_store::DeleteAllWithPrefix(prefix, error))
			{
				return error == NO_ERROR ? ERROR_GEN_FAILURE : error;
			}
		}
		DWORD metadataError = NO_ERROR;
		if (!DeleteAllCredentialMetadata(metadataError))
			return metadataError == NO_ERROR ? ERROR_GEN_FAILURE : metadataError;
		return NO_ERROR;
	}

	// Initializes the hardened broker service, audits ACLs, and runs the pipe server until stopped.
	void WINAPI ServiceMain(DWORD argumentCount, wchar_t** arguments)
	{
		serviceStatusHandle = RegisterServiceCtrlHandlerExW(
			xucrun::broker::BrokerServiceName, ServiceControlHandler, nullptr);
		if (!serviceStatusHandle)
			return;

		verboseBrokerLoggingEnabled = IsVerboseBrokerLoggingEnabled();
		SetServiceState(SERVICE_START_PENDING, NO_ERROR, 3000);
		for (const std::wstring& warning : processHardeningWarnings)
			LogEvent(EVENTLOG_WARNING_TYPE, warning);
		const std::vector<SecurityAclFinding> aclFindings =
			AuditBrokerSecurityAcls(false, nullptr, true);
		if (aclFindings.empty())
		{
			LogEvent(EVENTLOG_INFORMATION_TYPE,
				L"Startup security audit for broker, client, installed Tray resources, "
				L"configuration, and broker store succeeded.");
		}
		else
		{
			for (const SecurityAclFinding& finding : aclFindings)
			{
				LogEvent(EVENTLOG_WARNING_TYPE,
					L"ACL security warning\nObject: " + finding.objectName +
					L"\nProblem: " + finding.problem +
					L"\nSuggested remediation: " + finding.remediation);
			}
		}
		ClientImageIdentity brokerImageIdentity;
#ifndef _DEBUG
		DWORD brokerIdentityError = NO_ERROR;
		if (!ReadCurrentProcessImageIdentity(
			brokerImageIdentity, brokerIdentityError) ||
			!brokerImageIdentity.signatureTrusted ||
			brokerImageIdentity.signerCertificateSha256.size() != 32)
		{
			const DWORD error = brokerIdentityError == NO_ERROR
				? ERROR_INVALID_DATA
				: brokerIdentityError;
			LogEvent(EVENTLOG_ERROR_TYPE,
				L"Broker image signature could not be established. Error " +
				std::to_wstring(error));
			SetServiceState(SERVICE_STOPPED, error);
			return;
		}
#endif
		const bool uninstallPurge =
			IsUninstallPurgeInvocation(argumentCount, arguments);
		if (uninstallPurge)
		{
			DWORD purgeError = NO_ERROR;
			if (!IsUninstallPurgeServiceAccessRestricted(purgeError))
			{
				if (purgeError == NO_ERROR)
					purgeError = ERROR_ACCESS_DENIED;
			}
			else
			{
				purgeError = PurgeBrokerCredentials();
			}
			LogEvent(purgeError == NO_ERROR
				? EVENTLOG_INFORMATION_TYPE : EVENTLOG_ERROR_TYPE,
				purgeError == NO_ERROR
					? L"Active xucRun data was removed for uninstallation; backups will be removed by the installer."
					: L"Active xucRun data could not be removed for uninstallation. Error " +
						std::to_wstring(purgeError));
			SetServiceState(SERVICE_STOPPED, purgeError);
			return;
		}
		bool restoreRecovered = false;
		DWORD restoreRecoveryError = NO_ERROR;
		if (!xucrun::backup_store::RecoverPendingRestore(
			restoreRecovered, restoreRecoveryError))
		{
			const DWORD error = restoreRecoveryError == NO_ERROR
				? ERROR_WRITE_FAULT : restoreRecoveryError;
			LogEvent(EVENTLOG_ERROR_TYPE,
				L"Pending backup restore recovery failed; service remains stopped. Error " +
				std::to_wstring(error) + L".");
			SetServiceState(SERVICE_STOPPED, error);
			return;
		}
		if (restoreRecovered)
		{
			LogEvent(EVENTLOG_INFORMATION_TYPE,
				L"Pending backup restore recovery completed before service startup.");
		}
		std::uint32_t deletedMetadataCount = 0;
		DWORD metadataCleanupError = NO_ERROR;
		if (!DeleteOrphanedCredentialMetadata(
			deletedMetadataCount, metadataCleanupError))
		{
			LogEvent(EVENTLOG_WARNING_TYPE,
				L"Orphaned credential metadata cleanup failed. Error " +
				std::to_wstring(metadataCleanupError) + L".");
		}
		else if (deletedMetadataCount != 0)
		{
			LogEvent(EVENTLOG_WARNING_TYPE,
				L"Removed " + std::to_wstring(deletedMetadataCount) +
				L" orphaned credential metadata entries during service startup.");
		}
		stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		if (!stopEvent)
		{
			const DWORD error = GetLastError();
			LogEvent(EVENTLOG_ERROR_TYPE,
				L"Stop event could not be created. Error " +
				std::to_wstring(error));
			SetServiceState(SERVICE_STOPPED, error);
			return;
		}

		LogEvent(EVENTLOG_INFORMATION_TYPE, L"Service started.");
		SetServiceState(SERVICE_RUNNING);
		const DWORD pipeError = RunBrokerPipeServer(
			stopEvent, brokerImageIdentity, LogBrokerEvent);
		if (pipeError != NO_ERROR)
		{
			LogEvent(EVENTLOG_ERROR_TYPE,
				L"Pipe server terminated with error " + std::to_wstring(pipeError) + L".");
		}
		CloseHandle(stopEvent);
		stopEvent = nullptr;
		DWORD stopError = NO_ERROR;
		if (purgeStoredProductData.load())
		{
			stopError = PurgeBrokerCredentials();
			LogEvent(stopError == NO_ERROR ? EVENTLOG_INFORMATION_TYPE : EVENTLOG_ERROR_TYPE,
				stopError == NO_ERROR
					? L"Active xucRun data was removed; backups were retained."
					: L"Active xucRun data could not be removed completely. Error " +
						std::to_wstring(stopError));
		}
		LogEvent(EVENTLOG_INFORMATION_TYPE, L"Service stopped.");
		SetServiceState(SERVICE_STOPPED, stopError);
	}

	// Translates an SCM service state into a compact diagnostic name.
	const wchar_t* ServiceStateName(DWORD state)
	{
		switch (state)
		{
		case SERVICE_STOPPED: return L"stopped";
		case SERVICE_START_PENDING: return L"start-pending";
		case SERVICE_STOP_PENDING: return L"stop-pending";
		case SERVICE_RUNNING: return L"running";
		case SERVICE_CONTINUE_PENDING: return L"continue-pending";
		case SERVICE_PAUSE_PENDING: return L"pause-pending";
		case SERVICE_PAUSED: return L"paused";
		default: return L"unknown";
		}
	}

	// Reads the SID of the current process token for identity-bound diagnostic requests.
	bool GetCurrentUserSid(std::wstring& sidString)
	{
		HANDLE token = nullptr;
		if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
			return false;
		DWORD size = 0;
		GetTokenInformation(token, TokenUser, nullptr, 0, &size);
		std::vector<std::uint8_t> buffer(size);
		const bool read = size != 0 && GetTokenInformation(
			token, TokenUser, buffer.data(), size, &size) == TRUE;
		CloseHandle(token);
		if (!read)
			return false;
		wchar_t* value = nullptr;
		const auto* user = reinterpret_cast<const TOKEN_USER*>(buffer.data());
		if (!ConvertSidToStringSidW(user->User.Sid, &value))
			return false;
		sidString = value;
		LocalFree(value);
		return true;
	}

	// Queries the installed broker service and outputs its current state.
	int ShowHealth()
	{
		SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
		if (!manager)
		{
			std::wcerr << L"SCM is unavailable. Error " << GetLastError() << L'\n';
			return 2;
		}

		SC_HANDLE service = OpenServiceW(manager, xucrun::broker::BrokerServiceName,
			SERVICE_QUERY_STATUS);
		if (!service)
		{
			const DWORD error = GetLastError();
			CloseServiceHandle(manager);
			std::wcerr << (error == ERROR_SERVICE_DOES_NOT_EXIST
				? L"xucRunBroker is not installed.\n"
				: L"Service status is unreadable. Error " + std::to_wstring(error) + L"\n");
			return 3;
		}

		SERVICE_STATUS_PROCESS status = {};
		DWORD bytesNeeded = 0;
		const BOOL queried = QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO,
			reinterpret_cast<BYTE*>(&status), sizeof(status), &bytesNeeded);
		const DWORD error = queried ? NO_ERROR : GetLastError();
		CloseServiceHandle(service);
		CloseServiceHandle(manager);
		if (!queried)
		{
			std::wcerr << L"Service status is unreadable. Error " << error << L'\n';
			return 4;
		}

		std::wcout << ServiceDisplayName << L": " <<
			ServiceStateName(status.dwCurrentState) << L'\n';
		return status.dwCurrentState == SERVICE_RUNNING ? 0 : 1;
	}

	// Tests the launch pipe contract with valid and deliberately invalid identity-bound requests.
	int TestLaunchPipe()
	{
		wchar_t executablePath[32768] = {};
		const DWORD pathLength = GetModuleFileNameW(nullptr, executablePath,
			static_cast<DWORD>(std::size(executablePath)));
		if (pathLength == 0 || pathLength == std::size(executablePath))
			return 7;
		std::wstring accountSid;
		if (!GetCurrentUserSid(accountSid))
			return 11;

		const std::uint64_t requestId = xucrun::broker::GenerateBrokerRequestId();
		const std::uint64_t invalidRequestId = xucrun::broker::GenerateBrokerRequestId();
		const std::uint64_t invalidSidRequestId = xucrun::broker::GenerateBrokerRequestId();
		const std::uint64_t nonBinaryRequestId = xucrun::broker::GenerateBrokerRequestId();
		if (requestId == 0 || invalidRequestId == 0 || invalidSidRequestId == 0 ||
			nonBinaryRequestId == 0 ||
			requestId == invalidRequestId || requestId == invalidSidRequestId ||
			requestId == nonBinaryRequestId || invalidRequestId == invalidSidRequestId ||
			invalidRequestId == nonBinaryRequestId || invalidSidRequestId == nonBinaryRequestId)
			return 10;
		const xucrun::broker::LaunchRequest request = xucrun::broker::BuildLaunchRequest(
			requestId, accountSid, executablePath, L"", L"",
			xucrun::broker::ElevationMode::PreserveToken);
		const xucrun::broker::PipeLaunchResponse response =
			xucrun::broker::SubmitBrokerLaunchRequest(request);
		if (response.status != xucrun::broker::PipeLaunchStatus::Succeeded ||
			response.result.status != xucrun::broker::LaunchStatus::Denied ||
			response.result.windowsError != ERROR_NOT_SUPPORTED)
		{
			std::wcerr << L"xucRunBroker launch pipe: unexpected valid-target result (Transport " <<
				static_cast<int>(response.status) << L", error " << response.windowsError << L")\n";
			return 8;
		}

		const std::wstring missingPath = std::wstring(executablePath) + L"." +
			std::to_wstring(requestId) + L".missing";
		const xucrun::broker::LaunchRequest invalidRequest =
			xucrun::broker::BuildLaunchRequest(
				invalidRequestId, accountSid, missingPath, L"", L"",
				xucrun::broker::ElevationMode::PreserveToken);
		const xucrun::broker::PipeLaunchResponse invalidResponse =
			xucrun::broker::SubmitBrokerLaunchRequest(invalidRequest);
		if (invalidResponse.status != xucrun::broker::PipeLaunchStatus::Succeeded ||
			invalidResponse.result.status != xucrun::broker::LaunchStatus::InvalidRequest)
		{
			std::wcerr << L"xucRunBroker launch pipe: invalid target was not rejected as expected "
				L"(Transport " << static_cast<int>(invalidResponse.status) <<
				L", error " << invalidResponse.windowsError << L", broker status " <<
				static_cast<int>(invalidResponse.result.status) << L", broker error " <<
				invalidResponse.result.windowsError << L")\n";
			return 9;
		}

		wchar_t temporaryDirectory[MAX_PATH] = {};
		wchar_t nonBinaryPath[MAX_PATH] = {};
		if (GetTempPathW(static_cast<DWORD>(std::size(temporaryDirectory)),
			temporaryDirectory) == 0 ||
			GetTempFileNameW(temporaryDirectory, L"xur", 0, nonBinaryPath) == 0)
		{
			return 13;
		}
		const xucrun::broker::LaunchRequest nonBinaryRequest =
			xucrun::broker::BuildLaunchRequest(
				nonBinaryRequestId, accountSid, nonBinaryPath, L"", L"",
				xucrun::broker::ElevationMode::PreserveToken);
		const xucrun::broker::PipeLaunchResponse nonBinaryResponse =
			xucrun::broker::SubmitBrokerLaunchRequest(nonBinaryRequest);
		DeleteFileW(nonBinaryPath);
		if (nonBinaryResponse.status != xucrun::broker::PipeLaunchStatus::Succeeded ||
			nonBinaryResponse.result.status != xucrun::broker::LaunchStatus::InvalidRequest)
		{
			std::wcerr << L"xucRunBroker launch pipe: non-PE target was not rejected\n";
			return 13;
		}

		const xucrun::broker::LaunchRequest invalidSidRequest =
			xucrun::broker::BuildLaunchRequest(
				invalidSidRequestId, L"not-a-sid", executablePath, L"", L"",
				xucrun::broker::ElevationMode::PreserveToken);
		const xucrun::broker::PipeLaunchResponse invalidSidResponse =
			xucrun::broker::SubmitBrokerLaunchRequest(invalidSidRequest);
		if (invalidSidResponse.status == xucrun::broker::PipeLaunchStatus::Succeeded &&
			invalidSidResponse.result.status == xucrun::broker::LaunchStatus::InvalidRequest)
		{
			std::wcout << L"xucRunBroker launch pipe: account and target validated; invalid inputs rejected\n";
			return 0;
		}
		std::wcerr << L"xucRunBroker launch pipe: invalid account SID was not rejected\n";
		return 12;
	}

	// Tests the metadata pipe contract including SID binding and an invalid account SID.
	int TestCredentialMetadataPipe()
	{
		std::wstring accountSid;
		if (!GetCurrentUserSid(accountSid))
			return 17;

		for (int attempt = 0; attempt < 2; attempt++)
		{
			xucrun::broker::CredentialMetadataRequest request;
			request.requestId = xucrun::broker::GenerateBrokerRequestId();
			request.requestedAccountSid = accountSid;
			if (request.requestId == 0)
				return 18;
			const xucrun::broker::PipeCredentialMetadataResponse response =
				xucrun::broker::SubmitCredentialMetadataRequest(request);
			if (response.status !=
				xucrun::broker::PipeCredentialMetadataStatus::Succeeded ||
				response.result.status !=
					xucrun::broker::CredentialMetadataStatus::Succeeded)
			{
				std::wcerr << L"xucRunBroker metadata pipe: roundtrip failed (attempt " <<
					attempt + 1 << L", Transport " << static_cast<int>(response.status) <<
					L", error " << response.windowsError << L", broker status " <<
					static_cast<int>(response.result.status) << L", broker error " <<
					response.result.windowsError << L")\n";
				return 19;
			}
		}

		xucrun::broker::CredentialMetadataRequest invalidRequest;
		invalidRequest.requestId = xucrun::broker::GenerateBrokerRequestId();
		invalidRequest.requestedAccountSid = L"not-a-sid";
		if (invalidRequest.requestId == 0)
			return 20;
		const xucrun::broker::PipeCredentialMetadataResponse invalidResponse =
			xucrun::broker::SubmitCredentialMetadataRequest(invalidRequest);
		if (invalidResponse.status ==
			xucrun::broker::PipeCredentialMetadataStatus::Succeeded &&
			invalidResponse.result.status ==
				xucrun::broker::CredentialMetadataStatus::InvalidRequest)
		{
			std::wcout << L"xucRunBroker metadata pipe: SID-bound store roundtrip passed\n";
			return 0;
		}
		std::wcerr << L"xucRunBroker metadata pipe: invalid account SID was not rejected\n";
		return 21;
	}

	// Tests that the session pipe contract launches a test process in the client session.
	int TestSessionPipe()
	{
		xucrun::broker::SessionTestRequest request;
		request.requestId = xucrun::broker::GenerateBrokerRequestId();
		if (request.requestId == 0)
			return 22;
		const xucrun::broker::PipeSessionTestResponse response =
			xucrun::broker::SubmitSessionTestRequest(request);
		if (response.status != xucrun::broker::PipeSessionTestStatus::Succeeded ||
			response.result.status != xucrun::broker::SessionTestStatus::Succeeded ||
			response.result.processId == 0)
		{
			std::wcerr << L"xucRunBroker session pipe: start failed (Transport " <<
				static_cast<int>(response.status) << L", error " << response.windowsError <<
				L", broker status " << static_cast<int>(response.result.status) <<
				L", broker error " << response.result.windowsError << L")\n";
			return 23;
		}

		DWORD currentSessionId = 0;
		DWORD launchedSessionId = 0;
		if (!ProcessIdToSessionId(GetCurrentProcessId(), &currentSessionId) ||
			!ProcessIdToSessionId(response.result.processId, &launchedSessionId) ||
			launchedSessionId != currentSessionId)
		{
			std::wcerr << L"xucRunBroker session pipe: process started in wrong session\n";
			return 24;
		}
		std::wcout << L"xucRunBroker session pipe: fixed test console started in session " <<
			launchedSessionId << L" (PID " << response.result.processId << L")\n";
		return 0;
	}

	// Runs the LocalSystem-isolated credential store self-test through the pipe.
	int TestCredentialStorePipe()
	{
		xucrun::broker::CredentialStoreTestRequest request;
		request.requestId = xucrun::broker::GenerateBrokerRequestId();
		if (request.requestId == 0)
			return 25;
		const xucrun::broker::PipeCredentialStoreTestResponse response =
			xucrun::broker::SubmitCredentialStoreTestRequest(request);
		if (response.status ==
			xucrun::broker::PipeCredentialStoreTestStatus::Succeeded &&
			response.result.status ==
				xucrun::broker::CredentialStoreTestStatus::Succeeded)
		{
			std::wcout << L"xucRunBroker credential store: LocalSystem roundtrip passed\n";
			return 0;
		}
		std::wcerr << L"xucRunBroker credential store: self-test failed (Transport " <<
			static_cast<int>(response.status) << L", error " << response.windowsError <<
			L", broker status " << static_cast<int>(response.result.status) <<
			L", broker error " << response.result.windowsError << L")\n";
		return 26;
	}

	// Tests that an unauthorized client cannot store credentials.
	int TestCredentialSaveGate()
	{
		xucrun::broker::CredentialSaveRequest request;
		request.requestId = xucrun::broker::GenerateBrokerRequestId();
		request.requestedAccountSid = L"S-1-5-18";
		request.password.assign(L"diagnostic-not-a-password", 25);
		const xucrun::broker::PipeCredentialSaveResponse response =
			xucrun::broker::SubmitCredentialSaveRequest(request);
		request.password.clear();
		if (response.status == xucrun::broker::PipeCredentialSaveStatus::Succeeded &&
			response.result.status == xucrun::broker::CredentialSaveStatus::InvalidRequest &&
			response.result.windowsError == ERROR_ACCESS_DENIED)
		{
			std::wcout << L"xucRunBroker credential save: unauthorized client rejected\n";
			return 0;
		}
		std::wcerr << L"xucRunBroker credential save gate failed (Transport " <<
			static_cast<int>(response.status) << L", error " << response.windowsError <<
			L", broker status " << static_cast<int>(response.result.status) <<
			L", broker error " << response.result.windowsError << L")\n";
		return 27;
	}

	// Tests that an unauthorized client cannot delete credentials.
	int TestCredentialDeleteGate()
	{
		xucrun::broker::CredentialDeleteRequest request;
		request.requestId = xucrun::broker::GenerateBrokerRequestId();
		request.requestedAccountSid = L"S-1-5-18";
		const xucrun::broker::PipeCredentialDeleteResponse response =
			xucrun::broker::SubmitCredentialDeleteRequest(request);
		if (response.status == xucrun::broker::PipeCredentialDeleteStatus::Succeeded &&
			response.result.status == xucrun::broker::CredentialDeleteStatus::InvalidRequest &&
			response.result.windowsError == ERROR_ACCESS_DENIED)
		{
			std::wcout << L"xucRunBroker credential delete: unauthorized client rejected\n";
			return 0;
		}
		std::wcerr << L"xucRunBroker credential delete gate failed (Transport " <<
			static_cast<int>(response.status) << L", error " << response.windowsError <<
			L", broker status " << static_cast<int>(response.result.status) <<
			L", broker error " << response.result.windowsError << L")\n";
		return 29;
	}

	// Tests that an unauthorized client cannot use stored credentials.
	int TestCredentialUseGate()
	{
		xucrun::broker::CredentialUseTestRequest request;
		request.requestId = xucrun::broker::GenerateBrokerRequestId();
		request.requestedAccountSid = L"S-1-5-18";
		const xucrun::broker::PipeCredentialUseTestResponse response =
			xucrun::broker::SubmitCredentialUseTestRequest(request);
		if (response.status == xucrun::broker::PipeCredentialUseTestStatus::Succeeded &&
			response.result.status == xucrun::broker::CredentialUseTestStatus::InvalidRequest &&
			response.result.windowsError == ERROR_ACCESS_DENIED)
		{
			std::wcout << L"xucRunBroker credential use: unauthorized client rejected\n";
			return 0;
		}
		std::wcerr << L"xucRunBroker credential use gate failed (Transport " <<
			static_cast<int>(response.status) << L", error " << response.windowsError <<
			L", broker status " << static_cast<int>(response.result.status) <<
			L", broker error " << response.result.windowsError << L")\n";
		return 30;
	}

	// Lowers its own token integrity and tests rejection by the pipe integrity boundary.
	int TestLowIntegrityPipeRejection()
	{
		HANDLE token = nullptr;
		if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_DEFAULT | TOKEN_QUERY, &token))
		{
			std::wcerr << L"Process token could not be opened. Error " <<
				GetLastError() << L'\n';
			return 13;
		}

		SID_IDENTIFIER_AUTHORITY authority = SECURITY_MANDATORY_LABEL_AUTHORITY;
		PSID lowIntegritySid = nullptr;
		if (!AllocateAndInitializeSid(&authority, 1, SECURITY_MANDATORY_LOW_RID,
			0, 0, 0, 0, 0, 0, 0, &lowIntegritySid))
		{
			const DWORD error = GetLastError();
			CloseHandle(token);
			std::wcerr << L"Low-integrity SID could not be created. Error " <<
				error << L'\n';
			return 14;
		}

		TOKEN_MANDATORY_LABEL label = {};
		label.Label.Attributes = SE_GROUP_INTEGRITY;
		label.Label.Sid = lowIntegritySid;
		const DWORD labelSize = static_cast<DWORD>(
			sizeof(TOKEN_MANDATORY_LABEL) + GetLengthSid(lowIntegritySid));
		const BOOL lowered = SetTokenInformation(
			token, TokenIntegrityLevel, &label, labelSize);
		const DWORD lowerError = lowered ? NO_ERROR : GetLastError();
		FreeSid(lowIntegritySid);
		CloseHandle(token);
		if (!lowered)
		{
			std::wcerr << L"Integrity level could not be lowered. Error " <<
				lowerError << L'\n';
			return 15;
		}

		const xucrun::broker::PipeHealthCheck health =
			xucrun::broker::QueryBrokerPipeHealth();
		if (health.status != xucrun::broker::PipeHealthStatus::Succeeded &&
			health.windowsError == ERROR_ACCESS_DENIED)
		{
			std::wcout << L"xucRunBroker pipe: low-integrity client rejected as expected\n";
			return 0;
		}
		std::wcerr << L"xucRunBroker pipe: low-integrity rejection failed (status " <<
			static_cast<int>(health.status) << L", error " << health.windowsError << L")\n";
		return 16;
	}

	// Tests that unauthorized clients can use neither approval challenges nor confirmations.
	int TestApprovalChallengeGate()
	{
		std::wstring accountSid;
		wchar_t systemDirectory[MAX_PATH] = {};
		const UINT directoryLength = GetSystemDirectoryW(
			systemDirectory, static_cast<UINT>(std::size(systemDirectory)));
		if (!GetCurrentUserSid(accountSid) || directoryLength == 0 ||
			directoryLength >= std::size(systemDirectory))
		{
			return 31;
		}
		xucrun::broker::ApprovalChallengeRequest request;
		request.requestId = xucrun::broker::GenerateBrokerRequestId();
		request.requestedAccountSid = accountSid;
		request.executablePath = std::wstring(systemDirectory) + L"\\cmd.exe";
		request.workingDirectory = systemDirectory;
		request.visibility = xucrun::broker::LaunchVisibility::Normal;
		const xucrun::broker::PipeApprovalChallengeResponse response =
			xucrun::broker::SubmitApprovalChallengeRequest(request,
				xucrun::broker::DefaultBrokerRequestTimeoutMilliseconds);
		xucrun::broker::ApprovalConfirmationRequest confirmation;
		confirmation.requestId = xucrun::broker::GenerateBrokerRequestId();
		confirmation.challengeId[0] = 1;
		confirmation.decision = xucrun::broker::ApprovalDecision::ApproveOnce;
		const xucrun::broker::PipeApprovalConfirmationResponse confirmationResponse =
			xucrun::broker::SubmitApprovalConfirmationRequest(confirmation,
				xucrun::broker::DefaultBrokerRequestTimeoutMilliseconds);
		if (response.status == xucrun::broker::PipeApprovalChallengeStatus::Succeeded &&
			response.result.status == xucrun::broker::ApprovalChallengeStatus::Denied &&
			response.result.windowsError == ERROR_ACCESS_DENIED &&
			confirmationResponse.status ==
				xucrun::broker::PipeApprovalConfirmationStatus::Succeeded &&
			confirmationResponse.result.status ==
				xucrun::broker::ApprovalConfirmationStatus::Denied &&
			confirmationResponse.result.windowsError == ERROR_ACCESS_DENIED)
		{
			std::wcout << L"xucRunBroker approval challenge: unauthorized client rejected\n";
			return 0;
		}
		std::wcerr << L"xucRunBroker approval challenge gate failed (Transport " <<
			static_cast<int>(response.status) << L", error " << response.windowsError <<
			L", broker status " << static_cast<int>(response.result.status) <<
			L", broker error " << response.result.windowsError << L")\n";
		return 31;
	}
}

// Applies early hardening and dispatches service operation and explicit diagnostic commands.
int wmain(int argc, wchar_t* argv[])
{
	processHardeningWarnings = ApplyEarlyProcessHardening();
	if (argc == 7 && _wcsicmp(argv[1], L"/secure-desktop-host") == 0)
	{
		wchar_t* end = nullptr;
		const unsigned long long value = wcstoull(argv[2], &end, 10);
		HANDLE token = value != 0 && end && *end == L'\0'
			? reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(value))
			: nullptr;
		wchar_t* reportEnd = nullptr;
		const unsigned long long reportValue = wcstoull(argv[3], &reportEnd, 10);
		HANDLE report = reportValue != 0 && reportEnd && *reportEnd == L'\0'
			? reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(reportValue))
			: nullptr;
		HANDLE cancellationEvent = OpenEventW(SYNCHRONIZE, FALSE, argv[4]);
		if (!token || !report || !cancellationEvent)
		{
			if (cancellationEvent) CloseHandle(cancellationEvent);
			return ERROR_ACCESS_DENIED;
		}
		int result = xucrun::secure_desktop::RunHost(
			token, argv[5], argv[6], nullptr, report, cancellationEvent, true);
		CloseHandle(cancellationEvent);
		xucrun::broker::RequestNonce credentialChallengeId = {};
		if (result == ERROR_SUCCESS && ParseCredentialChallengeArgument(
			argv[6], credentialChallengeId))
		{
			xucrun::broker::CredentialCaptureCommitRequest request;
			request.requestId = xucrun::broker::GenerateBrokerRequestId();
			request.challengeId = credentialChallengeId;
			const auto response =
				xucrun::broker::SubmitCredentialCaptureCommitRequest(request);
			if (response.status != xucrun::broker::PipeCredentialSaveStatus::Succeeded ||
				response.result.status != xucrun::broker::CredentialSaveStatus::Succeeded)
			{
				result = static_cast<int>(response.status ==
					xucrun::broker::PipeCredentialSaveStatus::Succeeded
					? response.result.windowsError : response.windowsError);
				if (result == ERROR_SUCCESS)
					result = ERROR_GEN_FAILURE;
			}
		}
		return result;
	}
	if (argc == 3 && _wcsicmp(argv[1], L"/grant-desktop-access-helper") == 0)
	{
		DWORD error = NO_ERROR;
		return RunInteractiveDesktopAccessHelper(argv[2], error)
			? 0
			: static_cast<int>(error == NO_ERROR ? ERROR_GEN_FAILURE : error);
	}
	if (argc == 2)
	{
		const std::wstring command = argv[1];
		if (_wcsicmp(command.c_str(), L"/service-name") == 0)
		{
			std::wcout << xucrun::broker::BrokerServiceName << L'\n';
			return 0;
		}
		if (_wcsicmp(command.c_str(), L"/version") == 0)
		{
			std::wcout << L"xucRunBroker protocol " <<
				xucrun::broker::ProtocolVersion << L'\n';
			return 0;
		}
		if (_wcsicmp(command.c_str(), L"/health") == 0)
			return ShowHealth();
#ifdef _DEBUG
		if (_wcsicmp(command.c_str(), L"/uninstall-purge-mode-test") == 0)
		{
			wchar_t serviceName[] = L"xucRunBroker";
			wchar_t purgeArgument[] = L"uninstall-purge";
			wchar_t otherArgument[] = L"other";
			wchar_t* validArguments[] = { serviceName, purgeArgument };
			wchar_t* invalidArguments[] = { serviceName, otherArgument };
			if (IsUninstallPurgeInvocation(2, validArguments) &&
				!IsUninstallPurgeInvocation(1, validArguments) &&
				!IsUninstallPurgeInvocation(2, invalidArguments) &&
				!IsUninstallPurgeInvocation(3, validArguments))
			{
				std::wcout << L"xucRunBroker uninstall purge mode: tests passed\n";
				return 0;
			}
			std::wcerr << L"xucRunBroker uninstall purge mode: tests failed\n";
			return 35;
		}
		if (_wcsicmp(command.c_str(), L"/mitigation-test") == 0)
		{
			if (RunProcessMitigationSelfTest())
			{
				std::wcout << L"xucRunBroker process mitigations: tests passed\n";
				return 0;
			}
			for (const std::wstring& warning : processHardeningWarnings)
				std::wcerr << warning << L'\n';
			std::wcerr << L"xucRunBroker process mitigations: tests failed\n";
			return 32;
		}
		if (_wcsicmp(command.c_str(), L"/acl-evaluator-test") == 0)
		{
			if (RunSecurityAclAuditSelfTest())
			{
				std::wcout << L"xucRunBroker ACL evaluator: tests passed\n";
				return 0;
			}
			std::wcerr << L"xucRunBroker ACL evaluator: tests failed\n";
			return 34;
		}
		if (_wcsicmp(command.c_str(), L"/acl-audit-test") == 0)
		{
			const std::vector<SecurityAclFinding> findings = AuditBrokerSecurityAcls();
			for (const SecurityAclFinding& finding : findings)
			{
				std::wcerr << L"Object: " << finding.objectName << L'\n' <<
					L"Problem: " << finding.problem << L'\n' <<
					L"Suggested remediation: " << finding.remediation << L"\n\n";
			}
			if (RunSecurityAclAuditSelfTest() && findings.empty())
			{
				std::wcout << L"xucRunBroker ACL audit: tests passed\n";
				return 0;
			}
			std::wcerr << L"xucRunBroker ACL audit: tests failed\n";
			return 33;
		}
		if (_wcsicmp(command.c_str(), L"/store-security-test") == 0)
		{
			DWORD error = NO_ERROR;
			if (!RunCredentialMetadataStoreSecuritySelfTest(error))
			{
				std::wcerr << L"xucRunBroker metadata store security: tests failed (error " <<
					error << L")\n";
				return 34;
			}
			if (!xucrun::backup_store::RunSecuritySelfTest(error))
			{
				std::wcerr << L"xucRunBroker backup store security: tests failed (error " <<
					error << L")\n";
				return 34;
			}
			if (error == NO_ERROR)
			{
				std::wcout << L"xucRunBroker store security: tests passed\n";
				return 0;
			}
			std::wcerr << L"xucRunBroker store security: tests failed (error " <<
				error << L")\n";
			return 34;
		}
		if (_wcsicmp(command.c_str(), L"/client-policy-test") == 0)
		{
			if (RunSensitiveClientPolicySelfTest() &&
				RunBrokerRequestPolicySelfTest())
			{
				std::wcout << L"xucRunBroker sensitive client policy: tests passed\n";
				return 0;
			}
			std::wcerr << L"xucRunBroker sensitive client policy: tests failed\n";
			return 9;
		}
		if (_wcsicmp(command.c_str(), L"/client-policy-config-test") == 0)
		{
			DWORD policyError = NO_ERROR;
			std::wstring failedProtectionPath;
			if (RunSensitiveClientPolicyConfigurationTest(
				policyError, &failedProtectionPath))
			{
				std::wcout << L"xucRunBroker sensitive client policy: configured client accepted\n";
				return 0;
			}
			std::wcerr << L"xucRunBroker sensitive client policy: configured client rejected, error "
				<< policyError;
			if (!failedProtectionPath.empty())
				std::wcerr << L", protection path " << failedProtectionPath;
			std::wcerr << L'\n';
			return 10;
		}
		if (_wcsicmp(command.c_str(), L"/credential-validator-test") == 0)
		{
			if (RunCredentialValidatorSelfTest())
			{
				std::wcout << L"xucRunBroker credential validator: tests passed\n";
				return 0;
			}
			std::wcerr << L"xucRunBroker credential validator: tests failed\n";
			return 28;
		}
		if (_wcsicmp(command.c_str(), L"/approval-match-test") == 0)
		{
			if (xucrun::approval_store::RunApprovalMatchSelfTest())
			{
				std::wcout << L"xucRunBroker approval matching: tests passed\n";
				return 0;
			}
			std::wcerr << L"xucRunBroker approval matching: tests failed\n";
			return 34;
		}
		if (_wcsicmp(command.c_str(), L"/approval-challenge-identity-test") == 0)
		{
			if (RunApprovalChallengeIdentitySelfTest())
			{
				std::wcout << L"xucRunBroker approval challenge identity: tests passed\n";
				return 0;
			}
			std::wcerr << L"xucRunBroker approval challenge identity: tests failed\n";
			return 35;
		}
		if (_wcsicmp(command.c_str(), L"/tracked-process-list-test") == 0)
		{
			if (RunTrackedProcessListSelectionSelfTest())
			{
				std::wcout << L"xucRunBroker tracked process list: tests passed\n";
				return 0;
			}
			std::wcerr << L"xucRunBroker tracked process list: tests failed\n";
			return 36;
		}
		if (_wcsicmp(command.c_str(), L"/temporary-batch-test") == 0)
		{
			if (RunBrokerTemporaryBatchSelfTest())
			{
				std::wcout << L"xucRunBroker temporary batch: tests passed\n";
				return 0;
			}
			std::wcerr << L"xucRunBroker temporary batch: tests failed\n";
			return 37;
		}
		if (_wcsicmp(command.c_str(), L"/pipe-health") == 0)
		{
			const xucrun::broker::PipeHealthCheck health =
				xucrun::broker::QueryBrokerPipeHealth();
			if (health.status == xucrun::broker::PipeHealthStatus::Succeeded)
			{
				std::wcout << L"xucRunBroker pipe: healthy (PID " <<
					health.brokerProcessId << L", elevated processes " <<
					health.elevatedProcessCount << L")\n";
				return 0;
			}
			std::wcerr << L"xucRunBroker pipe: unavailable (status " <<
				static_cast<int>(health.status) << L", error " <<
				health.windowsError << L")\n";
			return 6;
		}
		if (_wcsicmp(command.c_str(), L"/pipe-launch-test") == 0)
			return TestLaunchPipe();
		if (_wcsicmp(command.c_str(), L"/pipe-metadata-test") == 0)
			return TestCredentialMetadataPipe();
		if (_wcsicmp(command.c_str(), L"/pipe-session-test") == 0)
			return TestSessionPipe();
		if (_wcsicmp(command.c_str(), L"/pipe-credential-store-test") == 0)
			return TestCredentialStorePipe();
		if (_wcsicmp(command.c_str(), L"/pipe-credential-save-gate-test") == 0)
			return TestCredentialSaveGate();
		if (_wcsicmp(command.c_str(), L"/pipe-credential-delete-gate-test") == 0)
			return TestCredentialDeleteGate();
		if (_wcsicmp(command.c_str(), L"/pipe-credential-use-gate-test") == 0)
			return TestCredentialUseGate();
		if (_wcsicmp(command.c_str(), L"/pipe-low-integrity-test") == 0)
			return TestLowIntegrityPipeRejection();
		if (_wcsicmp(command.c_str(), L"/pipe-approval-challenge-gate-test") == 0)
			return TestApprovalChallengeGate();
#endif
	}
	else if (argc == 1)
	{
		SERVICE_TABLE_ENTRYW serviceTable[] = {
			{ const_cast<wchar_t*>(xucrun::broker::BrokerServiceName), ServiceMain },
			{ nullptr, nullptr }
		};
		if (StartServiceCtrlDispatcherW(serviceTable))
			return 0;
		const DWORD error = GetLastError();
		LogEvent(EVENTLOG_ERROR_TYPE,
			L"Service Control Dispatcher could not be started. Error " +
			std::to_wstring(error));
		std::wcerr << L"xucRunBroker must be started by the Service Control Manager. Error " <<
			error << L'\n';
		return 5;
	}

	std::wcerr << L"Usage: xucRunBroker.exe [/service-name|/health";
#ifdef _DEBUG
	std::wcerr << L"|/mitigation-test|/acl-evaluator-test|/acl-audit-test|"
		L"/store-security-test|/client-policy-test|/client-policy-config-test|"
		L"/credential-validator-test|/approval-match-test|"
		L"/approval-challenge-identity-test|/tracked-process-list-test|"
		L"/temporary-batch-test|/pipe-health|/pipe-launch-test|"
		L"/pipe-metadata-test|/pipe-session-test|"
		L"/pipe-credential-store-test|/pipe-credential-save-gate-test|"
		L"/pipe-credential-delete-gate-test|/pipe-credential-use-gate-test|"
		L"/pipe-approval-challenge-gate-test|/pipe-low-integrity-test";
#endif
	std::wcerr << L"|/version]\n";
	return 1;
}