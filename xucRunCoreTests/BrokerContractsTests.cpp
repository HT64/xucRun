// Purpose: Tests contracts, serialization, identity helpers, and sensitive storage primitives.
// Runs positive round trips and targeted rejection cases as a release test program.
#include "ApprovalSerialization.h"
#include "BrokerBackupArchive.h"
#include "BrokerContracts.h"
#include "BrokerSerialization.h"
#include "ClientCommandLine.h"
#include "ClientProcessContext.h"
#include "FileIdentity.h"
#include "SecureDesktopHost.h"
#include "SensitiveString.h"
#include "TargetNaming.h"

#include <Windows.h>

#include <fstream>
#include <iostream>

using xucrun::broker::ContractValidationError;
using xucrun::broker::LaunchRequest;
using xucrun::broker::ValidateLaunchRequest;

namespace
{
	int failures = 0;

	void ExpectBrokerBackupArchiveSerialization()
	{
		using namespace xucrun::backup;
		if (GetArchiveVersionDirectoryName() != L"v3-a8")
		{
			std::cerr << "broker backup version directory mismatch\n";
			failures++;
		}
		Archive archive;
		archive.createdAt = 0x0123456789ABCDEFULL;
		archive.description = L"Vor Änderung der Netzwerkeinstellungen";
		archive.machineBinding.fill(0x5a);
		archive.credentials.push_back({ EntryKind::Credential,
			L"123-target", { 1, 2, 3, 4 } });
		archive.credentials.push_back({ EntryKind::Approval,
			L"456-target", { 5, 6, 7 } });
		archive.credentials.push_back({ EntryKind::AuthenticityMarker,
			L"789-target", { 8, 9 } });
		archive.metadata.push_back({ std::wstring(64, L'A') + L".meta",
			{ 10, 11, 12 } });
		const auto bytes = Serialize(archive);
		Archive parsed;
		if (bytes.empty() || !Deserialize(bytes, parsed) ||
			parsed.createdAt != archive.createdAt ||
			parsed.description != archive.description ||
			parsed.machineBinding != archive.machineBinding ||
			parsed.credentials.size() != 3 || parsed.metadata.size() != 1 ||
			parsed.credentials[1].kind != EntryKind::Approval ||
			parsed.credentials[1].target != L"456-target" ||
			parsed.credentials[1].protectedBlob != std::vector<std::uint8_t>({ 5, 6, 7 }) ||
			parsed.metadata[0].contents != std::vector<std::uint8_t>({ 10, 11, 12 }))
		{
			std::cerr << "broker backup archive round trip failed\n";
			failures++;
		}

		auto invalid = bytes;
		invalid.push_back(0);
		if (Deserialize(invalid, parsed))
		{
			std::cerr << "trailing broker backup archive data accepted\n";
			failures++;
		}
		invalid = bytes;
		invalid[8] = 99;
		if (Deserialize(invalid, parsed))
		{
			std::cerr << "unknown broker backup archive version accepted\n";
			failures++;
		}
		invalid = bytes;
		invalid.pop_back();
		if (Deserialize(invalid, parsed))
		{
			std::cerr << "truncated broker backup archive accepted\n";
			failures++;
		}
		archive.credentials.push_back(archive.credentials.front());
		if (!Serialize(archive).empty())
		{
			std::cerr << "duplicate broker backup target accepted\n";
			failures++;
		}
		archive.credentials.pop_back();
		archive.credentials.front().kind = static_cast<EntryKind>(99);
		if (!Serialize(archive).empty())
		{
			std::cerr << "unknown broker backup entry kind accepted\n";
			failures++;
		}
		archive.credentials.front().kind = EntryKind::Credential;
		archive.credentials.front().target = std::wstring(L"valid\0invalid", 13);
		if (!Serialize(archive).empty())
		{
			std::cerr << "embedded null in broker backup target accepted\n";
			failures++;
		}
		archive.credentials.front().target = L"123-target";
		archive.description = std::wstring(MaximumDescriptionLength + 1, L'A');
		if (!Serialize(archive).empty())
		{
			std::cerr << "oversized broker backup description accepted\n";
			failures++;
		}
		archive.description = std::wstring(L"valid\ninvalid");
		if (!Serialize(archive).empty())
		{
			std::cerr << "control character in broker backup description accepted\n";
			failures++;
		}
		archive.description = L"Vor Änderung der Netzwerkeinstellungen";
		archive.metadata.push_back(archive.metadata.front());
		if (!Serialize(archive).empty())
		{
			std::cerr << "duplicate broker backup metadata name accepted\n";
			failures++;
		}
		archive.metadata.pop_back();
		archive.credentials.front().protectedBlob.assign(
			MaximumCredentialBlobSize + 1, 1);
		if (!Serialize(archive).empty())
		{
			std::cerr << "oversized broker backup credential blob accepted\n";
			failures++;
		}
		archive.credentials.front().protectedBlob = { 1, 2, 3, 4 };
		archive.machineBinding.fill(0);
		if (!Serialize(archive).empty())
		{
			std::cerr << "empty broker backup machine binding accepted\n";
			failures++;
		}
	}

	void ExpectBrokerBackupContractSerialization()
	{
		using namespace xucrun::broker;
		std::wstring pipeToken;
		std::wstring command;
		std::wstring description;
		const std::wstring validToken = L"0123456789abcdef0123456789ABCDEF";
		if (!BuildAdministrativeBackupCreateCommand(
				L"Tägliche Sicherung", command) ||
			!ParseAdministrativeBackupCreateCommand(command, description) ||
			description != L"Tägliche Sicherung" ||
			!IsValidAdministrativeBackupCommand(command) ||
			BuildAdministrativeBackupCreateCommand(std::wstring(257, L'A'), command) ||
			BuildAdministrativeBackupCreateCommand(L"Zeile\nZwei", command) ||
			ParseAdministrativeBackupCreateCommand(L"create:", description) ||
			ParseAdministrativeBackupCreateCommand(L"create:000A", description) ||
			ParseAdministrativeBackupCreateCommand(L"create:00XZ", description) ||
			!IsValidBackupId(L"0123456789ABCDEF0123456789ABCDEF") ||
			IsValidBackupId(L"0123456789ABCDEF0123456789ABCDE") ||
			IsValidBackupId(L"0123456789ABCDEF0123456789ABCDEZ") ||
			!IsValidAdministrativeBackupCommand(L"create") ||
			!IsValidAdministrativeBackupCommand(L"list") ||
			!IsValidAdministrativeBackupCommand(L"delete-all") ||
			!IsValidAdministrativeBackupCommand(
				L"restore:0123456789ABCDEF0123456789ABCDEF") ||
			IsValidAdministrativeBackupCommand(L"restore:") ||
			IsValidAdministrativeBackupCommand(
				L"restore:0123456789ABCDEF0123456789ABCDEZ") ||
			IsValidAdministrativeBackupCommand(L"create-extra") ||
			!ParseAdministrativeBackupCommandArgument(
				std::wstring(AdministrativeBackupCommandArgumentPrefix) +
				validToken + L":list", pipeToken, command) ||
			pipeToken != validToken || command != L"list" ||
			ParseAdministrativeBackupCommandArgument(
				std::wstring(AdministrativeBackupCommandArgumentPrefix) +
				validToken.substr(1) + L":list", pipeToken, command) ||
			ParseAdministrativeBackupCommandArgument(
				std::wstring(AdministrativeBackupCommandArgumentPrefix) +
				validToken + L":list-extra", pipeToken, command))
		{
			std::cerr << "administrative backup command validation failed\n";
			failures++;
		}
		std::wstring language;
		const std::wstring languageArgument =
			std::wstring(SetLanguageGermanArgumentTail) + L":" + validToken;
		if (!ParseSetLanguageCommandArgument(
				languageArgument, language, pipeToken) ||
			language != L"de" || pipeToken != validToken ||
			!ParseSetLanguageCommandArgument(
				std::wstring(SetLanguageEnglishArgumentTail) + L":" + validToken,
				language, pipeToken) ||
			language != L"en" || pipeToken != validToken ||
			ParseSetLanguageCommandArgument(
				SetLanguageGermanArgumentTail, language, pipeToken) ||
			ParseSetLanguageCommandArgument(
				std::wstring(SetLanguageGermanArgumentTail) + L":" +
					validToken.substr(1), language, pipeToken) ||
			ParseSetLanguageCommandArgument(
				L"/internal-set-language:fr:" + validToken,
				language, pipeToken))
		{
			std::cerr << "language change command validation failed\n";
			failures++;
		}
		BackupRequest request;
		request.requestId = 0x1020304050607080ULL;
		request.action = BackupAction::Restore;
		request.createFallbackBackup = true;
		request.backupId = L"0123456789ABCDEF0123456789ABCDEF";
		const auto requestFrame = SerializeBackupRequest(request);
		BackupRequest parsedRequest;
		if (requestFrame.empty() || !DeserializeBackupRequest(requestFrame, parsedRequest) ||
			parsedRequest.requestId != request.requestId ||
			parsedRequest.action != BackupAction::Restore ||
			!parsedRequest.createFallbackBackup ||
			parsedRequest.backupId != request.backupId)
		{
			std::cerr << "broker backup request round trip failed\n";
			failures++;
		}
		auto invalidFallbackFlagFrame = requestFrame;
		invalidFallbackFlagFrame[6] = 2;
		if (DeserializeBackupRequest(invalidFallbackFlagFrame, parsedRequest))
		{
			std::cerr << "invalid broker backup fallback flag accepted\n";
			failures++;
		}
		BackupRequest createRequest;
		createRequest.requestId = 0x8877665544332211ULL;
		createRequest.action = BackupAction::Create;
		createRequest.description = L"Vor Änderung der Netzwerkeinstellungen";
		const auto createRequestFrame = SerializeBackupRequest(createRequest);
		BackupRequest parsedCreateRequest;
		if (createRequestFrame.empty() ||
			!DeserializeBackupRequest(createRequestFrame, parsedCreateRequest) ||
			parsedCreateRequest.description != createRequest.description)
		{
			std::cerr << "broker backup description request round trip failed\n";
			failures++;
		}
		createRequest.description.assign(
			xucrun::backup::MaximumDescriptionLength + 1, L'A');
		if (!SerializeBackupRequest(createRequest).empty())
		{
			std::cerr << "oversized broker backup request description accepted\n";
			failures++;
		}
		request.backupId = L"..\\invalid";
		if (!SerializeBackupRequest(request).empty())
		{
			std::cerr << "invalid broker backup id accepted\n";
			failures++;
		}
		request.action = BackupAction::Delete;
		request.createFallbackBackup = false;
		request.backupId = L"0123456789ABCDEF0123456789ABCDEF";
		const auto deleteRequestFrame = SerializeBackupRequest(request);
		if (deleteRequestFrame.empty() ||
			!DeserializeBackupRequest(deleteRequestFrame, parsedRequest) ||
			parsedRequest.action != BackupAction::Delete ||
			parsedRequest.backupId != request.backupId)
		{
			std::cerr << "broker backup delete request round trip failed\n";
			failures++;
		}
		request.backupId.clear();
		if (!SerializeBackupRequest(request).empty())
		{
			std::cerr << "broker backup delete request without id accepted\n";
			failures++;
		}

		BackupResult result;
		result.requestId = 0x1020304050607080ULL;
		result.status = BackupStatus::Succeeded;
		result.fallbackBackupId = L"FEDCBA9876543210FEDCBA9876543210";
		result.entries.push_back({ L"0123456789ABCDEF0123456789ABCDEF",
			L"Vor Änderung der Netzwerkeinstellungen", 123456789, 2, 3, 4, 5 });
		const auto resultFrame = SerializeBackupResult(result);
		BackupResult parsedResult;
		if (resultFrame.empty() || !DeserializeBackupResult(resultFrame, parsedResult) ||
			parsedResult.fallbackBackupId != result.fallbackBackupId ||
			parsedResult.entries.size() != 1 ||
			parsedResult.entries[0].description != result.entries[0].description ||
			parsedResult.entries[0].approvalCount != 3 ||
			parsedResult.entries[0].metadataCount != 5)
		{
			std::cerr << "broker backup result round trip failed\n";
			failures++;
		}
		auto invalid = resultFrame;
		invalid.push_back(0);
		if (DeserializeBackupResult(invalid, parsedResult))
		{
			std::cerr << "trailing broker backup result data accepted\n";
			failures++;
		}
		result.status = BackupStatus::Failed;
		if (!SerializeBackupResult(result).empty())
		{
			std::cerr << "failed broker backup result with metadata accepted\n";
			failures++;
		}
		result.status = BackupStatus::Succeeded;
		result.entries.clear();
		result.fallbackBackupId = L"fedcba9876543210fedcba9876543210";
		if (!SerializeBackupResult(result).empty())
		{
			std::cerr << "non-canonical broker fallback backup id accepted\n";
			failures++;
		}
		result.fallbackBackupId.clear();
		result.entries.assign(MaximumBackupEntries + 1,
			{ L"0123456789ABCDEF0123456789ABCDEF", L"", 1, 0, 0, 0, 0 });
		if (!SerializeBackupResult(result).empty())
		{
			std::cerr << "oversized broker backup result list accepted\n";
			failures++;
		}
	}

	void ExpectBrokerTimeoutContracts()
	{
		using namespace xucrun::broker;
		if (DefaultBrokerRequestTimeoutMilliseconds != 5000 ||
			BrokerIpcTimeoutReserveMilliseconds != 5000 ||
			ApprovalDialogTimeoutMilliseconds != 60000 ||
			ApprovalHelperDecisionLifetimeMilliseconds != 90000 ||
			ApprovalDialogTimeoutMilliseconds >=
				ApprovalHelperDecisionLifetimeMilliseconds ||
			ElevatedLaunchHelperTimeoutMilliseconds != 30000 ||
			ApprovalChallengeRequestTimeoutMilliseconds !=
				ElevatedLaunchHelperTimeoutMilliseconds + BrokerIpcTimeoutReserveMilliseconds ||
			ApprovalConfirmationRequestTimeoutMilliseconds !=
				ElevatedLaunchHelperTimeoutMilliseconds + BrokerIpcTimeoutReserveMilliseconds ||
			ApprovalHelperHostTimeoutMilliseconds !=
				ApprovalHelperMaximumLifetimeMilliseconds + BrokerIpcTimeoutReserveMilliseconds ||
			ApprovalHelperProcessWaitTimeoutMilliseconds !=
				ApprovalHelperHostTimeoutMilliseconds + BrokerIpcTimeoutReserveMilliseconds)
		{
			std::cerr << "broker timeout contracts failed\n";
			failures++;
		}
	}

