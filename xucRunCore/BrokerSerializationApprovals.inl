// Purpose: Internal protocol implementation fragment for BrokerSerialization.cpp.
//
// Inclusion contract:
// - This file is included exactly once inside the xucrun::broker namespace.
// - It uses the translation unit's private magic values and byte-level read/write helpers.
// - It is not a standalone translation unit and introduces no additional exported symbols.
// - Field order, length checks, complete-frame validation, and sensitive-frame handling must remain exact.

// Serializes a valid approval challenge request into a precisely sized frame.
	std::vector<std::uint8_t> SerializeApprovalChallengeRequest(
		const ApprovalChallengeRequest& request)
	{
		if (ValidateApprovalChallengeRequest(request) != ContractValidationError::None ||
			(request.elevationMode != ElevationMode::PreserveToken &&
			 request.elevationMode != ElevationMode::RequireElevated) ||
			HasEmbeddedNull(request.requestedAccountSid) ||
			HasEmbeddedNull(request.approvalAccountSid) ||
			HasEmbeddedNull(request.executablePath) ||
			HasEmbeddedNull(request.argumentTail) ||
			HasEmbeddedNull(request.workingDirectory))
		{
			return {};
		}
		const std::size_t characterCount = request.requestedAccountSid.size() +
			request.approvalAccountSid.size() +
			request.executablePath.size() + request.argumentTail.size() +
			request.workingDirectory.size();
		std::vector<std::uint8_t> frame;
		frame.reserve(ApprovalChallengeRequestHeaderSize + 2 * characterCount);
		frame.insert(frame.end(), std::begin(ApprovalChallengeRequestMagic),
			std::end(ApprovalChallengeRequestMagic));
		frame.push_back(ApprovalChallengeRequestMessageType);
		frame.push_back(static_cast<std::uint8_t>(request.elevationMode));
		frame.push_back(static_cast<std::uint8_t>(request.visibility));
		frame.push_back(0);
		WriteUint32(frame, request.protocolVersion);
		WriteUint64(frame, request.requestId);
		WriteUint32(frame, static_cast<std::uint32_t>(request.requestedAccountSid.size()));
		WriteUint32(frame, static_cast<std::uint32_t>(request.executablePath.size()));
		WriteUint32(frame, static_cast<std::uint32_t>(request.argumentTail.size()));
		WriteUint32(frame, static_cast<std::uint32_t>(request.workingDirectory.size()));
		WriteUint32(frame, static_cast<std::uint32_t>(request.approvalAccountSid.size()));
		WriteString(frame, request.requestedAccountSid);
		WriteString(frame, request.approvalAccountSid);
		WriteString(frame, request.executablePath);
		WriteString(frame, request.argumentTail);
		WriteString(frame, request.workingDirectory);
		return frame;
	}

// Accepts a challenge request only from a fully valid, completely consumed frame.
	bool DeserializeApprovalChallengeRequest(
		std::span<const std::uint8_t> frame,
		ApprovalChallengeRequest& request)
	{
		if (frame.size() < ApprovalChallengeRequestHeaderSize ||
			!std::equal(std::begin(ApprovalChallengeRequestMagic),
				std::end(ApprovalChallengeRequestMagic), frame.begin()) ||
			frame[4] != ApprovalChallengeRequestMessageType || frame[7] != 0 ||
			(frame[5] != static_cast<std::uint8_t>(ElevationMode::PreserveToken) &&
			 frame[5] != static_cast<std::uint8_t>(ElevationMode::RequireElevated)) ||
			(frame[6] != static_cast<std::uint8_t>(LaunchVisibility::Normal) &&
			 frame[6] != static_cast<std::uint8_t>(LaunchVisibility::Hidden) &&
			 frame[6] != static_cast<std::uint8_t>(LaunchVisibility::HiddenMonitor)))
		{
			return false;
		}
		const std::size_t sidLength = ReadUint32(frame, 20);
		const std::size_t executableLength = ReadUint32(frame, 24);
		const std::size_t argumentLength = ReadUint32(frame, 28);
		const std::size_t workingDirectoryLength = ReadUint32(frame, 32);
		const std::size_t approvalSidLength = ReadUint32(frame, 36);
		if (sidLength > MaximumAccountSidLength || executableLength > MaximumPathLength ||
			argumentLength > MaximumArgumentLength ||
			workingDirectoryLength > MaximumPathLength ||
			approvalSidLength > MaximumAccountSidLength)
		{
			return false;
		}
		const std::size_t characterCount = sidLength + approvalSidLength + executableLength +
			argumentLength + workingDirectoryLength;
		if (frame.size() != ApprovalChallengeRequestHeaderSize + 2 * characterCount)
			return false;

		ApprovalChallengeRequest parsed;
		parsed.protocolVersion = ReadUint32(frame, 8);
		parsed.requestId = ReadUint64(frame, 12);
		parsed.elevationMode = static_cast<ElevationMode>(frame[5]);
		parsed.visibility = static_cast<LaunchVisibility>(frame[6]);
		std::size_t offset = ApprovalChallengeRequestHeaderSize;
		if (!ReadString(frame, offset, sidLength, parsed.requestedAccountSid) ||
			!ReadString(frame, offset, approvalSidLength, parsed.approvalAccountSid) ||
			!ReadString(frame, offset, executableLength, parsed.executablePath) ||
			!ReadString(frame, offset, argumentLength, parsed.argumentTail) ||
			!ReadString(frame, offset, workingDirectoryLength, parsed.workingDirectory) ||
			ValidateApprovalChallengeRequest(parsed) != ContractValidationError::None)
		{
			return false;
		}
		request = std::move(parsed);
		return true;
	}

