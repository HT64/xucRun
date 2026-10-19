// Purpose: Internal command-line helpers for the xucRun client entry point.
//
// Inclusion contract:
// - Included once by main.cpp before mode and execution helpers.
// - Parsing delegates to ClientCommandLine so switch precedence and escaping stay centralized.
// - argv storage remains owned by wWinMain; helpers may reorder pointers but never free them.

// Escapes a single argument string for the command line according to Windows rules
// ==============================================================================
// Implements the official Microsoft escaping rules for command-line arguments.
// See: https://docs.microsoft.com/en-us/archive/blogs/twistylittlepassagesallalike/everyone-quotes-command-line-arguments-the-wrong-way
//
// SPECIAL CASES:
// 1. Arguments that are already fully quoted (for example, by Total Commander)
//    are NOT quoted again.
// 2. CMD operators (&, |, &&, ||, >, <, etc.) are NOT quoted so they are
//    interpreted as operators.
// 3. IF/ELSE/NOT/EXIST are also passed through unchanged.
//
// IMPORTANT: This function is intended for ARGUMENTS, not file names in
//            ShellExecuteEx! Quotes would be handled differently there.
// ==============================================================================
std::wstring EscapeArgument(const std::wstring& arg)
{
	return xucrun::command_line::EscapeArgument(arg, WriteLog);
}

// ==============================================================================
// Creates a command-line string from the arguments
// ==============================================================================
// Takes an argv array and builds a single command-line string from it.
// Each argument is escaped correctly via EscapeArgument().
//
// SPECIAL HANDLING:
// 1. Argument ends with & or | AND contains NO spaces:
//    "PN=test.bat&" → "PN=test.bat" & (operator is separated)
//    BUT: "Path with spaces &&" → remains unchanged (& is part of the name)
//
// 2. Total Commander can send & and | as quoted strings:
//    '" "&" ' → '" & ' (operator is unquoted)
//
// These corrections are necessary because Total Commander sometimes escapes cmd
// operators incorrectly when they occur in button definitions with %VAR%&.
// Arguments containing spaces were originally quoted and must not be changed,
// because & or | may be part of the file or path name.
// ==============================================================================
std::wstring BuildCommandLine(int argc, wchar_t* argv[], int startIndex)
{
	return xucrun::command_line::Build(argc, argv, startIndex, WriteLog);
}

// Extracts the username from the /user:<Username> parameter
// Returns: Username if found, otherwise an empty string
std::wstring ExtractUserParameter(int argc, LPWSTR* argv)
{
	return xucrun::command_line::ExtractUserParameter(argc, argv, WriteLog);
}

// Removes ONLY THE FIRST /user:<Username> parameter from argv
// Returns: New argc (number of remaining arguments)
// IMPORTANT: Removes only the first /user: parameter so nested xucrun calls
//            (for example, "xucrun /user:Admin xucrun /user:Karin cmd") work correctly!
int RemoveUserParameterFromArgv(int argc, LPWSTR* argv)
{
	return xucrun::command_line::RemoveFirstUserParameter(argc, argv);
}
