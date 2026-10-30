// Purpose: Parses and validates the internal broker-bound elevated-wrapper command line.
// Approach: Exercises the behavior with focused assertions against the shared implementation and Windows contracts.
#include "ClientElevatedWrapperParsing.h"

#include <algorithm>
#include <sstream>

#include "ClientLogging.h"

namespace xucrun::elevated_wrapper
{
	ElevatedWrapperParseStatus ParseElevatedWrapperCommandLine(
		int argc, const std::wstring& rawCommandLine, ElevatedWrapperArguments& arguments)
	{
		if (argc < 3)
			return ElevatedWrapperParseStatus::ProgramMissing;

		client_logging::Write(L"Info", L"[Wrapper mode] Parsing broker-bound command line");

		size_t elevatePos = rawCommandLine.find(L"/internal-elevate");
		if (elevatePos == std::wstring::npos)
			return ElevatedWrapperParseStatus::ElevationSwitchMissing;

		elevatePos += 17;
		while (elevatePos < rawCommandLine.length() && rawCommandLine[elevatePos] == L' ')
		{
			elevatePos++;
		}

		const std::wstring elevationMarker = L"/internal-elevated";
		if (rawCommandLine.compare(elevatePos, elevationMarker.length(), elevationMarker) == 0 &&
			(elevatePos + elevationMarker.length() == rawCommandLine.length() ||
			 rawCommandLine[elevatePos + elevationMarker.length()] == L' '))
		{
			arguments.requestElevation = true;
			elevatePos += elevationMarker.length();
			while (elevatePos < rawCommandLine.length() && rawCommandLine[elevatePos] == L' ')
			{
				elevatePos++;
			}
			client_logging::Write(L"Info", L"[Wrapper mode] Elevation was requested");
		}

		const std::wstring hiddenMarker = L"/internal-hidden";
		if (rawCommandLine.compare(elevatePos, hiddenMarker.length(), hiddenMarker) == 0 &&
			(elevatePos + hiddenMarker.length() == rawCommandLine.length() ||
			 rawCommandLine[elevatePos + hiddenMarker.length()] == L' '))
		{
			arguments.hidden = true;
			elevatePos += hiddenMarker.length();
			while (elevatePos < rawCommandLine.length() && rawCommandLine[elevatePos] == L' ')
				elevatePos++;
			client_logging::Write(L"Info", L"[Wrapper mode] Hidden target launch was requested");
		}

		const std::wstring hiddenMonitorMarker = L"/internal-hidden-monitor";
		if (rawCommandLine.compare(elevatePos, hiddenMonitorMarker.length(), hiddenMonitorMarker) == 0 &&
			(elevatePos + hiddenMonitorMarker.length() == rawCommandLine.length() ||
			 rawCommandLine[elevatePos + hiddenMonitorMarker.length()] == L' '))
		{
			arguments.hidden = true;
			arguments.monitorHiddenProcess = true;
			elevatePos += hiddenMonitorMarker.length();
			while (elevatePos < rawCommandLine.length() && rawCommandLine[elevatePos] == L' ')
				elevatePos++;
			client_logging::Write(L"Info", L"[Wrapper mode] Monitored hidden target launch was requested");
		}

		const std::wstring brokerResultMarker = L"/internal-broker-result:";
		if (rawCommandLine.compare(elevatePos, brokerResultMarker.length(), brokerResultMarker) == 0)
		{
			elevatePos = rawCommandLine.find(L' ', elevatePos);
			if (elevatePos == std::wstring::npos)
				return ElevatedWrapperParseStatus::InvalidBrokerResultMarker;
			while (elevatePos < rawCommandLine.length() && rawCommandLine[elevatePos] == L' ')
				elevatePos++;
		}
		const std::wstring brokerAcknowledgementMarker = L"/internal-broker-ack:";
		if (rawCommandLine.compare(elevatePos, brokerAcknowledgementMarker.length(),
			brokerAcknowledgementMarker) == 0)
		{
			elevatePos = rawCommandLine.find(L' ', elevatePos);
			if (elevatePos == std::wstring::npos)
				return ElevatedWrapperParseStatus::InvalidBrokerResultMarker;
			while (elevatePos < rawCommandLine.length() && rawCommandLine[elevatePos] == L' ')
				elevatePos++;
		}

		if (rawCommandLine.substr(elevatePos, 8) == L"/workdir")
		{
			elevatePos += 8;
			while (elevatePos < rawCommandLine.length() && rawCommandLine[elevatePos] == L' ')
			{
				elevatePos++;
			}

			if (elevatePos < rawCommandLine.length())
			{
				if (rawCommandLine[elevatePos] == L'"')
				{
					elevatePos++;
					size_t quoteEnd = rawCommandLine.find(L'"', elevatePos);
					if (quoteEnd != std::wstring::npos)
					{
						arguments.workingDirectory = rawCommandLine.substr(elevatePos, quoteEnd - elevatePos);
						elevatePos = quoteEnd + 1;
					}
				}
				else
				{
					size_t spacePos = rawCommandLine.find(L' ', elevatePos);
					if (spacePos != std::wstring::npos)
					{
						arguments.workingDirectory = rawCommandLine.substr(elevatePos, spacePos - elevatePos);
						elevatePos = spacePos;
					}
					else
					{
						arguments.workingDirectory = rawCommandLine.substr(elevatePos);
						elevatePos = rawCommandLine.length();
					}
				}

				client_logging::Write(L"Info", L"[Wrapper mode] Working directory from parameters: " + arguments.workingDirectory);
			}

			while (elevatePos < rawCommandLine.length() && rawCommandLine[elevatePos] == L' ')
			{
				elevatePos++;
			}
		}

		if (elevatePos < rawCommandLine.length())
		{
			if (rawCommandLine[elevatePos] == L'"')
			{
				elevatePos++;
				size_t quoteEnd = rawCommandLine.find(L'"', elevatePos);
				if (quoteEnd != std::wstring::npos)
				{
					arguments.targetProgram = rawCommandLine.substr(elevatePos, quoteEnd - elevatePos);
					elevatePos = quoteEnd + 1;
				}
			}
			else
			{
				size_t spacePos = rawCommandLine.find(L' ', elevatePos);
				if (spacePos != std::wstring::npos)
				{
					arguments.targetProgram = rawCommandLine.substr(elevatePos, spacePos - elevatePos);
					elevatePos = spacePos;
				}
				else
				{
					arguments.targetProgram = rawCommandLine.substr(elevatePos);
					elevatePos = rawCommandLine.length();
				}
			}

			while (elevatePos < rawCommandLine.length() && rawCommandLine[elevatePos] == L' ')
			{
				elevatePos++;
			}

			if (elevatePos < rawCommandLine.length())
			{
				arguments.targetArguments = rawCommandLine.substr(elevatePos);
				client_logging::Write(L"Info", L"[Wrapper mode] Target arguments are present");

				if (!arguments.targetArguments.empty() &&
					arguments.targetArguments.front() == L'"' &&
					arguments.targetArguments.back() == L'"')
				{
					size_t quoteCount = std::count(arguments.targetArguments.begin(), arguments.targetArguments.end(), L'"');
					if (quoteCount == 3)
					{
						std::wstring inner = arguments.targetArguments.substr(1, arguments.targetArguments.length() - 2);
						size_t pos = inner.find(L"\\\"");
						if (pos != std::wstring::npos && pos + 2 < inner.length() && inner[pos + 2] == L' ')
						{
							std::wstring fixedArgs = L"\"" + inner.substr(0, pos + 1) + L"\"";
							std::wstring rest = inner.substr(pos + 3);
							std::wistringstream iss(rest);
							std::wstring token;
							while (iss >> token)
							{
								fixedArgs += L" \"" + token + L"\"";
							}

							arguments.targetArguments = fixedArgs;
							client_logging::Write(L"Info", L"[Wrapper mode] Corrected arguments from transferred command line");
						}
					}
				}
			}
		}

		return ElevatedWrapperParseStatus::Succeeded;
	}
}