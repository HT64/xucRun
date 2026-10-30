// Purpose: Internal protocol implementation fragment for BrokerSerialization.cpp.
// Approach: Provides implementation helpers included by the owning translation unit to keep the related flow grouped.
//
// Inclusion contract:
// - This file is included exactly once inside the xucrun::broker namespace.
// - It uses the translation unit's private magic values and byte-level read/write helpers.
// - It is not a standalone translation unit and introduces no additional exported symbols.
// - Field order, length checks, complete-frame validation, and sensitive-frame handling must remain exact.

// Serializes a valid metadata request into a precisely sized protocol frame.
	std::vector<std::uint8_t> SerializeCredentialMetadataRequest(
		const CredentialMetadataRequest& request)
	{
		if (ValidateCredentialMetadataRequest(request) != ContractValidationError::None ||
			HasEmbeddedNull(request.requestedAccountSid))
		{
			return {};
		}
		std::vector<std::uint8_t> frame;
		frame.reserve(CredentialMetadataRequestHeaderSize +
			request.requestedAccountSid.size() * 2);
		frame.insert(frame.end(), std::begin(CredentialMetadataRequestMagic),
			std::end(CredentialMetadataRequestMagic));
		frame.push_back(CredentialMetadataRequestMessageType);
		frame.insert(frame.end(), 3, 0);
		WriteUint32(frame, request.protocolVersion);
		WriteUint64(frame, request.requestId);
		WriteUint32(frame, static_cast<std::uint32_t>(request.requestedAccountSid.size()));
		WriteString(frame, request.requestedAccountSid);
		return frame;
	}

// Accepts a metadata request only from a fully valid, completely consumed frame.
	bool DeserializeCredentialMetadataRequest(
		std::span<const std::uint8_t> frame,
		CredentialMetadataRequest& request)
	{
		if (frame.size() < CredentialMetadataRequestHeaderSize ||
			!std::equal(std::begin(CredentialMetadataRequestMagic),
				std::end(CredentialMetadataRequestMagic), frame.begin()) ||
			frame[4] != CredentialMetadataRequestMessageType ||
			frame[5] != 0 || frame[6] != 0 || frame[7] != 0)
		{
			return false;
		}
		const std::size_t sidLength = ReadUint32(frame, 20);
		if (sidLength > MaximumAccountSidLength ||
			frame.size() != CredentialMetadataRequestHeaderSize + sidLength * 2)
		{
			return false;
		}
		CredentialMetadataRequest parsed;
		parsed.protocolVersion = ReadUint32(frame, 8);
		parsed.requestId = ReadUint64(frame, 12);
		std::size_t offset = CredentialMetadataRequestHeaderSize;
		if (!ReadString(frame, offset, sidLength, parsed.requestedAccountSid) ||
			ValidateCredentialMetadataRequest(parsed) != ContractValidationError::None)
		{
			return false;
		}
		request = std::move(parsed);
		return true;
	}

// Serializes a valid metadata result into the fixed protocol format.
	std::vector<std::uint8_t> SerializeCredentialMetadataResult(
		const CredentialMetadataResult& result)
	{
		if (ValidateCredentialMetadataResult(result) != ResultValidationError::None)
			return {};
		std::vector<std::uint8_t> frame;
		frame.reserve(CredentialMetadataResultSize);
		frame.insert(frame.end(), std::begin(CredentialMetadataResultMagic),
			std::end(CredentialMetadataResultMagic));
		frame.push_back(CredentialMetadataResultMessageType);
		frame.push_back(static_cast<std::uint8_t>(result.status));
		frame.push_back(0);
		frame.push_back(0);
		WriteUint32(frame, result.protocolVersion);
		WriteUint64(frame, result.requestId);
		WriteUint32(frame, result.windowsError);
		return frame;
	}

// Accepts a metadata result only from a valid frame of the exact size.
	bool DeserializeCredentialMetadataResult(
		std::span<const std::uint8_t> frame,
		CredentialMetadataResult& result)
	{
		if (frame.size() != CredentialMetadataResultSize ||
			!std::equal(std::begin(CredentialMetadataResultMagic),
				std::end(CredentialMetadataResultMagic), frame.begin()) ||
			frame[4] != CredentialMetadataResultMessageType ||
			frame[6] != 0 || frame[7] != 0)
		{
			return false;
		}
		CredentialMetadataResult parsed;
		parsed.protocolVersion = ReadUint32(frame, 8);
		parsed.requestId = ReadUint64(frame, 12);
		parsed.status = static_cast<CredentialMetadataStatus>(frame[5]);
		parsed.windowsError = ReadUint32(frame, 20);
		if (ValidateCredentialMetadataResult(parsed) != ResultValidationError::None)
			return false;
		result = parsed;
		return true;
	}

// Serializes a valid store test request into the fixed protocol format.
	std::vector<std::uint8_t> SerializeCredentialStoreTestRequest(
		const CredentialStoreTestRequest& request)
	{
		if (request.protocolVersion != ProtocolVersion || request.requestId == 0)
			return {};
		std::vector<std::uint8_t> frame;
		frame.reserve(CredentialStoreTestRequestSize);
		frame.insert(frame.end(), std::begin(CredentialStoreTestRequestMagic),
			std::end(CredentialStoreTestRequestMagic));
		frame.push_back(CredentialStoreTestRequestMessageType);
		frame.insert(frame.end(), 3, 0);
		WriteUint32(frame, request.protocolVersion);
		WriteUint64(frame, request.requestId);
		return frame;
	}

// Accepts a store test request only from a valid frame of the exact size.
	bool DeserializeCredentialStoreTestRequest(
		std::span<const std::uint8_t> frame,
		CredentialStoreTestRequest& request)
	{
		if (frame.size() != CredentialStoreTestRequestSize ||
			!std::equal(std::begin(CredentialStoreTestRequestMagic),
				std::end(CredentialStoreTestRequestMagic), frame.begin()) ||
			frame[4] != CredentialStoreTestRequestMessageType ||
			frame[5] != 0 || frame[6] != 0 || frame[7] != 0)
		{
			return false;
		}
		CredentialStoreTestRequest parsed;
		parsed.protocolVersion = ReadUint32(frame, 8);
		parsed.requestId = ReadUint64(frame, 12);
		if (parsed.protocolVersion != ProtocolVersion || parsed.requestId == 0)
			return false;
		request = parsed;
		return true;
	}

