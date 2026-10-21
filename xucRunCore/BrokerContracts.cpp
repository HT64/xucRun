// Purpose: Implements shared construction and validation of broker contract data.
// Rejects incomplete, oversized, or semantically invalid requests.
#include "BrokerContracts.h"

#include <algorithm>

namespace
{
	bool IsValidAuthenticityMarker(const std::wstring& marker) noexcept
	{
		if (marker.size() != xucrun::broker::MaximumAuthenticityMarkerLength ||
			marker[4] != L'-')
		{
			return false;
		}
		for (std::size_t index = 0; index < marker.size(); index++)
		{
			if (index == 4)
				continue;
			if (!((marker[index] >= L'0' && marker[index] <= L'9') ||
				(marker[index] >= L'A' && marker[index] <= L'F')))
			{
				return false;
			}
		}
		return true;
	}

	bool IsValidBackupDescription(const std::wstring& description) noexcept
	{
		return description.size() <= xucrun::backup::MaximumDescriptionLength &&
			std::none_of(description.begin(), description.end(),
				[](wchar_t character) { return character < L' '; });
	}

	bool IsSpecifiedProductVersion(
		const xucrun::broker::ProductVersion& version) noexcept
	{
		return version.major != 0 || version.minor != 0 ||
			version.patch != 0 || version.build != 0;
	}

	int HexValue(wchar_t character) noexcept
	{
		if (character >= L'0' && character <= L'9') return character - L'0';
		if (character >= L'A' && character <= L'F') return character - L'A' + 10;
		if (character >= L'a' && character <= L'f') return character - L'a' + 10;
		return -1;
	}

	bool IsValidEncodedBackupDescription(const std::wstring& encoded) noexcept
	{
		if (encoded.empty() || encoded.size() % 4 != 0 ||
			encoded.size() / 4 > xucrun::backup::MaximumDescriptionLength)
		{
			return false;
		}
		for (std::size_t offset = 0; offset < encoded.size(); offset += 4)
		{
			unsigned int value = 0;
			for (std::size_t index = 0; index < 4; index++)
			{
				const int digit = HexValue(encoded[offset + index]);
				if (digit < 0) return false;
				value = value * 16 + static_cast<unsigned int>(digit);
			}
			if (value < L' ') return false;
		}
		return true;
	}
}

namespace xucrun::broker
{
	// Creates a start request from the contract fields supplied by the caller.
	LaunchRequest BuildLaunchRequest(
		std::uint64_t requestId,
		const std::wstring& requestedAccountSid,
		const std::wstring& executablePath,
		const std::wstring& argumentTail,
		const std::wstring& workingDirectory,
		ElevationMode elevationMode)
	{
		LaunchRequest request;
		request.requestId = requestId;
		request.requestedAccountSid = requestedAccountSid;
		request.executablePath = executablePath;
		request.argumentTail = argumentTail;
		request.workingDirectory = workingDirectory;
		request.elevationMode = elevationMode;
		return request;
	}

	// Accepts only start requests with the matching version, identifier, required fields, and bounds.
	ContractValidationError ValidateLaunchRequest(const LaunchRequest& request) noexcept
	{
		if (request.protocolVersion != ProtocolVersion)
			return ContractValidationError::UnsupportedProtocolVersion;
		if (request.requestId == 0)
			return ContractValidationError::MissingRequestId;
		if (request.requestedAccountSid.empty())
			return ContractValidationError::MissingAccountSid;
		if (request.requestedAccountSid.size() > MaximumAccountSidLength)
			return ContractValidationError::AccountSidTooLong;
		if (request.executablePath.empty())
			return ContractValidationError::MissingExecutablePath;
		if (request.executablePath.size() > MaximumPathLength)
			return ContractValidationError::ExecutablePathTooLong;
		if (request.argumentTail.size() > MaximumArgumentLength)
			return ContractValidationError::ArgumentsTooLong;
		if (request.workingDirectory.size() > MaximumPathLength)
			return ContractValidationError::WorkingDirectoryTooLong;
		if (request.elevationMode != ElevationMode::PreserveToken &&
			request.elevationMode != ElevationMode::RequireElevated)
		{
			return ContractValidationError::InvalidElevationMode;
		}
		return ContractValidationError::None;
	}

	// Accepts only start results with the matching version, identifier, and known status.
	ResultValidationError ValidateLaunchResult(const LaunchResult& result) noexcept
	{
		if (result.protocolVersion != ProtocolVersion)
			return ResultValidationError::UnsupportedProtocolVersion;
		if (result.requestId == 0)
			return ResultValidationError::MissingRequestId;
		switch (result.status)
		{
		case LaunchStatus::Succeeded:
		case LaunchStatus::Denied:
		case LaunchStatus::Cancelled:
		case LaunchStatus::InvalidRequest:
		case LaunchStatus::BrokerUnavailable:
		case LaunchStatus::Failed:
			break;
		default:
			return ResultValidationError::InvalidStatus;
		}
		return ResultValidationError::None;
	}

	// Accepts metadata requests only with a valid version, identifier, and bounded account SID.
	ContractValidationError ValidateCredentialMetadataRequest(
		const CredentialMetadataRequest& request) noexcept
	{
		if (request.protocolVersion != ProtocolVersion)
			return ContractValidationError::UnsupportedProtocolVersion;
		if (request.requestId == 0)
			return ContractValidationError::MissingRequestId;
		if (request.requestedAccountSid.empty())
			return ContractValidationError::MissingAccountSid;
		if (request.requestedAccountSid.size() > MaximumAccountSidLength)
			return ContractValidationError::AccountSidTooLong;
		return ContractValidationError::None;
	}

	// Accepts metadata results only with a valid version, identifier, and known status.
	ResultValidationError ValidateCredentialMetadataResult(
		const CredentialMetadataResult& result) noexcept
	{
		if (result.protocolVersion != ProtocolVersion)
			return ResultValidationError::UnsupportedProtocolVersion;
		if (result.requestId == 0)
			return ResultValidationError::MissingRequestId;
		switch (result.status)
		{
		case CredentialMetadataStatus::Succeeded:
		case CredentialMetadataStatus::InvalidRequest:
		case CredentialMetadataStatus::Failed:
			return ResultValidationError::None;
		default:
			return ResultValidationError::InvalidStatus;
		}
	}

