// Purpose: Implements the client-side selection dialog for local Windows accounts.
// Approach: Implements the behavior with focused helpers built on shared xucRun interfaces and Windows APIs.
// Behavior: Creates a modal Win32 dialog and separates display text from canonical account names.
#include "ClientLocalAccountSelectionDialog.h"

#include "ClientUiCommon.h"
#include "Localization.h"

namespace
{
	constexpr UINT AccountListId = 1500;

	struct SelectionContext
	{
		const std::vector<std::wstring>* accounts;
		const std::vector<std::wstring>* displayAccounts;
		const std::vector<bool>* storedAccounts;
		std::wstring* selectedAccount;
		const std::wstring* currentAccount;
		std::size_t currentAccountIndex;
		bool noStoredCredentials;
	};

	void UpdatePrimaryButton(HWND dialog, const SelectionContext* context,
		LRESULT selectedIndex)
	{
		const bool stored = context && context->storedAccounts &&
			selectedIndex >= 0 &&
			static_cast<std::size_t>(selectedIndex) < context->storedAccounts->size() &&
			(*context->storedAccounts)[static_cast<std::size_t>(selectedIndex)];
		SetWindowTextW(GetDlgItem(dialog, IDOK), xucrun::localization::GetText(
			stored ? xucrun::localization::TextId::ButtonUpdateCredential
				: xucrun::localization::TextId::ButtonCreateCredential).c_str());
	}