// Serializes a valid store test result into the fixed protocol format.
	std::vector<std::uint8_t> SerializeCredentialStoreTestResult(
		const CredentialStoreTestResult& result)
	{
		if (result.protocolVersion != ProtocolVersion || result.requestId == 0 ||
			(result.status != CredentialStoreTestStatus::Succeeded &&
			 result.status != CredentialStoreTestStatus::Failed))
		{
			return {};
		}
		std::vector<std::uint8_t> frame;
		frame.reserve(CredentialStoreTestResultSize);
		frame.insert(frame.end(), std::begin(CredentialStoreTestResultMagic),
			std::end(CredentialStoreTestResultMagic));
		frame.push_back(CredentialStoreTestResultMessageType);
		frame.push_back(static_cast<std::uint8_t>(result.status));
		frame.push_back(0);
		frame.push_back(0);
		WriteUint32(frame, result.protocolVersion);
		WriteUint64(frame, result.requestId);
		WriteUint32(frame, result.windowsError);
		return frame;
	}

// Accepts a store test result only from a valid frame of the exact size.
	bool DeserializeCredentialStoreTestResult(
		std::span<const std::uint8_t> frame,
		CredentialStoreTestResult& result)
	{
		if (frame.size() != CredentialStoreTestResultSize ||
			!std::equal(std::begin(CredentialStoreTestResultMagic),
				std::end(CredentialStoreTestResultMagic), frame.begin()) ||
			frame[4] != CredentialStoreTestResultMessageType ||
			frame[6] != 0 || frame[7] != 0)
		{
			return false;
		}
		CredentialStoreTestResult parsed;
		parsed.protocolVersion = ReadUint32(frame, 8);
		parsed.requestId = ReadUint64(frame, 12);
		parsed.status = static_cast<CredentialStoreTestStatus>(frame[5]);
		parsed.windowsError = ReadUint32(frame, 20);
		if (SerializeCredentialStoreTestResult(parsed).empty())
			return false;
		result = parsed;
		return true;
	}

// Serializes the account SID and password only from a valid store request.
	std::vector<std::uint8_t> SerializeCredentialSaveRequest(
		const CredentialSaveRequest& request)
	{
		if (ValidateCredentialSaveRequest(request) != ContractValidationError::None ||
			HasEmbeddedNull(request.requestedAccountSid))
		{
			return {};
		}
		std::vector<std::uint8_t> frame;
		frame.reserve(CredentialSaveRequestHeaderSize + 2 *
			(request.requestedAccountSid.size() + request.password.size()));
		frame.insert(frame.end(), std::begin(CredentialSaveRequestMagic),
			std::end(CredentialSaveRequestMagic));
		frame.push_back(CredentialSaveRequestMessageType);
		frame.insert(frame.end(), 3, 0);
		WriteUint32(frame, request.protocolVersion);
		WriteUint64(frame, request.requestId);
		WriteUint32(frame, static_cast<std::uint32_t>(request.requestedAccountSid.size()));
		WriteUint32(frame, static_cast<std::uint32_t>(request.password.size()));
		WriteString(frame, request.requestedAccountSid);
		WriteString(frame, request.password.c_str(), request.password.size());
		return frame;
	}

// Accepts the SID and sensitive password only from a completely validated frame.
	bool DeserializeCredentialSaveRequest(
		std::span<const std::uint8_t> frame,
		CredentialSaveRequest& request)
	{
		if (frame.size() < CredentialSaveRequestHeaderSize ||
			!std::equal(std::begin(CredentialSaveRequestMagic),
				std::end(CredentialSaveRequestMagic), frame.begin()) ||
			frame[4] != CredentialSaveRequestMessageType ||
			frame[5] != 0 || frame[6] != 0 || frame[7] != 0)
		{
			return false;
		}
		const std::size_t sidLength = ReadUint32(frame, 20);
		const std::size_t passwordLength = ReadUint32(frame, 24);
		if (sidLength > MaximumAccountSidLength || passwordLength > MaximumPasswordLength ||
			frame.size() != CredentialSaveRequestHeaderSize +
				2 * (sidLength + passwordLength))
		{
			return false;
		}
		CredentialSaveRequest parsed;
		parsed.protocolVersion = ReadUint32(frame, 8);
		parsed.requestId = ReadUint64(frame, 12);
		std::size_t offset = CredentialSaveRequestHeaderSize;
		if (!ReadString(frame, offset, sidLength, parsed.requestedAccountSid))
			return false;
		const auto passwordBytes = frame.subspan(offset, passwordLength * 2);
		if (!parsed.password.assignUtf16(passwordBytes, passwordLength) ||
			ValidateCredentialSaveRequest(parsed) != ContractValidationError::None)
		{
			return false;
		}
		request = std::move(parsed);
		return true;
	}

// Identifies store requests exclusively by a complete magic and type prefix.
	bool IsCredentialSaveRequestFrame(
		std::span<const std::uint8_t> frame) noexcept
	{
		return frame.size() >= 5 &&
			std::equal(std::begin(CredentialSaveRequestMagic),
				std::end(CredentialSaveRequestMagic), frame.begin()) &&
			frame[4] == CredentialSaveRequestMessageType;
	}

// Serializes a valid store result into the fixed protocol format.
	std::vector<std::uint8_t> SerializeCredentialSaveResult(
		const CredentialSaveResult& result)
	{
		if (ValidateCredentialSaveResult(result) != ResultValidationError::None)
			return {};
		std::vector<std::uint8_t> frame;
		frame.reserve(CredentialSaveResultSize);
		frame.insert(frame.end(), std::begin(CredentialSaveResultMagic),
			std::end(CredentialSaveResultMagic));
		frame.push_back(CredentialSaveResultMessageType);
		frame.push_back(static_cast<std::uint8_t>(result.status));
		frame.push_back(0);
		frame.push_back(0);
		WriteUint32(frame, result.protocolVersion);
		WriteUint64(frame, result.requestId);
		WriteUint32(frame, result.windowsError);
		return frame;
	}

