// Purpose: Internal protocol implementation fragment for BrokerSerialization.cpp.
//
// Inclusion contract:
// - This file is included exactly once inside the xucrun::broker namespace.
// - It uses the translation unit's private magic values and byte-level read/write helpers.
// - It is not a standalone translation unit and introduces no additional exported symbols.
// - Field order, length checks, complete-frame validation, and sensitive-frame handling must remain exact.

// Serializes a valid start request into a precisely sized protocol frame.
	std::vector<std::uint8_t> SerializeLaunchRequest(const LaunchRequest& request)
	{
		if (ValidateLaunchRequest(request) != ContractValidationError::None ||
			(request.elevationMode != ElevationMode::PreserveToken &&
			 request.elevationMode != ElevationMode::RequireElevated) ||
			HasEmbeddedNull(request.requestedAccountSid) ||
			HasEmbeddedNull(request.executablePath) ||
			HasEmbeddedNull(request.argumentTail) ||
			HasEmbeddedNull(request.workingDirectory))
		{
			return {};
		}

		const std::size_t characterCount = request.requestedAccountSid.size() +
			request.executablePath.size() + request.argumentTail.size() +
			request.workingDirectory.size();
		std::vector<std::uint8_t> frame;
		frame.reserve(LaunchRequestHeaderSize + characterCount * 2);
		frame.insert(frame.end(), std::begin(RequestMagic), std::end(RequestMagic));
		frame.push_back(RequestMessageType);
		frame.push_back(static_cast<std::uint8_t>(request.elevationMode));
		frame.push_back(0);
		frame.push_back(0);
		WriteUint32(frame, request.protocolVersion);
		WriteUint64(frame, request.requestId);
		WriteUint32(frame, static_cast<std::uint32_t>(request.requestedAccountSid.size()));
		WriteUint32(frame, static_cast<std::uint32_t>(request.executablePath.size()));
		WriteUint32(frame, static_cast<std::uint32_t>(request.argumentTail.size()));
		WriteUint32(frame, static_cast<std::uint32_t>(request.workingDirectory.size()));
		WriteString(frame, request.requestedAccountSid);
		WriteString(frame, request.executablePath);
		WriteString(frame, request.argumentTail);
		WriteString(frame, request.workingDirectory);
		return frame;
	}

// Accepts a start request only from a fully valid, completely consumed frame.
	bool DeserializeLaunchRequest(
		std::span<const std::uint8_t> frame,
		LaunchRequest& request)
	{
		if (frame.size() < LaunchRequestHeaderSize ||
			!std::equal(std::begin(RequestMagic), std::end(RequestMagic), frame.begin()) ||
			frame[4] != RequestMessageType || frame[6] != 0 || frame[7] != 0 ||
			(frame[5] != static_cast<std::uint8_t>(ElevationMode::PreserveToken) &&
			 frame[5] != static_cast<std::uint8_t>(ElevationMode::RequireElevated)))
		{
			return false;
		}

		const std::size_t sidLength = ReadUint32(frame, 20);
		const std::size_t executableLength = ReadUint32(frame, 24);
		const std::size_t argumentLength = ReadUint32(frame, 28);
		const std::size_t workingDirectoryLength = ReadUint32(frame, 32);
		if (sidLength > MaximumAccountSidLength ||
			executableLength > MaximumPathLength ||
			argumentLength > MaximumArgumentLength ||
			workingDirectoryLength > MaximumPathLength)
		{
			return false;
		}

		const std::size_t characterCount = sidLength + executableLength +
			argumentLength + workingDirectoryLength;
		if (frame.size() != LaunchRequestHeaderSize + characterCount * 2)
			return false;

		LaunchRequest parsed;
		parsed.protocolVersion = ReadUint32(frame, 8);
		parsed.requestId = ReadUint64(frame, 12);
		parsed.elevationMode = static_cast<ElevationMode>(frame[5]);
		std::size_t offset = LaunchRequestHeaderSize;
		if (!ReadString(frame, offset, sidLength, parsed.requestedAccountSid) ||
			!ReadString(frame, offset, executableLength, parsed.executablePath) ||
			!ReadString(frame, offset, argumentLength, parsed.argumentTail) ||
			!ReadString(frame, offset, workingDirectoryLength, parsed.workingDirectory) ||
			ValidateLaunchRequest(parsed) != ContractValidationError::None)
		{
			return false;
		}

		request = std::move(parsed);
		return true;
	}

