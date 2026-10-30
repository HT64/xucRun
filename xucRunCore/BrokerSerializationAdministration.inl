// Purpose: Internal protocol implementation fragment for BrokerSerialization.cpp.
// Approach: Provides implementation helpers included by the owning translation unit to keep the related flow grouped.
//
// Inclusion contract:
// - This file is included exactly once inside the xucrun::broker namespace.
// - It uses the translation unit's private magic values and byte-level read/write helpers.
// - It is not a standalone translation unit and introduces no additional exported symbols.
// - Field order, length checks, complete-frame validation, and sensitive-frame handling must remain exact.

	std::vector<std::uint8_t> SerializeAdministrativeInventoryRequest(
		const AdministrativeInventoryRequest& request)
	{
		if (ValidateAdministrativeInventoryRequest(request) !=
			ContractValidationError::None)
		{
			return {};
		}
		std::vector<std::uint8_t> frame;
		frame.reserve(AdministrativeInventoryRequestSize);
		frame.insert(frame.end(), std::begin(AdministrativeInventoryRequestMagic),
			std::end(AdministrativeInventoryRequestMagic));
		frame.push_back(AdministrativeInventoryRequestMessageType);
		frame.insert(frame.end(), 3, 0);
		WriteUint32(frame, request.protocolVersion);
		WriteUint64(frame, request.requestId);
		return frame;
	}

	bool DeserializeAdministrativeInventoryRequest(
		std::span<const std::uint8_t> frame,
		AdministrativeInventoryRequest& request)
	{
		if (frame.size() != AdministrativeInventoryRequestSize ||
			!std::equal(std::begin(AdministrativeInventoryRequestMagic),
				std::end(AdministrativeInventoryRequestMagic), frame.begin()) ||
			frame[4] != AdministrativeInventoryRequestMessageType ||
			frame[5] != 0 || frame[6] != 0 || frame[7] != 0)
		{
			return false;
		}
		AdministrativeInventoryRequest parsed;
		parsed.protocolVersion = ReadUint32(frame, 8);
		parsed.requestId = ReadUint64(frame, 12);
		if (ValidateAdministrativeInventoryRequest(parsed) !=
			ContractValidationError::None)
		{
			return false;
		}
		request = parsed;
		return true;
	}

	std::vector<std::uint8_t> SerializeAdministrativeInventoryResult(
		const AdministrativeInventoryResult& result)
	{
		if (ValidateAdministrativeInventoryResult(result) !=
			ResultValidationError::None)
		{
			return {};
		}
		std::vector<std::vector<std::uint8_t>> approvalValues;
		std::size_t totalSize = AdministrativeInventoryResultHeaderSize;
		for (const AdministrativeCredentialListEntry& entry : result.credentials)
		{
			if (HasEmbeddedNull(entry.ownerUserSid) ||
				HasEmbeddedNull(entry.accountSid) ||
				HasEmbeddedNull(entry.canonicalAccountName))
			{
				return {};
			}
			totalSize += AdministrativeCredentialEntryHeaderSize + 2 *
				(entry.ownerUserSid.size() + entry.accountSid.size() +
					entry.canonicalAccountName.size());
		}
		for (const auto& approval : result.approvals)
		{
			auto value = xucrun::approval::SerializeBrokerApproval(
				approval, MaximumApprovalEntrySize);
			if (value.empty())
				return {};
			totalSize += sizeof(std::uint32_t) + value.size();
			approvalValues.push_back(std::move(value));
		}
		if (totalSize > MaximumAdministrativeInventoryResultSize)
			return {};

		std::vector<std::uint8_t> frame;
		frame.reserve(totalSize);
		frame.insert(frame.end(), std::begin(AdministrativeInventoryResultMagic),
			std::end(AdministrativeInventoryResultMagic));
		frame.push_back(AdministrativeInventoryResultMessageType);
		frame.push_back(static_cast<std::uint8_t>(result.status));
		frame.push_back(0);
		frame.push_back(0);
		WriteUint32(frame, result.protocolVersion);
		WriteUint64(frame, result.requestId);
		WriteUint32(frame, result.windowsError);
		WriteUint32(frame, static_cast<std::uint32_t>(result.credentials.size()));
		WriteUint32(frame, static_cast<std::uint32_t>(approvalValues.size()));
		WriteUint32(frame, result.authenticityMarkerCount);
		for (const AdministrativeCredentialListEntry& entry : result.credentials)
		{
			WriteUint32(frame, static_cast<std::uint32_t>(entry.ownerUserSid.size()));
			WriteUint32(frame, static_cast<std::uint32_t>(entry.accountSid.size()));
			WriteUint32(frame, static_cast<std::uint32_t>(entry.canonicalAccountName.size()));
			WriteUint64(frame, entry.lastWritten);
			WriteString(frame, entry.ownerUserSid);
			WriteString(frame, entry.accountSid);
			WriteString(frame, entry.canonicalAccountName);
		}
		for (const auto& value : approvalValues)
		{
			WriteUint32(frame, static_cast<std::uint32_t>(value.size()));
			frame.insert(frame.end(), value.begin(), value.end());
		}
		return frame;
	}

	bool DeserializeAdministrativeInventoryResult(
		std::span<const std::uint8_t> frame,
		AdministrativeInventoryResult& result)
	{
		if (frame.size() < AdministrativeInventoryResultHeaderSize ||
			frame.size() > MaximumAdministrativeInventoryResultSize ||
			!std::equal(std::begin(AdministrativeInventoryResultMagic),
				std::end(AdministrativeInventoryResultMagic), frame.begin()) ||
			frame[4] != AdministrativeInventoryResultMessageType ||
			frame[6] != 0 || frame[7] != 0)
		{
			return false;
		}
		const std::size_t credentialCount = ReadUint32(frame, 24);
		const std::size_t approvalCount = ReadUint32(frame, 28);
		if (credentialCount > MaximumAdministrativeCredentialEntries ||
			approvalCount > MaximumApprovalEntries)
		{
			return false;
		}
		AdministrativeInventoryResult parsed;
		parsed.protocolVersion = ReadUint32(frame, 8);
		parsed.requestId = ReadUint64(frame, 12);
		parsed.status = static_cast<AdministrativeInventoryStatus>(frame[5]);
		parsed.windowsError = ReadUint32(frame, 20);
		parsed.authenticityMarkerCount = ReadUint32(frame, 32);
		std::size_t offset = AdministrativeInventoryResultHeaderSize;
		parsed.credentials.reserve(credentialCount);
		for (std::size_t index = 0; index < credentialCount; index++)
		{
			if (offset > frame.size() ||
				frame.size() - offset < AdministrativeCredentialEntryHeaderSize)
			{
				return false;
			}
			const std::size_t ownerSidLength = ReadUint32(frame, offset);
			const std::size_t accountSidLength = ReadUint32(frame, offset + 4);
			const std::size_t nameLength = ReadUint32(frame, offset + 8);
			const std::uint64_t lastWritten = ReadUint64(frame, offset + 12);
			offset += AdministrativeCredentialEntryHeaderSize;
			if (ownerSidLength == 0 || ownerSidLength > MaximumAccountSidLength ||
				accountSidLength == 0 || accountSidLength > MaximumAccountSidLength ||
				nameLength == 0 || nameLength > MaximumCanonicalAccountNameLength ||
				ownerSidLength + accountSidLength + nameLength >
					(frame.size() - offset) / 2)
			{
				return false;
			}
			AdministrativeCredentialListEntry entry;
			entry.lastWritten = lastWritten;
			if (!ReadString(frame, offset, ownerSidLength, entry.ownerUserSid) ||
				!ReadString(frame, offset, accountSidLength, entry.accountSid) ||
				!ReadString(frame, offset, nameLength, entry.canonicalAccountName))
			{
				return false;
			}
			parsed.credentials.push_back(std::move(entry));
		}
		parsed.approvals.reserve(approvalCount);
		for (std::size_t index = 0; index < approvalCount; index++)
		{
			if (offset > frame.size() || frame.size() - offset < sizeof(std::uint32_t))
				return false;
			const std::size_t size = ReadUint32(frame, offset);
			offset += sizeof(std::uint32_t);
			if (size == 0 || size > MaximumApprovalEntrySize ||
				size > frame.size() - offset)
			{
				return false;
			}
			xucrun::approval::BrokerApprovalValue approval;
			if (!xucrun::approval::DeserializeBrokerApproval(
				frame.subspan(offset, size), approval))
			{
				return false;
			}
			parsed.approvals.push_back(std::move(approval));
			offset += size;
		}
		if (offset != frame.size() ||
			ValidateAdministrativeInventoryResult(parsed) !=
				ResultValidationError::None)
		{
			return false;
		}
		result = std::move(parsed);
		return true;
	}

	std::vector<std::uint8_t> SerializeAdministrativeDeleteRequest(
		const AdministrativeDeleteRequest& request)
	{
		if (ValidateAdministrativeDeleteRequest(request) !=
			ContractValidationError::None || HasEmbeddedNull(request.ownerUserSid) ||
			HasEmbeddedNull(request.accountSid))
		{
			return {};
		}
		std::vector<std::uint8_t> approval;
		if (request.kind == AdministrativeDeleteKind::Approval)
		{
			approval = xucrun::approval::SerializeBrokerApproval(
				request.approval, MaximumApprovalEntrySize);
		}
		std::vector<std::uint8_t> frame;
		frame.reserve(AdministrativeDeleteRequestHeaderSize +
			2 * (request.ownerUserSid.size() + request.accountSid.size()) +
			approval.size());
		frame.insert(frame.end(), std::begin(AdministrativeDeleteRequestMagic),
			std::end(AdministrativeDeleteRequestMagic));
		frame.push_back(AdministrativeDeleteRequestMessageType);
		frame.push_back(static_cast<std::uint8_t>(request.kind));
		frame.push_back(0);
		frame.push_back(0);
		WriteUint32(frame, request.protocolVersion);
		WriteUint64(frame, request.requestId);
		WriteUint32(frame, static_cast<std::uint32_t>(request.ownerUserSid.size()));
		WriteUint32(frame, static_cast<std::uint32_t>(request.accountSid.size()));
		WriteUint32(frame, static_cast<std::uint32_t>(approval.size()));
		WriteUint32(frame, 0);
		WriteString(frame, request.ownerUserSid);
		WriteString(frame, request.accountSid);
		frame.insert(frame.end(), approval.begin(), approval.end());
		return frame;
	}

	bool DeserializeAdministrativeDeleteRequest(
		std::span<const std::uint8_t> frame,
		AdministrativeDeleteRequest& request)
	{
		if (frame.size() < AdministrativeDeleteRequestHeaderSize ||
			frame.size() > MaximumAdministrativeDeleteRequestSize ||
			!std::equal(std::begin(AdministrativeDeleteRequestMagic),
				std::end(AdministrativeDeleteRequestMagic), frame.begin()) ||
			frame[4] != AdministrativeDeleteRequestMessageType ||
			frame[6] != 0 || frame[7] != 0 || ReadUint32(frame, 32) != 0)
		{
			return false;
		}
		const std::size_t ownerLength = ReadUint32(frame, 20);
		const std::size_t accountLength = ReadUint32(frame, 24);
		const std::size_t approvalLength = ReadUint32(frame, 28);
		if (ownerLength == 0 || ownerLength > MaximumAccountSidLength ||
			accountLength > MaximumAccountSidLength ||
			approvalLength > MaximumApprovalEntrySize)
		{
			return false;
		}
		std::size_t offset = AdministrativeDeleteRequestHeaderSize;
		AdministrativeDeleteRequest parsed;
		parsed.protocolVersion = ReadUint32(frame, 8);
		parsed.requestId = ReadUint64(frame, 12);
		parsed.kind = static_cast<AdministrativeDeleteKind>(frame[5]);
		if (ownerLength > (frame.size() - offset) / sizeof(wchar_t) ||
			!ReadString(frame, offset, ownerLength, parsed.ownerUserSid) ||
			accountLength > (frame.size() - offset) / sizeof(wchar_t) ||
			!ReadString(frame, offset, accountLength, parsed.accountSid) ||
			approvalLength > frame.size() - offset)
		{
			return false;
		}
		if (approvalLength != 0 &&
			!xucrun::approval::DeserializeBrokerApproval(
				frame.subspan(offset, approvalLength), parsed.approval))
		{
			return false;
		}
		offset += approvalLength;
		if (offset != frame.size() || ValidateAdministrativeDeleteRequest(parsed) !=
			ContractValidationError::None)
		{
			return false;
		}
		request = std::move(parsed);
		return true;
	}

	std::vector<std::uint8_t> SerializeAdministrativeDeleteResult(
		const AdministrativeDeleteResult& result)
	{
		if (ValidateAdministrativeDeleteResult(result) != ResultValidationError::None)
			return {};
		std::vector<std::uint8_t> frame;
		frame.insert(frame.end(), std::begin(AdministrativeDeleteResultMagic),
			std::end(AdministrativeDeleteResultMagic));
		frame.push_back(AdministrativeDeleteResultMessageType);
		frame.push_back(static_cast<std::uint8_t>(result.status));
		frame.push_back(0);
		frame.push_back(0);
		WriteUint32(frame, result.protocolVersion);
		WriteUint64(frame, result.requestId);
		WriteUint32(frame, result.windowsError);
		return frame;
	}

	bool DeserializeAdministrativeDeleteResult(
		std::span<const std::uint8_t> frame,
		AdministrativeDeleteResult& result)
	{
		if (frame.size() != AdministrativeDeleteResultSize ||
			!std::equal(std::begin(AdministrativeDeleteResultMagic),
				std::end(AdministrativeDeleteResultMagic), frame.begin()) ||
			frame[4] != AdministrativeDeleteResultMessageType ||
			frame[6] != 0 || frame[7] != 0)
		{
			return false;
		}
		AdministrativeDeleteResult parsed;
		parsed.protocolVersion = ReadUint32(frame, 8);
		parsed.requestId = ReadUint64(frame, 12);
		parsed.status = static_cast<AdministrativeDeleteStatus>(frame[5]);
		parsed.windowsError = ReadUint32(frame, 20);
		if (ValidateAdministrativeDeleteResult(parsed) != ResultValidationError::None)
			return false;
		result = parsed;
		return true;
	}

	std::vector<std::uint8_t> SerializeBackupRequest(const BackupRequest& request)
	{
		if (ValidateBackupRequest(request) != ContractValidationError::None)
			return {};
		std::vector<std::uint8_t> frame;
		frame.reserve(BackupRequestHeaderSize +
			(request.backupId.size() + request.description.size()) * 2);
		frame.insert(frame.end(), std::begin(BackupRequestMagic), std::end(BackupRequestMagic));
		frame.push_back(BackupRequestMessageType);
		frame.push_back(static_cast<std::uint8_t>(request.action));
		frame.push_back(request.createFallbackBackup ? 1 : 0);
		frame.push_back(0);
		WriteUint32(frame, request.protocolVersion);
		WriteUint64(frame, request.requestId);
		WriteUint32(frame, static_cast<std::uint32_t>(request.backupId.size()));
		WriteUint32(frame, static_cast<std::uint32_t>(request.description.size()));
		WriteString(frame, request.backupId);
		WriteString(frame, request.description);
		return frame;
	}

	bool DeserializeBackupRequest(std::span<const std::uint8_t> frame,
		BackupRequest& request)
	{
		if (frame.size() < BackupRequestHeaderSize || frame.size() > MaximumBackupRequestSize ||
			!std::equal(std::begin(BackupRequestMagic), std::end(BackupRequestMagic), frame.begin()) ||
			frame[4] != BackupRequestMessageType || frame[6] > 1 || frame[7] != 0)
		{
			return false;
		}
		BackupRequest parsed;
		parsed.action = static_cast<BackupAction>(frame[5]);
		parsed.createFallbackBackup = frame[6] != 0;
		parsed.protocolVersion = ReadUint32(frame, 8);
		parsed.requestId = ReadUint64(frame, 12);
		const std::size_t idLength = ReadUint32(frame, 20);
		const std::size_t descriptionLength = ReadUint32(frame, 24);
		std::size_t offset = BackupRequestHeaderSize;
		if (idLength > BackupIdLength ||
			descriptionLength > xucrun::backup::MaximumDescriptionLength ||
			idLength + descriptionLength > (frame.size() - offset) / 2 ||
			!ReadString(frame, offset, idLength, parsed.backupId) ||
			!ReadString(frame, offset, descriptionLength, parsed.description) ||
			offset != frame.size() ||
			ValidateBackupRequest(parsed) != ContractValidationError::None)
		{
			return false;
		}
		request = std::move(parsed);
		return true;
	}

	std::vector<std::uint8_t> SerializeBackupResult(const BackupResult& result)
	{
		if (ValidateBackupResult(result) != ResultValidationError::None)
			return {};
		std::vector<std::uint8_t> frame;
		frame.reserve(BackupResultHeaderSize + result.fallbackBackupId.size() * 2 +
			result.entries.size() * (BackupEntryHeaderSize +
				(BackupIdLength + xucrun::backup::MaximumDescriptionLength) * 2));
		frame.insert(frame.end(), std::begin(BackupResultMagic), std::end(BackupResultMagic));
		frame.push_back(BackupResultMessageType);
		frame.push_back(static_cast<std::uint8_t>(result.status));
		frame.push_back(0);
		frame.push_back(0);
		WriteUint32(frame, result.protocolVersion);
		WriteUint64(frame, result.requestId);
		WriteUint32(frame, result.windowsError);
		WriteUint32(frame, static_cast<std::uint32_t>(result.fallbackBackupId.size()));
		WriteUint32(frame, static_cast<std::uint32_t>(result.entries.size()));
		WriteUint32(frame, 0);
		WriteString(frame, result.fallbackBackupId);
		for (const auto& entry : result.entries)
		{
			WriteUint32(frame, static_cast<std::uint32_t>(entry.id.size()));
			WriteUint32(frame, static_cast<std::uint32_t>(entry.description.size()));
			WriteUint64(frame, entry.createdAt);
			WriteUint32(frame, entry.credentialCount);
			WriteUint32(frame, entry.approvalCount);
			WriteUint32(frame, entry.authenticityMarkerCount);
			WriteUint32(frame, entry.metadataCount);
			WriteString(frame, entry.id);
			WriteString(frame, entry.description);
		}
		return frame.size() <= MaximumBackupResultSize ? frame : std::vector<std::uint8_t>{};
	}

	bool DeserializeBackupResult(std::span<const std::uint8_t> frame,
		BackupResult& result)
	{
		if (frame.size() < BackupResultHeaderSize || frame.size() > MaximumBackupResultSize ||
			!std::equal(std::begin(BackupResultMagic), std::end(BackupResultMagic), frame.begin()) ||
			frame[4] != BackupResultMessageType || frame[6] != 0 || frame[7] != 0 ||
			ReadUint32(frame, 32) != 0)
		{
			return false;
		}
		BackupResult parsed;
		parsed.status = static_cast<BackupStatus>(frame[5]);
		parsed.protocolVersion = ReadUint32(frame, 8);
		parsed.requestId = ReadUint64(frame, 12);
		parsed.windowsError = ReadUint32(frame, 20);
		const std::size_t fallbackLength = ReadUint32(frame, 24);
		const std::size_t entryCount = ReadUint32(frame, 28);
		std::size_t offset = BackupResultHeaderSize;
		if (fallbackLength > BackupIdLength || entryCount > MaximumBackupEntries ||
			fallbackLength > (frame.size() - offset) / 2 ||
			!ReadString(frame, offset, fallbackLength, parsed.fallbackBackupId))
		{
			return false;
		}
		parsed.entries.reserve(entryCount);
		for (std::size_t index = 0; index < entryCount; index++)
		{
			if (offset > frame.size() || frame.size() - offset < BackupEntryHeaderSize)
				return false;
			const std::size_t idLength = ReadUint32(frame, offset);
			const std::size_t descriptionLength = ReadUint32(frame, offset + 4);
			BackupEntry entry;
			entry.createdAt = ReadUint64(frame, offset + 8);
			entry.credentialCount = ReadUint32(frame, offset + 16);
			entry.approvalCount = ReadUint32(frame, offset + 20);
			entry.authenticityMarkerCount = ReadUint32(frame, offset + 24);
			entry.metadataCount = ReadUint32(frame, offset + 28);
			offset += BackupEntryHeaderSize;
			if (idLength > BackupIdLength ||
				descriptionLength > xucrun::backup::MaximumDescriptionLength ||
				idLength + descriptionLength > (frame.size() - offset) / 2 ||
				!ReadString(frame, offset, idLength, entry.id) ||
				!ReadString(frame, offset, descriptionLength, entry.description))
			{
				return false;
			}
			parsed.entries.push_back(std::move(entry));
		}
		if (offset != frame.size() ||
			ValidateBackupResult(parsed) != ResultValidationError::None)
		{
			return false;
		}
		result = std::move(parsed);
		return true;
	}

