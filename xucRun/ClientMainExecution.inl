// Purpose: Internal normal-launch implementation for the xucRun client entry point.
//
// Inclusion contract:
// - Included exactly once by main.cpp after argument and mode helpers are defined.
// - The caller retains ownership of argv and releases it after this function returns.
// - Explicit /user-elevated requests reject standard accounts before any credential prompt.
// - Account selection, broker routing, launch visibility, and completion logging stay in this phase.

int RunNormalProgramLaunch(int argc, LPWSTR* argv,
	const std::wstring& specificUserParam, bool requestElevation,
	bool selectCredentialWithoutElevation, bool hiddenLaunch,
	bool monitorHiddenLaunch)
{
	// ==============================================================================
	// Normal process launch with program path and arguments
	// ==============================================================================
	// IMPORTANT: For cmd.exe, the RAW command line is used because Windows argument
	// splitting cannot preserve the original spelling and quote boundaries of a
	// complex shell command line without loss.
	//
	// EXAMPLE PROBLEM:
	// SET PN=c:\Program Files\test.bat& CALL "C:\Program Files\..."
	// may no longer be represented faithfully in argv after shell and Windows parsing,
	// for example:
	// argv[5] = [PN=c:\Program Files"& CALL C:\Program]
	//
	// SOLUTION: For cmd.exe, pass the complete raw command line starting at "cmd".
	// ==============================================================================
	std::wstring programPath = argv[1];
	if (programPath.find(L'"') != std::wstring::npos)
	{
		WriteLog(L"Error", L"Raw command line: " + std::wstring(GetCommandLineW()));
		ReportError(xucrun::localization::Format(
			xucrun::localization::TextId::ProgramPathQuotesInvalid,
			{ programPath }),
			L"The target program path contains an invalid quotation mark: " + programPath);
		WriteLog(L"Info", L"========== xucRun finished (status: 1) ==========");
		return 1;
	}
	std::wstring arguments;
	const auto rawShellArguments = xucrun::command_line::ExtractRawShellArguments(
		programPath, GetCommandLineW());
	if (rawShellArguments.isShell)
	{
		WriteLog(L"Info", L"[" + rawShellArguments.shellType +
			L" detected] Raw command line: " + std::wstring(GetCommandLineW()));
		if (rawShellArguments.extracted)
		{
			arguments = rawShellArguments.arguments;
			WriteLog(L"Info", L"[" + rawShellArguments.shellType + L" detected] Using RAW command line for arguments");
			WriteLog(L"Info", L"  Raw arguments: " + arguments);
		}
		else
		{
			WriteLog(L"Info", L"[" + rawShellArguments.shellType + L" detected] Could not find shell in command line, using BuildCommandLine");
			arguments = BuildCommandLine(argc, argv, 2);
		}
	}
	else
	{
		arguments = BuildCommandLine(argc, argv, 2);
	}

	// Additional user switches in the target arguments remain unchanged for nested
	// XUCRun calls.

	// Get current user info
	std::wstring currentUser, currentDomain;
	if (!GetCurrentUserInfo(currentUser, currentDomain))
	{
		ReportError(xucrun::localization::GetText(
			xucrun::localization::TextId::CurrentUserQueryFailed),
			L"Determining the current account for the target launch failed.");
		return 1;
	}

	WriteLog(L"Info", L"Current user: " + currentDomain + L"\\" + currentUser);

	SameUserLaunchMode sameUserLaunchMode = specificUserParam.empty()
		? SameUserLaunchMode::Credentials
		: GetSameUserLaunchMode(specificUserParam, requestElevation);
	const bool automaticCurrentAdmin = IsUserInAdminGroup() && specificUserParam.empty() && !requestElevation;
	if (automaticCurrentAdmin)
	{
		sameUserLaunchMode = IsRunningAsAdministrator()
			? SameUserLaunchMode::CurrentToken
			: SameUserLaunchMode::ElevateCurrentToken;
	}

	// Without an explicit user, an administrator launches under the current account
	// and automatically receives an elevation prompt for a restricted UAC token.
	// A same-user switch uses the appropriate current token directly.
	if (automaticCurrentAdmin ||
		sameUserLaunchMode == SameUserLaunchMode::CurrentToken ||
		sameUserLaunchMode == SameUserLaunchMode::ElevateCurrentToken)
	{
		bool elevateCurrentToken = sameUserLaunchMode == SameUserLaunchMode::ElevateCurrentToken;
		WriteLog(L"Info", sameUserLaunchMode != SameUserLaunchMode::Credentials
			? (automaticCurrentAdmin
				? (elevateCurrentToken
					? L"Non-elevated admin account without user switch - request UAC elevation"
					: L"Already elevated admin account without user switch - use current token")
				: L"User switch refers to current account - launch program with matching token without credentials")
			: L"Current admin token is used directly");

		const bool launchSucceeded = LaunchDirectly(programPath, arguments,
			elevateCurrentToken, hiddenLaunch, monitorHiddenLaunch);
		WriteLog(L"Info", launchSucceeded
			? L"========== xucRun finished (status: 0) =========="
			: L"========== xucRun finished (status: 1) ==========");
		return launchSucceeded ? 0 : 1;
	}

	const bool useExplicitBrokerLaunch = requestElevation ||
		selectCredentialWithoutElevation || !specificUserParam.empty();
	if (useExplicitBrokerLaunch)
	{
		CredentialInfo selectedCredential;
		DWORD selectionError = NO_ERROR;
		BrokerCredentialSelectionOutcome selection = SelectBrokerCredential(
			specificUserParam, requestElevation, selectedCredential, selectionError);
		if (selection == BrokerCredentialSelectionOutcome::NotFound &&
			(!specificUserParam.empty() || requestElevation))
		{
			WriteLog(L"Info", specificUserParam.empty()
				? L"No administrator broker credential stored - starting administrator credential creation"
				: L"Explicit broker credential missing - starting credential prompt for: " +
					specificUserParam);
			if (!PromptAndSaveBrokerCredentials(
				specificUserParam, GetDialogOwnerWindow(), requestElevation))
			{
				WriteLog(L"Info",
					L"Credential creation for explicit program launch cancelled or failed");
				return 1;
			}
			selection = SelectBrokerCredential(
				specificUserParam, requestElevation, selectedCredential, selectionError);
		}
		if (selection == BrokerCredentialSelectionOutcome::Cancelled)
		{
			WriteLog(L"Info", L"Broker account selection cancelled");
			return 0;
		}
		if (selection == BrokerCredentialSelectionOutcome::Failed)
		{
			ReportError(xucrun::localization::Format(
				xucrun::localization::TextId::BrokerCredentialsQueryFailed,
				{ FormatWindowsError(selectionError) }),
				L"Selecting an explicit broker credential failed. Windows error: " +
					std::to_wstring(selectionError));
			return 1;
		}
		if (selection == BrokerCredentialSelectionOutcome::NotAdministrator)
		{
			ReportError(xucrun::localization::GetText(
				xucrun::localization::TextId::ElevationAccountNotAdmin),
				L"The explicitly selected broker credential account is not an administrator.");
			return 1;
		}
		if (selection == BrokerCredentialSelectionOutcome::NotFound)
		{
			std::wstring message = requestElevation
				? xucrun::localization::GetText(
					xucrun::localization::TextId::BrokerAdminCredentialNotFound)
				: xucrun::localization::GetText(
					xucrun::localization::TextId::BrokerStandardCredentialNotFound);
			if (!specificUserParam.empty())
				message += xucrun::localization::Format(
					xucrun::localization::TextId::AccountDetail,
					{ xucrun::documentation_names::DisplayAccount(
						specificUserParam) });
			message += xucrun::localization::Format(
				xucrun::localization::TextId::SaveCredentialInstruction,
				{ PROGRAM_EXE_NAME });
			ReportError(message,
				requestElevation
				? L"No matching administrator broker credential was found for the explicit launch."
				: L"No matching broker credential was found for the explicit launch.");
			return 1;
		}

		const BrokerLaunchOutcome launchOutcome = LaunchWithBrokerCredential(
			selectedCredential, programPath, arguments,
			requestElevation
				? xucrun::broker::ElevationMode::RequireElevated
				: xucrun::broker::ElevationMode::PreserveToken,
			monitorHiddenLaunch
				? xucrun::broker::LaunchVisibility::HiddenMonitor
				: 				hiddenLaunch
					? xucrun::broker::LaunchVisibility::Hidden
					: xucrun::broker::LaunchVisibility::Normal);
		WriteLog(L"Info", L"========== Broker launch finished (status: " +
				std::to_wstring(launchOutcome == BrokerLaunchOutcome::Failed ? 1 : 0) + L") ==========");
		return launchOutcome == BrokerLaunchOutcome::Failed ? 1 : 0;
	}

	const bool useAutomaticCredentialSelection = !requestElevation &&
		!selectCredentialWithoutElevation && specificUserParam.empty();
	if (useAutomaticCredentialSelection)
	{
		CredentialInfo selectedCredential;
		DWORD selectionError = NO_ERROR;
		WriteLog(L"Info", L"Without user switch -> prefer broker administrator accounts");
		BrokerCredentialSelectionOutcome selection = SelectBrokerCredential(
			L"", true, selectedCredential, selectionError);
		bool selectedAdminCredential = selection ==
			BrokerCredentialSelectionOutcome::Selected;
		if (selection == BrokerCredentialSelectionOutcome::NotFound)
		{
			WriteLog(L"Info", L"No broker administrator account found - using all broker credentials");
			selection = SelectBrokerCredential(
				L"", false, selectedCredential, selectionError);
		}
		if (selection == BrokerCredentialSelectionOutcome::NotFound)
		{
			WriteLog(L"Info", L"No broker credentials available - starting credential creation");
			if (!PromptAndSaveBrokerCredentials(L"", GetDialogOwnerWindow()))
			{
				WriteLog(L"Info", L"Automatic credential creation cancelled or failed");
				return 1;
			}

			selection = SelectBrokerCredential(
				L"", true, selectedCredential, selectionError);
			selectedAdminCredential = selection ==
				BrokerCredentialSelectionOutcome::Selected;
			if (selection == BrokerCredentialSelectionOutcome::NotFound)
			{
				selection = SelectBrokerCredential(
					L"", false, selectedCredential, selectionError);
			}
		}
		if (selection == BrokerCredentialSelectionOutcome::Cancelled)
		{
			WriteLog(L"Info", L"Automatic broker account selection cancelled");
			return 0;
		}
		if (selection == BrokerCredentialSelectionOutcome::Failed)
		{
			ReportError(xucrun::localization::Format(
				xucrun::localization::TextId::BrokerCredentialsQueryFailed,
				{ FormatWindowsError(selectionError) }),
				L"Selecting a broker credential for the automatic launch failed. Windows error: " +
					std::to_wstring(selectionError));
			return 1;
		}
		if (selection == BrokerCredentialSelectionOutcome::NotFound)
		{
			ReportError(xucrun::localization::Format(
				xucrun::localization::TextId::NoBrokerCredentials,
				{ PROGRAM_EXE_NAME }),
				L"No broker credentials are available for the automatic launch.");
			return 1;
		}

		const xucrun::broker::ElevationMode elevationMode = selectedAdminCredential
			? xucrun::broker::ElevationMode::RequireElevated
			: xucrun::broker::ElevationMode::PreserveToken;
		WriteLog(L"Info", selectedAdminCredential
			? L"Broker administrator account selected - requesting elevation automatically"
			: L"Broker standard account selected - launching without additional elevation");
		const BrokerLaunchOutcome launchOutcome = LaunchWithBrokerCredential(
			selectedCredential, programPath, arguments, elevationMode,
			monitorHiddenLaunch
				? xucrun::broker::LaunchVisibility::HiddenMonitor
				: hiddenLaunch
				? xucrun::broker::LaunchVisibility::Hidden
				: xucrun::broker::LaunchVisibility::Normal);
		WriteLog(L"Info", L"========== Automatic broker launch finished (status: " +
			std::to_wstring(launchOutcome == BrokerLaunchOutcome::Failed ? 1 : 0) +
			L") ==========");
		return launchOutcome == BrokerLaunchOutcome::Failed ? 1 : 0;
	}
	ReportError(xucrun::localization::GetText(
		xucrun::localization::TextId::NoSecureBrokerRoute),
		L"No secure broker route is available for the requested launch.");
	return 1;
}