// Serializes a valid start result into a precisely sized protocol frame.
	std::vector<std::uint8_t> SerializeLaunchResult(const LaunchResult& result)
	{
		if (ValidateLaunchResult(result) != ResultValidationError::None)
			return {};

		std::vector<std::uint8_t> frame;
		frame.reserve(LaunchResultSize);
		frame.insert(frame.end(), std::begin(ResultMagic), std::end(ResultMagic));
		frame.push_back(ResultMessageType);
		frame.push_back(static_cast<std::uint8_t>(result.status));
		frame.push_back(0);
		frame.push_back(0);
		WriteUint32(frame, result.protocolVersion);
		WriteUint64(frame, result.requestId);
		WriteUint32(frame, result.windowsError);
		return frame;
	}

// Accepts a start result only from a fully valid, completely consumed frame.
	bool DeserializeLaunchResult(
		std::span<const std::uint8_t> frame,
		LaunchResult& result)
	{
		if (frame.size() != LaunchResultSize ||
			!std::equal(std::begin(ResultMagic), std::end(ResultMagic), frame.begin()) ||
			frame[4] != ResultMessageType || frame[6] != 0 || frame[7] != 0)
		{
			return false;
		}

		LaunchResult parsed;
		parsed.protocolVersion = ReadUint32(frame, 8);
		parsed.requestId = ReadUint64(frame, 12);
		parsed.status = static_cast<LaunchStatus>(frame[5]);
		parsed.windowsError = ReadUint32(frame, 20);
		if (ValidateLaunchResult(parsed) != ResultValidationError::None)
		{
			return false;
		}

		result = std::move(parsed);
		return true;
	}

// Serializes a valid health request into the fixed protocol format.
	std::vector<std::uint8_t> SerializeHealthRequest(const HealthRequest& request)
	{
		if (request.protocolVersion != ProtocolVersion || request.requestId == 0 ||
			!HasNonce(request.nonce))
		{
			return {};
		}

		std::vector<std::uint8_t> frame;
		frame.reserve(HealthRequestSize);
		frame.insert(frame.end(), std::begin(HealthRequestMagic), std::end(HealthRequestMagic));
		frame.push_back(HealthRequestMessageType);
		frame.insert(frame.end(), 3, 0);
		WriteUint32(frame, request.protocolVersion);
		WriteUint64(frame, request.requestId);
		frame.insert(frame.end(), request.nonce.begin(), request.nonce.end());
		return frame;
	}

// Accepts a health request only from a valid frame of the exact size.
	bool DeserializeHealthRequest(
		std::span<const std::uint8_t> frame,
		HealthRequest& request)
	{
		if (frame.size() != HealthRequestSize ||
			!std::equal(std::begin(HealthRequestMagic), std::end(HealthRequestMagic), frame.begin()) ||
			frame[4] != HealthRequestMessageType || frame[5] != 0 || frame[6] != 0 || frame[7] != 0)
		{
			return false;
		}

		HealthRequest parsed;
		parsed.protocolVersion = ReadUint32(frame, 8);
		parsed.requestId = ReadUint64(frame, 12);
		std::copy_n(frame.begin() + 20, parsed.nonce.size(), parsed.nonce.begin());
		if (parsed.protocolVersion != ProtocolVersion || parsed.requestId == 0 ||
			!HasNonce(parsed.nonce))
		{
			return false;
		}
		request = parsed;
		return true;
	}

// Serializes a valid health result into the fixed protocol format.
	std::vector<std::uint8_t> SerializeHealthResult(const HealthResult& result)
	{
		if (result.protocolVersion != ProtocolVersion || result.requestId == 0 ||
			!HasNonce(result.nonce) || result.brokerProcessId == 0 ||
			(result.brokerVersion.major == 0 && result.brokerVersion.minor == 0 &&
				result.brokerVersion.patch == 0 && result.brokerVersion.build == 0))
		{
			return {};
		}

		std::vector<std::uint8_t> frame;
		frame.reserve(HealthResultSize);
		frame.insert(frame.end(), std::begin(HealthResultMagic), std::end(HealthResultMagic));
		frame.push_back(HealthResultMessageType);
		frame.insert(frame.end(), 3, 0);
		WriteUint32(frame, result.protocolVersion);
		WriteUint64(frame, result.requestId);
		frame.insert(frame.end(), result.nonce.begin(), result.nonce.end());
		WriteUint32(frame, result.brokerProcessId);
		WriteUint32(frame, result.elevatedProcessCount);
		WriteUint32(frame, result.brokerVersion.major);
		WriteUint32(frame, result.brokerVersion.minor);
		WriteUint32(frame, result.brokerVersion.patch);
		WriteUint32(frame, result.brokerVersion.build);
		return frame;
	}

