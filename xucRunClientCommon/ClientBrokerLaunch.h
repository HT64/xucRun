// Purpose: Defines client-side process launch through stored broker credentials.
// Approach: Exposes the declarations, constants, and types consumed by the components that provide this behavior.
// Behavior: Starts or waits for the requested approval helper, confirms challenges, and registers processes.
#pragma once

#include <Windows.h>

#include <string>

#include "BrokerContracts.h"
#include "ClientCredentialInfo.h"

namespace xucrun::broker_launch
{
	enum class BrokerLaunchOutcome
	{
		Succeeded,
		Cancelled,
		Failed
	};

	bool WaitForApprovalHelper(HANDLE process, DWORD& error);
	void RegisterElevatedProcessWithBroker(HANDLE process);
	bool LaunchWithUac(const std::wstring& executablePath,
		const std::wstring& argumentTail, broker::LaunchVisibility visibility,
		DWORD& error);
	BrokerLaunchOutcome LaunchSystemWithProtectedConfirmation(
		const std::wstring& executablePath,
		const std::wstring& argumentTail,
		const std::wstring& workingDirectory,
		DWORD& processId, DWORD& error);
	BrokerLaunchOutcome LaunchWithBrokerCredential(
		const CredentialInfo& credential,
		const std::wstring& executablePath,
		const std::wstring& argumentTail,
		broker::ElevationMode elevationMode,
		broker::LaunchVisibility visibility);
}