// Serializes a valid security check request into the fixed protocol format.
	std::vector<std::uint8_t> SerializeSecurityAuditRequest(
		const SecurityAuditRequest& request)
	{
		if (ValidateSecurityAuditRequest(request) != ContractValidationError::None)
			return {};
		std::vector<std::uint8_t> frame;
		frame.reserve(SecurityAuditRequestSize);
		frame.insert(frame.end(), std::begin(SecurityAuditRequestMagic),
			std::end(SecurityAuditRequestMagic));
		frame.push_back(SecurityAuditRequestMessageType);
		frame.push_back(static_cast<std::uint8_t>(request.language));
		frame.push_back(0);
		frame.push_back(0);
		WriteUint32(frame, request.protocolVersion);
		WriteUint64(frame, request.requestId);
		return frame;
	}

// Accepts a security check request only from a valid frame of the exact size.
	bool DeserializeSecurityAuditRequest(std::span<const std::uint8_t> frame,
		SecurityAuditRequest& request)
	{
		if (frame.size() != SecurityAuditRequestSize ||
			!std::equal(std::begin(SecurityAuditRequestMagic),
				std::end(SecurityAuditRequestMagic), frame.begin()) ||
			frame[4] != SecurityAuditRequestMessageType ||
			frame[6] != 0 || frame[7] != 0)
		{
			return false;
		}
		SecurityAuditRequest parsed;
		parsed.language = static_cast<SecurityAuditRequest::Language>(frame[5]);
		parsed.protocolVersion = ReadUint32(frame, 8);
		parsed.requestId = ReadUint64(frame, 12);
		if (ValidateSecurityAuditRequest(parsed) != ContractValidationError::None)
			return false;
		request = parsed;
		return true;
	}