// Accepts a health result only from a valid frame of the exact size.
	bool DeserializeHealthResult(
		std::span<const std::uint8_t> frame,
		HealthResult& result)
	{
		if (frame.size() != HealthResultSize ||
			!std::equal(std::begin(HealthResultMagic), std::end(HealthResultMagic), frame.begin()) ||
			frame[4] != HealthResultMessageType || frame[5] != 0 || frame[6] != 0 || frame[7] != 0)
		{
			return false;
		}

		HealthResult parsed;
		parsed.protocolVersion = ReadUint32(frame, 8);
		parsed.requestId = ReadUint64(frame, 12);
		std::copy_n(frame.begin() + 20, parsed.nonce.size(), parsed.nonce.begin());
		parsed.brokerProcessId = ReadUint32(frame, 36);
		parsed.elevatedProcessCount = ReadUint32(frame, 40);
		parsed.brokerVersion.major = ReadUint32(frame, 44);
		parsed.brokerVersion.minor = ReadUint32(frame, 48);
		parsed.brokerVersion.patch = ReadUint32(frame, 52);
		parsed.brokerVersion.build = ReadUint32(frame, 56);
		if (parsed.protocolVersion != ProtocolVersion || parsed.requestId == 0 ||
			!HasNonce(parsed.nonce) || parsed.brokerProcessId == 0 ||
			(parsed.brokerVersion.major == 0 && parsed.brokerVersion.minor == 0 &&
				parsed.brokerVersion.patch == 0 && parsed.brokerVersion.build == 0))
		{
			return false;
		}
		result = parsed;
		return true;
	}

// Serializes a valid health confirmation into the fixed protocol format.
	std::vector<std::uint8_t> SerializeHealthAcknowledgement(
		const HealthAcknowledgement& acknowledgement)
	{
		if (acknowledgement.protocolVersion != ProtocolVersion ||
			acknowledgement.requestId == 0 || !HasNonce(acknowledgement.nonce))
		{
			return {};
		}

		std::vector<std::uint8_t> frame;
		frame.reserve(HealthAcknowledgementSize);
		frame.insert(frame.end(), std::begin(HealthAcknowledgementMagic),
			std::end(HealthAcknowledgementMagic));
		frame.push_back(HealthAcknowledgementMessageType);
		frame.insert(frame.end(), 3, 0);
		WriteUint32(frame, acknowledgement.protocolVersion);
		WriteUint64(frame, acknowledgement.requestId);
		frame.insert(frame.end(), acknowledgement.nonce.begin(), acknowledgement.nonce.end());
		return frame;
	}

// Accepts a health confirmation only from a valid frame of the exact size.
	bool DeserializeHealthAcknowledgement(
		std::span<const std::uint8_t> frame,
		HealthAcknowledgement& acknowledgement)
	{
		if (frame.size() != HealthAcknowledgementSize ||
			!std::equal(std::begin(HealthAcknowledgementMagic),
				std::end(HealthAcknowledgementMagic), frame.begin()) ||
			frame[4] != HealthAcknowledgementMessageType ||
			frame[5] != 0 || frame[6] != 0 || frame[7] != 0)
		{
			return false;
		}

		HealthAcknowledgement parsed;
		parsed.protocolVersion = ReadUint32(frame, 8);
		parsed.requestId = ReadUint64(frame, 12);
		std::copy_n(frame.begin() + 20, parsed.nonce.size(), parsed.nonce.begin());
		if (parsed.protocolVersion != ProtocolVersion || parsed.requestId == 0 ||
			!HasNonce(parsed.nonce))
		{
			return false;
		}
		acknowledgement = parsed;
		return true;
	}

// Serializes a valid session test request into the fixed protocol format.
	std::vector<std::uint8_t> SerializeSessionTestRequest(
		const SessionTestRequest& request)
	{
		if (request.protocolVersion != ProtocolVersion || request.requestId == 0)
			return {};
		std::vector<std::uint8_t> frame;
		frame.reserve(SessionTestRequestSize);
		frame.insert(frame.end(), std::begin(SessionTestRequestMagic),
			std::end(SessionTestRequestMagic));
		frame.push_back(SessionTestRequestMessageType);
		frame.insert(frame.end(), 3, 0);
		WriteUint32(frame, request.protocolVersion);
		WriteUint64(frame, request.requestId);
		return frame;
	}

// Accepts a session test request only from a valid frame of the exact size.
	bool DeserializeSessionTestRequest(
		std::span<const std::uint8_t> frame,
		SessionTestRequest& request)
	{
		if (frame.size() != SessionTestRequestSize ||
			!std::equal(std::begin(SessionTestRequestMagic),
				std::end(SessionTestRequestMagic), frame.begin()) ||
			frame[4] != SessionTestRequestMessageType ||
			frame[5] != 0 || frame[6] != 0 || frame[7] != 0)
		{
			return false;
		}
		SessionTestRequest parsed;
		parsed.protocolVersion = ReadUint32(frame, 8);
		parsed.requestId = ReadUint64(frame, 12);
		if (parsed.protocolVersion != ProtocolVersion || parsed.requestId == 0)
			return false;
		request = parsed;
		return true;
	}

