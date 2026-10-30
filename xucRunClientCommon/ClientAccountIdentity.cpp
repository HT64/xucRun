// Purpose: Implements account, SID, and administrator checks for client workflows.
// Approach: Implements the behavior with focused helpers built on shared xucRun interfaces and Windows APIs.
// Behavior: Uses process tokens, account lookups, and local group membership APIs.
#include "ClientAccountIdentity.h"

#include <Lm.h>
#include <Lmcons.h>
#include <sddl.h>

#include "ClientLogging.h"

namespace xucrun::account_identity
{
	bool GetCurrentUserInfo(std::wstring& username, std::wstring& domain)
	{
		wchar_t usernameBuffer[UNLEN + 1] = {};
		wchar_t domainBuffer[UNLEN + 1] = {};
		DWORD usernameLength = UNLEN + 1;
		DWORD domainLength = UNLEN + 1;
		if (!GetUserNameW(usernameBuffer, &usernameLength))
			return false;

		username = usernameBuffer;
		HANDLE token = nullptr;
		if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
		{
			DWORD tokenSize = 0;
			GetTokenInformation(token, TokenUser, nullptr, 0, &tokenSize);
			if (tokenSize > 0)
			{
				std::vector<BYTE> buffer(tokenSize);
				if (GetTokenInformation(
					token, TokenUser, buffer.data(), tokenSize, &tokenSize))
				{
					const PTOKEN_USER tokenUser =
						reinterpret_cast<PTOKEN_USER>(buffer.data());
					DWORD resolvedDomainLength = domainLength;
					DWORD resolvedNameLength = usernameLength;
					SID_NAME_USE sidType;
					if (LookupAccountSidW(nullptr, tokenUser->User.Sid,
						usernameBuffer, &resolvedNameLength, domainBuffer,
						&resolvedDomainLength, &sidType))
					{
						domain = domainBuffer;
					}
				}
			}
			CloseHandle(token);
		}

		if (domain.empty())
		{
			wchar_t computerName[MAX_COMPUTERNAME_LENGTH + 1] = {};
			DWORD computerNameLength = MAX_COMPUTERNAME_LENGTH + 1;
			if (GetComputerNameW(computerName, &computerNameLength))
				domain = computerName;
		}
		return true;
	}

	bool GetCurrentUserSID(std::wstring& sidString)
	{
		HANDLE token = nullptr;
		if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
			return false;

		DWORD tokenInfoLength = 0;
		GetTokenInformation(token, TokenUser, nullptr, 0, &tokenInfoLength);
		std::vector<BYTE> tokenInfoBuffer(tokenInfoLength);
		TOKEN_USER* tokenUser =
			reinterpret_cast<TOKEN_USER*>(tokenInfoBuffer.data());
		bool success = false;
		if (GetTokenInformation(
			token, TokenUser, tokenUser, tokenInfoLength, &tokenInfoLength))
		{
			LPWSTR sid = nullptr;
			if (ConvertSidToStringSidW(tokenUser->User.Sid, &sid))
			{
				sidString = sid;
				LocalFree(sid);
				success = true;
			}
		}
		CloseHandle(token);
		return success;
	}

	bool IsRunningAsAdministrator()
	{
		HANDLE token = nullptr;
		if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
		{
			client_logging::Write(L"Warn",
				L"Failed to open process token for elevation check");
			return true;
		}

		TOKEN_ELEVATION elevation = {};
		DWORD returnLength = 0;
		const bool querySucceeded = GetTokenInformation(token, TokenElevation,
			&elevation, sizeof(elevation), &returnLength) != FALSE;
		CloseHandle(token);
		if (!querySucceeded)
		{
			client_logging::Write(L"Warn",
				L"Failed to query elevation state - treating token as elevated for safety");
			return true;
		}
		return elevation.TokenIsElevated != 0;
	}

	bool ResolveAccountSid(const std::wstring& accountName,
		std::vector<BYTE>& sidBuffer)
	{
		DWORD sidSize = 0;
		DWORD domainSize = 0;
		SID_NAME_USE sidType;
		LookupAccountNameW(nullptr, accountName.c_str(), nullptr,
			&sidSize, nullptr, &domainSize, &sidType);
		if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || sidSize == 0)
			return false;

