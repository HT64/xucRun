// Purpose: Declares controlled process launches and secure-desktop hosts in the client session.
// Covers diagnostic starts, interactive object access, and sensitive helper UI.
#pragma once

#include "AccountIdentity.h"
#include "BrokerContracts.h"
#include "SensitiveString.h"

#include <Windows.h>

#include <string>

bool LaunchSessionTestProcess(
	HANDLE clientProcess,
	DWORD sessionId,
	const std::wstring& expectedUserSid,
	DWORD& processId,
	DWORD& error,
	std::wstring& failedStage);

bool LaunchSystemProcess(
	DWORD sessionId,
	const std::wstring& executablePath,
	const std::wstring& argumentTail,
	const std::wstring& workingDirectory,
	DWORD& processId,
	std::wstring& effectiveWorkingDirectory,
	DWORD& error,
	std::wstring& failedStage);

bool LaunchCredentialTestProcess(
	HANDLE clientProcess,
	const AccountIdentity& identity,
	const xucrun::SensitiveString& password,
	DWORD sessionId,
	DWORD& processId,
	DWORD& error,
	std::wstring& failedStage);

bool ShouldRejectPreElevatedCrossAccountLaunch(
	xucrun::broker::ElevationMode elevationMode,
	bool crossAccount,
	bool tokenElevated) noexcept;

bool LaunchApprovedCredentialProcess(
	HANDLE clientProcess,
	const AccountIdentity& identity,
	const xucrun::SensitiveString& password,
	DWORD sessionId,
	const std::wstring& clientPath,
	const std::wstring& executablePath,
	const std::wstring& argumentTail,
	const std::wstring& workingDirectory,
	xucrun::broker::ElevationMode elevationMode,
	xucrun::broker::LaunchVisibility visibility,
	DWORD& processId,
	std::wstring& effectiveWorkingDirectory,
	DWORD& error,
	std::wstring& failedStage,
	HANDLE* processHandle = nullptr,
	xucrun::broker::ApprovalFailureReason* failureReason = nullptr);

bool LaunchApprovedClientProcess(
	HANDLE clientProcess,
	const std::wstring& expectedUserSid,
	DWORD sessionId,
	const std::wstring& executablePath,
	const std::wstring& argumentTail,
	const std::wstring& workingDirectory,
	DWORD& processId,
	std::wstring& effectiveWorkingDirectory,
	DWORD& error,
	std::wstring& failedStage);

bool LaunchSecureDesktopClientApproval(
	HANDLE clientProcess,
	const std::wstring& expectedUserSid,
	DWORD sessionId,
	const std::wstring& helperPath,
	const std::wstring& helperArgument,
	const std::wstring& cancellationEventName,
	HANDLE cancellationEvent,
	HANDLE& hostProcess,
	HANDLE& helperProcess,
	DWORD& hostProcessId,
	DWORD& helperProcessId,
	DWORD& error,
	std::wstring& failedStage);

bool LaunchSecureDesktopCredentialApproval(
	HANDLE clientProcess,
	const AccountIdentity& identity,
	const xucrun::SensitiveString& password,
	DWORD sessionId,
	const std::wstring& helperPath,
	const std::wstring& helperArgument,
	const std::wstring& cancellationEventName,
	HANDLE cancellationEvent,
	HANDLE& hostProcess,
	HANDLE& helperProcess,
	DWORD& hostProcessId,
	DWORD& helperProcessId,
	DWORD& error,
	std::wstring& failedStage);

bool RunInteractiveDesktopAccessHelper(
	const std::wstring& logonSid,
	DWORD& error);