// Purpose: Implements consistent text formatting for credential and approval workflows.
// Behavior: Maps status values via localization and formats paths, scopes, and FILETIME values.
#include "ClientCredentialFormatting.h"

#include "Localization.h"
#include "TargetNaming.h"

namespace
{
	std::wstring GetProgramNameFromPath(const std::wstring& path)
	{
		const std::size_t separator = path.find_last_of(L"\\/");
		return separator == std::wstring::npos ? path : path.substr(separator + 1);
	}

	std::wstring GetApprovalFailureReasonMessage(
		xucrun::broker::ApprovalFailureReason reason)
	{
		using xucrun::broker::ApprovalFailureReason;
		using xucrun::localization::TextId;
		switch (reason)
		{
		case ApprovalFailureReason::ElevatedTargetWritable:
			return xucrun::localization::GetText(TextId::ApprovalTargetWritable);
		case ApprovalFailureReason::ElevatedTargetDirectoryWritable:
			return xucrun::localization::GetText(TextId::ApprovalTargetDirectoryWritable);
		case ApprovalFailureReason::ElevatedPathValidationFailed:
			return xucrun::localization::GetText(TextId::ApprovalPathValidationFailed);
		case ApprovalFailureReason::LaunchObjectChanged:
			return xucrun::localization::GetText(TextId::ApprovalLaunchObjectChanged);
		case ApprovalFailureReason::LaunchObjectLockFailed:
			return xucrun::localization::GetText(TextId::ApprovalLaunchObjectLockFailed);
		case ApprovalFailureReason::ChallengeGlobalLimitReached:
			return xucrun::localization::GetText(TextId::ApprovalGlobalLimitReached);
		case ApprovalFailureReason::ChallengeSidLimitReached:
			return xucrun::localization::GetText(TextId::ApprovalSidLimitReached);
		case ApprovalFailureReason::ChallengeSessionLimitReached:
			return xucrun::localization::GetText(TextId::ApprovalSessionLimitReached);
		case ApprovalFailureReason::ChallengeProcessLimitReached:
			return xucrun::localization::GetText(TextId::ApprovalProcessLimitReached);
		case ApprovalFailureReason::CrossAccountUacUnavailable:
			return xucrun::localization::GetText(
				TextId::ApprovalCrossAccountUacUnavailable);
		case ApprovalFailureReason::None:
		default:
			return {};
		}
	}
}

namespace xucrun::credential_formatting
{
	std::wstring GetBrokerChallengeFailureMessage(
		const broker::ApprovalChallengeResult& result)
	{
		const std::wstring reason = GetApprovalFailureReasonMessage(result.failureReason);
		if (!reason.empty())
			return reason;
		if (result.windowsError == ERROR_NOT_SUPPORTED)
			return localization::GetText(localization::TextId::BrokerPolicyDisabled);
		return localization::GetText(localization::TextId::BrokerChallengeDenied);
	}

	std::wstring GetBrokerLaunchFailureMessage(
		const broker::ApprovalConfirmationResult& result)
	{
		const std::wstring reason = GetApprovalFailureReasonMessage(result.failureReason);
		if (!reason.empty())
			return reason;
		switch (result.status)
		{
		case broker::ApprovalConfirmationStatus::Denied:
			if (result.windowsError == ERROR_NOT_SUPPORTED)
				return localization::GetText(localization::TextId::BrokerPolicyDisabled);
			return localization::GetText(localization::TextId::BrokerConfirmationDenied);
		case broker::ApprovalConfirmationStatus::Expired:
			return localization::GetText(localization::TextId::BrokerApprovalExpired);
		case broker::ApprovalConfirmationStatus::InvalidRequest:
			return localization::GetText(localization::TextId::BrokerApprovalInvalid);
		default:
			if (result.windowsError == ERROR_CANCELLED)
				return localization::GetText(localization::TextId::BrokerUacCancelled);
			if (result.windowsError == ERROR_TIMEOUT)
				return localization::GetText(localization::TextId::BrokerUacTimeout);
			if (result.windowsError == ERROR_INVALID_DATA)
				return localization::GetText(
					localization::TextId::BrokerWrapperInvalidResult);
			return localization::GetText(localization::TextId::BrokerProgramStartFailed);
		}
	}

	std::wstring FormatCredentialTimestamp(ULONGLONG timestamp)
	{
		if (timestamp == 0)
			return localization::GetText(localization::TextId::ValueNotAvailable);

		ULARGE_INTEGER value = {};
		value.QuadPart = timestamp;
		const FILETIME utc = { value.LowPart, value.HighPart };
		FILETIME local = {};
		SYSTEMTIME time = {};
		if (!FileTimeToLocalFileTime(&utc, &local) ||
			!FileTimeToSystemTime(&local, &time))
		{
			return localization::GetText(localization::TextId::ValueNotAvailable);
		}

		wchar_t formatted[32] = {};
		swprintf_s(formatted, L"%02u.%02u.%04u %02u:%02u:%02u",
			time.wDay, time.wMonth, time.wYear,
			time.wHour, time.wMinute, time.wSecond);
		return formatted;
	}

