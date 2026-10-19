// Purpose: Implements console-faithful launch under a target account and parent console replacement.
// Behavior: Detects the parent shell, carries over its arguments, and launches replacement directly or via broker.
#include "ClientConsoleSession.h"

#include <Windows.h>
#include <shellapi.h>

#include "ClientCommandLine.h"
#include "ClientLogging.h"
#include "ClientUiCommon.h"
#include "ClientWindowsError.h"
#include "Localization.h"

namespace xucrun::console_session
{
	ParentShell DetectParentShell()
	{
		ParentShell result;
		const DWORD parentProcessId = process_context::GetParentProcessId(
			GetCurrentProcessId());
		if (parentProcessId == 0)
		{
			result.description = L"Unknown (no parent process found)";
			return result;
		}

		const std::wstring parentProcessName =
			process_context::GetProcessName(parentProcessId);
		if (parentProcessName.empty())
		{
			result.description =
				L"Unknown (parent process name unavailable)";
			return result;
		}

		result.type = process_context::DetectParentShellType(parentProcessName);
		result.description = process_context::ParentShellTypeToString(result.type);
		if (result.type != process_context::ParentShellType::Unknown)
		{
			result.description += L" [PID: " + std::to_wstring(parentProcessId) +
				L", " + parentProcessName + L"]";
		}
		else
		{
			result.description = L"Unknown [PID: " +
				std::to_wstring(parentProcessId) + L", " + parentProcessName + L"]";
		}
		return result;
	}

	std::wstring GetExecutablePath(process_context::ParentShellType type)
	{
		if (type != process_context::ParentShellType::Unknown)
		{
			const DWORD parentProcessId = process_context::GetParentProcessId(
				GetCurrentProcessId());
			if (parentProcessId != 0)
			{
				const std::wstring parentPath =
					process_context::GetProcessPath(parentProcessId);
				if (!parentPath.empty())
				{
					client_logging::Write(
						L"Info", L"Using parent process path: " + parentPath);
					return parentPath;
				}
			}
		}

		switch (type)
		{
		case process_context::ParentShellType::Cmd:
		{
			wchar_t systemPath[MAX_PATH] = {};
			if (GetSystemDirectoryW(systemPath, MAX_PATH) > 0)
				return std::wstring(systemPath) + L"\\cmd.exe";
			return L"cmd.exe";
		}
		case process_context::ParentShellType::PowerShell:
		{
			wchar_t systemPath[MAX_PATH] = {};
			if (GetSystemDirectoryW(systemPath, MAX_PATH) > 0)
			{
				return std::wstring(systemPath) +
					L"\\WindowsPowerShell\\v1.0\\powershell.exe";
			}
			return L"powershell.exe";
		}
		case process_context::ParentShellType::PowerShellCore:
		{
			wchar_t programFilesPath[MAX_PATH] = {};
			if (GetEnvironmentVariableW(
				L"ProgramFiles", programFilesPath, MAX_PATH) > 0)
			{
				const std::wstring pwshPath = std::wstring(programFilesPath) +
					L"\\PowerShell\\7\\pwsh.exe";
				const DWORD attributes = GetFileAttributesW(pwshPath.c_str());
				if (attributes != INVALID_FILE_ATTRIBUTES &&
					(attributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
				{
					return pwshPath;
				}
			}
			return L"pwsh.exe";
		}
		case process_context::ParentShellType::TCC:
			return L"tcc.exe";
		default:
			return L"";
		}
	}

	std::wstring BuildWorkingDirectoryArguments(
		process_context::ParentShellType type,
		const std::wstring& workingDirectory)
	{
		if (workingDirectory.empty())
			return L"";

		switch (type)
		{
		case process_context::ParentShellType::Cmd:
		case process_context::ParentShellType::TCC:
			return L"/k \"cd /d " +
				command_line::EscapeArgument(workingDirectory) + L"\"";
		case process_context::ParentShellType::PowerShell:
		case process_context::ParentShellType::PowerShellCore:
			return L"-NoExit -Command \"Set-Location '" + workingDirectory + L"'\"";
		default:
			return L"";
		}
	}

	bool Launch(process_context::ParentShellType type,
		const std::wstring& parameters, bool requestElevation)
	{
		const std::wstring consolePath = GetExecutablePath(type);
		if (consolePath.empty())
		{
			client_logging::Write(L"Error", L"Could not determine console path");
			return false;
		}

		client_logging::Write(L"Info", requestElevation
			? L"Launching elevated console: " + consolePath
			: L"Launching console: " + consolePath);
		if (!parameters.empty())
			client_logging::Write(L"Info", L"Console parameters: " + parameters);

		SHELLEXECUTEINFOW executeInfo = { sizeof(executeInfo) };
		executeInfo.lpVerb = requestElevation ? L"runas" : nullptr;
		executeInfo.lpFile = consolePath.c_str();
		executeInfo.lpParameters = parameters.empty() ? nullptr : parameters.c_str();
		executeInfo.nShow = SW_SHOWNORMAL;
		executeInfo.fMask = SEE_MASK_NOCLOSEPROCESS;

		wchar_t currentDirectory[MAX_PATH] = {};
		const DWORD directoryLength =
			GetCurrentDirectoryW(MAX_PATH, currentDirectory);
		if (directoryLength > 0 && directoryLength < MAX_PATH)
		{
			executeInfo.lpDirectory = currentDirectory;
			client_logging::Write(L"Info",
				L"Working directory for console: " + std::wstring(currentDirectory));
		}

		if (!ShellExecuteExW(&executeInfo))
		{
			const DWORD error = GetLastError();
			if (requestElevation && error == ERROR_CANCELLED)
			{
				ui::ReportError(localization::GetText(
					localization::TextId::ConsoleUacCancelled),
					L"UAC elevation for the console launch was cancelled.");
			}
			else
			{
				ui::ReportError(localization::Format(
					localization::TextId::ConsoleLaunchFailed,
					{ windows_error::FormatWindowsError(error) }),
					L"Launching the console failed. Windows error: " +
						std::to_wstring(error));
			}
			return false;
		}

		client_logging::Write(L"Ok", requestElevation
			? L"Elevated console launched successfully"
			: L"Console launched successfully without additional elevation");
		if (executeInfo.hProcess)
			CloseHandle(executeInfo.hProcess);
		return true;
	}

	bool CloseParentConsole()
	{
		const DWORD parentProcessId = process_context::GetParentProcessId(
			GetCurrentProcessId());
		if (parentProcessId == 0)
		{
			client_logging::Write(L"Warn", L"Could not determine parent process");
			return false;
		}

		client_logging::Write(L"Info", L"Closing parent console (PID: " +
			std::to_wstring(parentProcessId) + L")");
		HANDLE parentProcess = OpenProcess(PROCESS_TERMINATE, FALSE, parentProcessId);
		if (!parentProcess)
		{
			client_logging::Write(L"Warn", L"Could not open parent process: " +
				windows_error::FormatWindowsError(GetLastError()));
			return false;
		}

		const BOOL terminated = TerminateProcess(parentProcess, 0);
		CloseHandle(parentProcess);
		if (!terminated)
		{
			client_logging::Write(L"Warn",
				L"Could not terminate parent process: " +
				windows_error::FormatWindowsError(GetLastError()));
			return false;
		}

		client_logging::Write(L"Ok", L"Parent console closed successfully");
		return true;
	}
}