// Serializes a valid challenge result and its SHA-256 identities canonically.
	std::vector<std::uint8_t> SerializeApprovalChallengeResult(
		const ApprovalChallengeResult& result)
	{
		if (ValidateApprovalChallengeResult(result) != ResultValidationError::None ||
			HasEmbeddedNull(result.canonicalClientName) ||
			HasEmbeddedNull(result.canonicalAccountName) ||
			HasEmbeddedNull(result.clientPath) ||
			HasEmbeddedNull(result.finalExecutablePath) ||
			HasEmbeddedNull(result.argumentTail) || HasEmbeddedNull(result.workingDirectory) ||
			HasEmbeddedNull(result.directScriptPath) ||
			HasEmbeddedNull(result.changedProgramPath) ||
			HasEmbeddedNull(result.authenticityMarker))
		{
			return {};
		}
		const std::size_t characterCount = result.canonicalClientName.size() +
			result.canonicalAccountName.size() + result.clientPath.size() +
			result.finalExecutablePath.size() + result.argumentTail.size() +
			result.workingDirectory.size() + result.directScriptPath.size() +
			result.changedProgramPath.size() +
			result.authenticityMarker.size();
		std::vector<std::uint8_t> frame;
		frame.reserve(ApprovalChallengeResultHeaderSize + 2 * characterCount);
		frame.insert(frame.end(), std::begin(ApprovalChallengeResultMagic),
			std::end(ApprovalChallengeResultMagic));
		frame.push_back(ApprovalChallengeResultMessageType);
		frame.push_back(static_cast<std::uint8_t>(result.status));
		frame.push_back(static_cast<std::uint8_t>(result.elevationMode));
		frame.push_back((result.userConfirmationRequired ? 1 : 0) |
			(result.programUpdateAvailable ? 2 : 0) |
			(result.visibility == LaunchVisibility::Hidden ? 4 : 0) |
			(result.visibility == LaunchVisibility::HiddenMonitor ? 8 : 0) |
			(static_cast<std::uint8_t>(result.failureReason) << 4));
		WriteUint32(frame, result.protocolVersion);
		WriteUint64(frame, result.requestId);
		WriteUint32(frame, result.windowsError);
		frame.insert(frame.end(), result.challengeId.begin(), result.challengeId.end());
		frame.insert(frame.end(), result.executableSha256.begin(), result.executableSha256.end());
		frame.insert(frame.end(), result.oldProgramSha256.begin(), result.oldProgramSha256.end());
		frame.insert(frame.end(), result.newProgramSha256.begin(), result.newProgramSha256.end());
		frame.insert(frame.end(), result.clientSha256.begin(), result.clientSha256.end());
		WriteUint32(frame, result.affectedApprovalCount);
		WriteUint32(frame, result.affectedAccountCount);
		WriteUint32(frame, static_cast<std::uint32_t>(result.clientTrustStatus));
		WriteUint32(frame, static_cast<std::uint32_t>(result.executableTrustStatus));
		WriteUint64(frame, result.approvalHelperProcessHandle);
		WriteUint32(frame, static_cast<std::uint32_t>(result.canonicalClientName.size()));
		WriteUint32(frame, static_cast<std::uint32_t>(result.canonicalAccountName.size()));
		WriteUint32(frame, static_cast<std::uint32_t>(result.clientPath.size()));
		WriteUint32(frame, static_cast<std::uint32_t>(result.finalExecutablePath.size()));
		WriteUint32(frame, static_cast<std::uint32_t>(result.argumentTail.size()));
		WriteUint32(frame, static_cast<std::uint32_t>(result.workingDirectory.size()));
		WriteUint32(frame, static_cast<std::uint32_t>(result.changedProgramPath.size()));
		frame.push_back(static_cast<std::uint8_t>(result.helperStartMode));
		frame.push_back(result.approvalHelperView ? 1 : 0);
		frame.push_back(result.storedApprovalPresenceConfirmation ? 1 : 0);
		frame.push_back(static_cast<std::uint8_t>(
			result.writableElevatedTargetReason));
		WriteUint32(frame, static_cast<std::uint32_t>(result.authenticityMarker.size()));
		frame.insert(frame.end(), result.directScriptSha256.begin(),
			result.directScriptSha256.end());
		WriteUint32(frame, static_cast<std::uint32_t>(result.directScriptPath.size()));
		WriteString(frame, result.canonicalClientName);
		WriteString(frame, result.canonicalAccountName);
		WriteString(frame, result.clientPath);
		WriteString(frame, result.finalExecutablePath);
		WriteString(frame, result.argumentTail);
		WriteString(frame, result.workingDirectory);
		WriteString(frame, result.directScriptPath);
		WriteString(frame, result.changedProgramPath);
		WriteString(frame, result.authenticityMarker);
		return frame;
	}