// Accepts a store result only from a valid frame of the exact size.
	bool DeserializeCredentialSaveResult(
		std::span<const std::uint8_t> frame,
		CredentialSaveResult& result)
	{
		if (frame.size() != CredentialSaveResultSize ||
			!std::equal(std::begin(CredentialSaveResultMagic),
				std::end(CredentialSaveResultMagic), frame.begin()) ||
			frame[4] != CredentialSaveResultMessageType ||
			frame[6] != 0 || frame[7] != 0)
		{
			return false;
		}
		CredentialSaveResult parsed;
		parsed.protocolVersion = ReadUint32(frame, 8);
		parsed.requestId = ReadUint64(frame, 12);
		parsed.status = static_cast<CredentialSaveStatus>(frame[5]);
		parsed.windowsError = ReadUint32(frame, 20);
		if (ValidateCredentialSaveResult(parsed) != ResultValidationError::None)
			return false;
		result = parsed;
		return true;
	}

	std::vector<std::uint8_t> SerializeCredentialCaptureChallengeRequest(
		const CredentialCaptureChallengeRequest& request)
	{
		if (ValidateCredentialCaptureChallengeRequest(request) !=
			ContractValidationError::None || HasEmbeddedNull(request.requestedAccountSid) ||
			HasEmbeddedNull(request.approvalAccountSid))
			return {};
		std::vector<std::uint8_t> frame;
		frame.reserve(CredentialCaptureChallengeRequestHeaderSize +
			2 * (request.requestedAccountSid.size() + request.approvalAccountSid.size()));
		frame.insert(frame.end(), std::begin(CredentialCaptureChallengeRequestMagic),
			std::end(CredentialCaptureChallengeRequestMagic));
		frame.push_back(CredentialCaptureChallengeRequestMessageType);
		frame.insert(frame.end(), 3, 0);
		WriteUint32(frame, request.protocolVersion);
		WriteUint64(frame, request.requestId);
		WriteUint32(frame, static_cast<std::uint32_t>(request.requestedAccountSid.size()));
		WriteUint32(frame, static_cast<std::uint32_t>(request.approvalAccountSid.size()));
		WriteString(frame, request.requestedAccountSid);
		WriteString(frame, request.approvalAccountSid);
		return frame;
	}

	bool DeserializeCredentialCaptureChallengeRequest(
		std::span<const std::uint8_t> frame,
		CredentialCaptureChallengeRequest& request)
	{
		if (frame.size() < CredentialCaptureChallengeRequestHeaderSize ||
			!std::equal(std::begin(CredentialCaptureChallengeRequestMagic),
				std::end(CredentialCaptureChallengeRequestMagic), frame.begin()) ||
			frame[4] != CredentialCaptureChallengeRequestMessageType ||
			frame[5] != 0 || frame[6] != 0 || frame[7] != 0)
			return false;
		const std::size_t sidLength = ReadUint32(frame, 20);
		const std::size_t approvalSidLength = ReadUint32(frame, 24);
		if (sidLength > MaximumAccountSidLength ||
			approvalSidLength > MaximumAccountSidLength ||
			frame.size() != CredentialCaptureChallengeRequestHeaderSize +
				2 * (sidLength + approvalSidLength))
			return false;
		CredentialCaptureChallengeRequest parsed;
		parsed.protocolVersion = ReadUint32(frame, 8);
		parsed.requestId = ReadUint64(frame, 12);
		std::size_t offset = CredentialCaptureChallengeRequestHeaderSize;
		if (!ReadString(frame, offset, sidLength, parsed.requestedAccountSid) ||
			!ReadString(frame, offset, approvalSidLength, parsed.approvalAccountSid) ||
			ValidateCredentialCaptureChallengeRequest(parsed) != ContractValidationError::None)
			return false;
		request = std::move(parsed);
		return true;
	}

	bool IsCredentialCaptureChallengeRequestFrame(
		std::span<const std::uint8_t> frame) noexcept
	{
		return frame.size() >= 5 &&
			std::equal(std::begin(CredentialCaptureChallengeRequestMagic),
				std::end(CredentialCaptureChallengeRequestMagic), frame.begin()) &&
			frame[4] == CredentialCaptureChallengeRequestMessageType;
	}

	std::vector<std::uint8_t> SerializeCredentialCaptureChallengeResult(
		const CredentialCaptureChallengeResult& result)
	{
		if (ValidateCredentialCaptureChallengeResult(result) != ResultValidationError::None ||
			HasEmbeddedNull(result.canonicalClientName) ||
			HasEmbeddedNull(result.canonicalAccountName))
			return {};
		std::vector<std::uint8_t> frame;
		frame.reserve(CredentialCaptureChallengeResultHeaderSize + 2 *
			(result.canonicalClientName.size() + result.canonicalAccountName.size()));
		frame.insert(frame.end(), std::begin(CredentialCaptureChallengeResultMagic),
			std::end(CredentialCaptureChallengeResultMagic));
		frame.push_back(CredentialCaptureChallengeResultMessageType);
		frame.push_back(static_cast<std::uint8_t>(result.status));
		frame.insert(frame.end(), 2, 0);
		WriteUint32(frame, result.protocolVersion);
		WriteUint64(frame, result.requestId);
		WriteUint32(frame, result.windowsError);
		frame.insert(frame.end(), result.challengeId.begin(), result.challengeId.end());
		WriteUint32(frame, static_cast<std::uint32_t>(result.canonicalClientName.size()));
		WriteUint32(frame, static_cast<std::uint32_t>(result.canonicalAccountName.size()));
		frame.push_back(static_cast<std::uint8_t>(result.helperStartMode));
		frame.insert(frame.end(), 7, 0);
		WriteUint64(frame, result.approvalHelperProcessHandle);
		WriteString(frame, result.canonicalClientName);
		WriteString(frame, result.canonicalAccountName);
		return frame;
	}

	bool DeserializeCredentialCaptureChallengeResult(
		std::span<const std::uint8_t> frame,
		CredentialCaptureChallengeResult& result)
	{
		if (frame.size() < CredentialCaptureChallengeResultHeaderSize ||
			!std::equal(std::begin(CredentialCaptureChallengeResultMagic),
				std::end(CredentialCaptureChallengeResultMagic), frame.begin()) ||
			frame[4] != CredentialCaptureChallengeResultMessageType || frame[6] != 0 ||
			frame[7] != 0 || std::any_of(frame.begin() + 49, frame.begin() + 56,
				[](std::uint8_t value) { return value != 0; }))
			return false;
		const std::size_t clientLength = ReadUint32(frame, 40);
		const std::size_t accountLength = ReadUint32(frame, 44);
		if (clientLength > MaximumCanonicalAccountNameLength ||
			accountLength > MaximumCanonicalAccountNameLength ||
			frame.size() != CredentialCaptureChallengeResultHeaderSize +
				2 * (clientLength + accountLength))
			return false;
		CredentialCaptureChallengeResult parsed;
		parsed.protocolVersion = ReadUint32(frame, 8);
		parsed.requestId = ReadUint64(frame, 12);
		parsed.status = static_cast<ApprovalChallengeStatus>(frame[5]);
		parsed.windowsError = ReadUint32(frame, 20);
		std::copy_n(frame.begin() + 24, parsed.challengeId.size(), parsed.challengeId.begin());
		parsed.helperStartMode = static_cast<ApprovalHelperStartMode>(frame[48]);
		parsed.approvalHelperProcessHandle = ReadUint64(frame, 56);
		std::size_t offset = CredentialCaptureChallengeResultHeaderSize;
		if (!ReadString(frame, offset, clientLength, parsed.canonicalClientName) ||
			!ReadString(frame, offset, accountLength, parsed.canonicalAccountName) ||
			ValidateCredentialCaptureChallengeResult(parsed) != ResultValidationError::None)
			return false;
		result = std::move(parsed);
		return true;
	}

	std::vector<std::uint8_t> SerializeCredentialCaptureHelperRequest(
		const CredentialCaptureHelperRequest& request)
	{
		if (ValidateCredentialCaptureHelperRequest(request) != ContractValidationError::None)
			return {};
		std::vector<std::uint8_t> frame;
		frame.reserve(CredentialCaptureHelperRequestSize);
		frame.insert(frame.end(), std::begin(CredentialCaptureHelperRequestMagic),
			std::end(CredentialCaptureHelperRequestMagic));
		frame.push_back(CredentialCaptureHelperRequestMessageType);
		frame.insert(frame.end(), 3, 0);
		WriteUint32(frame, request.protocolVersion);
		WriteUint64(frame, request.requestId);
		frame.insert(frame.end(), request.challengeId.begin(), request.challengeId.end());
		return frame;
	}

	bool DeserializeCredentialCaptureHelperRequest(std::span<const std::uint8_t> frame,
		CredentialCaptureHelperRequest& request)
	{
		if (frame.size() != CredentialCaptureHelperRequestSize ||
			!std::equal(std::begin(CredentialCaptureHelperRequestMagic),
				std::end(CredentialCaptureHelperRequestMagic), frame.begin()) ||
			frame[4] != CredentialCaptureHelperRequestMessageType ||
			frame[5] != 0 || frame[6] != 0 || frame[7] != 0)
			return false;
		CredentialCaptureHelperRequest parsed;
		parsed.protocolVersion = ReadUint32(frame, 8);
		parsed.requestId = ReadUint64(frame, 12);
		std::copy_n(frame.begin() + 20, parsed.challengeId.size(), parsed.challengeId.begin());
		if (ValidateCredentialCaptureHelperRequest(parsed) != ContractValidationError::None)
			return false;
		request = parsed;
		return true;
	}

	bool IsCredentialCaptureHelperRequestFrame(std::span<const std::uint8_t> frame) noexcept
	{
		return frame.size() >= 5 &&
			std::equal(std::begin(CredentialCaptureHelperRequestMagic),
				std::end(CredentialCaptureHelperRequestMagic), frame.begin()) &&
			frame[4] == CredentialCaptureHelperRequestMessageType;
	}

	std::vector<std::uint8_t> SerializeCredentialCaptureHelperResult(
		const CredentialCaptureHelperResult& result)
	{
		if (ValidateCredentialCaptureHelperResult(result) != ResultValidationError::None ||
			HasEmbeddedNull(result.canonicalClientName) ||
			HasEmbeddedNull(result.canonicalAccountName) ||
			HasEmbeddedNull(result.authenticityMarker))
			return {};
		std::vector<std::uint8_t> frame;
		frame.reserve(CredentialCaptureHelperResultHeaderSize + 2 *
			(result.canonicalClientName.size() + result.canonicalAccountName.size() +
			 result.authenticityMarker.size()));
		frame.insert(frame.end(), std::begin(CredentialCaptureHelperResultMagic),
			std::end(CredentialCaptureHelperResultMagic));
		frame.push_back(CredentialCaptureHelperResultMessageType);
		frame.push_back(static_cast<std::uint8_t>(result.status));
		frame.insert(frame.end(), 2, 0);
		WriteUint32(frame, result.protocolVersion);
		WriteUint64(frame, result.requestId);
		WriteUint32(frame, result.windowsError);
		WriteUint32(frame, static_cast<std::uint32_t>(result.canonicalClientName.size()));
		WriteUint32(frame, static_cast<std::uint32_t>(result.canonicalAccountName.size()));
		WriteUint32(frame, static_cast<std::uint32_t>(result.authenticityMarker.size()));
		WriteString(frame, result.canonicalClientName);
		WriteString(frame, result.canonicalAccountName);
		WriteString(frame, result.authenticityMarker);
		return frame;
	}

	bool DeserializeCredentialCaptureHelperResult(std::span<const std::uint8_t> frame,
		CredentialCaptureHelperResult& result)
	{
		if (frame.size() < CredentialCaptureHelperResultHeaderSize ||
			!std::equal(std::begin(CredentialCaptureHelperResultMagic),
				std::end(CredentialCaptureHelperResultMagic), frame.begin()) ||
			frame[4] != CredentialCaptureHelperResultMessageType || frame[6] != 0 || frame[7] != 0)
			return false;
		const std::size_t clientLength = ReadUint32(frame, 24);
		const std::size_t accountLength = ReadUint32(frame, 28);
		const std::size_t markerLength = ReadUint32(frame, 32);
		if (clientLength > MaximumCanonicalAccountNameLength ||
			accountLength > MaximumCanonicalAccountNameLength ||
			markerLength > MaximumAuthenticityMarkerLength ||
			frame.size() != CredentialCaptureHelperResultHeaderSize +
				2 * (clientLength + accountLength + markerLength))
			return false;
		CredentialCaptureHelperResult parsed;
		parsed.protocolVersion = ReadUint32(frame, 8);
		parsed.requestId = ReadUint64(frame, 12);
		parsed.status = static_cast<ApprovalChallengeStatus>(frame[5]);
		parsed.windowsError = ReadUint32(frame, 20);
		std::size_t offset = CredentialCaptureHelperResultHeaderSize;
		if (!ReadString(frame, offset, clientLength, parsed.canonicalClientName) ||
			!ReadString(frame, offset, accountLength, parsed.canonicalAccountName) ||
			!ReadString(frame, offset, markerLength, parsed.authenticityMarker) ||
			ValidateCredentialCaptureHelperResult(parsed) != ResultValidationError::None)
			return false;
		result = std::move(parsed);
		return true;
	}

	std::vector<std::uint8_t> SerializeCredentialCaptureSubmitRequest(
		const CredentialCaptureSubmitRequest& request)
	{
		if (ValidateCredentialCaptureSubmitRequest(request) != ContractValidationError::None ||
			HasEmbeddedNull(request.enteredAccountName))
			return {};
		std::vector<std::uint8_t> frame;
		frame.reserve(CredentialCaptureSubmitRequestHeaderSize + 2 *
			(request.enteredAccountName.size() + request.password.size()));
		frame.insert(frame.end(), std::begin(CredentialCaptureSubmitRequestMagic),
			std::end(CredentialCaptureSubmitRequestMagic));
		frame.push_back(CredentialCaptureSubmitRequestMessageType);
		frame.push_back(request.cancelled ? 1 : 0);
		frame.insert(frame.end(), 2, 0);
		WriteUint32(frame, request.protocolVersion);
		WriteUint64(frame, request.requestId);
		frame.insert(frame.end(), request.challengeId.begin(), request.challengeId.end());
		WriteUint32(frame, static_cast<std::uint32_t>(request.enteredAccountName.size()));
		WriteUint32(frame, static_cast<std::uint32_t>(request.password.size()));
		WriteString(frame, request.enteredAccountName);
		WriteString(frame, request.password.c_str(), request.password.size());
		return frame;
	}

	bool DeserializeCredentialCaptureSubmitRequest(std::span<const std::uint8_t> frame,
		CredentialCaptureSubmitRequest& request)
	{
		if (frame.size() < CredentialCaptureSubmitRequestHeaderSize ||
			!std::equal(std::begin(CredentialCaptureSubmitRequestMagic),
				std::end(CredentialCaptureSubmitRequestMagic), frame.begin()) ||
			frame[4] != CredentialCaptureSubmitRequestMessageType ||
			frame[5] > 1 || frame[6] != 0 || frame[7] != 0)
			return false;
		const std::size_t accountLength = ReadUint32(frame, 36);
		const std::size_t passwordLength = ReadUint32(frame, 40);
		if (accountLength > MaximumCanonicalAccountNameLength ||
			passwordLength > MaximumPasswordLength ||
			frame.size() != CredentialCaptureSubmitRequestHeaderSize +
				2 * (accountLength + passwordLength))
			return false;
		CredentialCaptureSubmitRequest parsed;
		parsed.protocolVersion = ReadUint32(frame, 8);
		parsed.requestId = ReadUint64(frame, 12);
		parsed.cancelled = frame[5] != 0;
		std::copy_n(frame.begin() + 20, parsed.challengeId.size(), parsed.challengeId.begin());
		std::size_t offset = CredentialCaptureSubmitRequestHeaderSize;
		if (!ReadString(frame, offset, accountLength, parsed.enteredAccountName))
			return false;
		const auto passwordBytes = frame.subspan(offset, passwordLength * 2);
		if ((!parsed.cancelled &&
			 !parsed.password.assignUtf16(passwordBytes, passwordLength)) ||
			ValidateCredentialCaptureSubmitRequest(parsed) != ContractValidationError::None)
			return false;
		request = std::move(parsed);
		return true;
	}

	bool IsCredentialCaptureSubmitRequestFrame(std::span<const std::uint8_t> frame) noexcept
	{
		return frame.size() >= 5 &&
			std::equal(std::begin(CredentialCaptureSubmitRequestMagic),
				std::end(CredentialCaptureSubmitRequestMagic), frame.begin()) &&
			frame[4] == CredentialCaptureSubmitRequestMessageType;
	}

	std::vector<std::uint8_t> SerializeCredentialCaptureSubmitResult(
		const CredentialCaptureSubmitResult& result)
	{
		if (ValidateCredentialCaptureSubmitResult(result) != ResultValidationError::None)
			return {};
		std::vector<std::uint8_t> frame;
		frame.reserve(CredentialCaptureSubmitResultSize);
		frame.insert(frame.end(), std::begin(CredentialCaptureSubmitResultMagic),
			std::end(CredentialCaptureSubmitResultMagic));
		frame.push_back(CredentialCaptureSubmitResultMessageType);
		frame.push_back(static_cast<std::uint8_t>(result.status));
		frame.insert(frame.end(), 2, 0);
		WriteUint32(frame, result.protocolVersion);
		WriteUint64(frame, result.requestId);
		WriteUint32(frame, result.windowsError);
		return frame;
	}

	bool DeserializeCredentialCaptureSubmitResult(std::span<const std::uint8_t> frame,
		CredentialCaptureSubmitResult& result)
	{
		if (frame.size() != CredentialCaptureSubmitResultSize ||
			!std::equal(std::begin(CredentialCaptureSubmitResultMagic),
				std::end(CredentialCaptureSubmitResultMagic), frame.begin()) ||
			frame[4] != CredentialCaptureSubmitResultMessageType || frame[6] != 0 || frame[7] != 0)
			return false;
		CredentialCaptureSubmitResult parsed;
		parsed.protocolVersion = ReadUint32(frame, 8);
		parsed.requestId = ReadUint64(frame, 12);
		parsed.status = static_cast<CredentialSaveStatus>(frame[5]);
		parsed.windowsError = ReadUint32(frame, 20);
		if (ValidateCredentialCaptureSubmitResult(parsed) != ResultValidationError::None)
			return false;
		result = parsed;
		return true;
	}

	std::vector<std::uint8_t> SerializeCredentialCaptureCommitRequest(
		const CredentialCaptureCommitRequest& request)
	{
		if (ValidateCredentialCaptureCommitRequest(request) !=
			ContractValidationError::None)
		{
			return {};
		}
		std::vector<std::uint8_t> frame;
		frame.reserve(CredentialCaptureCommitRequestSize);
		frame.insert(frame.end(), std::begin(CredentialCaptureCommitRequestMagic),
			std::end(CredentialCaptureCommitRequestMagic));
		frame.push_back(CredentialCaptureCommitRequestMessageType);
		frame.insert(frame.end(), 3, 0);
		WriteUint32(frame, request.protocolVersion);
		WriteUint64(frame, request.requestId);
		frame.insert(frame.end(), request.challengeId.begin(), request.challengeId.end());
		return frame;
	}

	bool DeserializeCredentialCaptureCommitRequest(std::span<const std::uint8_t> frame,
		CredentialCaptureCommitRequest& request)
	{
		if (frame.size() != CredentialCaptureCommitRequestSize ||
			!std::equal(std::begin(CredentialCaptureCommitRequestMagic),
				std::end(CredentialCaptureCommitRequestMagic), frame.begin()) ||
			frame[4] != CredentialCaptureCommitRequestMessageType ||
			frame[5] != 0 || frame[6] != 0 || frame[7] != 0)
		{
			return false;
		}
		CredentialCaptureCommitRequest parsed;
		parsed.protocolVersion = ReadUint32(frame, 8);
		parsed.requestId = ReadUint64(frame, 12);
		std::copy_n(frame.begin() + 20, parsed.challengeId.size(), parsed.challengeId.begin());
		if (ValidateCredentialCaptureCommitRequest(parsed) !=
			ContractValidationError::None)
		{
			return false;
		}
		request = parsed;
		return true;
	}

	std::vector<std::uint8_t> SerializeCredentialCaptureCommitResult(
		const CredentialCaptureCommitResult& result)
	{
		if (ValidateCredentialCaptureCommitResult(result) != ResultValidationError::None)
			return {};
		std::vector<std::uint8_t> frame;
		frame.reserve(CredentialCaptureCommitResultSize);
		frame.insert(frame.end(), std::begin(CredentialCaptureCommitResultMagic),
			std::end(CredentialCaptureCommitResultMagic));
		frame.push_back(CredentialCaptureCommitResultMessageType);
		frame.push_back(static_cast<std::uint8_t>(result.status));
		frame.insert(frame.end(), 2, 0);
		WriteUint32(frame, result.protocolVersion);
		WriteUint64(frame, result.requestId);
		WriteUint32(frame, result.windowsError);
		return frame;
	}

	bool DeserializeCredentialCaptureCommitResult(std::span<const std::uint8_t> frame,
		CredentialCaptureCommitResult& result)
	{
		if (frame.size() != CredentialCaptureCommitResultSize ||
			!std::equal(std::begin(CredentialCaptureCommitResultMagic),
				std::end(CredentialCaptureCommitResultMagic), frame.begin()) ||
			frame[4] != CredentialCaptureCommitResultMessageType ||
			frame[6] != 0 || frame[7] != 0)
		{
			return false;
		}
		CredentialCaptureCommitResult parsed;
		parsed.protocolVersion = ReadUint32(frame, 8);
		parsed.requestId = ReadUint64(frame, 12);
		parsed.status = static_cast<CredentialSaveStatus>(frame[5]);
		parsed.windowsError = ReadUint32(frame, 20);
		if (ValidateCredentialCaptureCommitResult(parsed) != ResultValidationError::None)
			return false;
		result = parsed;
		return true;
	}