	void ExpectClientCommandLine()
	{
		using namespace xucrun::command_line;
		if (EscapeArgument(L"") != L"\"\"" ||
			EscapeArgument(L"plain") != L"plain" ||
			EscapeArgument(L"two words") != L"\"two words\"" ||
			EscapeArgument(L"a\"b") != L"\"a\\\"b\"" ||
			EscapeArgument(L"&&") != L"&&" ||
			EscapeArgument(L"\"already quoted\"") != L"\"already quoted\"" ||
			!IsAbsolutePath(L"C:\\work") || !IsAbsolutePath(L"\\\\server\\share") ||
			IsAbsolutePath(L"relative\\file"))
		{
			std::cerr << "client command line escaping failed\n";
			failures++;
		}

		wchar_t executable[] = L"xucRun.exe";
		wchar_t joinedOperator[] = L"PN=test.bat&";
		wchar_t next[] = L"next";
		wchar_t* buildArguments[] = { executable, joinedOperator, next };
		if (Build(3, buildArguments, 1) != L"PN=test.bat & next")
		{
			std::cerr << "client command line operator normalization failed\n";
			failures++;
		}

		const std::wstring preservedCmdSwitches = InjectShellWorkingDirectory(
			L"C:\\Windows\\System32\\cmd.exe",
			L"/D /C exit /b 0 /K value", L"D:\\Users Common");
		if (preservedCmdSwitches !=
			L"/D /C \"cd /d \"D:\\Users Common\" && exit /b 0 /K value\"")
		{
			std::wcerr << L"cmd switch preservation failed: " <<
				preservedCmdSwitches << L"\n";
			failures++;
		}

		if (InjectShellWorkingDirectory(
				L"C:\\Windows\\System32\\cmd.exe",
				L"/v:on /c SET P=right & SET R=right & SET Active=L & "
				L"(IF !P! == !R! SET Active=R) & xucRun.exe /P=!Active!",
				L"Z:\\Work") !=
			L"/v:on /c \"cd /d Z:\\Work && SET P=right & SET R=right & "
			L"SET Active=L & (IF !P! == !R! SET Active=R) & "
			L"xucRun.exe /P=!Active!\"" ||
			InjectShellWorkingDirectory(
				L"cmd.exe",
				L"/V:ON /D /Q /K \"Z:\\Temp\\xucRun_wrapper_0123456789abcdef.bat\" alpha",
				L"Z:\\2 & & !! (# )% #-\\$ !! !!! &&") !=
			L"/V:ON /D /Q /K \"Z:\\Temp\\xucRun_wrapper_0123456789abcdef.bat\" alpha" ||
			InjectShellWorkingDirectory(
				L"cmd.exe", L"/d /c \"exit /b 7\"", L"") !=
			L"/d /c \"exit /b 7\"" ||
			InjectShellWorkingDirectory(
				L"cmd.exe",
				L"/c \"C:\\Program Files\\Notepad++\\notepad++.exe\"",
				L"C:\\Users\\Harald\\AppData\\Local\\Temp") !=
			L"/c \"cd /d C:\\Users\\Harald\\AppData\\Local\\Temp && "
			L"\"C:\\Program Files\\Notepad++\\notepad++.exe\"\"" ||
			InjectShellWorkingDirectory(
				L"cmd.exe", L"/q", L"C:\\Temp") != L"/q")
		{
			std::cerr << "client shell working directory injection failed\n";
			failures++;
		}

		const RawShellArguments rawCmd = ExtractRawShellArguments(
			L"cmd.exe",
			L"\"C:\\Program Files\\xucRun\\xucRun.exe\" cmd.exe /c \"echo A & echo B\"");
		const RawShellArguments quotedCmd = ExtractRawShellArguments(
			L"C:\\Windows\\System32\\cmd.exe",
			L"\"C:\\Program Files\\xucRun\\xucRun.exe\" "
			L"\"C:\\Windows\\System32\\cmd.exe\" /k dir");
		const RawShellArguments userTcc = ExtractRawShellArguments(
			L"tcc.exe",
			L"xucRun.exe /user:Admin tcc.exe /c echo test");
		if (!rawCmd.isShell || !rawCmd.extracted || rawCmd.shellType != L"cmd" ||
			rawCmd.arguments != L"/c \"echo A & echo B\"" ||
			!quotedCmd.isShell || !quotedCmd.extracted ||
			quotedCmd.arguments != L"/k dir" ||
			!userTcc.isShell || !userTcc.extracted || userTcc.shellType != L"tcc" ||
			userTcc.arguments != L"/c echo test" ||
			ExtractRawShellArguments(L"notepad.exe", L"xucRun.exe notepad.exe").isShell)
		{
			std::cerr << "client raw shell argument extraction failed\n";
			failures++;
		}

		wchar_t firstUser[] = L"/UsEr:Administrator";
		wchar_t nestedExecutable[] = L"xucRun.exe";
		wchar_t secondUser[] = L"/user:Karin";
		wchar_t* userArguments[] = {
			executable, firstUser, nestedExecutable, secondUser, next
		};
		const UserSwitch selectUser = ParseUserSwitch(L"/UsEr");
		const UserSwitch selectElevated = ParseUserSwitch(L"/USER-ELEVATED:");
		const UserSwitch namedUser = ParseUserSwitch(L"/user:  DOMAIN\\Karin\t");
		const UserSwitch namedElevated = ParseUserSwitch(
			L"/user-elevated: Administrator ");
		if (ExtractUserParameter(5, userArguments) != L"Administrator" ||
			RemoveFirstUserParameter(5, userArguments) != 4 ||
			std::wstring(userArguments[1]) != L"xucRun.exe" ||
			std::wstring(userArguments[2]) != L"/user:Karin" ||
			selectUser.kind != UserSwitchKind::SelectCredential ||
			selectElevated.kind != UserSwitchKind::SelectElevatedCredential ||
			namedUser.kind != UserSwitchKind::NamedUser ||
			namedUser.username != L"DOMAIN\\Karin" ||
			namedElevated.kind != UserSwitchKind::NamedElevatedUser ||
			namedElevated.username != L"Administrator" ||
			ParseUserSwitch(L"/user:").kind != UserSwitchKind::Invalid ||
			ParseUserSwitch(L"/user-elevated: \t").kind != UserSwitchKind::Invalid ||
			ParseUserSwitch(L"/user_elevated:Administrator").kind != UserSwitchKind::None ||
			ParseUserSwitch(L"/userland").kind != UserSwitchKind::None)
		{
			std::cerr << "client /user argument handling failed\n";
			failures++;
		}
	}

	void ExpectClientProcessContext()
	{
		using namespace xucrun::process_context;
		if (DetectParentShellType(L"CMD.EXE") != ParentShellType::Cmd ||
			DetectParentShellType(L"powershell.exe") != ParentShellType::PowerShell ||
			DetectParentShellType(L"PwSh.ExE") != ParentShellType::PowerShellCore ||
			DetectParentShellType(L"tcmd.exe") != ParentShellType::TCC ||
			DetectParentShellType(L"explorer.exe") != ParentShellType::Unknown ||
			ParentShellTypeToString(ParentShellType::Cmd) != L"Command Prompt (cmd.exe)" ||
			ParentShellTypeToString(ParentShellType::Unknown) != L"Unknown")
		{
			std::cerr << "client process context classification failed\n";
			failures++;
		}
	}

	// Verifies that a launch request produces exactly the expected validation error.
	void ExpectValidation(const char* name, const LaunchRequest& request,
		ContractValidationError expected)
	{
		const ContractValidationError actual = ValidateLaunchRequest(request);
		if (actual == expected)
			return;

		std::cerr << name << " failed: expected " << static_cast<int>(expected)
			<< ", got " << static_cast<int>(actual) << '\n';
		failures++;
	}

	// Creates a fully valid reference request for targeted negative tests.
	LaunchRequest MakeValidRequest()
	{
		LaunchRequest request;
		request.requestId = 1;
		request.requestedAccountSid = L"S-1-5-21-1-2-3-1001";
		request.executablePath = L"C:\\Windows\\System32\\notepad.exe";
		request.workingDirectory = L"C:\\Windows";
		return request;
	}

	// Verifies that all inputs are copied unchanged into a launch request.
	void ExpectLaunchRequestConstruction()
	{
		const LaunchRequest request = xucrun::broker::BuildLaunchRequest(
			42,
			L"S-1-5-21-1-2-3-1001",
			L"C:\\Windows\\System32\\cmd.exe",
			L"/c whoami",
			L"C:\\Windows",
			xucrun::broker::ElevationMode::RequireElevated);
		if (request.protocolVersion != xucrun::broker::ProtocolVersion ||
			request.requestId != 42 ||
			request.requestedAccountSid != L"S-1-5-21-1-2-3-1001" ||
			request.executablePath != L"C:\\Windows\\System32\\cmd.exe" ||
			request.argumentTail != L"/c whoami" ||
			request.workingDirectory != L"C:\\Windows" ||
			request.elevationMode != xucrun::broker::ElevationMode::RequireElevated)
		{
			std::cerr << "launch request construction failed\n";
			failures++;
		}

		LaunchRequest invalid = request;
		invalid.elevationMode = static_cast<xucrun::broker::ElevationMode>(0xFF);
		ExpectValidation("unknown elevation mode", invalid,
			ContractValidationError::InvalidElevationMode);
	}

	// Tests the canonical round trip and rejection of invalid launch request frames.
	void ExpectLaunchRequestSerialization()
	{
		LaunchRequest expected = xucrun::broker::BuildLaunchRequest(
			42, L"S-1-5-21-1-2-3-1001", L"C:\\Tools\\ä.exe", L"--name \"Test\"",
			L"C:\\Work", xucrun::broker::ElevationMode::RequireElevated);
		const std::vector<std::uint8_t> frame = xucrun::broker::SerializeLaunchRequest(expected);
		LaunchRequest actual;
		if (frame.size() <= xucrun::broker::LaunchRequestHeaderSize ||
			!xucrun::broker::DeserializeLaunchRequest(frame, actual) ||
			actual.protocolVersion != expected.protocolVersion ||
			actual.requestId != expected.requestId ||
			actual.requestedAccountSid != expected.requestedAccountSid ||
			actual.executablePath != expected.executablePath ||
			actual.argumentTail != expected.argumentTail ||
			actual.workingDirectory != expected.workingDirectory ||
			actual.elevationMode != expected.elevationMode)
		{
			std::cerr << "launch request serialization failed\n";
			failures++;
		}

		for (std::size_t size = 0; size < frame.size(); size++)
		{
			if (xucrun::broker::DeserializeLaunchRequest(
				std::span<const std::uint8_t>(frame.data(), size), actual))
			{
				std::cerr << "truncated launch request accepted\n";
				failures++;
				break;
			}
		}

		std::vector<std::uint8_t> invalid = frame;
		invalid.push_back(0);
		if (xucrun::broker::DeserializeLaunchRequest(invalid, actual))
		{
			std::cerr << "trailing launch request data accepted\n";
			failures++;
		}
		invalid = frame;
		invalid[5] = 0xFF;
		if (xucrun::broker::DeserializeLaunchRequest(invalid, actual))
		{
			std::cerr << "unknown elevation mode accepted\n";
			failures++;
		}
		expected.requestedAccountSid.assign(xucrun::broker::MaximumAccountSidLength + 1, L'x');
		if (!xucrun::broker::SerializeLaunchRequest(expected).empty())
		{
			std::cerr << "oversized account SID serialized\n";
			failures++;
		}

		expected = xucrun::broker::BuildLaunchRequest(
			0x0102030405060708ULL, L"S", L"E", L"", L"W",
			xucrun::broker::ElevationMode::RequireElevated);
		const std::vector<std::uint8_t> expectedFrame = {
			'X', 'U', 'C', 'R', 1, 1, 0, 0,
			38, 0, 0, 0, 8, 7, 6, 5, 4, 3, 2, 1,
			1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0,
			'S', 0, 'E', 0, 'W', 0
		};
		if (xucrun::broker::SerializeLaunchRequest(expected) != expectedFrame)
		{
			std::cerr << "launch request wire format changed\n";
			failures++;
		}
	}

	// Tests the canonical round trip and rejection of invalid launch result frames.
	void ExpectLaunchResultSerialization()
	{
		xucrun::broker::LaunchResult expected;
		expected.requestId = 42;
		expected.status = xucrun::broker::LaunchStatus::Failed;
		expected.windowsError = ERROR_ACCESS_DENIED;
		const std::vector<std::uint8_t> frame =
			xucrun::broker::SerializeLaunchResult(expected);
		xucrun::broker::LaunchResult actual;
		if (frame.size() != xucrun::broker::LaunchResultSize ||
			!xucrun::broker::DeserializeLaunchResult(frame, actual) ||
			actual.protocolVersion != expected.protocolVersion ||
			actual.requestId != expected.requestId ||
			actual.status != expected.status ||
			actual.windowsError != expected.windowsError)
		{
			std::cerr << "launch result serialization failed\n";
			failures++;
		}

		for (std::size_t size = 0; size < frame.size(); size++)
		{
			if (xucrun::broker::DeserializeLaunchResult(
				std::span<const std::uint8_t>(frame.data(), size), actual))
			{
				std::cerr << "truncated launch result accepted\n";
				failures++;
				break;
			}
		}

		std::vector<std::uint8_t> invalid = frame;
		invalid[5] = 0xFF;
		if (xucrun::broker::DeserializeLaunchResult(invalid, actual))
		{
			std::cerr << "unknown launch status accepted\n";
			failures++;
		}
		expected.requestId = 0x0102030405060708ULL;
		expected.status = xucrun::broker::LaunchStatus::Failed;
		expected.windowsError = 5;
		const std::vector<std::uint8_t> expectedFrame = {
			'X', 'U', 'C', 'S', 2, 5, 0, 0,
			38, 0, 0, 0, 8, 7, 6, 5, 4, 3, 2, 1,
			5, 0, 0, 0
		};
		if (xucrun::broker::SerializeLaunchResult(expected) != expectedFrame)
		{
			std::cerr << "launch result wire format changed\n";
			failures++;
		}
	}

	// Tests fixed frame sizes and identifier and nonce fidelity for all health messages.
	void ExpectHealthSerialization()
	{
		xucrun::broker::HealthRequest request;
		request.requestId = 0x0102030405060708ULL;
		for (std::size_t index = 0; index < request.nonce.size(); index++)
			request.nonce[index] = static_cast<std::uint8_t>(index + 1);
		const std::vector<std::uint8_t> requestFrame =
			xucrun::broker::SerializeHealthRequest(request);
		xucrun::broker::HealthRequest parsedRequest;
		if (requestFrame.size() != xucrun::broker::HealthRequestSize ||
			!xucrun::broker::DeserializeHealthRequest(requestFrame, parsedRequest) ||
			parsedRequest.requestId != request.requestId || parsedRequest.nonce != request.nonce)
		{
			std::cerr << "health request serialization failed\n";
			failures++;
		}

		xucrun::broker::HealthResult result;
		result.requestId = request.requestId;
		result.nonce = request.nonce;
		result.brokerProcessId = 1234;
		result.elevatedProcessCount = 2;
		result.brokerVersion = { 2, 3, 4, 567 };
		const std::vector<std::uint8_t> resultFrame =
			xucrun::broker::SerializeHealthResult(result);
		xucrun::broker::HealthResult parsedResult;
		if (resultFrame.size() != xucrun::broker::HealthResultSize ||
			!xucrun::broker::DeserializeHealthResult(resultFrame, parsedResult) ||
			parsedResult.requestId != result.requestId || parsedResult.nonce != result.nonce ||
			parsedResult.brokerProcessId != result.brokerProcessId ||
			parsedResult.elevatedProcessCount != result.elevatedProcessCount ||
			parsedResult.brokerVersion.major != result.brokerVersion.major ||
			parsedResult.brokerVersion.minor != result.brokerVersion.minor ||
			parsedResult.brokerVersion.patch != result.brokerVersion.patch ||
			parsedResult.brokerVersion.build != result.brokerVersion.build)
		{
			std::cerr << "health result serialization failed\n";
			failures++;
		}

		request.nonce = {};
		if (!xucrun::broker::SerializeHealthRequest(request).empty())
		{
			std::cerr << "zero health nonce accepted\n";
			failures++;
		}

		xucrun::broker::HealthAcknowledgement acknowledgement;
		acknowledgement.requestId = result.requestId;
		acknowledgement.nonce = result.nonce;
		const std::vector<std::uint8_t> acknowledgementFrame =
			xucrun::broker::SerializeHealthAcknowledgement(acknowledgement);
		xucrun::broker::HealthAcknowledgement parsedAcknowledgement;
		if (acknowledgementFrame.size() != xucrun::broker::HealthAcknowledgementSize ||
			!xucrun::broker::DeserializeHealthAcknowledgement(
				acknowledgementFrame, parsedAcknowledgement) ||
			parsedAcknowledgement.requestId != acknowledgement.requestId ||
			parsedAcknowledgement.nonce != acknowledgement.nonce)
		{
			std::cerr << "health acknowledgement serialization failed\n";
			failures++;
		}
	}

	// Tests the round trip and complete validation of metadata messages.
	void ExpectCredentialMetadataSerialization()
	{
		xucrun::broker::CredentialMetadataRequest request;
		request.requestId = 0x0102030405060708ULL;
		request.requestedAccountSid = L"S-1";
		const std::vector<std::uint8_t> requestFrame =
			xucrun::broker::SerializeCredentialMetadataRequest(request);
		xucrun::broker::CredentialMetadataRequest parsedRequest;
		const std::vector<std::uint8_t> expectedRequestFrame = {
			'X', 'U', 'C', 'M', 6, 0, 0, 0,
			38, 0, 0, 0, 8, 7, 6, 5, 4, 3, 2, 1,
			3, 0, 0, 0, 'S', 0, '-', 0, '1', 0
		};
		if (requestFrame != expectedRequestFrame ||
			!xucrun::broker::DeserializeCredentialMetadataRequest(
				requestFrame, parsedRequest) ||
			parsedRequest.requestId != request.requestId ||
			parsedRequest.requestedAccountSid != request.requestedAccountSid)
		{
			std::cerr << "credential metadata request serialization failed\n";
			failures++;
		}
		for (std::size_t size = 0; size < requestFrame.size(); size++)
		{
			if (xucrun::broker::DeserializeCredentialMetadataRequest(
				std::span<const std::uint8_t>(requestFrame.data(), size), parsedRequest))
			{
				std::cerr << "truncated credential metadata request accepted\n";
				failures++;
				break;
			}
		}

		xucrun::broker::CredentialMetadataResult result;
		result.requestId = request.requestId;
		result.status = xucrun::broker::CredentialMetadataStatus::InvalidRequest;
		result.windowsError = ERROR_INVALID_SID;
		const std::vector<std::uint8_t> resultFrame =
			xucrun::broker::SerializeCredentialMetadataResult(result);
		xucrun::broker::CredentialMetadataResult parsedResult;
		if (resultFrame.size() != xucrun::broker::CredentialMetadataResultSize ||
			!xucrun::broker::DeserializeCredentialMetadataResult(resultFrame, parsedResult) ||
			parsedResult.requestId != result.requestId ||
			parsedResult.status != result.status ||
			parsedResult.windowsError != result.windowsError)
		{
			std::cerr << "credential metadata result serialization failed\n";
			failures++;
		}
		std::vector<std::uint8_t> invalidResult = resultFrame;
		invalidResult[5] = 0xFF;
		if (xucrun::broker::DeserializeCredentialMetadataResult(
			invalidResult, parsedResult))
		{
			std::cerr << "unknown credential metadata status accepted\n";
			failures++;
		}
	}

