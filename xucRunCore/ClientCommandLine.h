// Purpose: Defines the UI-independent rules for parsing and generating client command lines.
// Operation: Encapsulates escaping, path validation, user switches, and unchanged cmd/TCC raw arguments.
#pragma once

#include <string>

namespace xucrun::command_line
{
	using LogCallback = void (*)(const std::wstring& level, const std::wstring& message);

	enum class UserSwitchKind
	{
		None,
		SelectCredential,
		SelectElevatedCredential,
		NamedUser,
		NamedElevatedUser,
		Invalid
	};

	struct UserSwitch
	{
		UserSwitchKind kind = UserSwitchKind::None;
		std::wstring username;
	};

	struct RawShellArguments
	{
		bool isShell = false;
		bool extracted = false;
		std::wstring shellType;
		std::wstring arguments;
	};

	std::wstring EscapeArgument(const std::wstring& argument,
		LogCallback log = nullptr);
	std::wstring Build(int argumentCount, wchar_t* arguments[], int startIndex,
		LogCallback log = nullptr);
	std::wstring InjectShellWorkingDirectory(
		const std::wstring& shellPath,
		const std::wstring& arguments,
		const std::wstring& workingDirectory);
	bool IsAbsolutePath(const std::wstring& path);
	RawShellArguments ExtractRawShellArguments(
		const std::wstring& programPath,
		const std::wstring& rawCommandLine);
	UserSwitch ParseUserSwitch(const std::wstring& argument);
	std::wstring ExtractUserParameter(int argumentCount, wchar_t* arguments[],
		LogCallback log = nullptr);
	int RemoveFirstUserParameter(int argumentCount, wchar_t* arguments[]);
}