// Accepts a challenge result only after complete frame and identity validation.
	bool DeserializeApprovalChallengeResult(
		std::span<const std::uint8_t> frame,
		ApprovalChallengeResult& result)
	{
		if (frame.size() < ApprovalChallengeResultHeaderSize ||
			!std::equal(std::begin(ApprovalChallengeResultMagic),
				std::end(ApprovalChallengeResultMagic), frame.begin()) ||
			frame[4] != ApprovalChallengeResultMessageType ||
			(frame[6] != static_cast<std::uint8_t>(ElevationMode::PreserveToken) &&
			 frame[6] != static_cast<std::uint8_t>(ElevationMode::RequireElevated)) ||
			frame[220] > static_cast<std::uint8_t>(
				ApprovalHelperStartMode::ClientRunAsRequired) ||
			frame[221] > 1 || frame[222] > 1 ||
			(frame[223] != static_cast<std::uint8_t>(ApprovalFailureReason::None) &&
			 frame[223] != static_cast<std::uint8_t>(
				 ApprovalFailureReason::ElevatedTargetWritable)))
		{
			return false;
		}
		const std::size_t clientNameLength = ReadUint32(frame, 192);
		const std::size_t accountLength = ReadUint32(frame, 196);
		const std::size_t clientPathLength = ReadUint32(frame, 200);
		const std::size_t executableLength = ReadUint32(frame, 204);
		const std::size_t argumentLength = ReadUint32(frame, 208);
		const std::size_t workingDirectoryLength = ReadUint32(frame, 212);
		const std::size_t changedProgramPathLength = ReadUint32(frame, 216);
		const std::size_t authenticityMarkerLength = ReadUint32(frame, 224);
		const std::size_t directScriptPathLength = ReadUint32(frame, 260);
		if (clientNameLength > MaximumCanonicalAccountNameLength ||
			accountLength > MaximumCanonicalAccountNameLength ||
			clientPathLength > MaximumPathLength ||
			executableLength > MaximumPathLength || argumentLength > MaximumArgumentLength ||
			workingDirectoryLength > MaximumPathLength ||
			directScriptPathLength > MaximumPathLength ||
			changedProgramPathLength > MaximumPathLength ||
			authenticityMarkerLength > MaximumAuthenticityMarkerLength)
		{
			return false;
		}
		const std::size_t characterCount = clientNameLength + accountLength +
			clientPathLength + executableLength + argumentLength +
			workingDirectoryLength + directScriptPathLength +
			changedProgramPathLength + authenticityMarkerLength;
		if (frame.size() != ApprovalChallengeResultHeaderSize + 2 * characterCount)
			return false;

		ApprovalChallengeResult parsed;
		parsed.protocolVersion = ReadUint32(frame, 8);
		parsed.requestId = ReadUint64(frame, 12);
		parsed.status = static_cast<ApprovalChallengeStatus>(frame[5]);
		parsed.elevationMode = static_cast<ElevationMode>(frame[6]);
		parsed.failureReason = static_cast<ApprovalFailureReason>(frame[7] >> 4);
		parsed.userConfirmationRequired = (frame[7] & 1) != 0;
		parsed.programUpdateAvailable = (frame[7] & 2) != 0;
		if ((frame[7] & 12) == 12)
			return false;
		parsed.visibility = (frame[7] & 8) != 0
			? LaunchVisibility::HiddenMonitor
			: (frame[7] & 4) != 0
				? LaunchVisibility::Hidden
				: LaunchVisibility::Normal;
		parsed.windowsError = ReadUint32(frame, 20);
		std::copy_n(frame.begin() + 24, parsed.challengeId.size(), parsed.challengeId.begin());
		std::copy_n(frame.begin() + 40, parsed.executableSha256.size(),
			parsed.executableSha256.begin());
		std::copy_n(frame.begin() + 72, parsed.oldProgramSha256.size(),
			parsed.oldProgramSha256.begin());
		std::copy_n(frame.begin() + 104, parsed.newProgramSha256.size(),
			parsed.newProgramSha256.begin());
		std::copy_n(frame.begin() + 136, parsed.clientSha256.size(),
			parsed.clientSha256.begin());
		parsed.affectedApprovalCount = ReadUint32(frame, 168);
		parsed.affectedAccountCount = ReadUint32(frame, 172);
		parsed.clientTrustStatus = static_cast<std::int32_t>(ReadUint32(frame, 176));
		parsed.clientSignatureTrusted = parsed.clientTrustStatus == 0;
		parsed.executableTrustStatus = static_cast<std::int32_t>(ReadUint32(frame, 180));
		parsed.executableSignatureTrusted = parsed.executableTrustStatus == 0;
		parsed.approvalHelperProcessHandle = ReadUint64(frame, 184);
		parsed.helperStartMode = static_cast<ApprovalHelperStartMode>(frame[220]);
		parsed.approvalHelperView = frame[221] != 0;
		parsed.storedApprovalPresenceConfirmation = frame[222] != 0;
		parsed.writableElevatedTargetReason =
			static_cast<ApprovalFailureReason>(frame[223]);
		std::copy_n(frame.begin() + 228, parsed.directScriptSha256.size(),
			parsed.directScriptSha256.begin());
		std::size_t offset = ApprovalChallengeResultHeaderSize;
		if (!ReadString(frame, offset, clientNameLength, parsed.canonicalClientName) ||
			!ReadString(frame, offset, accountLength, parsed.canonicalAccountName) ||
			!ReadString(frame, offset, clientPathLength, parsed.clientPath) ||
			!ReadString(frame, offset, executableLength, parsed.finalExecutablePath) ||
			!ReadString(frame, offset, argumentLength, parsed.argumentTail) ||
			!ReadString(frame, offset, workingDirectoryLength, parsed.workingDirectory) ||
			!ReadString(frame, offset, directScriptPathLength, parsed.directScriptPath) ||
			!ReadString(frame, offset, changedProgramPathLength, parsed.changedProgramPath) ||
			!ReadString(frame, offset, authenticityMarkerLength,
				parsed.authenticityMarker) ||
			ValidateApprovalChallengeResult(parsed) != ResultValidationError::None)
		{
			return false;
		}
		result = std::move(parsed);
		return true;
	}