	// Tests the round trip and complete validation of session test messages.
	void ExpectSessionTestSerialization()
	{
		xucrun::broker::SessionTestRequest request;
		request.requestId = 0x0102030405060708ULL;
		const std::vector<std::uint8_t> requestFrame =
			xucrun::broker::SerializeSessionTestRequest(request);
		const std::vector<std::uint8_t> expectedRequestFrame = {
			'X', 'U', 'C', 'T', 8, 0, 0, 0,
			38, 0, 0, 0, 8, 7, 6, 5, 4, 3, 2, 1
		};
		xucrun::broker::SessionTestRequest parsedRequest;
		if (requestFrame != expectedRequestFrame ||
			!xucrun::broker::DeserializeSessionTestRequest(requestFrame, parsedRequest) ||
			parsedRequest.requestId != request.requestId)
		{
			std::cerr << "session test request serialization failed\n";
			failures++;
		}

		xucrun::broker::SessionTestResult result;
		result.requestId = request.requestId;
		result.status = xucrun::broker::SessionTestStatus::Succeeded;
		result.processId = 0x11223344;
		const std::vector<std::uint8_t> resultFrame =
			xucrun::broker::SerializeSessionTestResult(result);
		const std::vector<std::uint8_t> expectedResultFrame = {
			'X', 'U', 'C', 'U', 9, 0, 0, 0,
			38, 0, 0, 0, 8, 7, 6, 5, 4, 3, 2, 1,
			0, 0, 0, 0, 0x44, 0x33, 0x22, 0x11
		};
		xucrun::broker::SessionTestResult parsedResult;
		if (resultFrame != expectedResultFrame ||
			!xucrun::broker::DeserializeSessionTestResult(resultFrame, parsedResult) ||
			parsedResult.requestId != result.requestId ||
			parsedResult.status != result.status ||
			parsedResult.processId != result.processId)
		{
			std::cerr << "session test result serialization failed\n";
			failures++;
		}
		result.processId = 0;
		if (!xucrun::broker::SerializeSessionTestResult(result).empty())
		{
			std::cerr << "successful session test without process accepted\n";
			failures++;
		}
	}

	// Tests the round trip and complete validation of credential store test messages.
	void ExpectCredentialStoreTestSerialization()
	{
		xucrun::broker::CredentialStoreTestRequest request;
		request.requestId = 0x0102030405060708ULL;
		const std::vector<std::uint8_t> requestFrame =
			xucrun::broker::SerializeCredentialStoreTestRequest(request);
		const std::vector<std::uint8_t> expectedRequestFrame = {
			'X', 'U', 'C', 'V', 10, 0, 0, 0,
			38, 0, 0, 0, 8, 7, 6, 5, 4, 3, 2, 1
		};
		xucrun::broker::CredentialStoreTestRequest parsedRequest;
		if (requestFrame != expectedRequestFrame ||
			!xucrun::broker::DeserializeCredentialStoreTestRequest(
				requestFrame, parsedRequest) ||
			parsedRequest.requestId != request.requestId)
		{
			std::cerr << "credential store test request serialization failed\n";
			failures++;
		}

		xucrun::broker::CredentialStoreTestResult result;
		result.requestId = request.requestId;
		result.status = xucrun::broker::CredentialStoreTestStatus::Failed;
		result.windowsError = ERROR_INVALID_DATA;
		const std::vector<std::uint8_t> resultFrame =
			xucrun::broker::SerializeCredentialStoreTestResult(result);
		const std::vector<std::uint8_t> expectedResultFrame = {
			'X', 'U', 'C', 'W', 11, 1, 0, 0,
			38, 0, 0, 0, 8, 7, 6, 5, 4, 3, 2, 1,
			13, 0, 0, 0
		};
		xucrun::broker::CredentialStoreTestResult parsedResult;
		if (resultFrame != expectedResultFrame ||
			!xucrun::broker::DeserializeCredentialStoreTestResult(
				resultFrame, parsedResult) ||
			parsedResult.requestId != result.requestId ||
			parsedResult.status != result.status ||
			parsedResult.windowsError != result.windowsError)
		{
			std::cerr << "credential store test result serialization failed\n";
			failures++;
		}
	}

	// Tests exclusive ownership, memory locking, and secure state transitions of sensitive strings.
	void ExpectSensitiveString()
	{
		xucrun::SensitiveString value;
		if (!value.assign(L"Kennwort", 8) || value.size() != 8 ||
			wcscmp(value.c_str(), L"Kennwort") != 0 || !value.isMemoryLocked())
		{
			std::cerr << "sensitive string assignment failed\n";
			failures++;
		}
		xucrun::SensitiveString moved(std::move(value));
		if (!value.empty() || moved.size() != 8 ||
			wcscmp(moved.c_str(), L"Kennwort") != 0 || !moved.isMemoryLocked() ||
			value.isMemoryLocked())
		{
			std::cerr << "sensitive string move failed\n";
			failures++;
		}
		const std::vector<std::uint8_t> utf16 = {
			'P', 0, 0xE4, 0, 's', 0, 's', 0
		};
		if (!value.assignUtf16(utf16, 4) || value.size() != 4 ||
			wcscmp(value.c_str(), L"Päss") != 0 || !value.isMemoryLocked())
		{
			std::cerr << "sensitive UTF-16 assignment failed\n";
			failures++;
		}
		const std::vector<std::uint8_t> embeddedNull = { 'A', 0, 0, 0 };
		if (value.assignUtf16(embeddedNull, 2) || !value.empty())
		{
			std::cerr << "embedded null in sensitive string accepted\n";
			failures++;
		}
		if (!value.assign(L"", 0) || !value.isAssigned() || !value.empty() ||
			wcscmp(value.c_str(), L"") != 0 || !value.isMemoryLocked())
		{
			std::cerr << "assigned empty sensitive string rejected\n";
			failures++;
		}
		value.clear();
		if (value.isAssigned() || value.isMemoryLocked())
		{
			std::cerr << "cleared sensitive string remains assigned\n";
			failures++;
		}
	}

	// Tests the round trip without a plaintext copy and rejection of invalid password frames.
	void ExpectCredentialSaveSerialization()
	{
		xucrun::broker::CredentialSaveRequest request;
		request.requestId = 0x0102030405060708ULL;
		request.requestedAccountSid = L"S-1";
		request.password.assign(L"Päss", 4);
		std::vector<std::uint8_t> frame =
			xucrun::broker::SerializeCredentialSaveRequest(request);
		xucrun::broker::CredentialSaveRequest parsed;
		if (frame.size() != xucrun::broker::CredentialSaveRequestHeaderSize + 14 ||
			!xucrun::broker::DeserializeCredentialSaveRequest(frame, parsed) ||
			parsed.requestId != request.requestId ||
			parsed.requestedAccountSid != request.requestedAccountSid ||
			wcscmp(parsed.password.c_str(), L"Päss") != 0)
		{
			std::cerr << "credential save request serialization failed\n";
			failures++;
		}
		for (std::size_t size = 0; size < frame.size(); size++)
		{
			if (xucrun::broker::DeserializeCredentialSaveRequest(
				std::span<const std::uint8_t>(frame.data(), size), parsed))
			{
				std::cerr << "truncated credential save request accepted\n";
				failures++;
				break;
			}
		}
		std::vector<std::uint8_t> invalid = frame;
		invalid.push_back(0);
		if (!xucrun::broker::IsCredentialSaveRequestFrame(invalid) ||
			xucrun::broker::DeserializeCredentialSaveRequest(invalid, parsed))
		{
			std::cerr << "malformed credential save frame not identified or accepted\n";
			failures++;
		}
		invalid = frame;
		invalid[24] = 0x01;
		invalid[25] = 0x01;
		if (xucrun::broker::DeserializeCredentialSaveRequest(invalid, parsed))
		{
			std::cerr << "oversized credential password accepted\n";
			failures++;
		}
		xucrun::broker::CredentialSaveRequest emptyPasswordRequest;
		emptyPasswordRequest.requestId = request.requestId;
		emptyPasswordRequest.requestedAccountSid = request.requestedAccountSid;
		emptyPasswordRequest.password.assign(L"", 0);
		const std::vector<std::uint8_t> emptyPasswordFrame =
			xucrun::broker::SerializeCredentialSaveRequest(emptyPasswordRequest);
		xucrun::broker::CredentialSaveRequest parsedEmptyPasswordRequest;
		if (emptyPasswordFrame.size() !=
			xucrun::broker::CredentialSaveRequestHeaderSize +
				2 * emptyPasswordRequest.requestedAccountSid.size() ||
			!xucrun::broker::DeserializeCredentialSaveRequest(
				emptyPasswordFrame, parsedEmptyPasswordRequest) ||
			!parsedEmptyPasswordRequest.password.isAssigned() ||
			!parsedEmptyPasswordRequest.password.empty())
		{
			std::cerr << "empty credential password roundtrip failed\n";
			failures++;
		}
		xucrun::broker::CredentialSaveRequest missingPasswordRequest;
		missingPasswordRequest.requestId = request.requestId;
		missingPasswordRequest.requestedAccountSid = request.requestedAccountSid;
		if (!xucrun::broker::SerializeCredentialSaveRequest(missingPasswordRequest).empty())
		{
			std::cerr << "missing credential password accepted\n";
			failures++;
		}
		invalid = frame;
		invalid[xucrun::broker::CredentialSaveRequestHeaderSize + 6] = 0;
		invalid[xucrun::broker::CredentialSaveRequestHeaderSize + 7] = 0;
		if (xucrun::broker::DeserializeCredentialSaveRequest(invalid, parsed))
		{
			std::cerr << "embedded null in credential password accepted\n";
			failures++;
		}

		xucrun::broker::CredentialSaveResult result;
		result.requestId = request.requestId;
		result.status = xucrun::broker::CredentialSaveStatus::InvalidCredentials;
		result.windowsError = ERROR_LOGON_FAILURE;
		const std::vector<std::uint8_t> resultFrame =
			xucrun::broker::SerializeCredentialSaveResult(result);
		xucrun::broker::CredentialSaveResult parsedResult;
		if (!xucrun::broker::DeserializeCredentialSaveResult(resultFrame, parsedResult) ||
			parsedResult.requestId != result.requestId ||
			parsedResult.status != result.status ||
			parsedResult.windowsError != result.windowsError)
		{
			std::cerr << "credential save result serialization failed\n";
			failures++;
		}
	}

	// Tests the isolated client challenge, helper view, and sensitive submit frames.
	void ExpectCredentialCaptureSerialization()
	{
		xucrun::broker::CredentialCaptureChallengeRequest challenge;
		challenge.requestId = 0x0102030405060708ULL;
		challenge.requestedAccountSid = L"S-1-5-21";
		challenge.approvalAccountSid = L"S-1-5-32-544";
		const auto challengeFrame =
			xucrun::broker::SerializeCredentialCaptureChallengeRequest(challenge);
		xucrun::broker::CredentialCaptureChallengeRequest parsedChallenge;
		if (!xucrun::broker::DeserializeCredentialCaptureChallengeRequest(
				challengeFrame, parsedChallenge) ||
			parsedChallenge.requestedAccountSid != challenge.requestedAccountSid ||
			parsedChallenge.approvalAccountSid != challenge.approvalAccountSid)
		{
			std::cerr << "credential capture challenge serialization failed\n";
			failures++;
		}

		xucrun::broker::CredentialCaptureChallengeResult challengeResult;
		challengeResult.requestId = challenge.requestId;
		challengeResult.status = xucrun::broker::ApprovalChallengeStatus::Issued;
		challengeResult.challengeId[0] = 1;
		challengeResult.canonicalClientName = L"CLIENT\\User";
		challengeResult.canonicalAccountName = L"TARGET\\Admin";
		challengeResult.helperStartMode =
			xucrun::broker::ApprovalHelperStartMode::ClientRunAsRequired;
		const auto challengeResultFrame =
			xucrun::broker::SerializeCredentialCaptureChallengeResult(challengeResult);
		xucrun::broker::CredentialCaptureChallengeResult parsedChallengeResult;
		if (!xucrun::broker::DeserializeCredentialCaptureChallengeResult(
				challengeResultFrame, parsedChallengeResult) ||
			parsedChallengeResult.challengeId != challengeResult.challengeId ||
			parsedChallengeResult.canonicalAccountName != challengeResult.canonicalAccountName)
		{
			std::cerr << "credential capture challenge result serialization failed\n";
			failures++;
		}

		xucrun::broker::CredentialCaptureHelperRequest helperRequest;
		helperRequest.requestId = 2;
		helperRequest.challengeId = challengeResult.challengeId;
		const auto helperFrame =
			xucrun::broker::SerializeCredentialCaptureHelperRequest(helperRequest);
		xucrun::broker::CredentialCaptureHelperRequest parsedHelperRequest;
		if (!xucrun::broker::DeserializeCredentialCaptureHelperRequest(
				helperFrame, parsedHelperRequest) ||
			parsedHelperRequest.challengeId != helperRequest.challengeId)
		{
			std::cerr << "credential capture helper request serialization failed\n";
			failures++;
		}

		xucrun::broker::CredentialCaptureHelperResult helperResult;
		helperResult.requestId = helperRequest.requestId;
		helperResult.status = xucrun::broker::ApprovalChallengeStatus::Issued;
		helperResult.canonicalClientName = challengeResult.canonicalClientName;
		helperResult.canonicalAccountName = challengeResult.canonicalAccountName;
		helperResult.authenticityMarker = L"A7C2-F91D";
		const auto helperResultFrame =
			xucrun::broker::SerializeCredentialCaptureHelperResult(helperResult);
		xucrun::broker::CredentialCaptureHelperResult parsedHelperResult;
		if (!xucrun::broker::DeserializeCredentialCaptureHelperResult(
				helperResultFrame, parsedHelperResult) ||
			parsedHelperResult.canonicalClientName != helperResult.canonicalClientName ||
			parsedHelperResult.authenticityMarker != helperResult.authenticityMarker)
		{
			std::cerr << "credential capture helper result serialization failed\n";
			failures++;
		}
		helperResult.authenticityMarker = L"INVALID!!";
		if (!xucrun::broker::SerializeCredentialCaptureHelperResult(helperResult).empty())
		{
			std::cerr << "invalid credential capture authenticity marker accepted\n";
			failures++;
		}

		xucrun::broker::CredentialCaptureSubmitRequest submit;
		submit.requestId = 3;
		submit.challengeId = challengeResult.challengeId;
		submit.enteredAccountName = challengeResult.canonicalAccountName;
		submit.password.assign(L"secret", 6);
		const auto submitFrame = xucrun::broker::SerializeCredentialCaptureSubmitRequest(submit);
		xucrun::broker::CredentialCaptureSubmitRequest parsedSubmit;
		if (!xucrun::broker::DeserializeCredentialCaptureSubmitRequest(
				submitFrame, parsedSubmit) ||
			parsedSubmit.enteredAccountName != submit.enteredAccountName ||
			wcscmp(parsedSubmit.password.c_str(), L"secret") != 0)
		{
			std::cerr << "credential capture submit serialization failed\n";
			failures++;
		}
		std::vector<std::uint8_t> invalidSubmitFrame = submitFrame;
		invalidSubmitFrame[6] = 1;
		if (xucrun::broker::DeserializeCredentialCaptureSubmitRequest(
			invalidSubmitFrame, parsedSubmit))
		{
			std::cerr << "credential capture submit reserved byte accepted\n";
			failures++;
		}
		xucrun::broker::CredentialCaptureSubmitRequest cancelledSubmit;
		cancelledSubmit.requestId = 4;
		cancelledSubmit.challengeId = challengeResult.challengeId;
		cancelledSubmit.cancelled = true;
		cancelledSubmit.enteredAccountName = challengeResult.canonicalAccountName;
		const auto cancelledFrame =
			xucrun::broker::SerializeCredentialCaptureSubmitRequest(cancelledSubmit);
		xucrun::broker::CredentialCaptureSubmitRequest parsedCancelledSubmit;
		if (!xucrun::broker::DeserializeCredentialCaptureSubmitRequest(
			cancelledFrame, parsedCancelledSubmit) ||
			!parsedCancelledSubmit.cancelled ||
			parsedCancelledSubmit.password.isAssigned())
		{
			std::cerr << "credential capture cancellation serialization failed\n";
			failures++;
		}
		submit.challengeId = {};
		if (!xucrun::broker::SerializeCredentialCaptureSubmitRequest(submit).empty())
		{
			std::cerr << "credential capture submit without challenge accepted\n";
			failures++;
		}

		xucrun::broker::CredentialCaptureSubmitResult submitResult;
		submitResult.requestId = 3;
		submitResult.status = xucrun::broker::CredentialSaveStatus::Succeeded;
		const auto submitResultFrame =
			xucrun::broker::SerializeCredentialCaptureSubmitResult(submitResult);
		xucrun::broker::CredentialCaptureSubmitResult parsedSubmitResult;
		if (!xucrun::broker::DeserializeCredentialCaptureSubmitResult(
				submitResultFrame, parsedSubmitResult) ||
			parsedSubmitResult.status != submitResult.status)
		{
			std::cerr << "credential capture submit result serialization failed\n";
			failures++;
		}

		xucrun::broker::CredentialCaptureCommitRequest commit;
		commit.requestId = 5;
		commit.challengeId = challengeResult.challengeId;
		const auto commitFrame =
			xucrun::broker::SerializeCredentialCaptureCommitRequest(commit);
		xucrun::broker::CredentialCaptureCommitRequest parsedCommit;
		if (commitFrame.size() != xucrun::broker::CredentialCaptureCommitRequestSize ||
			!xucrun::broker::DeserializeCredentialCaptureCommitRequest(
				commitFrame, parsedCommit) ||
			parsedCommit.requestId != commit.requestId ||
			parsedCommit.challengeId != commit.challengeId)
		{
			std::cerr << "credential capture commit serialization failed\n";
			failures++;
		}
		std::vector<std::uint8_t> invalidCommitFrame = commitFrame;
		invalidCommitFrame[5] = 1;
		if (xucrun::broker::DeserializeCredentialCaptureCommitRequest(
			invalidCommitFrame, parsedCommit))
		{
			std::cerr << "credential capture commit reserved byte accepted\n";
			failures++;
		}
		commit.challengeId = {};
		if (!xucrun::broker::SerializeCredentialCaptureCommitRequest(commit).empty())
		{
			std::cerr << "credential capture commit without challenge accepted\n";
			failures++;
		}
		xucrun::broker::CredentialCaptureCommitResult commitResult;
		commitResult.requestId = 5;
		commitResult.status = xucrun::broker::CredentialSaveStatus::Succeeded;
		const auto commitResultFrame =
			xucrun::broker::SerializeCredentialCaptureCommitResult(commitResult);
		xucrun::broker::CredentialCaptureCommitResult parsedCommitResult;
		if (commitResultFrame.size() !=
			xucrun::broker::CredentialCaptureCommitResultSize ||
			!xucrun::broker::DeserializeCredentialCaptureCommitResult(
				commitResultFrame, parsedCommitResult) ||
			parsedCommitResult.status != commitResult.status)
		{
			std::cerr << "credential capture commit result serialization failed\n";
			failures++;
		}
	}