	std::wstring GetCredentialSelectionTitle(CredentialSelectionMode mode)
	{
		if (mode == CredentialSelectionMode::Delete)
			return localization::GetText(
				localization::TextId::CredentialSelectionDeleteTitle);
		if (mode == CredentialSelectionMode::ResetApproval)
			return localization::GetText(
				localization::TextId::CredentialSelectionResetTitle);
		if (mode == CredentialSelectionMode::ApprovalConfirmation)
			return localization::GetText(
				localization::TextId::CredentialSelectionApprovalTitle);
		return localization::GetText(
			localization::TextId::CredentialSelectionLaunchTitle);
	}

	std::wstring GetResetApprovalSelectionHint(std::size_t approvalCount,
		const std::wstring& accountName)
	{
		if (approvalCount == 0)
			return localization::Format(localization::TextId::ResetHintNone,
				{ accountName });
		if (approvalCount == 1)
			return localization::Format(localization::TextId::ResetHintOne,
				{ accountName });
		return localization::Format(localization::TextId::ResetHintMany,
			{ std::to_wstring(approvalCount), accountName });
	}

	std::wstring FormatBrokerApprovalForReset(
		const approval::BrokerApprovalValue& approval)
	{
		std::wstring text;
		if (approval.programScope == approval::BrokerApprovalProgramScope::General)
		{
			text = localization::GetText(localization::TextId::ResetGeneralApproval);
		}
		else
		{
			text = GetProgramNameFromPath(approval.targetPath) + L" | " +
				approval.targetPath;
			if (approval.programScope ==
				approval::BrokerApprovalProgramScope::ExactInvocation)
			{
				text += L" | " + localization::GetText(
					localization::TextId::ShowApprovalsProgramExactValue);
				text += L" | " + FormatExactInvocationBinding(approval, L" | ");
			}
		}
		text += approval.accountScope ==
			approval::BrokerApprovalAccountScope::AllAccounts
			? L" | " + localization::GetText(localization::TextId::ResetScopeAllAccounts)
			: L" | " + localization::GetText(
				localization::TextId::ResetScopeSelectedAccount);
		text += approval.elevationMode == 0
			? L" | " + localization::GetText(localization::TextId::ResetTokenExisting)
			: L" | " + localization::GetText(localization::TextId::ResetTokenElevated);
		if (approval.lifetime == approval::BrokerApprovalLifetime::Forever)
			text += L" | " + localization::GetText(
				localization::TextId::ApprovalDurationForever);
		else if (approval.lifetime == approval::BrokerApprovalLifetime::LogonSession)
			text += L" | " + localization::GetText(
				localization::TextId::ApprovalDurationLogoff);
		else
			text += L" | " + localization::Format(
				localization::TextId::ResetLifetimeUntil,
				{ FormatCredentialTimestamp(approval.expiresAt) });
		return text;
	}

	std::wstring FormatExactInvocationBinding(
		const approval::BrokerApprovalValue& approval,
		const std::wstring& separator)
	{
		if (approval.programScope !=
			approval::BrokerApprovalProgramScope::ExactInvocation)
		{
			return L"";
		}
		std::wstring text =
			localization::GetText(localization::TextId::ApprovalBindingArgumentHash) +
			L": " + targets::BytesToUpperHex(approval.argumentHash) + separator +
			localization::GetText(
				localization::TextId::ApprovalBindingWorkingDirectory) +
			L": " + approval.workingDirectory;
		if (!approval.scriptPath.empty())
		{
			text += separator +
				localization::GetText(
					localization::TextId::ApprovalBindingScriptPath) +
				L": " + approval.scriptPath + separator +
				localization::GetText(
					localization::TextId::ApprovalBindingScriptHash) +
				L": " + targets::BytesToUpperHex(approval.scriptHash);
		}
		return text;
	}

	std::wstring FormatBrokerApprovalScope(
		const approval::BrokerApprovalValue& approval)
	{
		switch (approval.programScope)
		{
		case approval::BrokerApprovalProgramScope::General:
			return localization::GetText(
				localization::TextId::ShowApprovalsProgramGeneralValue);
		case approval::BrokerApprovalProgramScope::ExactInvocation:
			return localization::GetText(
				localization::TextId::ShowApprovalsProgramExactValue);
		default:
			return localization::GetText(
				localization::TextId::ShowApprovalsProgramTargetValue);
		}
	}
}