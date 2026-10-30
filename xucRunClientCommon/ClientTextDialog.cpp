// Purpose: Implements console-aware display of extensive informational and error text.
// Approach: Implements the behavior with focused helpers built on shared xucRun interfaces and Windows APIs.
// Behavior: Delegates available text output to ClientConsoleOutput and otherwise builds a modal dialog.
#include "ClientTextDialog.h"

#include <algorithm>

#include "ClientConsoleOutput.h"
#include "Localization.h"
#include "ClientUiCommon.h"

namespace
{
	constexpr int TextDialogHeadingId = 1001;
	constexpr int TextDialogContentId = 1002;

	struct TextDialogContext
	{
		const std::wstring* windowTitle;
		const std::wstring* heading;
		const std::wstring* content;
	};

	void LayoutTextDialog(HWND dialog)
	{
		HWND heading = GetDlgItem(dialog, TextDialogHeadingId);
		HWND content = GetDlgItem(dialog, TextDialogContentId);
		HWND okButton = GetDlgItem(dialog, IDOK);
		if (!heading || !content || !okButton)
			return;

		RECT clientRect = {};
		GetClientRect(dialog, &clientRect);
		const int clientWidth = static_cast<int>(clientRect.right - clientRect.left);
		const int clientHeight = static_cast<int>(clientRect.bottom - clientRect.top);
		RECT layoutUnits = { 0, 0, 8, 8 };
		MapDialogRect(dialog, &layoutUnits);
		const int horizontalMargin = layoutUnits.right;
		const int verticalGap = layoutUnits.bottom;
		layoutUnits = { 0, 0, 30, 10 };
		MapDialogRect(dialog, &layoutUnits);
		const int headingX = layoutUnits.right;
		const int headingY = layoutUnits.bottom;
		layoutUnits = { 0, 0, 0, 12 };
		MapDialogRect(dialog, &layoutUnits);
		const int headingHeight = layoutUnits.bottom;
		layoutUnits = { 0, 0, 0, 30 };
		MapDialogRect(dialog, &layoutUnits);
		const int contentY = layoutUnits.bottom;
		layoutUnits = { 0, 0, 50, 14 };
		MapDialogRect(dialog, &layoutUnits);
		const int buttonWidth = layoutUnits.right;
		const int buttonHeight = layoutUnits.bottom;
		const int buttonY = clientHeight - verticalGap - buttonHeight;
		const int contentBottom = buttonY - verticalGap;
		MoveWindow(heading, headingX, headingY,
			(std::max)(1, clientWidth - headingX - horizontalMargin),
			headingHeight, FALSE);
		MoveWindow(content, horizontalMargin, contentY,
			(std::max)(1, clientWidth - 2 * horizontalMargin),
			(std::max)(1, contentBottom - contentY), FALSE);
		MoveWindow(okButton, (clientWidth - buttonWidth) / 2,
			buttonY, buttonWidth, buttonHeight, FALSE);

		RECT formattingRect = {};
		SendMessageW(content, EM_GETRECT, 0, reinterpret_cast<LPARAM>(&formattingRect));
		HDC contentDc = GetDC(content);
		TEXTMETRICW textMetrics = {};
		HFONT contentFont = reinterpret_cast<HFONT>(
			SendMessageW(content, WM_GETFONT, 0, 0));
		HGDIOBJ previousFont = contentDc && contentFont
			? SelectObject(contentDc, contentFont) : nullptr;
		const bool metricsAvailable = contentDc &&
			GetTextMetricsW(contentDc, &textMetrics) != FALSE;
		if (contentDc && previousFont)
			SelectObject(contentDc, previousFont);
		if (contentDc)
			ReleaseDC(content, contentDc);
		const LRESULT lineCount = SendMessageW(content, EM_GETLINECOUNT, 0, 0);
		const bool needsScrollBar = metricsAvailable &&
			lineCount * textMetrics.tmHeight > formattingRect.bottom - formattingRect.top;
		ShowScrollBar(content, SB_VERT, needsScrollBar);
		RedrawWindow(dialog, nullptr, nullptr,
			RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
	}

	INT_PTR CALLBACK TextDialogProc(HWND dialog, UINT message, WPARAM wParam,
		LPARAM lParam)
	{
		switch (message)
		{
		case WM_INITDIALOG:
		{
			const auto* context = reinterpret_cast<const TextDialogContext*>(lParam);
			xucrun::ui::ApplyApplicationIcon(dialog);
			SetWindowTextW(dialog, context && context->windowTitle
				? context->windowTitle->c_str()
				: L"Information");

			std::wstring displayText;
			if (context && context->content)
			{
				displayText.reserve(context->content->size() + 32);
				for (size_t index = 0; index < context->content->size(); index++)
				{
					if ((*context->content)[index] == L'\n' &&
						(index == 0 || (*context->content)[index - 1] != L'\r'))
					{
						displayText += L'\r';
					}
					displayText += (*context->content)[index];
				}
			}

			RECT dialogRect = {};
			GetWindowRect(dialog, &dialogRect);
			RECT initialClientRect = {};
			GetClientRect(dialog, &initialClientRect);
			HWND owner = GetWindow(dialog, GW_OWNER);
			HMONITOR monitor = MonitorFromWindow(
				owner ? owner : dialog, MONITOR_DEFAULTTONEAREST);
			MONITORINFO monitorInfo = { sizeof(monitorInfo) };
			RECT workArea = {};
			if (GetMonitorInfoW(monitor, &monitorInfo))
				workArea = monitorInfo.rcWork;
			else
				workArea = { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };

			const UINT dpi = GetDpiForWindow(dialog);
			const int screenMargin = MulDiv(16, dpi ? dpi : 96, 96);
			const int workWidth = workArea.right - workArea.left;
			const int workHeight = workArea.bottom - workArea.top;
			const int availableWidth = (std::max)(1, workWidth - 2 * screenMargin);
			const int availableHeight = (std::max)(1, workHeight - 2 * screenMargin);
			const int currentWidth = dialogRect.right - dialogRect.left;
			const int currentHeight = dialogRect.bottom - dialogRect.top;
			const int dialogWidth = (std::min)(availableWidth, currentWidth * 4 / 3);
			const int nonClientWidth = currentWidth - initialClientRect.right;
			const int nonClientHeight = currentHeight - initialClientRect.bottom;
			RECT sizingUnits = { 0, 0, 8, 30 };
			MapDialogRect(dialog, &sizingUnits);
			const int sizingHorizontalMargin = static_cast<int>(sizingUnits.right);
			const int contentWidth = (std::max)(1,
				dialogWidth - nonClientWidth - 2 * sizingHorizontalMargin);
			RECT measuredText = { 0, 0, contentWidth, 0 };
			HDC dialogDc = GetDC(dialog);
			HGDIOBJ previousFont = dialogDc
				? SelectObject(dialogDc, GetStockObject(DEFAULT_GUI_FONT)) : nullptr;
			if (dialogDc)
			{
				DrawTextW(dialogDc, displayText.c_str(), -1, &measuredText,
					DT_CALCRECT | DT_EDITCONTROL | DT_NOPREFIX | DT_WORDBREAK);
				if (previousFont)
					SelectObject(dialogDc, previousFont);
				ReleaseDC(dialog, dialogDc);
			}
			sizingUnits = { 0, 0, 0, 14 };
			MapDialogRect(dialog, &sizingUnits);
			const int desiredHeight = nonClientHeight + measuredText.bottom +
				sizingUnits.bottom + MulDiv(96, dpi ? dpi : 96, 96);
			const int minimumHeight = (std::min)(
				availableHeight, MulDiv(320, dpi ? dpi : 96, 96));
			const int maximumHeight = (std::min)(availableHeight, workHeight * 4 / 5);
			const int dialogHeight = (std::min)(maximumHeight,
				(std::max)(minimumHeight, desiredHeight));
			const int x = workArea.left + (workWidth - dialogWidth) / 2;
			const int y = workArea.top + (workHeight - dialogHeight) / 2;
			SetWindowPos(dialog, HWND_TOPMOST, x, y, dialogWidth, dialogHeight,
				SWP_SHOWWINDOW);
			SetForegroundWindow(dialog);

			HWND icon = xucrun::ui::CreateDialogControl(dialog, 0,
				L"STATIC", nullptr, SS_ICON, 8, 8, 18, 18);
			SendMessageW(icon, STM_SETICON,
				reinterpret_cast<WPARAM>(LoadIconW(nullptr, IDI_INFORMATION)), 0);

			HWND heading = xucrun::ui::CreateDialogControl(dialog, 0,
				L"STATIC", context && context->heading ? context->heading->c_str() : L"",
				SS_LEFT, 30, 10, 262, 12, TextDialogHeadingId);

			HWND content = xucrun::ui::CreateDialogControl(dialog, WS_EX_CLIENTEDGE,
				L"EDIT", displayText.c_str(),
				WS_VSCROLL | ES_LEFT | ES_MULTILINE |
					ES_READONLY | ES_AUTOVSCROLL,
				8, 30, 284, 267, TextDialogContentId);
			SendMessageW(content, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
				MAKELPARAM(4, 4));
			SendMessageW(content, EM_SETSEL, 0, 0);

			HWND okButton = xucrun::ui::CreateDialogControl(dialog, 0,
				L"BUTTON", L"OK", BS_DEFPUSHBUTTON | WS_TABSTOP,
				125, 304, 50, 14, IDOK);

			LayoutTextDialog(dialog);
			SetFocus(okButton);
			return FALSE;
		}

		case WM_SIZE:
			LayoutTextDialog(dialog);
			return TRUE;

		case WM_GETMINMAXINFO:
		{
			auto* sizeInfo = reinterpret_cast<MINMAXINFO*>(lParam);
			const UINT dpi = GetDpiForWindow(dialog);
			sizeInfo->ptMinTrackSize.x = MulDiv(320, dpi ? dpi : 96, 96);
			sizeInfo->ptMinTrackSize.y = MulDiv(240, dpi ? dpi : 96, 96);
			return TRUE;
		}

		case WM_COMMAND:
			if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
			{
				EndDialog(dialog, LOWORD(wParam));
				return TRUE;
			}
			break;

		case WM_CLOSE:
			EndDialog(dialog, IDCANCEL);
			return TRUE;
		}

		return FALSE;
	}
}

namespace xucrun::text_dialog
{
	void Show(const std::wstring& windowTitle, const std::wstring& heading,
		const std::wstring& content, HWND owner)
	{
		if (console_output::WriteToAvailableTarget(heading, content) ==
			console_output::TextOutputResult::Complete)
		{
			return;
		}

		struct DialogTemplateHeader
		{
			WORD dlgVer;
			WORD signature;
			DWORD helpID;
			DWORD exStyle;
			DWORD style;
			WORD itemCount;
			short x;
			short y;
			short cx;
			short cy;
		};

		BYTE buffer[1024] = {};
		auto* dialogTemplate = reinterpret_cast<DialogTemplateHeader*>(buffer);
		dialogTemplate->dlgVer = 1;
		dialogTemplate->signature = 0xFFFF;
		dialogTemplate->style = DS_SETFONT | DS_CENTER | WS_POPUP | WS_CAPTION |
			WS_SYSMENU | WS_SIZEBOX | WS_MAXIMIZEBOX;
		dialogTemplate->cx = 300;
		dialogTemplate->cy = 325;

		WORD* data = reinterpret_cast<WORD*>(buffer + sizeof(DialogTemplateHeader));
		*data++ = 0;
		*data++ = 0;
		wcscpy_s(reinterpret_cast<wchar_t*>(data), 100, windowTitle.c_str());
		data += windowTitle.length() + 1;
		ui::WriteDefaultDialogFont(data, 50);

		TextDialogContext context = { &windowTitle, &heading, &content };
		DialogBoxIndirectParamW(GetModuleHandleW(nullptr),
			reinterpret_cast<LPCDLGTEMPLATEW>(buffer), owner, TextDialogProc,
			reinterpret_cast<LPARAM>(&context));
	}
}