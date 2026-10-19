// Purpose: Defines client-side resolution and checks for Windows account identities.
// Behavior: Exposes SID, account name, token, and administrator queries through one API.
#pragma once

#include <Windows.h>

#include <string>
#include <vector>

namespace xucrun::account_identity
{
	bool GetCurrentUserInfo(std::wstring& username, std::wstring& domain);
	bool GetCurrentUserSID(std::wstring& sidString);
	bool IsRunningAsAdministrator();
	bool IsCurrentUserAccount(const std::wstring& accountName);
	bool IsUserInAdminGroup();
	bool IsUserInAdminGroup(const std::wstring& username);
	bool ResolveAccountSid(const std::wstring& accountName,
		std::vector<BYTE>& sidBuffer);
	bool GetCanonicalAccountName(PSID sid, std::wstring& accountName);
	bool GetSidString(PSID sid, std::wstring& sidString);
	bool ResolveAccountIdentity(const std::wstring& accountName,
		std::wstring& canonicalName, std::wstring& sidString);
}