// Purpose: Implements client-side process launch via stored broker credentials.
// Behavior: Handles broker- or UAC-started approval, confirmation, and window activation.
#include "ClientBrokerLaunch.h"

#include <Windows.h>
#include <shellapi.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "BrokerContracts.h"
#include "BrokerPipeClient.h"
#include "ClientAccountIdentity.h"
#include "ClientBrokerCredentialManagement.h"
#include "ClientBrokerCredentialSelection.h"
#include "ClientCredentialFormatting.h"
#include "ClientCredentialInfo.h"
#include "ClientLaunchDetection.h"
#include "ClientLogging.h"
#include "ClientUiCommon.h"
#include "ClientWindowActivation.h"
#include "ClientWindowsError.h"
#include "DocumentationNameMapping.h"
#include "Localization.h"
#include "TargetNaming.h"

namespace xucrun::broker_launch
{
	bool LaunchWithUac(const std::wstring& executablePath,
		const std::wstring& argumentTail, broker::LaunchVisibility visibility,
		DWORD& error)
	{
		SHELLEXECUTEINFOW executeInfo = { sizeof(executeInfo) };
		executeInfo.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
		executeInfo.lpVerb = L"runas";
		executeInfo.lpFile = executablePath.c_str();
		executeInfo.lpParameters = argumentTail.c_str();
		executeInfo.nShow = visibility == broker::LaunchVisibility::Hidden
			? SW_HIDE : SW_SHOWNORMAL;
		if (!ShellExecuteExW(&executeInfo))
		{
			error = GetLastError();
			return false;
		}
		if (!executeInfo.hProcess)
		{
			error = ERROR_INVALID_HANDLE;
			return false;
		}
		const DWORD processId = GetProcessId(executeInfo.hProcess);
		if (processId != 0)
			AllowSetForegroundWindow(processId);
		CloseHandle(executeInfo.hProcess);
		error = NO_ERROR;
		return true;
	}

	namespace
	{
		// Starts only the installed sibling helper and passes no data beyond the opaque challenge ID.
		bool LaunchApprovalHelperWithRunAs(
			const broker::RequestNonce& challengeId, HANDLE& process, DWORD& error)
		{
			process = nullptr;
			std::vector<wchar_t> pathBuffer(512);
			DWORD pathLength = 0;
			for (;;)
			{
				SetLastError(NO_ERROR);
				pathLength = GetModuleFileNameW(nullptr, pathBuffer.data(),
					static_cast<DWORD>(pathBuffer.size()));
				if (pathLength == 0)
				{
					error = GetLastError();
					return false;
				}
				if (pathLength < pathBuffer.size() - 1)
					break;
				pathBuffer.resize(pathBuffer.size() * 2);
			}
			const std::filesystem::path helperPath =
				std::filesystem::path(std::wstring(pathBuffer.data(), pathLength))
					.parent_path() / L"xucRunApproval.exe";
			const std::wstring parameters = L"/secure-desktop-bootstrap /challenge:" +
				targets::BytesToUpperHex(challengeId);
			const std::wstring workingDirectory = helperPath.parent_path().wstring();
			SHELLEXECUTEINFOW executeInfo = { sizeof(executeInfo) };
			executeInfo.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
			executeInfo.lpVerb = L"runas";
			executeInfo.lpFile = helperPath.c_str();
			executeInfo.lpParameters = parameters.c_str();
			executeInfo.lpDirectory = workingDirectory.c_str();
			executeInfo.nShow = SW_SHOWNORMAL;
			if (!ShellExecuteExW(&executeInfo))
			{
				error = GetLastError();
				return false;
			}
			if (!executeInfo.hProcess)
			{
				error = ERROR_INVALID_HANDLE;
				return false;
			}
			process = executeInfo.hProcess;
			error = NO_ERROR;
			return true;
		}