		sidBuffer.resize(sidSize);
		std::vector<wchar_t> domain(domainSize);
		return LookupAccountNameW(nullptr, accountName.c_str(), sidBuffer.data(),
			&sidSize, domain.empty() ? nullptr : domain.data(), &domainSize,
			&sidType) != FALSE;
	}

	bool GetCanonicalAccountName(PSID sid, std::wstring& accountName)
	{
		DWORD nameSize = 0;
		DWORD domainSize = 0;
		SID_NAME_USE sidType;
		LookupAccountSidW(nullptr, sid, nullptr, &nameSize, nullptr,
			&domainSize, &sidType);
		if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || nameSize == 0)
			return false;

		std::vector<wchar_t> name(nameSize);
		std::vector<wchar_t> domain(domainSize);
		if (!LookupAccountSidW(nullptr, sid, name.data(), &nameSize,
			domain.empty() ? nullptr : domain.data(), &domainSize, &sidType))
		{
			return false;
		}
		accountName = domainSize > 1
			? std::wstring(domain.data()) + L"\\" + name.data()
			: std::wstring(name.data());
		return true;
	}

	bool GetSidString(PSID sid, std::wstring& sidString)
	{
		LPWSTR value = nullptr;
		if (!ConvertSidToStringSidW(sid, &value))
			return false;
		sidString = value;
		LocalFree(value);
		return true;
	}

	bool ResolveAccountIdentity(const std::wstring& accountName,
		std::wstring& canonicalName, std::wstring& sidString)
	{
		std::vector<BYTE> sid;
		if (!ResolveAccountSid(accountName, sid))
			return false;
		return GetCanonicalAccountName(sid.data(), canonicalName) &&
			GetSidString(sid.data(), sidString);
	}

	bool IsCurrentUserAccount(const std::wstring& accountName)
	{
		HANDLE token = nullptr;
		if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
		{
			client_logging::Write(L"Warn",
				L"Failed to open process token for user comparison");
			return false;
		}

		DWORD tokenUserSize = 0;
		GetTokenInformation(token, TokenUser, nullptr, 0, &tokenUserSize);
		if (tokenUserSize == 0)
		{
			CloseHandle(token);
			return false;
		}
		std::vector<BYTE> tokenUserBuffer(tokenUserSize);
		if (!GetTokenInformation(token, TokenUser, tokenUserBuffer.data(),
			tokenUserSize, &tokenUserSize))
		{
			CloseHandle(token);
			return false;
		}
		CloseHandle(token);
		const PTOKEN_USER tokenUser =
			reinterpret_cast<PTOKEN_USER>(tokenUserBuffer.data());

		if (accountName.find(L'\\') == std::wstring::npos &&
			accountName.find(L'@') == std::wstring::npos)
		{
			DWORD currentNameSize = 0;
			DWORD currentDomainSize = 0;
			SID_NAME_USE currentSidType;
			LookupAccountSidW(nullptr, tokenUser->User.Sid, nullptr,
				&currentNameSize, nullptr, &currentDomainSize, &currentSidType);
			if (GetLastError() == ERROR_INSUFFICIENT_BUFFER)
			{
				std::vector<wchar_t> currentName(currentNameSize);
				std::vector<wchar_t> currentDomain(currentDomainSize);
				if (LookupAccountSidW(nullptr, tokenUser->User.Sid,
					currentName.data(), &currentNameSize, currentDomain.data(),
					&currentDomainSize, &currentSidType) &&
					_wcsicmp(accountName.c_str(), currentName.data()) == 0)
				{
					return true;
				}
			}
		}

		DWORD accountSidSize = 0;
		DWORD accountDomainSize = 0;
		SID_NAME_USE accountSidType;
		LookupAccountNameW(nullptr, accountName.c_str(), nullptr,
			&accountSidSize, nullptr, &accountDomainSize, &accountSidType);
		if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || accountSidSize == 0)
		{
			client_logging::Write(L"Info",
				L"Failed to resolve account name to SID: " + accountName);
			return false;
		}

		std::vector<BYTE> accountSidBuffer(accountSidSize);
		std::vector<wchar_t> accountDomain(accountDomainSize);
		if (!LookupAccountNameW(nullptr, accountName.c_str(),
			accountSidBuffer.data(), &accountSidSize, accountDomain.data(),
			&accountDomainSize, &accountSidType))
		{
			client_logging::Write(L"Info",
				L"SID resolution failed for account name: " + accountName);
			return false;
		}
		return EqualSid(tokenUser->User.Sid, accountSidBuffer.data()) == TRUE;
	}

	bool IsUserInAdminGroup()
	{
		BOOL isAdmin = FALSE;
		PSID adminGroup = nullptr;
		SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
		if (AllocateAndInitializeSid(&ntAuthority, 2,
			SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS,
			0, 0, 0, 0, 0, 0, &adminGroup))
		{
			HANDLE token = nullptr;
			if (OpenProcessToken(
				GetCurrentProcess(), TOKEN_QUERY | TOKEN_DUPLICATE, &token))
			{
				CheckTokenMembership(nullptr, adminGroup, &isAdmin);
				if (!isAdmin)
				{
					TOKEN_ELEVATION_TYPE elevationType;
					DWORD returnLength;
					if (GetTokenInformation(token, TokenElevationType,
						&elevationType, sizeof(elevationType), &returnLength) &&
						elevationType == TokenElevationTypeLimited)
					{
						TOKEN_LINKED_TOKEN linkedToken;
						if (GetTokenInformation(token, TokenLinkedToken,
							&linkedToken, sizeof(linkedToken), &returnLength))
						{
							CheckTokenMembership(
								linkedToken.LinkedToken, adminGroup, &isAdmin);
							CloseHandle(linkedToken.LinkedToken);
						}
					}
				}
				CloseHandle(token);
			}
			FreeSid(adminGroup);
		}
		return isAdmin == TRUE;
	}

	bool IsUserInAdminGroup(const std::wstring& username)
	{
		std::vector<BYTE> userSid;
		if (!ResolveAccountSid(username, userSid))
		{
			client_logging::Write(L"Warning",
				L"SID resolution failed for '" + username + L"'");
			return false;
		}

		std::wstring canonicalUsername;
		if (!GetCanonicalAccountName(userSid.data(), canonicalUsername))
		{
			client_logging::Write(L"Warning",
				L"Failed to determine canonical account name for '" +
				username + L"'");
			return false;
		}

		LPLOCALGROUP_USERS_INFO_0 groups = nullptr;
		DWORD entriesRead = 0;
		DWORD totalEntries = 0;
		const NET_API_STATUS status = NetUserGetLocalGroups(nullptr,
			canonicalUsername.c_str(), 0, LG_INCLUDE_INDIRECT,
			reinterpret_cast<LPBYTE*>(&groups), MAX_PREFERRED_LENGTH,
			&entriesRead, &totalEntries);
		if (status != NERR_Success)
		{
			client_logging::Write(L"Warning",
				L"NetUserGetLocalGroups failed for '" + canonicalUsername +
				L"' (Error: " + std::to_wstring(status) + L")");
			return false;
		}

		BYTE adminSidBuffer[SECURITY_MAX_SID_SIZE] = {};
		DWORD adminSidSize = sizeof(adminSidBuffer);
		if (!CreateWellKnownSid(
			WinBuiltinAdministratorsSid, nullptr, adminSidBuffer, &adminSidSize))
		{
			if (groups)
				NetApiBufferFree(groups);
			return false;
		}

		bool isAdmin = false;
		if (groups)
		{
			for (DWORD index = 0; index < entriesRead; index++)
			{
				std::vector<BYTE> groupSid;
				if (ResolveAccountSid(groups[index].lgrui0_name, groupSid) &&
					EqualSid(groupSid.data(), adminSidBuffer))
				{
					isAdmin = true;
					client_logging::Write(L"Info", L"User '" +
						canonicalUsername +
						L"' is a member of the built-in Administrators group");
					break;
				}
			}
			NetApiBufferFree(groups);
		}
		return isAdmin;
	}
}