// Serializes a valid delete request into a precisely sized protocol frame.
	std::vector<std::uint8_t> SerializeCredentialDeleteRequest(
		const CredentialDeleteRequest& request)
	{
		if (ValidateCredentialDeleteRequest(request) != ContractValidationError::None ||
			HasEmbeddedNull(request.requestedAccountSid))
		{
			return {};
		}
		std::vector<std::uint8_t> frame;
		frame.reserve(CredentialDeleteRequestHeaderSize +
			2 * request.requestedAccountSid.size());
		frame.insert(frame.end(), std::begin(CredentialDeleteRequestMagic),
			std::end(CredentialDeleteRequestMagic));
		frame.push_back(CredentialDeleteRequestMessageType);
		frame.insert(frame.end(), 3, 0);
		WriteUint32(frame, request.protocolVersion);
		WriteUint64(frame, request.requestId);
		WriteUint32(frame, static_cast<std::uint32_t>(request.requestedAccountSid.size()));
		WriteString(frame, request.requestedAccountSid);
		return frame;
	}

// Accepts a delete request only from a fully valid, completely consumed frame.
	bool DeserializeCredentialDeleteRequest(
		std::span<const std::uint8_t> frame,
		CredentialDeleteRequest& request)
	{
		if (frame.size() < CredentialDeleteRequestHeaderSize ||
			!std::equal(std::begin(CredentialDeleteRequestMagic),
				std::end(CredentialDeleteRequestMagic), frame.begin()) ||
			frame[4] != CredentialDeleteRequestMessageType ||
			frame[5] != 0 || frame[6] != 0 || frame[7] != 0)
		{
			return false;
		}
		const std::size_t sidLength = ReadUint32(frame, 20);
		if (sidLength > MaximumAccountSidLength ||
			frame.size() != CredentialDeleteRequestHeaderSize + 2 * sidLength)
		{
			return false;
		}
		CredentialDeleteRequest parsed;
		parsed.protocolVersion = ReadUint32(frame, 8);
		parsed.requestId = ReadUint64(frame, 12);
		std::size_t offset = CredentialDeleteRequestHeaderSize;
		if (!ReadString(frame, offset, sidLength, parsed.requestedAccountSid) ||
			ValidateCredentialDeleteRequest(parsed) != ContractValidationError::None)
		{
			return false;
		}
		request = std::move(parsed);
		return true;
	}

