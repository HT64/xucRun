// Purpose: Serializes and deserializes broker protocol messages.
// Validates identifiers, versions, lengths, and complete frame consumption.
#include "BrokerSerialization.h"

#include <algorithm>
#include <iterator>
#include <utility>

namespace
{
	constexpr std::uint8_t RequestMagic[] = { 'X', 'U', 'C', 'R' };
	constexpr std::uint8_t RequestMessageType = 1;
	constexpr std::uint8_t ResultMagic[] = { 'X', 'U', 'C', 'S' };
	constexpr std::uint8_t ResultMessageType = 2;
	constexpr std::uint8_t HealthRequestMagic[] = { 'X', 'U', 'C', 'H' };
	constexpr std::uint8_t HealthRequestMessageType = 3;
	constexpr std::uint8_t HealthResultMagic[] = { 'X', 'U', 'C', 'P' };
	constexpr std::uint8_t HealthResultMessageType = 4;
	constexpr std::uint8_t HealthAcknowledgementMagic[] = { 'X', 'U', 'C', 'A' };
	constexpr std::uint8_t HealthAcknowledgementMessageType = 5;
	constexpr std::uint8_t CredentialMetadataRequestMagic[] = { 'X', 'U', 'C', 'M' };
	constexpr std::uint8_t CredentialMetadataRequestMessageType = 6;
	constexpr std::uint8_t CredentialMetadataResultMagic[] = { 'X', 'U', 'C', 'N' };
	constexpr std::uint8_t CredentialMetadataResultMessageType = 7;
	constexpr std::uint8_t SessionTestRequestMagic[] = { 'X', 'U', 'C', 'T' };
	constexpr std::uint8_t SessionTestRequestMessageType = 8;
	constexpr std::uint8_t SessionTestResultMagic[] = { 'X', 'U', 'C', 'U' };
	constexpr std::uint8_t SessionTestResultMessageType = 9;
	constexpr std::uint8_t CredentialStoreTestRequestMagic[] = { 'X', 'U', 'C', 'V' };
	constexpr std::uint8_t CredentialStoreTestRequestMessageType = 10;
	constexpr std::uint8_t CredentialStoreTestResultMagic[] = { 'X', 'U', 'C', 'W' };
	constexpr std::uint8_t CredentialStoreTestResultMessageType = 11;
	constexpr std::uint8_t CredentialSaveRequestMagic[] = { 'X', 'U', 'C', 'Y' };
	constexpr std::uint8_t CredentialSaveRequestMessageType = 12;
	constexpr std::uint8_t CredentialSaveResultMagic[] = { 'X', 'U', 'C', 'Z' };
	constexpr std::uint8_t CredentialSaveResultMessageType = 13;
	constexpr std::uint8_t CredentialDeleteRequestMagic[] = { 'X', 'U', 'D', 'A' };
	constexpr std::uint8_t CredentialDeleteRequestMessageType = 14;
	constexpr std::uint8_t CredentialDeleteResultMagic[] = { 'X', 'U', 'D', 'B' };
	constexpr std::uint8_t CredentialDeleteResultMessageType = 15;
	constexpr std::uint8_t CredentialUseTestRequestMagic[] = { 'X', 'U', 'D', 'C' };
	constexpr std::uint8_t CredentialUseTestRequestMessageType = 16;
	constexpr std::uint8_t CredentialUseTestResultMagic[] = { 'X', 'U', 'D', 'D' };
	constexpr std::uint8_t CredentialUseTestResultMessageType = 17;
	constexpr std::uint8_t ApprovalChallengeRequestMagic[] = { 'X', 'U', 'D', 'E' };
	constexpr std::uint8_t ApprovalChallengeRequestMessageType = 18;
	constexpr std::uint8_t ApprovalChallengeResultMagic[] = { 'X', 'U', 'D', 'F' };
	constexpr std::uint8_t ApprovalChallengeResultMessageType = 19;
	constexpr std::uint8_t ApprovalConfirmationRequestMagic[] = { 'X', 'U', 'D', 'G' };
	constexpr std::uint8_t ApprovalConfirmationRequestMessageType = 20;
	constexpr std::uint8_t ApprovalConfirmationResultMagic[] = { 'X', 'U', 'D', 'H' };
	constexpr std::uint8_t ApprovalConfirmationResultMessageType = 21;
	constexpr std::uint8_t CredentialListRequestMagic[] = { 'X', 'U', 'D', 'I' };
	constexpr std::uint8_t CredentialListRequestMessageType = 22;
	constexpr std::uint8_t CredentialListResultMagic[] = { 'X', 'U', 'D', 'J' };
	constexpr std::uint8_t CredentialListResultMessageType = 23;
	constexpr std::uint8_t ApprovalListRequestMagic[] = { 'X', 'U', 'D', 'K' };
	constexpr std::uint8_t ApprovalListRequestMessageType = 24;
	constexpr std::uint8_t ApprovalListResultMagic[] = { 'X', 'U', 'D', 'L' };
	constexpr std::uint8_t ApprovalListResultMessageType = 25;
	constexpr std::uint8_t ApprovalResetRequestMagic[] = { 'X', 'U', 'D', 'M' };
	constexpr std::uint8_t ApprovalResetRequestMessageType = 26;
	constexpr std::uint8_t ApprovalResetResultMagic[] = { 'X', 'U', 'D', 'N' };
	constexpr std::uint8_t ApprovalResetResultMessageType = 27;
	constexpr std::uint8_t SecurityAuditRequestMagic[] = { 'X', 'U', 'D', 'O' };
	constexpr std::uint8_t SecurityAuditRequestMessageType = 28;
	constexpr std::uint8_t SecurityAuditResultMagic[] = { 'X', 'U', 'D', 'P' };
	constexpr std::uint8_t SecurityAuditResultMessageType = 29;
	constexpr std::uint8_t TrackedProcessListRequestMagic[] = { 'X', 'U', 'D', 'Q' };
	constexpr std::uint8_t TrackedProcessListRequestMessageType = 30;
	constexpr std::uint8_t TrackedProcessListResultMagic[] = { 'X', 'U', 'D', 'R' };
	constexpr std::uint8_t TrackedProcessListResultMessageType = 31;
	constexpr std::uint8_t TrackedProcessRegistrationRequestMagic[] = { 'X', 'U', 'D', 'S' };
	constexpr std::uint8_t TrackedProcessRegistrationRequestMessageType = 32;
	constexpr std::uint8_t TrackedProcessRegistrationResultMagic[] = { 'X', 'U', 'D', 'T' };
	constexpr std::uint8_t TrackedProcessRegistrationResultMessageType = 33;
	constexpr std::uint8_t BrokerStopRequestMagic[] = { 'X', 'U', 'D', 'U' };
	constexpr std::uint8_t BrokerStopRequestMessageType = 34;
	constexpr std::uint8_t BrokerStopResultMagic[] = { 'X', 'U', 'D', 'V' };
	constexpr std::uint8_t BrokerStopResultMessageType = 35;
	constexpr std::uint8_t ApprovalHelperRequestMagic[] = { 'X', 'U', 'D', 'W' };
	constexpr std::uint8_t ApprovalHelperRequestMessageType = 36;
	constexpr std::uint8_t ApprovalHelperDecisionRequestMagic[] = { 'X', 'U', 'D', 'X' };
	constexpr std::uint8_t ApprovalHelperDecisionRequestMessageType = 37;
	constexpr std::uint8_t ApprovalHelperDecisionResultMagic[] = { 'X', 'U', 'D', 'Y' };
	constexpr std::uint8_t ApprovalHelperDecisionResultMessageType = 38;
	constexpr std::uint8_t ApprovalHelperLeaseRequestMagic[] = { 'X', 'U', 'D', 'Z' };
	constexpr std::uint8_t ApprovalHelperLeaseRequestMessageType = 39;
	constexpr std::uint8_t ApprovalHelperLeaseResultMagic[] = { 'X', 'U', 'E', 'A' };
	constexpr std::uint8_t ApprovalHelperLeaseResultMessageType = 40;
	constexpr std::uint8_t CredentialCaptureChallengeRequestMagic[] = { 'X', 'U', 'E', 'B' };
	constexpr std::uint8_t CredentialCaptureChallengeRequestMessageType = 41;
	constexpr std::uint8_t CredentialCaptureChallengeResultMagic[] = { 'X', 'U', 'E', 'C' };
	constexpr std::uint8_t CredentialCaptureChallengeResultMessageType = 42;
	constexpr std::uint8_t CredentialCaptureHelperRequestMagic[] = { 'X', 'U', 'E', 'D' };
	constexpr std::uint8_t CredentialCaptureHelperRequestMessageType = 43;
	constexpr std::uint8_t CredentialCaptureHelperResultMagic[] = { 'X', 'U', 'E', 'E' };
	constexpr std::uint8_t CredentialCaptureHelperResultMessageType = 44;
	constexpr std::uint8_t CredentialCaptureSubmitRequestMagic[] = { 'X', 'U', 'E', 'F' };
	constexpr std::uint8_t CredentialCaptureSubmitRequestMessageType = 45;
	constexpr std::uint8_t CredentialCaptureSubmitResultMagic[] = { 'X', 'U', 'E', 'G' };
	constexpr std::uint8_t CredentialCaptureSubmitResultMessageType = 46;
	constexpr std::uint8_t AdministrativeInventoryRequestMagic[] = { 'X', 'U', 'E', 'H' };
	constexpr std::uint8_t AdministrativeInventoryRequestMessageType = 47;
	constexpr std::uint8_t AdministrativeInventoryResultMagic[] = { 'X', 'U', 'E', 'I' };
	constexpr std::uint8_t AdministrativeInventoryResultMessageType = 48;
	constexpr std::uint8_t BackupRequestMagic[] = { 'X', 'U', 'E', 'J' };
	constexpr std::uint8_t BackupRequestMessageType = 49;
	constexpr std::uint8_t BackupResultMagic[] = { 'X', 'U', 'E', 'K' };
	constexpr std::uint8_t BackupResultMessageType = 50;
	constexpr std::uint8_t CredentialCaptureCommitRequestMagic[] = { 'X', 'U', 'E', 'L' };
	constexpr std::uint8_t CredentialCaptureCommitRequestMessageType = 51;
	constexpr std::uint8_t CredentialCaptureCommitResultMagic[] = { 'X', 'U', 'E', 'M' };
	constexpr std::uint8_t CredentialCaptureCommitResultMessageType = 52;
	constexpr std::uint8_t AdministrativeDeleteRequestMagic[] = { 'X', 'U', 'E', 'N' };
	constexpr std::uint8_t AdministrativeDeleteRequestMessageType = 53;
	constexpr std::uint8_t AdministrativeDeleteResultMagic[] = { 'X', 'U', 'E', 'O' };
	constexpr std::uint8_t AdministrativeDeleteResultMessageType = 54;
	constexpr std::uint8_t StatusIconListRequestMagic[] = { 'X', 'U', 'E', 'P' };
	constexpr std::uint8_t StatusIconListRequestMessageType = 55;
	constexpr std::uint8_t StatusIconListResultMagic[] = { 'X', 'U', 'E', 'Q' };
	constexpr std::uint8_t StatusIconListResultMessageType = 56;
	constexpr std::uint8_t BrokerPolicyRejectionMagic[] = { 'X', 'U', 'E', 'R' };
	constexpr std::uint8_t BrokerPolicyRejectionMessageType = 57;
	constexpr std::uint8_t SystemLaunchRequestMagic[] = { 'X', 'U', 'E', 'S' };
	constexpr std::uint8_t SystemLaunchRequestMessageType = 58;
	constexpr std::uint8_t SystemLaunchResultMagic[] = { 'X', 'U', 'E', 'T' };
	constexpr std::uint8_t SystemLaunchResultMessageType = 59;

