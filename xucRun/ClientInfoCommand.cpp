// Purpose: Implements the public client information command.
// Approach: Implements the behavior with focused helpers built on shared xucRun interfaces and Windows APIs.
// Behavior: Collects read-only diagnostics and emits them through the shared text output path.
#include "ClientInfoCommand.h"

#include <Windows.h>

#include <string>
#include <vector>

#include "BrokerContracts.h"
#include "BrokerPipeClient.h"
#include "BuildVersion.h"
#include "ClientAccountIdentity.h"
#include "ClientBrokerCredentialSelection.h"
#include "ClientBrokerStatus.h"
#include "ClientCredentialInfo.h"
#include "ClientImageTrust.h"
#include "ClientTextDialog.h"
#include "ClientUiCommon.h"
#include "ClientWindowsError.h"
#include "DocumentationNameMapping.h"
#include "Localization.h"

namespace xucrun::info_command
{
	namespace
	{
		std::wstring GetExecutablePath()
		{
			std::vector<wchar_t> buffer(512);
			for (;;)
			{
				const DWORD length = GetModuleFileNameW(
					nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
				if (length == 0)
					return localization::GetText(localization::TextId::ValueNotAvailable);
				if (length < buffer.size() - 1)
					return std::wstring(buffer.data(), length);
				buffer.resize(buffer.size() * 2);
			}
		}

		std::wstring GetConfigurationPath()
		{
			wchar_t programFiles[MAX_PATH] = {};
			const DWORD length = GetEnvironmentVariableW(
				L"ProgramFiles", programFiles, ARRAYSIZE(programFiles));
			if (length == 0 || length >= ARRAYSIZE(programFiles))
				return localization::GetText(localization::TextId::ValueNotAvailable);
			return std::wstring(programFiles) + L"\\xucRun\\xucRun.ini";
		}

		std::wstring GetVersionResourceString(const std::wstring& path,
			const wchar_t* valueName)
		{
			DWORD ignored = 0;
			const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &ignored);
			if (size == 0)
				return {};
			std::vector<BYTE> versionInfo(size);
			if (!GetFileVersionInfoW(
				path.c_str(), 0, size, versionInfo.data()))
			{
				return {};
			}

			struct LanguageAndCodePage
			{
				WORD language;
				WORD codePage;
			};
			LanguageAndCodePage* translations = nullptr;
			UINT translationsSize = 0;
			if (!VerQueryValueW(versionInfo.data(), L"\\VarFileInfo\\Translation",
				reinterpret_cast<void**>(&translations), &translationsSize))
			{
				return {};
			}

			const UINT translationCount =
				translationsSize / sizeof(LanguageAndCodePage);
			for (UINT index = 0; index < translationCount; ++index)
			{
				wchar_t query[128] = {};
				if (swprintf_s(query,
					L"\\StringFileInfo\\%04x%04x\\%s",
					translations[index].language,
					translations[index].codePage,
					valueName) < 0)
				{
					continue;
				}
				wchar_t* value = nullptr;
				UINT valueLength = 0;
				if (VerQueryValueW(versionInfo.data(), query,
					reinterpret_cast<void**>(&value), &valueLength) &&
					value && valueLength > 1)
				{
					return value;
				}
			}
			return {};
		}

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

		std::wstring GetElevationText()
		{
			HANDLE token = nullptr;
			if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
				return localization::GetText(localization::TextId::ValueNotAvailable);
			TOKEN_ELEVATION elevation = {};
			DWORD returnedLength = 0;
			const bool available = GetTokenInformation(token, TokenElevation,
				&elevation, sizeof(elevation), &returnedLength) != FALSE;
			CloseHandle(token);
			if (!available)
				return localization::GetText(localization::TextId::ValueNotAvailable);
			return localization::GetText(elevation.TokenIsElevated
				? localization::TextId::InfoElevated
				: localization::TextId::InfoNotElevated);
		}
	}

	int ShowInfo()
	{
		const std::wstring unavailable = localization::GetText(
			localization::TextId::ValueNotAvailable);
		std::wstring username;
		std::wstring domain;
		const std::wstring account = account_identity::GetCurrentUserInfo(username, domain)
			? (domain.empty() ? username : domain + L"\\" + username)
			: unavailable;

		const std::wstring configurationPath = GetConfigurationPath();
		wchar_t configuredLogDirectory[32768] = {};
		wchar_t brokerInformationEvents[16] = {};
		if (configurationPath != unavailable)
		{
			GetPrivateProfileStringW(L"Logging", L"ClientLogDirectory", L"",
				configuredLogDirectory, ARRAYSIZE(configuredLogDirectory),
				configurationPath.c_str());
			GetPrivateProfileStringW(L"Logging", L"BrokerInformationEvents", L"false",
				brokerInformationEvents, ARRAYSIZE(brokerInformationEvents),
				configurationPath.c_str());
		}
		const std::wstring clientLogging = configuredLogDirectory[0]
			? configuredLogDirectory
			: localization::GetText(localization::TextId::StatusDisabled);
		const bool brokerInformationEnabled =
			_wcsicmp(brokerInformationEvents, L"true") == 0;
		const std::wstring executablePath = GetExecutablePath();
		std::wstring copyright = executablePath == unavailable
			? std::wstring{}
			: GetVersionResourceString(executablePath, L"LegalCopyright");
		if (copyright.empty())
			copyright = unavailable;
		LONG signerTrustStatus = TRUST_E_NOSIGNATURE;
		std::vector<BYTE> signerCertificate;
		image_trust::SignerCertificateInfo signerInfo;
		const bool signerInfoAvailable = executablePath != unavailable &&
			image_trust::VerifyWithSignerCertificate(
				executablePath, signerTrustStatus, signerCertificate) &&
			image_trust::GetSignerCertificateInfo(signerCertificate, signerInfo);

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

		const std::wstring content = localization::Format(
			localization::TextId::InfoContent,
			{ std::to_wstring(VERSION_MAJOR) + L"." +
					std::to_wstring(VERSION_MINOR) + L"." +
					std::to_wstring(VERSION_PATCH) + L"." +
					std::to_wstring(BUILD_NUMBER),
				copyright,
				executablePath,
				L"x64",
				localization::GetText(localization::UsesGermanResources()
					? localization::TextId::InfoLanguageGerman
					: localization::TextId::InfoLanguageEnglish),
				signerInfoAvailable ? signerInfo.subject : unavailable,
				signerInfoAvailable ? signerInfo.sha256Fingerprint : unavailable,
				signerInfoAvailable ? signerInfo.validFrom : unavailable,
				signerInfoAvailable ? signerInfo.validUntil : unavailable,
				GetServiceStatusText(serviceState),
				brokerCommunication,
				brokerAvailable ? FormatVersion(health.brokerVersion) : unavailable,
				documentation_names::DisplayAccount(account),
				GetElevationText(),
				configurationPath,
				clientLogging,
				localization::GetText(brokerInformationEnabled
					? localization::TextId::InfoEnabled
					: localization::TextId::StatusDisabled),
				credentialCount,
				approvalCount });
		text_dialog::Show(
			ui::GetWindowTitle(localization::GetText(localization::TextId::InfoTitle)),
			localization::GetText(localization::TextId::InfoHeading),
			content, ui::GetDialogOwnerWindow());
		return 0;
	}
}