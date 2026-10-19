// Purpose: Declares secure filesystem operations used by broker backup, restore, and purge workflows.
#pragma once

#include <Windows.h>

#include <string>
#include <vector>

namespace xucrun::broker_directory
{
	enum class OpenMode
	{
		OpenExisting,
		CreateNew
	};

	struct Entry
	{
		std::wstring name;
		DWORD attributes = 0;
	};

	bool OpenFile(HANDLE directory, const std::wstring& name,
		DWORD desiredAccess, DWORD shareAccess, OpenMode mode,
		HANDLE& file, DWORD& error);
	bool RenameFile(HANDLE file, HANDLE directory,
		const std::wstring& newName, DWORD& error);
	bool MarkFileForDeletion(HANDLE file, DWORD& error);
	bool Flush(HANDLE directory, DWORD& error);
	bool DeleteFile(HANDLE directory, const std::wstring& name,
		bool missingIsSuccess, DWORD& error);
	bool Enumerate(HANDLE directory, std::vector<Entry>& entries, DWORD& error);
}