	// Tests the canonical round trip and rejection of invalid deletion messages.
	void ExpectCredentialDeleteSerialization()
	{
		xucrun::broker::CredentialDeleteRequest request;
		request.requestId = 0x1122334455667788ULL;
		request.requestedAccountSid = L"S-1-5-21";
		const std::vector<std::uint8_t> frame =
			xucrun::broker::SerializeCredentialDeleteRequest(request);
		xucrun::broker::CredentialDeleteRequest parsed;
		if (!xucrun::broker::DeserializeCredentialDeleteRequest(frame, parsed) ||
			parsed.requestId != request.requestId ||
			parsed.requestedAccountSid != request.requestedAccountSid)
		{
			std::cerr << "credential delete request serialization failed\n";
			failures++;
		}
		for (std::size_t size = 0; size < frame.size(); size++)
		{
			if (xucrun::broker::DeserializeCredentialDeleteRequest(
				std::span<const std::uint8_t>(frame.data(), size), parsed))
			{
				std::cerr << "truncated credential delete request accepted\n";
				failures++;
				break;
			}
		}
		std::vector<std::uint8_t> invalid = frame;
		invalid.push_back(0);
		if (xucrun::broker::DeserializeCredentialDeleteRequest(invalid, parsed))
		{
			std::cerr << "trailing credential delete data accepted\n";
			failures++;
		}
		invalid = frame;
		invalid[xucrun::broker::CredentialDeleteRequestHeaderSize] = 0;
		invalid[xucrun::broker::CredentialDeleteRequestHeaderSize + 1] = 0;
		if (xucrun::broker::DeserializeCredentialDeleteRequest(invalid, parsed))
		{
			std::cerr << "embedded null in credential delete SID accepted\n";
			failures++;
		}

		xucrun::broker::CredentialDeleteResult result;
		result.requestId = request.requestId;
		result.status = xucrun::broker::CredentialDeleteStatus::NotFound;
		result.windowsError = ERROR_NOT_FOUND;
		const std::vector<std::uint8_t> resultFrame =
			xucrun::broker::SerializeCredentialDeleteResult(result);
		xucrun::broker::CredentialDeleteResult parsedResult;
		if (!xucrun::broker::DeserializeCredentialDeleteResult(resultFrame, parsedResult) ||
			parsedResult.requestId != result.requestId ||
			parsedResult.status != result.status ||
			parsedResult.windowsError != result.windowsError)
		{
			std::cerr << "credential delete result serialization failed\n";
			failures++;
		}
	}

	// Tests the round trip and status-dependent process ID of credential use test messages.
	void ExpectCredentialUseTestSerialization()
	{
		xucrun::broker::CredentialUseTestRequest request;
		request.requestId = 0x8877665544332211ULL;
		request.requestedAccountSid = L"S-1-5-32";
		const std::vector<std::uint8_t> frame =
			xucrun::broker::SerializeCredentialUseTestRequest(request);
		xucrun::broker::CredentialUseTestRequest parsed;
		if (!xucrun::broker::DeserializeCredentialUseTestRequest(frame, parsed) ||
			parsed.requestId != request.requestId ||
			parsed.requestedAccountSid != request.requestedAccountSid)
		{
			std::cerr << "credential use test request serialization failed\n";
			failures++;
		}
		for (std::size_t size = 0; size < frame.size(); size++)
		{
			if (xucrun::broker::DeserializeCredentialUseTestRequest(
				std::span<const std::uint8_t>(frame.data(), size), parsed))
			{
				std::cerr << "truncated credential use test request accepted\n";
				failures++;
				break;
			}
		}
		std::vector<std::uint8_t> invalid = frame;
		invalid.push_back(0);
		if (xucrun::broker::DeserializeCredentialUseTestRequest(invalid, parsed))
		{
			std::cerr << "trailing credential use test data accepted\n";
			failures++;
		}

		xucrun::broker::CredentialUseTestResult result;
		result.requestId = request.requestId;
		result.status = xucrun::broker::CredentialUseTestStatus::Succeeded;
		result.windowsError = NO_ERROR;
		result.processId = 1234;
		const std::vector<std::uint8_t> resultFrame =
			xucrun::broker::SerializeCredentialUseTestResult(result);
		xucrun::broker::CredentialUseTestResult parsedResult;
		if (!xucrun::broker::DeserializeCredentialUseTestResult(resultFrame, parsedResult) ||
			parsedResult.requestId != result.requestId ||
			parsedResult.status != result.status ||
			parsedResult.windowsError != result.windowsError ||
			parsedResult.processId != result.processId)
		{
			std::cerr << "credential use test result serialization failed\n";
			failures++;
		}
	}

	// Tests the round trip and identity binding of all approval challenge fields.
	void ExpectApprovalChallengeSerialization()
	{
		xucrun::broker::ApprovalChallengeRequest request;
		request.requestId = 0x1020304050607080ULL;
		request.requestedAccountSid = L"S-1-5-21-1";
		request.approvalAccountSid = L"S-1-5-21-2";
		request.executablePath = L"C:\\Windows\\System32\\cmd.exe";
		request.argumentTail = L"/c whoami";
		request.workingDirectory = L"C:\\Windows";
		request.elevationMode = xucrun::broker::ElevationMode::RequireElevated;
		request.visibility = xucrun::broker::LaunchVisibility::HiddenMonitor;
		xucrun::broker::ApprovalChallengeRequest invalidElevationRequest = request;
		invalidElevationRequest.elevationMode =
			static_cast<xucrun::broker::ElevationMode>(0xFF);
		if (xucrun::broker::ValidateApprovalChallengeRequest(invalidElevationRequest) !=
			xucrun::broker::ContractValidationError::InvalidElevationMode)
		{
			std::cerr << "approval challenge accepted unknown elevation mode\n";
			failures++;
		}
		xucrun::broker::ApprovalChallengeRequest invalidVisibilityRequest = request;
		invalidVisibilityRequest.visibility =
			static_cast<xucrun::broker::LaunchVisibility>(0xFF);
		if (xucrun::broker::ValidateApprovalChallengeRequest(invalidVisibilityRequest) !=
			xucrun::broker::ContractValidationError::InvalidLaunchVisibility)
		{
			std::cerr << "approval challenge accepted unknown launch visibility\n";
			failures++;
		}
		const std::vector<std::uint8_t> requestFrame =
			xucrun::broker::SerializeApprovalChallengeRequest(request);
		xucrun::broker::ApprovalChallengeRequest parsedRequest;
		if (!xucrun::broker::DeserializeApprovalChallengeRequest(
				requestFrame, parsedRequest) ||
			parsedRequest.requestId != request.requestId ||
			parsedRequest.requestedAccountSid != request.requestedAccountSid ||
			parsedRequest.approvalAccountSid != request.approvalAccountSid ||
			parsedRequest.executablePath != request.executablePath ||
			parsedRequest.argumentTail != request.argumentTail ||
			parsedRequest.workingDirectory != request.workingDirectory ||
			parsedRequest.elevationMode != request.elevationMode ||
			parsedRequest.visibility != request.visibility)
		{
			std::cerr << "approval challenge request serialization failed\n";
			failures++;
		}
		for (const xucrun::broker::LaunchVisibility visibility : {
			xucrun::broker::LaunchVisibility::Normal,
			xucrun::broker::LaunchVisibility::Hidden,
			xucrun::broker::LaunchVisibility::HiddenMonitor })
		{
			xucrun::broker::ApprovalChallengeRequest visibilityRequest = request;
			visibilityRequest.visibility = visibility;
			const std::vector<std::uint8_t> visibilityFrame =
				xucrun::broker::SerializeApprovalChallengeRequest(visibilityRequest);
			xucrun::broker::ApprovalChallengeRequest parsedVisibilityRequest;
			if (!xucrun::broker::DeserializeApprovalChallengeRequest(
					visibilityFrame, parsedVisibilityRequest) ||
				parsedVisibilityRequest.visibility != visibility)
			{
				std::cerr << "approval challenge visibility roundtrip failed\n";
				failures++;
			}
		}
		for (std::size_t size = 0; size < requestFrame.size(); size++)
		{
			if (xucrun::broker::DeserializeApprovalChallengeRequest(
				std::span<const std::uint8_t>(requestFrame.data(), size), parsedRequest))
			{
				std::cerr << "truncated approval challenge request accepted\n";
				failures++;
				break;
			}
		}
		std::vector<std::uint8_t> invalidRequest = requestFrame;
		invalidRequest[5] = 0xFF;
		if (xucrun::broker::DeserializeApprovalChallengeRequest(
			invalidRequest, parsedRequest))
		{
			std::cerr << "unknown approval challenge elevation mode accepted\n";
			failures++;
		}
		invalidRequest = requestFrame;
		invalidRequest[6] = 0xFF;
		if (xucrun::broker::DeserializeApprovalChallengeRequest(
			invalidRequest, parsedRequest))
		{
			std::cerr << "unknown approval challenge visibility accepted\n";
			failures++;
		}
		invalidRequest = requestFrame;
		invalidRequest[7] = 1;
		if (xucrun::broker::DeserializeApprovalChallengeRequest(
			invalidRequest, parsedRequest))
		{
			std::cerr << "nonzero approval challenge reserved byte accepted\n";
			failures++;
		}

		xucrun::broker::ApprovalChallengeResult result;
		result.requestId = request.requestId;
		result.status = xucrun::broker::ApprovalChallengeStatus::Issued;
		result.challengeId[0] = 1;
		result.canonicalClientName = L"Z2\\Harald";
		result.canonicalAccountName = L"Z2\\Administrator";
		result.clientPath = L"C:\\Tools\\xucRun.exe";
		result.clientSha256[0] = 0x5A;
		result.clientTrustStatus = 0;
		result.clientSignatureTrusted = true;
		result.finalExecutablePath = request.executablePath;
		result.executableSha256[31] = 0xA5;
		result.executableTrustStatus = static_cast<std::int32_t>(0x800B0100u);
		result.argumentTail = request.argumentTail;
		result.workingDirectory = request.workingDirectory;
		result.elevationMode = request.elevationMode;
		result.visibility = request.visibility;
		result.programUpdateAvailable = true;
		result.helperStartMode =
			xucrun::broker::ApprovalHelperStartMode::BrokerStarted;
		result.approvalHelperProcessHandle = 0x123456789ABCDEF0ull;
		result.writableElevatedTargetReason =
			xucrun::broker::ApprovalFailureReason::ElevatedTargetWritable;
		result.changedProgramPath = L"C:\\Tools\\xucRun.exe";
		result.oldProgramSha256[0] = 0x11;
		result.newProgramSha256[0] = 0x22;
		result.affectedApprovalCount = 5;
		result.affectedAccountCount = 2;
		const std::vector<std::uint8_t> resultFrame =
			xucrun::broker::SerializeApprovalChallengeResult(result);
		xucrun::broker::ApprovalChallengeResult parsedResult;
		if (!xucrun::broker::DeserializeApprovalChallengeResult(resultFrame, parsedResult) ||
			parsedResult.requestId != result.requestId || parsedResult.status != result.status ||
			parsedResult.challengeId != result.challengeId ||
			parsedResult.canonicalClientName != result.canonicalClientName ||
			parsedResult.canonicalAccountName != result.canonicalAccountName ||
			parsedResult.clientPath != result.clientPath ||
			parsedResult.clientSha256 != result.clientSha256 ||
			parsedResult.clientTrustStatus != result.clientTrustStatus ||
			parsedResult.clientSignatureTrusted != result.clientSignatureTrusted ||
			parsedResult.finalExecutablePath != result.finalExecutablePath ||
			parsedResult.executableSha256 != result.executableSha256 ||
			parsedResult.executableTrustStatus != result.executableTrustStatus ||
			parsedResult.executableSignatureTrusted != result.executableSignatureTrusted ||
			parsedResult.argumentTail != result.argumentTail ||
			parsedResult.workingDirectory != result.workingDirectory ||
			parsedResult.elevationMode != result.elevationMode ||
			parsedResult.visibility != result.visibility ||
			parsedResult.programUpdateAvailable != result.programUpdateAvailable ||
			parsedResult.helperStartMode != result.helperStartMode ||
			parsedResult.approvalHelperView != result.approvalHelperView ||
			parsedResult.storedApprovalPresenceConfirmation !=
				result.storedApprovalPresenceConfirmation ||
			parsedResult.writableElevatedTargetReason !=
				result.writableElevatedTargetReason ||
			parsedResult.approvalHelperProcessHandle !=
				result.approvalHelperProcessHandle ||
			parsedResult.changedProgramPath != result.changedProgramPath ||
			parsedResult.oldProgramSha256 != result.oldProgramSha256 ||
			parsedResult.newProgramSha256 != result.newProgramSha256 ||
			parsedResult.affectedApprovalCount != result.affectedApprovalCount ||
			parsedResult.affectedAccountCount != result.affectedAccountCount ||
			parsedResult.authenticityMarker != result.authenticityMarker)
		{
			std::cerr << "approval challenge result serialization failed\n";
			failures++;
		}
		result.writableElevatedTargetReason =
			xucrun::broker::ApprovalFailureReason::None;
		invalidRequest = resultFrame;
		invalidRequest[220] = 0xFF;
		if (xucrun::broker::DeserializeApprovalChallengeResult(invalidRequest, parsedResult))
		{
			std::cerr << "unknown approval helper start mode accepted\n";
			failures++;
		}
		invalidRequest = resultFrame;
		invalidRequest[222] = 2;
		if (xucrun::broker::DeserializeApprovalChallengeResult(invalidRequest, parsedResult))
		{
			std::cerr << "invalid stored approval presence confirmation accepted\n";
			failures++;
		}
		invalidRequest = resultFrame;
		invalidRequest[223] = static_cast<std::uint8_t>(
			xucrun::broker::ApprovalFailureReason::ElevatedTargetDirectoryWritable);
		if (xucrun::broker::DeserializeApprovalChallengeResult(invalidRequest, parsedResult))
		{
			std::cerr << "writable elevated target directory override accepted\n";
			failures++;
		}
		xucrun::broker::ApprovalChallengeResult writableHelperResult = result;
		writableHelperResult.programUpdateAvailable = false;
		writableHelperResult.changedProgramPath.clear();
		writableHelperResult.oldProgramSha256 = {};
		writableHelperResult.newProgramSha256 = {};
		writableHelperResult.affectedApprovalCount = 0;
		writableHelperResult.affectedAccountCount = 0;
		writableHelperResult.userConfirmationRequired = false;
		writableHelperResult.approvalHelperView = true;
		writableHelperResult.helperStartMode =
			xucrun::broker::ApprovalHelperStartMode::ClientRunAsRequired;
		writableHelperResult.approvalHelperProcessHandle = 0;
		writableHelperResult.writableElevatedTargetReason =
			xucrun::broker::ApprovalFailureReason::ElevatedTargetWritable;
		writableHelperResult.authenticityMarker = L"A7C2-F91D";
		const std::vector<std::uint8_t> writableHelperFrame =
			xucrun::broker::SerializeApprovalChallengeResult(writableHelperResult);
		if (!xucrun::broker::DeserializeApprovalChallengeResult(
				writableHelperFrame, parsedResult) ||
			parsedResult.writableElevatedTargetReason !=
				writableHelperResult.writableElevatedTargetReason ||
			!parsedResult.approvalHelperView ||
			parsedResult.authenticityMarker != writableHelperResult.authenticityMarker)
		{
			std::cerr << "writable elevated target helper view roundtrip failed\n";
			failures++;
		}
		writableHelperResult.authenticityMarker = L"invalid-1";
		if (!xucrun::broker::SerializeApprovalChallengeResult(
			writableHelperResult).empty())
		{
			std::cerr << "invalid approval authenticity marker accepted\n";
			failures++;
		}
		xucrun::broker::ApprovalChallengeResult presenceResult = result;
		presenceResult.elevationMode = xucrun::broker::ElevationMode::PreserveToken;
		presenceResult.programUpdateAvailable = false;
		presenceResult.changedProgramPath.clear();
		presenceResult.oldProgramSha256 = {};
		presenceResult.newProgramSha256 = {};
		presenceResult.affectedApprovalCount = 0;
		presenceResult.affectedAccountCount = 0;
		presenceResult.storedApprovalPresenceConfirmation = true;
		const std::vector<std::uint8_t> presenceResultFrame =
			xucrun::broker::SerializeApprovalChallengeResult(presenceResult);
		if (!xucrun::broker::DeserializeApprovalChallengeResult(
				presenceResultFrame, parsedResult) ||
			!parsedResult.storedApprovalPresenceConfirmation)
		{
			std::cerr << "stored approval presence confirmation roundtrip failed\n";
			failures++;
		}
		xucrun::broker::ApprovalChallengeResult invalidPresenceResult = presenceResult;
		invalidPresenceResult.helperStartMode =
			xucrun::broker::ApprovalHelperStartMode::ClientRunAsRequired;
		invalidPresenceResult.approvalHelperProcessHandle = 0;
		const std::vector<std::uint8_t> clientPresenceResultFrame =
			xucrun::broker::SerializeApprovalChallengeResult(invalidPresenceResult);
		if (!xucrun::broker::DeserializeApprovalChallengeResult(
			clientPresenceResultFrame, parsedResult) ||
			!parsedResult.storedApprovalPresenceConfirmation ||
			parsedResult.helperStartMode !=
				xucrun::broker::ApprovalHelperStartMode::ClientRunAsRequired)
		{
			std::cerr << "client-started stored approval presence confirmation failed\n";
			failures++;
		}
		invalidPresenceResult = result;
		invalidPresenceResult.elevationMode =
			xucrun::broker::ElevationMode::PreserveToken;
		invalidPresenceResult.storedApprovalPresenceConfirmation = true;
		if (!xucrun::broker::SerializeApprovalChallengeResult(
			invalidPresenceResult).empty())
		{
			std::cerr << "program update with stored approval presence confirmation accepted\n";
			failures++;
		}
		result.helperStartMode =
			xucrun::broker::ApprovalHelperStartMode::ClientRunAsRequired;
		result.approvalHelperProcessHandle = 0;
		const std::vector<std::uint8_t> clientStartedResultFrame =
			xucrun::broker::SerializeApprovalChallengeResult(result);
		if (!xucrun::broker::DeserializeApprovalChallengeResult(
				clientStartedResultFrame, parsedResult) ||
			parsedResult.helperStartMode !=
				xucrun::broker::ApprovalHelperStartMode::ClientRunAsRequired ||
			parsedResult.approvalHelperProcessHandle != 0)
		{
			std::cerr << "client-started approval helper mode roundtrip failed\n";
			failures++;
		}
		result.helperStartMode = xucrun::broker::ApprovalHelperStartMode::BrokerStarted;
		result.approvalHelperProcessHandle = 0x123456789ABCDEF0ull;
		result.approvalHelperProcessHandle = 0;
		if (!xucrun::broker::SerializeApprovalChallengeResult(result).empty())
		{
			std::cerr << "broker-started approval helper without handle accepted\n";
			failures++;
		}
		result.helperStartMode =
			xucrun::broker::ApprovalHelperStartMode::ClientRunAsRequired;
		result.approvalHelperProcessHandle = 1;
		if (!xucrun::broker::SerializeApprovalChallengeResult(result).empty())
		{
			std::cerr << "client-started approval helper with broker handle accepted\n";
			failures++;
		}
		result.helperStartMode = xucrun::broker::ApprovalHelperStartMode::None;
		result.approvalHelperProcessHandle = 0;
		if (!xucrun::broker::SerializeApprovalChallengeResult(result).empty())
		{
			std::cerr << "approval confirmation without helper start mode accepted\n";
			failures++;
		}
		result.userConfirmationRequired = false;
		if (xucrun::broker::SerializeApprovalChallengeResult(result).empty())
		{
			std::cerr << "approval challenge without required confirmation rejected\n";
			failures++;
		}
		result.approvalHelperView = true;
		result.helperStartMode =
			xucrun::broker::ApprovalHelperStartMode::ClientRunAsRequired;
		result.authenticityMarker = L"A7C2-F91D";
		const std::vector<std::uint8_t> clientStartedHelperViewFrame =
			xucrun::broker::SerializeApprovalChallengeResult(result);
		if (!xucrun::broker::DeserializeApprovalChallengeResult(
				clientStartedHelperViewFrame, parsedResult) ||
			!parsedResult.approvalHelperView ||
			parsedResult.helperStartMode !=
				xucrun::broker::ApprovalHelperStartMode::ClientRunAsRequired)
		{
			std::cerr << "client-started approval helper view roundtrip failed\n";
			failures++;
		}
		result.helperStartMode = xucrun::broker::ApprovalHelperStartMode::BrokerStarted;
		if (xucrun::broker::SerializeApprovalChallengeResult(result).empty())
		{
			std::cerr << "broker-started approval helper view rejected\n";
			failures++;
		}
		result.userConfirmationRequired = true;
		if (!xucrun::broker::SerializeApprovalChallengeResult(result).empty())
		{
			std::cerr << "helper view requiring another confirmation accepted\n";
			failures++;
		}
		result.approvalHelperView = false;
		result.authenticityMarker.clear();
		result.userConfirmationRequired = true;
		result.helperStartMode = xucrun::broker::ApprovalHelperStartMode::BrokerStarted;
		result.approvalHelperProcessHandle = 0x123456789ABCDEF0ull;
		invalidRequest = resultFrame;
		invalidRequest.push_back(0);
		if (xucrun::broker::DeserializeApprovalChallengeResult(invalidRequest, parsedResult))
		{
			std::cerr << "trailing approval challenge result data accepted\n";
			failures++;
		}
		invalidRequest = resultFrame;
		invalidRequest[7] |= 0x0C;
		if (xucrun::broker::DeserializeApprovalChallengeResult(invalidRequest, parsedResult))
		{
			std::cerr << "combined hidden visibility flags accepted\n";
			failures++;
		}
		result.failureReason =
			xucrun::broker::ApprovalFailureReason::ChallengeSidLimitReached;
		if (!xucrun::broker::SerializeApprovalChallengeResult(result).empty())
		{
			std::cerr << "issued approval challenge with failure reason accepted\n";
			failures++;
		}
		xucrun::broker::ApprovalChallengeResult failedResult;
		failedResult.requestId = request.requestId;
		failedResult.status = xucrun::broker::ApprovalChallengeStatus::Denied;
		failedResult.windowsError = ERROR_BUSY;
		failedResult.failureReason =
			xucrun::broker::ApprovalFailureReason::ChallengeSidLimitReached;
		const std::vector<std::uint8_t> failedResultFrame =
			xucrun::broker::SerializeApprovalChallengeResult(failedResult);
		if (!xucrun::broker::DeserializeApprovalChallengeResult(
				failedResultFrame, parsedResult) ||
			parsedResult.failureReason != failedResult.failureReason)
		{
			std::cerr << "approval challenge failure reason roundtrip failed\n";
			failures++;
		}
		invalidRequest = failedResultFrame;
		invalidRequest[7] = static_cast<std::uint8_t>(
			(invalidRequest[7] & 0x0F) | 0xF0);
		if (xucrun::broker::DeserializeApprovalChallengeResult(invalidRequest, parsedResult))
		{
			std::cerr << "unknown approval challenge failure reason accepted\n";
			failures++;
		}
		result.failureReason = xucrun::broker::ApprovalFailureReason::None;
		result.challengeId = {};
		if (!xucrun::broker::SerializeApprovalChallengeResult(result).empty())
		{
			std::cerr << "issued approval challenge without nonce accepted\n";
			failures++;
		}
		result.status = xucrun::broker::ApprovalChallengeStatus::Denied;
		if (!xucrun::broker::SerializeApprovalChallengeResult(result).empty())
		{
			std::cerr << "denied approval challenge leaked bound target data\n";
			failures++;
		}
	}

