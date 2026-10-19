// Purpose: Defines creation and deletion of stored broker credentials.
// Behavior: Combines account selection, secure input, and broker management calls.
#pragma once

#include <Windows.h>

#include <string>

namespace xucrun::broker_credential_management
{
	bool PromptAndSaveBrokerCredentials(
		const std::wstring& prefilledUsername, HWND owner = nullptr,
		bool requireAdministrator = false);
	int DeleteBrokerCredential(
		const std::wstring& accountName, HWND owner = nullptr);
	int DeleteStoredCredential(const std::wstring& specificUser = L"");
	int ShowBrokerCredentials(const std::wstring& programExecutableName);
}