	// Checks whether a nonce contains at least one nonzero byte.
	bool HasNonce(const xucrun::broker::RequestNonce& nonce)
	{
		return std::any_of(nonce.begin(), nonce.end(), [](std::uint8_t byte) {
			return byte != 0;
		});
	}

	// Writes a 32-bit integer canonically in little-endian order.
	void WriteUint32(std::vector<std::uint8_t>& value, std::uint32_t number)
	{
		for (std::size_t index = 0; index < sizeof(number); index++)
			value.push_back(static_cast<std::uint8_t>(number >> (index * 8)));
	}

	// Writes a 64-bit integer canonically in little-endian order.
	void WriteUint64(std::vector<std::uint8_t>& value, std::uint64_t number)
	{
		for (std::size_t index = 0; index < sizeof(number); index++)
			value.push_back(static_cast<std::uint8_t>(number >> (index * 8)));
	}

	// Writes a wide string canonically as UTF-16LE without a terminator.
	void WriteString(std::vector<std::uint8_t>& value, const std::wstring& text)
	{
		for (wchar_t character : text)
		{
			const std::uint16_t utf16 = static_cast<std::uint16_t>(character);
			value.push_back(static_cast<std::uint8_t>(utf16));
			value.push_back(static_cast<std::uint8_t>(utf16 >> 8));
		}
	}