// Serializes a valid session test result into the fixed protocol format.
	std::vector<std::uint8_t> SerializeSessionTestResult(
		const SessionTestResult& result)
	{
		if (result.protocolVersion != ProtocolVersion || result.requestId == 0 ||
			(result.status != SessionTestStatus::Succeeded &&
			 result.status != SessionTestStatus::InvalidSession &&
			 result.status != SessionTestStatus::Failed) ||
			(result.status == SessionTestStatus::Succeeded) != (result.processId != 0))
		{
			return {};
		}
		std::vector<std::uint8_t> frame;
		frame.reserve(SessionTestResultSize);
		frame.insert(frame.end(), std::begin(SessionTestResultMagic),
			std::end(SessionTestResultMagic));
		frame.push_back(SessionTestResultMessageType);
		frame.push_back(static_cast<std::uint8_t>(result.status));
		frame.push_back(0);
		frame.push_back(0);
		WriteUint32(frame, result.protocolVersion);
		WriteUint64(frame, result.requestId);
		WriteUint32(frame, result.windowsError);
		WriteUint32(frame, result.processId);
		return frame;
	}

// Accepts a session test result only from a valid frame of the exact size.
	bool DeserializeSessionTestResult(
		std::span<const std::uint8_t> frame,
		SessionTestResult& result)
	{
		if (frame.size() != SessionTestResultSize ||
			!std::equal(std::begin(SessionTestResultMagic),
				std::end(SessionTestResultMagic), frame.begin()) ||
			frame[4] != SessionTestResultMessageType ||
			frame[6] != 0 || frame[7] != 0)
		{
			return false;
		}
		SessionTestResult parsed;
		parsed.protocolVersion = ReadUint32(frame, 8);
		parsed.requestId = ReadUint64(frame, 12);
		parsed.status = static_cast<SessionTestStatus>(frame[5]);
		parsed.windowsError = ReadUint32(frame, 20);
		parsed.processId = ReadUint32(frame, 24);
		if (SerializeSessionTestResult(parsed).empty())
			return false;
		result = parsed;
		return true;
	}

// Serializes a valid process list request into the fixed protocol format.
	std::vector<std::uint8_t> SerializeTrackedProcessListRequest(
		const TrackedProcessListRequest& request)
	{
		if (ValidateTrackedProcessListRequest(request) != ContractValidationError::None)
			return {};
		std::vector<std::uint8_t> frame;
		frame.reserve(TrackedProcessListRequestSize);
		frame.insert(frame.end(), std::begin(TrackedProcessListRequestMagic),
			std::end(TrackedProcessListRequestMagic));
		frame.push_back(TrackedProcessListRequestMessageType);
		frame.insert(frame.end(), 3, 0);
		WriteUint32(frame, request.protocolVersion);
		WriteUint64(frame, request.requestId);
		return frame;
	}

// Accepts a process list request only from a valid frame of the exact size.
	bool DeserializeTrackedProcessListRequest(
		std::span<const std::uint8_t> frame, TrackedProcessListRequest& request)
	{
		if (frame.size() != TrackedProcessListRequestSize ||
			!std::equal(std::begin(TrackedProcessListRequestMagic),
				std::end(TrackedProcessListRequestMagic), frame.begin()) ||
			frame[4] != TrackedProcessListRequestMessageType ||
			frame[5] != 0 || frame[6] != 0 || frame[7] != 0)
		{
			return false;
		}
		TrackedProcessListRequest parsed;
		parsed.protocolVersion = ReadUint32(frame, 8);
		parsed.requestId = ReadUint64(frame, 12);
		if (ValidateTrackedProcessListRequest(parsed) != ContractValidationError::None)
			return false;
		request = parsed;
		return true;
	}

// Serializes a valid process list result with fully bound entries.
	std::vector<std::uint8_t> SerializeTrackedProcessListResult(
		const TrackedProcessListResult& result)
	{
		if (ValidateTrackedProcessListResult(result) != ResultValidationError::None)
			return {};
		std::size_t characterCount = 0;
		for (const TrackedProcessListEntry& entry : result.entries)
		{
			if (HasEmbeddedNull(entry.processBaseName))
				return {};
			characterCount += entry.processBaseName.size();
		}
		std::vector<std::uint8_t> frame;
		frame.reserve(TrackedProcessListResultHeaderSize +
			result.entries.size() * TrackedProcessListEntryHeaderSize + 2 * characterCount);
		frame.insert(frame.end(), std::begin(TrackedProcessListResultMagic),
			std::end(TrackedProcessListResultMagic));
		frame.push_back(TrackedProcessListResultMessageType);
		frame.push_back(static_cast<std::uint8_t>(result.status));
		frame.push_back(0);
		frame.push_back(0);
		WriteUint32(frame, result.protocolVersion);
		WriteUint64(frame, result.requestId);
		WriteUint32(frame, result.windowsError);
		WriteUint32(frame, static_cast<std::uint32_t>(result.entries.size()));
		for (const TrackedProcessListEntry& entry : result.entries)
		{
			frame.insert(frame.end(), entry.trackingId.begin(), entry.trackingId.end());
			WriteUint32(frame, entry.processId);
			WriteUint32(frame, static_cast<std::uint32_t>(entry.processBaseName.size()));
			WriteString(frame, entry.processBaseName);
		}
		return frame;
	}

