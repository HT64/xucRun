// Purpose: Parses hidden-launch modifiers and removes them from the target command line.
#include "ClientHiddenLaunchModifiers.h"

#include <algorithm>
#include <cwchar>
#include <cwctype>
#include <string>

#include "ClientLogging.h"
#include "ClientUiCommon.h"
#include "Localization.h"

namespace xucrun::hidden_launch
{
	bool ProcessHiddenLaunchModifier(
		int& argc, wchar_t* argv[], bool& hiddenLaunch, bool& monitorHiddenLaunch)
	{
		hiddenLaunch = false;
		monitorHiddenLaunch = false;

		auto isUserLaunchModifier = [](const std::wstring& value)
		{
			std::wstring lowerValue = value;
			std::transform(lowerValue.begin(), lowerValue.end(), lowerValue.begin(), ::towlower);
			return lowerValue == L"/user" || lowerValue == L"/user-elevated" ||
				lowerValue.starts_with(L"/user:") ||
				lowerValue.starts_with(L"/user-elevated:");
		};
		int hiddenIndex = 0;
		auto getHiddenModifier = [](const std::wstring& value)
		{
			if (_wcsicmp(value.c_str(), L"/hidden-monitor") == 0 ||
				_wcsicmp(value.c_str(), L"--hidden-monitor") == 0)
				return 2;
			if (_wcsicmp(value.c_str(), L"/hidden") == 0 ||
				_wcsicmp(value.c_str(), L"--hidden") == 0)
				return 1;
			return 0;
		};
		int hiddenModifier = argc >= 2 ? getHiddenModifier(argv[1]) : 0;
		if (hiddenModifier != 0)
		{
			hiddenIndex = 1;
		}
		else if (argc >= 3 && isUserLaunchModifier(argv[1]) &&
			(hiddenModifier = getHiddenModifier(argv[2])) != 0)
		{
			hiddenIndex = 2;
		}
		if (hiddenIndex == 0)
			return true;

		hiddenLaunch = true;
		monitorHiddenLaunch = hiddenModifier == 2;
		for (int index = hiddenIndex; index < argc - 1; index++)
			argv[index] = argv[index + 1];
		argc--;

		const int targetIndex = argc >= 2 && isUserLaunchModifier(argv[1]) ? 2 : 1;
		const bool missingTarget = targetIndex >= argc;
		const bool targetIsXucRunSwitch = !missingTarget &&
			(argv[targetIndex][0] == L'/' ||
			 (argv[targetIndex][0] == L'-' && argv[targetIndex][1] != L'\0' &&
			  argv[targetIndex][1] == L'-'));
		if (missingTarget || targetIsXucRunSwitch)
		{
			ui::ReportError(
				localization::GetText(localization::TextId::HiddenTargetRequired),
				L"A hidden launch modifier was specified without a valid target.");
			return false;
		}
		client_logging::Write(L"Info", monitorHiddenLaunch
			? L"Monitored hidden target launch requested"
			: L"Hidden target launch requested");
		return true;
	}
}