// Serializes a valid check result into a precisely sized protocol frame.
	std::vector<std::uint8_t> SerializeSecurityAuditResult(
		const SecurityAuditResult& result)
	{
		if (ValidateSecurityAuditResult(result) != ResultValidationError::None ||
			HasEmbeddedNull(result.report))
		{
			return {};
		}
		std::vector<std::uint8_t> frame;
		frame.reserve(SecurityAuditResultHeaderSize + 2 * result.report.size());
		frame.insert(frame.end(), std::begin(SecurityAuditResultMagic),
			std::end(SecurityAuditResultMagic));
		frame.push_back(SecurityAuditResultMessageType);
		frame.push_back(static_cast<std::uint8_t>(result.status));
		frame.push_back(0);
		frame.push_back(0);
		WriteUint32(frame, result.protocolVersion);
		WriteUint64(frame, result.requestId);
		WriteUint32(frame, result.windowsError);
		WriteUint32(frame, result.findingCount);
		WriteUint32(frame, static_cast<std::uint32_t>(result.report.size()));
		WriteString(frame, result.report);
		return frame;
	}

// Accepts a check result only from a fully valid, completely consumed frame.
	bool DeserializeSecurityAuditResult(std::span<const std::uint8_t> frame,
		SecurityAuditResult& result)
	{
		if (frame.size() < SecurityAuditResultHeaderSize ||
			!std::equal(std::begin(SecurityAuditResultMagic),
				std::end(SecurityAuditResultMagic), frame.begin()) ||
			frame[4] != SecurityAuditResultMessageType ||
			frame[6] != 0 || frame[7] != 0)
		{
			return false;
		}
		const std::size_t reportLength = ReadUint32(frame, 28);
		if (reportLength > MaximumSecurityAuditReportLength ||
			frame.size() != SecurityAuditResultHeaderSize + 2 * reportLength)
		{
			return false;
		}
		SecurityAuditResult parsed;
		parsed.protocolVersion = ReadUint32(frame, 8);
		parsed.requestId = ReadUint64(frame, 12);
		parsed.status = static_cast<SecurityAuditStatus>(frame[5]);
		parsed.windowsError = ReadUint32(frame, 20);
		parsed.findingCount = ReadUint32(frame, 24);
		std::size_t offset = SecurityAuditResultHeaderSize;
		if (!ReadString(frame, offset, reportLength, parsed.report) ||
			ValidateSecurityAuditResult(parsed) != ResultValidationError::None)
		{
			return false;
		}
		result = std::move(parsed);
		return true;
	}
