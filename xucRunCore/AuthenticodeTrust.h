// Purpose: Defines shared embedded and catalog Authenticode verification.
// Approach: Exposes the declarations, constants, and types consumed by the components that provide this behavior.
#pragma once

#include <Windows.h>

#include <array>
#include <string>
#include <vector>

namespace xucrun::authenticode
{
	struct VerificationResult
	{
		LONG trustStatus = static_cast<LONG>(0x800B0100u);
		bool trusted = false;
		bool microsoftPublisher = false;
		std::vector<BYTE> signerCertificate;
		std::array<BYTE, 32> signerCertificateSha256 = {};
	};

	bool Verify(const std::wstring& filePath, HANDLE file,
		VerificationResult& result);
}