// Serializes a valid challenge confirmation into the fixed protocol format.
	std::vector<std::uint8_t> SerializeApprovalConfirmationRequest(
		const ApprovalConfirmationRequest& request)
	{
		if (ValidateApprovalConfirmationRequest(request) != ContractValidationError::None)
			return {};
		std::vector<std::uint8_t> frame;
		frame.reserve(ApprovalConfirmationRequestSize);
		frame.insert(frame.end(), std::begin(ApprovalConfirmationRequestMagic),
			std::end(ApprovalConfirmationRequestMagic));
		frame.push_back(ApprovalConfirmationRequestMessageType);
		frame.push_back(static_cast<std::uint8_t>(request.decision));
		frame.push_back(static_cast<std::uint8_t>(request.persistence));
		frame.push_back(static_cast<std::uint8_t>(request.programScope));
		frame.push_back(static_cast<std::uint8_t>(request.accountScope));
		frame.push_back(static_cast<std::uint8_t>(request.updateScope));
		frame.push_back(0);
		frame.push_back(0);
		WriteUint32(frame, request.protocolVersion);
		WriteUint64(frame, request.requestId);
		frame.insert(frame.end(), request.challengeId.begin(), request.challengeId.end());
		return frame;
	}

// Accepts a challenge confirmation only from a valid frame of the exact size.
	bool DeserializeApprovalConfirmationRequest(
		std::span<const std::uint8_t> frame,
		ApprovalConfirmationRequest& request)
	{
		if (frame.size() != ApprovalConfirmationRequestSize ||
			!std::equal(std::begin(ApprovalConfirmationRequestMagic),
				std::end(ApprovalConfirmationRequestMagic), frame.begin()) ||
			frame[4] != ApprovalConfirmationRequestMessageType ||
			frame[10] != 0 || frame[11] != 0)
		{
			return false;
		}
		ApprovalConfirmationRequest parsed;
		parsed.protocolVersion = ReadUint32(frame, 12);
		parsed.requestId = ReadUint64(frame, 16);
		parsed.decision = static_cast<ApprovalDecision>(frame[5]);
		parsed.persistence = static_cast<ApprovalPersistence>(frame[6]);
		parsed.programScope = static_cast<ApprovalProgramScope>(frame[7]);
		parsed.accountScope = static_cast<ApprovalAccountScope>(frame[8]);
		parsed.updateScope = static_cast<ApprovalUpdateScope>(frame[9]);
		std::copy_n(frame.begin() + 24, parsed.challengeId.size(), parsed.challengeId.begin());
		if (ValidateApprovalConfirmationRequest(parsed) != ContractValidationError::None)
			return false;
		request = parsed;
		return true;
	}

// Serializes a valid confirmation result into the fixed protocol format.
	std::vector<std::uint8_t> SerializeApprovalConfirmationResult(
		const ApprovalConfirmationResult& result)
	{
		if (ValidateApprovalConfirmationResult(result) != ResultValidationError::None)
			return {};
		std::vector<std::uint8_t> frame;
		frame.reserve(ApprovalConfirmationResultSize);
		frame.insert(frame.end(), std::begin(ApprovalConfirmationResultMagic),
			std::end(ApprovalConfirmationResultMagic));
		frame.push_back(ApprovalConfirmationResultMessageType);
		frame.push_back(static_cast<std::uint8_t>(result.status));
		frame.push_back(static_cast<std::uint8_t>(result.failureReason));
		frame.push_back(0);
		WriteUint32(frame, result.protocolVersion);
		WriteUint64(frame, result.requestId);
		WriteUint32(frame, result.windowsError);
		WriteUint32(frame, result.processId);
		return frame;
	}

