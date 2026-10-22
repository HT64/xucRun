// Purpose: Defines trust verification for executable Windows files.
// Behavior: Produces results for embedded signatures, catalog signatures, and publisher checks.
#pragma once

#include <Windows.h>

#include <string>
#include <vector>

namespace xucrun::image_trust
{
	struct SignerCertificateInfo
	{
		std::wstring subject;
		std::wstring sha256Fingerprint;
		std::wstring validFrom;
		std::wstring validUntil;
	};

	bool Verify(const std::wstring& imagePath, LONG& trustStatus,
		bool& microsoftPublisher);
	bool VerifyWithSignerCertificate(const std::wstring& imagePath, LONG& trustStatus,
		std::vector<BYTE>& signerCertificate);
	bool GetSignerCertificateInfo(const std::vector<BYTE>& signerCertificate,
		SignerCertificateInfo& certificateInfo);
}