	// Accepts store requests only with a valid account SID and an assigned, bounded password.
	ContractValidationError ValidateCredentialSaveRequest(
		const CredentialSaveRequest& request) noexcept
	{
		if (request.protocolVersion != ProtocolVersion)
			return ContractValidationError::UnsupportedProtocolVersion;
		if (request.requestId == 0)
			return ContractValidationError::MissingRequestId;
		if (request.requestedAccountSid.empty())
			return ContractValidationError::MissingAccountSid;
		if (request.requestedAccountSid.size() > MaximumAccountSidLength)
			return ContractValidationError::AccountSidTooLong;
		if (!request.password.isAssigned())
			return ContractValidationError::MissingPassword;
		if (request.password.size() > MaximumPasswordLength)
			return ContractValidationError::PasswordTooLong;
		return ContractValidationError::None;
	}

	// Accepts store results only with the matching version, identifier, and known status.
	ResultValidationError ValidateCredentialSaveResult(
		const CredentialSaveResult& result) noexcept
	{
		if (result.protocolVersion != ProtocolVersion)
			return ResultValidationError::UnsupportedProtocolVersion;
		if (result.requestId == 0)
			return ResultValidationError::MissingRequestId;
		switch (result.status)
		{
		case CredentialSaveStatus::Succeeded:
		case CredentialSaveStatus::InvalidCredentials:
		case CredentialSaveStatus::InvalidRequest:
		case CredentialSaveStatus::Failed:
			return ResultValidationError::None;
		default:
			return ResultValidationError::InvalidStatus;
		}
	}

	// Accepts delete requests only with a valid version, identifier, and bounded account SID.
	ContractValidationError ValidateCredentialDeleteRequest(
		const CredentialDeleteRequest& request) noexcept
	{
		if (request.protocolVersion != ProtocolVersion)
			return ContractValidationError::UnsupportedProtocolVersion;
		if (request.requestId == 0)
			return ContractValidationError::MissingRequestId;
		if (request.requestedAccountSid.empty())
			return ContractValidationError::MissingAccountSid;
		if (request.requestedAccountSid.size() > MaximumAccountSidLength)
			return ContractValidationError::AccountSidTooLong;
		return ContractValidationError::None;
	}

	// Accepts delete results only with the matching version, identifier, and known status.
	ResultValidationError ValidateCredentialDeleteResult(
		const CredentialDeleteResult& result) noexcept
	{
		if (result.protocolVersion != ProtocolVersion)
			return ResultValidationError::UnsupportedProtocolVersion;
		if (result.requestId == 0)
			return ResultValidationError::MissingRequestId;
		switch (result.status)
		{
		case CredentialDeleteStatus::Succeeded:
		case CredentialDeleteStatus::NotFound:
		case CredentialDeleteStatus::InvalidRequest:
		case CredentialDeleteStatus::Failed:
			return ResultValidationError::None;
		default:
			return ResultValidationError::InvalidStatus;
		}
	}

	// Accepts list requests only with the matching protocol version and a present identifier.
	ContractValidationError ValidateCredentialListRequest(
		const CredentialListRequest& request) noexcept
	{
		if (request.protocolVersion != ProtocolVersion)
			return ContractValidationError::UnsupportedProtocolVersion;
		if (request.requestId == 0)
			return ContractValidationError::MissingRequestId;
		return ContractValidationError::None;
	}

	// Requires bounded, complete entries exclusively for a successful list result.
	ResultValidationError ValidateCredentialListResult(
		const CredentialListResult& result) noexcept
	{
		if (result.protocolVersion != ProtocolVersion)
			return ResultValidationError::UnsupportedProtocolVersion;
		if (result.requestId == 0)
			return ResultValidationError::MissingRequestId;
		if (result.entries.size() > MaximumCredentialListEntries)
			return ResultValidationError::InvalidStatus;
		switch (result.status)
		{
		case CredentialListStatus::Succeeded:
			for (const CredentialListEntry& entry : result.entries)
			{
				if (entry.accountSid.empty() ||
					entry.accountSid.size() > MaximumAccountSidLength ||
					entry.canonicalAccountName.empty() ||
					entry.canonicalAccountName.size() > MaximumCanonicalAccountNameLength ||
					entry.lastWritten == 0)
				{
					return ResultValidationError::InvalidStatus;
				}
			}
			return ResultValidationError::None;
		case CredentialListStatus::InvalidRequest:
		case CredentialListStatus::Failed:
			return result.entries.empty()
				? ResultValidationError::None
				: ResultValidationError::InvalidStatus;
		default:
			return ResultValidationError::InvalidStatus;
		}
	}

	ContractValidationError ValidateAdministrativeInventoryRequest(
		const AdministrativeInventoryRequest& request) noexcept
	{
		if (request.protocolVersion != ProtocolVersion)
			return ContractValidationError::UnsupportedProtocolVersion;
		if (request.requestId == 0)
			return ContractValidationError::MissingRequestId;
		return ContractValidationError::None;
	}

	ResultValidationError ValidateAdministrativeInventoryResult(
		const AdministrativeInventoryResult& result) noexcept
	{
		if (result.protocolVersion != ProtocolVersion)
			return ResultValidationError::UnsupportedProtocolVersion;
		if (result.requestId == 0)
			return ResultValidationError::MissingRequestId;
		if (result.credentials.size() > MaximumAdministrativeCredentialEntries ||
			result.approvals.size() > MaximumAdministrativeApprovalEntries)
		{
			return ResultValidationError::InvalidStatus;
		}
		switch (result.status)
		{
		case AdministrativeInventoryStatus::Succeeded:
			for (const AdministrativeCredentialListEntry& entry : result.credentials)
			{
				if (entry.ownerUserSid.empty() ||
					entry.ownerUserSid.size() > MaximumAccountSidLength ||
					entry.accountSid.empty() ||
					entry.accountSid.size() > MaximumAccountSidLength ||
					entry.canonicalAccountName.empty() ||
					entry.canonicalAccountName.size() > MaximumCanonicalAccountNameLength ||
					entry.lastWritten == 0)
				{
					return ResultValidationError::InvalidStatus;
				}
			}
			return ResultValidationError::None;
		case AdministrativeInventoryStatus::InvalidRequest:
		case AdministrativeInventoryStatus::Failed:
			return result.authenticityMarkerCount == 0 &&
				result.credentials.empty() && result.approvals.empty()
				? ResultValidationError::None
				: ResultValidationError::InvalidStatus;
		default:
			return ResultValidationError::InvalidStatus;
		}
	}