// Accepts a confirmation result only from a valid frame of the exact size.
	bool DeserializeApprovalConfirmationResult(
		std::span<const std::uint8_t> frame,
		ApprovalConfirmationResult& result)
	{
		if (frame.size() != ApprovalConfirmationResultSize ||
			!std::equal(std::begin(ApprovalConfirmationResultMagic),
				std::end(ApprovalConfirmationResultMagic), frame.begin()) ||
			frame[4] != ApprovalConfirmationResultMessageType || frame[7] != 0)
		{
			return false;
		}
		ApprovalConfirmationResult parsed;
		parsed.protocolVersion = ReadUint32(frame, 8);
		parsed.requestId = ReadUint64(frame, 12);
		parsed.status = static_cast<ApprovalConfirmationStatus>(frame[5]);
		parsed.failureReason = static_cast<ApprovalFailureReason>(frame[6]);
		parsed.windowsError = ReadUint32(frame, 20);
		parsed.processId = ReadUint32(frame, 24);
		if (ValidateApprovalConfirmationResult(parsed) != ResultValidationError::None)
			return false;
		result = parsed;
		return true;
	}

	std::vector<std::uint8_t> SerializeApprovalHelperRequest(
		const ApprovalHelperRequest& request)
	{
		if (ValidateApprovalHelperRequest(request) != ContractValidationError::None)
			return {};
		std::vector<std::uint8_t> frame;
		frame.reserve(ApprovalHelperRequestSize);
		frame.insert(frame.end(), std::begin(ApprovalHelperRequestMagic),
			std::end(ApprovalHelperRequestMagic));
		frame.push_back(ApprovalHelperRequestMessageType);
		frame.push_back(0);
		frame.push_back(0);
		frame.push_back(0);
		WriteUint32(frame, request.protocolVersion);
		WriteUint64(frame, request.requestId);
		frame.insert(frame.end(), request.challengeId.begin(), request.challengeId.end());
		return frame;
	}

	bool DeserializeApprovalHelperRequest(
		std::span<const std::uint8_t> frame,
		ApprovalHelperRequest& request)
	{
		if (frame.size() != ApprovalHelperRequestSize ||
			!std::equal(std::begin(ApprovalHelperRequestMagic),
				std::end(ApprovalHelperRequestMagic), frame.begin()) ||
			frame[4] != ApprovalHelperRequestMessageType ||
			frame[5] != 0 || frame[6] != 0 || frame[7] != 0)
		{
			return false;
		}
		ApprovalHelperRequest parsed;
		parsed.protocolVersion = ReadUint32(frame, 8);
		parsed.requestId = ReadUint64(frame, 12);
		std::copy_n(frame.begin() + 20, parsed.challengeId.size(),
			parsed.challengeId.begin());
		if (ValidateApprovalHelperRequest(parsed) != ContractValidationError::None)
			return false;
		request = parsed;
		return true;
	}

	std::vector<std::uint8_t> SerializeApprovalHelperLeaseRequest(
		const ApprovalHelperRequest& request)
	{
		if (ValidateApprovalHelperRequest(request) != ContractValidationError::None)
			return {};
		std::vector<std::uint8_t> frame;
		frame.reserve(ApprovalHelperLeaseRequestSize);
		frame.insert(frame.end(), std::begin(ApprovalHelperLeaseRequestMagic),
			std::end(ApprovalHelperLeaseRequestMagic));
		frame.push_back(ApprovalHelperLeaseRequestMessageType);
		frame.push_back(0);
		frame.push_back(0);
		frame.push_back(0);
		WriteUint32(frame, request.protocolVersion);
		WriteUint64(frame, request.requestId);
		frame.insert(frame.end(), request.challengeId.begin(), request.challengeId.end());
		return frame;
	}

	bool DeserializeApprovalHelperLeaseRequest(
		std::span<const std::uint8_t> frame,
		ApprovalHelperRequest& request)
	{
		if (frame.size() != ApprovalHelperLeaseRequestSize ||
			!std::equal(std::begin(ApprovalHelperLeaseRequestMagic),
				std::end(ApprovalHelperLeaseRequestMagic), frame.begin()) ||
			frame[4] != ApprovalHelperLeaseRequestMessageType ||
			frame[5] != 0 || frame[6] != 0 || frame[7] != 0)
		{
			return false;
		}
		ApprovalHelperRequest parsed;
		parsed.protocolVersion = ReadUint32(frame, 8);
		parsed.requestId = ReadUint64(frame, 12);
		std::copy_n(frame.begin() + 20, parsed.challengeId.size(),
			parsed.challengeId.begin());
		if (ValidateApprovalHelperRequest(parsed) != ContractValidationError::None)
			return false;
		request = parsed;
		return true;
	}

	std::vector<std::uint8_t> SerializeApprovalHelperLeaseResult(
		const ApprovalHelperDecisionResult& result)
	{
		if (ValidateApprovalHelperDecisionResult(result) != ResultValidationError::None)
			return {};
		std::vector<std::uint8_t> frame;
		frame.reserve(ApprovalHelperLeaseResultSize);
		frame.insert(frame.end(), std::begin(ApprovalHelperLeaseResultMagic),
			std::end(ApprovalHelperLeaseResultMagic));
		frame.push_back(ApprovalHelperLeaseResultMessageType);
		frame.push_back(static_cast<std::uint8_t>(result.status));
		frame.push_back(0);
		frame.push_back(0);
		WriteUint32(frame, result.protocolVersion);
		WriteUint64(frame, result.requestId);
		WriteUint32(frame, result.windowsError);
		return frame;
	}

	bool DeserializeApprovalHelperLeaseResult(
		std::span<const std::uint8_t> frame,
		ApprovalHelperDecisionResult& result)
	{
		if (frame.size() != ApprovalHelperLeaseResultSize ||
			!std::equal(std::begin(ApprovalHelperLeaseResultMagic),
				std::end(ApprovalHelperLeaseResultMagic), frame.begin()) ||
			frame[4] != ApprovalHelperLeaseResultMessageType ||
			frame[6] != 0 || frame[7] != 0)
		{
			return false;
		}
		ApprovalHelperDecisionResult parsed;
		parsed.protocolVersion = ReadUint32(frame, 8);
		parsed.requestId = ReadUint64(frame, 12);
		parsed.status = static_cast<ApprovalConfirmationStatus>(frame[5]);
		parsed.windowsError = ReadUint32(frame, 20);
		if (ValidateApprovalHelperDecisionResult(parsed) != ResultValidationError::None)
			return false;
		result = parsed;
		return true;
	}

	std::vector<std::uint8_t> SerializeApprovalHelperDecisionRequest(
		const ApprovalHelperDecisionRequest& request)
	{
		if (ValidateApprovalHelperDecisionRequest(request) !=
			ContractValidationError::None)
		{
			return {};
		}
		std::vector<std::uint8_t> frame;
		frame.reserve(ApprovalHelperDecisionRequestSize);
		frame.insert(frame.end(), std::begin(ApprovalHelperDecisionRequestMagic),
			std::end(ApprovalHelperDecisionRequestMagic));
		frame.push_back(ApprovalHelperDecisionRequestMessageType);
		frame.push_back(static_cast<std::uint8_t>(request.decision));
		frame.push_back(static_cast<std::uint8_t>(request.persistence));
		frame.push_back(static_cast<std::uint8_t>(request.programScope));
		WriteUint32(frame, request.protocolVersion);
		WriteUint64(frame, request.requestId);
		frame.insert(frame.end(), request.challengeId.begin(), request.challengeId.end());
		frame.push_back(static_cast<std::uint8_t>(request.accountScope));
		frame.push_back(static_cast<std::uint8_t>(request.updateScope));
		frame.push_back(request.broadInterpreterApprovalConfirmed ? 1 : 0);
		frame.push_back(0);
		return frame;
	}

	bool DeserializeApprovalHelperDecisionRequest(
		std::span<const std::uint8_t> frame,
		ApprovalHelperDecisionRequest& request)
	{
		if (frame.size() != ApprovalHelperDecisionRequestSize ||
			!std::equal(std::begin(ApprovalHelperDecisionRequestMagic),
				std::end(ApprovalHelperDecisionRequestMagic), frame.begin()) ||
			frame[4] != ApprovalHelperDecisionRequestMessageType ||
			frame[38] > 1 || frame[39] != 0)
		{
			return false;
		}
		ApprovalHelperDecisionRequest parsed;
		parsed.protocolVersion = ReadUint32(frame, 8);
		parsed.requestId = ReadUint64(frame, 12);
		parsed.decision = static_cast<ApprovalDecision>(frame[5]);
		parsed.persistence = static_cast<ApprovalPersistence>(frame[6]);
		parsed.programScope = static_cast<ApprovalProgramScope>(frame[7]);
		std::copy_n(frame.begin() + 20, parsed.challengeId.size(),
			parsed.challengeId.begin());
		parsed.accountScope = static_cast<ApprovalAccountScope>(frame[36]);
		parsed.updateScope = static_cast<ApprovalUpdateScope>(frame[37]);
		parsed.broadInterpreterApprovalConfirmed = frame[38] == 1;
		if (ValidateApprovalHelperDecisionRequest(parsed) !=
			ContractValidationError::None)
		{
			return false;
		}
		request = parsed;
		return true;
	}

	std::vector<std::uint8_t> SerializeApprovalHelperDecisionResult(
		const ApprovalHelperDecisionResult& result)
	{
		if (ValidateApprovalHelperDecisionResult(result) != ResultValidationError::None)
			return {};
		std::vector<std::uint8_t> frame;
		frame.reserve(ApprovalHelperDecisionResultSize);
		frame.insert(frame.end(), std::begin(ApprovalHelperDecisionResultMagic),
			std::end(ApprovalHelperDecisionResultMagic));
		frame.push_back(ApprovalHelperDecisionResultMessageType);
		frame.push_back(static_cast<std::uint8_t>(result.status));
		frame.push_back(0);
		frame.push_back(0);
		WriteUint32(frame, result.protocolVersion);
		WriteUint64(frame, result.requestId);
		WriteUint32(frame, result.windowsError);
		return frame;
	}

	bool DeserializeApprovalHelperDecisionResult(
		std::span<const std::uint8_t> frame,
		ApprovalHelperDecisionResult& result)
	{
		if (frame.size() != ApprovalHelperDecisionResultSize ||
			!std::equal(std::begin(ApprovalHelperDecisionResultMagic),
				std::end(ApprovalHelperDecisionResultMagic), frame.begin()) ||
			frame[4] != ApprovalHelperDecisionResultMessageType ||
			frame[6] != 0 || frame[7] != 0)
		{
			return false;
		}
		ApprovalHelperDecisionResult parsed;
		parsed.protocolVersion = ReadUint32(frame, 8);
		parsed.requestId = ReadUint64(frame, 12);
		parsed.status = static_cast<ApprovalConfirmationStatus>(frame[5]);
		parsed.windowsError = ReadUint32(frame, 20);
		if (ValidateApprovalHelperDecisionResult(parsed) != ResultValidationError::None)
			return false;
		result = parsed;
		return true;
	}

