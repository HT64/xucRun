// Purpose: Verifies embedded signatures and Windows catalog signatures uniformly.
#include "AuthenticodeTrust.h"

#include <Softpub.h>
#include <mscat.h>
#include <wincrypt.h>
#include <wintrust.h>

#include <string>
#include <vector>

#pragma comment(lib, "Crypt32.lib")
#pragma comment(lib, "Wintrust.lib")

namespace
{
	PCCERT_CONTEXT GetSignerCertificate(const WINTRUST_DATA& trustData)
	{
		CRYPT_PROVIDER_DATA* providerData =
			WTHelperProvDataFromStateData(trustData.hWVTStateData);
		CRYPT_PROVIDER_SGNR* signer = providerData
			? WTHelperGetProvSignerFromChain(providerData, 0, FALSE, 0)
			: nullptr;
		return signer && signer->csCertChain > 0
			? signer->pasCertChain[0].pCert
			: nullptr;
	}

	void ReadSigner(const WINTRUST_DATA& trustData,
		xucrun::authenticode::VerificationResult& result)
	{
		PCCERT_CONTEXT certificate = GetSignerCertificate(trustData);
		if (!certificate)
			return;

		result.signerCertificate.assign(
			certificate->pbCertEncoded,
			certificate->pbCertEncoded + certificate->cbCertEncoded);
		DWORD fingerprintSize =
			static_cast<DWORD>(result.signerCertificateSha256.size());
		if (!CertGetCertificateContextProperty(
			certificate, CERT_SHA256_HASH_PROP_ID,
			result.signerCertificateSha256.data(), &fingerprintSize) ||
			fingerprintSize != result.signerCertificateSha256.size())
		{
			result.signerCertificateSha256 = {};
		}

		wchar_t organization[256] = {};
		const DWORD organizationLength = CertGetNameStringW(
			certificate, CERT_NAME_ATTR_TYPE, 0,
			const_cast<char*>(szOID_ORGANIZATION_NAME), organization,
			static_cast<DWORD>(std::size(organization)));
		result.microsoftPublisher = organizationLength > 1 &&
			_wcsicmp(organization, L"Microsoft Corporation") == 0;
	}

