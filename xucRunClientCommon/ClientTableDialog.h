// Purpose: Defines a reusable read-only table dialog for client information views.
// Approach: Exposes the declarations, constants, and types consumed by the components that provide this behavior.
// Behavior: Displays localized columns and rows in a resizable Win32 list view.
#pragma once

#include <Windows.h>

#include <string>
#include <vector>

namespace xucrun::table_dialog
{
	struct Column
	{
		std::wstring heading;
		int preferredWidth;
		bool abbreviateLongValues = false;
	};

	using Row = std::vector<std::wstring>;

	enum class TableDialogAction
	{
		Closed,
		Action,
		SecondaryAction,
		SelectionAction
	};

	struct TableDialogResult
	{
		TableDialogAction action = TableDialogAction::Closed;
		int selectedRow = -1;
	};

	TableDialogResult Show(const std::wstring& windowTitle, const std::wstring& summary,
		const std::wstring& note, const std::vector<Column>& columns,
		const std::vector<Row>& rows, HWND owner,
		const std::wstring& actionText = L"",
		const std::wstring& secondaryActionText = L"",
		const std::wstring& selectionActionText = L"",
		int initialWidth = 560, int minimumWidth = 720);
}