	// Tests the round trip and consistent decision combination of the confirmation.
	void ExpectApprovalConfirmationSerialization()
	{
		xucrun::broker::ApprovalConfirmationRequest request;
		request.requestId = 0x1122334455667788ULL;
		request.challengeId[0] = 0xA5;
		request.decision = xucrun::broker::ApprovalDecision::ApproveOnce;
		request.persistence = xucrun::broker::ApprovalPersistence::OneDay;
		request.programScope =
			xucrun::broker::ApprovalProgramScope::TargetProgram;
		request.accountScope = xucrun::broker::ApprovalAccountScope::AllAccounts;
		request.updateScope = xucrun::broker::ApprovalUpdateScope::CurrentAccount;
		request.persistence = xucrun::broker::ApprovalPersistence::Once;
		const std::vector<std::uint8_t> requestFrame =
			xucrun::broker::SerializeApprovalConfirmationRequest(request);
		xucrun::broker::ApprovalConfirmationRequest parsedRequest;
		if (requestFrame.size() != xucrun::broker::ApprovalConfirmationRequestSize ||
			!xucrun::broker::DeserializeApprovalConfirmationRequest(
				requestFrame, parsedRequest) ||
			parsedRequest.requestId != request.requestId ||
			parsedRequest.challengeId != request.challengeId ||
			parsedRequest.decision != request.decision ||
			parsedRequest.persistence != request.persistence ||
			parsedRequest.programScope != request.programScope ||
			parsedRequest.accountScope != request.accountScope ||
			parsedRequest.updateScope != request.updateScope)
		{
			std::cerr << "approval confirmation request serialization failed\n";
			failures++;
		}
		std::vector<std::uint8_t> invalid = requestFrame;
		invalid[5] = 0xFF;
		if (xucrun::broker::DeserializeApprovalConfirmationRequest(invalid, parsedRequest))
		{
			std::cerr << "unknown approval confirmation decision accepted\n";
			failures++;
		}
		invalid = requestFrame;
		invalid[6] = 0xFF;
		if (xucrun::broker::DeserializeApprovalConfirmationRequest(invalid, parsedRequest))
		{
			std::cerr << "unknown approval persistence accepted\n";
			failures++;
		}
		invalid = requestFrame;
		invalid[9] = 0xFF;
		if (xucrun::broker::DeserializeApprovalConfirmationRequest(invalid, parsedRequest))
		{
			std::cerr << "unknown approval update scope accepted\n";
			failures++;
		}
		request.persistence = xucrun::broker::ApprovalPersistence::Forever;
		if (!xucrun::broker::SerializeApprovalConfirmationRequest(request).empty())
		{
			std::cerr << "approval update combined with persistence\n";
			failures++;
		}
		request.persistence = xucrun::broker::ApprovalPersistence::Once;
		request.challengeId = {};
		if (!xucrun::broker::SerializeApprovalConfirmationRequest(request).empty())
		{
			std::cerr << "approval confirmation without challenge accepted\n";
			failures++;
		}

		xucrun::broker::ApprovalConfirmationResult result;
		result.requestId = 0x8877665544332211ULL;
		result.status = xucrun::broker::ApprovalConfirmationStatus::Expired;
		result.windowsError = ERROR_TIMEOUT;
		result.failureReason = xucrun::broker::ApprovalFailureReason::LaunchObjectChanged;
		const std::vector<std::uint8_t> resultFrame =
			xucrun::broker::SerializeApprovalConfirmationResult(result);
		xucrun::broker::ApprovalConfirmationResult parsedResult;
		if (resultFrame.size() != xucrun::broker::ApprovalConfirmationResultSize ||
			!xucrun::broker::DeserializeApprovalConfirmationResult(
				resultFrame, parsedResult) ||
			parsedResult.requestId != result.requestId ||
			parsedResult.status != result.status ||
			parsedResult.windowsError != result.windowsError ||
			parsedResult.failureReason != result.failureReason)
		{
			std::cerr << "approval confirmation result serialization failed\n";
			failures++;
		}
		invalid = resultFrame;
		invalid.push_back(0);
		if (xucrun::broker::DeserializeApprovalConfirmationResult(invalid, parsedResult))
		{
			std::cerr << "trailing approval confirmation result data accepted\n";
			failures++;
		}
		invalid = resultFrame;
		invalid[6] = 0xFF;
		if (xucrun::broker::DeserializeApprovalConfirmationResult(invalid, parsedResult))
		{
			std::cerr << "unknown approval confirmation failure reason accepted\n";
			failures++;
		}
		result.status = xucrun::broker::ApprovalConfirmationStatus::Confirmed;
		result.windowsError = NO_ERROR;
		result.processId = 1234;
		if (!xucrun::broker::SerializeApprovalConfirmationResult(result).empty())
		{
			std::cerr << "confirmed approval with failure reason accepted\n";
			failures++;
		}
		result.failureReason = xucrun::broker::ApprovalFailureReason::None;
		if (xucrun::broker::SerializeApprovalConfirmationResult(result).empty())
		{
			std::cerr << "confirmed approval with process rejected\n";
			failures++;
		}
	}

	void ExpectApprovalHelperSerialization()
	{
		xucrun::broker::ApprovalHelperRequest request;
		request.requestId = 0x0102030405060708ULL;
		request.challengeId[0] = 0xA5;
		const std::vector<std::uint8_t> requestFrame =
			xucrun::broker::SerializeApprovalHelperRequest(request);
		xucrun::broker::ApprovalHelperRequest parsedRequest;
		if (requestFrame.size() != xucrun::broker::ApprovalHelperRequestSize ||
			!xucrun::broker::DeserializeApprovalHelperRequest(requestFrame, parsedRequest) ||
			parsedRequest.requestId != request.requestId ||
			parsedRequest.challengeId != request.challengeId)
		{
			std::cerr << "approval helper request serialization failed\n";
			failures++;
		}
		request.requestId++;
		const std::vector<std::uint8_t> leaseFrame =
			xucrun::broker::SerializeApprovalHelperLeaseRequest(request);
		if (leaseFrame.size() != xucrun::broker::ApprovalHelperLeaseRequestSize ||
			!xucrun::broker::DeserializeApprovalHelperLeaseRequest(
				leaseFrame, parsedRequest) ||
			parsedRequest.requestId != request.requestId ||
			parsedRequest.challengeId != request.challengeId ||
			xucrun::broker::DeserializeApprovalHelperRequest(leaseFrame, parsedRequest))
		{
			std::cerr << "approval helper lease request serialization failed\n";
			failures++;
		}
		request.challengeId = {};
		if (!xucrun::broker::SerializeApprovalHelperRequest(request).empty())
		{
			std::cerr << "approval helper request without challenge accepted\n";
			failures++;
		}

		xucrun::broker::ApprovalHelperDecisionRequest decision;
		decision.requestId = 0x1020304050607080ULL;
		decision.challengeId[0] = 0x5A;
		decision.decision = xucrun::broker::ApprovalDecision::ApproveOnce;
		decision.persistence = xucrun::broker::ApprovalPersistence::OneDay;
		decision.programScope =
			xucrun::broker::ApprovalProgramScope::TargetProgram;
		decision.accountScope = xucrun::broker::ApprovalAccountScope::CurrentAccount;
		const std::vector<std::uint8_t> decisionFrame =
			xucrun::broker::SerializeApprovalHelperDecisionRequest(decision);
		xucrun::broker::ApprovalHelperDecisionRequest parsedDecision;
		if (decisionFrame.size() !=
			xucrun::broker::ApprovalHelperDecisionRequestSize ||
			!xucrun::broker::DeserializeApprovalHelperDecisionRequest(
				decisionFrame, parsedDecision) ||
			parsedDecision.requestId != decision.requestId ||
			parsedDecision.challengeId != decision.challengeId ||
			parsedDecision.decision != decision.decision ||
			parsedDecision.persistence != decision.persistence ||
			parsedDecision.programScope != decision.programScope ||
			parsedDecision.accountScope != decision.accountScope ||
			parsedDecision.updateScope != decision.updateScope)
		{
			std::cerr << "approval helper decision serialization failed\n";
			failures++;
		}
		std::vector<std::uint8_t> invalidDecision = decisionFrame;
		invalidDecision[5] = 0xFF;
		if (xucrun::broker::DeserializeApprovalHelperDecisionRequest(
			invalidDecision, parsedDecision))
		{
			std::cerr << "invalid approval helper decision accepted\n";
			failures++;
		}

		xucrun::broker::ApprovalHelperDecisionResult result;
		result.requestId = decision.requestId;
		result.status = xucrun::broker::ApprovalConfirmationStatus::Confirmed;
		const std::vector<std::uint8_t> resultFrame =
			xucrun::broker::SerializeApprovalHelperDecisionResult(result);
		xucrun::broker::ApprovalHelperDecisionResult parsedResult;
		if (resultFrame.size() != xucrun::broker::ApprovalHelperDecisionResultSize ||
			!xucrun::broker::DeserializeApprovalHelperDecisionResult(
				resultFrame, parsedResult) ||
			parsedResult.requestId != result.requestId ||
			parsedResult.status != result.status ||
			parsedResult.windowsError != result.windowsError)
		{
			std::cerr << "approval helper decision result serialization failed\n";
			failures++;
		}
		result.requestId++;
		const std::vector<std::uint8_t> leaseResultFrame =
			xucrun::broker::SerializeApprovalHelperLeaseResult(result);
		if (leaseResultFrame.size() != xucrun::broker::ApprovalHelperLeaseResultSize ||
			!xucrun::broker::DeserializeApprovalHelperLeaseResult(
				leaseResultFrame, parsedResult) ||
			parsedResult.requestId != result.requestId ||
			parsedResult.status != result.status ||
			parsedResult.windowsError != result.windowsError ||
			xucrun::broker::DeserializeApprovalHelperDecisionResult(
				leaseResultFrame, parsedResult))
		{
			std::cerr << "approval helper lease result serialization failed\n";
			failures++;
		}
	}

