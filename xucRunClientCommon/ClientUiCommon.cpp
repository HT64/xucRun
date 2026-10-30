// Purpose: Implements central rendering and notification functions for the client UI.
// Approach: Implements the behavior with focused helpers built on shared xucRun interfaces and Windows APIs.
// Behavior: Connects resources, localization, and console-aware error output.
#include "ClientUiCommon.h"

#include <sstream>

#include "BrokerPipeClient.h"
#include "BuildVersion.h"
#include "ClientAccountIdentity.h"
#include "ClientConsoleOutput.h"
#include "ClientLogging.h"
#include "Localization.h"
#include "ClientResourceIds.h"

#ifndef BUILD_NUMBER
#define BUILD_NUMBER 0
#endif

namespace
{
	LRESULT CALLBACK ApplyMessageBoxIcon(int code, WPARAM wParam, LPARAM lParam)
	{
		if (code == HCBT_ACTIVATE)
		{
			HWND window = reinterpret_cast<HWND>(wParam);
			wchar_t className[16] = {};
			if (GetClassNameW(window, className, ARRAYSIZE(className)) &&
				wcscmp(className, L"#32770") == 0)
			{
				xucrun::ui::ApplyApplicationIcon(window);
			}
		}
		return CallNextHookEx(nullptr, code, wParam, lParam);
	}

	class DialogOwnerWindow
	{
	public:
		DialogOwnerWindow()
		{
			RECT ownerRect = {};
			HWND foregroundWindow = GetForegroundWindow();
			HMONITOR monitor = MonitorFromWindow(
				foregroundWindow, MONITOR_DEFAULTTOPRIMARY);
			MONITORINFO monitorInfo = { sizeof(monitorInfo) };
			if (GetMonitorInfoW(monitor, &monitorInfo))
				ownerRect = monitorInfo.rcWork;

			window = CreateWindowExW(
				WS_EX_TOOLWINDOW, L"STATIC", xucrun::broker::ProductName, WS_POPUP,
				ownerRect.left, ownerRect.top,
				ownerRect.right - ownerRect.left,
				ownerRect.bottom - ownerRect.top,
				nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
			if (window)
				xucrun::ui::ApplyApplicationIcon(window);
		}

		~DialogOwnerWindow()
		{
			if (window)
				DestroyWindow(window);
		}

		HWND get() const
		{
			return window;
		}

		DialogOwnerWindow(const DialogOwnerWindow&) = delete;
		DialogOwnerWindow& operator=(const DialogOwnerWindow&) = delete;

	private:
		HWND window = nullptr;
	};
}

namespace xucrun::ui
{
	HICON CreateColorizedApplicationIcon(HICON sourceIcon, ApplicationIconColor color)
	{
		return application_icon::CreateColorized(sourceIcon, color);
	}

	void ApplyApplicationIcon(HWND window)
	{
		static HICON largeIcon = static_cast<HICON>(LoadImageW(
			GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_XUCRUN), IMAGE_ICON,
			GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR));
		static HICON smallIcon = static_cast<HICON>(LoadImageW(
			GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_XUCRUN), IMAGE_ICON,
			GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
		static HICON redLargeIcon = CreateColorizedApplicationIcon(
			largeIcon, ApplicationIconColor::Red);
		static HICON redSmallIcon = CreateColorizedApplicationIcon(
			smallIcon, ApplicationIconColor::Red);
		static const bool elevated = account_identity::IsRunningAsAdministrator();
		HICON selectedLargeIcon = elevated && redLargeIcon ? redLargeIcon : largeIcon;
		HICON selectedSmallIcon = elevated && redSmallIcon ? redSmallIcon : smallIcon;

		if (selectedLargeIcon)
			SendMessageW(window, WM_SETICON, ICON_BIG,
				reinterpret_cast<LPARAM>(selectedLargeIcon));
		if (selectedSmallIcon)
			SendMessageW(window, WM_SETICON, ICON_SMALL,
				reinterpret_cast<LPARAM>(selectedSmallIcon));
	}