// Serializes a valid approval list request into the fixed protocol format.
	std::vector<std::uint8_t> SerializeApprovalListRequest(
		const ApprovalListRequest& request)
	{
		if (ValidateApprovalListRequest(request) != ContractValidationError::None)
			return {};
		std::vector<std::uint8_t> frame;
		frame.insert(frame.end(), std::begin(ApprovalListRequestMagic),
			std::end(ApprovalListRequestMagic));
		frame.push_back(ApprovalListRequestMessageType);
		frame.insert(frame.end(), 3, 0);
		WriteUint32(frame, request.protocolVersion);
		WriteUint64(frame, request.requestId);
		return frame;
	}

// Accepts an approval list request only from a valid frame of the exact size.
	bool DeserializeApprovalListRequest(std::span<const std::uint8_t> frame,
		ApprovalListRequest& request)
	{
		if (frame.size() != ApprovalListRequestSize ||
			!std::equal(std::begin(ApprovalListRequestMagic),
				std::end(ApprovalListRequestMagic), frame.begin()) ||
			frame[4] != ApprovalListRequestMessageType || frame[5] || frame[6] || frame[7])
		{
			return false;
		}
		ApprovalListRequest parsed;
		parsed.protocolVersion = ReadUint32(frame, 8);
		parsed.requestId = ReadUint64(frame, 12);
		if (ValidateApprovalListRequest(parsed) != ContractValidationError::None)
			return false;
		request = parsed;
		return true;
	}