	INT_PTR CALLBACK DialogProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
	{
		if (message == WM_INITDIALOG)
		{
			auto* context = reinterpret_cast<SelectionContext*>(lParam);
			SetWindowLongPtrW(dialog, DWLP_USER, reinterpret_cast<LONG_PTR>(context));
			xucrun::ui::ApplyApplicationIcon(dialog);
			SetWindowTextW(dialog, xucrun::ui::GetWindowTitle(
				xucrun::localization::GetText(
					xucrun::localization::TextId::AccountSelectionTitle)).c_str());
			SetWindowPos(dialog, HWND_TOPMOST, 0, 0, 0, 0,
				SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
			SetForegroundWindow(dialog);

			std::wstring headerText;
			if (context && context->noStoredCredentials)
			{
				headerText = xucrun::localization::GetText(
					xucrun::localization::TextId::AccountSelectionFirstUse);
			}
			headerText += xucrun::localization::Format(
				xucrun::localization::TextId::AccountSelectionHeader,
				{ context && context->currentAccount
					? *context->currentAccount
					: xucrun::localization::GetText(
						xucrun::localization::TextId::StatusAccountUnavailable) });
			const int headerHeight = xucrun::ui::MeasureDialogTextHeight(
				dialog, headerText, 284, 25);
			const int listY = 9 + headerHeight;
			const int buttonY = listY + 134;
			xucrun::ui::ResizeDialogClientHeight(dialog, buttonY + 22);
			xucrun::ui::CreateDialogControl(dialog, 0, L"STATIC", headerText.c_str(),
				SS_LEFT, 8, 7, 284, headerHeight);

			HWND list = xucrun::ui::CreateDialogControl(dialog, WS_EX_CLIENTEDGE,
				L"LISTBOX", nullptr, WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY |
				LBS_NOINTEGRALHEIGHT | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS,
				8, listY, 284, 126, AccountListId);
			if (context && context->displayAccounts)
			{
				for (const auto& account : *context->displayAccounts)
				{
					SendMessageW(list, LB_ADDSTRING, 0,
						reinterpret_cast<LPARAM>(account.c_str()));
				}
			}
			LRESULT initialIndex = LB_ERR;
			if (context && context->accounts)
			{
				for (std::size_t index = 0; index < context->accounts->size(); index++)
				{
					if (index != context->currentAccountIndex)
					{
						initialIndex = static_cast<LRESULT>(index);
						break;
					}
				}
			}
			if (initialIndex != LB_ERR)
				SendMessageW(list, LB_SETCURSEL, initialIndex, 0);

			HWND okButton = xucrun::ui::CreateDialogControl(dialog, 0, L"BUTTON",
				xucrun::localization::GetText(
				xucrun::localization::TextId::ButtonNext).c_str(),
				WS_TABSTOP | BS_DEFPUSHBUTTON, 80, buttonY, 78, 14, IDOK);
			xucrun::ui::CreateDialogControl(dialog, 0, L"BUTTON", xucrun::localization::GetText(
				xucrun::localization::TextId::ButtonCancel).c_str(),
				WS_TABSTOP | BS_PUSHBUTTON, 164, buttonY, 52, 14, IDCANCEL);
			UpdatePrimaryButton(dialog, context, static_cast<LRESULT>(initialIndex));
			EnableWindow(okButton, initialIndex != LB_ERR);
			SetFocus(list);
			return FALSE;
		}

		if (message == WM_MEASUREITEM)
		{
			auto* measureItem = reinterpret_cast<MEASUREITEMSTRUCT*>(lParam);
			if (measureItem && measureItem->CtlID == AccountListId)
			{
				RECT itemBounds = { 0, 0, 0, 10 };
				MapDialogRect(dialog, &itemBounds);
				measureItem->itemHeight = itemBounds.bottom;
				return TRUE;
			}
		}

		if (message == WM_DRAWITEM)
		{
			auto* context = reinterpret_cast<SelectionContext*>(
				GetWindowLongPtrW(dialog, DWLP_USER));
			auto* drawItem = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
			if (!context || !drawItem || drawItem->CtlID != AccountListId ||
				drawItem->itemID == static_cast<UINT>(-1))
			{
				return FALSE;
			}

			std::wstring text;
			if (context->displayAccounts &&
				drawItem->itemID < context->displayAccounts->size())
			{
				text = (*context->displayAccounts)[drawItem->itemID];
			}
			const bool disabled = drawItem->itemID == context->currentAccountIndex;
			const bool selected = (drawItem->itemState & ODS_SELECTED) != 0;
			const COLORREF backgroundColor = selected && !disabled
				? GetSysColor(COLOR_HIGHLIGHT) : GetSysColor(COLOR_WINDOW);
			const COLORREF textColor = disabled
				? GetSysColor(COLOR_GRAYTEXT)
				: GetSysColor(selected ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT);
			FillRect(drawItem->hDC, &drawItem->rcItem,
				GetSysColorBrush(selected && !disabled ? COLOR_HIGHLIGHT : COLOR_WINDOW));
			SetBkColor(drawItem->hDC, backgroundColor);
			SetTextColor(drawItem->hDC, textColor);
			RECT textRect = drawItem->rcItem;
			RECT padding = { 0, 0, 2, 0 };
			MapDialogRect(dialog, &padding);
			textRect.left += padding.right;
			DrawTextW(drawItem->hDC, text.c_str(), -1, &textRect,
				DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
			if ((drawItem->itemState & ODS_FOCUS) != 0 && !disabled)
				DrawFocusRect(drawItem->hDC, &drawItem->rcItem);
			return TRUE;
		}

		if (message == WM_COMMAND)
		{
			const WORD command = LOWORD(wParam);
			if (command == AccountListId && HIWORD(wParam) == LBN_SELCHANGE)
			{
				auto* context = reinterpret_cast<SelectionContext*>(
					GetWindowLongPtrW(dialog, DWLP_USER));
				const LRESULT selectedIndex = SendMessageW(
					GetDlgItem(dialog, AccountListId), LB_GETCURSEL, 0, 0);
				EnableWindow(GetDlgItem(dialog, IDOK), !context ||
					selectedIndex == LB_ERR || static_cast<std::size_t>(selectedIndex) !=
					context->currentAccountIndex);
				UpdatePrimaryButton(dialog, context, selectedIndex);
				return TRUE;
			}
			if (command == IDOK ||
				(command == AccountListId && HIWORD(wParam) == LBN_DBLCLK))
			{
				auto* context = reinterpret_cast<SelectionContext*>(
					GetWindowLongPtrW(dialog, DWLP_USER));
				const LRESULT selectedIndex = SendMessageW(
					GetDlgItem(dialog, AccountListId), LB_GETCURSEL, 0, 0);
				if (!context || !context->accounts || !context->selectedAccount ||
					selectedIndex == LB_ERR)
				{
					MessageBeep(MB_ICONWARNING);
					return TRUE;
				}
				if (static_cast<std::size_t>(selectedIndex) ==
					context->currentAccountIndex)
				{
					MessageBeep(MB_ICONWARNING);
					return TRUE;
				}
				context->selectedAccount->clear();
				if (static_cast<std::size_t>(selectedIndex) < context->accounts->size())
				{
					*context->selectedAccount =
						(*context->accounts)[static_cast<std::size_t>(selectedIndex)];
				}
				EndDialog(dialog, IDOK);
				return TRUE;
			}
			if (command == IDCANCEL)
			{
				EndDialog(dialog, IDCANCEL);
				return TRUE;
			}
		}

		if (message == WM_CLOSE)
		{
			EndDialog(dialog, IDCANCEL);
			return TRUE;
		}
		return FALSE;
	}
}

namespace xucrun::local_account_selection_dialog
{
	bool Show(const std::vector<std::wstring>& accounts,
		const std::vector<std::wstring>& displayAccounts,
		const std::vector<bool>& storedAccounts,
		const std::wstring& currentAccount, std::size_t currentAccountIndex,
		bool noStoredCredentials, std::wstring& selectedAccount, HWND owner)
	{
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
			WS_SYSMENU | WS_BORDER;
		dialogTemplate->cx = 300;
		dialogTemplate->cy = 190;
		WORD* data = reinterpret_cast<WORD*>(buffer + sizeof(DialogTemplateHeader));
		*data++ = 0;
		*data++ = 0;
		const std::wstring title = ui::GetWindowTitle(localization::GetText(
			localization::TextId::AccountSelectionTitle));
		wcscpy_s(reinterpret_cast<wchar_t*>(data), 100, title.c_str());
		data += title.size() + 1;
		ui::WriteDefaultDialogFont(data, 50);

		selectedAccount.clear();
		SelectionContext context = { &accounts, &displayAccounts, &storedAccounts,
			&selectedAccount, &currentAccount, currentAccountIndex, noStoredCredentials };
		const INT_PTR result = DialogBoxIndirectParamW(GetModuleHandle(nullptr),
			reinterpret_cast<LPCDLGTEMPLATEW>(buffer),
			owner ? owner : ui::GetDialogOwnerWindow(), DialogProc,
			reinterpret_cast<LPARAM>(&context));
		return result == IDOK;
	}
}