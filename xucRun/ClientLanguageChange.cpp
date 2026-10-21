// Purpose: Coordinates elevated language changes without granting Broker launch rights to the Tray.
#include "ClientLanguageChange.h"

#include <Windows.h>
#include <sddl.h>
#include <shellapi.h>

#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include "BrokerContracts.h"
#include "BrokerPipeClient.h"
#include "BuildVersion.h"
#include "ClientAccountIdentity.h"
#include "ClientBrokerCredentialSelection.h"
#include "ClientBrokerLaunch.h"
#include "ClientLogging.h"
#include "ClientCredentialInfo.h"
#include "ClientUiCommon.h"
#include "ClientWindowsError.h"
#include "Localization.h"

namespace xucrun::language_change
{
	namespace
	{
		bool GetExecutablePath(std::wstring& path)
		{
			std::vector<wchar_t> buffer(512);
			for (;;)
			{
				const DWORD length = GetModuleFileNameW(
					nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
				if (length == 0)
					return false;
				if (length < buffer.size() - 1)
				{
					path.assign(buffer.data(), length);
					return true;
				}
				buffer.resize(buffer.size() * 2);
			}
		}

		bool LaunchElevatedAndWait(const std::wstring& executablePath,
			const wchar_t* argument)
		{
			SHELLEXECUTEINFOW executeInfo = { sizeof(executeInfo) };
			executeInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
			executeInfo.hwnd = ui::GetDialogOwnerWindow();
			executeInfo.lpVerb = L"runas";
			executeInfo.lpFile = executablePath.c_str();
			executeInfo.lpParameters = argument;
			executeInfo.nShow = SW_HIDE;
			if (!ShellExecuteExW(&executeInfo))
				return false;

			const DWORD waitResult =
				WaitForSingleObject(executeInfo.hProcess, INFINITE);
			DWORD exitCode = ERROR_GEN_FAILURE;
			const bool succeeded = waitResult == WAIT_OBJECT_0 &&
				GetExitCodeProcess(executeInfo.hProcess, &exitCode) &&
				exitCode == ERROR_SUCCESS;
			CloseHandle(executeInfo.hProcess);
			return succeeded;
		}

		std::wstring CreatePipeToken()
		{
			const std::uint64_t first = broker::GenerateBrokerRequestId();
			const std::uint64_t second = broker::GenerateBrokerRequestId();
			if (first == 0 || second == 0)
				return {};
			std::wostringstream token;
			token << std::hex << std::nouppercase << std::setfill(L'0') <<
				std::setw(16) << first << std::setw(16) << second;
			return token.str();
		}

		bool CreateResultPipe(const std::wstring& pipeToken,
			HANDLE& pipe, OVERLAPPED& connection, DWORD& error)
		{
			pipe = INVALID_HANDLE_VALUE;
			connection = {};
			std::wstring currentUserSid;
			if (!account_identity::GetCurrentUserSID(currentUserSid))
			{
				error = GetLastError();
				return false;
			}
			const std::wstring descriptorText =
				L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GA;;;" + currentUserSid + L")";
			PSECURITY_DESCRIPTOR descriptor = nullptr;
			if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
				descriptorText.c_str(), SDDL_REVISION_1, &descriptor, nullptr))
			{
				error = GetLastError();
				return false;
			}
			SECURITY_ATTRIBUTES security = { sizeof(security), descriptor, FALSE };
			const std::wstring pipeName =
				std::wstring(broker::SetLanguagePipeNamePrefix) + pipeToken;
			pipe = CreateNamedPipeW(pipeName.c_str(),
				PIPE_ACCESS_INBOUND | FILE_FLAG_FIRST_PIPE_INSTANCE | FILE_FLAG_OVERLAPPED,
				PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT |
					PIPE_REJECT_REMOTE_CLIENTS,
				1, 0, sizeof(DWORD), 0, &security);
			LocalFree(descriptor);
			if (pipe == INVALID_HANDLE_VALUE)
			{
				error = GetLastError();
				return false;
			}
			connection.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
			if (!connection.hEvent)
			{
				error = GetLastError();
				CloseHandle(pipe);
				pipe = INVALID_HANDLE_VALUE;
				return false;
			}
			if (ConnectNamedPipe(pipe, &connection))
			{
				SetEvent(connection.hEvent);
				error = ERROR_SUCCESS;
				return true;
			}
			error = GetLastError();
			if (error == ERROR_IO_PENDING)
				return true;
			if (error == ERROR_PIPE_CONNECTED)
			{
				SetEvent(connection.hEvent);
				error = ERROR_SUCCESS;
				return true;
			}
			CloseHandle(connection.hEvent);
			connection.hEvent = nullptr;
			CloseHandle(pipe);
			pipe = INVALID_HANDLE_VALUE;
			return false;
		}

		void CloseResultPipe(HANDLE pipe, OVERLAPPED& connection)
		{
			if (pipe != INVALID_HANDLE_VALUE)
			{
				if (connection.hEvent && CancelIoEx(pipe, &connection))
					WaitForSingleObject(connection.hEvent, INFINITE);
				CloseHandle(pipe);
			}
			if (connection.hEvent)
				CloseHandle(connection.hEvent);
			connection = {};
		}

		bool ReceiveResult(HANDLE pipe, OVERLAPPED& connection,
			DWORD& result, DWORD& error)
		{
			error = ERROR_SUCCESS;
			const DWORD connectionWait =
				WaitForSingleObject(connection.hEvent, 15000);
			if (connectionWait != WAIT_OBJECT_0)
			{
				error = connectionWait == WAIT_TIMEOUT
					? ERROR_TIMEOUT : GetLastError();
				return false;
			}
			DWORD transferred = 0;
			if (!GetOverlappedResult(pipe, &connection, &transferred, FALSE))
			{
				error = GetLastError();
				return false;
			}

			OVERLAPPED read = {};
			read.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
			if (!read.hEvent)
			{
				error = GetLastError();
				return false;
			}
			bool readCompleted =
				ReadFile(pipe, &result, sizeof(result), nullptr, &read) != FALSE;
			if (!readCompleted && GetLastError() == ERROR_IO_PENDING)
			{
				const DWORD readWait = WaitForSingleObject(read.hEvent, 15000);
				if (readWait == WAIT_OBJECT_0)
				{
					readCompleted = GetOverlappedResult(
						pipe, &read, &transferred, FALSE) != FALSE;
				}
				else
				{
					error = readWait == WAIT_TIMEOUT
						? ERROR_TIMEOUT : GetLastError();
					if (CancelIoEx(pipe, &read))
						WaitForSingleObject(read.hEvent, INFINITE);
				}
			}
			else if (readCompleted)
			{
				readCompleted = GetOverlappedResult(
					pipe, &read, &transferred, TRUE) != FALSE;
			}
			else
			{
				error = GetLastError();
			}
			if (!readCompleted && error == ERROR_SUCCESS)
				error = GetLastError();
			CloseHandle(read.hEvent);
			if (!readCompleted)
				return false;
			if (transferred != sizeof(result))
			{
				error = ERROR_INVALID_DATA;
				return false;
			}
			error = ERROR_SUCCESS;
			return true;
		}

		void NotifyStatusIcons()
		{
			broker::StatusIconListRequest request;
			request.requestId = broker::GenerateBrokerRequestId();
			request.clientVersion = {
				VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH, BUILD_NUMBER
			};
			request.includeAllSessions = true;
			if (request.requestId == 0)
			{
				client_logging::Write(L"Warning",
					L"Generating the status icon language-change request ID failed.");
				return;
			}
			const auto response =
				broker::SubmitStatusIconListRequest(request, 1000);
			if (response.status != broker::PipeStatusIconListStatus::Succeeded ||
				response.result.status != broker::CredentialListStatus::Succeeded)
			{
				client_logging::Write(L"Warning",
					L"Querying registered status icons after the language change "
					L"failed. Windows error: " +
					std::to_wstring(response.windowsError != NO_ERROR
						? response.windowsError : response.result.windowsError) + L".");
				return;
			}

			std::size_t signaledCount = 0;
			for (const auto& icon : response.result.entries)
			{
				const std::wstring eventName =
					std::wstring(broker::StatusIconLanguageChangedEventNamePrefix) +
					std::to_wstring(icon.processId);
				HANDLE event = OpenEventW(EVENT_MODIFY_STATE, FALSE, eventName.c_str());
				if (!event)
				{
					client_logging::Write(L"Warning",
						L"Opening a status icon language-change event failed. PID " +
						std::to_wstring(icon.processId) + L"; Windows error: " +
						std::to_wstring(GetLastError()) + L".");
					continue;
				}
				const bool signaled = SetEvent(event) != FALSE;
				const DWORD error = signaled ? NO_ERROR : GetLastError();
				CloseHandle(event);
				if (signaled)
					++signaledCount;
				else
				{
					client_logging::Write(L"Warning",
						L"Signaling a status icon language change failed. PID " +
						std::to_wstring(icon.processId) + L"; Windows error: " +
						std::to_wstring(error) + L".");
				}
			}
			client_logging::Write(L"Info",
				L"Status icon language-change notifications sent: " +
				std::to_wstring(signaledCount) + L" of " +
				std::to_wstring(response.result.entries.size()) + L".");
		}

		bool LaunchElevatedAndNotifyStatusIcons(
			const std::wstring& executablePath, const wchar_t* argument)
		{
			const bool changed = LaunchElevatedAndWait(executablePath, argument);
			if (changed)
				NotifyStatusIcons();
			return changed;
		}
	}

	bool Request(const wchar_t* elevatedArgument)
	{
		std::wstring executablePath;
		if (!GetExecutablePath(executablePath))
		{
			ui::ReportError(localization::Format(
				localization::TextId::CommandLaunchFailed,
				{ elevatedArgument }),
				L"Resolving the xucRun client path for a language change failed.");
			return false;
		}
		if (account_identity::IsUserInAdminGroup())
			return LaunchElevatedAndNotifyStatusIcons(
				executablePath, elevatedArgument);

		CredentialInfo credential;
		DWORD error = ERROR_SUCCESS;
		const auto selection =
			broker_credential_selection::SelectBrokerCredential(
				L"", true, credential, error);
		if (selection == broker_credential_selection::
			BrokerCredentialSelectionOutcome::Cancelled)
		{
			return false;
		}
		if (selection == broker_credential_selection::
			BrokerCredentialSelectionOutcome::Failed)
		{
			ui::ReportError(localization::Format(
				localization::TextId::BrokerCredentialsQueryFailed,
				{ windows_error::FormatWindowsError(error) }),
				L"Querying administrator credentials for the language change failed. "
				L"Windows error: " + std::to_wstring(error));
			return false;
		}
		if (selection != broker_credential_selection::
			BrokerCredentialSelectionOutcome::Selected)
			return LaunchElevatedAndNotifyStatusIcons(
				executablePath, elevatedArgument);

		const std::wstring pipeToken = CreatePipeToken();
		if (pipeToken.empty())
		{
			ui::ReportError(localization::Format(
				localization::TextId::LanguageChangeFailed,
				{ windows_error::FormatWindowsError(ERROR_GEN_FAILURE) }),
				L"Generating the language-change result pipe token failed.");
			return false;
		}
		HANDLE resultPipe = INVALID_HANDLE_VALUE;
		OVERLAPPED pipeConnection = {};
		if (!CreateResultPipe(pipeToken, resultPipe, pipeConnection, error))
		{
			ui::ReportError(localization::Format(
				localization::TextId::LanguageChangeFailed,
				{ windows_error::FormatWindowsError(error) }),
				L"Creating the language-change result pipe failed. Windows error: " +
					std::to_wstring(error));
			return false;
		}
		const std::wstring brokerArgument =
			std::wstring(elevatedArgument) + L":" + pipeToken;
		const auto outcome = broker_launch::LaunchWithBrokerCredential(
			credential, executablePath, brokerArgument,
			broker::ElevationMode::RequireElevated,
			broker::LaunchVisibility::Hidden);
		if (outcome != broker_launch::BrokerLaunchOutcome::Succeeded)
		{
			CloseResultPipe(resultPipe, pipeConnection);
			return false;
		}
		DWORD languageChangeResult = ERROR_GEN_FAILURE;
		const bool resultReceived = ReceiveResult(
			resultPipe, pipeConnection, languageChangeResult, error);
		CloseResultPipe(resultPipe, pipeConnection);
		if (resultReceived && languageChangeResult == ERROR_SUCCESS)
		{
			NotifyStatusIcons();
			return true;
		}
		ui::ReportError(localization::Format(
			localization::TextId::LanguageChangeFailed,
			{ windows_error::FormatWindowsError(
				resultReceived ? languageChangeResult : error) }),
			L"The elevated language change failed. Windows error: " +
				std::to_wstring(resultReceived ? languageChangeResult : error));
		return false;
	}
}
