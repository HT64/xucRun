// Purpose: Reliably routes client text to an available Windows output target.
// Approach: Implements the behavior with focused helpers built on shared xucRun interfaces and Windows APIs.
// Behavior: Detects console and redirection, writes with matching encoding, otherwise falls back to UI.
#include "ClientConsoleOutput.h"

#include <Windows.h>

#include <algorithm>
#include <climits>
#include <vector>

#include "Localization.h"

namespace
{
	std::wstring BuildTextOutput(const std::wstring& heading,
		const std::wstring& content)
	{
		std::wstring text = heading + L"\r\n\r\n";
		text.reserve(text.size() + content.size() + 2);
		for (size_t index = 0; index < content.size(); index++)
		{
			if (content[index] == L'\n' &&
				(index == 0 || content[index - 1] != L'\r'))
			{
				text += L'\r';
			}
			text += content[index];
		}
		while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n'))
			text.pop_back();
		text += L"\r\n";
		return text;
	}

	bool WriteUtf8Text(HANDLE output, const std::wstring& text)
	{
		if (text.size() > INT_MAX)
			return false;
		const int byteCount = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
			text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
		if (byteCount <= 0)
			return text.empty();
		std::vector<char> bytes(static_cast<size_t>(byteCount));
		if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
			static_cast<int>(text.size()), bytes.data(), byteCount, nullptr, nullptr) !=
			byteCount)
		{
			return false;
		}

		size_t position = 0;
		while (position < bytes.size())
		{
			const DWORD requested = static_cast<DWORD>(
				std::min<size_t>(bytes.size() - position, MAXDWORD));
			DWORD written = 0;
			if (!WriteFile(output, bytes.data() + position, requested, &written, nullptr) ||
				written == 0)
			{
				return false;
			}
			position += written;
		}
		return true;
	}

	bool WriteCompleteConsoleText(HANDLE output, const wchar_t* text, size_t length)
	{
		size_t position = 0;
		while (position < length)
		{
			const DWORD requested = static_cast<DWORD>(
				std::min<size_t>(length - position, MAXDWORD));
			DWORD written = 0;
			if (!WriteConsoleW(output, text + position, requested, &written, nullptr) ||
				written == 0)
			{
				return false;
			}
			position += written;
		}
		return true;
	}
}

namespace xucrun::console_output
{
	bool HasAvailableTarget()
	{
		HANDLE standardOutput = GetStdHandle(STD_OUTPUT_HANDLE);
		if (standardOutput && standardOutput != INVALID_HANDLE_VALUE)
		{
			DWORD mode = 0;
			if (GetConsoleMode(standardOutput, &mode))
				return true;
			SetLastError(NO_ERROR);
			if (GetFileType(standardOutput) != FILE_TYPE_UNKNOWN ||
				GetLastError() == NO_ERROR)
			{
				return true;
			}
		}

		if (!AttachConsole(ATTACH_PARENT_PROCESS))
			return false;
		FreeConsole();
		return true;
	}

	TextOutputResult WriteToAvailableTarget(const std::wstring& heading,
		const std::wstring& content)
	{
		HANDLE standardOutput = GetStdHandle(STD_OUTPUT_HANDLE);
		DWORD standardOutputMode = 0;
		const bool inheritedConsoleOutput = standardOutput &&
			standardOutput != INVALID_HANDLE_VALUE &&
			GetConsoleMode(standardOutput, &standardOutputMode);
		if (standardOutput && standardOutput != INVALID_HANDLE_VALUE &&
			!inheritedConsoleOutput)
		{
			const DWORD fileType = GetFileType(standardOutput);
			if (fileType != FILE_TYPE_UNKNOWN || GetLastError() == NO_ERROR)
			{
				const std::wstring redirectedText = BuildTextOutput(heading, content);
				return WriteUtf8Text(standardOutput, redirectedText)
					? TextOutputResult::Complete
					: TextOutputResult::WriteFailed;
			}
		}

		bool attachedParentConsole = false;
		bool closeOutput = false;
		HANDLE output = standardOutput;
		if (!inheritedConsoleOutput)
		{
			if (!AttachConsole(ATTACH_PARENT_PROCESS))
				return TextOutputResult::NoOutputTarget;
			attachedParentConsole = true;
			output = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE,
				FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
			if (output == INVALID_HANDLE_VALUE)
			{
				FreeConsole();
				return TextOutputResult::NoOutputTarget;
			}
			closeOutput = true;
		}

		std::wstring text = BuildTextOutput(heading, content);
		text += L"\r\n";
		text += localization::GetText(localization::TextId::ConsoleOutputComplete);
		text += L"\r\n";
		const bool complete = WriteCompleteConsoleText(output, text.data(), text.size());
		if (closeOutput)
			CloseHandle(output);
		if (attachedParentConsole)
			FreeConsole();
		return complete ? TextOutputResult::Complete : TextOutputResult::WriteFailed;
	}

	bool WriteErrorToAvailableTarget(const std::wstring& message)
	{
		return WriteToAvailableTarget(
			localization::GetText(localization::TextId::ErrorTitle),
			message) ==
			TextOutputResult::Complete;
	}
}