	ContractValidationError ValidateAdministrativeDeleteRequest(
		const AdministrativeDeleteRequest& request) noexcept
	{
		if (request.protocolVersion != ProtocolVersion)
			return ContractValidationError::UnsupportedProtocolVersion;
		if (request.requestId == 0)
			return ContractValidationError::MissingRequestId;
		if (request.ownerUserSid.empty())
			return ContractValidationError::MissingAccountSid;
		if (request.ownerUserSid.size() > MaximumAccountSidLength ||
			request.accountSid.size() > MaximumAccountSidLength)
		{
			return ContractValidationError::AccountSidTooLong;
		}
		switch (request.kind)
		{
		case AdministrativeDeleteKind::Credential:
			if (request.accountSid.empty())
				return ContractValidationError::MissingAccountSid;
			return xucrun::approval::SerializeBrokerApproval(
				request.approval, 4096).empty()
				? ContractValidationError::None
				: ContractValidationError::InvalidApprovalDecision;
		case AdministrativeDeleteKind::Approval:
			if (!request.accountSid.empty() ||
				request.approval.ownerUserSid != request.ownerUserSid ||
				xucrun::approval::SerializeBrokerApproval(request.approval, 4096).empty())
			{
				return ContractValidationError::InvalidApprovalDecision;
			}
			return ContractValidationError::None;
		default:
			return ContractValidationError::InvalidAdministrativeDeleteKind;
		}
	}

	ResultValidationError ValidateAdministrativeDeleteResult(
		const AdministrativeDeleteResult& result) noexcept
	{
		if (result.protocolVersion != ProtocolVersion)
			return ResultValidationError::UnsupportedProtocolVersion;
		if (result.requestId == 0)
			return ResultValidationError::MissingRequestId;
		switch (result.status)
		{
		case AdministrativeDeleteStatus::Succeeded:
			return result.windowsError == NO_ERROR
				? ResultValidationError::None
				: ResultValidationError::InvalidStatus;
		case AdministrativeDeleteStatus::NotFound:
			return result.windowsError == ERROR_NOT_FOUND
				? ResultValidationError::None
				: ResultValidationError::InvalidStatus;
		case AdministrativeDeleteStatus::InvalidRequest:
		case AdministrativeDeleteStatus::Failed:
			return result.windowsError != NO_ERROR
				? ResultValidationError::None
				: ResultValidationError::InvalidStatus;
		default:
			return ResultValidationError::InvalidStatus;
		}
	}

	ContractValidationError ValidateBackupRequest(const BackupRequest& request) noexcept
	{
		if (request.protocolVersion != ProtocolVersion)
			return ContractValidationError::UnsupportedProtocolVersion;
		if (request.requestId == 0)
			return ContractValidationError::MissingRequestId;
		switch (request.action)
		{
		case BackupAction::Create:
			return request.backupId.empty() && !request.createFallbackBackup &&
				IsValidBackupDescription(request.description)
				? ContractValidationError::None
				: ContractValidationError::InvalidBackupRequest;
		case BackupAction::List:
		case BackupAction::DeleteAll:
			return request.backupId.empty() && request.description.empty() &&
				!request.createFallbackBackup
				? ContractValidationError::None
				: ContractValidationError::InvalidBackupRequest;
		case BackupAction::Restore:
		case BackupAction::Delete:
			return IsValidBackupId(request.backupId) && request.description.empty() &&
				(request.action == BackupAction::Restore || !request.createFallbackBackup)
				? ContractValidationError::None
				: ContractValidationError::InvalidBackupRequest;
		default:
			return ContractValidationError::InvalidBackupRequest;
		}
	}

	ResultValidationError ValidateBackupResult(const BackupResult& result) noexcept
	{
		if (result.protocolVersion != ProtocolVersion)
			return ResultValidationError::UnsupportedProtocolVersion;
		if (result.requestId == 0)
			return ResultValidationError::MissingRequestId;
		if (result.entries.size() > MaximumBackupEntries ||
			(!result.fallbackBackupId.empty() &&
				!IsValidBackupId(result.fallbackBackupId)))
		{
			return ResultValidationError::InvalidStatus;
		}
		if (result.status != BackupStatus::Succeeded)
		{
			return result.entries.empty() && result.fallbackBackupId.empty()
				? ResultValidationError::None : ResultValidationError::InvalidStatus;
		}
		for (const auto& entry : result.entries)
		{
			if (!IsValidBackupId(entry.id) || entry.createdAt == 0 ||
				!IsValidBackupDescription(entry.description))
				return ResultValidationError::InvalidStatus;
		}
		return ResultValidationError::None;
	}

	bool IsValidBackupId(const std::wstring& id) noexcept
	{
		return id.size() == BackupIdLength &&
			std::all_of(id.begin(), id.end(), [](wchar_t character) {
				return (character >= L'0' && character <= L'9') ||
					(character >= L'A' && character <= L'F');
			});
	}

	bool BuildAdministrativeBackupCreateCommand(const std::wstring& description,
		std::wstring& command)
	{
		if (!IsValidBackupDescription(description)) return false;
		if (description.empty())
		{
			command = L"create";
			return true;
		}
		constexpr wchar_t digits[] = L"0123456789ABCDEF";
		command = L"create:";
		command.reserve(command.size() + description.size() * 4);
		for (const wchar_t character : description)
		{
			const unsigned int value = static_cast<unsigned short>(character);
			command.push_back(digits[(value >> 12) & 0x0f]);
			command.push_back(digits[(value >> 8) & 0x0f]);
			command.push_back(digits[(value >> 4) & 0x0f]);
			command.push_back(digits[value & 0x0f]);
		}
		return true;
	}

	bool ParseAdministrativeBackupCreateCommand(const std::wstring& command,
		std::wstring& description)
	{
		if (command == L"create")
		{
			description.clear();
			return true;
		}
		constexpr wchar_t prefix[] = L"create:";
		if (!command.starts_with(prefix)) return false;
		const std::wstring encoded = command.substr(std::size(prefix) - 1);
		if (!IsValidEncodedBackupDescription(encoded)) return false;
		description.clear();
		description.reserve(encoded.size() / 4);
		for (std::size_t offset = 0; offset < encoded.size(); offset += 4)
		{
			unsigned int value = 0;
			for (std::size_t index = 0; index < 4; index++)
				value = value * 16 + static_cast<unsigned int>(HexValue(encoded[offset + index]));
			description.push_back(static_cast<wchar_t>(value));
		}
		return true;
	}

	bool IsValidAdministrativeBackupCommand(
		const std::wstring& command) noexcept
	{
		if (command == L"create" || command == L"list" || command == L"delete-all")
		{
			return true;
		}
		constexpr wchar_t createPrefix[] = L"create:";
		if (command.starts_with(createPrefix))
			return IsValidEncodedBackupDescription(
				command.substr(std::size(createPrefix) - 1));
		constexpr wchar_t restorePrefix[] = L"restore:";
		return command.starts_with(restorePrefix) &&
			IsValidBackupId(command.substr(std::size(restorePrefix) - 1));
	}