	bool VerifyCatalog(const std::wstring& filePath, HANDLE suppliedFile,
		xucrun::authenticode::VerificationResult& result)
	{
		const bool ownsFile = !suppliedFile || suppliedFile == INVALID_HANDLE_VALUE;
		HANDLE file = ownsFile
			? CreateFileW(filePath.c_str(), GENERIC_READ,
				FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
				nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)
			: suppliedFile;
		if (file == INVALID_HANDLE_VALUE)
			return false;

		HCATADMIN catalogAdmin = nullptr;
		if (!CryptCATAdminAcquireContext2(&catalogAdmin, nullptr, nullptr, nullptr, 0))
		{
			if (ownsFile)
				CloseHandle(file);
			return false;
		}

		DWORD hashSize = 0;
		CryptCATAdminCalcHashFromFileHandle2(catalogAdmin, file, &hashSize, nullptr, 0);
		std::vector<BYTE> hash(hashSize);
		if (hashSize == 0 || !CryptCATAdminCalcHashFromFileHandle2(
			catalogAdmin, file, &hashSize, hash.data(), 0))
		{
			CryptCATAdminReleaseContext(catalogAdmin, 0);
			if (ownsFile)
				CloseHandle(file);
			return false;
		}
		hash.resize(hashSize);

		HCATINFO catalogContext = CryptCATAdminEnumCatalogFromHash(
			catalogAdmin, hash.data(), static_cast<DWORD>(hash.size()), 0, nullptr);
		if (!catalogContext)
		{
			CryptCATAdminReleaseContext(catalogAdmin, 0);
			if (ownsFile)
				CloseHandle(file);
			return false;
		}

		CATALOG_INFO catalogInfo = {};
		catalogInfo.cbStruct = sizeof(catalogInfo);
		if (!CryptCATCatalogInfoFromContext(catalogContext, &catalogInfo, 0))
		{
			CryptCATAdminReleaseCatalogContext(catalogAdmin, catalogContext, 0);
			CryptCATAdminReleaseContext(catalogAdmin, 0);
			if (ownsFile)
				CloseHandle(file);
			return false;
		}

		static constexpr wchar_t HexDigits[] = L"0123456789ABCDEF";
		std::wstring memberTag;
		memberTag.reserve(hash.size() * 2);
		for (BYTE value : hash)
		{
			memberTag += HexDigits[value >> 4];
			memberTag += HexDigits[value & 0x0F];
		}

		WINTRUST_CATALOG_INFO catalog = {};
		catalog.cbStruct = sizeof(catalog);
		catalog.pcwszCatalogFilePath = catalogInfo.wszCatalogFile;
		catalog.pcwszMemberTag = memberTag.c_str();
		catalog.pcwszMemberFilePath = filePath.c_str();
		catalog.hMemberFile = file;
		catalog.pbCalculatedFileHash = hash.data();
		catalog.cbCalculatedFileHash = static_cast<DWORD>(hash.size());
		catalog.hCatAdmin = catalogAdmin;

		WINTRUST_DATA trustData = {};
		trustData.cbStruct = sizeof(trustData);
		trustData.dwUIChoice = WTD_UI_NONE;
		trustData.fdwRevocationChecks = WTD_REVOKE_NONE;
		trustData.dwUnionChoice = WTD_CHOICE_CATALOG;
		trustData.pCatalog = &catalog;
		trustData.dwStateAction = WTD_STATEACTION_VERIFY;
		trustData.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL | WTD_SAFER_FLAG;

		GUID policy = WINTRUST_ACTION_GENERIC_VERIFY_V2;
		result.trustStatus = WinVerifyTrust(nullptr, &policy, &trustData);
		result.trusted = result.trustStatus == ERROR_SUCCESS;
		if (result.trusted)
			ReadSigner(trustData, result);

		trustData.dwStateAction = WTD_STATEACTION_CLOSE;
		WinVerifyTrust(nullptr, &policy, &trustData);
		CryptCATAdminReleaseCatalogContext(catalogAdmin, catalogContext, 0);
		CryptCATAdminReleaseContext(catalogAdmin, 0);
		if (ownsFile)
			CloseHandle(file);
		return result.trusted;
	}
}

bool xucrun::authenticode::Verify(const std::wstring& filePath, HANDLE file,
	VerificationResult& result)
{
	result = {};
	WINTRUST_FILE_INFO fileInfo = {};
	fileInfo.cbStruct = sizeof(fileInfo);
	fileInfo.pcwszFilePath = filePath.c_str();
	fileInfo.hFile = file && file != INVALID_HANDLE_VALUE ? file : nullptr;

	WINTRUST_DATA trustData = {};
	trustData.cbStruct = sizeof(trustData);
	trustData.dwUIChoice = WTD_UI_NONE;
	trustData.fdwRevocationChecks = WTD_REVOKE_NONE;
	trustData.dwUnionChoice = WTD_CHOICE_FILE;
	trustData.pFile = &fileInfo;
	trustData.dwStateAction = WTD_STATEACTION_VERIFY;
	trustData.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL | WTD_SAFER_FLAG;

	GUID policy = WINTRUST_ACTION_GENERIC_VERIFY_V2;
	result.trustStatus = WinVerifyTrust(nullptr, &policy, &trustData);
	result.trusted = result.trustStatus == ERROR_SUCCESS;
	if (result.trusted)
		ReadSigner(trustData, result);

	trustData.dwStateAction = WTD_STATEACTION_CLOSE;
	WinVerifyTrust(nullptr, &policy, &trustData);
	if (result.trustStatus == TRUST_E_NOSIGNATURE)
		return VerifyCatalog(filePath, file, result);
	return result.trusted;
}