// Serializes a valid delete result into the fixed protocol format.
	std::vector<std::uint8_t> SerializeCredentialDeleteResult(
		const CredentialDeleteResult& result)
	{
		if (ValidateCredentialDeleteResult(result) != ResultValidationError::None)
			return {};
		std::vector<std::uint8_t> frame;
		frame.reserve(CredentialDeleteResultSize);
		frame.insert(frame.end(), std::begin(CredentialDeleteResultMagic),
			std::end(CredentialDeleteResultMagic));
		frame.push_back(CredentialDeleteResultMessageType);
		frame.push_back(static_cast<std::uint8_t>(result.status));
		frame.push_back(0);
		frame.push_back(0);
		WriteUint32(frame, result.protocolVersion);
		WriteUint64(frame, result.requestId);
		WriteUint32(frame, result.windowsError);
		return frame;
	}

// Accepts a delete result only from a valid frame of the exact size.
	bool DeserializeCredentialDeleteResult(
		std::span<const std::uint8_t> frame,
		CredentialDeleteResult& result)
	{
		if (frame.size() != CredentialDeleteResultSize ||
			!std::equal(std::begin(CredentialDeleteResultMagic),
				std::end(CredentialDeleteResultMagic), frame.begin()) ||
			frame[4] != CredentialDeleteResultMessageType ||
			frame[6] != 0 || frame[7] != 0)
		{
			return false;
		}
		CredentialDeleteResult parsed;
		parsed.protocolVersion = ReadUint32(frame, 8);
		parsed.requestId = ReadUint64(frame, 12);
		parsed.status = static_cast<CredentialDeleteStatus>(frame[5]);
		parsed.windowsError = ReadUint32(frame, 20);
		if (ValidateCredentialDeleteResult(parsed) != ResultValidationError::None)
			return false;
		result = parsed;
		return true;
	}

