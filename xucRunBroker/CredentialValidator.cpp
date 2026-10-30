// Purpose: Validates credentials through Windows logon and matches the token account against the SID.
// Approach: Exercises the behavior with focused assertions against the shared implementation and Windows contracts.
// Prevents alias resolution or unexpected accounts from being accepted as success.
#include "CredentialValidator.h"

#include <sddl.h>

#include <vector>

// Classifies only typical logon errors as credential rejection.
bool IsCredentialRejectionError(DWORD error) noexcept
{
	switch (error)
	{
	case ERROR_LOGON_FAILURE:
	case ERROR_ACCOUNT_RESTRICTION:
	case ERROR_INVALID_LOGON_HOURS:
	case ERROR_INVALID_WORKSTATION:
	case ERROR_PASSWORD_EXPIRED:
	case ERROR_ACCOUNT_DISABLED:
	case ERROR_ACCOUNT_EXPIRED:
	case ERROR_PASSWORD_MUST_CHANGE:
	case ERROR_ACCOUNT_LOCKED_OUT:
		return true;
	default:
		return false;
	}
}

// Logs on an account with the password and returns only a token matching the canonical account SID.
CredentialValidationStatus LogonAccountWithPassword(
	const AccountIdentity& identity,
	const xucrun::SensitiveString& password,
	HANDLE& token,
	DWORD& error)
{
	token = nullptr;
	error = NO_ERROR;
	if (identity.sid.empty() || identity.userName.empty() || !password.isAssigned())
	{
		error = ERROR_INVALID_PARAMETER;
		return CredentialValidationStatus::Failed;
	}

	if (!LogonUserW(identity.userName.c_str(),
		identity.domain.empty() ? nullptr : identity.domain.c_str(),
		password.c_str(), LOGON32_LOGON_INTERACTIVE, LOGON32_PROVIDER_DEFAULT, &token))
	{
		error = GetLastError();
		return IsCredentialRejectionError(error)
			? CredentialValidationStatus::Rejected
			: CredentialValidationStatus::Failed;
	}

	DWORD tokenUserSize = 0;
	GetTokenInformation(token, TokenUser, nullptr, 0, &tokenUserSize);
	std::vector<std::uint8_t> tokenUser(tokenUserSize);
	bool success = tokenUserSize != 0 && GetTokenInformation(token, TokenUser,
		tokenUser.data(), tokenUserSize, &tokenUserSize) == TRUE;
	if (!success)
		error = GetLastError();

	PSID expectedSid = nullptr;
	if (success && (!ConvertStringSidToSidW(identity.sid.c_str(), &expectedSid) ||
		!IsValidSid(expectedSid)))
	{
		error = GetLastError();
		if (error == NO_ERROR)
			error = ERROR_INVALID_SID;
		success = false;
	}
	if (success)
	{
		const auto* user = reinterpret_cast<const TOKEN_USER*>(tokenUser.data());
		success = EqualSid(user->User.Sid, expectedSid) == TRUE;
		if (!success)
			error = ERROR_ACCESS_DENIED;
	}
	if (expectedSid)
		LocalFree(expectedSid);
	if (!success)
	{
		CloseHandle(token);
		token = nullptr;
		return CredentialValidationStatus::Failed;
	}
	return CredentialValidationStatus::Succeeded;
}

// Validates a password through logon without returning the generated token to the caller.
CredentialValidationStatus ValidateAccountPassword(
	const AccountIdentity& identity,
	const xucrun::SensitiveString& password,
	DWORD& error)
{
	HANDLE token = nullptr;
	const CredentialValidationStatus status =
		LogonAccountWithPassword(identity, password, token, error);
	if (token)
		CloseHandle(token);
	return status;
}

// Tests the distinction between expected credential rejections and system errors.
bool RunCredentialValidatorSelfTest()
{
	if (!IsCredentialRejectionError(ERROR_LOGON_FAILURE) ||
		!IsCredentialRejectionError(ERROR_ACCOUNT_RESTRICTION) ||
		!IsCredentialRejectionError(ERROR_PASSWORD_EXPIRED) ||
		!IsCredentialRejectionError(ERROR_ACCOUNT_LOCKED_OUT) ||
		IsCredentialRejectionError(ERROR_NOT_ENOUGH_MEMORY))
	{
		return false;
	}
	AccountIdentity identity;
	xucrun::SensitiveString password;
	DWORD error = NO_ERROR;
	return ValidateAccountPassword(identity, password, error) ==
		CredentialValidationStatus::Failed && error == ERROR_INVALID_PARAMETER;
}