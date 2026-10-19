// Purpose: Implements internal end-to-end probes for broker protocol and authorization behavior.
#include "ClientInternalBrokerTests.h"

#include <Windows.h>

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <string>
#include <vector>

#include "BrokerContracts.h"
#include "BrokerPipeClient.h"
#include "ClientAccountIdentity.h"
#include "ClientBrokerLaunch.h"
#include "ClientCredentialPrompt.h"
#include "ClientSensitiveString.h"
#include "Localization.h"

namespace
{
	constexpr const wchar_t* PROGRAM_EXE_NAME = L"xucRun.exe";
	constexpr const wchar_t* PRODUCTION_TEST_ARGUMENT_TAIL =
		L"/d /k \"title xucRun Broker Production PreserveToken Test & "
		L"echo Target identity: & whoami & echo Argument binding: alpha ^& beta\"";
}

namespace xucrun::internal_broker_tests
{
	std::optional<int> HandleInternalBrokerTestCommand(
		const std::wstring& firstArg, int argc, wchar_t* argv[])
	{
		if (firstArg == L"/internal-broker-save-gate-test")
		{
			broker::CredentialSaveRequest request;
			request.requestId = broker::GenerateBrokerRequestId();
			if (!account_identity::GetCurrentUserSID(request.requestedAccountSid))
				return 28;
			request.password.assign(L"diagnostic-not-a-password", 25);
			const broker::PipeCredentialSaveResponse response =
				broker::SubmitCredentialSaveRequest(request);
			request.password.clear();
			return response.status == broker::PipeCredentialSaveStatus::Succeeded &&
				response.result.status == broker::CredentialSaveStatus::InvalidCredentials &&
				response.result.windowsError != NO_ERROR ? 0 : 28;
		}

		if (firstArg == L"/internal-broker-tracked-process-registration-gate-test")
		{
			broker::SecurityAuditRequest auditRequest;
			auditRequest.requestId = broker::GenerateBrokerRequestId();
			auditRequest.language = localization::UsesGermanResources()
				? broker::SecurityAuditRequest::Language::German
				: broker::SecurityAuditRequest::Language::English;
			const auto auditResponse = broker::SubmitSecurityAuditRequest(auditRequest);
			broker::TrackedProcessRegistrationRequest request;
			request.requestId = broker::GenerateBrokerRequestId();
			request.processHandle = static_cast<std::uint64_t>(
				reinterpret_cast<ULONG_PTR>(GetCurrentProcess()));
			request.processId = GetCurrentProcessId() + 1;
			const auto response = broker::SubmitTrackedProcessRegistrationRequest(request);
			return auditResponse.status == broker::PipeSecurityAuditStatus::Succeeded &&
				auditResponse.result.status == broker::SecurityAuditStatus::Succeeded &&
				response.status == broker::PipeTrackedProcessRegistrationStatus::Succeeded &&
				response.result.status == broker::TrackedProcessRegistrationStatus::InvalidRequest &&
				response.result.windowsError == ERROR_PROCESS_ABORTED ? 0 : 1;
		}

		if (firstArg == L"/internal-broker-tracked-process-list-test")
		{
			broker::TrackedProcessListRequest request;
			request.requestId = broker::GenerateBrokerRequestId();
			const auto response = broker::SubmitTrackedProcessListRequest(request);
			return response.status == broker::PipeTrackedProcessListStatus::Succeeded &&
				response.result.status == broker::CredentialListStatus::Succeeded &&
				response.result.windowsError == NO_ERROR &&
				response.result.entries.size() <= broker::MaximumTrackedProcessEntries ? 0 : 1;
		}

		if (firstArg == L"/internal-broker-credential-delete-gate-test")
		{
			broker::CredentialListRequest listRequest;
			listRequest.requestId = broker::GenerateBrokerRequestId();
			const auto listResponse = broker::SubmitCredentialListRequest(listRequest);
			broker::CredentialDeleteRequest request;
			request.requestId = broker::GenerateBrokerRequestId();
			request.requestedAccountSid = L"not-a-sid";
			const auto response = broker::SubmitCredentialDeleteRequest(request);
			return listResponse.status == broker::PipeCredentialListStatus::Succeeded &&
				listResponse.result.status == broker::CredentialListStatus::Succeeded &&
				response.status == broker::PipeCredentialDeleteStatus::Succeeded &&
				response.result.status == broker::CredentialDeleteStatus::InvalidRequest &&
				response.result.windowsError != NO_ERROR &&
				response.result.windowsError != ERROR_ACCESS_DENIED ? 0 : 1;
		}

		if (firstArg == L"/internal-broker-credential-use-gate-test")
		{
			broker::CredentialUseTestRequest request;
			request.requestId = broker::GenerateBrokerRequestId();
			request.requestedAccountSid = L"not-a-sid";
			const auto response = broker::SubmitCredentialUseTestRequest(request);
			return response.status == broker::PipeCredentialUseTestStatus::Succeeded &&
				response.result.status == broker::CredentialUseTestStatus::InvalidRequest &&
				response.result.windowsError == ERROR_INVALID_SID ? 0 : 1;
		}

		if (firstArg == L"/internal-broker-approval-reset-gate-test")
		{
			broker::ApprovalListRequest listRequest;
			listRequest.requestId = broker::GenerateBrokerRequestId();
			const auto listResponse = broker::SubmitApprovalListRequest(listRequest);
			broker::ApprovalResetRequest request;
			request.requestId = broker::GenerateBrokerRequestId();
			request.approval.programScope = approval::BrokerApprovalProgramScope::General;
			request.approval.accountScope = approval::BrokerApprovalAccountScope::AllAccounts;
			request.approval.lifetime = approval::BrokerApprovalLifetime::Forever;
			request.approval.createdAt = 1;
			request.approval.ownerUserSid = L"S-1-0-0";
			const auto response = broker::SubmitApprovalResetRequest(request);
			return listResponse.status == broker::PipeApprovalManagementStatus::Succeeded &&
				listResponse.result.status == broker::ApprovalManagementStatus::Succeeded &&
				response.status == broker::PipeApprovalManagementStatus::Succeeded &&
				response.result.status == broker::ApprovalManagementStatus::InvalidRequest &&
				response.result.windowsError == ERROR_ACCESS_DENIED &&
				response.result.deletedCount == 0 ? 0 : 1;
		}

		if (firstArg == L"/internal-broker-approval-list-test")
		{
			broker::ApprovalListRequest request;
			request.requestId = broker::GenerateBrokerRequestId();
			const auto response = broker::SubmitApprovalListRequest(request);
			return response.status == broker::PipeApprovalManagementStatus::Succeeded &&
				response.result.status == broker::ApprovalManagementStatus::Succeeded &&
				response.result.windowsError == NO_ERROR ? 0 : 1;
		}

		if (firstArg == L"/internal-broker-credential-list-test")
		{
			broker::CredentialListRequest request;
			request.requestId = broker::GenerateBrokerRequestId();
			const auto response = broker::SubmitCredentialListRequest(request);
			return response.status == broker::PipeCredentialListStatus::Succeeded &&
				response.result.status == broker::CredentialListStatus::Succeeded &&
				response.result.windowsError == NO_ERROR ? 0 : 1;
		}

		if (firstArg == L"/internal-broker-security-audit-test")
		{
			broker::SecurityAuditRequest request;
			request.requestId = broker::GenerateBrokerRequestId();
			const auto response = broker::SubmitSecurityAuditRequest(request);
			return response.status == broker::PipeSecurityAuditStatus::Succeeded &&
				response.result.status == broker::SecurityAuditStatus::Succeeded &&
				response.result.windowsError == NO_ERROR && response.result.findingCount == 0 &&
				!response.result.report.empty() ? 0 : 1;
		}

		if (firstArg == L"/internal-broker-production-challenge-test")
		{
			std::wstring accountSid;
			wchar_t systemDirectory[MAX_PATH] = {};
			const UINT directoryLength = GetSystemDirectoryW(
				systemDirectory, static_cast<UINT>(std::size(systemDirectory)));
			if (!account_identity::GetCurrentUserSID(accountSid) || directoryLength == 0 ||
				directoryLength >= std::size(systemDirectory))
				return 28;

			broker::ApprovalChallengeRequest request;
			request.requestId = broker::GenerateBrokerRequestId();
			request.requestedAccountSid = accountSid;
			request.executablePath = std::wstring(systemDirectory) + L"\\cmd.exe";
			request.argumentTail = PRODUCTION_TEST_ARGUMENT_TAIL;
			request.workingDirectory = systemDirectory;
			const broker::PipeApprovalChallengeResponse challenge =
				broker::SubmitApprovalChallengeRequest(request,
					broker::DefaultBrokerRequestTimeoutMilliseconds);
			if (challenge.status != broker::PipeApprovalChallengeStatus::Succeeded ||
				challenge.result.status != broker::ApprovalChallengeStatus::Issued ||
				challenge.result.windowsError != NO_ERROR)
				return 28;

			broker::ApprovalConfirmationRequest confirmation;
			confirmation.requestId = broker::GenerateBrokerRequestId();
			confirmation.challengeId = challenge.result.challengeId;
			confirmation.decision = broker::ApprovalDecision::Deny;
			const broker::PipeApprovalConfirmationResponse denied =
				broker::SubmitApprovalConfirmationRequest(confirmation,
					broker::DefaultBrokerRequestTimeoutMilliseconds);
			return denied.status == broker::PipeApprovalConfirmationStatus::Succeeded &&
				denied.result.status == broker::ApprovalConfirmationStatus::Denied &&
				denied.result.windowsError == ERROR_CANCELLED && denied.result.processId == 0
				? 0 : 28;
		}

		if (firstArg == L"/internal-broker-save-test" ||
			firstArg == L"/internal-broker-production-launch-test")
		{
			const bool productionLaunchTest =
				firstArg == L"/internal-broker-production-launch-test";
			std::wstring username;
			SecureString password;
			const std::wstring credentialTestPrompt = productionLaunchTest
				? localization::GetText(localization::TextId::CredentialTestProductionPrompt)
				: localization::GetText(localization::TextId::CredentialTestValidationPrompt);
			const std::wstring credentialTestTitle =
				localization::GetText(localization::TextId::CredentialTestTitle);
			if (!credential_prompt::PromptForCredentials(L"", username, password, nullptr,
				credentialTestPrompt.c_str(), credentialTestTitle.c_str()))
				return 28;

			std::wstring canonicalName;
			std::wstring accountSid;
			if (!account_identity::ResolveAccountIdentity(username, canonicalName, accountSid))
			{
				password.clear();
				return 28;
			}

			broker::CredentialSaveRequest request;
			request.requestId = broker::GenerateBrokerRequestId();
			request.requestedAccountSid = accountSid;
			const bool assigned = request.password.assign(password.c_str(), password.size());
			password.clear();
			if (!assigned)
				return 28;
			const broker::PipeCredentialSaveResponse response =
				broker::SubmitCredentialSaveRequest(request);
			request.password.clear();
			if (response.status != broker::PipeCredentialSaveStatus::Succeeded ||
				response.result.status != broker::CredentialSaveStatus::Succeeded ||
				response.result.windowsError != NO_ERROR)
				return 28;

			wchar_t systemDirectory[MAX_PATH] = {};
			const UINT directoryLength = GetSystemDirectoryW(
				systemDirectory, static_cast<UINT>(std::size(systemDirectory)));
			broker::PipeApprovalConfirmationResponse confirmationResponse;
			broker::PipeCredentialUseTestResponse useResponse;
			if (!productionLaunchTest)
			{
				broker::CredentialUseTestRequest useRequest;
				useRequest.requestId = broker::GenerateBrokerRequestId();
				useRequest.requestedAccountSid = accountSid;
				useResponse = broker::SubmitCredentialUseTestRequest(useRequest);
			}
			else if (directoryLength != 0 && directoryLength < std::size(systemDirectory))
			{
				broker::ApprovalChallengeRequest challenge;
				challenge.requestId = broker::GenerateBrokerRequestId();
				challenge.requestedAccountSid = accountSid;
				challenge.executablePath = std::wstring(systemDirectory) + L"\\cmd.exe";
				challenge.argumentTail = productionLaunchTest
					? PRODUCTION_TEST_ARGUMENT_TAIL
					: broker::ApprovalDiagnosticArgumentTail;
				challenge.workingDirectory = systemDirectory;
				const broker::PipeApprovalChallengeResponse challengeResponse =
					broker::SubmitApprovalChallengeRequest(challenge,
						broker::DefaultBrokerRequestTimeoutMilliseconds);
				if (challengeResponse.status == broker::PipeApprovalChallengeStatus::Succeeded &&
					challengeResponse.result.status == broker::ApprovalChallengeStatus::Issued)
				{
					broker::ApprovalConfirmationRequest confirmation;
					confirmation.requestId = broker::GenerateBrokerRequestId();
					confirmation.challengeId = challengeResponse.result.challengeId;
					confirmation.decision = broker::ApprovalDecision::ApproveOnce;
					if (challengeResponse.result.userConfirmationRequired)
					{
						DWORD helperError = NO_ERROR;
						broker_launch::WaitForApprovalHelper(
							reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(
								challengeResponse.result.approvalHelperProcessHandle)),
							helperError);
					}
					confirmationResponse =
						broker::SubmitApprovalConfirmationRequest(confirmation,
							broker::DefaultBrokerRequestTimeoutMilliseconds);
				}
			}

			broker::CredentialDeleteRequest deleteRequest;
			deleteRequest.requestId = broker::GenerateBrokerRequestId();
			deleteRequest.requestedAccountSid = accountSid;
			const broker::PipeCredentialDeleteResponse deleteResponse =
				broker::SubmitCredentialDeleteRequest(deleteRequest);
			const bool launchSucceeded = productionLaunchTest
				? confirmationResponse.status == broker::PipeApprovalConfirmationStatus::Succeeded &&
					confirmationResponse.result.status ==
						broker::ApprovalConfirmationStatus::Confirmed &&
					confirmationResponse.result.windowsError == NO_ERROR &&
					confirmationResponse.result.processId != 0
				: useResponse.status == broker::PipeCredentialUseTestStatus::Succeeded &&
					useResponse.result.status == broker::CredentialUseTestStatus::Succeeded &&
					useResponse.result.windowsError == NO_ERROR &&
					useResponse.result.processId != 0;
			return launchSucceeded &&
				deleteResponse.status == broker::PipeCredentialDeleteStatus::Succeeded &&
				deleteResponse.result.status == broker::CredentialDeleteStatus::Succeeded &&
				deleteResponse.result.windowsError == NO_ERROR ? 0 : 28;
		}

