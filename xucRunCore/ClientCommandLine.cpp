// Purpose: Implements canonical processing of client arguments and /user options.
// Approach: Implements the behavior with focused helpers built on shared xucRun interfaces and Windows APIs.
// Operation: Parses argv structurally and quotes arguments according to Windows process startup rules.
#include "ClientCommandLine.h"

#include <algorithm>
#include <cwctype>
#include <sstream>
#include <utility>

namespace
{
	bool StartsWithUserSwitch(const std::wstring& argument)
	{
		constexpr wchar_t prefix[] = L"/user:";
		if (argument.size() < std::size(prefix) - 1)
			return false;
		for (std::size_t index = 0; index < std::size(prefix) - 1; index++)
		{
			if (std::towlower(argument[index]) != prefix[index])
				return false;
		}
		return true;
	}

	std::wstring ToLower(const std::wstring& value)
	{
		std::wstring result = value;
		std::transform(result.begin(), result.end(), result.begin(), std::towlower);
		return result;
	}

	std::wstring TrimUsername(const std::wstring& value)
	{
		const std::size_t first = value.find_first_not_of(L" \t");
		if (first == std::wstring::npos)
			return L"";
		const std::size_t last = value.find_last_not_of(L" \t");
		return value.substr(first, last - first + 1);
	}

	std::size_t FindCommandSwitch(const std::wstring& arguments)
	{
		for (std::size_t position = 0; position + 1 < arguments.size(); position++)
		{
			if (arguments[position] == L'/' &&
				(std::towlower(arguments[position + 1]) == L'c' ||
				 std::towlower(arguments[position + 1]) == L'k') &&
				(position == 0 || std::iswspace(arguments[position - 1])) &&
				(position + 2 == arguments.size() ||
				 std::iswspace(arguments[position + 2])))
			{
				return position;
			}
		}
		return std::wstring::npos;
	}

	bool StartsWithAbsoluteBatchPath(const std::wstring& command)
	{
		if (command.empty() || command.front() != L'"')
			return false;
		const std::size_t closingQuote = command.find(L'"', 1);
		if (closingQuote == std::wstring::npos)
			return false;
		const std::wstring path = command.substr(1, closingQuote - 1);
		if (!xucrun::command_line::IsAbsolutePath(path))
			return false;
		std::wstring lowerPath = ToLower(path);
		return lowerPath.ends_with(L".bat") || lowerPath.ends_with(L".cmd");
	}
}

namespace xucrun::command_line
{
	std::wstring EscapeArgument(const std::wstring& argument, LogCallback log)
	{
		if (argument.empty())
			return L"\"\"";

		if (argument.size() >= 2 && argument.front() == L'"' &&
			argument.back() == L'"')
		{
			if (log)
				log(L"Info", L"Argument is already quoted; skipping escaping: " + argument);
			return argument;
		}

		if (argument == L"&" || argument == L"|" || argument == L"&&" ||
			argument == L"||" || argument == L">" || argument == L">>" ||
			argument == L"<" || argument == L"2>" || argument == L"2>&1" ||
			argument == L"(" || argument == L")" || argument == L"IF" ||
			argument == L"ELSE" || argument == L"NOT" || argument == L"EXIST" ||
			argument == L"if" || argument == L"else" || argument == L"not" ||
			argument == L"exist")
		{
			return argument;
		}

		bool needsQuoting = false;
		for (wchar_t character : argument)
		{
			if (character == L' ' || character == L'\t' || character == L'"' ||
				character == L'&' || character == L'|' || character == L'<' ||
				character == L'>' || character == L'^' || character == L'(' ||
				character == L')' || character == L'%' || character == L'!')
			{
				needsQuoting = true;
				break;
			}
		}
		if (!needsQuoting)
			return argument;

		std::wstring result = L"\"";
		std::size_t backslashCount = 0;
		for (wchar_t character : argument)
		{
			if (character == L'\\')
			{
				backslashCount++;
			}
			else if (character == L'"')
			{
				result.append(backslashCount * 2 + 1, L'\\');
				result += character;
				backslashCount = 0;
			}
			else
			{
				result.append(backslashCount, L'\\');
				result += character;
				backslashCount = 0;
			}
		}
		result.append(backslashCount * 2, L'\\');
		result += L'"';
		return result;
	}

