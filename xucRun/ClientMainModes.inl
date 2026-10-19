// Purpose: Internal standalone-mode helpers for the xucRun client entry point.
//
// Inclusion contract:
// - Included once after ClientMainArguments.inl and before normal launch execution.
// - Internal elevation, help presentation, and result reporting retain their existing order.
// - Each helper returns an explicit result; argv and process-wide initialization remain owned by wWinMain.

// Returns the current UTC time in numeric FILETIME format.
ULONGLONG GetCurrentFileTimeValue()
{
	FILETIME fileTime = {};
	GetSystemTimeAsFileTime(&fileTime);
	ULARGE_INTEGER value = {};
	value.LowPart = fileTime.dwLowDateTime;
	value.HighPart = fileTime.dwHighDateTime;
	return value.QuadPart;
}

// ==============================================================================
// Wrapper mode: Launches the target program via ShellExecuteEx
// ==============================================================================
// This function is launched internally with /internal-elevate by the LocalSystem
// broker under the stored target account.
//
// PURPOSE: Normal target launch or requested UAC elevation with "runas"
//
// SECURITY:
// - Binds broker calls to the actual broker PID through the result pipe handle
// - Prevents external misuse of the internal /internal-elevate switch
//
// PARAMETER PARSING:
// - Uses the raw command line instead of argv[] to avoid quote/backslash issues
//   (argv[] may split parameters incorrectly at \")
//
// WORKING DIRECTORY:
// - Passed via the /workdir parameter and forwarded to ShellExecuteEx
// ==============================================================================
bool ElevatedWrapperMode(int argc, wchar_t* argv[])
{
	BrokerElevatedResultReporter brokerResult(
		ParseBrokerElevatedResultHandle(argc, argv),
		ParseBrokerElevatedAcknowledgementHandle(argc, argv));
	// ==============================================================================
	// SECURITY CHECK: Parent Process Validation
	// ==============================================================================
	// Wrapper mode is an internal mechanism. Calls must carry the inherited result
	// pipe whose actual server process is also the wrapper's parent process.
	// ==============================================================================

	// 1. Determine own path
	wchar_t ownPath[MAX_PATH];
	DWORD ownPathLength = GetModuleFileNameW(NULL, ownPath, MAX_PATH);
	if (ownPathLength == 0 || ownPathLength >= MAX_PATH)
	{
		ReportError(xucrun::localization::GetText(
			xucrun::localization::TextId::SecurityOwnPathFailed),
			L"[Wrapper mode] Security validation could not determine the xucRun path.");
		return false;
	}

	// 2. Determine parent process ID
	DWORD currentPid = GetCurrentProcessId();
	DWORD parentPid = GetParentProcessId(currentPid);
	if (parentPid == 0)
	{
		ReportError(xucrun::localization::GetText(
			xucrun::localization::TextId::SecurityParentFailed),
			L"[Wrapper mode] Security validation could not determine the parent process ID.");
		return false;
	}

	// 3. The server PID reported by the pipe kernel object and the running service
	// PID reported by SCM must both exactly match the actual parent PID.
	DWORD brokerServicePid = 0;
	DWORD brokerServiceError = NO_ERROR;
	if (!brokerResult.active() || !brokerResult.serverProcessMatches(parentPid) ||
		!xucrun::broker::QueryRunningBrokerServiceProcessId(
			brokerServicePid, brokerServiceError) ||
		brokerServicePid != parentPid)
	{
		WriteLog(L"Error", L"[Wrapper mode] Own path:     " + std::wstring(ownPath));
		WriteLog(L"Error", L"[Wrapper mode] Parent PID:   " + std::to_wstring(parentPid));
		WriteLog(L"Error", L"[Wrapper mode] Service PID:  " +
			std::to_wstring(brokerServicePid));
		if (brokerServiceError != NO_ERROR)
		{
			WriteLog(L"Error", L"[Wrapper mode] Service query error: " +
				std::to_wstring(brokerServiceError));
		}

		ReportError(xucrun::localization::Format(
			xucrun::localization::TextId::SecurityUnauthorizedParent,
			{ ownPath, std::to_wstring(parentPid) }),
			L"[Wrapper mode] Security validation rejected the unauthorized parent process.");
		return false;
	}

	WriteLog(L"Info", L"[Wrapper mode] Security check passed: parent process is authorized.");
	WriteLog(L"Info", L"[Wrapper mode] Parent PID: " + std::to_wstring(parentPid));

	// ===============================================================================
	// End of security check
	// ===============================================================================

	// Format: xucRun.exe /internal-elevate [/internal-elevated]
	//   [/internal-hidden|/internal-hidden-monitor] /internal-broker-result:<Handle>
	//   /internal-broker-ack:<Handle>
	//   [/workdir "<Directory>"] <TargetProgram> [<Parameters...>]
	ElevatedWrapperArguments wrapperArguments;
	const ElevatedWrapperParseStatus parseStatus = ParseElevatedWrapperCommandLine(
		argc, GetCommandLineW(), wrapperArguments);
	if (parseStatus == ElevatedWrapperParseStatus::ProgramMissing)
	{
		ReportError(xucrun::localization::GetText(
			xucrun::localization::TextId::InternalElevationProgramMissing),
			L"[Wrapper mode] No target program was specified.");
		return false;
	}
	if (parseStatus == ElevatedWrapperParseStatus::ElevationSwitchMissing)
	{
		ReportError(xucrun::localization::GetText(
			xucrun::localization::TextId::InternalElevationSwitchMissing),
			L"[Wrapper mode] The required /internal-elevate switch is missing.");
		return false;
	}
	if (parseStatus == ElevatedWrapperParseStatus::InvalidBrokerResultMarker)
	{
		brokerResult.complete(ERROR_INVALID_PARAMETER);
		return false;
	}

	const bool requestElevation = wrapperArguments.requestElevation;
	const bool hidden = wrapperArguments.hidden;
	const bool monitorHiddenProcess = wrapperArguments.monitorHiddenProcess;
	const std::wstring& targetWorkingDirectory = wrapperArguments.workingDirectory;
	std::wstring& targetProgram = wrapperArguments.targetProgram;
	std::wstring& targetArguments = wrapperArguments.targetArguments;

	bool isInAdminGroup = IsUserInAdminGroup();
	if (requestElevation && !isInAdminGroup)
	{
		ReportError(xucrun::localization::GetText(
			xucrun::localization::TextId::ElevationAccountNotAdmin),
			L"[Wrapper mode] Elevation was requested for a target account that is not "
			L"an administrator.");
		return false;
	}
	const bool shouldElevate = requestElevation;
	const auto launchPreparation = PrepareElevatedWrapperLaunch(
		targetProgram, targetArguments, targetWorkingDirectory, shouldElevate);
	return LaunchPreparedElevatedWrapperProcess(
		launchPreparation,
		targetProgram,
		shouldElevate,
		hidden,
		monitorHiddenProcess,
		brokerResult);
}