	HWND CreateDialogControl(HWND dialog, DWORD extendedStyle,
		const wchar_t* className, const wchar_t* text, DWORD style,
		int x, int y, int width, int height, int id)
	{
		RECT bounds = { x, y, x + width, y + height };
		MapDialogRect(dialog, &bounds);
		HWND control = CreateWindowExW(extendedStyle, className, text,
			WS_CHILD | WS_VISIBLE | style, bounds.left, bounds.top,
			bounds.right - bounds.left, bounds.bottom - bounds.top, dialog,
			reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
			GetModuleHandleW(nullptr), nullptr);
		if (control)
		{
			SendMessageW(control, WM_SETFONT,
				reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
		}
		return control;
	}

	int MeasureDialogTextHeight(HWND dialog, const std::wstring& text,
		int width, int minimumHeight)
	{
		RECT textBounds = { 0, 0, width, 0 };
		MapDialogRect(dialog, &textBounds);
		HDC dialogDc = GetDC(dialog);
		if (!dialogDc)
			return minimumHeight;
		HGDIOBJ previousFont = SelectObject(
			dialogDc, GetStockObject(DEFAULT_GUI_FONT));
		DrawTextW(dialogDc, text.c_str(), -1, &textBounds,
			DT_CALCRECT | DT_NOPREFIX | DT_WORDBREAK);
		if (previousFont)
			SelectObject(dialogDc, previousFont);
		ReleaseDC(dialog, dialogDc);

		RECT verticalUnits = { 0, 0, 0, 8 };
		MapDialogRect(dialog, &verticalUnits);
		if (verticalUnits.bottom <= 0)
			return minimumHeight;
		const int measuredHeight =
			(textBounds.bottom * 8 + verticalUnits.bottom - 1) / verticalUnits.bottom;
		return measuredHeight > minimumHeight ? measuredHeight : minimumHeight;
	}

	void ResizeDialogClientHeight(HWND dialog, int clientHeight)
	{
		RECT clientRect = {};
		RECT windowRect = {};
		RECT desiredClientRect = { 0, 0, 0, clientHeight };
		if (!GetClientRect(dialog, &clientRect) || !GetWindowRect(dialog, &windowRect) ||
			!MapDialogRect(dialog, &desiredClientRect))
		{
			return;
		}
		const int heightDelta = desiredClientRect.bottom - clientRect.bottom;
		SetWindowPos(dialog, nullptr, windowRect.left,
			windowRect.top - heightDelta / 2,
			windowRect.right - windowRect.left,
			windowRect.bottom - windowRect.top + heightDelta,
			SWP_NOACTIVATE | SWP_NOZORDER);
	}

	void WriteDefaultDialogFont(WORD* destination, std::size_t faceCapacity)
	{
		LOGFONTW font = {};
		GetObjectW(GetStockObject(DEFAULT_GUI_FONT), sizeof(font), &font);
		UINT dpi = 96;
		HDC screen = GetDC(nullptr);
		if (screen)
		{
			dpi = static_cast<UINT>(GetDeviceCaps(screen, LOGPIXELSY));
			ReleaseDC(nullptr, screen);
		}
		const LONG fontHeight = font.lfHeight < 0 ? -font.lfHeight : font.lfHeight;
		const WORD pointSize = static_cast<WORD>(MulDiv(fontHeight, 72, dpi));
		*destination++ = pointSize ? pointSize : 8;
		*destination++ = static_cast<WORD>(font.lfWeight ? font.lfWeight : FW_NORMAL);
		BYTE* attributes = reinterpret_cast<BYTE*>(destination);
		*attributes++ = font.lfItalic;
		*attributes++ = font.lfCharSet;
		wcscpy_s(reinterpret_cast<wchar_t*>(attributes), faceCapacity,
			font.lfFaceName[0] ? font.lfFaceName : L"MS Shell Dlg");
	}

	HWND GetDialogOwnerWindow()
	{
		static DialogOwnerWindow owner;
		return owner.get();
	}

	std::wstring GetWindowTitle(const std::wstring& suffix)
	{
		std::wstringstream title;
		title << L"xucRun " << VERSION_MAJOR << L"." << VERSION_MINOR << L"."
			<< VERSION_PATCH << L"." << BUILD_NUMBER;
		if (!suffix.empty())
			title << L" - " << suffix;
		return title.str();
	}

	int ShowMessageBox(HWND owner, const std::wstring& message,
		const std::wstring& title, UINT type)
	{
		HHOOK iconHook = SetWindowsHookExW(
			WH_CBT, ApplyMessageBoxIcon, nullptr, GetCurrentThreadId());
		const int result = MessageBoxW(owner, message.c_str(), title.c_str(), type);
		if (iconHook)
			UnhookWindowsHookEx(iconHook);
		return result;
	}

	void ShowError(const std::wstring& message, HWND owner)
	{
		if (console_output::WriteErrorToAvailableTarget(message))
			return;
		if (!owner)
			owner = GetDialogOwnerWindow();
		ShowMessageBox(owner, message,
			GetWindowTitle(localization::GetText(
				localization::TextId::ErrorTitle)),
			MB_OK | MB_ICONERROR | MB_SETFOREGROUND | MB_TOPMOST);
	}

	void ShowInfo(const std::wstring& message, HWND owner)
	{
		if (!owner)
			owner = GetDialogOwnerWindow();
		ShowMessageBox(owner, message, GetWindowTitle(),
			MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND | MB_TOPMOST);
	}

	void ShowWarning(const std::wstring& message, HWND owner)
	{
		if (!owner)
			owner = GetDialogOwnerWindow();
		ShowMessageBox(owner, message,
			GetWindowTitle(localization::GetText(
				localization::TextId::WarningTitle)),
			MB_OK | MB_ICONWARNING | MB_SETFOREGROUND | MB_TOPMOST);
	}

	void ReportError(const std::wstring& message,
		const std::wstring& technicalMessage, HWND owner)
	{
		client_logging::Write(L"Error", technicalMessage);
		ShowError(message, owner);
	}

	void ReportInfo(const std::wstring& message,
		const std::wstring& technicalMessage, HWND owner)
	{
		client_logging::Write(L"Info", technicalMessage);
		ShowInfo(message, owner);
	}

	void ReportWarning(const std::wstring& message,
		const std::wstring& technicalMessage, HWND owner)
	{
		client_logging::Write(L"Warning", technicalMessage);
		ShowWarning(message, owner);
	}
}