	std::wstring Build(int argumentCount, wchar_t* arguments[], int startIndex,
		LogCallback log)
	{
		std::wostringstream commandLine;
		for (int index = startIndex; index < argumentCount; index++)
		{
			if (index > startIndex)
				commandLine << L" ";

			std::wstring argument = arguments[index];
			const bool containsSpaces = argument.find(L' ') != std::wstring::npos;
			const bool endsWithOperator = argument.size() > 1 &&
				(argument.back() == L'&' || argument.back() == L'|');
			if (!containsSpaces && endsWithOperator)
			{
				const wchar_t operation = argument.back();
				argument.pop_back();
				commandLine << EscapeArgument(argument, log) << L" " << operation;
				if (log)
					log(L"Info", L"Auto-Fix: Operator '" + std::wstring(1, operation) +
						L"' vom Ende des ungequoteten Arguments getrennt");
				continue;
			}

			std::size_t position = 0;
			bool fixApplied = false;
			while ((position = argument.find(L"\" \"&\" ", position)) != std::wstring::npos)
			{
				argument.replace(position, 6, L"\" & ");
				fixApplied = true;
				position += 4;
			}
			position = 0;
			while ((position = argument.find(L"\" \"|\" ", position)) != std::wstring::npos)
			{
				argument.replace(position, 6, L"\" | ");
				fixApplied = true;
				position += 4;
			}
			if (fixApplied && log)
			{
				log(L"Info", L"Auto-Fix angewendet: Quotierte Befehlsverkettungen "
					L"('\" \"&\"' or '\" \"|\"') corrected to unquoted operators.");
			}
			commandLine << EscapeArgument(argument, log);
		}
		return commandLine.str();
	}

	std::wstring InjectShellWorkingDirectory(
		const std::wstring&,
		const std::wstring& arguments,
		const std::wstring& workingDirectory)
	{
		if (workingDirectory.empty())
			return arguments;

		const std::size_t switchPosition = FindCommandSwitch(arguments);
		if (switchPosition != std::wstring::npos)
		{
			const std::size_t commandStart = arguments.find_first_not_of(
				L" \t", switchPosition + 2);
			if (commandStart != std::wstring::npos &&
				StartsWithAbsoluteBatchPath(arguments.substr(commandStart)))
			{
				return arguments;
			}
			std::wstring result = arguments.substr(0, switchPosition) +
				arguments.substr(switchPosition, 2) + L" \"cd /d " +
				EscapeArgument(workingDirectory);
			if (commandStart != std::wstring::npos)
			{
				std::wstring existingCommand = arguments.substr(commandStart);
				if (existingCommand.size() >= 2 &&
					existingCommand.front() == L'"' &&
					existingCommand.back() == L'"' &&
					std::count(existingCommand.begin(), existingCommand.end(), L'"') == 2 &&
					!IsAbsolutePath(existingCommand.substr(1, existingCommand.size() - 2)))
				{
					existingCommand = existingCommand.substr(
						1, existingCommand.size() - 2);
				}
				result += L" && " + existingCommand;
			}
			result += L"\"";
			return result;
		}

		return arguments;
	}

	bool IsAbsolutePath(const std::wstring& path)
	{
		return path.size() >= 2 &&
			((path[0] == L'\\' && path[1] == L'\\') ||
			(std::iswalpha(path[0]) && path[1] == L':'));
	}