// Serializes fully valid approvals as length-bound list entries.
	std::vector<std::uint8_t> SerializeApprovalListResult(
		const ApprovalListResult& result)
	{
		if (ValidateApprovalListResult(result) != ResultValidationError::None ||
			result.entries.size() > MaximumApprovalEntries)
		{
			return {};
		}
		std::vector<std::vector<std::uint8_t>> values;
		std::size_t totalSize = ApprovalListResultHeaderSize;
		for (const auto& entry : result.entries)
		{
			auto value = xucrun::approval::SerializeBrokerApproval(
				entry, MaximumApprovalEntrySize);
			if (value.empty())
				return {};
			totalSize += sizeof(std::uint32_t) + value.size();
			values.push_back(std::move(value));
		}
		if (totalSize > MaximumApprovalListResultSize)
			return {};
		std::vector<std::uint8_t> frame;
		frame.reserve(totalSize);
		frame.insert(frame.end(), std::begin(ApprovalListResultMagic),
			std::end(ApprovalListResultMagic));
		frame.push_back(ApprovalListResultMessageType);
		frame.push_back(static_cast<std::uint8_t>(result.status));
		frame.push_back(0);
		frame.push_back(0);
		WriteUint32(frame, result.protocolVersion);
		WriteUint64(frame, result.requestId);
		WriteUint32(frame, result.windowsError);
		WriteUint32(frame, static_cast<std::uint32_t>(values.size()));
		for (const auto& value : values)
		{
			WriteUint32(frame, static_cast<std::uint32_t>(value.size()));
			frame.insert(frame.end(), value.begin(), value.end());
		}
		return frame;
	}