		bool WaitForApprovalHelperProcess(HANDLE process, DWORD& error)
		{
			if (!process)
			{
				error = ERROR_INVALID_HANDLE;
				return false;
			}
			const DWORD processId = GetProcessId(process);
			if (processId != 0 && !AllowSetForegroundWindow(processId))
			{
				client_logging::Write(L"Info",
					L"Windows did not grant foreground activation to approval helper PID " +
					std::to_wstring(processId));
			}
			const DWORD wait = WaitForSingleObject(process,
				broker::ApprovalHelperProcessWaitTimeoutMilliseconds);
			if (wait != WAIT_OBJECT_0)
			{
				error = wait == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError();
				CloseHandle(process);
				return false;
			}
			DWORD exitCode = ERROR_GEN_FAILURE;
			const bool readExitCode = GetExitCodeProcess(process, &exitCode) == TRUE;
			CloseHandle(process);
			error = readExitCode ? exitCode : GetLastError();
			return readExitCode && exitCode == ERROR_SUCCESS;
		}
	}

	bool WaitForApprovalHelper(HANDLE process, DWORD& error)
	{
		return WaitForApprovalHelperProcess(process, error);
	}

	void RegisterElevatedProcessWithBroker(HANDLE process)
	{
		if (!process)
			return;
		const DWORD processId = GetProcessId(process);
		broker::TrackedProcessRegistrationRequest request;
		request.requestId = broker::GenerateBrokerRequestId();
		request.processHandle = static_cast<std::uint64_t>(
			reinterpret_cast<ULONG_PTR>(process));
		request.processId = processId;
		if (request.requestId == 0 || request.processHandle == 0 || processId == 0)
		{
			client_logging::Write(L"Warning", L"Elevated process could not be registered for monitoring");
			return;
		}
		const auto response = broker::SubmitTrackedProcessRegistrationRequest(request, 1000);
		if (response.status != broker::PipeTrackedProcessRegistrationStatus::Succeeded ||
			response.result.status != broker::TrackedProcessRegistrationStatus::Succeeded)
		{
			const DWORD error = response.status ==
				broker::PipeTrackedProcessRegistrationStatus::Succeeded
				? response.result.windowsError
				: response.windowsError;
			client_logging::Write(L"Warning", L"Elevated process started but was not registered in the broker. "
				L"Error " + windows_error::FormatWindowsError(error));
			return;
		}
		client_logging::Write(L"Ok", L"Elevated process registered in broker. PID " +
			std::to_wstring(processId));
	}