	bool ParseAdministrativeBackupCommandArgument(const std::wstring& argumentTail,
		std::wstring& pipeToken, std::wstring& command) noexcept
	{
		const std::wstring prefix = AdministrativeBackupCommandArgumentPrefix;
		const std::size_t separator = prefix.size() +
			AdministrativeBackupCommandTokenLength;
		if (!argumentTail.starts_with(prefix) ||
			argumentTail.size() <= separator || argumentTail[separator] != L':')
		{
			return false;
		}
		const std::wstring parsedToken = argumentTail.substr(
			prefix.size(), AdministrativeBackupCommandTokenLength);
		if (!std::all_of(parsedToken.begin(), parsedToken.end(), [](wchar_t character) {
			return (character >= L'0' && character <= L'9') ||
				(character >= L'a' && character <= L'f') ||
				(character >= L'A' && character <= L'F');
		}))
		{
			return false;
		}
		const std::wstring parsedCommand = argumentTail.substr(separator + 1);
		if (!IsValidAdministrativeBackupCommand(parsedCommand))
			return false;
		pipeToken = parsedToken;
		command = parsedCommand;
		return true;
	}

	bool ParseSetLanguageCommandArgument(const std::wstring& argumentTail,
		std::wstring& language, std::wstring& pipeToken) noexcept
	{
		const std::wstring germanPrefix =
			std::wstring(SetLanguageGermanArgumentTail) + L":";
		const std::wstring englishPrefix =
			std::wstring(SetLanguageEnglishArgumentTail) + L":";
		const std::wstring* prefix = nullptr;
		if (argumentTail.starts_with(germanPrefix))
		{
			language = L"de";
			prefix = &germanPrefix;
		}
		else if (argumentTail.starts_with(englishPrefix))
		{
			language = L"en";
			prefix = &englishPrefix;
		}
		else
		{
			return false;
		}
		if (argumentTail.size() != prefix->size() + SetLanguagePipeTokenLength)
			return false;
		pipeToken = argumentTail.substr(prefix->size());
		return std::all_of(pipeToken.begin(), pipeToken.end(), [](wchar_t character) {
			return (character >= L'0' && character <= L'9') ||
				(character >= L'a' && character <= L'f') ||
				(character >= L'A' && character <= L'F');
		});
	}

	// Accepts process list requests only with the matching version and a present identifier.
	ContractValidationError ValidateTrackedProcessListRequest(
		const TrackedProcessListRequest& request) noexcept
	{
		if (request.protocolVersion != ProtocolVersion)
			return ContractValidationError::UnsupportedProtocolVersion;
		if (request.requestId == 0)
			return ContractValidationError::MissingRequestId;
		return ContractValidationError::None;
	}

	// Requires bounded, identifiable process entries exclusively on success.
	ResultValidationError ValidateTrackedProcessListResult(
		const TrackedProcessListResult& result) noexcept
	{
		if (result.protocolVersion != ProtocolVersion)
			return ResultValidationError::UnsupportedProtocolVersion;
		if (result.requestId == 0)
			return ResultValidationError::MissingRequestId;
		if (result.entries.size() > MaximumTrackedProcessEntries)
			return ResultValidationError::InvalidStatus;
		if (result.status == CredentialListStatus::Succeeded)
		{
			for (const TrackedProcessListEntry& entry : result.entries)
			{
				if (entry.processId == 0 || entry.processBaseName.empty() ||
					entry.processBaseName.size() > MaximumProcessBaseNameLength ||
					std::all_of(entry.trackingId.begin(), entry.trackingId.end(),
						[](std::uint8_t byte) { return byte == 0; }))
				{
					return ResultValidationError::InvalidStatus;
				}

			}
			return ResultValidationError::None;
		}
		return (result.status == CredentialListStatus::InvalidRequest ||
			result.status == CredentialListStatus::Failed) && result.entries.empty()
			? ResultValidationError::None
			: ResultValidationError::InvalidStatus;
	}

	ContractValidationError ValidateStatusIconListRequest(
		const StatusIconListRequest& request) noexcept
	{
		if (request.protocolVersion != ProtocolVersion)
			return ContractValidationError::UnsupportedProtocolVersion;
		if (request.requestId == 0)
			return ContractValidationError::MissingRequestId;
		if (!IsSpecifiedProductVersion(request.clientVersion))
			return ContractValidationError::InvalidProductVersion;
		return ContractValidationError::None;
	}

	ResultValidationError ValidateStatusIconListResult(
		const StatusIconListResult& result) noexcept
	{
		if (result.protocolVersion != ProtocolVersion)
			return ResultValidationError::UnsupportedProtocolVersion;
		if (result.requestId == 0)
			return ResultValidationError::MissingRequestId;
		if (result.entries.size() > MaximumStatusIconEntries)
			return ResultValidationError::InvalidStatus;
		if (result.status == CredentialListStatus::Succeeded)
		{
			for (const StatusIconListEntry& entry : result.entries)
			{
				if (entry.processId == 0 ||
					!IsSpecifiedProductVersion(entry.clientVersion) ||
					entry.accountSid.empty() ||
					entry.accountSid.size() > MaximumAccountSidLength ||
					entry.canonicalAccountName.empty() ||
					entry.canonicalAccountName.size() > MaximumCanonicalAccountNameLength)
				{
					return ResultValidationError::InvalidStatus;
				}
			}
			return ResultValidationError::None;
		}
		return (result.status == CredentialListStatus::InvalidRequest ||
			result.status == CredentialListStatus::Failed) && result.entries.empty()
			? ResultValidationError::None
			: ResultValidationError::InvalidStatus;
	}

	// Accepts process registrations only with a version, identifier, handle, and process ID.
	ContractValidationError ValidateTrackedProcessRegistrationRequest(
		const TrackedProcessRegistrationRequest& request) noexcept
	{
		if (request.protocolVersion != ProtocolVersion)
			return ContractValidationError::UnsupportedProtocolVersion;
		if (request.requestId == 0)
			return ContractValidationError::MissingRequestId;
		if (request.processHandle == 0)
			return ContractValidationError::MissingProcessHandle;
		if (request.processId == 0)
			return ContractValidationError::MissingProcessId;
		return ContractValidationError::None;
	}

