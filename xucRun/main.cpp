// Purpose: Implements the xucRun client entry point with broker integration and file logging.
// Approach: Implements the behavior with focused helpers built on shared xucRun interfaces and Windows APIs.

// ============================================================================
// COMMAND-LINE SYNTAX & SWITCHES
// ============================================================================
//
// NORMAL USAGE:
// -------------
// xucRun.exe <Program> [Parameters...]
//
//   Launches <Program> under the current or a stored account.
//   Without a user switch, stored administrator accounts are preferred:
//   a current administrator account is elevated via UAC when necessary; otherwise,
//   a single credential match launches elevated automatically, while multiple matches are presented for selection.
//   Standard accounts are launched normally only if no administrator match exists.
//   /user[:<Username>] suppresses additional elevation; for a named same-user
//   account, the current token state remains unchanged.
//   /user-elevated[:<Username>] explicitly ensures elevated privileges.
//
//   IMPORTANT: Stored credentials are required to switch accounts.
//              UAC elevation is possible only with an administrator account.
//
//   Examples:
//     xucRun.exe notepad.exe
//     xucRun.exe cmd /k "dir"
//     xucRun.exe powershell.exe -NoExit
//     xucRun.exe /hidden powershell.exe -File script.ps1
//     xucRun.exe /hidden-monitor cmd.exe /c task.cmd
//
//   /hidden hides only the immediate target program. /hidden-monitor also launches
//   it hidden and monitors its exit code in a separate invisible xucRun process.
//   A nonzero exit code displays a visible notification; process output and child
//   processes are not captured.
//
//
// SWITCHES:
// ---------
//
// Only the first unprocessed argument is evaluated as a possible XUCRun switch.
// If it starts with "/" or "--", it must be a known XUCRun switch. After the
// program name, all subsequent switches belong unchanged to the target program
// or the explicitly launched shell.
//
// Examples:
//   xucRun.exe /unknown                  -> Error: unknown XUCRun switch
//   xucRun.exe cmd.exe /c dir            -> /c belongs to cmd.exe
//   xucRun.exe app.exe /config:value     -> /config:value belongs to app.exe
//
// Shell built-in commands such as dir or Set-Location are not program names.
// They must be invoked through cmd.exe /c or PowerShell -Command, respectively.
//
// 1. /addcred [<Username>]
//    Without a username, enabled local accounts are offered for selection;
//    "Other account" still permits free-form input in the secure CredUI dialog.
//    With a username, CredUI opens directly with the username prefilled.
//    The password is passed to the LocalSystem broker, validated there, and
//    stored exclusively in its credential store.
//
//    SECURITY: The password does NOT appear in the command line, process list, or logs!
//
//    IMPORTANT: For /user-elevated, the stored account must be a member of the
//               Administrators group.
//
//    Usage:
//      xucRun.exe /addcred Administrator
//      xucRun.exe /addcred DOMAIN\AdminUser
//      xucRun.exe /addcred              (select a local account or enter one freely)
//      xucRun.exe /addcred /user:Administrator      (runas-compatible)
//      xucRun.exe /addcred /user:DOMAIN\AdminUser   (runas-compatible)
//
//    Note: Credentials from "runas /savecred" are not used.
//          xucRunBroker manages its own LocalSystem store;
//          runas uses CRED_TYPE_DOMAIN_PASSWORD with a different target schema.
//
//
// 2. /user[:<Username>]
//    Without a username, the switch selects from all stored accounts.
//    With a username, it selects a specific account. No credentials are needed
//    for the same Windows SID; the current token state remains unchanged.
//
//    Usage:
//      xucRun.exe /user notepad.exe
//      xucRun.exe /user:Administrator notepad.exe
//      xucRun.exe /user:DOMAIN\AdminUser cmd.exe
//
//    Note: Without a user switch, administrator accounts are preferred.
//          In contrast, /user without a name offers all stored accounts.
//
//    /user-elevated can also be specified without a username. In that case,
//    only stored accounts that are members of the local Administrators group
//    are considered. A single match is used automatically; multiple matches
//    are displayed in the selection box.
//
//
// 3. /showcred
//    Displays the stored accounts reported by the broker.
//    Output uses the console, redirected standard output, or a table dialog.
//
//    Usage:
//      xucRun.exe /showcred
//
//    Table columns: target account, last update, account type, and number of
//    applicable active approvals. Console output adds the account SID.
//
//
// 4. /showapprovals
//    Displays all-program and target-program broker approvals with their status.
//    Target-program approvals are bound to the target path and target hash.
//    Details include lifetime, account and approval scope, target path, target hash,
//    working directory, and privilege mode. Expired timed approvals and approvals from previous logon
//    sessions are removed by the broker when read.
//
//
// 5. /deletecred [<Username>]
//    Deletes a stored broker credential after confirmation.
//    Without a username, a selection is always displayed.
//
//    Usage:
//      xucRun.exe /deletecred
//      xucRun.exe /deletecred Administrator
//      xucRun.exe /deletecred /user:DOMAIN\AdminUser
//
//
// 6. /resetapprovals[:<Username>]
//    Selects a credential or the profile-wide account scope and resets active
//    approvals. A targeted invocation does not modify profile-wide entries.
//    With multiple approvals, a second multi-selection displays the all-program
//    approval and individual target-program approvals.
//
//
// 7. /clear-all
//    After administrative confirmation, deletes all xucRun credentials and
//    approvals from every Windows user profile on this computer, then stops
//    the broker service.
//
//    Usage from an elevated console:
//      xucRun.exe /clear-all
//