// Accepts a process list result only after complete consumption of all valid entries.
	bool DeserializeTrackedProcessListResult(
		std::span<const std::uint8_t> frame, TrackedProcessListResult& result)
	{
		if (frame.size() < TrackedProcessListResultHeaderSize ||
			frame.size() > MaximumTrackedProcessListResultSize ||
			!std::equal(std::begin(TrackedProcessListResultMagic),
				std::end(TrackedProcessListResultMagic), frame.begin()) ||
			frame[4] != TrackedProcessListResultMessageType || frame[6] != 0 || frame[7] != 0)
		{
			return false;
		}
		const std::size_t entryCount = ReadUint32(frame, 24);
		if (entryCount > MaximumTrackedProcessEntries)
			return false;
		TrackedProcessListResult parsed;
		parsed.protocolVersion = ReadUint32(frame, 8);
		parsed.requestId = ReadUint64(frame, 12);
		parsed.status = static_cast<CredentialListStatus>(frame[5]);
		parsed.windowsError = ReadUint32(frame, 20);
		std::size_t offset = TrackedProcessListResultHeaderSize;
		parsed.entries.reserve(entryCount);
		for (std::size_t index = 0; index < entryCount; index++)
		{
			if (offset > frame.size() || frame.size() - offset < TrackedProcessListEntryHeaderSize)
				return false;
			TrackedProcessListEntry entry;
			std::copy_n(frame.begin() + offset, entry.trackingId.size(), entry.trackingId.begin());
			entry.processId = ReadUint32(frame, offset + 16);
			const std::size_t nameLength = ReadUint32(frame, offset + 20);
			offset += TrackedProcessListEntryHeaderSize;
			if (nameLength == 0 || nameLength > MaximumProcessBaseNameLength ||
				nameLength > (frame.size() - offset) / 2 ||
				!ReadString(frame, offset, nameLength, entry.processBaseName))
			{
				return false;
			}
			parsed.entries.push_back(std::move(entry));
		}
		if (offset != frame.size() ||
			ValidateTrackedProcessListResult(parsed) != ResultValidationError::None)
		{
			return false;
		}
		result = std::move(parsed);
		return true;
	}

	std::vector<std::uint8_t> SerializeStatusIconListRequest(
		const StatusIconListRequest& request)
	{
		if (ValidateStatusIconListRequest(request) != ContractValidationError::None)
			return {};
		std::vector<std::uint8_t> frame;
		frame.reserve(StatusIconListRequestSize);
		frame.insert(frame.end(), std::begin(StatusIconListRequestMagic),
			std::end(StatusIconListRequestMagic));
		frame.push_back(StatusIconListRequestMessageType);
		frame.insert(frame.end(), 3, 0);
		WriteUint32(frame, request.protocolVersion);
		WriteUint64(frame, request.requestId);
		WriteUint32(frame, request.clientVersion.major);
		WriteUint32(frame, request.clientVersion.minor);
		WriteUint32(frame, request.clientVersion.patch);
		WriteUint32(frame, request.clientVersion.build);
		WriteUint32(frame, request.includeAllSessions ? 1 : 0);
		return frame;
	}

	bool DeserializeStatusIconListRequest(
		std::span<const std::uint8_t> frame, StatusIconListRequest& request)
	{
		if (frame.size() != StatusIconListRequestSize ||
			!std::equal(std::begin(StatusIconListRequestMagic),
				std::end(StatusIconListRequestMagic), frame.begin()) ||
			frame[4] != StatusIconListRequestMessageType ||
			frame[5] != 0 || frame[6] != 0 || frame[7] != 0)
		{
			return false;
		}
		StatusIconListRequest parsed;
		parsed.protocolVersion = ReadUint32(frame, 8);
		parsed.requestId = ReadUint64(frame, 12);
		parsed.clientVersion.major = ReadUint32(frame, 20);
		parsed.clientVersion.minor = ReadUint32(frame, 24);
		parsed.clientVersion.patch = ReadUint32(frame, 28);
		parsed.clientVersion.build = ReadUint32(frame, 32);
		const std::uint32_t includeAllSessions = ReadUint32(frame, 36);
		if (includeAllSessions > 1)
			return false;
		parsed.includeAllSessions = includeAllSessions != 0;
		if (ValidateStatusIconListRequest(parsed) != ContractValidationError::None)
			return false;
		request = parsed;
		return true;
	}

	std::vector<std::uint8_t> SerializeBrokerPolicyRejection(
		const BrokerPolicyRejection& rejection)
	{
		if (rejection.protocolVersion != ProtocolVersion ||
			rejection.requestId == 0 ||
			rejection.windowsError != PolicyDeniedWindowsError)
		{
			return {};
		}
		std::vector<std::uint8_t> frame;
		frame.reserve(BrokerPolicyRejectionSize);
		frame.insert(frame.end(), std::begin(BrokerPolicyRejectionMagic),
			std::end(BrokerPolicyRejectionMagic));
		frame.push_back(BrokerPolicyRejectionMessageType);
		frame.insert(frame.end(), 3, 0);
		WriteUint32(frame, rejection.protocolVersion);
		WriteUint64(frame, rejection.requestId);
		WriteUint32(frame, rejection.windowsError);
		return frame;
	}

	bool DeserializeBrokerPolicyRejection(
		std::span<const std::uint8_t> frame, BrokerPolicyRejection& rejection)
	{
		if (frame.size() != BrokerPolicyRejectionSize ||
			!std::equal(std::begin(BrokerPolicyRejectionMagic),
				std::end(BrokerPolicyRejectionMagic), frame.begin()) ||
			frame[4] != BrokerPolicyRejectionMessageType ||
			frame[5] != 0 || frame[6] != 0 || frame[7] != 0)
		{
			return false;
		}
		BrokerPolicyRejection parsed;
		parsed.protocolVersion = ReadUint32(frame, 8);
		parsed.requestId = ReadUint64(frame, 12);
		parsed.windowsError = ReadUint32(frame, 20);
		if (parsed.protocolVersion != ProtocolVersion ||
			parsed.requestId == 0 ||
			parsed.windowsError != PolicyDeniedWindowsError)
		{
			return false;
		}
		rejection = parsed;
		return true;
	}

	std::vector<std::uint8_t> SerializeStatusIconListResult(
		const StatusIconListResult& result)
	{
		if (ValidateStatusIconListResult(result) != ResultValidationError::None)
			return {};
		std::size_t characterCount = 0;
		for (const StatusIconListEntry& entry : result.entries)
		{
			if (HasEmbeddedNull(entry.accountSid) ||
				HasEmbeddedNull(entry.canonicalAccountName))
				return {};
			characterCount += entry.accountSid.size() + entry.canonicalAccountName.size();
		}
		std::vector<std::uint8_t> frame;
		frame.reserve(StatusIconListResultHeaderSize +
			result.entries.size() * StatusIconListEntryHeaderSize + 2 * characterCount);
		frame.insert(frame.end(), std::begin(StatusIconListResultMagic),
			std::end(StatusIconListResultMagic));
		frame.push_back(StatusIconListResultMessageType);
		frame.push_back(static_cast<std::uint8_t>(result.status));
		frame.insert(frame.end(), 2, 0);
		WriteUint32(frame, result.protocolVersion);
		WriteUint64(frame, result.requestId);
		WriteUint32(frame, result.windowsError);
		WriteUint32(frame, static_cast<std::uint32_t>(result.entries.size()));
		for (const StatusIconListEntry& entry : result.entries)
		{
			WriteUint32(frame, entry.processId);
			frame.push_back(entry.elevated ? 1 : 0);
			frame.insert(frame.end(), 3, 0);
			WriteUint32(frame, entry.clientVersion.major);
			WriteUint32(frame, entry.clientVersion.minor);
			WriteUint32(frame, entry.clientVersion.patch);
			WriteUint32(frame, entry.clientVersion.build);
			WriteUint32(frame, static_cast<std::uint32_t>(entry.accountSid.size()));
			WriteUint32(frame,
				static_cast<std::uint32_t>(entry.canonicalAccountName.size()));
			WriteString(frame, entry.accountSid);
			WriteString(frame, entry.canonicalAccountName);
		}
		return frame;
	}

	bool DeserializeStatusIconListResult(
		std::span<const std::uint8_t> frame, StatusIconListResult& result)
	{
		if (frame.size() < StatusIconListResultHeaderSize ||
			frame.size() > MaximumStatusIconListResultSize ||
			!std::equal(std::begin(StatusIconListResultMagic),
				std::end(StatusIconListResultMagic), frame.begin()) ||
			frame[4] != StatusIconListResultMessageType || frame[6] != 0 || frame[7] != 0)
			return false;
		const std::size_t entryCount = ReadUint32(frame, 24);
		if (entryCount > MaximumStatusIconEntries)
			return false;
		StatusIconListResult parsed;
		parsed.protocolVersion = ReadUint32(frame, 8);
		parsed.requestId = ReadUint64(frame, 12);
		parsed.status = static_cast<CredentialListStatus>(frame[5]);
		parsed.windowsError = ReadUint32(frame, 20);
		std::size_t offset = StatusIconListResultHeaderSize;
		parsed.entries.reserve(entryCount);
		for (std::size_t index = 0; index < entryCount; ++index)
		{
			if (offset > frame.size() || frame.size() - offset < StatusIconListEntryHeaderSize)
				return false;
			StatusIconListEntry entry;
			entry.processId = ReadUint32(frame, offset);
			entry.elevated = frame[offset + 4] != 0;
			if (frame[offset + 4] > 1 || frame[offset + 5] != 0 ||
				frame[offset + 6] != 0 || frame[offset + 7] != 0)
				return false;
			entry.clientVersion.major = ReadUint32(frame, offset + 8);
			entry.clientVersion.minor = ReadUint32(frame, offset + 12);
			entry.clientVersion.patch = ReadUint32(frame, offset + 16);
			entry.clientVersion.build = ReadUint32(frame, offset + 20);
			const std::size_t sidLength = ReadUint32(frame, offset + 24);
			const std::size_t nameLength = ReadUint32(frame, offset + 28);
			offset += StatusIconListEntryHeaderSize;
			if (sidLength == 0 || sidLength > MaximumAccountSidLength ||
				nameLength == 0 || nameLength > MaximumCanonicalAccountNameLength ||
				sidLength + nameLength > (frame.size() - offset) / 2 ||
				!ReadString(frame, offset, sidLength, entry.accountSid) ||
				!ReadString(frame, offset, nameLength, entry.canonicalAccountName))
				return false;
			parsed.entries.push_back(std::move(entry));
		}
		if (offset != frame.size() ||
			ValidateStatusIconListResult(parsed) != ResultValidationError::None)
			return false;
		result = std::move(parsed);
		return true;
	}