	// Accepts registration results only with the matching version, identifier, and known status.
	ResultValidationError ValidateTrackedProcessRegistrationResult(
		const TrackedProcessRegistrationResult& result) noexcept
	{
		if (result.protocolVersion != ProtocolVersion)
			return ResultValidationError::UnsupportedProtocolVersion;
		if (result.requestId == 0)
			return ResultValidationError::MissingRequestId;
		switch (result.status)
		{
		case TrackedProcessRegistrationStatus::Succeeded:
		case TrackedProcessRegistrationStatus::InvalidRequest:
		case TrackedProcessRegistrationStatus::Failed:
			return ResultValidationError::None;
		default:
			return ResultValidationError::InvalidStatus;
		}
	}

	// Accepts security checks only with the matching version and a present identifier.
	ContractValidationError ValidateSecurityAuditRequest(
		const SecurityAuditRequest& request) noexcept
	{
		if (request.protocolVersion != ProtocolVersion)
			return ContractValidationError::UnsupportedProtocolVersion;
		if (request.language != SecurityAuditRequest::Language::English &&
			request.language != SecurityAuditRequest::Language::German)
			return ContractValidationError::InvalidUserInterfaceLanguage;
		return request.requestId == 0
			? ContractValidationError::MissingRequestId
			: ContractValidationError::None;
	}

	// Keeps the status, error code, finding count, and report of a check result consistent.
	ResultValidationError ValidateSecurityAuditResult(
		const SecurityAuditResult& result) noexcept
	{
		if (result.protocolVersion != ProtocolVersion)
			return ResultValidationError::UnsupportedProtocolVersion;
		if (result.requestId == 0)
			return ResultValidationError::MissingRequestId;
		if (result.findingCount > MaximumSecurityAuditFindings ||
			result.report.size() > MaximumSecurityAuditReportLength)
		{
			return ResultValidationError::InvalidStatus;
		}
		switch (result.status)
		{
		case SecurityAuditStatus::Succeeded:
			return result.windowsError == NO_ERROR && !result.report.empty()
				? ResultValidationError::None
				: ResultValidationError::InvalidStatus;
		case SecurityAuditStatus::InvalidRequest:
		case SecurityAuditStatus::Failed:
			return result.findingCount == 0 && result.report.empty()
				? ResultValidationError::None
				: ResultValidationError::InvalidStatus;
		default:
			return ResultValidationError::InvalidStatus;
		}
	}

	// Accepts stop requests only with the matching version and a present identifier.
	ContractValidationError ValidateBrokerStopRequest(
		const BrokerStopRequest& request) noexcept
	{
		if (request.protocolVersion != ProtocolVersion)
			return ContractValidationError::UnsupportedProtocolVersion;
		if (request.requestId == 0)
			return ContractValidationError::MissingRequestId;
		switch (request.action)
		{
		case BrokerServiceControlAction::Disable:
			return ContractValidationError::None;
		default:
			return ContractValidationError::InvalidServiceControlAction;
		}
	}

	// Accepts stop responses only with the matching version, identifier, and known status.
	ResultValidationError ValidateBrokerStopResult(
		const BrokerStopResult& result) noexcept
	{
		if (result.protocolVersion != ProtocolVersion)
			return ResultValidationError::UnsupportedProtocolVersion;
		if (result.requestId == 0)
			return ResultValidationError::MissingRequestId;
		switch (result.status)
		{
		case BrokerStopStatus::Accepted:
		case BrokerStopStatus::InvalidRequest:
		case BrokerStopStatus::Failed:
			return ResultValidationError::None;
		default:
			return ResultValidationError::InvalidStatus;
		}
	}

	// Accepts usage tests only with a version, identifier, and bounded account SID.
	ContractValidationError ValidateCredentialUseTestRequest(
		const CredentialUseTestRequest& request) noexcept
	{
		if (request.protocolVersion != ProtocolVersion)
			return ContractValidationError::UnsupportedProtocolVersion;
		if (request.requestId == 0)
			return ContractValidationError::MissingRequestId;
		if (request.requestedAccountSid.empty())
			return ContractValidationError::MissingAccountSid;
		if (request.requestedAccountSid.size() > MaximumAccountSidLength)
			return ContractValidationError::AccountSidTooLong;
		return ContractValidationError::None;
	}

	// Requires a process ID exactly for successful credential usage tests.
	ResultValidationError ValidateCredentialUseTestResult(
		const CredentialUseTestResult& result) noexcept
	{
		if (result.protocolVersion != ProtocolVersion)
			return ResultValidationError::UnsupportedProtocolVersion;
		if (result.requestId == 0)
			return ResultValidationError::MissingRequestId;
		if ((result.status == CredentialUseTestStatus::Succeeded) !=
			(result.processId != 0))
		{
			return ResultValidationError::InvalidStatus;
		}
		switch (result.status)
		{
		case CredentialUseTestStatus::Succeeded:
		case CredentialUseTestStatus::NotFound:
		case CredentialUseTestStatus::InvalidCredentials:
		case CredentialUseTestStatus::InvalidRequest:
		case CredentialUseTestStatus::Failed:
			return ResultValidationError::None;
		default:
			return ResultValidationError::InvalidStatus;
		}
	}

	// Validates an approval request against the same bounds as a start request.
	ContractValidationError ValidateApprovalChallengeRequest(
		const ApprovalChallengeRequest& request) noexcept
	{
		LaunchRequest launchRequest;
		launchRequest.protocolVersion = request.protocolVersion;
		launchRequest.requestId = request.requestId;
		launchRequest.requestedAccountSid = request.requestedAccountSid;
		launchRequest.executablePath = request.executablePath;
		launchRequest.argumentTail = request.argumentTail;
		launchRequest.workingDirectory = request.workingDirectory;
		launchRequest.elevationMode = request.elevationMode;
		const ContractValidationError launchError = ValidateLaunchRequest(launchRequest);
		if (launchError != ContractValidationError::None)
			return launchError;
		if (request.approvalAccountSid.size() > MaximumAccountSidLength)
			return ContractValidationError::AccountSidTooLong;
		return request.visibility == LaunchVisibility::Normal ||
			request.visibility == LaunchVisibility::Hidden ||
			request.visibility == LaunchVisibility::HiddenMonitor
			? ContractValidationError::None
			: ContractValidationError::InvalidLaunchVisibility;
	}