// 8. /audit
//    Audits the running client, installed Core files, signatures, certificate,
//    broker policy and ACLs, configuration, and an optional Tray installation.
//    The result and detailed findings use the same text output path as /help,
//    /showcred, and /showapprovals. No credentials are read or decrypted.
//
//    Usage:
//      xucRun.exe /audit
//
//
// 9. /help, --help, /?
//    Displays this help.
//
//    Usage:
//      xucRun.exe /help
//      xucRun.exe /?
//
//
// 10. /internal-elevate [/internal-elevated] [/internal-hidden|/internal-hidden-monitor]
//     /internal-broker-result:<Handle> /internal-broker-ack:<Handle>
//                      [/workdir "<Directory>"] <Program> [Parameters...]
//    INTERNAL SWITCH - Not for manual use!
//
//    The LocalSystem broker uses this wrapper for RequireElevated, direct batch
//    targets, all HiddenMonitor launches, and as the PreserveToken fallback when
//    CreateProcessAsUserW fails with ERROR_ACCESS_DENIED.
//
//    The /workdir parameter carries the original working directory through both stages.
//
//    Note: This switch is used ONLY internally and should not be invoked manually!
//
//
// WORKING DIRECTORY PRESERVATION:
// --------------------------------
// XUCRun preserves the working directory throughout the entire process:
//
// - Stage 1: Passed as /workdir; lpCurrentDirectory remains NULL
// - Stage 2: lpDirectory → ShellExecuteEx
// - Replacement consoles:
//     • cmd/tcc:     Preserve /c or /k and inject "cd /d <workdir>" unless an
//                    absolute batch path must remain unchanged
//     • PowerShell:  Inject "Set-Location '<workdir>'; <command>"
//
// This causes shells to start in the original directory instead of C:\Windows\System32.
//
//
// SPECIAL CHARACTER HANDLING:
// ---------------------------
// Broker-mediated batch files with cmd metacharacters (& | ( ) ! % ^) in the file name
// are launched through protected temporary wrappers. The broker removes the wrapper
// after the target exits.
//
//
// INCLUDED HEADER DIRECTORY:
// --------------------------
// Windows and platform headers:
//   Windows.h                         Win32 types, process, token, window, and service APIs.
//   shellapi.h                        ShellExecuteEx and Windows shell integration.
//   shlobj.h                          Windows shell folders and related shell helpers.
//   wincred.h                         Windows Credential UI and credential structures.
//   wincrypt.h                        Windows cryptographic and certificate APIs.
//   tlhelp32.h                        Tool Help process and thread snapshots.
//   sddl.h                            Security descriptor and SID string conversion.
//   Softpub.h                         Authenticode trust policy definitions.
//   mscat.h                           Windows catalog signature definitions.
//
// C and C++ standard headers:
//   string                            Wide strings used throughout the client.
//   sstream                           Structured in-memory text formatting.
//   vector                            Dynamic collections and Win32 buffers.
//   algorithm                         Search, transformation, and collection algorithms.
//   fstream                           Client file output and file-backed diagnostics.
//   io.h                              Microsoft C runtime file descriptor operations.
//   fcntl.h                           File descriptor mode constants.
//   chrono                            Time points and duration calculations.
//   iomanip                           Stream formatting manipulators.
//
// xucRun project headers:
//   ApprovalSerialization.h           Stored approval types, hashes, and serialization.
//   BrokerContracts.h                 Versioned broker request, result, and status contracts.
//   BrokerPipeClient.h                Authenticated client calls to the LocalSystem broker pipe.
//   ClientCommandLine.h               Parsing, validation, and reconstruction of client arguments.
//   ClientProcessContext.h            Parent-process queries and supported shell classification.
//   BuildVersion.h                    Generated build number and build date constants.
//   ClientAccountIdentity.h           Account, SID, token, and administrator identity checks.
//   ClientAdministrativeCommands.h    Dispatch of public credential and approval commands.
//   ClientBrokerStatus.h              Broker service status and administrative commands.
//   ClientBrokerLaunch.h              Challenge-bound launches through stored broker credentials.
//   ClientBrokerCredentialManagement.h Creation and deletion of broker-stored credentials.
//   ClientBrokerCredentialSelection.h Retrieval and selection of broker credential metadata.
//   ClientConsoleReplacement.h        Replacement of an invoking console under the chosen account.
//   ClientConsoleSession.h            Detection, arguments, and closure of the invoking console.
//   ClientCredentialFormatting.h      Localized formatting of credential and broker status data.
//   ClientCredentialInfo.h            Client-side metadata for broker-reported credentials.
//   ClientCredentialSelectionDialog.h Selection UI for stored credential accounts.
//   ClientElevatedWrapperLaunch.h      ShellExecuteEx launch of the prepared internal wrapper.
//   ClientElevatedWrapperPreparation.h Program, argument, and directory preparation for the wrapper.
//   ClientElevatedWrapperParsing.h     Parsing of internal elevated-wrapper command lines.
//   ClientElevatedWrapperReporter.h    Broker-bound reporting of wrapper launch results.
//   ClientHiddenLaunchModifiers.h      Parsing of hidden and monitored-hidden launch modifiers.
//   ClientInternalBrokerTests.h        Dispatch of internal broker integration-test commands.
//   ClientMainArguments.inl            Command-line escaping and user-switch pointer helpers.
//   ClientMainModes.inl                Internal wrapper, help, and standalone client modes.
//   ClientMainExecution.inl            Normal account selection and program launch execution.
//   ClientLogging.h                    Initialization and writing of the shared client log.
//   ClientProcessMonitoring.h          Monitoring and error reporting for hidden target processes.
//   ClientSameUserLaunch.h             Direct launch with the current or elevated user token.
//   ClientTextDialog.h                 Console-aware display of long read-only text.
//   ClientUiCommon.h                   Shared window ownership, icons, titles, and notifications.
//   ClientWindowsError.h               Localized formatting of Windows error codes.
//   ClientWindowActivation.h           PID-bound activation of launched process windows.
//   DocumentationNameMapping.h         Optional display-only names for documentation screenshots.
//   Localization.h                     Language-resource loading, lookup, and formatting.
//   ClientResourceIds.h                Shared resource identifiers for dialogs, icons, and text.
//
// ============================================================================

