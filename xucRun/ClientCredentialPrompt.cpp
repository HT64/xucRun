// Purpose: Implements protected client-side input of Windows credentials.
// Behavior: Uses CredUI, clears all authentication buffers, and passes password data to SecureString.
#include "ClientCredentialPrompt.h"

#include <wincred.h>

#include "ClientLogging.h"
#include "ClientUiCommon.h"
#include "ClientWindowsError.h"
#include "DocumentationNameMapping.h"
#include "Localization.h"

namespace xucrun::credential_prompt
{
	bool PromptForCredentials(const std::wstring& prefilledUsername,
		std::wstring& outUsername, SecureString& outPassword, HWND owner,
		const wchar_t* messageText, const wchar_t* captionText)
	{
		client_logging::Write(
			L"Info", L"Opening CredUI dialog for secure password entry...");
		const std::wstring defaultMessage = localization::GetText(
			localization::TextId::CredentialPromptMessage);
		const std::wstring defaultCaption = localization::GetText(
			localization::TextId::CredentialPromptCaption);
		HWND foregroundWindow = GetForegroundWindow();
		if (foregroundWindow && IsWindow(foregroundWindow) &&
			IsWindowVisible(foregroundWindow))
		{
			owner = foregroundWindow;
			BringWindowToTop(owner);
			SetForegroundWindow(owner);
		}
		else if (!owner || !IsWindow(owner))
		{
			owner = ui::GetDialogOwnerWindow();
		}

		CREDUI_INFOW uiInfo = { sizeof(uiInfo) };
		uiInfo.pszMessageText = messageText ? messageText : defaultMessage.c_str();
		uiInfo.pszCaptionText = captionText ? captionText : defaultCaption.c_str();
		uiInfo.hwndParent = owner;

		ULONG authPackage = 0;
		LPVOID authBuffer = nullptr;
		ULONG authBufferSize = 0;
		LPVOID inputAuthBuffer = nullptr;
		ULONG inputAuthBufferSize = 0;
		BOOL save = FALSE;
		const std::wstring displayedPrefilledUsername =
			documentation_names::DisplayAccount(prefilledUsername);

		if (!displayedPrefilledUsername.empty())
		{
			wchar_t emptyPassword[] = L"";
			if (!CredPackAuthenticationBufferW(0,
				const_cast<LPWSTR>(displayedPrefilledUsername.c_str()), emptyPassword,
				nullptr, &inputAuthBufferSize) &&
				GetLastError() == ERROR_INSUFFICIENT_BUFFER)
			{
				inputAuthBuffer = CoTaskMemAlloc(inputAuthBufferSize);
				if (inputAuthBuffer && !CredPackAuthenticationBufferW(0,
					const_cast<LPWSTR>(displayedPrefilledUsername.c_str()), emptyPassword,
					static_cast<PBYTE>(inputAuthBuffer), &inputAuthBufferSize))
				{
					CoTaskMemFree(inputAuthBuffer);
					inputAuthBuffer = nullptr;
				}
			}
		}

		const DWORD result = CredUIPromptForWindowsCredentialsW(&uiInfo, 0,
			&authPackage, inputAuthBuffer, inputAuthBufferSize, &authBuffer,
			&authBufferSize, &save, CREDUIWIN_GENERIC);

		if (inputAuthBuffer)
		{
			SecureZeroMemory(inputAuthBuffer, inputAuthBufferSize);
			CoTaskMemFree(inputAuthBuffer);
		}

		if (result != ERROR_SUCCESS)
		{
			if (authBuffer)
			{
				SecureZeroMemory(authBuffer, authBufferSize);
				CoTaskMemFree(authBuffer);
			}
			if (result == ERROR_CANCELLED)
			{
				client_logging::Write(
					L"Info", L"Credential prompt cancelled by user");
			}
			else
			{
				client_logging::Write(L"Error",
					L"CredUI dialog failed. Error code: " +
					windows_error::FormatWindowsError(result));
			}
			return false;
		}

		wchar_t username[CREDUI_MAX_USERNAME_LENGTH + 1] = {};
		wchar_t domain[CREDUI_MAX_DOMAIN_TARGET_LENGTH + 1] = {};
		wchar_t password[CREDUI_MAX_PASSWORD_LENGTH + 1] = {};
		DWORD usernameLength = CREDUI_MAX_USERNAME_LENGTH;
		DWORD domainLength = CREDUI_MAX_DOMAIN_TARGET_LENGTH;
		DWORD passwordLength = CREDUI_MAX_PASSWORD_LENGTH;

		if (!CredUnPackAuthenticationBufferW(0, authBuffer, authBufferSize,
			username, &usernameLength, domain, &domainLength,
			password, &passwordLength))
		{
			const DWORD error = GetLastError();
			client_logging::Write(L"Error",
				L"Error extracting credentials from buffer. Error code: " +
				windows_error::FormatWindowsError(error));
			SecureZeroMemory(authBuffer, authBufferSize);
			CoTaskMemFree(authBuffer);
			return false;
		}

		SecureZeroMemory(authBuffer, authBufferSize);
		CoTaskMemFree(authBuffer);
		outUsername = wcslen(domain) > 0
			? std::wstring(domain) + L"\\" + username
			: std::wstring(username);
		outUsername = documentation_names::OperationalAccount(outUsername);
		outPassword.assign(password, wcslen(password));
		SecureZeroMemory(username, sizeof(username));
		SecureZeroMemory(domain, sizeof(domain));
		SecureZeroMemory(password, sizeof(password));

		client_logging::Write(
			L"Ok", L"Credentials captured successfully via CredUI dialog");
		client_logging::Write(L"Info", L"Entered username: " + outUsername);
		return true;
	}
}