// Serializes a valid usage test request into a precisely sized frame.
	std::vector<std::uint8_t> SerializeCredentialUseTestRequest(
		const CredentialUseTestRequest& request)
	{
		if (ValidateCredentialUseTestRequest(request) != ContractValidationError::None ||
			HasEmbeddedNull(request.requestedAccountSid))
		{
			return {};
		}
		std::vector<std::uint8_t> frame;
		frame.reserve(CredentialUseTestRequestHeaderSize +
			2 * request.requestedAccountSid.size());
		frame.insert(frame.end(), std::begin(CredentialUseTestRequestMagic),
			std::end(CredentialUseTestRequestMagic));
		frame.push_back(CredentialUseTestRequestMessageType);
		frame.insert(frame.end(), 3, 0);
		WriteUint32(frame, request.protocolVersion);
		WriteUint64(frame, request.requestId);
		WriteUint32(frame, static_cast<std::uint32_t>(request.requestedAccountSid.size()));
		WriteString(frame, request.requestedAccountSid);
		return frame;
	}

// Accepts a usage test request only from a completely validated frame.
	bool DeserializeCredentialUseTestRequest(
		std::span<const std::uint8_t> frame,
		CredentialUseTestRequest& request)
	{
		if (frame.size() < CredentialUseTestRequestHeaderSize ||
			!std::equal(std::begin(CredentialUseTestRequestMagic),
				std::end(CredentialUseTestRequestMagic), frame.begin()) ||
			frame[4] != CredentialUseTestRequestMessageType ||
			frame[5] != 0 || frame[6] != 0 || frame[7] != 0)
		{
			return false;
		}
		const std::size_t sidLength = ReadUint32(frame, 20);
		if (sidLength > MaximumAccountSidLength ||
			frame.size() != CredentialUseTestRequestHeaderSize + 2 * sidLength)
		{
			return false;
		}
		CredentialUseTestRequest parsed;
		parsed.protocolVersion = ReadUint32(frame, 8);
		parsed.requestId = ReadUint64(frame, 12);
		std::size_t offset = CredentialUseTestRequestHeaderSize;
		if (!ReadString(frame, offset, sidLength, parsed.requestedAccountSid) ||
			ValidateCredentialUseTestRequest(parsed) != ContractValidationError::None)
		{
			return false;
		}
		request = std::move(parsed);
		return true;
	}