#include <Windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wincred.h>
#include <wincrypt.h>
#include <tlhelp32.h>
#include <sddl.h>
#include <Softpub.h>
#include <mscat.h>
#include <string>
#include <sstream>
#include <vector>
#include <algorithm>
#include <fstream>
#include <io.h>
#include <fcntl.h>
#include <chrono>
#include <iomanip>
#include "ApprovalSerialization.h"
#include "BrokerContracts.h"
#include "BrokerPipeClient.h"
#include "ClientCommandLine.h"
#include "ClientProcessContext.h"
#include "BuildVersion.h"
#include "ClientAccountIdentity.h"
#include "ClientAdministrativeCommands.h"
#include "ClientBrokerStatus.h"
#include "ClientBrokerLaunch.h"
#include "ClientBrokerCredentialManagement.h"
#include "ClientBrokerCredentialSelection.h"
#include "ClientConsoleReplacement.h"
#include "ClientConsoleSession.h"
#include "ClientCredentialFormatting.h"
#include "ClientCredentialInfo.h"
#include "ClientCredentialSelectionDialog.h"
#include "ClientElevatedWrapperLaunch.h"
#include "ClientElevatedWrapperPreparation.h"
#include "ClientElevatedWrapperParsing.h"
#include "ClientElevatedWrapperReporter.h"
#include "ClientHiddenLaunchModifiers.h"
#ifdef _DEBUG
#include "ClientInternalBrokerTests.h"
#endif
#include "ClientLogging.h"
#include "ClientProcessMonitoring.h"
#include "ClientSameUserLaunch.h"
#include "ClientTextDialog.h"
#include "ClientUiCommon.h"
#include "ClientWindowActivation.h"
#include "ClientWindowsError.h"
#include "DocumentationNameMapping.h"
#include "Localization.h"
#include "ClientResourceIds.h"