	BrokerLaunchOutcome LaunchWithBrokerCredential(const CredentialInfo& credential,
		const std::wstring& executablePath, const std::wstring& argumentTail,
		broker::ElevationMode elevationMode, broker::LaunchVisibility visibility)
	{
		std::wstring currentUsername;
		std::wstring currentDomain;
		const std::wstring invokingAccount =
			account_identity::GetCurrentUserInfo(currentUsername, currentDomain)
			? (currentDomain.empty()
				? currentUsername : currentDomain + L"\\" + currentUsername)
			: localization::GetText(localization::TextId::ValueUnknown);
		const std::wstring accountContext = localization::Format(
			localization::TextId::BrokerAccountContext,
			{ documentation_names::DisplayAccount(invokingAccount),
				documentation_names::DisplayAccount(credential.username) });
		const DWORD directoryLength = GetCurrentDirectoryW(0, nullptr);
		std::vector<wchar_t> directoryBuffer(directoryLength);
		if (directoryLength == 0 ||
			GetCurrentDirectoryW(directoryLength, directoryBuffer.data()) == 0)
		{
			const DWORD directoryError = GetLastError();
			ui::ReportError(localization::Format(
				localization::TextId::BrokerWorkingDirectoryFailed,
				{ windows_error::FormatWindowsError(directoryError) }) + accountContext,
				L"Determining the working directory for a broker launch failed. Windows error: " +
					std::to_wstring(directoryError));
			return BrokerLaunchOutcome::Failed;
		}

		const std::wstring& brokerExecutablePath = executablePath;
		const std::wstring& brokerArgumentTail = argumentTail;
		const std::wstring brokerWorkingDirectory = directoryBuffer.data();

		CredentialInfo approvalCredential;
		if (elevationMode == broker::ElevationMode::PreserveToken &&
			!account_identity::IsUserInAdminGroup() &&
			!account_identity::IsUserInAdminGroup(credential.username))
		{
			DWORD selectionError = NO_ERROR;
			auto selection =
				broker_credential_selection::SelectBrokerApprovalCredential(
					approvalCredential, selectionError);
			if (selection == broker_credential_selection::
				BrokerCredentialSelectionOutcome::NotFound)
			{
				if (!broker_credential_management::PromptAndSaveBrokerCredentials(
					L"", ui::GetDialogOwnerWindow(), true))
				{
					return BrokerLaunchOutcome::Cancelled;
				}
				selection = broker_credential_selection::SelectBrokerApprovalCredential(
					approvalCredential, selectionError);
			}
			if (selection == broker_credential_selection::
				BrokerCredentialSelectionOutcome::Cancelled)
				return BrokerLaunchOutcome::Cancelled;
			if (selection != broker_credential_selection::
				BrokerCredentialSelectionOutcome::Selected)
			{
				ui::ReportError(localization::Format(
					localization::TextId::BrokerCredentialsQueryFailed,
					{ windows_error::FormatWindowsError(selectionError) }) +
					accountContext,
					L"Selecting an administrator credential for broker approval failed. "
					L"Windows error: " + std::to_wstring(selectionError));
				return BrokerLaunchOutcome::Failed;
			}
		}

		broker::ApprovalChallengeRequest request;
		request.requestId = broker::GenerateBrokerRequestId();
		request.requestedAccountSid = credential.accountSid;
		request.approvalAccountSid = approvalCredential.accountSid;
		request.executablePath = brokerExecutablePath;
		request.argumentTail = brokerArgumentTail;
		request.workingDirectory = brokerWorkingDirectory;
		request.elevationMode = elevationMode;
		request.visibility = visibility;
		if (request.requestId == 0)
		{
			ui::ReportError(localization::GetText(
				localization::TextId::BrokerLaunchRequestFailed) + accountContext,
				L"Generating the broker launch request identifier failed.");
			return BrokerLaunchOutcome::Failed;
		}

		const broker::PipeApprovalChallengeResponse challenge =
			broker::SubmitApprovalChallengeRequest(request,
				broker::ApprovalChallengeRequestTimeoutMilliseconds);
		if (challenge.status != broker::PipeApprovalChallengeStatus::Succeeded)
		{
			ui::ReportError(localization::Format(
				localization::TextId::CredentialBrokerUnreachable,
				{ windows_error::FormatWindowsError(challenge.windowsError) }) +
				accountContext,
				L"Submitting the broker launch challenge failed. Windows error: " +
					std::to_wstring(challenge.windowsError));
			return BrokerLaunchOutcome::Failed;
		}
		if (challenge.result.status != broker::ApprovalChallengeStatus::Issued)
		{
			ui::ReportError(localization::Format(
				localization::TextId::BrokerFailureWithError,
				{ credential_formatting::GetBrokerChallengeFailureMessage(challenge.result),
				  windows_error::FormatWindowsError(challenge.result.windowsError) }) +
				accountContext,
				L"The broker rejected the launch challenge. Status: " +
					std::to_wstring(static_cast<unsigned int>(challenge.result.status)) +
					L"; Windows error: " +
					std::to_wstring(challenge.result.windowsError));
			return BrokerLaunchOutcome::Failed;
		}

		broker::ApprovalConfirmationRequest confirmation;
		confirmation.requestId = broker::GenerateBrokerRequestId();
		confirmation.challengeId = challenge.result.challengeId;
		confirmation.decision = broker::ApprovalDecision::ApproveOnce;
		if (challenge.result.userConfirmationRequired)
		{
			DWORD helperError = NO_ERROR;
			HANDLE helperProcess = nullptr;
			if (challenge.result.helperStartMode ==
				broker::ApprovalHelperStartMode::BrokerStarted)
			{
				helperProcess = reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(
					challenge.result.approvalHelperProcessHandle));
			}
			else if (challenge.result.helperStartMode ==
				broker::ApprovalHelperStartMode::ClientRunAsRequired)
			{
				if (!LaunchApprovalHelperWithRunAs(
					challenge.result.challengeId, helperProcess, helperError))
				{
					if (helperError != ERROR_CANCELLED)
					{
						ui::ReportError(localization::Format(
							localization::TextId::ApprovalHelperFailed,
							{ windows_error::FormatWindowsError(helperError) }) +
							accountContext,
							L"Elevating the approval helper failed. Windows error: " +
								std::to_wstring(helperError));
					}
					return helperError == ERROR_CANCELLED
						? BrokerLaunchOutcome::Cancelled : BrokerLaunchOutcome::Failed;
				}
			}
			if (!WaitForApprovalHelper(helperProcess, helperError))
			{
				if (helperError != ERROR_CANCELLED)
				{
					ui::ReportError(localization::Format(
						localization::TextId::ApprovalHelperFailed,
						{ windows_error::FormatWindowsError(helperError) }) +
						accountContext,
						L"The approval helper failed. Windows error: " +
							std::to_wstring(helperError));
				}
				return helperError == ERROR_CANCELLED
					? BrokerLaunchOutcome::Cancelled : BrokerLaunchOutcome::Failed;
			}
		}
		if (confirmation.requestId == 0)
		{
			ui::ReportError(localization::GetText(
				localization::TextId::BrokerConfirmationRequestFailed) + accountContext,
				L"Generating the broker confirmation request identifier failed.");
			return BrokerLaunchOutcome::Failed;
		}
		const broker::PipeApprovalConfirmationResponse response =
			broker::SubmitApprovalConfirmationRequest(confirmation,
				elevationMode == broker::ElevationMode::RequireElevated
					? broker::ApprovalConfirmationRequestTimeoutMilliseconds
					: broker::DefaultBrokerRequestTimeoutMilliseconds);
		if (response.status != broker::PipeApprovalConfirmationStatus::Succeeded)
		{
			ui::ReportError(localization::Format(
				localization::TextId::BrokerConfirmationTransportFailed,
				{ windows_error::FormatWindowsError(response.windowsError) }) +
				accountContext,
				L"Submitting the broker launch confirmation failed. Windows error: " +
					std::to_wstring(response.windowsError));
			return BrokerLaunchOutcome::Failed;
		}
		if (response.result.status == broker::ApprovalConfirmationStatus::Denied &&
			response.result.windowsError == ERROR_CANCELLED)
		{
			return BrokerLaunchOutcome::Cancelled;
		}
		if (response.result.status != broker::ApprovalConfirmationStatus::Confirmed ||
			response.result.processId == 0)
		{
			ui::ReportError(localization::Format(
				localization::TextId::BrokerFailureWithError,
				{ credential_formatting::GetBrokerLaunchFailureMessage(response.result),
				  windows_error::FormatWindowsError(response.result.windowsError) }) +
				accountContext,
				L"The broker did not confirm the launch. Status: " +
					std::to_wstring(static_cast<unsigned int>(response.result.status)) +
					L"; Windows error: " +
					std::to_wstring(response.result.windowsError));
			return BrokerLaunchOutcome::Failed;
		}
		client_logging::Write(L"Ok", L"Broker launch confirmed; process ID " +
			std::to_wstring(response.result.processId));
		if (visibility != broker::LaunchVisibility::Normal ||
			launch_detection::IsExplicitlyHiddenPowerShellLaunch(
				brokerExecutablePath, brokerArgumentTail))
			client_logging::Write(L"Info", L"Skipped window activation for hidden launch");
		else
			window_activation::ActivateBrokerProcessWindow(response.result.processId);
		return BrokerLaunchOutcome::Succeeded;
	}
}