	// Enforces status-dependent consistency of the challenge, SHA-256 identities, and change data.
	ResultValidationError ValidateApprovalChallengeResult(
		const ApprovalChallengeResult& result) noexcept
	{
		if (result.protocolVersion != ProtocolVersion)
			return ResultValidationError::UnsupportedProtocolVersion;
		if (result.requestId == 0)
			return ResultValidationError::MissingRequestId;
		if (result.failureReason > ApprovalFailureReason::ChallengeProcessLimitReached)
			return ResultValidationError::InvalidStatus;
		switch (result.status)
		{
		case ApprovalChallengeStatus::Issued:
		{
			const bool authenticityMarkerValid = result.approvalHelperView
				? IsValidAuthenticityMarker(result.authenticityMarker)
				: result.authenticityMarker.empty();
			const bool writableTargetReasonValid =
				result.writableElevatedTargetReason == ApprovalFailureReason::None ||
				result.writableElevatedTargetReason ==
					ApprovalFailureReason::ElevatedTargetWritable;
			const bool oldHashPresent = std::any_of(result.oldProgramSha256.begin(),
				result.oldProgramSha256.end(), [](std::uint8_t value) { return value != 0; });
			const bool newHashPresent = std::any_of(result.newProgramSha256.begin(),
				result.newProgramSha256.end(), [](std::uint8_t value) { return value != 0; });
			const bool helperStateValid = result.approvalHelperView
				? !result.userConfirmationRequired &&
					(result.helperStartMode == ApprovalHelperStartMode::BrokerStarted ||
					 result.helperStartMode == ApprovalHelperStartMode::ClientRunAsRequired) &&
					result.approvalHelperProcessHandle == 0
				: result.userConfirmationRequired
				? (result.helperStartMode == ApprovalHelperStartMode::BrokerStarted
					? result.approvalHelperProcessHandle != 0
					: result.helperStartMode ==
						ApprovalHelperStartMode::ClientRunAsRequired &&
						result.approvalHelperProcessHandle == 0)
				: result.helperStartMode == ApprovalHelperStartMode::None &&
					result.approvalHelperProcessHandle == 0;
			const bool presenceConfirmationStateValid =
				!result.storedApprovalPresenceConfirmation ||
				(result.elevationMode == ElevationMode::PreserveToken &&
				 (result.helperStartMode == ApprovalHelperStartMode::BrokerStarted ||
				  result.helperStartMode == ApprovalHelperStartMode::ClientRunAsRequired) &&
				 !result.programUpdateAvailable &&
				 (result.approvalHelperView || result.userConfirmationRequired));
			if (result.windowsError != NO_ERROR ||
				result.failureReason != ApprovalFailureReason::None ||
				std::all_of(result.challengeId.begin(), result.challengeId.end(),
				[](std::uint8_t value) { return value == 0; }) ||
				result.canonicalClientName.empty() || result.canonicalAccountName.empty() ||
				result.clientPath.empty() || result.finalExecutablePath.empty() ||
				std::all_of(result.clientSha256.begin(), result.clientSha256.end(),
					[](std::uint8_t value) { return value == 0; }) ||
				std::all_of(result.executableSha256.begin(), result.executableSha256.end(),
					[](std::uint8_t value) { return value == 0; }) ||
				result.canonicalClientName.size() > MaximumCanonicalAccountNameLength ||
				result.canonicalAccountName.size() > MaximumCanonicalAccountNameLength ||
				result.clientPath.size() > MaximumPathLength ||
				result.finalExecutablePath.size() > MaximumPathLength ||
				result.argumentTail.size() > MaximumArgumentLength ||
				result.workingDirectory.size() > MaximumPathLength ||
				result.changedProgramPath.size() > MaximumPathLength ||
				result.clientSignatureTrusted != (result.clientTrustStatus == 0) ||
				result.executableSignatureTrusted != (result.executableTrustStatus == 0) ||
				(result.elevationMode != ElevationMode::PreserveToken &&
				 result.elevationMode != ElevationMode::RequireElevated) ||
				 (result.visibility != LaunchVisibility::Normal &&
				 result.visibility != LaunchVisibility::Hidden &&
				 result.visibility != LaunchVisibility::HiddenMonitor) ||
				!writableTargetReasonValid ||
				(result.writableElevatedTargetReason != ApprovalFailureReason::None &&
				 (result.elevationMode != ElevationMode::RequireElevated ||
				  (!result.userConfirmationRequired && !result.approvalHelperView))) ||
				!helperStateValid || !presenceConfirmationStateValid ||
				!authenticityMarkerValid ||
				(result.programUpdateAvailable !=
					(!result.changedProgramPath.empty() && oldHashPresent && newHashPresent &&
					 result.oldProgramSha256 != result.newProgramSha256 &&
					 result.affectedApprovalCount != 0 && result.affectedAccountCount != 0)))
			{
				return ResultValidationError::InvalidStatus;
			}
			return ResultValidationError::None;
		}
		case ApprovalChallengeStatus::InvalidRequest:
		case ApprovalChallengeStatus::Denied:
		case ApprovalChallengeStatus::Failed:
			if (!result.userConfirmationRequired ||
				result.approvalHelperView ||
				result.storedApprovalPresenceConfirmation ||
				result.helperStartMode != ApprovalHelperStartMode::None ||
				result.approvalHelperProcessHandle != 0 ||
				result.visibility != LaunchVisibility::Normal ||
				std::any_of(result.challengeId.begin(), result.challengeId.end(),
					[](std::uint8_t value) { return value != 0; }) ||
				std::any_of(result.executableSha256.begin(), result.executableSha256.end(),
					[](std::uint8_t value) { return value != 0; }) ||
				std::any_of(result.clientSha256.begin(), result.clientSha256.end(),
					[](std::uint8_t value) { return value != 0; }) ||
				!result.canonicalClientName.empty() || !result.canonicalAccountName.empty() ||
				!result.clientPath.empty() || !result.finalExecutablePath.empty() ||
				!result.argumentTail.empty() || !result.workingDirectory.empty() ||
				!result.authenticityMarker.empty() ||
				result.writableElevatedTargetReason != ApprovalFailureReason::None ||
				result.programUpdateAvailable || !result.changedProgramPath.empty() ||
				result.affectedApprovalCount != 0 || result.affectedAccountCount != 0 ||
				std::any_of(result.oldProgramSha256.begin(), result.oldProgramSha256.end(),
					[](std::uint8_t value) { return value != 0; }) ||
				std::any_of(result.newProgramSha256.begin(), result.newProgramSha256.end(),
					[](std::uint8_t value) { return value != 0; }))
			{
				return ResultValidationError::InvalidStatus;
			}
			return ResultValidationError::None;
		default:
			return ResultValidationError::InvalidStatus;
		}
	}

