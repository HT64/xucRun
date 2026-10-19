// Purpose: Checks Authenticode trust and publisher identity of executable files.
// Behavior: Uses WinTrust, cryptographic catalogs, and Windows certificate metadata.
#include "ClientImageTrust.h"

#include <Softpub.h>
#include <mscat.h>
#include <wincrypt.h>
#include <wintrust.h>

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

	void ReadMicrosoftPublisher(const WINTRUST_DATA& trustData,
		bool& microsoftPublisher)
	{
		microsoftPublisher = false;
		PCCERT_CONTEXT certificate = GetSignerCertificate(trustData);
		if (!certificate)
			return;

		wchar_t organization[256] = {};
		const DWORD organizationLength = CertGetNameStringW(
			certificate, CERT_NAME_ATTR_TYPE, 0,
			const_cast<char*>(szOID_ORGANIZATION_NAME), organization,
			static_cast<DWORD>(std::size(organization)));
		microsoftPublisher = organizationLength > 1 &&
			_wcsicmp(organization, L"Microsoft Corporation") == 0;
	}

	void ReadSignerCertificate(const WINTRUST_DATA& trustData,
		std::vector<BYTE>* signerCertificate)
	{
		if (!signerCertificate)
			return;
		signerCertificate->clear();
		PCCERT_CONTEXT certificate = GetSignerCertificate(trustData);
		if (certificate)
		{
			signerCertificate->assign(
				certificate->pbCertEncoded,
				certificate->pbCertEncoded + certificate->cbCertEncoded);
		}
	}

	bool VerifyCatalog(const std::wstring& imagePath, LONG& trustStatus,
		bool& microsoftPublisher, std::vector<BYTE>* signerCertificate)
	{
		HANDLE file = CreateFileW(imagePath.c_str(), GENERIC_READ,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
			OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE)
			return false;

		HCATADMIN catalogAdmin = nullptr;
		if (!CryptCATAdminAcquireContext2(&catalogAdmin, nullptr, nullptr, nullptr, 0))
		{
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
			CloseHandle(file);
			return false;
		}
		hash.resize(hashSize);

		HCATINFO catalogContext = CryptCATAdminEnumCatalogFromHash(
			catalogAdmin, hash.data(), static_cast<DWORD>(hash.size()), 0, nullptr);
		if (!catalogContext)
		{
			CryptCATAdminReleaseContext(catalogAdmin, 0);
			CloseHandle(file);
			return false;
		}

		CATALOG_INFO catalogInfo = {};
		catalogInfo.cbStruct = sizeof(catalogInfo);
		if (!CryptCATCatalogInfoFromContext(catalogContext, &catalogInfo, 0))
		{
			CryptCATAdminReleaseCatalogContext(catalogAdmin, catalogContext, 0);
			CryptCATAdminReleaseContext(catalogAdmin, 0);
			CloseHandle(file);
			return false;
		}

		static constexpr wchar_t hexDigits[] = L"0123456789ABCDEF";
		std::wstring memberTag;
		memberTag.reserve(hash.size() * 2);
		for (BYTE value : hash)
		{
			memberTag += hexDigits[value >> 4];
			memberTag += hexDigits[value & 0x0F];
		}

		WINTRUST_CATALOG_INFO catalog = {};
		catalog.cbStruct = sizeof(catalog);
		catalog.pcwszCatalogFilePath = catalogInfo.wszCatalogFile;
		catalog.pcwszMemberTag = memberTag.c_str();
		catalog.pcwszMemberFilePath = imagePath.c_str();
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
		trustStatus = WinVerifyTrust(nullptr, &policy, &trustData);
		if (trustStatus == ERROR_SUCCESS)
		{
			ReadMicrosoftPublisher(trustData, microsoftPublisher);
			ReadSignerCertificate(trustData, signerCertificate);
		}

		trustData.dwStateAction = WTD_STATEACTION_CLOSE;
		WinVerifyTrust(nullptr, &policy, &trustData);
		CryptCATAdminReleaseCatalogContext(catalogAdmin, catalogContext, 0);
		CryptCATAdminReleaseContext(catalogAdmin, 0);
		CloseHandle(file);
		return trustStatus == ERROR_SUCCESS;
	}

	bool VerifyInternal(const std::wstring& imagePath, LONG& trustStatus,
		bool& microsoftPublisher, std::vector<BYTE>* signerCertificate)
	{
		microsoftPublisher = false;
		if (signerCertificate)
			signerCertificate->clear();
		WINTRUST_FILE_INFO fileInfo = {};
		fileInfo.cbStruct = sizeof(fileInfo);
		fileInfo.pcwszFilePath = imagePath.c_str();

		WINTRUST_DATA trustData = {};
		trustData.cbStruct = sizeof(trustData);
		trustData.dwUIChoice = WTD_UI_NONE;
		trustData.fdwRevocationChecks = WTD_REVOKE_NONE;
		trustData.dwUnionChoice = WTD_CHOICE_FILE;
		trustData.pFile = &fileInfo;
		trustData.dwStateAction = WTD_STATEACTION_VERIFY;
		trustData.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL | WTD_SAFER_FLAG;

		GUID policy = WINTRUST_ACTION_GENERIC_VERIFY_V2;
		trustStatus = WinVerifyTrust(nullptr, &policy, &trustData);
		if (trustStatus == ERROR_SUCCESS)
		{
			ReadMicrosoftPublisher(trustData, microsoftPublisher);
			ReadSignerCertificate(trustData, signerCertificate);
		}

		trustData.dwStateAction = WTD_STATEACTION_CLOSE;
		WinVerifyTrust(nullptr, &policy, &trustData);
		if (trustStatus == TRUST_E_NOSIGNATURE)
		{
			return VerifyCatalog(
				imagePath, trustStatus, microsoftPublisher, signerCertificate);
		}
		return trustStatus == ERROR_SUCCESS;
	}
}

namespace xucrun::image_trust
{
	bool Verify(const std::wstring& imagePath, LONG& trustStatus,
		bool& microsoftPublisher)
	{
		return VerifyInternal(
			imagePath, trustStatus, microsoftPublisher, nullptr);
	}

	bool VerifyWithSignerCertificate(const std::wstring& imagePath, LONG& trustStatus,
		std::vector<BYTE>& signerCertificate)
	{
		bool microsoftPublisher = false;
		return VerifyInternal(
			imagePath, trustStatus, microsoftPublisher, &signerCertificate) &&
			!signerCertificate.empty();
	}
}