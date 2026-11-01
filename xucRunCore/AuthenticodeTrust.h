// Purpose: Defines shared embedded and catalog Authenticode verification.
// Approach: Exposes the declarations, constants, and types consumed by the components that provide this behavior.
#pragma once

#include <Windows.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace xucrun::authenticode
{
	enum class SignatureKind : std::uint8_t
	{
		None,
		Embedded,
		Catalog
	};

	struct VerificationResult
	{
		LONG trustStatus = static_cast<LONG>(0x800B0100u);
		bool trusted = false;
		bool microsoftPublisher = false;
		SignatureKind signatureKind = SignatureKind::None;
		std::wstring signerSubject;
		std::wstring issuerSubject;
		std::wstring timestampSignerSubject;
		std::uint64_t certificateValidFrom = 0;
		std::uint64_t certificateValidUntil = 0;
		std::uint64_t timestamp = 0;
		std::vector<BYTE> signerCertificate;
		std::array<BYTE, 32> signerCertificateSha256 = {};
	};

	bool Verify(const std::wstring& filePath, HANDLE file,
		VerificationResult& result);
}
