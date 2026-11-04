// Purpose: Implements the public client information command.
// Approach: Implements the behavior with focused helpers built on shared xucRun interfaces and Windows APIs.
// Behavior: Collects read-only diagnostics and emits them through the shared text output path.
#include "ClientInfoCommand.h"

#include <Windows.h>

#include <string>
#include <vector>

#include "BrokerContracts.h"
#include "BrokerPipeClient.h"
#include "ClientBrokerCredentialSelection.h"
#include "ClientBrokerStatus.h"
#include "ClientCredentialInfo.h"
#include "ClientRuntimeInfo.h"
#include "ClientTextDialog.h"
#include "ClientUiCommon.h"
#include "ClientWindowsError.h"
#include "Localization.h"

namespace xucrun::info_command
{
	namespace
	{
		std::wstring FormatVersion(const broker::ProductVersion& version)
		{
			return std::to_wstring(version.major) + L"." +
				std::to_wstring(version.minor) + L"." +
				std::to_wstring(version.patch) + L"." +
				std::to_wstring(version.build);
		}

		std::wstring GetServiceStatusText(
			const broker_status::BrokerServiceState& state)
		{
			using broker_status::BrokerServiceStatus;
			switch (state.status)
			{
			case BrokerServiceStatus::NotInstalled:
				return localization::GetText(localization::TextId::StatusNotInstalled);
			case BrokerServiceStatus::Stopped:
				return localization::GetText(localization::TextId::StatusStopped);
			case BrokerServiceStatus::Disabled:
				return localization::GetText(localization::TextId::StatusDisabled);
			case BrokerServiceStatus::Starting:
				return localization::GetText(localization::TextId::StatusStarting);
			case BrokerServiceStatus::Stopping:
				return localization::GetText(localization::TextId::StatusStopping);
			case BrokerServiceStatus::Running:
				return localization::GetText(localization::TextId::StatusRunning);
			default:
				return state.error == NO_ERROR
					? localization::GetText(localization::TextId::StatusUnavailable)
					: localization::Format(
						localization::TextId::StatusUnavailableWithError,
						{ windows_error::FormatWindowsError(state.error) });
			}
		}

	}

	int ShowInfo()
	{
		const std::wstring unavailable = localization::GetText(
			localization::TextId::ValueNotAvailable);
		const runtime_info::RuntimeInfo commonInfo = runtime_info::Collect();
		wchar_t brokerInformationEvents[16] = {};
		if (commonInfo.configurationPath != unavailable)
		{
			GetPrivateProfileStringW(L"Logging", L"BrokerInformationEvents", L"false",
				brokerInformationEvents, ARRAYSIZE(brokerInformationEvents),
				commonInfo.configurationPath.c_str());
		}
		const bool brokerInformationEnabled =
			_wcsicmp(brokerInformationEvents, L"true") == 0;

		const broker_status::BrokerServiceState serviceState =
			broker_status::QueryBrokerServiceState();
		const broker::PipeHealthCheck health = serviceState.status ==
			broker_status::BrokerServiceStatus::Running
			? broker::QueryBrokerPipeHealth(1000)
			: broker::PipeHealthCheck{};
		const bool brokerAvailable = health.status == broker::PipeHealthStatus::Succeeded;
		const std::wstring brokerCommunication = brokerAvailable
			? localization::GetText(localization::TextId::ConnectionActive)
			: (health.windowsError == NO_ERROR
				? localization::GetText(localization::TextId::StatusUnavailable)
				: localization::Format(
					localization::TextId::StatusUnavailableWithError,
					{ windows_error::FormatWindowsError(health.windowsError) }));
		std::wstring credentialCount = unavailable;
		std::wstring approvalCount = unavailable;
		if (brokerAvailable)
		{
			std::vector<CredentialInfo> credentials;
			DWORD credentialError = NO_ERROR;
			if (broker_credential_selection::GetAllBrokerCredentials(
				credentials, credentialError))
			{
				credentialCount = std::to_wstring(credentials.size());
			}
			broker::ApprovalListRequest request;
			request.requestId = broker::GenerateBrokerRequestId();
			if (request.requestId != 0)
			{
				const auto response = broker::SubmitApprovalListRequest(request);
				if (response.status == broker::PipeApprovalManagementStatus::Succeeded &&
					response.result.status == broker::ApprovalManagementStatus::Succeeded)
				{
					approvalCount = std::to_wstring(response.result.entries.size());
				}
			}
		}

		const std::wstring content = runtime_info::Format(commonInfo) +
			localization::Format(localization::TextId::InfoContent,
				{
				GetServiceStatusText(serviceState),
				brokerCommunication,
				brokerAvailable ? FormatVersion(health.brokerVersion) : unavailable,
				localization::GetText(brokerInformationEnabled
					? localization::TextId::InfoEnabled
					: localization::TextId::StatusDisabled),
				credentialCount,
				approvalCount
				});
		text_dialog::Show(
			ui::GetComputerContextWindowTitle(
				localization::GetText(localization::TextId::InfoTitle)),
			localization::GetText(localization::TextId::InfoHeading),
			content, ui::GetDialogOwnerWindow());
		return 0;
	}
}