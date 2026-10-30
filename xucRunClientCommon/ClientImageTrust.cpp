// Purpose: Adapts shared Authenticode verification for client-facing trust details.
// Approach: Implements the behavior with focused helpers built on shared xucRun interfaces and Windows APIs.
#include "ClientImageTrust.h"

#include "AuthenticodeTrust.h"

#include <wincrypt.h>

#include <utility>
#include <vector>

#pragma comment(lib, "Crypt32.lib")

namespace xucrun::image_trust
{
	bool Verify(const std::wstring& imagePath, LONG& trustStatus,
		bool& microsoftPublisher)
	{
		authenticode::VerificationResult result;
		const bool trusted = authenticode::Verify(imagePath, nullptr, result);
		trustStatus = result.trustStatus;
		microsoftPublisher = result.microsoftPublisher;
		return trusted;
	}

	bool VerifyWithSignerCertificate(const std::wstring& imagePath, LONG& trustStatus,
		std::vector<BYTE>& signerCertificate)
	{
		authenticode::VerificationResult result;
		const bool trusted = authenticode::Verify(imagePath, nullptr, result);
		trustStatus = result.trustStatus;
		signerCertificate = std::move(result.signerCertificate);
		return trusted && !signerCertificate.empty();
	}

	bool GetSignerCertificateInfo(const std::vector<BYTE>& signerCertificate,
		SignerCertificateInfo& certificateInfo)
	{
		certificateInfo = {};
		if (signerCertificate.empty())
			return false;

		PCCERT_CONTEXT certificate = CertCreateCertificateContext(
			X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, signerCertificate.data(),
			static_cast<DWORD>(signerCertificate.size()));
		if (!certificate)
			return false;

		const DWORD subjectLength = CertGetNameStringW(certificate,
			CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, nullptr, 0);
		std::vector<wchar_t> subject(subjectLength);
		DWORD fingerprintLength = 32;
		std::vector<BYTE> fingerprint(fingerprintLength);
		SYSTEMTIME validFrom = {};
		SYSTEMTIME validUntil = {};
		const bool available = subjectLength > 1 &&
			CertGetNameStringW(certificate, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0,
				nullptr, subject.data(), subjectLength) == subjectLength &&
			CryptHashCertificate2(L"SHA256", 0, nullptr,
				certificate->pbCertEncoded, certificate->cbCertEncoded,
				fingerprint.data(), &fingerprintLength) != FALSE &&
			fingerprintLength == fingerprint.size() &&
			FileTimeToSystemTime(&certificate->pCertInfo->NotBefore, &validFrom) != FALSE &&
			FileTimeToSystemTime(&certificate->pCertInfo->NotAfter, &validUntil) != FALSE;
		if (available)
		{
			certificateInfo.subject.assign(subject.data(), subjectLength - 1);
			static constexpr wchar_t HexDigits[] = L"0123456789ABCDEF";
			certificateInfo.sha256Fingerprint.reserve(fingerprint.size() * 2);
			for (BYTE value : fingerprint)
			{
				certificateInfo.sha256Fingerprint += HexDigits[value >> 4];
				certificateInfo.sha256Fingerprint += HexDigits[value & 0x0F];
			}
			wchar_t date[11] = {};
			swprintf_s(date, L"%04u-%02u-%02u",
				validFrom.wYear, validFrom.wMonth, validFrom.wDay);
			certificateInfo.validFrom = date;
			swprintf_s(date, L"%04u-%02u-%02u",
				validUntil.wYear, validUntil.wMonth, validUntil.wDay);
			certificateInfo.validUntil = date;
		}
		CertFreeCertificateContext(certificate);
		return available;
	}
}
