// Purpose: Prepares normalized target, argument, and working-directory data for elevated wrapper launch.
#include "ClientElevatedWrapperPreparation.h"

#include <Windows.h>

#include <algorithm>
#include <cwctype>
#include <string>

#include "ClientCommandLine.h"
#include "ClientLogging.h"

namespace xucrun::elevated_wrapper
{
	ElevatedWrapperLaunchPreparation PrepareElevatedWrapperLaunch(
		const std::wstring& targetProgram,
		const std::wstring& targetArguments,
		const std::wstring& workingDirectory,
		bool shouldElevate)
	{
		std::wstring preparedArguments = targetArguments;
		std::wstring preparedWorkingDirectory = workingDirectory;
		const std::wstring& safeTargetProgram = targetProgram;

		std::wstring targetBasename = targetProgram;
		const size_t lastSlash = targetProgram.find_last_of(L"\\/");
		if (lastSlash != std::wstring::npos)
			targetBasename = targetProgram.substr(lastSlash + 1);

		std::wstring lowerTargetBasename = targetBasename;
		std::transform(lowerTargetBasename.begin(), lowerTargetBasename.end(),
			lowerTargetBasename.begin(), ::towlower);

		if ((lowerTargetBasename == L"cmd" || lowerTargetBasename == L"cmd.exe") &&
			!preparedArguments.empty())
		{
			client_logging::Write(L"Info", L"[Wrapper mode] cmd detected, checking parameters for batch files with metacharacters");
			size_t cmdSwitchPos = std::wstring::npos;
			for (size_t position = 0; position + 1 < preparedArguments.size(); position++)
			{
				if (preparedArguments[position] == L'/' &&
					(std::towlower(preparedArguments[position + 1]) == L'c' ||
					 std::towlower(preparedArguments[position + 1]) == L'k') &&
					(position == 0 || std::iswspace(preparedArguments[position - 1])) &&
					(position + 2 == preparedArguments.size() ||
					 std::iswspace(preparedArguments[position + 2])))
				{
					cmdSwitchPos = position;
					break;
				}
			}

			if (cmdSwitchPos != std::wstring::npos)
			{
				size_t fileStartPos = cmdSwitchPos + 2;
				while (fileStartPos < preparedArguments.length() &&
					std::iswspace(preparedArguments[fileStartPos]))
					fileStartPos++;

				std::wstring batchFileName;
				std::wstring trailingBatchArguments;
				if (fileStartPos < preparedArguments.length())
				{
					if (preparedArguments[fileStartPos] == L'"')
					{
						const size_t quoteEndPos = preparedArguments.find(L'"', fileStartPos + 1);
						if (quoteEndPos != std::wstring::npos)
						{
							batchFileName = preparedArguments.substr(fileStartPos + 1,
								quoteEndPos - fileStartPos - 1);
							const size_t argumentsStart = preparedArguments.find_first_not_of(
								L" \t", quoteEndPos + 1);
							if (argumentsStart != std::wstring::npos)
								trailingBatchArguments = preparedArguments.substr(argumentsStart);
						}
					}
					else
					{
						const size_t spacePos = preparedArguments.find_first_of(
							L" \t", fileStartPos);
						batchFileName = spacePos != std::wstring::npos
							? preparedArguments.substr(fileStartPos, spacePos - fileStartPos)
							: preparedArguments.substr(fileStartPos);
						if (spacePos != std::wstring::npos)
						{
							const size_t argumentsStart = preparedArguments.find_first_not_of(
								L" \t", spacePos);
							if (argumentsStart != std::wstring::npos)
								trailingBatchArguments = preparedArguments.substr(argumentsStart);
						}
					}
				}

				if (!batchFileName.empty())
				{
					std::wstring lowerBatchFileName = batchFileName;
					std::transform(lowerBatchFileName.begin(), lowerBatchFileName.end(),
						lowerBatchFileName.begin(), ::towlower);
					if (lowerBatchFileName.ends_with(L".bat") || lowerBatchFileName.ends_with(L".cmd"))
					{
						client_logging::Write(L"Info", L"[Wrapper mode] Batch file found: " + batchFileName);
						std::wstring absoluteBatchFileName = batchFileName;
						const bool isAbsolute = command_line::IsAbsolutePath(batchFileName);
						client_logging::Write(L"Info", L"[Wrapper mode] IsAbsolutePath(\"" + batchFileName + L"\") = " +
							(isAbsolute ? L"true" : L"false"));
						client_logging::Write(L"Info", L"[Wrapper mode] workingDirectory = \"" +
							preparedWorkingDirectory + L"\"");

						if (!isAbsolute && !preparedWorkingDirectory.empty())
						{
							absoluteBatchFileName = preparedWorkingDirectory;
							if (absoluteBatchFileName.back() != L'\\' && absoluteBatchFileName.back() != L'/')
								absoluteBatchFileName += L'\\';
							absoluteBatchFileName += batchFileName;
							client_logging::Write(L"Info", L"[Wrapper mode] Relative path detected, converted to: " +
								absoluteBatchFileName);
						}

						const std::wstring& safeBatchFileName = absoluteBatchFileName;
						std::wstring finalBatchFileName;
						bool batchFileWasReplaced = false;
						if (safeBatchFileName != absoluteBatchFileName)
						{
							client_logging::Write(L"Info", L"[Wrapper mode] Replacing batch file in parameters");
							client_logging::Write(L"Info", L"[Wrapper mode] Original: " + batchFileName);
							client_logging::Write(L"Info", L"[Wrapper mode] Safe: " + safeBatchFileName);
							finalBatchFileName = safeBatchFileName;
							batchFileWasReplaced = true;
						}
						else if (absoluteBatchFileName != batchFileName)
						{
							client_logging::Write(L"Info", L"[Wrapper mode] Replacing relative path with absolute path");
							client_logging::Write(L"Info", L"[Wrapper mode] Original: " + batchFileName);
							client_logging::Write(L"Info", L"[Wrapper mode] Absolute: " + absoluteBatchFileName);
							finalBatchFileName = absoluteBatchFileName;
							batchFileWasReplaced = true;
						}

						if (batchFileWasReplaced)
						{
							if (cmdSwitchPos != std::wstring::npos)
							{
								const std::wstring batchDirectory = absoluteBatchFileName.substr(
									0, absoluteBatchFileName.find_last_of(L"\\/"));
								client_logging::Write(L"Info", L"[Wrapper mode] Setting working directory to batch directory: " +
									batchDirectory);
								preparedWorkingDirectory = batchDirectory;
								const std::wstring commandPrefix = preparedArguments.substr(
									0, cmdSwitchPos + 2);
								preparedArguments = commandPrefix + L" \"" +
									finalBatchFileName + L"\"";
								if (!trailingBatchArguments.empty())
									preparedArguments += L" " + trailingBatchArguments;
								client_logging::Write(L"Info", L"[Wrapper mode] New parameters (without cd /d): " +
									preparedArguments);
							}
						}
					}
				}
			}
		}

		if ((lowerTargetBasename == L"cmd" || lowerTargetBasename == L"cmd.exe" ||
			 lowerTargetBasename == L"tcc" || lowerTargetBasename == L"tcc.exe" ||
			 lowerTargetBasename == L"tcmd" || lowerTargetBasename == L"tcmd.exe") &&
			!preparedWorkingDirectory.empty())
		{
			const std::wstring injectedArguments = command_line::InjectShellWorkingDirectory(
				targetProgram, preparedArguments, preparedWorkingDirectory);
			if (injectedArguments != preparedArguments)
			{
				preparedArguments = injectedArguments;
				client_logging::Write(L"Info", L"[Wrapper mode] Injected cmd/TCC working directory: " +
					preparedArguments);
			}
		}

		if ((lowerTargetBasename == L"powershell" || lowerTargetBasename == L"powershell.exe" ||
			 lowerTargetBasename == L"pwsh" || lowerTargetBasename == L"pwsh.exe") &&
			!preparedWorkingDirectory.empty() && !preparedArguments.empty() &&
			(preparedArguments.find(L"-NoExit") != std::wstring::npos ||
			 preparedArguments.find(L"-noexit") != std::wstring::npos))
		{
			client_logging::Write(L"Info", L"[Wrapper mode] PowerShell with -NoExit detected, injecting Set-Location");
			std::wstring escapedWorkDir = preparedWorkingDirectory;
			size_t pos = 0;
			while ((pos = escapedWorkDir.find(L"'", pos)) != std::wstring::npos)
			{
				escapedWorkDir.replace(pos, 1, L"''");
				pos += 2;
			}

			const std::wstring setLocationCmd = L"Set-Location '" + escapedWorkDir + L"'";
			size_t commandPos = preparedArguments.find(L"-Command");
			if (commandPos == std::wstring::npos)
				commandPos = preparedArguments.find(L"-command");
			if (commandPos != std::wstring::npos)
			{
				size_t commandEndPos = commandPos + 8;
				while (commandEndPos < preparedArguments.length() && preparedArguments[commandEndPos] == L' ')
					commandEndPos++;
				const std::wstring beforeCommand = preparedArguments.substr(0, commandEndPos);
				const std::wstring commandAndRest = preparedArguments.substr(commandEndPos);
				preparedArguments = beforeCommand + setLocationCmd + L"; " + commandAndRest;
			}
			else
			{
				preparedArguments = L"-NoExit -Command \"" + setLocationCmd + L"\"";
			}
			client_logging::Write(L"Info", L"[Wrapper mode] Rebuilt PowerShell command line for working directory");
			client_logging::Write(L"Info", L"[Wrapper mode] New parameters: " + preparedArguments);
		}

		client_logging::Write(L"Info", std::wstring(L"[Wrapper mode] Launching ") +
			(shouldElevate ? L"with elevation: " : L"without additional elevation: ") + targetProgram);
		if (!preparedArguments.empty())
			client_logging::Write(L"Info", L"[Wrapper mode] Parameters are present");
		if (!preparedWorkingDirectory.empty())
			client_logging::Write(L"Info", L"[Wrapper mode] Working directory: " + preparedWorkingDirectory);

		if (shouldElevate)
		{
			client_logging::Write(L"Ok", L"[Wrapper mode] Account check: current account is a member of the Administrators group");
			client_logging::Write(L"Info", L"[Wrapper mode] UAC elevation will be possible (after user confirmation)");
		}
		else
		{
			client_logging::Write(L"Info", L"[Wrapper mode] Target program starts without requested UAC elevation");
		}

		ElevatedWrapperLaunchPreparation preparation{
			safeTargetProgram, preparedArguments, preparedWorkingDirectory };
		std::wstring lowerTargetProgram = safeTargetProgram;
		std::transform(lowerTargetProgram.begin(), lowerTargetProgram.end(),
			lowerTargetProgram.begin(), ::towlower);
		if (lowerTargetProgram.ends_with(L".bat") || lowerTargetProgram.ends_with(L".cmd"))
		{
			wchar_t systemDirectory[MAX_PATH];
			preparation.finalProgram = GetSystemDirectoryW(systemDirectory, MAX_PATH) > 0
				? std::wstring(systemDirectory) + L"\\cmd.exe"
				: L"cmd.exe";
			preparation.finalArguments = L"/D /S /C \"\"" + safeTargetProgram + L"\"";
			if (!preparedArguments.empty())
				preparation.finalArguments += L" " + preparedArguments;
			preparation.finalArguments += L"\"";
		}

		return preparation;
	}
}