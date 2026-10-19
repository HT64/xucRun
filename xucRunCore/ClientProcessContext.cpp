// Purpose: Determines the process context from which xucRun was invoked.
// Operation: Reads Windows process information and maps known executables to their shell type.
#include "ClientProcessContext.h"

#include <TlHelp32.h>

#include <algorithm>
#include <cwctype>

namespace xucrun::process_context
{
	DWORD GetParentProcessId(DWORD processId)
	{
		HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
		if (snapshot == INVALID_HANDLE_VALUE)
			return 0;

		PROCESSENTRY32W process = {};
		process.dwSize = sizeof(process);
		if (!Process32FirstW(snapshot, &process))
		{
			CloseHandle(snapshot);
			return 0;
		}

		DWORD parentProcessId = 0;
		do
		{
			if (process.th32ProcessID == processId)
			{
				parentProcessId = process.th32ParentProcessID;
				break;
			}
		} while (Process32NextW(snapshot, &process));

		CloseHandle(snapshot);
		return parentProcessId;
	}

	std::wstring GetProcessName(DWORD processId)
	{
		HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
		if (snapshot == INVALID_HANDLE_VALUE)
			return L"";

		PROCESSENTRY32W process = {};
		process.dwSize = sizeof(process);
		if (!Process32FirstW(snapshot, &process))
		{
			CloseHandle(snapshot);
			return L"";
		}

		std::wstring processName;
		do
		{
			if (process.th32ProcessID == processId)
			{
				processName = process.szExeFile;
				break;
			}
		} while (Process32NextW(snapshot, &process));

		CloseHandle(snapshot);
		return processName;
	}

	std::wstring GetProcessPath(DWORD processId)
	{
		HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
		if (!process)
			return L"";

		wchar_t processPath[MAX_PATH] = {};
		DWORD size = MAX_PATH;
		const bool resolved = QueryFullProcessImageNameW(
			process, 0, processPath, &size) == TRUE;
		CloseHandle(process);
		return resolved ? processPath : L"";
	}

	ParentShellType DetectParentShellType(const std::wstring& processName)
	{
		std::wstring lowerName = processName;
		std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
			[](wchar_t character) { return std::towlower(character); });

		if (lowerName == L"cmd.exe")
			return ParentShellType::Cmd;
		if (lowerName == L"powershell.exe")
			return ParentShellType::PowerShell;
		if (lowerName == L"pwsh.exe")
			return ParentShellType::PowerShellCore;
		if (lowerName == L"tcc.exe" || lowerName == L"tcmd.exe")
			return ParentShellType::TCC;
		return ParentShellType::Unknown;
	}

	std::wstring ParentShellTypeToString(ParentShellType type)
	{
		switch (type)
		{
		case ParentShellType::Cmd:
			return L"Command Prompt (cmd.exe)";
		case ParentShellType::PowerShell:
			return L"Windows PowerShell (powershell.exe)";
		case ParentShellType::PowerShellCore:
			return L"PowerShell Core (pwsh.exe)";
		case ParentShellType::TCC:
			return L"Take Command Console (TCC)";
		default:
			return L"Unknown";
		}
	}
}