using xucrun::account_identity::GetCanonicalAccountName;
using xucrun::account_identity::GetCurrentUserInfo;
using xucrun::account_identity::IsCurrentUserAccount;
using xucrun::account_identity::IsRunningAsAdministrator;
using xucrun::account_identity::IsUserInAdminGroup;
using xucrun::account_identity::ResolveAccountSid;
using xucrun::broker_launch::BrokerLaunchOutcome;
using xucrun::broker_launch::LaunchWithBrokerCredential;
using xucrun::broker_credential_management::DeleteBrokerCredential;
using xucrun::broker_credential_management::DeleteStoredCredential;
using xucrun::broker_credential_management::PromptAndSaveBrokerCredentials;
using xucrun::broker_credential_management::ShowBrokerCredentials;
using xucrun::broker_credential_selection::BrokerCredentialSelectionOutcome;
using xucrun::broker_credential_selection::SelectBrokerCredential;
using xucrun::credential_formatting::CredentialSelectionMode;
using xucrun::credential_formatting::FormatCredentialTimestamp;
using xucrun::credential_formatting::GetCredentialSelectionTitle;
using xucrun::credential_selection_dialog::ShowCredentialSelectionDialog;
using xucrun::elevated_wrapper::BrokerElevatedResultReporter;
using xucrun::elevated_wrapper::ParseBrokerElevatedAcknowledgementHandle;
using xucrun::elevated_wrapper::ElevatedWrapperArguments;
using xucrun::elevated_wrapper::ElevatedWrapperParseStatus;
using xucrun::elevated_wrapper::LaunchPreparedElevatedWrapperProcess;
using xucrun::elevated_wrapper::ParseElevatedWrapperCommandLine;
using xucrun::elevated_wrapper::ParseBrokerElevatedResultHandle;
using xucrun::elevated_wrapper::PrepareElevatedWrapperLaunch;
using xucrun::process_monitoring::HiddenProcessMonitorMode;
using xucrun::same_user_launch::GetSameUserLaunchMode;
using xucrun::same_user_launch::LaunchDirectly;
using xucrun::same_user_launch::SameUserLaunchMode;
using xucrun::ui::ApplyApplicationIcon;
using xucrun::ui::GetDialogOwnerWindow;
using xucrun::ui::GetWindowTitle;
using xucrun::ui::ShowInfo;
using xucrun::ui::ShowMessageBox;
using xucrun::ui::ReportError;
using xucrun::ui::ReportWarning;
using xucrun::windows_error::FormatWindowsError;
using CredentialApprovalMode = xucrun::approval::ApprovalMode;
using xucrun::CredentialInfo;

// Defensive defaults if these generated BuildVersion.h macros are unavailable.
#ifndef BUILD_NUMBER
#define BUILD_NUMBER 0
#endif
#ifndef BUILD_DATE
#define BUILD_DATE __DATE__
#endif

#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "Credui.lib")
#pragma comment(lib, "Netapi32.lib")
#pragma comment(lib, "Bcrypt.lib")
#pragma comment(lib, "Crypt32.lib")
#pragma comment(lib, "Wintrust.lib")

// ====== PROGRAM CONSTANTS ======

constexpr const wchar_t* PROGRAM_EXE_NAME = L"xucRun.exe";

// Enables early process mitigations before security-sensitive access and collects failures as warnings.
std::vector<std::wstring> ApplyEarlyProcessHardening()
{
	std::vector<std::wstring> warnings;
	auto recordFailure = [&warnings](const wchar_t* mitigation)
	{
		warnings.push_back(std::wstring(mitigation) + L" could not be enabled (error " +
			FormatWindowsError(GetLastError()) + L")");
	};

	if (!SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32 | LOAD_LIBRARY_SEARCH_USER_DIRS))
		recordFailure(L"Restricted DLL search");

	PROCESS_MITIGATION_DYNAMIC_CODE_POLICY dynamicCodePolicy = {};
	dynamicCodePolicy.ProhibitDynamicCode = 1;
	if (!SetProcessMitigationPolicy(ProcessDynamicCodePolicy, &dynamicCodePolicy, sizeof(dynamicCodePolicy)))
		recordFailure(L"Prohibit dynamic code generation");

	PROCESS_MITIGATION_EXTENSION_POINT_DISABLE_POLICY extensionPointPolicy = {};
	extensionPointPolicy.DisableExtensionPoints = 1;
	if (!SetProcessMitigationPolicy(ProcessExtensionPointDisablePolicy, &extensionPointPolicy, sizeof(extensionPointPolicy)))
		recordFailure(L"Disable legacy extension points");

	PROCESS_MITIGATION_IMAGE_LOAD_POLICY imageLoadPolicy = {};
	imageLoadPolicy.NoRemoteImages = 1;
	imageLoadPolicy.NoLowMandatoryLabelImages = 1;
	imageLoadPolicy.PreferSystem32Images = 1;
	if (!SetProcessMitigationPolicy(ProcessImageLoadPolicy, &imageLoadPolicy, sizeof(imageLoadPolicy)))
		recordFailure(L"Restricted image loading");

	return warnings;
}

// ====== PARENT SHELL DETECTION FOR CONSOLE REPLACEMENT ======

using xucrun::process_context::GetParentProcessId;
// ====== LOGGING SYSTEM (CONTINUED) ======

void WriteLog(const std::wstring& level, const std::wstring& message)
{
	xucrun::client_logging::Write(level, message);
}

#include "ClientMainArguments.inl"
#include "ClientMainModes.inl"
#include "ClientMainExecution.inl"

// ====== WINMAIN ENTRY POINT ======