// Serializes a valid usage test result into the fixed protocol format.
	std::vector<std::uint8_t> SerializeCredentialUseTestResult(
		const CredentialUseTestResult& result)
	{
		if (ValidateCredentialUseTestResult(result) != ResultValidationError::None)
			return {};
		std::vector<std::uint8_t> frame;
		frame.reserve(CredentialUseTestResultSize);
		frame.insert(frame.end(), std::begin(CredentialUseTestResultMagic),
			std::end(CredentialUseTestResultMagic));
		frame.push_back(CredentialUseTestResultMessageType);
		frame.push_back(static_cast<std::uint8_t>(result.status));
		frame.push_back(0);
		frame.push_back(0);
		WriteUint32(frame, result.protocolVersion);
		WriteUint64(frame, result.requestId);
		WriteUint32(frame, result.windowsError);
		WriteUint32(frame, result.processId);
		return frame;
	}

// Accepts a usage test result only from a valid frame of the exact size.
	bool DeserializeCredentialUseTestResult(
		std::span<const std::uint8_t> frame,
		CredentialUseTestResult& result)
	{
		if (frame.size() != CredentialUseTestResultSize ||
			!std::equal(std::begin(CredentialUseTestResultMagic),
				std::end(CredentialUseTestResultMagic), frame.begin()) ||
			frame[4] != CredentialUseTestResultMessageType ||
			frame[6] != 0 || frame[7] != 0)
		{
			return false;
		}
		CredentialUseTestResult parsed;
		parsed.protocolVersion = ReadUint32(frame, 8);
		parsed.requestId = ReadUint64(frame, 12);
		parsed.status = static_cast<CredentialUseTestStatus>(frame[5]);
		parsed.windowsError = ReadUint32(frame, 20);
		parsed.processId = ReadUint32(frame, 24);
		if (ValidateCredentialUseTestResult(parsed) != ResultValidationError::None)
			return false;
		result = parsed;
		return true;
	}

