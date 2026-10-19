// Purpose: Implements a reusable read-only table dialog for client information views.
// Behavior: Uses a resizable report-style list view with stable column widths.
#include "ClientTableDialog.h"

#include <CommCtrl.h>

#include <algorithm>
#include <numeric>

#include "ClientUiCommon.h"

#pragma comment(lib, "Comctl32.lib")

namespace
{
	constexpr int SummaryId = 1700;
	constexpr int TableId = 1701;
	constexpr int NoteId = 1702;
	constexpr int ActionId = 1703;
	constexpr int SecondaryActionId = 1704;
	constexpr int SelectionActionId = 1705;

	struct TableContext
	{
		const std::wstring* title;
		const std::wstring* summary;
		const std::wstring* note;
		const std::wstring* actionText;
		const std::wstring* secondaryActionText;
		const std::wstring* selectionActionText;
		const std::vector<xucrun::table_dialog::Column>* columns;
		const std::vector<xucrun::table_dialog::Row>* rows;
		HWND tooltip = nullptr;
		std::wstring tooltipText;
		int selectedRow = -1;
		int sortColumn = -1;
		bool sortAscending = true;
		int minimumWidth = 720;
	};

	std::wstring FormatCellText(
		const xucrun::table_dialog::Column& column, const std::wstring& value)
	{
		constexpr std::size_t VisibleEndCharacters = 12;
		constexpr std::size_t EllipsisCharacters = 3;
		constexpr std::size_t MaximumVisibleCharacters =
			2 * VisibleEndCharacters + EllipsisCharacters;
		if (!column.abbreviateLongValues ||
			value.size() <= MaximumVisibleCharacters)
		{
			return value;
		}
		return value.substr(0, VisibleEndCharacters) + L"..." +
			value.substr(value.size() - VisibleEndCharacters);
	}

	void UpdateSortIndicator(HWND table, int sortColumn, bool ascending)
	{
		HWND header = ListView_GetHeader(table);
		const int columnCount = Header_GetItemCount(header);
		for (int column = 0; column < columnCount; column++)
		{
			HDITEMW item = {};
			item.mask = HDI_FORMAT;
			if (!Header_GetItem(header, column, &item))
				continue;
			item.fmt &= ~(HDF_SORTUP | HDF_SORTDOWN);
			if (column == sortColumn)
				item.fmt |= ascending ? HDF_SORTUP : HDF_SORTDOWN;
			Header_SetItem(header, column, &item);
		}
	}

	int CALLBACK CompareRows(LPARAM leftValue, LPARAM rightValue, LPARAM contextValue)
	{
		const auto* context = reinterpret_cast<const TableContext*>(contextValue);
		const std::size_t leftRow = static_cast<std::size_t>(leftValue);
		const std::size_t rightRow = static_cast<std::size_t>(rightValue);
		if (!context || context->sortColumn < 0 ||
			leftRow >= context->rows->size() || rightRow >= context->rows->size())
		{
			return 0;
		}
		const auto& left = (*context->rows)[leftRow];
		const auto& right = (*context->rows)[rightRow];
		const std::size_t column = static_cast<std::size_t>(context->sortColumn);
		const std::wstring empty;
		const std::wstring& leftText = column < left.size() ? left[column] : empty;
		const std::wstring& rightText = column < right.size() ? right[column] : empty;
		int comparison = _wcsicmp(leftText.c_str(), rightText.c_str());
		if (comparison == 0)
			comparison = leftRow < rightRow ? -1 : leftRow > rightRow ? 1 : 0;
		return context->sortAscending ? comparison : -comparison;
	}

	void SortTable(HWND dialog, int column)
	{
		auto* context = reinterpret_cast<TableContext*>(
			GetWindowLongPtrW(dialog, DWLP_USER));
		if (!context || column < 0 ||
			column >= static_cast<int>(context->columns->size()))
		{
			return;
		}
		context->sortAscending = context->sortColumn != column ||
			!context->sortAscending;
		context->sortColumn = column;
		HWND table = GetDlgItem(dialog, TableId);
		ListView_SortItems(table, CompareRows, context);
		UpdateSortIndicator(table, column, context->sortAscending);
	}