	// Accepts confirmations only with a challenge ID and a consistent decision combination.
	ContractValidationError ValidateApprovalConfirmationRequest(
		const ApprovalConfirmationRequest& request) noexcept
	{
		if (request.protocolVersion != ProtocolVersion)
			return ContractValidationError::UnsupportedProtocolVersion;
		if (request.requestId == 0)
			return ContractValidationError::MissingRequestId;
		if (std::all_of(request.challengeId.begin(), request.challengeId.end(),
			[](std::uint8_t value) { return value == 0; }))
		{
			return ContractValidationError::MissingChallengeId;
		}
		if (request.decision != ApprovalDecision::ApproveOnce &&
			request.decision != ApprovalDecision::Deny)
		{
			return ContractValidationError::InvalidApprovalDecision;
		}
		if (request.persistence > ApprovalPersistence::Forever ||
			request.programScope > ApprovalProgramScope::TargetProgram ||
			request.accountScope > ApprovalAccountScope::AllAccounts ||
			request.updateScope > ApprovalUpdateScope::AllAccounts ||
			(request.decision == ApprovalDecision::Deny &&
				request.persistence != ApprovalPersistence::Once) ||
			(request.updateScope != ApprovalUpdateScope::None &&
				(request.decision != ApprovalDecision::ApproveOnce ||
				 request.persistence != ApprovalPersistence::Once)))
		{
			return ContractValidationError::InvalidApprovalDecision;
		}
		return ContractValidationError::None;
	}

	// Requires a process ID exactly for successfully confirmed approvals.
	ResultValidationError ValidateApprovalConfirmationResult(
		const ApprovalConfirmationResult& result) noexcept
	{
		if (result.protocolVersion != ProtocolVersion)
			return ResultValidationError::UnsupportedProtocolVersion;
		if (result.requestId == 0)
			return ResultValidationError::MissingRequestId;
		if ((result.status == ApprovalConfirmationStatus::Confirmed) !=
			(result.processId != 0))
		{
			return ResultValidationError::InvalidStatus;
		}
		if (result.failureReason > ApprovalFailureReason::ChallengeProcessLimitReached ||
			(result.status == ApprovalConfirmationStatus::Confirmed &&
			 result.failureReason != ApprovalFailureReason::None))
		{
			return ResultValidationError::InvalidStatus;
		}
		switch (result.status)
		{
		case ApprovalConfirmationStatus::Confirmed:
		case ApprovalConfirmationStatus::Denied:
		case ApprovalConfirmationStatus::Expired:
		case ApprovalConfirmationStatus::InvalidRequest:
		case ApprovalConfirmationStatus::Failed:
			return ResultValidationError::None;
		default:
			return ResultValidationError::InvalidStatus;
		}
	}

	ContractValidationError ValidateApprovalHelperRequest(
		const ApprovalHelperRequest& request) noexcept
	{
		if (request.protocolVersion != ProtocolVersion)
			return ContractValidationError::UnsupportedProtocolVersion;
		if (request.requestId == 0)
			return ContractValidationError::MissingRequestId;
		return std::any_of(request.challengeId.begin(), request.challengeId.end(),
			[](std::uint8_t value) { return value != 0; })
			? ContractValidationError::None
			: ContractValidationError::MissingChallengeId;
	}

	ContractValidationError ValidateApprovalHelperDecisionRequest(
		const ApprovalHelperDecisionRequest& request) noexcept
	{
		ApprovalConfirmationRequest confirmation;
		confirmation.protocolVersion = request.protocolVersion;
		confirmation.requestId = request.requestId;
		confirmation.challengeId = request.challengeId;
		confirmation.decision = request.decision;
		confirmation.persistence = request.persistence;
		confirmation.programScope = request.programScope;
		confirmation.accountScope = request.accountScope;
		confirmation.updateScope = request.updateScope;
		return ValidateApprovalConfirmationRequest(confirmation);
	}

	ResultValidationError ValidateApprovalHelperDecisionResult(
		const ApprovalHelperDecisionResult& result) noexcept
	{
		if (result.protocolVersion != ProtocolVersion)
			return ResultValidationError::UnsupportedProtocolVersion;
		if (result.requestId == 0)
			return ResultValidationError::MissingRequestId;
		switch (result.status)
		{
		case ApprovalConfirmationStatus::Confirmed:
		case ApprovalConfirmationStatus::Denied:
		case ApprovalConfirmationStatus::Expired:
		case ApprovalConfirmationStatus::InvalidRequest:
		case ApprovalConfirmationStatus::Failed:
			return ResultValidationError::None;
		default:
			return ResultValidationError::InvalidStatus;
		}
	}

	// Accepts approval list requests only with the matching version and a present identifier.
	ContractValidationError ValidateApprovalListRequest(
		const ApprovalListRequest& request) noexcept
	{
		if (request.protocolVersion != ProtocolVersion)
			return ContractValidationError::UnsupportedProtocolVersion;
		return request.requestId == 0
			? ContractValidationError::MissingRequestId
			: ContractValidationError::None;
	}

	// Allows approval list entries exclusively with a successful result status.
	ResultValidationError ValidateApprovalListResult(
		const ApprovalListResult& result) noexcept
	{
		if (result.protocolVersion != ProtocolVersion)
			return ResultValidationError::UnsupportedProtocolVersion;
		if (result.requestId == 0)
			return ResultValidationError::MissingRequestId;
		if (result.status > ApprovalManagementStatus::Failed ||
			(result.status != ApprovalManagementStatus::Succeeded && !result.entries.empty()))
		{
			return ResultValidationError::InvalidStatus;
		}
		return ResultValidationError::None;
	}

	ContractValidationError ValidateCredentialCaptureChallengeRequest(
		const CredentialCaptureChallengeRequest& request) noexcept
	{
		if (request.protocolVersion != ProtocolVersion)
			return ContractValidationError::UnsupportedProtocolVersion;
		if (request.requestId == 0)
			return ContractValidationError::MissingRequestId;
		if (request.requestedAccountSid.empty())
			return ContractValidationError::MissingAccountSid;
		if (request.requestedAccountSid.size() > MaximumAccountSidLength ||
			request.approvalAccountSid.size() > MaximumAccountSidLength)
			return ContractValidationError::AccountSidTooLong;
		return ContractValidationError::None;
	}

