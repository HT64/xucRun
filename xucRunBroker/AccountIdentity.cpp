// Purpose: Resolves Windows account SIDs and validates the returned account type.
// Provides stable SID, domain, and name values for broker operations.
#include "AccountIdentity.h"

#include <lm.h>
#include <sddl.h>

#include <vector>

// Resolves a requested user SID canonically and accepts user accounts only.
bool ResolveUserAccountIdentity(
	const std::wstring& requestedSid,
	AccountIdentity& identity,
	DWORD& windowsError)
{
	identity = {};
	windowsError = NO_ERROR;

	PSID sid = nullptr;
	if (!ConvertStringSidToSidW(requestedSid.c_str(), &sid) || !IsValidSid(sid))
	{
		windowsError = GetLastError();
		if (windowsError == NO_ERROR)
			windowsError = ERROR_INVALID_SID;
		if (sid)
			LocalFree(sid);
		return false;
	}

	DWORD nameLength = 0;
	DWORD domainLength = 0;
	SID_NAME_USE use = SidTypeUnknown;
	LookupAccountSidW(nullptr, sid, nullptr, &nameLength, nullptr, &domainLength, &use);
	if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || nameLength == 0)
	{
		windowsError = GetLastError();
		LocalFree(sid);
		return false;
	}

	std::vector<wchar_t> name(nameLength);
	std::vector<wchar_t> domain(domainLength);
	if (!LookupAccountSidW(nullptr, sid, name.data(), &nameLength,
		domain.empty() ? nullptr : domain.data(), &domainLength, &use) ||
		use != SidTypeUser)
	{
		windowsError = GetLastError();
		if (windowsError == NO_ERROR)
			windowsError = ERROR_NONE_MAPPED;
		LocalFree(sid);
		return false;
	}

	wchar_t* canonicalSid = nullptr;
	if (!ConvertSidToStringSidW(sid, &canonicalSid))
	{
		windowsError = GetLastError();
		LocalFree(sid);
		return false;
	}
	identity.sid = canonicalSid;
	identity.userName = name.data();
	identity.domain = domainLength > 1 ? domain.data() : L"";
	identity.canonicalName = identity.domain.empty()
		? identity.userName
		: identity.domain + L"\\" + identity.userName;
	LocalFree(canonicalSid);
	LocalFree(sid);
	return true;
}

// Resolves a Windows account name to the same canonical user identity used for SID input.
bool ResolveUserAccountNameIdentity(
	const std::wstring& requestedName,
	AccountIdentity& identity,
	DWORD& windowsError)
{
	identity = {};
	windowsError = NO_ERROR;
	if (requestedName.empty())
	{
		windowsError = ERROR_INVALID_ACCOUNT_NAME;
		return false;
	}

	DWORD sidLength = 0;
	DWORD domainLength = 0;
	SID_NAME_USE use = SidTypeUnknown;
	LookupAccountNameW(nullptr, requestedName.c_str(), nullptr,
		&sidLength, nullptr, &domainLength, &use);
	if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || sidLength == 0)
	{
		windowsError = GetLastError();
		if (windowsError == NO_ERROR)
			windowsError = ERROR_NONE_MAPPED;
		return false;
	}

	std::vector<BYTE> sid(sidLength);
	std::vector<wchar_t> domain(domainLength);
	if (!LookupAccountNameW(nullptr, requestedName.c_str(), sid.data(),
		&sidLength, domain.empty() ? nullptr : domain.data(), &domainLength, &use) ||
		!IsValidSid(sid.data()) || use != SidTypeUser)
	{
		windowsError = GetLastError();
		if (windowsError == NO_ERROR)
			windowsError = use == SidTypeUser ? ERROR_INVALID_SID : ERROR_NONE_MAPPED;
		return false;
	}

	wchar_t* sidText = nullptr;
	if (!ConvertSidToStringSidW(sid.data(), &sidText))
	{
		windowsError = GetLastError();
		return false;
	}
	const std::wstring canonicalSid(sidText);
	LocalFree(sidText);
	return ResolveUserAccountIdentity(canonicalSid, identity, windowsError);
}

bool RunAccountIdentitySelfTest()
{
	HANDLE token = nullptr;
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
		return false;
	DWORD tokenUserSize = 0;
	GetTokenInformation(token, TokenUser, nullptr, 0, &tokenUserSize);
	if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || tokenUserSize == 0)
	{
		CloseHandle(token);
		return false;
	}
	std::vector<BYTE> tokenUser(tokenUserSize);
	const bool tokenRead = GetTokenInformation(token, TokenUser,
		tokenUser.data(), tokenUserSize, &tokenUserSize) == TRUE;
	CloseHandle(token);
	if (!tokenRead)
		return false;
	const auto* user = reinterpret_cast<const TOKEN_USER*>(tokenUser.data());
	wchar_t* sidText = nullptr;
	if (!ConvertSidToStringSidW(user->User.Sid, &sidText))
		return false;
	const std::wstring sid(sidText);
	LocalFree(sidText);

	DWORD error = NO_ERROR;
	AccountIdentity fromSid;
	AccountIdentity fromName;
	return ResolveUserAccountIdentity(sid, fromSid, error) &&
		ResolveUserAccountNameIdentity(fromSid.canonicalName, fromName, error) &&
		_wcsicmp(fromSid.sid.c_str(), fromName.sid.c_str()) == 0 &&
		_wcsicmp(fromSid.canonicalName.c_str(), fromName.canonicalName.c_str()) == 0;
}

// Resolves direct and indirect membership in the local Administrators group.
bool IsAccountMemberOfAdministrators(
	const AccountIdentity& identity,
	bool& isAdministrator,
	DWORD& windowsError)
{
	isAdministrator = false;
	windowsError = NO_ERROR;
	if (identity.canonicalName.empty())
	{
		windowsError = ERROR_INVALID_PARAMETER;
		return false;
	}

	LPLOCALGROUP_USERS_INFO_0 groups = nullptr;
	DWORD entriesRead = 0;
	DWORD totalEntries = 0;
	const NET_API_STATUS status = NetUserGetLocalGroups(nullptr,
		identity.canonicalName.c_str(), 0, LG_INCLUDE_INDIRECT,
		reinterpret_cast<LPBYTE*>(&groups), MAX_PREFERRED_LENGTH,
		&entriesRead, &totalEntries);
	if (status != NERR_Success)
	{
		windowsError = status;
		return false;
	}

	BYTE administratorsSid[SECURITY_MAX_SID_SIZE] = {};
	DWORD administratorsSidSize = sizeof(administratorsSid);
	if (!CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr,
		administratorsSid, &administratorsSidSize))
	{
		windowsError = GetLastError();
		if (groups)
			NetApiBufferFree(groups);
		return false;
	}

	for (DWORD index = 0; index < entriesRead; index++)
	{
		DWORD groupSidSize = 0;
		DWORD domainSize = 0;
		SID_NAME_USE use = SidTypeUnknown;
		LookupAccountNameW(nullptr, groups[index].lgrui0_name,
			nullptr, &groupSidSize, nullptr, &domainSize, &use);
		if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || groupSidSize == 0)
			continue;
		std::vector<BYTE> groupSid(groupSidSize);
		std::vector<wchar_t> domain(domainSize);
		if (LookupAccountNameW(nullptr, groups[index].lgrui0_name,
			groupSid.data(), &groupSidSize,
			domain.empty() ? nullptr : domain.data(), &domainSize, &use) &&
			EqualSid(groupSid.data(), administratorsSid))
		{
			isAdministrator = true;
			break;
		}
	}
	if (groups)
		NetApiBufferFree(groups);
	return true;
}