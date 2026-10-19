// Purpose: Defines the canonical identity of a Windows account.
// Declares the validated resolution of SIDs to domain and account names.
#pragma once

#include <Windows.h>

#include <string>

struct AccountIdentity
{
	std::wstring sid;
	std::wstring userName;
	std::wstring domain;
	std::wstring canonicalName;
};

bool ResolveUserAccountIdentity(
	const std::wstring& requestedSid,
	AccountIdentity& identity,
	DWORD& windowsError);

bool ResolveUserAccountNameIdentity(
	const std::wstring& requestedName,
	AccountIdentity& identity,
	DWORD& windowsError);

bool IsAccountMemberOfAdministrators(
	const AccountIdentity& identity,
	bool& isAdministrator,
	DWORD& windowsError);

bool RunAccountIdentitySelfTest();