	// Writes the specified string canonically as UTF-16LE without a terminator.
	void WriteString(std::vector<std::uint8_t>& value, const wchar_t* text,
		std::size_t length)
	{
		for (std::size_t index = 0; index < length; index++)
		{
			const std::uint16_t utf16 = static_cast<std::uint16_t>(text[index]);
			value.push_back(static_cast<std::uint8_t>(utf16));
			value.push_back(static_cast<std::uint8_t>(utf16 >> 8));
		}
	}

	// Reads a 32-bit integer from the canonical little-endian representation.
	std::uint32_t ReadUint32(std::span<const std::uint8_t> value, std::size_t offset)
	{
		std::uint32_t number = 0;
		for (std::size_t index = 0; index < sizeof(number); index++)
			number |= static_cast<std::uint32_t>(value[offset + index]) << (index * 8);
		return number;
	}

	// Reads a 64-bit integer from the canonical little-endian representation.
	std::uint64_t ReadUint64(std::span<const std::uint8_t> value, std::size_t offset)
	{
		std::uint64_t number = 0;
		for (std::size_t index = 0; index < sizeof(number); index++)
			number |= static_cast<std::uint64_t>(value[offset + index]) << (index * 8);
		return number;
	}

	// Detects embedded null characters that the wire format cannot represent.
	bool HasEmbeddedNull(const std::wstring& value)
	{
		return value.find(L'\0') != std::wstring::npos;
	}

	// Reads exactly the specified number of null-free UTF-16LE characters.
	bool ReadString(std::span<const std::uint8_t> frame, std::size_t& offset,
		std::size_t length, std::wstring& value)
	{
		value.resize(length);
		for (std::size_t index = 0; index < length; index++)
		{
			const std::uint16_t character = static_cast<std::uint16_t>(frame[offset]) |
				(static_cast<std::uint16_t>(frame[offset + 1]) << 8);
			if (character == 0)
				return false;
			value[index] = static_cast<wchar_t>(character);
			offset += 2;
		}
		return true;
	}
}

namespace xucrun::broker
{

	// The wire-format implementations remain in this translation unit so every domain
	// uses exactly the same magic values, primitive encoding, and validation helpers.
#include "BrokerSerializationCore.inl"
#include "BrokerSerializationCredentials.inl"
#include "BrokerSerializationApprovals.inl"
#include "BrokerSerializationAdministration.inl"

}