// Serializes a valid process registration into the fixed protocol format.
	std::vector<std::uint8_t> SerializeTrackedProcessRegistrationRequest(
		const TrackedProcessRegistrationRequest& request)
	{
		if (ValidateTrackedProcessRegistrationRequest(request) !=
			ContractValidationError::None)
		{
			return {};
		}
		std::vector<std::uint8_t> frame;
		frame.reserve(TrackedProcessRegistrationRequestSize);
		frame.insert(frame.end(), std::begin(TrackedProcessRegistrationRequestMagic),
			std::end(TrackedProcessRegistrationRequestMagic));
		frame.push_back(TrackedProcessRegistrationRequestMessageType);
		frame.insert(frame.end(), 3, 0);
		WriteUint32(frame, request.protocolVersion);
		WriteUint64(frame, request.requestId);
		WriteUint64(frame, request.processHandle);
		WriteUint32(frame, request.processId);
		return frame;
	}

// Accepts a process registration only from a valid frame of the exact size.
	bool DeserializeTrackedProcessRegistrationRequest(
		std::span<const std::uint8_t> frame,
		TrackedProcessRegistrationRequest& request)
	{
		if (frame.size() != TrackedProcessRegistrationRequestSize ||
			!std::equal(std::begin(TrackedProcessRegistrationRequestMagic),
				std::end(TrackedProcessRegistrationRequestMagic), frame.begin()) ||
			frame[4] != TrackedProcessRegistrationRequestMessageType ||
			frame[5] != 0 || frame[6] != 0 || frame[7] != 0)
		{
			return false;
		}
		TrackedProcessRegistrationRequest parsed;
		parsed.protocolVersion = ReadUint32(frame, 8);
		parsed.requestId = ReadUint64(frame, 12);
		parsed.processHandle = ReadUint64(frame, 20);
		parsed.processId = ReadUint32(frame, 28);
		if (ValidateTrackedProcessRegistrationRequest(parsed) !=
			ContractValidationError::None)
		{
			return false;
		}
		request = parsed;
		return true;
	}

