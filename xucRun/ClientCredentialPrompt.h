// Purpose: Defines protected client-side input of Windows credentials.
// Approach: Exposes the declarations, constants, and types consumed by the components that provide this behavior.
// Behavior: Opens CredUI and returns password data only through SecureString.
#pragma once

#include <Windows.h>

#include <string>

#include "ClientSensitiveString.h"

namespace xucrun::credential_prompt
{
	bool PromptForCredentials(const std::wstring& prefilledUsername,
		std::wstring& outUsername, SecureString& outPassword,
		HWND owner = nullptr, const wchar_t* messageText = nullptr,
		const wchar_t* captionText = nullptr);
}