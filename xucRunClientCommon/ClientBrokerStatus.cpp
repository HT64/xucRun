// Purpose: Queries and formats broker service health for Core and Tray status surfaces.
#include "ClientBrokerStatus.h"

#include <Windows.h>
#include <winsvc.h>

#include <cstdint>
#include <string>
#include <vector>

#include "BrokerPipeClient.h"
#include "ClientUiCommon.h"
#include "Localization.h"

namespace xucrun::broker_status
{
	namespace
	{
		constexpr DWORD PurgeBrokerStoreControl = 128;
	}

	BrokerServiceState QueryBrokerServiceState()
	{
		BrokerServiceState result;
		SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
		if (!manager)
		{
			result.error = GetLastError();
			return result;
		}
		SC_HANDLE service = OpenServiceW(manager, broker::BrokerServiceName,
			SERVICE_QUERY_STATUS | SERVICE_QUERY_CONFIG);
		if (!service)
		{
			result.error = GetLastError();
			if (result.error == ERROR_SERVICE_DOES_NOT_EXIST)
				result.status = BrokerServiceStatus::NotInstalled;
			CloseServiceHandle(manager);
			return result;
		}
		SERVICE_STATUS_PROCESS status = {};
		DWORD bytesNeeded = 0;
		if (!QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO,
			reinterpret_cast<LPBYTE>(&status), sizeof(status), &bytesNeeded))
		{
			result.error = GetLastError();
			CloseServiceHandle(service);
			CloseServiceHandle(manager);
			return result;
		}
		DWORD configurationSize = 0;
		QueryServiceConfigW(service, nullptr, 0, &configurationSize);
		std::vector<std::uint8_t> configurationBuffer(configurationSize);
		auto* configuration = configurationSize == 0
			? nullptr
			: reinterpret_cast<QUERY_SERVICE_CONFIGW*>(configurationBuffer.data());
		if (!configuration || !QueryServiceConfigW(service, configuration,
			configurationSize, &configurationSize))
		{
			result.error = GetLastError();
			CloseServiceHandle(service);
			CloseServiceHandle(manager);
			return result;
		}
		CloseServiceHandle(service);
		CloseServiceHandle(manager);
		result.serviceExitCode = status.dwWin32ExitCode;
		if (status.dwCurrentState == SERVICE_RUNNING)
			result.status = BrokerServiceStatus::Running;
		else if (status.dwCurrentState == SERVICE_START_PENDING ||
			status.dwCurrentState == SERVICE_CONTINUE_PENDING)
			result.status = BrokerServiceStatus::Starting;
		else if (status.dwCurrentState == SERVICE_STOP_PENDING)
			result.status = BrokerServiceStatus::Stopping;
		else if (status.dwCurrentState == SERVICE_STOPPED &&
			configuration->dwStartType == SERVICE_DISABLED)
			result.status = BrokerServiceStatus::Disabled;
		else
			result.status = BrokerServiceStatus::Stopped;
		return result;
	}

	bool EnableBrokerService(DWORD& windowsError)
	{
		windowsError = NO_ERROR;
		SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
		if (!manager)
		{
			windowsError = GetLastError();
			return false;
		}
		SC_HANDLE service = OpenServiceW(manager, broker::BrokerServiceName,
			SERVICE_CHANGE_CONFIG);
		if (!service)
		{
			windowsError = GetLastError();
			CloseServiceHandle(manager);
			return false;
		}
		const bool configured = ChangeServiceConfigW(service, SERVICE_NO_CHANGE,
			SERVICE_DEMAND_START, SERVICE_NO_CHANGE, nullptr, nullptr, nullptr,
			nullptr, nullptr, nullptr, nullptr) == TRUE;
		if (!configured)
			windowsError = GetLastError();
		CloseServiceHandle(service);
		CloseServiceHandle(manager);
		if (!configured)
			return false;
		std::uint32_t serviceError = NO_ERROR;
		const bool started = broker::StartBrokerService(5000, &serviceError);
		windowsError = serviceError;
		return started;
	}

	bool WaitForBrokerServiceState(BrokerServiceStatus expectedStatus,
		DWORD timeoutMilliseconds, BrokerServiceState& finalState)
	{
		const ULONGLONG deadline = GetTickCount64() + timeoutMilliseconds;
		do
		{
			finalState = QueryBrokerServiceState();
			if (finalState.status == expectedStatus)
				return true;
			if (finalState.status == BrokerServiceStatus::Unknown ||
				finalState.status == BrokerServiceStatus::NotInstalled)
			{
				return false;
			}
			Sleep(100);
		} while (GetTickCount64() < deadline);

		finalState.error = ERROR_TIMEOUT;
		return false;
	}

	BrokerStorePurgeResult PurgeBrokerStore()
	{
		HANDLE token = nullptr;
		TOKEN_ELEVATION elevation = {};
		DWORD returnLength = 0;
		const bool elevationKnown =
			OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token) &&
			GetTokenInformation(token, TokenElevation, &elevation,
				sizeof(elevation), &returnLength);
		const DWORD elevationError = elevationKnown ? NO_ERROR : GetLastError();
		if (token)
			CloseHandle(token);
		if (!elevationKnown)
		{
			ui::ReportError(localization::Format(
				localization::TextId::PurgeElevationUnknown,
				{ std::to_wstring(elevationError) }),
				L"Determining elevation for broker store cleanup failed. Windows error: " +
					std::to_wstring(elevationError));
			return BrokerStorePurgeResult::Failed;
		}
		if (!elevation.TokenIsElevated)
		{
			ui::ReportError(localization::GetText(
				localization::TextId::PurgeAdminRequired),
				L"Broker store cleanup was rejected because the process is not elevated.");
			return BrokerStorePurgeResult::Failed;
		}

		const int confirmation = ui::ShowMessageBox(ui::GetDialogOwnerWindow(),
			localization::GetText(localization::TextId::PurgeConfirmation),
			ui::GetWindowTitle(localization::GetText(
				localization::TextId::PurgeTitle)),
			MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2 |
				MB_SETFOREGROUND | MB_TOPMOST);
		if (confirmation != IDYES)
			return BrokerStorePurgeResult::Cancelled;

		SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
		if (!manager)
		{
			const DWORD error = GetLastError();
			ui::ReportError(localization::Format(
				localization::TextId::PurgeScmOpenFailed,
				{ std::to_wstring(error) }),
				L"Opening the Service Control Manager for broker store cleanup failed. "
				L"Windows error: " + std::to_wstring(error));
			return BrokerStorePurgeResult::Failed;
		}
		SC_HANDLE service = OpenServiceW(manager, broker::BrokerServiceName,
			SERVICE_START | SERVICE_QUERY_STATUS | SERVICE_USER_DEFINED_CONTROL);
		if (!service)
		{
			const DWORD error = GetLastError();
			CloseServiceHandle(manager);
			ui::ReportError(localization::Format(
				localization::TextId::PurgeServiceOpenFailed,
				{ std::to_wstring(error) }),
				L"Opening the broker service for store cleanup failed. Windows error: " +
					std::to_wstring(error));
			return BrokerStorePurgeResult::Failed;
		}

		BrokerServiceState state = QueryBrokerServiceState();
		if (state.status == BrokerServiceStatus::Stopped)
		{
			if (!StartServiceW(service, 0, nullptr) &&
				GetLastError() != ERROR_SERVICE_ALREADY_RUNNING)
			{
				const DWORD error = GetLastError();
				CloseServiceHandle(service);
				CloseServiceHandle(manager);
				ui::ReportError(localization::Format(
					localization::TextId::PurgeServiceStartFailed,
					{ std::to_wstring(error) }),
					L"Starting the broker service for store cleanup failed. Windows error: " +
						std::to_wstring(error));
				return BrokerStorePurgeResult::Failed;
			}
			if (!WaitForBrokerServiceState(
				BrokerServiceStatus::Running, 10000, state))
			{
				CloseServiceHandle(service);
				CloseServiceHandle(manager);
				ui::ReportError(localization::Format(
					localization::TextId::PurgeServiceStartTimeout,
					{ std::to_wstring(state.error) }),
					L"Waiting for the broker service to start for store cleanup failed. "
					L"Windows error: " + std::to_wstring(state.error));
				return BrokerStorePurgeResult::Failed;
			}
		}
		else if (state.status != BrokerServiceStatus::Running)
		{
			CloseServiceHandle(service);
			CloseServiceHandle(manager);
			ui::ReportError(localization::GetText(
				localization::TextId::PurgeServiceInvalidState),
				L"Broker store cleanup encountered an invalid broker service state.");
			return BrokerStorePurgeResult::Failed;
		}

		SERVICE_STATUS controlStatus = {};
		if (!ControlService(service, PurgeBrokerStoreControl, &controlStatus))
		{
			const DWORD error = GetLastError();
			CloseServiceHandle(service);
			CloseServiceHandle(manager);
			ui::ReportError(localization::Format(
				localization::TextId::PurgeRequestRejected,
				{ std::to_wstring(error) }),
				L"The broker service rejected the store cleanup request. Windows error: " +
					std::to_wstring(error));
			return BrokerStorePurgeResult::Failed;
		}
		CloseServiceHandle(service);
		CloseServiceHandle(manager);

		if (!WaitForBrokerServiceState(
			BrokerServiceStatus::Stopped, 30000, state))
		{
			ui::ReportError(localization::Format(
				localization::TextId::PurgeStopTimeout,
				{ std::to_wstring(state.error) }),
				L"Waiting for broker store cleanup to stop the service failed. Windows error: " +
					std::to_wstring(state.error));
			return BrokerStorePurgeResult::Failed;
		}
		if (state.serviceExitCode != NO_ERROR)
		{
			ui::ReportError(localization::Format(
				localization::TextId::PurgeIncomplete,
				{ std::to_wstring(state.serviceExitCode) }),
				L"Broker store cleanup completed with a service error: " +
					std::to_wstring(state.serviceExitCode));
			return BrokerStorePurgeResult::Failed;
		}

		ui::ReportInfo(localization::GetText(
			localization::TextId::PurgeSucceeded),
			L"Broker store cleanup completed successfully.");
		return BrokerStorePurgeResult::Succeeded;
	}
}