// Serializes a valid registration result into the fixed protocol format.
	std::vector<std::uint8_t> SerializeTrackedProcessRegistrationResult(
		const TrackedProcessRegistrationResult& result)
	{
		if (ValidateTrackedProcessRegistrationResult(result) !=
			ResultValidationError::None)
		{
			return {};
		}
		std::vector<std::uint8_t> frame;
		frame.reserve(TrackedProcessRegistrationResultSize);
		frame.insert(frame.end(), std::begin(TrackedProcessRegistrationResultMagic),
			std::end(TrackedProcessRegistrationResultMagic));
		frame.push_back(TrackedProcessRegistrationResultMessageType);
		frame.push_back(static_cast<std::uint8_t>(result.status));
		frame.push_back(0);
		frame.push_back(0);
		WriteUint32(frame, result.protocolVersion);
		WriteUint64(frame, result.requestId);
		WriteUint32(frame, result.windowsError);
		return frame;
	}

// Accepts a registration result only from a valid frame of the exact size.
	bool DeserializeTrackedProcessRegistrationResult(
		std::span<const std::uint8_t> frame,
		TrackedProcessRegistrationResult& result)
	{
		if (frame.size() != TrackedProcessRegistrationResultSize ||
			!std::equal(std::begin(TrackedProcessRegistrationResultMagic),
				std::end(TrackedProcessRegistrationResultMagic), frame.begin()) ||
			frame[4] != TrackedProcessRegistrationResultMessageType ||
			frame[6] != 0 || frame[7] != 0)
		{
			return false;
		}
		TrackedProcessRegistrationResult parsed;
		parsed.protocolVersion = ReadUint32(frame, 8);
		parsed.requestId = ReadUint64(frame, 12);
		parsed.status = static_cast<TrackedProcessRegistrationStatus>(frame[5]);
		parsed.windowsError = ReadUint32(frame, 20);
		if (ValidateTrackedProcessRegistrationResult(parsed) !=
			ResultValidationError::None)
		{
			return false;
		}
		result = parsed;
		return true;
	}

