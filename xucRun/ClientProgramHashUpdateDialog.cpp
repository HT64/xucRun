// Purpose: Implements the selection dialog for program hash update scope.
// Behavior: Creates a modal Win32 dialog and stores the result only in dialog context.
#include "ClientProgramHashUpdateDialog.h"

#include <Windows.h>

#include "ClientUiCommon.h"
#include "Localization.h"

namespace
{
	using xucrun::program_hash_update_dialog::ProgramHashUpdateChoice;

	struct DialogContext
	{
		const std::wstring* message;
		bool signatureValid;
		ProgramHashUpdateChoice choice = ProgramHashUpdateChoice::Cancel;
	};

	INT_PTR CALLBACK DialogProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
	{
		if (message == WM_INITDIALOG)
		{
			auto* context = reinterpret_cast<DialogContext*>(lParam);
			SetWindowLongPtrW(dialog, DWLP_USER, reinterpret_cast<LONG_PTR>(context));
			xucrun::ui::ApplyApplicationIcon(dialog);
			SetWindowTextW(dialog, xucrun::ui::GetWindowTitle(
				xucrun::localization::GetText(
					xucrun::localization::TextId::ProgramUpdateTitle)).c_str());
			SetWindowPos(dialog, HWND_TOPMOST, 0, 0, 0, 0,
				SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
			SetForegroundWindow(dialog);

			HWND icon = xucrun::ui::CreateDialogControl(dialog, 0, L"STATIC", nullptr,
				SS_ICON, 8, 8, 18, 18);
			SendMessageW(icon, STM_SETICON, reinterpret_cast<WPARAM>(LoadIconW(nullptr,
				context->signatureValid ? IDI_INFORMATION : IDI_WARNING)), 0);
			xucrun::ui::CreateDialogControl(dialog, 0, L"STATIC", xucrun::localization::GetText(
				xucrun::localization::TextId::ProgramUpdateHeading).c_str(),
				SS_LEFT, 30, 10, 352, 12);
			HWND details = xucrun::ui::CreateDialogControl(dialog, WS_EX_CLIENTEDGE,
				L"EDIT", context->message->c_str(), WS_VSCROLL |
				ES_LEFT | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
				8, 30, 374, 210);
			SendMessageW(details, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
				MAKELPARAM(4, 4));
			SendMessageW(details, EM_SETSEL, 0, 0);

			xucrun::ui::CreateDialogControl(dialog, 0, L"BUTTON", xucrun::localization::GetText(
				xucrun::localization::TextId::ProgramUpdateAll).c_str(),
				BS_PUSHBUTTON | WS_TABSTOP, 40, 248, 98, 14, 1700);
			xucrun::ui::CreateDialogControl(dialog, 0, L"BUTTON", xucrun::localization::GetText(
				xucrun::localization::TextId::ProgramUpdateCurrent).c_str(),
				BS_PUSHBUTTON | WS_TABSTOP, 146, 248, 98, 14, 1701);
			HWND cancelButton = xucrun::ui::CreateDialogControl(dialog, 0, L"BUTTON", xucrun::localization::GetText(
				xucrun::localization::TextId::ProgramUpdateCancel).c_str(),
				BS_DEFPUSHBUTTON | WS_TABSTOP, 252, 248, 98, 14, IDCANCEL);
			SetFocus(cancelButton);
			MessageBeep(context->signatureValid ? MB_ICONQUESTION : MB_ICONWARNING);
			return FALSE;
		}

		if (message == WM_COMMAND)
		{
			auto* context = reinterpret_cast<DialogContext*>(
				GetWindowLongPtrW(dialog, DWLP_USER));
			if (!context)
				return FALSE;
			if (LOWORD(wParam) == 1700)
				context->choice = ProgramHashUpdateChoice::AllAccounts;
			else if (LOWORD(wParam) == 1701)
				context->choice = ProgramHashUpdateChoice::CurrentAccount;
			else if (LOWORD(wParam) == IDCANCEL)
				context->choice = ProgramHashUpdateChoice::Cancel;
			else
				return FALSE;
			EndDialog(dialog, context->choice == ProgramHashUpdateChoice::Cancel
				? IDCANCEL : IDOK);
			return TRUE;
		}
		if (message == WM_CLOSE)
		{
			auto* context = reinterpret_cast<DialogContext*>(
				GetWindowLongPtrW(dialog, DWLP_USER));
			if (context)
				context->choice = ProgramHashUpdateChoice::Cancel;
			EndDialog(dialog, IDCANCEL);
			return TRUE;
		}
		return FALSE;
	}
}

namespace xucrun::program_hash_update_dialog
{
	ProgramHashUpdateChoice ShowProgramHashUpdateDialog(
		const std::wstring& message, bool signatureValid)
	{
		struct DialogTemplateHeader
		{
			WORD dlgVer; WORD signature; DWORD helpID; DWORD exStyle; DWORD style;
			WORD itemCount; short x; short y; short cx; short cy;
		};
		BYTE buffer[1024] = {};
		auto* dialogTemplate = reinterpret_cast<DialogTemplateHeader*>(buffer);
		dialogTemplate->dlgVer = 1;
		dialogTemplate->signature = 0xFFFF;
		dialogTemplate->style = DS_SETFONT | DS_CENTER | WS_POPUP | WS_CAPTION |
			WS_SYSMENU | WS_BORDER;
		dialogTemplate->cx = 390;
		dialogTemplate->cy = 270;
		WORD* data = reinterpret_cast<WORD*>(buffer + sizeof(DialogTemplateHeader));
		*data++ = 0; *data++ = 0;
		const std::wstring title = ui::GetWindowTitle(localization::GetText(
			localization::TextId::ProgramUpdateTitle));
		wcscpy_s(reinterpret_cast<wchar_t*>(data), 100, title.c_str());
		data += title.size() + 1;
		ui::WriteDefaultDialogFont(data, 50);

		std::wstring displayMessage;
		displayMessage.reserve(message.size() + 32);
		for (std::size_t index = 0; index < message.size(); index++)
		{
			if (message[index] == L'\n' &&
				(index == 0 || message[index - 1] != L'\r'))
			{
				displayMessage += L'\r';
			}
			displayMessage += message[index];
		}
		DialogContext context = { &displayMessage, signatureValid };
		DialogBoxIndirectParamW(GetModuleHandle(nullptr),
			reinterpret_cast<LPCDLGTEMPLATEW>(buffer), ui::GetDialogOwnerWindow(),
			DialogProc, reinterpret_cast<LPARAM>(&context));
		return context.choice;
	}
}