	ResultValidationError ValidateCredentialCaptureChallengeResult(
		const CredentialCaptureChallengeResult& result) noexcept
	{
		if (result.protocolVersion != ProtocolVersion)
			return ResultValidationError::UnsupportedProtocolVersion;
		if (result.requestId == 0)
			return ResultValidationError::MissingRequestId;
		if (result.status < ApprovalChallengeStatus::Issued ||
			result.status > ApprovalChallengeStatus::Failed)
		{
			return ResultValidationError::InvalidStatus;
		}
		const bool issued = result.status == ApprovalChallengeStatus::Issued;
		const bool missingChallenge = std::all_of(result.challengeId.begin(),
			result.challengeId.end(), [](std::uint8_t value) { return value == 0; });
		if ((issued && (missingChallenge || result.canonicalClientName.empty() ||
			result.canonicalAccountName.empty() ||
			result.helperStartMode == ApprovalHelperStartMode::None)) ||
			(!issued && (!missingChallenge ||
				result.helperStartMode != ApprovalHelperStartMode::None ||
				result.approvalHelperProcessHandle != 0)) ||
			(result.helperStartMode == ApprovalHelperStartMode::BrokerStarted &&
				result.approvalHelperProcessHandle == 0) ||
			(result.helperStartMode != ApprovalHelperStartMode::BrokerStarted &&
				result.approvalHelperProcessHandle != 0))
		{
			return ResultValidationError::InvalidStatus;
		}
		return ResultValidationError::None;
	}

	ContractValidationError ValidateCredentialCaptureHelperRequest(
		const CredentialCaptureHelperRequest& request) noexcept
	{
		if (request.protocolVersion != ProtocolVersion)
			return ContractValidationError::UnsupportedProtocolVersion;
		if (request.requestId == 0)
			return ContractValidationError::MissingRequestId;
		if (std::all_of(request.challengeId.begin(), request.challengeId.end(),
			[](std::uint8_t value) { return value == 0; }))
		{
			return ContractValidationError::MissingChallengeId;
		}
		return ContractValidationError::None;
	}

	ResultValidationError ValidateCredentialCaptureHelperResult(
		const CredentialCaptureHelperResult& result) noexcept
	{
		if (result.protocolVersion != ProtocolVersion)
			return ResultValidationError::UnsupportedProtocolVersion;
		if (result.requestId == 0)
			return ResultValidationError::MissingRequestId;
		if (result.status < ApprovalChallengeStatus::Issued ||
			result.status > ApprovalChallengeStatus::Failed)
		{
			return ResultValidationError::InvalidStatus;
		}
		if (result.status == ApprovalChallengeStatus::Issued &&
			(result.canonicalClientName.empty() || result.canonicalAccountName.empty() ||
			 !IsValidAuthenticityMarker(result.authenticityMarker)))
		{
			return ResultValidationError::InvalidStatus;
		}
		if (result.status != ApprovalChallengeStatus::Issued &&
			(!result.canonicalClientName.empty() || !result.canonicalAccountName.empty() ||
			 !result.authenticityMarker.empty()))
		{
			return ResultValidationError::InvalidStatus;
		}
		return ResultValidationError::None;
	}

	ContractValidationError ValidateCredentialCaptureSubmitRequest(
		const CredentialCaptureSubmitRequest& request) noexcept
	{
		if (request.protocolVersion != ProtocolVersion)
			return ContractValidationError::UnsupportedProtocolVersion;
		if (request.requestId == 0)
			return ContractValidationError::MissingRequestId;
		if (std::all_of(request.challengeId.begin(), request.challengeId.end(),
			[](std::uint8_t value) { return value == 0; }))
		{
			return ContractValidationError::MissingChallengeId;
		}
		if (request.enteredAccountName.empty())
			return ContractValidationError::MissingAccountSid;
		if (request.enteredAccountName.size() > MaximumCanonicalAccountNameLength)
			return ContractValidationError::AccountSidTooLong;
		if (request.cancelled && request.password.isAssigned())
			return ContractValidationError::MissingPassword;
		if (!request.cancelled && !request.password.isAssigned())
			return ContractValidationError::MissingPassword;
		if (request.password.size() > MaximumPasswordLength)
			return ContractValidationError::PasswordTooLong;
		return ContractValidationError::None;
	}

	ResultValidationError ValidateCredentialCaptureSubmitResult(
		const CredentialCaptureSubmitResult& result) noexcept
	{
		if (result.protocolVersion != ProtocolVersion)
			return ResultValidationError::UnsupportedProtocolVersion;
		if (result.requestId == 0)
			return ResultValidationError::MissingRequestId;
		if (result.status < CredentialSaveStatus::Succeeded ||
			result.status > CredentialSaveStatus::Failed)
		{
			return ResultValidationError::InvalidStatus;
		}
		return ResultValidationError::None;
	}

	ContractValidationError ValidateCredentialCaptureCommitRequest(
		const CredentialCaptureCommitRequest& request) noexcept
	{
		if (request.protocolVersion != ProtocolVersion)
			return ContractValidationError::UnsupportedProtocolVersion;
		if (request.requestId == 0)
			return ContractValidationError::MissingRequestId;
		return std::all_of(request.challengeId.begin(), request.challengeId.end(),
			[](std::uint8_t value) { return value == 0; })
			? ContractValidationError::MissingChallengeId
			: ContractValidationError::None;
	}

	ResultValidationError ValidateCredentialCaptureCommitResult(
		const CredentialCaptureCommitResult& result) noexcept
	{
		return ValidateCredentialCaptureSubmitResult(result);
	}

	// Accepts reset requests only with a fully serializable approval identity.
	ContractValidationError ValidateApprovalResetRequest(
		const ApprovalResetRequest& request) noexcept
	{
		if (request.protocolVersion != ProtocolVersion)
			return ContractValidationError::UnsupportedProtocolVersion;
		if (request.requestId == 0)
			return ContractValidationError::MissingRequestId;
		return xucrun::approval::SerializeBrokerApproval(
			request.approval, 4096).empty()
			? ContractValidationError::InvalidApprovalDecision
			: ContractValidationError::None;
	}

	// Allows a deletion count exclusively with a successful reset result.
	ResultValidationError ValidateApprovalResetResult(
		const ApprovalResetResult& result) noexcept
	{
		if (result.protocolVersion != ProtocolVersion)
			return ResultValidationError::UnsupportedProtocolVersion;
		if (result.requestId == 0)
			return ResultValidationError::MissingRequestId;
		if (result.status > ApprovalManagementStatus::Failed ||
			(result.status != ApprovalManagementStatus::Succeeded && result.deletedCount != 0))
		{
			return ResultValidationError::InvalidStatus;
		}
		return ResultValidationError::None;
	}
}