// Serializes a valid broker stop request into the fixed protocol format.
	std::vector<std::uint8_t> SerializeBrokerStopRequest(
		const BrokerStopRequest& request)
	{
		if (ValidateBrokerStopRequest(request) != ContractValidationError::None)
			return {};
		std::vector<std::uint8_t> frame;
		frame.reserve(BrokerStopRequestSize);
		frame.insert(frame.end(), std::begin(BrokerStopRequestMagic),
			std::end(BrokerStopRequestMagic));
		frame.push_back(BrokerStopRequestMessageType);
		frame.push_back(static_cast<std::uint8_t>(request.action));
		frame.insert(frame.end(), 2, 0);
		WriteUint32(frame, request.protocolVersion);
		WriteUint64(frame, request.requestId);
		return frame;
	}

// Accepts a broker stop request only from a valid frame of the exact size.
	bool DeserializeBrokerStopRequest(std::span<const std::uint8_t> frame,
		BrokerStopRequest& request)
	{
		if (frame.size() != BrokerStopRequestSize ||
			!std::equal(std::begin(BrokerStopRequestMagic),
				std::end(BrokerStopRequestMagic), frame.begin()) ||
			frame[4] != BrokerStopRequestMessageType ||
			frame[6] != 0 || frame[7] != 0)
		{
			return false;
		}
		BrokerStopRequest parsed;
		parsed.action = static_cast<BrokerServiceControlAction>(frame[5]);
		parsed.protocolVersion = ReadUint32(frame, 8);
		parsed.requestId = ReadUint64(frame, 12);
		if (ValidateBrokerStopRequest(parsed) != ContractValidationError::None)
			return false;
		request = parsed;
		return true;
	}

// Serializes a valid broker stop result into the fixed protocol format.
	std::vector<std::uint8_t> SerializeBrokerStopResult(
		const BrokerStopResult& result)
	{
		if (ValidateBrokerStopResult(result) != ResultValidationError::None)
			return {};
		std::vector<std::uint8_t> frame;
		frame.reserve(BrokerStopResultSize);
		frame.insert(frame.end(), std::begin(BrokerStopResultMagic),
			std::end(BrokerStopResultMagic));
		frame.push_back(BrokerStopResultMessageType);
		frame.push_back(static_cast<std::uint8_t>(result.status));
		frame.push_back(0);
		frame.push_back(0);
		WriteUint32(frame, result.protocolVersion);
		WriteUint64(frame, result.requestId);
		WriteUint32(frame, result.windowsError);
		return frame;
	}

// Accepts a broker stop result only from a valid frame of the exact size.
	bool DeserializeBrokerStopResult(std::span<const std::uint8_t> frame,
		BrokerStopResult& result)
	{
		if (frame.size() != BrokerStopResultSize ||
			!std::equal(std::begin(BrokerStopResultMagic),
				std::end(BrokerStopResultMagic), frame.begin()) ||
			frame[4] != BrokerStopResultMessageType || frame[6] != 0 || frame[7] != 0)
		{
			return false;
		}
		BrokerStopResult parsed;
		parsed.protocolVersion = ReadUint32(frame, 8);
		parsed.requestId = ReadUint64(frame, 12);
		parsed.status = static_cast<BrokerStopStatus>(frame[5]);
		parsed.windowsError = ReadUint32(frame, 20);
		if (ValidateBrokerStopResult(parsed) != ResultValidationError::None)
			return false;
		result = parsed;
		return true;
	}