	// Tests path resolution and stable SHA-256 identity with the file position restored.
	void ExpectFileIdentity()
	{
		std::wstring systemExecutable;
		if (!xucrun::files::ResolveExecutablePath(L"cmd.exe", systemExecutable) ||
			systemExecutable.empty())
		{
			std::cerr << "executable search path resolution failed\n";
			failures++;
		}

		wchar_t temporaryDirectory[MAX_PATH] = {};
		wchar_t temporaryFile[MAX_PATH] = {};
		if (GetTempPathW(MAX_PATH, temporaryDirectory) == 0 ||
			GetTempFileNameW(temporaryDirectory, L"xct", 0, temporaryFile) == 0)
		{
			std::cerr << "temporary file setup failed\n";
			failures++;
			return;
		}

		{
			std::ofstream output(temporaryFile, std::ios::binary | std::ios::trunc);
			output << "xucRun file identity test";
		}

		std::wstring finalPath;
		std::wstring handleFinalPath;
		std::wstring executablePath;
		std::vector<std::uint8_t> firstHash;
		std::vector<std::uint8_t> secondHash;
		if (!xucrun::files::ResolveExecutablePath(temporaryFile, executablePath) ||
			executablePath.empty() ||
			xucrun::files::ResolveExecutablePath(L"Z:\\xucRun-missing-file.exe", executablePath))
		{
			std::cerr << "explicit executable path resolution failed\n";
			failures++;
		}
		if (!xucrun::files::ResolveFinalFilePath(temporaryFile, finalPath) ||
			finalPath.empty() || finalPath.rfind(L"\\\\?\\", 0) == 0)
		{
			std::cerr << "final path resolution failed\n";
			failures++;
		}
		HANDLE temporaryHandle = CreateFileW(temporaryFile, GENERIC_READ,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			nullptr, OPEN_EXISTING, 0, nullptr);
		std::vector<std::uint8_t> handleHash;
		LARGE_INTEGER beforeHash = {};
		LARGE_INTEGER afterHash = {};
		LARGE_INTEGER zero = {};
		if (temporaryHandle == INVALID_HANDLE_VALUE ||
			!SetFilePointerEx(temporaryHandle, zero, &beforeHash, FILE_CURRENT) ||
			!xucrun::files::ResolveFinalFilePathFromHandle(temporaryHandle, handleFinalPath) ||
			handleFinalPath != finalPath ||
			!xucrun::files::ComputeFileSHA256FromHandle(temporaryHandle, handleHash) ||
			!SetFilePointerEx(temporaryHandle, zero, &afterHash, FILE_CURRENT) ||
			afterHash.QuadPart != beforeHash.QuadPart)
		{
			std::cerr << "handle-based file identity failed\n";
			failures++;
		}
		if (temporaryHandle != INVALID_HANDLE_VALUE)
			CloseHandle(temporaryHandle);
		if (!xucrun::files::ComputeFileSHA256(temporaryFile, firstHash) ||
			firstHash.size() != 32 ||
			handleHash != firstHash ||
			!xucrun::files::ComputeFileSHA256(temporaryFile, secondHash) ||
			firstHash != secondHash)
		{
			std::cerr << "stable SHA-256 calculation failed\n";
			failures++;
		}

		{
			std::ofstream output(temporaryFile, std::ios::binary | std::ios::app);
			output << " changed";
		}
		if (!xucrun::files::ComputeFileSHA256(temporaryFile, secondHash) ||
			firstHash == secondHash)
		{
			std::cerr << "changed file was not detected\n";
			failures++;
		}

		DeleteFileW(temporaryFile);
	}

	// Tests canonical round trips and size constraints of approval management messages.
	void ExpectApprovalManagementSerialization()
	{
		xucrun::broker::ApprovalListRequest listRequest;
		listRequest.requestId = 901;
		const auto listRequestFrame =
			xucrun::broker::SerializeApprovalListRequest(listRequest);
		xucrun::broker::ApprovalListRequest parsedListRequest;
		if (listRequestFrame.size() != xucrun::broker::ApprovalListRequestSize ||
			!xucrun::broker::DeserializeApprovalListRequest(
				listRequestFrame, parsedListRequest) ||
			parsedListRequest.requestId != listRequest.requestId)
		{
			std::cerr << "approval list request round trip failed\n";
			failures++;
		}

		xucrun::approval::BrokerApprovalValue approval;
		approval.programScope = xucrun::approval::BrokerApprovalProgramScope::General;
		approval.accountScope = xucrun::approval::BrokerApprovalAccountScope::AllAccounts;
		approval.lifetime = xucrun::approval::BrokerApprovalLifetime::Forever;
		approval.createdAt = 100;
		approval.ownerUserSid = L"S-1-5-21-1-2-3-1001";
		xucrun::broker::ApprovalListResult listResult;
		listResult.requestId = listRequest.requestId;
		listResult.status = xucrun::broker::ApprovalManagementStatus::Succeeded;
		listResult.entries.push_back(approval);
		const auto listResultFrame = xucrun::broker::SerializeApprovalListResult(listResult);
		xucrun::broker::ApprovalListResult parsedListResult;
		if (!xucrun::broker::DeserializeApprovalListResult(
			listResultFrame, parsedListResult) ||
			parsedListResult.entries.size() != 1 ||
			parsedListResult.entries[0].ownerUserSid != approval.ownerUserSid)
		{
			std::cerr << "approval list result round trip failed\n";
			failures++;
		}
		auto invalidList = listResultFrame;
		invalidList.pop_back();
		if (xucrun::broker::DeserializeApprovalListResult(
			invalidList, parsedListResult))
		{
			std::cerr << "truncated approval list accepted\n";
			failures++;
		}

		xucrun::broker::ApprovalResetRequest resetRequest;
		resetRequest.requestId = 902;
		resetRequest.approval = approval;
		const auto resetRequestFrame =
			xucrun::broker::SerializeApprovalResetRequest(resetRequest);
		xucrun::broker::ApprovalResetRequest parsedResetRequest;
		if (!xucrun::broker::DeserializeApprovalResetRequest(
			resetRequestFrame, parsedResetRequest) ||
			parsedResetRequest.approval.ownerUserSid != approval.ownerUserSid ||
			parsedResetRequest.approval.accountScope != approval.accountScope)
		{
			std::cerr << "approval reset request round trip failed\n";
			failures++;
		}
		auto truncatedResetRequest = resetRequestFrame;
		truncatedResetRequest.pop_back();
		if (xucrun::broker::DeserializeApprovalResetRequest(
			truncatedResetRequest, parsedResetRequest))
		{
			std::cerr << "truncated approval reset request accepted\n";
			failures++;
		}
		xucrun::broker::ApprovalResetResult resetResult;
		resetResult.requestId = resetRequest.requestId;
		resetResult.status = xucrun::broker::ApprovalManagementStatus::Succeeded;
		resetResult.deletedCount = 3;
		const auto resetResultFrame =
			xucrun::broker::SerializeApprovalResetResult(resetResult);
		xucrun::broker::ApprovalResetResult parsedResetResult;
		if (!xucrun::broker::DeserializeApprovalResetResult(
			resetResultFrame, parsedResetResult) || parsedResetResult.deletedCount != 3)
		{
			std::cerr << "approval reset result round trip failed\n";
			failures++;
		}
	}

	// Tests the round trip, entry limits, and status-dependent emptiness of the credential list.
	void ExpectCredentialListSerialization()
	{
		xucrun::broker::CredentialListRequest request;
		request.requestId = 0x1020304050607080ULL;
		const std::vector<std::uint8_t> requestFrame =
			xucrun::broker::SerializeCredentialListRequest(request);
		xucrun::broker::CredentialListRequest parsedRequest;
		if (requestFrame.size() != xucrun::broker::CredentialListRequestSize ||
			!xucrun::broker::DeserializeCredentialListRequest(requestFrame, parsedRequest) ||
			parsedRequest.requestId != request.requestId)
		{
			std::cerr << "credential list request serialization failed\n";
			failures++;
		}

		xucrun::broker::CredentialListResult result;
		result.requestId = request.requestId;
		result.status = xucrun::broker::CredentialListStatus::Succeeded;
		result.entries.push_back({
			L"S-1-5-21-1-2-3-1001", L"TEST\\Benutzer", 0x1122334455667788ULL });
		const std::vector<std::uint8_t> resultFrame =
			xucrun::broker::SerializeCredentialListResult(result);
		xucrun::broker::CredentialListResult parsedResult;
		if (!xucrun::broker::DeserializeCredentialListResult(resultFrame, parsedResult) ||
			parsedResult.requestId != result.requestId || parsedResult.entries.size() != 1 ||
			parsedResult.entries[0].accountSid != result.entries[0].accountSid ||
			parsedResult.entries[0].canonicalAccountName !=
				result.entries[0].canonicalAccountName ||
			parsedResult.entries[0].lastWritten != result.entries[0].lastWritten)
		{
			std::cerr << "credential list result serialization failed\n";
			failures++;
		}
		std::vector<std::uint8_t> invalid = resultFrame;
		invalid.push_back(0);
		if (xucrun::broker::DeserializeCredentialListResult(invalid, parsedResult))
		{
			std::cerr << "trailing credential list result data accepted\n";
			failures++;
		}
		invalid = resultFrame;
		invalid[24] = static_cast<std::uint8_t>(
			xucrun::broker::MaximumCredentialListEntries + 1);
		if (xucrun::broker::DeserializeCredentialListResult(invalid, parsedResult))
		{
			std::cerr << "oversized credential list accepted\n";
			failures++;
		}
		result.status = xucrun::broker::CredentialListStatus::Failed;
		if (!xucrun::broker::SerializeCredentialListResult(result).empty())
		{
			std::cerr << "failed credential list with entries accepted\n";
			failures++;
		}
	}

	void ExpectAdministrativeInventorySerialization()
	{
		xucrun::broker::AdministrativeInventoryRequest request;
		request.requestId = 0x1357246813572468ULL;
		const auto requestFrame =
			xucrun::broker::SerializeAdministrativeInventoryRequest(request);
		xucrun::broker::AdministrativeInventoryRequest parsedRequest;
		if (requestFrame.size() !=
			xucrun::broker::AdministrativeInventoryRequestSize ||
			!xucrun::broker::DeserializeAdministrativeInventoryRequest(
				requestFrame, parsedRequest) ||
			parsedRequest.requestId != request.requestId)
		{
			std::cerr << "administrative inventory request round trip failed\n";
			failures++;
		}

		xucrun::broker::AdministrativeInventoryResult result;
		result.requestId = request.requestId;
		result.status = xucrun::broker::AdministrativeInventoryStatus::Succeeded;
		result.authenticityMarkerCount = 1;
		result.credentials.push_back({ L"S-1-5-21-1-2-3-1001",
			L"S-1-5-21-1-2-3-500", L"TEST\\Administrator", 12345 });
		xucrun::approval::BrokerApprovalValue approval;
		approval.programScope =
			xucrun::approval::BrokerApprovalProgramScope::General;
		approval.accountScope =
			xucrun::approval::BrokerApprovalAccountScope::AllAccounts;
		approval.lifetime = xucrun::approval::BrokerApprovalLifetime::Forever;
		approval.createdAt = 100;
		approval.ownerUserSid = L"S-1-5-21-1-2-3-1001";
		result.approvals.push_back(approval);
		const auto resultFrame =
			xucrun::broker::SerializeAdministrativeInventoryResult(result);
		xucrun::broker::AdministrativeInventoryResult parsedResult;
		if (!xucrun::broker::DeserializeAdministrativeInventoryResult(
			resultFrame, parsedResult) ||
			parsedResult.credentials.size() != 1 ||
			parsedResult.approvals.size() != 1 ||
			parsedResult.authenticityMarkerCount != 1 ||
			parsedResult.credentials[0].ownerUserSid !=
				result.credentials[0].ownerUserSid ||
			parsedResult.credentials[0].canonicalAccountName !=
				result.credentials[0].canonicalAccountName ||
			parsedResult.approvals[0].ownerUserSid != approval.ownerUserSid)
		{
			std::cerr << "administrative inventory result round trip failed\n";
			failures++;
		}

		auto invalid = resultFrame;
		invalid.push_back(0);
		if (xucrun::broker::DeserializeAdministrativeInventoryResult(
			invalid, parsedResult))
		{
			std::cerr << "trailing administrative inventory data accepted\n";
			failures++;
		}
		invalid = resultFrame;
		invalid[24] = 1;
		invalid[25] = 1;
		invalid[26] = 0;
		invalid[27] = 0;
		if (xucrun::broker::DeserializeAdministrativeInventoryResult(
			invalid, parsedResult))
		{
			std::cerr << "oversized administrative credential count accepted\n";
			failures++;
		}
		result.credentials[0].ownerUserSid.clear();
		if (!xucrun::broker::SerializeAdministrativeInventoryResult(result).empty())
		{
			std::cerr << "administrative credential without owner SID accepted\n";
			failures++;
		}
		result.credentials[0].ownerUserSid = L"S-1-5-21-1-2-3-1001";
		result.status = xucrun::broker::AdministrativeInventoryStatus::Failed;
		if (!xucrun::broker::SerializeAdministrativeInventoryResult(result).empty())
		{
			std::cerr << "failed administrative inventory with entries accepted\n";
			failures++;
		}
		result.credentials.clear();
		result.approvals.clear();
		if (!xucrun::broker::SerializeAdministrativeInventoryResult(result).empty())
		{
			std::cerr << "failed administrative inventory with marker count accepted\n";
			failures++;
		}
	}

	void ExpectAdministrativeDeleteSerialization()
	{
		xucrun::broker::AdministrativeDeleteRequest request;
		request.requestId = 0x2468135724681357ULL;
		request.ownerUserSid = L"S-1-5-21-1-2-3-1001";
		request.accountSid = L"S-1-5-21-1-2-3-500";
		const auto credentialFrame =
			xucrun::broker::SerializeAdministrativeDeleteRequest(request);
		xucrun::broker::AdministrativeDeleteRequest parsedRequest;
		if (!xucrun::broker::DeserializeAdministrativeDeleteRequest(
			credentialFrame, parsedRequest) ||
			parsedRequest.kind != xucrun::broker::AdministrativeDeleteKind::Credential ||
			parsedRequest.ownerUserSid != request.ownerUserSid ||
			parsedRequest.accountSid != request.accountSid)
		{
			std::cerr << "administrative credential delete round trip failed\n";
			failures++;
		}
		auto truncated = credentialFrame;
		truncated.pop_back();
		if (xucrun::broker::DeserializeAdministrativeDeleteRequest(
			truncated, parsedRequest))
		{
			std::cerr << "truncated administrative credential delete accepted\n";
			failures++;
		}

		request.kind = xucrun::broker::AdministrativeDeleteKind::Approval;
		request.accountSid.clear();
		request.approval.programScope =
			xucrun::approval::BrokerApprovalProgramScope::General;
		request.approval.accountScope =
			xucrun::approval::BrokerApprovalAccountScope::AllAccounts;
		request.approval.lifetime = xucrun::approval::BrokerApprovalLifetime::Forever;
		request.approval.createdAt = 100;
		request.approval.ownerUserSid = request.ownerUserSid;
		const auto approvalFrame =
			xucrun::broker::SerializeAdministrativeDeleteRequest(request);
		if (!xucrun::broker::DeserializeAdministrativeDeleteRequest(
			approvalFrame, parsedRequest) ||
			parsedRequest.kind != xucrun::broker::AdministrativeDeleteKind::Approval ||
			parsedRequest.approval.ownerUserSid != request.ownerUserSid)
		{
			std::cerr << "administrative approval delete round trip failed\n";
			failures++;
		}
		auto invalid = approvalFrame;
		invalid.push_back(0);
		if (xucrun::broker::DeserializeAdministrativeDeleteRequest(
			invalid, parsedRequest))
		{
			std::cerr << "trailing administrative delete data accepted\n";
			failures++;
		}
		truncated = approvalFrame;
		truncated.pop_back();
		if (xucrun::broker::DeserializeAdministrativeDeleteRequest(
			truncated, parsedRequest))
		{
			std::cerr << "truncated administrative approval delete accepted\n";
			failures++;
		}
		request.ownerUserSid = L"S-1-5-21-1-2-3-1002";
		if (!xucrun::broker::SerializeAdministrativeDeleteRequest(request).empty())
		{
			std::cerr << "mismatched administrative approval owner accepted\n";
			failures++;
		}

		xucrun::broker::AdministrativeDeleteResult result;
		result.requestId = 0x2468135724681357ULL;
		result.status = xucrun::broker::AdministrativeDeleteStatus::Succeeded;
		const auto resultFrame =
			xucrun::broker::SerializeAdministrativeDeleteResult(result);
		xucrun::broker::AdministrativeDeleteResult parsedResult;
		if (resultFrame.size() != xucrun::broker::AdministrativeDeleteResultSize ||
			!xucrun::broker::DeserializeAdministrativeDeleteResult(
				resultFrame, parsedResult) || parsedResult.requestId != result.requestId ||
			parsedResult.status != result.status)
		{
			std::cerr << "administrative delete result round trip failed\n";
			failures++;
		}
		result.status = xucrun::broker::AdministrativeDeleteStatus::NotFound;
		if (!xucrun::broker::SerializeAdministrativeDeleteResult(result).empty())
		{
			std::cerr << "administrative not-found result without error accepted\n";
			failures++;
		}
		result.windowsError = ERROR_NOT_FOUND;
		if (xucrun::broker::SerializeAdministrativeDeleteResult(result).size() !=
			xucrun::broker::AdministrativeDeleteResultSize)
		{
			std::cerr << "valid administrative not-found result rejected\n";
			failures++;
		}
	}