// Serializes a valid credential list request into the fixed format.
	std::vector<std::uint8_t> SerializeCredentialListRequest(
		const CredentialListRequest& request)
	{
		if (ValidateCredentialListRequest(request) != ContractValidationError::None)
			return {};
		std::vector<std::uint8_t> frame;
		frame.reserve(CredentialListRequestSize);
		frame.insert(frame.end(), std::begin(CredentialListRequestMagic),
			std::end(CredentialListRequestMagic));
		frame.push_back(CredentialListRequestMessageType);
		frame.insert(frame.end(), 3, 0);
		WriteUint32(frame, request.protocolVersion);
		WriteUint64(frame, request.requestId);
		return frame;
	}

// Accepts a list request only from a valid frame of the exact size.
	bool DeserializeCredentialListRequest(
		std::span<const std::uint8_t> frame, CredentialListRequest& request)
	{
		if (frame.size() != CredentialListRequestSize ||
			!std::equal(std::begin(CredentialListRequestMagic),
				std::end(CredentialListRequestMagic), frame.begin()) ||
			frame[4] != CredentialListRequestMessageType ||
			frame[5] != 0 || frame[6] != 0 || frame[7] != 0)
		{
			return false;
		}
		CredentialListRequest parsed;
		parsed.protocolVersion = ReadUint32(frame, 8);
		parsed.requestId = ReadUint64(frame, 12);
		if (ValidateCredentialListRequest(parsed) != ContractValidationError::None)
			return false;
		request = parsed;
		return true;
	}

// Serializes a valid list result with canonically length-bound entries.
	std::vector<std::uint8_t> SerializeCredentialListResult(
		const CredentialListResult& result)
	{
		if (ValidateCredentialListResult(result) != ResultValidationError::None)
			return {};
		std::size_t characterCount = 0;
		for (const CredentialListEntry& entry : result.entries)
		{
			if (HasEmbeddedNull(entry.accountSid) ||
				HasEmbeddedNull(entry.canonicalAccountName))
			{
				return {};
			}
			characterCount += entry.accountSid.size() + entry.canonicalAccountName.size();
		}
		std::vector<std::uint8_t> frame;
		frame.reserve(CredentialListResultHeaderSize +
			result.entries.size() * CredentialListEntryHeaderSize + 2 * characterCount);
		frame.insert(frame.end(), std::begin(CredentialListResultMagic),
			std::end(CredentialListResultMagic));
		frame.push_back(CredentialListResultMessageType);
		frame.push_back(static_cast<std::uint8_t>(result.status));
		frame.push_back(0);
		frame.push_back(0);
		WriteUint32(frame, result.protocolVersion);
		WriteUint64(frame, result.requestId);
		WriteUint32(frame, result.windowsError);
		WriteUint32(frame, static_cast<std::uint32_t>(result.entries.size()));
		for (const CredentialListEntry& entry : result.entries)
		{
			WriteUint32(frame, static_cast<std::uint32_t>(entry.accountSid.size()));
			WriteUint32(frame, static_cast<std::uint32_t>(entry.canonicalAccountName.size()));
			WriteUint64(frame, entry.lastWritten);
			WriteString(frame, entry.accountSid);
			WriteString(frame, entry.canonicalAccountName);
		}
		return frame;
	}

// Accepts a list result only after complete consumption of all valid entries.
	bool DeserializeCredentialListResult(
		std::span<const std::uint8_t> frame, CredentialListResult& result)
	{
		if (frame.size() < CredentialListResultHeaderSize ||
			frame.size() > MaximumCredentialListResultSize ||
			!std::equal(std::begin(CredentialListResultMagic),
				std::end(CredentialListResultMagic), frame.begin()) ||
			frame[4] != CredentialListResultMessageType || frame[6] != 0 || frame[7] != 0)
		{
			return false;
		}
		const std::size_t entryCount = ReadUint32(frame, 24);
		if (entryCount > MaximumCredentialListEntries)
			return false;
		CredentialListResult parsed;
		parsed.protocolVersion = ReadUint32(frame, 8);
		parsed.requestId = ReadUint64(frame, 12);
		parsed.status = static_cast<CredentialListStatus>(frame[5]);
		parsed.windowsError = ReadUint32(frame, 20);
		std::size_t offset = CredentialListResultHeaderSize;
		parsed.entries.reserve(entryCount);
		for (std::size_t index = 0; index < entryCount; index++)
		{
			if (offset > frame.size() || frame.size() - offset < CredentialListEntryHeaderSize)
				return false;
			const std::size_t sidLength = ReadUint32(frame, offset);
			const std::size_t nameLength = ReadUint32(frame, offset + 4);
			const std::uint64_t lastWritten = ReadUint64(frame, offset + 8);
			offset += CredentialListEntryHeaderSize;
			if (sidLength == 0 || sidLength > MaximumAccountSidLength ||
				nameLength == 0 || nameLength > MaximumCanonicalAccountNameLength ||
				sidLength + nameLength > (frame.size() - offset) / 2)
			{
				return false;
			}
			CredentialListEntry entry;
			entry.lastWritten = lastWritten;
			if (!ReadString(frame, offset, sidLength, entry.accountSid) ||
				!ReadString(frame, offset, nameLength, entry.canonicalAccountName))
			{
				return false;
			}
			parsed.entries.push_back(std::move(entry));
		}
		if (offset != frame.size() ||
			ValidateCredentialListResult(parsed) != ResultValidationError::None)
		{
			return false;
		}
		result = std::move(parsed);
		return true;
	}
