// Purpose: Implements multi-selection of broker approvals to reset.
// Behavior: Creates a modal Win32 list dialog and keeps selected indices in dialog context.
#include "ClientApprovalSelectionDialog.h"

#include <Windows.h>

#include "ClientUiCommon.h"
#include "DocumentationNameMapping.h"
#include "Localization.h"

namespace
{
	using xucrun::approval_selection_dialog::ResetApprovalOption;

	struct DialogContext
	{
		const std::wstring* invokingAccount;
		const std::wstring* targetAccount;
		const std::vector<ResetApprovalOption>* options;
		bool canGoBack;
		std::vector<int> selectedIndices;
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
					xucrun::localization::TextId::ResetTitle)).c_str());
			SetWindowPos(dialog, HWND_TOPMOST, 0, 0, 0, 0,
				SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
			SetForegroundWindow(dialog);
			const std::wstring header = xucrun::localization::Format(
				xucrun::localization::TextId::ResetHeader,
				{ xucrun::documentation_names::DisplayAccount(
						*context->invokingAccount),
					xucrun::documentation_names::DisplayAccount(
						*context->targetAccount),
					std::to_wstring(context->options->size()) });
			xucrun::ui::CreateDialogControl(dialog, 0, L"STATIC", header.c_str(),
				SS_LEFT, 8, 8, 364, 20);
			xucrun::ui::CreateDialogControl(dialog, 0, L"STATIC", xucrun::localization::GetText(
				xucrun::localization::TextId::ResetSelectionHint).c_str(),
				SS_LEFT, 8, 30, 364, 18);
			HWND list = xucrun::ui::CreateDialogControl(dialog, WS_EX_CLIENTEDGE,
				L"LISTBOX", nullptr, WS_TABSTOP | WS_VSCROLL | WS_HSCROLL |
				LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | LBS_EXTENDEDSEL,
				8, 50, 364, 145, 1500);
			SendMessageW(list, LB_SETHORIZONTALEXTENT, 1600, 0);
			for (const auto& option : *context->options)
			{
				SendMessageW(list, LB_ADDSTRING, 0,
					reinterpret_cast<LPARAM>(option.displayText.c_str()));
			}
			SendMessageW(list, LB_SETSEL, TRUE, 0);

			xucrun::ui::CreateDialogControl(dialog, 0, L"BUTTON",
				xucrun::localization::GetText(
					xucrun::localization::TextId::ButtonSelectAll).c_str(),
				BS_PUSHBUTTON | WS_TABSTOP, 105, 204, 54, 14, 1501);
			xucrun::ui::CreateDialogControl(dialog, 0, L"BUTTON", xucrun::localization::GetText(
				xucrun::localization::TextId::ButtonDelete).c_str(),
				BS_DEFPUSHBUTTON | WS_TABSTOP, 163, 204, 54, 14, IDOK);
			xucrun::ui::CreateDialogControl(dialog, 0, L"BUTTON", xucrun::localization::GetText(
				context->canGoBack ? xucrun::localization::TextId::ButtonBack
					: xucrun::localization::TextId::ButtonCancel).c_str(),
				BS_PUSHBUTTON | WS_TABSTOP, 221, 204, 54, 14, IDCANCEL);
			SetFocus(list);
			return FALSE;
		}

		if (message == WM_COMMAND)
		{
			auto* context = reinterpret_cast<DialogContext*>(
				GetWindowLongPtrW(dialog, DWLP_USER));
			if (!context)
				return FALSE;
			if (LOWORD(wParam) == 1501)
			{
				SendMessageW(GetDlgItem(dialog, 1500), LB_SETSEL, TRUE, -1);
				return TRUE;
			}
			if (LOWORD(wParam) == IDOK)
			{
				HWND list = GetDlgItem(dialog, 1500);
				const LRESULT count = SendMessageW(list, LB_GETSELCOUNT, 0, 0);
				if (count <= 0)
				{
					MessageBeep(MB_ICONWARNING);
					return TRUE;
				}
				context->selectedIndices.resize(static_cast<std::size_t>(count));
				SendMessageW(list, LB_GETSELITEMS, static_cast<WPARAM>(count),
					reinterpret_cast<LPARAM>(context->selectedIndices.data()));
				EndDialog(dialog, IDOK);
				return TRUE;
			}
			if (LOWORD(wParam) == IDCANCEL)
			{
				context->selectedIndices.clear();
				EndDialog(dialog, IDCANCEL);
				return TRUE;
			}
		}
		if (message == WM_CLOSE)
		{
			auto* context = reinterpret_cast<DialogContext*>(
				GetWindowLongPtrW(dialog, DWLP_USER));
			if (context)
				context->selectedIndices.clear();
			EndDialog(dialog, IDCANCEL);
			return TRUE;
		}
		return FALSE;
	}
}

namespace xucrun::approval_selection_dialog
{
	std::vector<int> ShowResetApprovalSelectionDialog(
		const std::wstring& invokingAccount, const std::wstring& targetAccount,
		const std::vector<ResetApprovalOption>& options, bool canGoBack,
		bool& cancelled)
	{
		cancelled = false;
		if (options.empty())
			return {};
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
		dialogTemplate->cx = 380;
		dialogTemplate->cy = 230;
		WORD* data = reinterpret_cast<WORD*>(buffer + sizeof(DialogTemplateHeader));
		*data++ = 0; *data++ = 0;
		const std::wstring title = ui::GetWindowTitle(
			localization::GetText(localization::TextId::ResetTitle));
		wcscpy_s(reinterpret_cast<wchar_t*>(data), 100, title.c_str());
		data += title.size() + 1;
		ui::WriteDefaultDialogFont(data, 50);

		DialogContext context = {
			&invokingAccount, &targetAccount, &options, canGoBack };
		const INT_PTR result = DialogBoxIndirectParamW(GetModuleHandle(nullptr),
			reinterpret_cast<LPCDLGTEMPLATEW>(buffer), ui::GetDialogOwnerWindow(),
			DialogProc, reinterpret_cast<LPARAM>(&context));
		cancelled = result == IDCANCEL;
		return context.selectedIndices;
	}
}