	// Tests the round trip, entry limits, and nonempty identities of the process list.
	void ExpectTrackedProcessListSerialization()
	{
		xucrun::broker::TrackedProcessListRequest request;
		request.requestId = 0x8877665544332211ULL;
		const auto requestFrame =
			xucrun::broker::SerializeTrackedProcessListRequest(request);
		xucrun::broker::TrackedProcessListRequest parsedRequest;
		if (requestFrame.size() != xucrun::broker::TrackedProcessListRequestSize ||
			!xucrun::broker::DeserializeTrackedProcessListRequest(
				requestFrame, parsedRequest) || parsedRequest.requestId != request.requestId)
		{
			std::cerr << "tracked process list request round trip failed\n";
			failures++;
		}

		xucrun::broker::TrackedProcessListResult result;
		result.requestId = request.requestId;
		result.status = xucrun::broker::CredentialListStatus::Succeeded;
		if (xucrun::broker::SerializeTrackedProcessListResult(result).size() !=
			xucrun::broker::TrackedProcessListResultHeaderSize)
		{
			std::cerr << "empty tracked process list result failed\n";
			failures++;
		}
		xucrun::broker::TrackedProcessListEntry entry;
		entry.trackingId[0] = 0x42;
		entry.trackingId[15] = 0x24;
		entry.processId = 1234;
		entry.processBaseName = L"notepad.exe";
		result.entries.push_back(entry);
		const auto resultFrame = xucrun::broker::SerializeTrackedProcessListResult(result);
		xucrun::broker::TrackedProcessListResult parsedResult;
		if (!xucrun::broker::DeserializeTrackedProcessListResult(resultFrame, parsedResult) ||
			parsedResult.requestId != result.requestId || parsedResult.entries.size() != 1 ||
			parsedResult.entries[0].trackingId != entry.trackingId ||
			parsedResult.entries[0].processId != entry.processId ||
			parsedResult.entries[0].processBaseName != entry.processBaseName)
		{
			std::cerr << "tracked process list result round trip failed\n";
			failures++;
		}
		auto invalidFrame = resultFrame;
		invalidFrame.pop_back();
		if (xucrun::broker::DeserializeTrackedProcessListResult(invalidFrame, parsedResult))
		{
			std::cerr << "truncated tracked process list result accepted\n";
			failures++;
		}
		invalidFrame = resultFrame;
		invalidFrame.push_back(0);
		if (xucrun::broker::DeserializeTrackedProcessListResult(invalidFrame, parsedResult))
		{
			std::cerr << "trailing tracked process list result data accepted\n";
			failures++;
		}
		invalidFrame = resultFrame;
		invalidFrame[24] = static_cast<std::uint8_t>(
			xucrun::broker::MaximumTrackedProcessEntries + 1);
		if (xucrun::broker::DeserializeTrackedProcessListResult(invalidFrame, parsedResult))
		{
			std::cerr << "oversized tracked process list accepted\n";
			failures++;
		}

		result.entries[0].trackingId = {};
		if (!xucrun::broker::SerializeTrackedProcessListResult(result).empty())
		{
			std::cerr << "zero tracked process id serialized\n";
			failures++;
		}
		result.entries[0] = entry;
		result.entries[0].processId = 0;
		if (!xucrun::broker::SerializeTrackedProcessListResult(result).empty())
		{
			std::cerr << "zero tracked process pid serialized\n";
			failures++;
		}
		result.entries[0] = entry;
		result.entries[0].processBaseName.clear();
		if (!xucrun::broker::SerializeTrackedProcessListResult(result).empty())
		{
			std::cerr << "empty tracked process name serialized\n";
			failures++;
		}
		result.entries[0] = entry;
		result.entries[0].processBaseName = std::wstring(L"bad\0name", 8);
		if (!xucrun::broker::SerializeTrackedProcessListResult(result).empty())
		{
			std::cerr << "embedded null tracked process name serialized\n";
			failures++;
		}
		result.entries[0] = entry;
		result.entries[0].processBaseName.assign(
			xucrun::broker::MaximumProcessBaseNameLength + 1, L'x');
		if (!xucrun::broker::SerializeTrackedProcessListResult(result).empty())
		{
			std::cerr << "oversized tracked process name serialized\n";
			failures++;
		}
		result.entries.assign(xucrun::broker::MaximumTrackedProcessEntries, entry);
		const auto maximumResultFrame =
			xucrun::broker::SerializeTrackedProcessListResult(result);
		if (maximumResultFrame.empty() ||
			!xucrun::broker::DeserializeTrackedProcessListResult(
				maximumResultFrame, parsedResult) ||
			parsedResult.entries.size() != xucrun::broker::MaximumTrackedProcessEntries)
		{
			std::cerr << "maximum tracked process list round trip failed\n";
			failures++;
		}
		result.entries.assign(xucrun::broker::MaximumTrackedProcessEntries + 1, entry);
		if (!xucrun::broker::SerializeTrackedProcessListResult(result).empty())
		{
			std::cerr << "too many tracked process entries serialized\n";
			failures++;
		}
	}

	// Tests the round trip and required fields of process registration messages.
	void ExpectTrackedProcessRegistrationSerialization()
	{
		xucrun::broker::TrackedProcessRegistrationRequest request;
		request.requestId = 0x0102030405060708ULL;
		request.processHandle = 0x1122334455667788ULL;
		request.processId = 0x12345678;
		const auto requestFrame =
			xucrun::broker::SerializeTrackedProcessRegistrationRequest(request);
		xucrun::broker::TrackedProcessRegistrationRequest parsedRequest;
		if (requestFrame.size() !=
			xucrun::broker::TrackedProcessRegistrationRequestSize ||
			!xucrun::broker::DeserializeTrackedProcessRegistrationRequest(
				requestFrame, parsedRequest) ||
			parsedRequest.requestId != request.requestId ||
			parsedRequest.processHandle != request.processHandle ||
			parsedRequest.processId != request.processId)
		{
			std::cerr << "tracked process registration request round trip failed\n";
			failures++;
		}

		auto invalidRequestFrame = requestFrame;
		invalidRequestFrame.pop_back();
		if (xucrun::broker::DeserializeTrackedProcessRegistrationRequest(
			invalidRequestFrame, parsedRequest))
		{
			std::cerr << "truncated tracked process registration request accepted\n";
			failures++;
		}
		request.processHandle = 0;
		if (!xucrun::broker::SerializeTrackedProcessRegistrationRequest(request).empty())
		{
			std::cerr << "zero tracked process registration handle serialized\n";
			failures++;
		}
		request.processHandle = 1;
		request.processId = 0;
		if (!xucrun::broker::SerializeTrackedProcessRegistrationRequest(request).empty())
		{
			std::cerr << "zero tracked process registration pid serialized\n";
			failures++;
		}

		xucrun::broker::TrackedProcessRegistrationResult result;
		result.requestId = 0x0102030405060708ULL;
		result.status = xucrun::broker::TrackedProcessRegistrationStatus::Succeeded;
		const auto resultFrame =
			xucrun::broker::SerializeTrackedProcessRegistrationResult(result);
		xucrun::broker::TrackedProcessRegistrationResult parsedResult;
		if (resultFrame.size() !=
			xucrun::broker::TrackedProcessRegistrationResultSize ||
			!xucrun::broker::DeserializeTrackedProcessRegistrationResult(
				resultFrame, parsedResult) ||
			parsedResult.requestId != result.requestId ||
			parsedResult.status != result.status)
		{
			std::cerr << "tracked process registration result round trip failed\n";
			failures++;
		}
		auto invalidResultFrame = resultFrame;
		invalidResultFrame[5] = 0xff;
		if (xucrun::broker::DeserializeTrackedProcessRegistrationResult(
			invalidResultFrame, parsedResult))
		{
			std::cerr << "unknown tracked process registration status accepted\n";
			failures++;
		}
	}

	void ExpectStatusIconListSerialization()
	{
		xucrun::broker::StatusIconListRequest request;
		request.requestId = 0x1020304050607080ULL;
		request.clientVersion = { 3, 0, 2, 212 };
		request.includeAllSessions = true;
		const auto requestFrame =
			xucrun::broker::SerializeStatusIconListRequest(request);
		xucrun::broker::StatusIconListRequest parsedRequest;
		if (requestFrame.size() != xucrun::broker::StatusIconListRequestSize ||
			!xucrun::broker::DeserializeStatusIconListRequest(
				requestFrame, parsedRequest) ||
			parsedRequest.requestId != request.requestId ||
			parsedRequest.clientVersion.build != request.clientVersion.build ||
			!parsedRequest.includeAllSessions)
		{
			std::cerr << "status icon list request round trip failed\n";
			failures++;
		}

		xucrun::broker::BrokerPolicyRejection rejection;
		rejection.requestId = request.requestId;
		const auto rejectionFrame =
			xucrun::broker::SerializeBrokerPolicyRejection(rejection);
		xucrun::broker::BrokerPolicyRejection parsedRejection;
		if (rejectionFrame.size() !=
				xucrun::broker::BrokerPolicyRejectionSize ||
			!xucrun::broker::DeserializeBrokerPolicyRejection(
				rejectionFrame, parsedRejection) ||
			parsedRejection.requestId != rejection.requestId ||
			parsedRejection.windowsError !=
				xucrun::broker::PolicyDeniedWindowsError)
		{
			std::cerr << "broker policy rejection round trip failed\n";
			failures++;
		}

		xucrun::broker::StatusIconListResult result;
		result.requestId = request.requestId;
		result.status = xucrun::broker::CredentialListStatus::Succeeded;
		result.entries.push_back({ 1234, true, { 3, 0, 2, 212 },
			L"S-1-5-21-1", L"DOMAIN\\User" });
		const auto resultFrame =
			xucrun::broker::SerializeStatusIconListResult(result);
		xucrun::broker::StatusIconListResult parsedResult;
		if (resultFrame.empty() ||
			!xucrun::broker::DeserializeStatusIconListResult(
				resultFrame, parsedResult) ||
			parsedResult.entries.size() != 1 ||
			parsedResult.entries[0].processId != 1234 ||
			!parsedResult.entries[0].elevated ||
			parsedResult.entries[0].accountSid != L"S-1-5-21-1" ||
			parsedResult.entries[0].canonicalAccountName != L"DOMAIN\\User")
		{
			std::cerr << "status icon list result round trip failed\n";
			failures++;
		}
		auto invalidFrame = resultFrame;
		invalidFrame.pop_back();
		if (xucrun::broker::DeserializeStatusIconListResult(
			invalidFrame, parsedResult))
		{
			std::cerr << "truncated status icon list result accepted\n";
			failures++;
		}
		const auto entry = result.entries.front();
		result.entries.assign(xucrun::broker::MaximumStatusIconEntries + 1, entry);
		if (!xucrun::broker::SerializeStatusIconListResult(result).empty())
		{
			std::cerr << "oversized status icon list serialized\n";
			failures++;
		}
		request.clientVersion = {};
		if (!xucrun::broker::SerializeStatusIconListRequest(request).empty())
		{
			std::cerr << "versionless status icon list request serialized\n";
			failures++;
		}
	}

	// Tests the round trip and status-dependent consistency of findings and the report.
	void ExpectSecurityAuditSerialization()
	{
		xucrun::broker::SecurityAuditRequest request;
		request.requestId = 903;
		request.language = xucrun::broker::SecurityAuditRequest::Language::German;
		const auto requestFrame = xucrun::broker::SerializeSecurityAuditRequest(request);
		xucrun::broker::SecurityAuditRequest parsedRequest;
		if (requestFrame.size() != xucrun::broker::SecurityAuditRequestSize ||
			!xucrun::broker::DeserializeSecurityAuditRequest(requestFrame, parsedRequest) ||
			parsedRequest.requestId != request.requestId ||
			parsedRequest.language != request.language)
		{
			std::cerr << "security audit request round trip failed\n";
			failures++;
		}
		auto invalidLanguageFrame = requestFrame;
		invalidLanguageFrame[5] = 0xff;
		if (xucrun::broker::DeserializeSecurityAuditRequest(
			invalidLanguageFrame, parsedRequest))
		{
			std::cerr << "unknown security audit language accepted\n";
			failures++;
		}

		xucrun::broker::SecurityAuditResult result;
		result.requestId = request.requestId;
		result.status = xucrun::broker::SecurityAuditStatus::Succeeded;
		result.findingCount = 1;
		result.report = L"Object: C:\\Test\nProblem: Write permission\nRemediation: Correct the ACL";
		const auto resultFrame = xucrun::broker::SerializeSecurityAuditResult(result);
		xucrun::broker::SecurityAuditResult parsedResult;
		if (!xucrun::broker::DeserializeSecurityAuditResult(resultFrame, parsedResult) ||
			parsedResult.requestId != result.requestId ||
			parsedResult.findingCount != result.findingCount ||
			parsedResult.report != result.report)
		{
			std::cerr << "security audit result round trip failed\n";
			failures++;
		}
		auto invalid = resultFrame;
		invalid.pop_back();
		if (xucrun::broker::DeserializeSecurityAuditResult(invalid, parsedResult))
		{
			std::cerr << "truncated security audit result accepted\n";
			failures++;
		}
		invalid = resultFrame;
		invalid[5] = 0xFF;
		if (xucrun::broker::DeserializeSecurityAuditResult(invalid, parsedResult))
		{
			std::cerr << "unknown security audit status accepted\n";
			failures++;
		}
		result.report.assign(xucrun::broker::MaximumSecurityAuditReportLength + 1, L'x');
		if (!xucrun::broker::SerializeSecurityAuditResult(result).empty())
		{
			std::cerr << "oversized security audit report serialized\n";
			failures++;
		}
	}

	// Tests the round trip and complete validation of broker stop messages.
	void ExpectBrokerStopSerialization()
	{
		xucrun::broker::BrokerStopRequest request;
		request.requestId = 0x0102030405060708ULL;
		request.action = xucrun::broker::BrokerServiceControlAction::Disable;
		if (static_cast<std::uint8_t>(request.action) != 1)
		{
			std::cerr << "broker disable action wire value changed\n";
			failures++;
		}
		const auto requestFrame = xucrun::broker::SerializeBrokerStopRequest(request);
		xucrun::broker::BrokerStopRequest parsedRequest;
		if (requestFrame.size() != xucrun::broker::BrokerStopRequestSize ||
			!xucrun::broker::DeserializeBrokerStopRequest(requestFrame, parsedRequest) ||
			parsedRequest.requestId != request.requestId ||
			parsedRequest.action != request.action)
		{
			std::cerr << "broker stop request round trip failed\n";
			failures++;
		}
		auto invalidRequest = requestFrame;
		invalidRequest[5] = 0xff;
		if (xucrun::broker::DeserializeBrokerStopRequest(invalidRequest, parsedRequest))
		{
			std::cerr << "unknown broker service control action accepted\n";
			failures++;
		}

		xucrun::broker::BrokerStopResult result;
		result.requestId = request.requestId;
		result.status = xucrun::broker::BrokerStopStatus::Accepted;
		const auto resultFrame = xucrun::broker::SerializeBrokerStopResult(result);
		xucrun::broker::BrokerStopResult parsedResult;
		if (resultFrame.size() != xucrun::broker::BrokerStopResultSize ||
			!xucrun::broker::DeserializeBrokerStopResult(resultFrame, parsedResult) ||
			parsedResult.requestId != result.requestId ||
			parsedResult.status != result.status)
		{
			std::cerr << "broker stop result round trip failed\n";
			failures++;
		}
		auto invalid = resultFrame;
		invalid[5] = 0xff;
		if (xucrun::broker::DeserializeBrokerStopResult(invalid, parsedResult))
		{
			std::cerr << "unknown broker stop status accepted\n";
			failures++;
		}
	}

