// Purpose: Defines shared Win32 helpers for client windows and notifications.
// Approach: Exposes the declarations, constants, and types consumed by the components that provide this behavior.
// Behavior: Unifies app icon, owner window, window title, and standard message types.
#pragma once

#include <Windows.h>

#include <cstddef>
#include <string>

#include "ApplicationIcon.h"

namespace xucrun::ui
{
	using ApplicationIconColor = application_icon::Color;

	HICON CreateColorizedApplicationIcon(HICON sourceIcon, ApplicationIconColor color);
	void ApplyApplicationIcon(HWND window);
	HWND CreateDialogControl(HWND dialog, DWORD extendedStyle,
		const wchar_t* className, const wchar_t* text, DWORD style,
		int x, int y, int width, int height, int id = 0);
	int MeasureDialogTextHeight(HWND dialog, const std::wstring& text,
		int width, int minimumHeight);
	void ResizeDialogClientHeight(HWND dialog, int clientHeight);
	void WriteDefaultDialogFont(WORD* destination, std::size_t faceCapacity);
	HWND GetDialogOwnerWindow();
	std::wstring GetWindowTitle(const std::wstring& suffix = L"");
	int ShowMessageBox(HWND owner, const std::wstring& message,
		const std::wstring& title, UINT type);
	void ShowError(const std::wstring& message, HWND owner = nullptr);
	void ShowInfo(const std::wstring& message, HWND owner = nullptr);
	void ShowWarning(const std::wstring& message, HWND owner = nullptr);
	void ReportError(const std::wstring& message,
		const std::wstring& technicalMessage, HWND owner = nullptr);
	void ReportInfo(const std::wstring& message,
		const std::wstring& technicalMessage, HWND owner = nullptr);
	void ReportWarning(const std::wstring& message,
		const std::wstring& technicalMessage, HWND owner = nullptr);
}