// Purpose: Implements central formatting of client Win32 and pipe errors.
// Behavior: Uses FormatMessage, cleans system text, and appends localized fallback explanations.
#include "ClientWindowsError.h"

#include "Localization.h"

namespace xucrun::windows_error
{
	std::wstring FormatWindowsError(DWORD error)
	{
		const std::wstring errorCode = std::to_wstring(error);
		if (error == ERROR_SERVICE_DISABLED)
		{
			return localization::Format(
				localization::TextId::BrokerServiceDisabledError, { errorCode });
		}
		if (error == ERROR_INVALID_SECURITY_DESCR)
		{
			return localization::Format(
				localization::TextId::StorageSecurityBlocked, { errorCode });
		}
		wchar_t* messageBuffer = nullptr;
		const DWORD length = FormatMessageW(
			FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
				FORMAT_MESSAGE_IGNORE_INSERTS,
			nullptr, error, 0, reinterpret_cast<wchar_t*>(&messageBuffer), 0, nullptr);
		std::wstring result;
		if (length == 0 || !messageBuffer)
		{
			result = localization::Format(
				localization::TextId::WindowsErrorTextUnavailable,
				{ errorCode });
		}
		else
		{
			std::wstring message(messageBuffer, length);
			LocalFree(messageBuffer);
			while (!message.empty() && (message.back() == L'\r' ||
				message.back() == L'\n' || message.back() == L' ' ||
				message.back() == L'.'))
			{
				message.pop_back();
			}
			result = message.empty()
				? localization::Format(
					localization::TextId::WindowsErrorTextEmpty,
					{ errorCode })
				: errorCode + L" (" + message + L")";
		}

		if (error == ERROR_BROKEN_PIPE || error == ERROR_NO_DATA ||
			error == ERROR_PIPE_NOT_CONNECTED)
		{
			result += localization::GetText(
				localization::TextId::BrokerCommunicationExplanation);
		}
		return result;
	}
}