	// Tests canonical approval round trips and rejection of inconsistent values.
	void ExpectApprovalSerialization()
	{
		using xucrun::approval::ApprovalMode;
		constexpr std::uint64_t approvalTime = 0x0102030405060708ULL;
		const std::vector<std::uint8_t> expected = {
			1, 1, 8, 7, 6, 5, 4, 3, 2, 1
		};
		std::vector<std::uint8_t> value =
			xucrun::approval::SerializeGeneralApproval(ApprovalMode::AskAt, approvalTime);
		if (value != expected)
		{
			std::cerr << "approval byte format changed\n";
			failures++;
		}

		ApprovalMode parsedMode = ApprovalMode::Forever;
		std::uint64_t parsedTime = 0;
		if (!xucrun::approval::DeserializeGeneralApproval(value, parsedMode, parsedTime) ||
			parsedMode != ApprovalMode::AskAt || parsedTime != approvalTime)
		{
			std::cerr << "approval round trip failed\n";
			failures++;
		}

		value[0]++;
		if (xucrun::approval::DeserializeGeneralApproval(value, parsedMode, parsedTime))
		{
			std::cerr << "unknown approval version accepted\n";
			failures++;
		}

		value = xucrun::approval::SerializeGeneralApproval(ApprovalMode::AskAt, 0);
		if (xucrun::approval::DeserializeGeneralApproval(value, parsedMode, parsedTime))
		{
			std::cerr << "zero AskAt timestamp accepted\n";
			failures++;
		}

		value = xucrun::approval::SerializeGeneralApproval(ApprovalMode::Forever, 0);
		if (!xucrun::approval::DeserializeGeneralApproval(value, parsedMode, parsedTime) ||
			parsedMode != ApprovalMode::Forever || parsedTime != 0)
		{
			std::cerr << "permanent approval rejected\n";
			failures++;
		}

		const std::wstring caller = L"C:\\Tools\\caller.exe";
		const std::wstring target = L"C:\\Windows\\target.exe";
		std::vector<std::uint8_t> scoped(
			xucrun::approval::ScopedApprovalHeaderSize + (caller.size() + target.size()) * 2);
		scoped[0] = xucrun::approval::ScopedApprovalVersion;
		scoped[106] = static_cast<std::uint8_t>(caller.size());
		scoped[107] = static_cast<std::uint8_t>(caller.size() >> 8);
		scoped[108] = static_cast<std::uint8_t>(target.size());
		scoped[109] = static_cast<std::uint8_t>(target.size() >> 8);
		auto writePath = [&scoped](std::size_t offset, const std::wstring& path)
		{
			for (std::size_t index = 0; index < path.size(); index++)
			{
				scoped[offset + index * 2] = static_cast<std::uint8_t>(path[index]);
				scoped[offset + index * 2 + 1] = static_cast<std::uint8_t>(path[index] >> 8);
			}
		};
		writePath(xucrun::approval::ScopedApprovalHeaderSize, caller);
		writePath(xucrun::approval::ScopedApprovalHeaderSize + caller.size() * 2, target);

		std::wstring parsedCaller;
		std::wstring parsedTarget;
		if (!xucrun::approval::DeserializeScopedApprovalPaths(
			scoped, parsedCaller, parsedTarget) ||
			parsedCaller != caller || parsedTarget != target)
		{
			std::cerr << "scoped approval paths failed\n";
			failures++;
		}

		const std::uint8_t firstCallerByte = scoped[xucrun::approval::ScopedApprovalHeaderSize];
		scoped[xucrun::approval::ScopedApprovalHeaderSize] = 0;
		if (xucrun::approval::DeserializeScopedApprovalPaths(
			scoped, parsedCaller, parsedTarget))
		{
			std::cerr << "null character in scoped path accepted\n";
			failures++;
		}
		scoped[xucrun::approval::ScopedApprovalHeaderSize] = firstCallerByte;

		scoped[0] = 4;
		if (xucrun::approval::DeserializeScopedApprovalPaths(
			scoped, parsedCaller, parsedTarget))
		{
			std::cerr << "unknown scoped approval version accepted\n";
			failures++;
		}
		scoped[0] = xucrun::approval::GlobalScopedApprovalVersion;
		scoped.pop_back();
		if (xucrun::approval::DeserializeScopedApprovalPaths(
			scoped, parsedCaller, parsedTarget))
		{
			std::cerr << "truncated scoped approval accepted\n";
			failures++;
		}

		xucrun::approval::ScopedApprovalValue approval;
		approval.kind = xucrun::approval::ScopedApprovalKind::CurrentAccount;
		approval.mode = ApprovalMode::AskAt;
		approval.approvalTime = approvalTime;
		approval.callerPath = L"A";
		approval.targetPath = L"BC";
		for (std::size_t index = 0; index < approval.callerHash.size(); index++)
		{
			approval.callerHash[index] = static_cast<std::uint8_t>(index);
			approval.targetHash[index] = static_cast<std::uint8_t>(32 + index);
			approval.ownerHash[index] = static_cast<std::uint8_t>(64 + index);
		}

		std::vector<std::uint8_t> serialized =
			xucrun::approval::SerializeScopedApproval(approval, 1024);
		bool fixedFormatMatches = serialized.size() == 116 &&
			serialized[0] == 5 && serialized[1] == 1 &&
			serialized[2] == 8 && serialized[9] == 1 &&
			serialized[106] == 1 && serialized[107] == 0 &&
			serialized[108] == 2 && serialized[109] == 0 &&
			serialized[110] == 'A' && serialized[111] == 0 &&
			serialized[112] == 'B' && serialized[113] == 0 &&
			serialized[114] == 'C' && serialized[115] == 0;
		for (std::size_t index = 0; fixedFormatMatches && index < approval.callerHash.size(); index++)
		{
			fixedFormatMatches = serialized[10 + index] == index &&
				serialized[42 + index] == 32 + index &&
				serialized[74 + index] == 64 + index;
		}
		if (!fixedFormatMatches)
		{
			std::cerr << "scoped approval byte format changed\n";
			failures++;
		}

		xucrun::approval::ScopedApprovalValue parsedApproval;
		if (!xucrun::approval::DeserializeScopedApproval(serialized, parsedApproval) ||
			parsedApproval.kind != approval.kind || parsedApproval.mode != approval.mode ||
			parsedApproval.approvalTime != approval.approvalTime ||
			parsedApproval.callerHash != approval.callerHash ||
			parsedApproval.targetHash != approval.targetHash ||
			parsedApproval.ownerHash != approval.ownerHash ||
			parsedApproval.callerPath != approval.callerPath ||
			parsedApproval.targetPath != approval.targetPath)
		{
			std::cerr << "scoped approval round trip failed\n";
			failures++;
		}

		approval.kind = xucrun::approval::ScopedApprovalKind::AllAccounts;
		serialized = xucrun::approval::SerializeScopedApproval(approval, 1024);
		if (serialized.empty() || serialized[0] != 6 ||
			!xucrun::approval::DeserializeScopedApproval(serialized, parsedApproval) ||
			parsedApproval.kind != xucrun::approval::ScopedApprovalKind::AllAccounts)
		{
			std::cerr << "global scoped approval version failed\n";
			failures++;
		}

		if (!xucrun::approval::SerializeScopedApproval(approval, serialized.size() - 1).empty())
		{
			std::cerr << "scoped approval size limit ignored\n";
			failures++;
		}
		serialized[1] = 99;
		if (xucrun::approval::DeserializeScopedApproval(serialized, parsedApproval))
		{
			std::cerr << "unknown scoped approval mode accepted\n";
			failures++;
		}

		xucrun::approval::BrokerApprovalValue brokerApproval;
		brokerApproval.programScope =
			xucrun::approval::BrokerApprovalProgramScope::TargetProgram;
		brokerApproval.accountScope =
			xucrun::approval::BrokerApprovalAccountScope::CurrentAccount;
		brokerApproval.lifetime = xucrun::approval::BrokerApprovalLifetime::Until;
		brokerApproval.elevationMode = 1;
		brokerApproval.createdAt = approvalTime;
		brokerApproval.expiresAt = approvalTime + 100;
		brokerApproval.ownerUserSid = L"S-1-5-21-1-2-3-1001";
		brokerApproval.targetAccountSid = L"S-1-5-21-1-2-3-1002";
		brokerApproval.callerPath = L"C:\\Tools\\xucRun.exe";
		brokerApproval.targetPath = L"C:\\Windows\\notepad.exe";
		for (std::size_t index = 0; index < brokerApproval.callerHash.size(); index++)
		{
			brokerApproval.callerHash[index] = static_cast<std::uint8_t>(index);
			brokerApproval.targetHash[index] = static_cast<std::uint8_t>(32 + index);
		}
		std::vector<std::uint8_t> brokerSerialized =
			xucrun::approval::SerializeBrokerApproval(brokerApproval, 4096);
		xucrun::approval::BrokerApprovalValue parsedBrokerApproval;
		if (brokerSerialized.empty() || brokerSerialized[0] != 8 ||
			!xucrun::approval::DeserializeBrokerApproval(
				brokerSerialized, parsedBrokerApproval) ||
			parsedBrokerApproval.programScope != brokerApproval.programScope ||
			parsedBrokerApproval.accountScope != brokerApproval.accountScope ||
			parsedBrokerApproval.lifetime != brokerApproval.lifetime ||
			parsedBrokerApproval.elevationMode != brokerApproval.elevationMode ||
			parsedBrokerApproval.createdAt != brokerApproval.createdAt ||
			parsedBrokerApproval.expiresAt != brokerApproval.expiresAt ||
			parsedBrokerApproval.callerHash != brokerApproval.callerHash ||
			parsedBrokerApproval.targetHash != brokerApproval.targetHash ||
			parsedBrokerApproval.ownerUserSid != brokerApproval.ownerUserSid ||
			parsedBrokerApproval.targetAccountSid != brokerApproval.targetAccountSid ||
			parsedBrokerApproval.sessionLogonSid != brokerApproval.sessionLogonSid ||
			parsedBrokerApproval.callerPath != brokerApproval.callerPath ||
			parsedBrokerApproval.targetPath != brokerApproval.targetPath)
		{
			std::cerr << "broker approval round trip failed\n";
			failures++;
		}

		std::vector<std::uint8_t> invalidBroker = brokerSerialized;
		invalidBroker[0] = 7;
		if (xucrun::approval::DeserializeBrokerApproval(
			invalidBroker, parsedBrokerApproval))
		{
			std::cerr << "broker approval version 7 accepted\n";
			failures++;
		}
		invalidBroker = brokerSerialized;
		invalidBroker.pop_back();
		if (xucrun::approval::DeserializeBrokerApproval(
			invalidBroker, parsedBrokerApproval))
		{
			std::cerr << "truncated broker approval accepted\n";
			failures++;
		}
		brokerApproval.lifetime = xucrun::approval::BrokerApprovalLifetime::LogonSession;
		brokerApproval.expiresAt = 0;
		if (!xucrun::approval::SerializeBrokerApproval(brokerApproval, 4096).empty())
		{
			std::cerr << "session approval without logon SID accepted\n";
			failures++;
		}

		brokerApproval.lifetime = xucrun::approval::BrokerApprovalLifetime::Until;
		brokerApproval.expiresAt = approvalTime + 100;
		if (xucrun::approval::IsBrokerApprovalExpired(
			brokerApproval, approvalTime + 99, L"") ||
			!xucrun::approval::IsBrokerApprovalExpired(
				brokerApproval, approvalTime + 100, L""))
		{
			std::cerr << "broker approval expiration boundary failed\n";
			failures++;
		}
		brokerApproval.lifetime =
			xucrun::approval::BrokerApprovalLifetime::LogonSession;
		brokerApproval.sessionLogonSid = L"S-1-5-5-1-2";
		if (xucrun::approval::IsBrokerApprovalExpired(
				brokerApproval, approvalTime, L"S-1-5-5-1-2") ||
			!xucrun::approval::IsBrokerApprovalExpired(
				brokerApproval, approvalTime, L"S-1-5-5-3-4"))
		{
			std::cerr << "broker approval logon-session expiration failed\n";
			failures++;
		}
	}

	// Tests the expected formats and concrete binding values of target name construction.
	void ExpectTargetNaming()
	{
		const std::vector<std::uint8_t> hexBytes = { 0x00, 0x0F, 0x10, 0xFF };
		if (xucrun::targets::BytesToUpperHex(hexBytes) != L"000F10FF")
		{
			std::cerr << "uppercase hex format changed\n";
			failures++;
		}

		std::vector<std::uint8_t> hash(32, 0xFF);
		if (xucrun::targets::HashToDecimalTargetName(hash) !=
			L"1844674407370955161518446744073709551615")
		{
			std::cerr << "decimal target format changed\n";
			failures++;
		}
		hash.resize(15);
		if (!xucrun::targets::HashToDecimalTargetName(hash).empty())
		{
			std::cerr << "short target hash accepted\n";
			failures++;
		}

		const std::vector<std::uint8_t> callerHash(32, 0xAB);
		const std::vector<std::uint8_t> targetHash(32, 0x01);
		const std::wstring expectedHashes =
			L"ABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABAB|"
			L"0101010101010101010101010101010101010101010101010101010101010101";
		const std::wstring currentInput = xucrun::targets::BuildScopedApprovalHmacInput(
			xucrun::approval::ScopedApprovalKind::CurrentAccount,
			L"123", L"C", L"TT", callerHash, targetHash);
		const std::wstring globalInput = xucrun::targets::BuildScopedApprovalHmacInput(
			xucrun::approval::ScopedApprovalKind::AllAccounts,
			L"ignored", L"C", L"TT", callerHash, targetHash);
		if (currentInput != L"scoped-approval-v5|123|1:C|2:TT|" + expectedHashes ||
			globalInput != L"scoped-approval-v6|all-accounts|1:C|2:TT|" + expectedHashes)
		{
			std::cerr << "scoped HMAC input format changed\n";
			failures++;
		}

		if (xucrun::targets::BuildScopedOwnerHmacInput(
			xucrun::approval::ScopedApprovalKind::CurrentAccount, L"123") !=
			L"scoped-approval-owner-v5|123" ||
			xucrun::targets::BuildScopedOwnerHmacInput(
			xucrun::approval::ScopedApprovalKind::AllAccounts, L"ignored") !=
			L"scoped-approval-owner-v6|all-accounts")
		{
			std::cerr << "approval owner HMAC input format changed\n";
			failures++;
		}
	}
}

// Runs all core contract tests and reports their overall result.
int main()
{
	ExpectBrokerBackupArchiveSerialization();
	ExpectBrokerBackupContractSerialization();
	LaunchRequest request = MakeValidRequest();
	ExpectValidation("valid request", request, ContractValidationError::None);

	request = MakeValidRequest();
	request.protocolVersion++;
	ExpectValidation("protocol version", request,
		ContractValidationError::UnsupportedProtocolVersion);

	request = MakeValidRequest();
	request.requestId = 0;
	ExpectValidation("request id", request, ContractValidationError::MissingRequestId);

	request = MakeValidRequest();
	request.requestedAccountSid.clear();
	ExpectValidation("account SID", request, ContractValidationError::MissingAccountSid);

	request = MakeValidRequest();
	request.requestedAccountSid.assign(xucrun::broker::MaximumAccountSidLength + 1, L'x');
	ExpectValidation("account SID length", request, ContractValidationError::AccountSidTooLong);

	request = MakeValidRequest();
	request.executablePath.clear();
	ExpectValidation("executable path", request,
		ContractValidationError::MissingExecutablePath);

	request = MakeValidRequest();
	request.executablePath.assign(xucrun::broker::MaximumPathLength + 1, L'x');
	ExpectValidation("executable path length", request,
		ContractValidationError::ExecutablePathTooLong);

	request = MakeValidRequest();
	request.argumentTail.assign(xucrun::broker::MaximumArgumentLength + 1, L'x');
	ExpectValidation("argument length", request, ContractValidationError::ArgumentsTooLong);

	request = MakeValidRequest();
	request.workingDirectory.assign(xucrun::broker::MaximumPathLength + 1, L'x');
	ExpectValidation("working directory length", request,
		ContractValidationError::WorkingDirectoryTooLong);

	ExpectBrokerTimeoutContracts();
	if (!xucrun::secure_desktop::RunSecurityDescriptorSelfTest())
	{
		std::cerr << "secure desktop security descriptor test failed\n";
		failures++;
	}
	if (!xucrun::secure_desktop::RunCancellationWaitSelfTest())
	{
		std::cerr << "secure desktop cancellation wait test failed\n";
		failures++;
	}
	ExpectClientCommandLine();
	ExpectClientProcessContext();
	ExpectFileIdentity();
	ExpectApprovalSerialization();
	ExpectTargetNaming();
	ExpectLaunchRequestConstruction();
	ExpectLaunchRequestSerialization();
	ExpectLaunchResultSerialization();
	ExpectHealthSerialization();
	ExpectCredentialMetadataSerialization();
	ExpectSessionTestSerialization();
	ExpectCredentialStoreTestSerialization();
	ExpectSensitiveString();
	ExpectCredentialSaveSerialization();
	ExpectCredentialCaptureSerialization();
	ExpectCredentialDeleteSerialization();
	ExpectCredentialUseTestSerialization();
	ExpectApprovalChallengeSerialization();
	ExpectApprovalConfirmationSerialization();
	ExpectApprovalHelperSerialization();
	ExpectApprovalManagementSerialization();
	ExpectCredentialListSerialization();
	ExpectAdministrativeInventorySerialization();
	ExpectAdministrativeDeleteSerialization();
	ExpectTrackedProcessListSerialization();
	ExpectTrackedProcessRegistrationSerialization();
	ExpectStatusIconListSerialization();
	ExpectSecurityAuditSerialization();
	ExpectBrokerStopSerialization();

	if (failures != 0)
		return 1;

	std::cout << "xucRunCore tests passed\n";
	return 0;
}