// Accepts an approval list only after complete consumption of all valid entries.
	bool DeserializeApprovalListResult(std::span<const std::uint8_t> frame,
		ApprovalListResult& result)
	{
		if (frame.size() < ApprovalListResultHeaderSize ||
			frame.size() > MaximumApprovalListResultSize ||
			!std::equal(std::begin(ApprovalListResultMagic),
				std::end(ApprovalListResultMagic), frame.begin()) ||
			frame[4] != ApprovalListResultMessageType || frame[6] || frame[7])
		{
			return false;
		}
		ApprovalListResult parsed;
		parsed.protocolVersion = ReadUint32(frame, 8);
		parsed.requestId = ReadUint64(frame, 12);
		parsed.status = static_cast<ApprovalManagementStatus>(frame[5]);
		parsed.windowsError = ReadUint32(frame, 20);
		const std::size_t count = ReadUint32(frame, 24);
		if (count > MaximumApprovalEntries)
			return false;
		std::size_t offset = ApprovalListResultHeaderSize;
		for (std::size_t index = 0; index < count; index++)
		{
			if (offset > frame.size() || frame.size() - offset < sizeof(std::uint32_t))
				return false;
			const std::size_t size = ReadUint32(frame, offset);
			offset += sizeof(std::uint32_t);
			if (size == 0 || size > MaximumApprovalEntrySize || size > frame.size() - offset)
				return false;
			xucrun::approval::BrokerApprovalValue entry;
			if (!xucrun::approval::DeserializeBrokerApproval(
				frame.subspan(offset, size), entry))
				return false;
			parsed.entries.push_back(std::move(entry));
			offset += size;
		}
		if (offset != frame.size() ||
			ValidateApprovalListResult(parsed) != ResultValidationError::None)
			return false;
		result = std::move(parsed);
		return true;
	}

// Serializes a valid reset request with exactly one canonical approval.
	std::vector<std::uint8_t> SerializeApprovalResetRequest(
		const ApprovalResetRequest& request)
	{
		if (ValidateApprovalResetRequest(request) != ContractValidationError::None)
			return {};
		const std::vector<std::uint8_t> approval =
			xucrun::approval::SerializeBrokerApproval(
				request.approval, MaximumApprovalEntrySize);
		std::vector<std::uint8_t> frame;
		frame.insert(frame.end(), std::begin(ApprovalResetRequestMagic),
			std::end(ApprovalResetRequestMagic));
		frame.push_back(ApprovalResetRequestMessageType);
		frame.insert(frame.end(), 3, 0);
		WriteUint32(frame, request.protocolVersion);
		WriteUint64(frame, request.requestId);
		WriteUint32(frame, static_cast<std::uint32_t>(approval.size()));
		frame.insert(frame.end(), approval.begin(), approval.end());
		return frame;
	}

// Accepts a reset request only after complete validation of the embedded approval.
	bool DeserializeApprovalResetRequest(std::span<const std::uint8_t> frame,
		ApprovalResetRequest& request)
	{
		if (frame.size() < ApprovalResetRequestHeaderSize ||
			!std::equal(std::begin(ApprovalResetRequestMagic),
				std::end(ApprovalResetRequestMagic), frame.begin()) ||
			frame[4] != ApprovalResetRequestMessageType || frame[5] || frame[6] || frame[7])
			return false;
		ApprovalResetRequest parsed;
		parsed.protocolVersion = ReadUint32(frame, 8);
		parsed.requestId = ReadUint64(frame, 12);
		const std::size_t length = ReadUint32(frame, 20);
		std::size_t offset = ApprovalResetRequestHeaderSize;
		if (length == 0 || length > MaximumApprovalEntrySize ||
			length != frame.size() - offset ||
			!xucrun::approval::DeserializeBrokerApproval(
				frame.subspan(offset, length), parsed.approval) ||
			ValidateApprovalResetRequest(parsed) != ContractValidationError::None)
			return false;
		request = std::move(parsed);
		return true;
	}

// Serializes a valid reset result into the fixed protocol format.
	std::vector<std::uint8_t> SerializeApprovalResetResult(
		const ApprovalResetResult& result)
	{
		if (ValidateApprovalResetResult(result) != ResultValidationError::None)
			return {};
		std::vector<std::uint8_t> frame;
		frame.insert(frame.end(), std::begin(ApprovalResetResultMagic),
			std::end(ApprovalResetResultMagic));
		frame.push_back(ApprovalResetResultMessageType);
		frame.push_back(static_cast<std::uint8_t>(result.status));
		frame.push_back(0);
		frame.push_back(0);
		WriteUint32(frame, result.protocolVersion);
		WriteUint64(frame, result.requestId);
		WriteUint32(frame, result.windowsError);
		WriteUint32(frame, result.deletedCount);
		return frame;
	}

// Accepts a reset result only from a valid frame of the exact size.
	bool DeserializeApprovalResetResult(std::span<const std::uint8_t> frame,
		ApprovalResetResult& result)
	{
		if (frame.size() != ApprovalResetResultSize ||
			!std::equal(std::begin(ApprovalResetResultMagic),
				std::end(ApprovalResetResultMagic), frame.begin()) ||
			frame[4] != ApprovalResetResultMessageType || frame[6] || frame[7])
			return false;
		ApprovalResetResult parsed;
		parsed.protocolVersion = ReadUint32(frame, 8);
		parsed.requestId = ReadUint64(frame, 12);
		parsed.status = static_cast<ApprovalManagementStatus>(frame[5]);
		parsed.windowsError = ReadUint32(frame, 20);
		parsed.deletedCount = ReadUint32(frame, 24);
		if (ValidateApprovalResetResult(parsed) != ResultValidationError::None)
			return false;
		result = parsed;
		return true;
	}
