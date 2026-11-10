// Purpose: Declares TrustedInstaller token acquisition, validation rules, and internal diagnostics.
// Approach: Keeps production token creation and deterministic/live self-tests on one fail-closed implementation.
#pragma once

#include <Windows.h>

#include <string>

struct TrustedInstallerTokenRuleInput
{
	bool userIsLocalSystem = false;
	bool serviceSidPresent = false;
	DWORD serviceSidAttributes = 0;
	TOKEN_TYPE tokenType = TokenImpersonation;
	DWORD integrityRid = 0;
	DWORD sessionId = 0;
};

struct TrustedInstallerServiceRuleInput
{
	DWORD serviceType = 0;
	bool runsAsLocalSystem = false;
	DWORD stateBefore = SERVICE_STOPPED;
	DWORD processIdBefore = 0;
	DWORD stateAfter = SERVICE_STOPPED;
	DWORD processIdAfter = 0;
	bool processHandleAlive = false;
	DWORD openedProcessId = 0;
};

bool ValidateTrustedInstallerTokenRules(
	const TrustedInstallerTokenRuleInput& input,
	const DWORD* expectedSessionId, DWORD& error, std::wstring& failedStage);
bool ValidateTrustedInstallerServiceRules(
	const TrustedInstallerServiceRuleInput& input,
	DWORD& error, std::wstring& failedStage);
bool InspectTrustedInstallerToken(HANDLE token,
	const DWORD* expectedSessionId, LUID* authenticationId,
	DWORD& error, std::wstring& failedStage);
bool AcquireTrustedInstallerLaunchToken(DWORD sessionId, HANDLE& token,
	DWORD& error, std::wstring& failedStage);
bool RunTrustedInstallerTokenRuleSelfTest();
bool RunTrustedInstallerTokenSelfTest(DWORD& error, std::wstring& failedStage);
bool RunTrustedInstallerTokenChildSelfTest(
	DWORD expectedSessionId, DWORD& error, std::wstring& failedStage);
