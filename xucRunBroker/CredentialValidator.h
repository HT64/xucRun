// Purpose: Declares server-side validation of a password for an expected account SID.
// Classifies successful logon, rejection, and technical errors separately.
#pragma once

#include "AccountIdentity.h"
#include "SensitiveString.h"

#include <Windows.h>

enum class CredentialValidationStatus
{
	Succeeded,
	Rejected,
	Failed
};

CredentialValidationStatus ValidateAccountPassword(
	const AccountIdentity& identity,
	const xucrun::SensitiveString& password,
	DWORD& error);

CredentialValidationStatus LogonAccountWithPassword(
	const AccountIdentity& identity,
	const xucrun::SensitiveString& password,
	HANDLE& token,
	DWORD& error);

bool IsCredentialRejectionError(DWORD error) noexcept;
bool RunCredentialValidatorSelfTest();