		if (firstArg == L"/internal-broker-delete-test")
		{
			std::wstring accountSid;
			if (argc >= 3)
			{
				std::wstring canonicalName;
				if (!account_identity::ResolveAccountIdentity(argv[2], canonicalName, accountSid))
					return 28;
			}
			else if (!account_identity::GetCurrentUserSID(accountSid))
				return 28;

			broker::CredentialDeleteRequest request;
			request.requestId = broker::GenerateBrokerRequestId();
			request.requestedAccountSid = accountSid;
			const broker::PipeCredentialDeleteResponse response =
				broker::SubmitCredentialDeleteRequest(request);
			return response.status == broker::PipeCredentialDeleteStatus::Succeeded &&
				(response.result.status == broker::CredentialDeleteStatus::Succeeded ||
					response.result.status == broker::CredentialDeleteStatus::NotFound) ? 0 : 28;
		}

		if (firstArg == L"/internal-broker-challenge-test")
		{
			std::wstring accountSid;
			wchar_t systemDirectory[MAX_PATH] = {};
			const UINT directoryLength = GetSystemDirectoryW(
				systemDirectory, static_cast<UINT>(std::size(systemDirectory)));
			if (!account_identity::GetCurrentUserSID(accountSid) || directoryLength == 0 ||
				directoryLength >= std::size(systemDirectory))
				return 28;
			broker::ApprovalChallengeRequest request;
			request.requestId = broker::GenerateBrokerRequestId();
			request.requestedAccountSid = accountSid;
			request.executablePath = std::wstring(systemDirectory) + L"\\cmd.exe";
			request.argumentTail = L"/c whoami";
			request.workingDirectory = systemDirectory;
			const broker::PipeApprovalChallengeResponse rejectedResponse =
				broker::SubmitApprovalChallengeRequest(request,
					broker::DefaultBrokerRequestTimeoutMilliseconds);
			if (rejectedResponse.status != broker::PipeApprovalChallengeStatus::Succeeded ||
				rejectedResponse.result.status != broker::ApprovalChallengeStatus::InvalidRequest ||
				rejectedResponse.result.windowsError != ERROR_NOT_SUPPORTED ||
				std::any_of(rejectedResponse.result.challengeId.begin(),
					rejectedResponse.result.challengeId.end(),
					[](std::uint8_t value) { return value != 0; }))
				return 28;

			request.requestId = broker::GenerateBrokerRequestId();
			request.argumentTail = broker::ApprovalDiagnosticArgumentTail;
			const broker::PipeApprovalChallengeResponse response =
				broker::SubmitApprovalChallengeRequest(request,
					broker::DefaultBrokerRequestTimeoutMilliseconds);
			if (response.status != broker::PipeApprovalChallengeStatus::Succeeded ||
				response.result.status != broker::ApprovalChallengeStatus::Issued ||
				response.result.windowsError != NO_ERROR)
				return 28;

			broker::ApprovalConfirmationRequest confirmation;
			confirmation.requestId = broker::GenerateBrokerRequestId();
			confirmation.challengeId = response.result.challengeId;
			confirmation.decision = broker::ApprovalDecision::Deny;
			const broker::PipeApprovalConfirmationResponse confirmationResponse =
				broker::SubmitApprovalConfirmationRequest(confirmation,
					broker::DefaultBrokerRequestTimeoutMilliseconds);
			confirmation.requestId = broker::GenerateBrokerRequestId();
			const broker::PipeApprovalConfirmationResponse replayResponse =
				broker::SubmitApprovalConfirmationRequest(confirmation,
					broker::DefaultBrokerRequestTimeoutMilliseconds);
			return confirmationResponse.status == broker::PipeApprovalConfirmationStatus::Succeeded &&
				confirmationResponse.result.status == broker::ApprovalConfirmationStatus::Denied &&
				confirmationResponse.result.windowsError == ERROR_ACCESS_DENIED &&
				replayResponse.status == broker::PipeApprovalConfirmationStatus::Succeeded &&
				replayResponse.result.status == broker::ApprovalConfirmationStatus::InvalidRequest &&
				replayResponse.result.windowsError == ERROR_NOT_FOUND ? 0 : 28;
		}