	RawShellArguments ExtractRawShellArguments(
		const std::wstring& programPath,
		const std::wstring& rawCommandLine)
	{
		RawShellArguments result;
		std::wstring fileName = ToLower(programPath);
		const std::size_t lastSeparator = fileName.find_last_of(L"\\/");
		if (lastSeparator != std::wstring::npos)
			fileName = fileName.substr(lastSeparator + 1);

		if (fileName == L"cmd" || fileName == L"cmd.exe")
		{
			result.isShell = true;
			result.shellType = L"cmd";
		}
		else if (fileName == L"tcc.exe" || fileName == L"tcmd.exe")
		{
			result.isShell = true;
			result.shellType = L"tcc";
		}
		else
		{
			return result;
		}

		const std::size_t firstQuote = rawCommandLine.find(L'"');
		const std::size_t secondQuote = firstQuote == std::wstring::npos
			? std::wstring::npos
			: rawCommandLine.find(L'"', firstQuote + 1);
		const std::size_t searchStart = secondQuote == std::wstring::npos
			? 0
			: secondQuote + 1;
		std::size_t parametersStart = std::wstring::npos;

		std::size_t quotedProgramStart = rawCommandLine.find(L" \"", searchStart);
		if (quotedProgramStart != std::wstring::npos)
		{
			quotedProgramStart++;
			const std::size_t quotedProgramEnd = rawCommandLine.find(
				L'"', quotedProgramStart + 1);
			if (quotedProgramEnd != std::wstring::npos)
			{
				const std::wstring quotedPath = ToLower(rawCommandLine.substr(
					quotedProgramStart + 1,
					quotedProgramEnd - quotedProgramStart - 1));
				if ((quotedPath.size() >= 7 &&
					(quotedPath.ends_with(L"cmd.exe") ||
					 quotedPath.ends_with(L"tcc.exe"))) ||
					(quotedPath.size() >= 8 && quotedPath.ends_with(L"tcmd.exe")) ||
					quotedPath == L"cmd")
				{
					parametersStart = quotedProgramEnd + 1;
				}
			}
		}

		if (parametersStart == std::wstring::npos)
		{
			for (const std::wstring shellToken : {
				L" cmd ", L" cmd.exe ", L" tcc.exe ", L" tcmd.exe " })
			{
				const std::size_t shellPosition = rawCommandLine.find(
					shellToken, searchStart);
				if (shellPosition != std::wstring::npos)
				{
					parametersStart = shellPosition + shellToken.size();
					break;
				}
			}
		}

		if (parametersStart == std::wstring::npos)
			return result;
		while (parametersStart < rawCommandLine.size() &&
			rawCommandLine[parametersStart] == L' ')
		{
			parametersStart++;
		}
		result.extracted = true;
		result.arguments = rawCommandLine.substr(parametersStart);
		return result;
	}

	UserSwitch ParseUserSwitch(const std::wstring& argument)
	{
		const std::wstring lowerArgument = ToLower(argument);
		constexpr wchar_t userSwitch[] = L"/user";
		constexpr wchar_t userPrefix[] = L"/user:";
		constexpr wchar_t elevatedSwitch[] = L"/user-elevated";
		constexpr wchar_t elevatedPrefix[] = L"/user-elevated:";

		if (lowerArgument == userSwitch)
			return { UserSwitchKind::SelectCredential, L"" };
		if (lowerArgument == elevatedSwitch || lowerArgument == elevatedPrefix)
			return { UserSwitchKind::SelectElevatedCredential, L"" };

		UserSwitchKind namedKind = UserSwitchKind::None;
		std::size_t prefixLength = 0;
		if (lowerArgument.starts_with(userPrefix))
		{
			namedKind = UserSwitchKind::NamedUser;
			prefixLength = std::size(userPrefix) - 1;
		}
		else if (lowerArgument.starts_with(elevatedPrefix))
		{
			namedKind = UserSwitchKind::NamedElevatedUser;
			prefixLength = std::size(elevatedPrefix) - 1;
		}
		else
		{
			return {};
		}

		std::wstring username = TrimUsername(argument.substr(prefixLength));
		if (username.empty())
			return { UserSwitchKind::Invalid, L"" };
		return { namedKind, std::move(username) };
	}

	std::wstring ExtractUserParameter(int argumentCount, wchar_t* arguments[],
		LogCallback log)
	{
		for (int index = 1; index < argumentCount; index++)
		{
			const std::wstring argument = arguments[index];
			if (argument.size() > 6 && StartsWithUserSwitch(argument))
			{
				const std::wstring username = argument.substr(6);
				if (log)
					log(L"Info", L"User parameter found: " + username);
				return username;
			}
		}
		return L"";
	}

	int RemoveFirstUserParameter(int argumentCount, wchar_t* arguments[])
	{
		int writeIndex = 1;
		bool removed = false;
		for (int readIndex = 1; readIndex < argumentCount; readIndex++)
		{
			if (!removed && StartsWithUserSwitch(arguments[readIndex]))
			{
				removed = true;
				continue;
			}
			arguments[writeIndex++] = arguments[readIndex];
		}
		return writeIndex;
	}
}