// Purpose: Detects PowerShell launches that explicitly request hidden windows.
// Approach: Implements the behavior with focused helpers built on shared xucRun interfaces and Windows APIs.
// Behavior: Parses the Windows command line and checks for -WindowStyle hidden case-insensitively.
#include "ClientLaunchDetection.h"

#include <Windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cwctype>

namespace
{
	std::wstring GetFileName(const std::wstring& path)
	{
		const std::size_t separator = path.find_last_of(L"\\/");
		return separator == std::wstring::npos ? path : path.substr(separator + 1);
	}
}

namespace xucrun::launch_detection
{
	bool IsExplicitlyHiddenPowerShellLaunch(
		const std::wstring& executablePath, const std::wstring& argumentTail)
	{
		std::wstring executableName = GetFileName(executablePath);
		std::transform(executableName.begin(), executableName.end(),
			executableName.begin(), ::towlower);
		if (executableName != L"powershell.exe" && executableName != L"pwsh.exe")
			return false;

		const std::wstring commandLine = L"\"" + executablePath + L"\" " + argumentTail;
		int argumentCount = 0;
		wchar_t** arguments = CommandLineToArgvW(commandLine.c_str(), &argumentCount);
		if (!arguments)
			return false;

		bool hidden = false;
		for (int index = 1; index + 1 < argumentCount; ++index)
		{
			std::wstring option = arguments[index];
			std::wstring value = arguments[index + 1];
			std::transform(option.begin(), option.end(), option.begin(), ::towlower);
			std::transform(value.begin(), value.end(), value.begin(), ::towlower);
			if (option == L"-windowstyle" && value == L"hidden")
			{
				hidden = true;
				break;
			}
		}
		LocalFree(arguments);
		return hidden;
	}
}