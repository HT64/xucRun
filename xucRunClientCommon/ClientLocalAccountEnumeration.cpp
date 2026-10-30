// Purpose: Implements enumeration of enabled local Windows accounts.
// Approach: Implements the behavior with focused helpers built on shared xucRun interfaces and Windows APIs.
// Behavior: Reads local standard users via NetAPI and returns sorted computer account names.
#include "ClientLocalAccountEnumeration.h"

#include <Windows.h>
#include <Lm.h>

#include <algorithm>

#include "ClientLogging.h"

namespace xucrun::local_account_enumeration
{
	std::vector<std::wstring> GetEnabledLocalAccounts()
	{
		std::vector<std::wstring> accounts;
		wchar_t computerName[MAX_COMPUTERNAME_LENGTH + 1] = {};
		DWORD computerNameLength = static_cast<DWORD>(std::size(computerName));
		if (!GetComputerNameW(computerName, &computerNameLength))
			return accounts;

		DWORD resumeHandle = 0;
		NET_API_STATUS status = NERR_Success;
		do
		{
			LPUSER_INFO_1 entries = nullptr;
			DWORD entriesRead = 0;
			DWORD totalEntries = 0;
			status = NetUserEnum(nullptr, 1, FILTER_NORMAL_ACCOUNT,
				reinterpret_cast<LPBYTE*>(&entries), MAX_PREFERRED_LENGTH,
				&entriesRead, &totalEntries, &resumeHandle);
			if (status != NERR_Success && status != ERROR_MORE_DATA)
			{
				if (entries)
					NetApiBufferFree(entries);
				client_logging::Write(L"Warning",
					L"Failed to enumerate local user accounts. Error code: " +
					std::to_wstring(status));
				accounts.clear();
				return accounts;
			}
			for (DWORD index = 0; index < entriesRead; index++)
			{
				if (entries[index].usri1_name && entries[index].usri1_name[0] != L'\0' &&
					(entries[index].usri1_flags & UF_ACCOUNTDISABLE) == 0)
				{
					accounts.emplace_back(std::wstring(computerName) + L"\\" +
						entries[index].usri1_name);
				}
			}
			if (entries)
				NetApiBufferFree(entries);
		} while (status == ERROR_MORE_DATA);

		std::sort(accounts.begin(), accounts.end(), [](const std::wstring& left,
			const std::wstring& right) { return _wcsicmp(left.c_str(), right.c_str()) < 0; });
		accounts.erase(std::unique(accounts.begin(), accounts.end(),
			[](const std::wstring& left, const std::wstring& right) {
				return _wcsicmp(left.c_str(), right.c_str()) == 0;
			}), accounts.end());
		return accounts;
	}
}