// Controls initialization, switch parsing, security checks, credential selection, and the chosen launch path.
// Internal modes are recognized before normal arguments; security-relevant errors always abort instead of using a weaker fallback.
int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow)
{
	std::wstring localizationError;
	if (!xucrun::localization::Initialize(localizationError))
	{
		ShowMessageBox(nullptr, localizationError, L"xucRun - Localization error",
			MB_OK | MB_ICONERROR);
		return ERROR_RESOURCE_LANG_NOT_FOUND;
	}
	const std::vector<std::wstring> hardeningWarnings = ApplyEarlyProcessHardening();
	std::wstring documentationNameError;
	if (!xucrun::documentation_names::Initialize(documentationNameError))
	{
		ShowMessageBox(nullptr, documentationNameError,
			L"xucRun - Documentation name mapping error",
			MB_OK | MB_ICONERROR);
		return ERROR_INVALID_DATA;
	}

	// Initialize logging and report a configured but unusable target path once.
	std::wstring configuredLogDirectory;
	std::wstring failedLogDirectory;
	std::wstring logConfigurationPath;
	DWORD logInitializationError = NO_ERROR;
	if (!xucrun::client_logging::Initialize(configuredLogDirectory, failedLogDirectory,
		logConfigurationPath, logInitializationError))
	{
		ReportWarning(xucrun::localization::Format(
			xucrun::localization::TextId::ClientLogInitializationFailed,
			{ configuredLogDirectory, failedLogDirectory, logConfigurationPath,
				FormatWindowsError(logInitializationError) }),
			L"Client file logging initialization failed. Windows error: " +
				std::to_wstring(logInitializationError));
	}
	for (const std::wstring& warning : hardeningWarnings)
		WriteLog(L"Warning", warning);

	// Parse command-line arguments
	int argc;
	LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	if (argv == NULL)
	{
		ReportError(xucrun::localization::GetText(
			xucrun::localization::TextId::CommandLineParseFailed),
			L"Parsing the process command line with CommandLineToArgvW failed.");
		return 1;
	}
	const auto writeModeStart = [](const std::wstring& mode)
	{
		std::wstring username;
		std::wstring domain;
		const std::wstring account = GetCurrentUserInfo(username, domain)
			? (domain.empty() ? username : domain + L"\\" + username)
			: L"<unavailable>";
		std::wostringstream message;
		message << L"xucRun process started. Mode: " << mode <<
			L"; Account: " << account <<
			L"; Elevated: " << (IsRunningAsAdministrator() ? L"true" : L"false") <<
			L"; build from " << __DATE__ << L" " << __TIME__;
		WriteLog(L"Info", message.str());
	};
	std::wstring initialArgument = argc >= 2 ? argv[1] : L"";
	std::transform(initialArgument.begin(), initialArgument.end(),
		initialArgument.begin(), ::towlower);
	std::wstring processMode = L"program or console launch";
	if (argc < 2)
		processMode = L"default console launch";
	else if (initialArgument.starts_with(L"/internal-monitor:"))
		processMode = L"hidden process monitor";
	else if (initialArgument == L"/internal-elevate")
		processMode = L"elevated launch wrapper";
	else if (initialArgument == L"/internal-admin-status-view")
		processMode = L"administrative status view";
	else if (initialArgument.starts_with(L"/internal-admin-status-console:"))
		processMode = L"administrative status console";
	else if (initialArgument == L"/internal-backup-view")
		processMode = L"administrative backup view";
	else if (initialArgument == L"/internal-backup-restore-view")
		processMode = L"administrative backup restore view";
	else if (initialArgument.starts_with(L"/internal-backup-command:"))
		processMode = L"administrative backup command";
	else if (initialArgument.starts_with(L"/internal-set-language:"))
		processMode = L"language change helper";
	else if (initialArgument.starts_with(L"/internal-broker-"))
		processMode = L"internal broker test";
	else if (initialArgument == L"/enable-broker")
		processMode = L"broker enable command";
	else if (initialArgument == L"/help" || initialArgument == L"--help" ||
		initialArgument == L"/?")
		processMode = L"help command";
	else if (initialArgument == L"/info")
		processMode = L"runtime information command";
	else if (initialArgument == L"/audit")
		processMode = L"security diagnostics command";
	else if (initialArgument == L"/admin-status")
		processMode = L"administrative status launcher";
	else if (initialArgument == L"/backup" ||
		initialArgument.starts_with(L"/backup-"))
		processMode = L"backup command";
	else if (initialArgument == L"/addcred" ||
		initialArgument == L"/deletecred" ||
		initialArgument == L"/showcred")
		processMode = L"credential management command";
	else if (initialArgument == L"/showapprovals" ||
		initialArgument == L"/resetapprovals" ||
		initialArgument.starts_with(L"/resetapprovals:"))
		processMode = L"approval management command";
	else if (initialArgument == L"/clear-all")
		processMode = L"broker store purge command";
	else if (initialArgument == L"/system")
		processMode = L"LocalSystem launch";
	writeModeStart(processMode);

	if (argc == 2 && _wcsicmp(argv[1], L"/enable-broker") == 0)
	{
		LocalFree(argv);
		DWORD error = NO_ERROR;
		if (xucrun::broker_status::EnableBrokerService(error))
			return 0;
		const std::wstring details = error == NO_ERROR
			? xucrun::localization::GetText(xucrun::localization::TextId::CauseUnknown)
			: xucrun::localization::Format(xucrun::localization::TextId::ErrorCode,
				{ FormatWindowsError(error) });
		ReportError(xucrun::localization::Format(
			xucrun::localization::TextId::BrokerStartFailed, { details }),
			L"Enabling the broker service failed. Windows error: " +
				std::to_wstring(error));
		return 1;
	}
	bool hiddenLaunch = false;
	bool monitorHiddenLaunch = false;
	if (!xucrun::hidden_launch::ProcessHiddenLaunchModifier(
		argc, argv, hiddenLaunch, monitorHiddenLaunch))
	{
		LocalFree(argv);
		return 1;
	}
	// Check whether only a /user parameter is present (without a program)
	bool onlyUserParameter = false;
	bool requestElevation = false;
	bool selectCredentialWithoutElevation = false;
	std::wstring userParamForConsole = L"";

	if (argc == 2)
	{
		const auto userSwitch = xucrun::command_line::ParseUserSwitch(argv[1]);
		switch (userSwitch.kind)
		{
		case xucrun::command_line::UserSwitchKind::Invalid:
			ReportError(xucrun::localization::Format(
				xucrun::localization::TextId::SwitchUsernameRequired, { argv[1] }),
				L"An invalid user switch without a required account name was specified: " +
					std::wstring(argv[1]));
			LocalFree(argv);
			return 1;
		case xucrun::command_line::UserSwitchKind::SelectCredential:
			onlyUserParameter = true;
			selectCredentialWithoutElevation = true;
			WriteLog(L"Info", L"/user without user - select stored account without additional elevation");
			break;
		case xucrun::command_line::UserSwitchKind::SelectElevatedCredential:
			onlyUserParameter = true;
			requestElevation = true;
			WriteLog(L"Info", L"/user-elevated without user - select stored administrator account for elevated console");
			break;
		case xucrun::command_line::UserSwitchKind::NamedUser:
			onlyUserParameter = true;
			userParamForConsole = userSwitch.username;
			WriteLog(L"Info", L"Only /user parameter without program - launching console without elevation for user: " + userParamForConsole);
			break;
		case xucrun::command_line::UserSwitchKind::NamedElevatedUser:
			userParamForConsole = userSwitch.username;
			onlyUserParameter = true;
			requestElevation = true;
			WriteLog(L"Info", L"/user-elevated as sole parameter - launching elevated console for user: " + userParamForConsole);
			break;
		case xucrun::command_line::UserSwitchKind::None:
			break;
		}
	}

	if (argc < 2 || onlyUserParameter)
	{
		xucrun::console_replacement::ConsoleReplacementRequest request;
		request.onlyUserParameter = onlyUserParameter;
		request.requestElevation = requestElevation;
		request.selectCredentialWithoutElevation = selectCredentialWithoutElevation;
		request.username = userParamForConsole;
		request.programExecutableName = PROGRAM_EXE_NAME;

		const xucrun::console_replacement::ConsoleReplacementResult result =
			xucrun::console_replacement::Run(request);
		if (result.showHelp)
		{
			ShowHelp();
		}
		LocalFree(argv);

		switch (result.completion)
		{
		case xucrun::console_replacement::ConsoleReplacementCompletion::Succeeded:
			WriteLog(L"Info", L"========== xucRun finished (console replacement) ==========");
			break;
		case xucrun::console_replacement::ConsoleReplacementCompletion::LaunchFailed:
			WriteLog(L"Error", L"Program exit (error launching elevated console)");
			break;
		case xucrun::console_replacement::ConsoleReplacementCompletion::UnknownParent:
			WriteLog(L"Info", L"Program exit (no arguments, unknown console)");
			break;
		case xucrun::console_replacement::ConsoleReplacementCompletion::Cancelled:
		case xucrun::console_replacement::ConsoleReplacementCompletion::Failed:
			break;
		}
		return result.exitCode;
	}

	std::wstring firstArg = argv[1];
	std::transform(firstArg.begin(), firstArg.end(), firstArg.begin(), ::towlower);
	const std::wstring resetApprovalPrefix = L"/resetapprovals:";
	const bool resetApprovalWithUser = firstArg.starts_with(resetApprovalPrefix) &&
		firstArg.length() > resetApprovalPrefix.length();
	std::wstring resetApprovalUser = resetApprovalWithUser
		? std::wstring(argv[1]).substr(resetApprovalPrefix.length())
		: L"";
	if (firstArg == resetApprovalPrefix)
	{
		ReportError(xucrun::localization::Format(
			xucrun::localization::TextId::SwitchUsernameRequired,
			{ L"/resetapprovals:" }),
			L"The /resetapprovals switch was specified without a required account name.");
		LocalFree(argv);
		return 1;
	}

	// ==============================================================================
	// IMPORTANT: Remove a leading user switch from argv IMMEDIATELY
	// ==============================================================================
	// The user switch must be removed BEFORE any further processing so that
	// programPath = argv[1] later receives the ACTUAL program path!
	// ==============================================================================
	std::wstring specificUserParam;
	// Recognize it only as the first XUCRun argument so target program arguments
	// and switches of nested XUCRun calls remain untouched.
	if (argc >= 2)
	{
		const std::wstring firstParameter = argv[1];
		const auto userSwitch = xucrun::command_line::ParseUserSwitch(firstParameter);
		if (userSwitch.kind == xucrun::command_line::UserSwitchKind::Invalid)
		{
			ReportError(xucrun::localization::Format(
				xucrun::localization::TextId::SwitchUsernameRequired,
				{ firstParameter }),
				L"An invalid user switch without a required account name was specified: " +
					firstParameter);
			LocalFree(argv);
			return 1;
		}
		if (userSwitch.kind != xucrun::command_line::UserSwitchKind::None)
		{
			requestElevation =
				userSwitch.kind == xucrun::command_line::UserSwitchKind::SelectElevatedCredential ||
				userSwitch.kind == xucrun::command_line::UserSwitchKind::NamedElevatedUser;
			selectCredentialWithoutElevation =
				userSwitch.kind == xucrun::command_line::UserSwitchKind::SelectCredential;
			specificUserParam = userSwitch.username;
			for (int index = 1; index < argc - 1; index++)
			{
				argv[index] = argv[index + 1];
			}
			argc--;
			WriteLog(L"Info", selectCredentialWithoutElevation
				? L"/user without username found - select stored account without additional elevation"
				: requestElevation
				? (specificUserParam.empty()
					? L"User parameter with elevation found without username"
					: L"User parameter with elevation found: " + specificUserParam)
				: L"User parameter without additional elevation found: " + specificUserParam);
			firstArg = argv[1];
			std::transform(firstArg.begin(), firstArg.end(), firstArg.begin(), ::towlower);
		}
	}

	if (specificUserParam.empty() && (firstArg == L"/addcred" || firstArg == L"/deletecred"))
	{
		specificUserParam = ExtractUserParameter(argc, argv);
	}

	if (!specificUserParam.empty() && (firstArg == L"/addcred" || firstArg == L"/deletecred"))
	{
		argc = RemoveUserParameterFromArgv(argc, argv);
		WriteLog(L"Info", L"argc after /user removal: " + std::to_wstring(argc));

		// firstArg must be reset if /user: was in the first position
		if (argc >= 2)
		{
			firstArg = argv[1];
			std::transform(firstArg.begin(), firstArg.end(), firstArg.begin(), ::towlower);
		}
	}

	// Only argv[1] belongs to the XUCRun switch namespace. After an optional user
	// switch, argv[1] already points to the target program. If this argument still
	// begins with "/" or "--", it is an unknown XUCRun switch.
	const bool looksLikeXUCRunSwitch =
		(!firstArg.empty() && firstArg.front() == L'/') ||
		(firstArg.length() >= 2 && firstArg.substr(0, 2) == L"--");
	const bool isInternalMonitorSwitch = firstArg.starts_with(L"/internal-monitor:");
	bool isInternalBrokerTestSwitch = false;
#ifdef _DEBUG
	isInternalBrokerTestSwitch =
		firstArg == L"/internal-broker-save-gate-test" ||
		firstArg == L"/internal-broker-save-test" ||
		firstArg == L"/internal-broker-production-launch-test" ||
		firstArg == L"/internal-broker-production-challenge-test" ||
		firstArg == L"/internal-broker-delete-test" ||
		firstArg == L"/internal-broker-challenge-test" ||
		firstArg == L"/internal-broker-challenge-expiry-test" ||
		firstArg == L"/internal-broker-approval-reset-gate-test" ||
		firstArg == L"/internal-broker-approval-list-test" ||
		firstArg == L"/internal-broker-credential-delete-gate-test" ||
		firstArg == L"/internal-broker-credential-use-gate-test" ||
		firstArg == L"/internal-broker-credential-list-test" ||
		firstArg == L"/internal-broker-security-audit-test" ||
		firstArg == L"/internal-broker-tracked-process-registration-gate-test" ||
		firstArg == L"/internal-broker-tracked-process-list-test";
#endif
	std::wstring languageCommandLanguage;
	std::wstring languagePipeToken;
	const bool isInternalLanguageCommand =
		xucrun::broker::ParseSetLanguageCommandArgument(
			firstArg, languageCommandLanguage, languagePipeToken);
	const bool isKnownXUCRunSwitch =
		firstArg == L"/internal-elevate" ||
		firstArg == L"/internal-admin-status-view" ||
		firstArg == L"/internal-backup-view" ||
		firstArg == L"/internal-backup-restore-view" ||
		firstArg == xucrun::broker::SetLanguageGermanArgumentTail ||
		firstArg == xucrun::broker::SetLanguageEnglishArgumentTail ||
		firstArg == xucrun::broker::SetLanguageAutomaticArgumentTail ||
		firstArg == xucrun::broker::RequestLanguageGermanArgumentTail ||
		firstArg == xucrun::broker::RequestLanguageEnglishArgumentTail ||
		firstArg == xucrun::broker::RequestLanguageAutomaticArgumentTail ||
		isInternalLanguageCommand ||
		firstArg.starts_with(
			xucrun::broker::AdministrativeInventoryConsoleArgumentPrefix) ||
		firstArg.starts_with(
			xucrun::broker::AdministrativeBackupCommandArgumentPrefix) ||
		isInternalMonitorSwitch ||
		isInternalBrokerTestSwitch ||
		firstArg == L"/help" || firstArg == L"--help" || firstArg == L"/?" ||
		firstArg == L"/info" ||
		firstArg == L"/system" ||
		firstArg == L"/admin-status" ||
		firstArg == L"/backup" || firstArg == L"/backup-create" ||
		firstArg == L"/backup-list" || firstArg == L"/backup-delete-all" ||
		firstArg == L"/backup-restore" ||
		firstArg.starts_with(L"/backup-restore:") ||
		firstArg == L"/addcred" || firstArg == L"/deletecred" || firstArg == L"/showcred" ||
		firstArg == L"/showapprovals" ||
		firstArg == L"/resetapprovals" || resetApprovalWithUser ||
		firstArg == L"/clear-all" || firstArg == L"/audit";
	if (looksLikeXUCRunSwitch && !isKnownXUCRunSwitch)
	{
		ReportError(xucrun::localization::Format(
			xucrun::localization::TextId::UnknownSwitch,
			{ argv[1], PROGRAM_EXE_NAME }),
			L"An unknown xucRun switch was specified: " + std::wstring(argv[1]));
		LocalFree(argv);
		return 1;
	}
	if (isInternalMonitorSwitch)
	{
		const std::wstring programName = argc >= 3 ? argv[2] :
			xucrun::localization::GetText(
				xucrun::localization::TextId::UnknownProgramName);
		const int result = HiddenProcessMonitorMode(argv[1], programName);
		LocalFree(argv);
		return result;
	}

	// /internal-elevate (internal call for two-stage elevation)
	if (firstArg == L"/internal-elevate")
	{
		bool result = ElevatedWrapperMode(argc, argv);
		LocalFree(argv);
		WriteLog(result ? L"Info" : L"Error",
			result ? L"Elevated launch wrapper finished. Status: 0"
				: L"Elevated launch wrapper finished. Status: 1");
		return result ? 0 : 1;
	}

	if (firstArg == L"/system")
	{
		const int result = RunSystemProgramLaunch(argc, argv);
		LocalFree(argv);
		return result;
	}

	if (firstArg == L"/clear-all")
	{
		if (argc != 2)
		{
			ReportError(xucrun::localization::GetText(
				xucrun::localization::TextId::PurgeArgumentsInvalid),
				L"The /clear-all command was specified with invalid additional arguments.");
			LocalFree(argv);
			return 1;
		}
		WriteLog(L"Warning", L"=== /clear-all: full broker store purge requested ===");
		const int result = xucrun::broker_status::PurgeBrokerStore() ==
			xucrun::broker_status::BrokerStorePurgeResult::Succeeded ? 0 : 1;
		LocalFree(argv);
		WriteLog(result == 0 ? L"Info" : L"Warning",
			L"Program exit (/clear-all)");
		return result;
	}

#ifdef _DEBUG
	if (const auto internalBrokerTestResult =
		xucrun::internal_broker_tests::HandleInternalBrokerTestCommand(
			firstArg, argc, argv);
		internalBrokerTestResult.has_value())
	{
		const int result = *internalBrokerTestResult;
		LocalFree(argv);
		return result;
	}
#endif

	// /help or --help
	if (firstArg == L"/help" || firstArg == L"--help" || firstArg == L"/?")
	{
		ShowHelp();
		LocalFree(argv);
		WriteLog(L"Info", L"Program exit (help)");
		return 0;
	}

	if (const auto administrativeCommandResult =
		xucrun::administrative_commands::HandleAdministrativeCommand(
			firstArg, argc, argv, specificUserParam, resetApprovalWithUser,
			resetApprovalUser, PROGRAM_EXE_NAME);
		administrativeCommandResult.has_value())
	{
		const int result = *administrativeCommandResult;
		LocalFree(argv);
		return result;
	}

	const int result = RunNormalProgramLaunch(argc, argv, specificUserParam,
		requestElevation, selectCredentialWithoutElevation,
		hiddenLaunch, monitorHiddenLaunch);
	LocalFree(argv);
	return result;
}