		if (firstArg == L"/internal-broker-challenge-expiry-test")
		{
			std::wstring accountSid;
			wchar_t systemDirectory[MAX_PATH] = {};
			const UINT directoryLength = GetSystemDirectoryW(
				systemDirectory, static_cast<UINT>(std::size(systemDirectory)));
			if (!account_identity::GetCurrentUserSID(accountSid) || directoryLength == 0 ||
				directoryLength >= std::size(systemDirectory))
				return 28;

			broker::ApprovalChallengeRequest request;
			request.requestId = broker::GenerateBrokerRequestId();
			request.requestedAccountSid = accountSid;
			request.executablePath = std::wstring(systemDirectory) + L"\\cmd.exe";
			request.argumentTail = broker::ApprovalDiagnosticArgumentTail;
			request.workingDirectory = systemDirectory;
			const broker::PipeApprovalChallengeResponse challenge =
				broker::SubmitApprovalChallengeRequest(request,
					broker::DefaultBrokerRequestTimeoutMilliseconds);
			if (challenge.status != broker::PipeApprovalChallengeStatus::Succeeded ||
				challenge.result.status != broker::ApprovalChallengeStatus::Issued)
				return 28;

			Sleep(broker::ApprovalChallengeLifetimeMilliseconds + 1000);
			broker::ApprovalConfirmationRequest confirmation;
			confirmation.requestId = broker::GenerateBrokerRequestId();
			confirmation.challengeId = challenge.result.challengeId;
			confirmation.decision = broker::ApprovalDecision::ApproveOnce;
			const broker::PipeApprovalConfirmationResponse response =
				broker::SubmitApprovalConfirmationRequest(confirmation,
					broker::DefaultBrokerRequestTimeoutMilliseconds);
			return response.status == broker::PipeApprovalConfirmationStatus::Succeeded &&
				response.result.status == broker::ApprovalConfirmationStatus::Expired &&
				response.result.windowsError == ERROR_TIMEOUT && response.result.processId == 0
				? 0 : 28;
		}

		return std::nullopt;
	}
}