	int GetSelectedSourceRow(HWND dialog)
	{
		HWND table = GetDlgItem(dialog, TableId);
		const int selectedItem = ListView_GetNextItem(table, -1, LVNI_SELECTED);
		if (selectedItem < 0)
			return -1;
		LVITEMW item = {};
		item.mask = LVIF_PARAM;
		item.iItem = selectedItem;
		return ListView_GetItem(table, &item) ? static_cast<int>(item.lParam) : -1;
	}

	void LayoutTable(HWND dialog)
	{
		auto* context = reinterpret_cast<const TableContext*>(
			GetWindowLongPtrW(dialog, DWLP_USER));
		if (!context)
			return;

		RECT clientRect = {};
		GetClientRect(dialog, &clientRect);
		const int width = static_cast<int>(clientRect.right - clientRect.left);
		const int height = static_cast<int>(clientRect.bottom - clientRect.top);
		const UINT dpi = GetDpiForWindow(dialog);
		const int margin = MulDiv(12, dpi ? dpi : 96, 96);
		const int summaryHeight = context->summary->empty()
			? 0 : MulDiv(54, dpi ? dpi : 96, 96);
		const int noteHeight = context->note->empty()
			? 0 : MulDiv(54, dpi ? dpi : 96, 96);
		const int buttonWidth = MulDiv(76, dpi ? dpi : 96, 96);
		const int actionWidth = MulDiv(170, dpi ? dpi : 96, 96);
		const int buttonHeight = MulDiv(26, dpi ? dpi : 96, 96);
		const int gap = MulDiv(8, dpi ? dpi : 96, 96);
		const int buttonY = height - margin - buttonHeight;
		const int noteY = buttonY - (noteHeight > 0 ? gap + noteHeight : 0);
		const int tableY = margin + summaryHeight;

		MoveWindow(GetDlgItem(dialog, SummaryId), margin, margin,
			(std::max)(1, width - 2 * margin), summaryHeight, FALSE);
		HWND table = GetDlgItem(dialog, TableId);
		MoveWindow(table, margin, tableY, (std::max)(1, width - 2 * margin),
			(std::max)(1, noteY - gap - tableY), FALSE);
		MoveWindow(GetDlgItem(dialog, NoteId), margin, noteY,
			(std::max)(1, width - 2 * margin), noteHeight, FALSE);
		HWND actionButton = GetDlgItem(dialog, ActionId);
		if (actionButton)
		{
			HWND secondaryActionButton = GetDlgItem(dialog, SecondaryActionId);
			HWND selectionActionButton = GetDlgItem(dialog, SelectionActionId);
			const int actionCount = 1 + (secondaryActionButton ? 1 : 0) +
				(selectionActionButton ? 1 : 0);
			const int buttonsWidth = actionWidth * actionCount + gap * actionCount +
				buttonWidth;
			const int buttonsX = (width - buttonsWidth) / 2;
			MoveWindow(actionButton, buttonsX, buttonY,
				actionWidth, buttonHeight, FALSE);
			if (secondaryActionButton)
			{
				MoveWindow(secondaryActionButton, buttonsX + actionWidth + gap,
					buttonY, actionWidth, buttonHeight, FALSE);
			}
			if (selectionActionButton)
			{
				const int selectionIndex = 1 + (secondaryActionButton ? 1 : 0);
				MoveWindow(selectionActionButton,
					buttonsX + selectionIndex * (actionWidth + gap), buttonY,
					actionWidth, buttonHeight, FALSE);
			}
			MoveWindow(GetDlgItem(dialog, IDOK),
				buttonsX + actionWidth * actionCount + gap * actionCount,
				buttonY, buttonWidth, buttonHeight, FALSE);
		}
		else
		{
			MoveWindow(GetDlgItem(dialog, IDOK), (width - buttonWidth) / 2,
				buttonY, buttonWidth, buttonHeight, FALSE);
		}

		RECT tableRect = {};
		GetClientRect(table, &tableRect);
		const int availableWidth = (std::max)(1,
			static_cast<int>(tableRect.right - tableRect.left) -
			GetSystemMetrics(SM_CXVSCROLL) - 2);
		const int preferredWidth = std::accumulate(
			context->columns->begin(), context->columns->end(), 0,
			[](int sum, const xucrun::table_dialog::Column& column) {
				return sum + column.preferredWidth;
			});
		for (int index = 0; index < static_cast<int>(context->columns->size()); index++)
		{
			const int columnWidth = preferredWidth > 0 && preferredWidth < availableWidth
				? (*context->columns)[index].preferredWidth * availableWidth / preferredWidth
				: MulDiv((*context->columns)[index].preferredWidth, dpi ? dpi : 96, 96);
			ListView_SetColumnWidth(table, index, columnWidth);
		}
		RedrawWindow(dialog, nullptr, nullptr,
			RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
	}

	INT_PTR CALLBACK TableDialogProc(HWND dialog, UINT message,
		WPARAM wParam, LPARAM lParam)
	{
		switch (message)
		{
		case WM_INITDIALOG:
		{
			auto* context = reinterpret_cast<TableContext*>(lParam);
			SetWindowLongPtrW(dialog, DWLP_USER, reinterpret_cast<LONG_PTR>(context));
			xucrun::ui::ApplyApplicationIcon(dialog);
			SetWindowTextW(dialog, context->title->c_str());
			SetWindowPos(dialog, HWND_TOPMOST, 0, 0, 0, 0,
				SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
			SetForegroundWindow(dialog);

			xucrun::ui::CreateDialogControl(dialog, 0, L"STATIC",
				context->summary->c_str(), SS_LEFT, 8, 8, 464, 34, SummaryId);
			HWND table = xucrun::ui::CreateDialogControl(dialog, WS_EX_CLIENTEDGE,
				WC_LISTVIEWW, nullptr, WS_TABSTOP | WS_VSCROLL | WS_HSCROLL |
				LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
				8, 42, 464, 190, TableId);
			ListView_SetExtendedListViewStyle(table,
				LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
			context->tooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
				WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
				CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
				dialog, nullptr, GetModuleHandleW(nullptr), nullptr);
			if (context->tooltip)
			{
				TOOLINFOW tool = { sizeof(tool) };
				tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
				tool.hwnd = dialog;
				tool.uId = reinterpret_cast<UINT_PTR>(table);
				tool.lpszText = LPSTR_TEXTCALLBACKW;
				SendMessageW(context->tooltip, TTM_ADDTOOLW, 0,
					reinterpret_cast<LPARAM>(&tool));
				SendMessageW(context->tooltip, TTM_SETMAXTIPWIDTH, 0, 800);
			}

			for (int column = 0; column < static_cast<int>(context->columns->size()); column++)
			{
				LVCOLUMNW item = {};
				item.mask = LVCF_TEXT | LVCF_FMT;
				item.fmt = LVCFMT_LEFT;
				item.pszText = const_cast<LPWSTR>(
					(*context->columns)[column].heading.c_str());
				ListView_InsertColumn(table, column, &item);
			}
			for (int row = 0; row < static_cast<int>(context->rows->size()); row++)
			{
				const auto& values = (*context->rows)[row];
				LVITEMW item = {};
				item.mask = LVIF_TEXT | LVIF_PARAM;
				item.iItem = row;
				item.lParam = row;
				const std::wstring firstText = values.empty()
					? L"" : FormatCellText((*context->columns)[0], values[0]);
				item.pszText = const_cast<LPWSTR>(firstText.c_str());
				ListView_InsertItem(table, &item);
				for (int column = 1; column < static_cast<int>(values.size()) &&
					column < static_cast<int>(context->columns->size()); column++)
				{
					const std::wstring displayText = FormatCellText(
						(*context->columns)[column], values[column]);
					ListView_SetItemText(table, row, column,
						const_cast<LPWSTR>(displayText.c_str()));
				}
			}

			xucrun::ui::CreateDialogControl(dialog, 0, L"STATIC",
				context->note->c_str(), SS_LEFT, 8, 238, 464, 30, NoteId);
			if (!context->actionText->empty())
			{
				xucrun::ui::CreateDialogControl(dialog, 0, L"BUTTON",
					context->actionText->c_str(), BS_PUSHBUTTON | WS_TABSTOP,
					125, 274, 140, 14, ActionId);
			}
			if (!context->secondaryActionText->empty())
			{
				xucrun::ui::CreateDialogControl(dialog, 0, L"BUTTON",
					context->secondaryActionText->c_str(), BS_PUSHBUTTON | WS_TABSTOP,
					273, 274, 140, 14, SecondaryActionId);
			}
			if (!context->selectionActionText->empty())
			{
				HWND selectionAction = xucrun::ui::CreateDialogControl(dialog, 0,
					L"BUTTON", context->selectionActionText->c_str(),
					BS_PUSHBUTTON | WS_TABSTOP, 421, 274, 140, 14,
					SelectionActionId);
				EnableWindow(selectionAction, FALSE);
			}
			HWND okButton = xucrun::ui::CreateDialogControl(dialog, 0, L"BUTTON", L"OK",
				BS_DEFPUSHBUTTON | WS_TABSTOP, 202, 274, 76, 14, IDOK);
			LayoutTable(dialog);
			SetFocus(okButton);
			return FALSE;
		}

		case WM_SIZE:
			LayoutTable(dialog);
			return TRUE;

		case WM_NOTIFY:
		{
			const auto* header = reinterpret_cast<const NMHDR*>(lParam);
			auto* context = reinterpret_cast<TableContext*>(
				GetWindowLongPtrW(dialog, DWLP_USER));
			if (header && context && header->hwndFrom == context->tooltip &&
				header->code == TTN_GETDISPINFOW)
			{
				context->tooltipText.clear();
				POINT cursor = {};
				GetCursorPos(&cursor);
				HWND table = GetDlgItem(dialog, TableId);
				ScreenToClient(table, &cursor);
				LVHITTESTINFO hit = {};
				hit.pt = cursor;
				const int itemIndex = ListView_SubItemHitTest(table, &hit);
				if (itemIndex >= 0 && hit.iSubItem >= 0 &&
					hit.iSubItem < static_cast<int>(context->columns->size()) &&
					(*context->columns)[hit.iSubItem].abbreviateLongValues)
				{
					LVITEMW item = {};
					item.mask = LVIF_PARAM;
					item.iItem = itemIndex;
					if (ListView_GetItem(table, &item))
					{
						const std::size_t row = static_cast<std::size_t>(item.lParam);
						const std::size_t column =
							static_cast<std::size_t>(hit.iSubItem);
						if (row < context->rows->size() &&
							column < (*context->rows)[row].size())
						{
							context->tooltipText = (*context->rows)[row][column];
						}
					}
				}
				auto* tooltip = reinterpret_cast<NMTTDISPINFOW*>(lParam);
				tooltip->lpszText = context->tooltipText.empty()
					? const_cast<LPWSTR>(L"")
					: context->tooltipText.data();
				return TRUE;
			}
			if (header && header->idFrom == TableId &&
				header->code == LVN_COLUMNCLICK)
			{
				SortTable(dialog,
					reinterpret_cast<const NMLISTVIEW*>(lParam)->iSubItem);
				return TRUE;
			}
			if (header && header->idFrom == TableId &&
				header->code == LVN_ITEMCHANGED)
			{
				EnableWindow(GetDlgItem(dialog, SelectionActionId),
					ListView_GetNextItem(GetDlgItem(dialog, TableId),
						-1, LVNI_SELECTED) >= 0);
				return TRUE;
			}
			break;
		}

		case WM_GETMINMAXINFO:
		{
			auto* sizeInfo = reinterpret_cast<MINMAXINFO*>(lParam);
			const UINT dpi = GetDpiForWindow(dialog);
			const auto* context = reinterpret_cast<const TableContext*>(
				GetWindowLongPtrW(dialog, DWLP_USER));
			const int minimumWidth = context ? context->minimumWidth : 720;
			sizeInfo->ptMinTrackSize.x = MulDiv(
				minimumWidth, dpi ? dpi : 96, 96);
			sizeInfo->ptMinTrackSize.y = MulDiv(360, dpi ? dpi : 96, 96);
			return TRUE;
		}

		case WM_COMMAND:
			if (LOWORD(wParam) == ActionId)
			{
				auto* context = reinterpret_cast<TableContext*>(
					GetWindowLongPtrW(dialog, DWLP_USER));
				if (context)
					context->selectedRow = GetSelectedSourceRow(dialog);
				EndDialog(dialog, ActionId);
				return TRUE;
			}
			if (LOWORD(wParam) == SecondaryActionId)
			{
				auto* context = reinterpret_cast<TableContext*>(
					GetWindowLongPtrW(dialog, DWLP_USER));
				if (context)
					context->selectedRow = GetSelectedSourceRow(dialog);
				EndDialog(dialog, SecondaryActionId);
				return TRUE;
			}
			if (LOWORD(wParam) == SelectionActionId)
			{
				auto* context = reinterpret_cast<TableContext*>(
					GetWindowLongPtrW(dialog, DWLP_USER));
				if (context)
					context->selectedRow = GetSelectedSourceRow(dialog);
				EndDialog(dialog, SelectionActionId);
				return TRUE;
			}
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

namespace xucrun::table_dialog
{
	TableDialogResult Show(const std::wstring& windowTitle, const std::wstring& summary,
		const std::wstring& note, const std::vector<Column>& columns,
		const std::vector<Row>& rows, HWND owner, const std::wstring& actionText,
		const std::wstring& secondaryActionText,
		const std::wstring& selectionActionText,
		int initialWidth, int minimumWidth)
	{
		INITCOMMONCONTROLSEX controls = {
			sizeof(controls), ICC_LISTVIEW_CLASSES };
		InitCommonControlsEx(&controls);

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
		dialogTemplate->cx = static_cast<short>(initialWidth);
		dialogTemplate->cy = 300;

		WORD* data = reinterpret_cast<WORD*>(buffer + sizeof(DialogTemplateHeader));
		*data++ = 0;
		*data++ = 0;
		*data++ = 0;
		xucrun::ui::WriteDefaultDialogFont(data, 50);

		TableContext context = {};
		context.title = &windowTitle;
		context.summary = &summary;
		context.note = &note;
		context.actionText = &actionText;
		context.secondaryActionText = &secondaryActionText;
		context.selectionActionText = &selectionActionText;
		context.columns = &columns;
		context.rows = &rows;
		context.minimumWidth = minimumWidth;
		const INT_PTR result = DialogBoxIndirectParamW(GetModuleHandleW(nullptr),
			reinterpret_cast<LPCDLGTEMPLATEW>(buffer), owner, TableDialogProc,
			reinterpret_cast<LPARAM>(&context));
		return {
			result == ActionId ? TableDialogAction::Action
				: result == SecondaryActionId ? TableDialogAction::SecondaryAction
				: result == SelectionActionId ? TableDialogAction::SelectionAction
				: TableDialogAction::Closed,
			context.selectedRow };
	}
}