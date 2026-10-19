// Purpose: Defines trust verification for executable Windows files.
// Behavior: Produces results for embedded signatures, catalog signatures, and publisher checks.
#pragma once

#include <Windows.h>

#include <string>
#include <vector>

namespace xucrun::image_trust
{
	bool Verify(const std::wstring& imagePath, LONG& trustStatus,
		bool& microsoftPublisher);
	bool VerifyWithSignerCertificate(const std::wstring& imagePath, LONG& trustStatus,
		std::vector<BYTE>& signerCertificate);
}