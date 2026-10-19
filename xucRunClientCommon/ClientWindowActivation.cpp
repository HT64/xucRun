// Purpose: Implements safe window lookup, foreground activation, and Total Commander refresh.
// Behavior: Validates process ownership, tries Win32 activation with thread fallback, and signals rejection.
#include "ClientWindowActivation.h"

#include <algorithm>
#include <cwchar>
#include <iterator>

#include "BrokerPipeClient.h"
#include "ClientLogging.h"
#include "ClientUiCommon.h"
#include "Localization.h"

namespace
{
	struct BrokerWindowSearch
	{
		DWORD processId = 0;
		HWND window = nullptr;
	};

	BOOL CALLBACK FindBrokerMainWindow(HWND window, LPARAM parameter)
	{
		auto* search = reinterpret_cast<BrokerWindowSearch*>(parameter);
		DWORD processId = 0;
		GetWindowThreadProcessId(window, &processId);
		if (processId != search->processId || !IsWindowVisible(window) ||
			GetWindow(window, GW_OWNER) != nullptr ||
			(GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) != 0)
		{
			return TRUE;
		}
		search->window = window;
		return FALSE;
	}

	bool IsTrackedProcessWindow(HWND window, DWORD expectedProcessId)
	{
		DWORD processId = 0;
		return IsWindow(window) && IsWindowVisible(window) &&
			GetWindowThreadProcessId(window, &processId) != 0 &&
			processId == expectedProcessId &&
			GetWindow(window, GW_OWNER) == nullptr &&
			(GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) == 0;
	}

	void RefreshTotalCommanderFocusedList(HWND mainWindow)
	{
		wchar_t mainClassName[64] = {};
		if (GetClassNameW(mainWindow, mainClassName,
			static_cast<int>(std::size(mainClassName))) == 0 ||
			wcscmp(mainClassName, L"TTOTAL_CMD") != 0)
		{
			return;
		}

		const DWORD targetThreadId = GetWindowThreadProcessId(mainWindow, nullptr);
		GUITHREADINFO threadInfo = { sizeof(threadInfo) };
		if (targetThreadId == 0 || !GetGUIThreadInfo(targetThreadId, &threadInfo) ||
			!threadInfo.hwndFocus || !IsChild(mainWindow, threadInfo.hwndFocus))
		{
			return;
		}

		wchar_t focusClassName[64] = {};
		if (GetClassNameW(threadInfo.hwndFocus, focusClassName,
			static_cast<int>(std::size(focusClassName))) == 0 ||
			wcscmp(focusClassName, L"LCLListBox") != 0)
		{
			return;
		}

		if (RedrawWindow(threadInfo.hwndFocus, nullptr, nullptr,
			RDW_INVALIDATE | RDW_UPDATENOW | RDW_FRAME | RDW_ALLCHILDREN))
		{
			xucrun::client_logging::Write(
				L"Info", L"Refreshed focused Total Commander file list");
		}
	}

	void ActivateBrokerWindow(HWND brokerWindow, DWORD processId)
	{
		AllowSetForegroundWindow(processId);
		if (IsIconic(brokerWindow))
		{
			DWORD_PTR restoreResult = 0;
			if (!SendMessageTimeoutW(brokerWindow, WM_SYSCOMMAND, SC_RESTORE, 0,
				SMTO_ABORTIFHUNG | SMTO_BLOCK, 1000, &restoreResult))
			{
				ShowWindowAsync(brokerWindow, SW_RESTORE);
			}
		}
		else
			ShowWindowAsync(brokerWindow, SW_SHOW);
		BringWindowToTop(brokerWindow);
		bool activated = SetForegroundWindow(brokerWindow) == TRUE;
		if (!activated)
		{
			const DWORD currentThreadId = GetCurrentThreadId();
			const DWORD targetThreadId =
				GetWindowThreadProcessId(brokerWindow, nullptr);
			const HWND foregroundWindow = GetForegroundWindow();
			const DWORD foregroundThreadId = foregroundWindow
				? GetWindowThreadProcessId(foregroundWindow, nullptr)
				: 0;
			const bool attachedTarget = targetThreadId != 0 &&
				targetThreadId != currentThreadId &&
				AttachThreadInput(currentThreadId, targetThreadId, TRUE) == TRUE;
			const bool attachedForeground = foregroundThreadId != 0 &&
				foregroundThreadId != currentThreadId &&
				foregroundThreadId != targetThreadId &&
				AttachThreadInput(
					currentThreadId, foregroundThreadId, TRUE) == TRUE;
			BringWindowToTop(brokerWindow);
			activated = SetForegroundWindow(brokerWindow) == TRUE;
			if (attachedForeground)
				AttachThreadInput(currentThreadId, foregroundThreadId, FALSE);
			if (attachedTarget)
				AttachThreadInput(currentThreadId, targetThreadId, FALSE);
		}
		if (!activated)
		{
			FLASHWINFO flash = { sizeof(flash), brokerWindow,
				FLASHW_TRAY | FLASHW_TIMERNOFG, 3, 0 };
			FlashWindowEx(&flash);
			xucrun::client_logging::Write(L"Info",
				L"Windows rejected foreground activation; taskbar is flashing");
			return;
		}
		RefreshTotalCommanderFocusedList(brokerWindow);
		xucrun::client_logging::Write(
			L"Ok", L"Brought broker process window to foreground");
	}
}

namespace xucrun::window_activation
{
	void ActivateTrackedProcessWindow(
		const broker::TrackedProcessListEntry& process, HWND targetWindow)
	{
		broker::TrackedProcessListRequest request;
		request.requestId = broker::GenerateBrokerRequestId();
		if (request.requestId == 0)
			return;
		const auto response = broker::SubmitTrackedProcessListRequest(request, 1000);
		const bool processStillTracked =
			response.status == broker::PipeTrackedProcessListStatus::Succeeded &&
			response.result.status == broker::CredentialListStatus::Succeeded &&
			std::any_of(response.result.entries.begin(), response.result.entries.end(),
				[&process](const broker::TrackedProcessListEntry& current) {
					return current.processId == process.processId &&
						current.trackingId == process.trackingId;
				});
		if (!processStillTracked ||
			!IsTrackedProcessWindow(targetWindow, process.processId))
		{
			ui::ShowMessageBox(nullptr,
				localization::GetText(
					localization::TextId::ProcessWindowUnavailable),
				broker::ProductName, MB_OK | MB_ICONINFORMATION);
			return;
		}
		ActivateBrokerWindow(targetWindow, process.processId);
	}

	void ActivateBrokerProcessWindow(DWORD processId)
	{
		BrokerWindowSearch search{ processId };
		const ULONGLONG deadline = GetTickCount64() + 1500;
		do
		{
			search.window = nullptr;
			EnumWindows(FindBrokerMainWindow, reinterpret_cast<LPARAM>(&search));
			if (search.window)
				break;
			Sleep(50);
		} while (GetTickCount64() < deadline);
		if (!search.window)
		{
			client_logging::Write(
				L"Info", L"No activatable main window found for broker process");
			return;
		}

		ActivateBrokerWindow(search.window, processId);
	}
}