// ==============================================================================
// Displays a consistent scrollable text dialog
// ==============================================================================
// Simplified user help with the most important commands.
// See README.md and the header comment in main.cpp for complete documentation.
// ==============================================================================
// Creates a modal dialog template for longer read-only informational text.
void ShowScrollableTextDialog(const std::wstring& title, const std::wstring& heading,
	const std::wstring& content)
{
	xucrun::text_dialog::Show(
		GetWindowTitle(title), heading, content, GetDialogOwnerWindow());
}

// Emits the prepared help text through the consistent text output path.
void ShowHelpDialog(const std::wstring& help)
{
	ShowScrollableTextDialog(xucrun::localization::GetText(
		xucrun::localization::TextId::HelpTitle),
		xucrun::localization::GetText(xucrun::localization::TextId::HelpHeading), help);
}

// Composes the compact command overview and the most important behavioral notes.
void ShowHelp()
{
	const std::wstring executableName = PROGRAM_EXE_NAME;
	std::wstring help = xucrun::localization::Format(
		xucrun::localization::TextId::HelpUsage, { executableName });
	help += xucrun::localization::Format(
		xucrun::localization::TextId::HelpInfo, { executableName });
	help += xucrun::localization::Format(
		xucrun::localization::TextId::HelpExamples, { executableName });
	help += xucrun::localization::GetText(xucrun::localization::TextId::HelpNotes);


	WriteLog(L"Info", L"Help displayed");
	ShowHelpDialog(help);
}
