// Purpose: Collects and formats runtime information shared by xucRun clients.
// Approach: Reads process, resource, signer, account, and configuration data through common interfaces.
#include "ClientRuntimeInfo.h"

#include <Windows.h>

#include <string>
#include <vector>

#include "BuildVersion.h"
#include "ClientAccountIdentity.h"
#include "ClientImageTrust.h"
#include "DocumentationNameMapping.h"
#include "Localization.h"

namespace
{
	std::wstring GetExecutablePath()
	{
		std::vector<wchar_t> buffer(512);
		for (;;)
		{
			const DWORD length = GetModuleFileNameW(
				nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
			if (length == 0)
				return {};
			if (length < buffer.size() - 1)
				return std::wstring(buffer.data(), length);
			buffer.resize(buffer.size() * 2);
		}
	}

	std::wstring GetVersionResourceString(const std::wstring& path,
		const wchar_t* valueName)
	{
		DWORD ignored = 0;
		const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &ignored);
		if (size == 0)
			return {};
		std::vector<BYTE> versionInfo(size);
		if (!GetFileVersionInfoW(path.c_str(), 0, size, versionInfo.data()))
			return {};

		struct LanguageAndCodePage
		{
			WORD language;
			WORD codePage;
		};
		LanguageAndCodePage* translations = nullptr;
		UINT translationsSize = 0;
		if (!VerQueryValueW(versionInfo.data(), L"\\VarFileInfo\\Translation",
			reinterpret_cast<void**>(&translations), &translationsSize))
		{
			return {};
		}

		const UINT translationCount =
			translationsSize / sizeof(LanguageAndCodePage);
		for (UINT index = 0; index < translationCount; ++index)
		{
			wchar_t query[128] = {};
			if (swprintf_s(query,
				L"\\StringFileInfo\\%04x%04x\\%s",
				translations[index].language,
				translations[index].codePage,
				valueName) < 0)
			{
				continue;
			}
			wchar_t* value = nullptr;
			UINT valueLength = 0;
			if (VerQueryValueW(versionInfo.data(), query,
				reinterpret_cast<void**>(&value), &valueLength) &&
				value && valueLength > 1)
			{
				return value;
			}
		}
		return {};
	}

	std::wstring GetConfigurationPath()
	{
		wchar_t programFiles[MAX_PATH] = {};
		const DWORD length = GetEnvironmentVariableW(
			L"ProgramFiles", programFiles, ARRAYSIZE(programFiles));
		if (length == 0 || length >= ARRAYSIZE(programFiles))
			return {};
		return std::wstring(programFiles) + L"\\xucRun\\xucRun.ini";
	}

	std::wstring GetAccount()
	{
		std::wstring username;
		std::wstring domain;
		if (!xucrun::account_identity::GetCurrentUserInfo(username, domain))
			return {};
		const std::wstring account =
			domain.empty() ? username : domain + L"\\" + username;
		return xucrun::documentation_names::DisplayAccount(account);
	}

	std::wstring GetElevation()
	{
		HANDLE token = nullptr;
		if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
			return {};
		TOKEN_ELEVATION elevation = {};
		DWORD returnedLength = 0;
		const bool available = GetTokenInformation(token, TokenElevation,
			&elevation, sizeof(elevation), &returnedLength) != FALSE;
		CloseHandle(token);
		if (!available)
			return {};
		return xucrun::localization::GetText(elevation.TokenIsElevated
			? xucrun::localization::TextId::InfoElevated
			: xucrun::localization::TextId::InfoNotElevated);
	}

	std::wstring ValueOrUnavailable(const std::wstring& value)
	{
		return value.empty()
			? xucrun::localization::GetText(
				xucrun::localization::TextId::ValueNotAvailable)
			: value;
	}
}

namespace xucrun::runtime_info
{
	RuntimeInfo Collect(const std::wstring& maintenanceScriptPath)
	{
		RuntimeInfo info;
		info.version = std::to_wstring(VERSION_MAJOR) + L"." +
			std::to_wstring(VERSION_MINOR) + L"." +
			std::to_wstring(VERSION_PATCH) + L"." +
			std::to_wstring(BUILD_NUMBER);
		info.executablePath = ValueOrUnavailable(GetExecutablePath());
		info.copyright = info.executablePath ==
			localization::GetText(localization::TextId::ValueNotAvailable)
			? info.executablePath
			: ValueOrUnavailable(GetVersionResourceString(
				info.executablePath, L"LegalCopyright"));
		info.architecture = L"x64";
		info.language = localization::GetText(localization::UsesGermanResources()
			? localization::TextId::InfoLanguageGerman
			: localization::TextId::InfoLanguageEnglish);
		if (localization::UsesAutomaticLanguage())
		{
			info.language += localization::GetText(
				localization::TextId::InfoLanguageAutomaticSuffix);
		}
		info.languageResourcePath =
			ValueOrUnavailable(localization::GetLoadedLanguageResourcePath());

		LONG signerTrustStatus = ERROR_SUCCESS;
		std::vector<BYTE> signerCertificate;
		image_trust::SignerCertificateInfo signerInfo;
		if (info.executablePath !=
			localization::GetText(localization::TextId::ValueNotAvailable) &&
			image_trust::VerifyWithSignerCertificate(
				info.executablePath, signerTrustStatus, signerCertificate) &&
			image_trust::GetSignerCertificateInfo(signerCertificate, signerInfo))
		{
			info.signerSubject =
				documentation_names::DisplayCertificateSubject(signerInfo.subject);
			info.signerSha256Fingerprint = signerInfo.sha256Fingerprint;
			info.signerValidFrom = signerInfo.validFrom;
			info.signerValidUntil = signerInfo.validUntil;
		}
		info.signerSubject = ValueOrUnavailable(info.signerSubject);
		info.signerSha256Fingerprint =
			ValueOrUnavailable(info.signerSha256Fingerprint);
		info.signerValidFrom = ValueOrUnavailable(info.signerValidFrom);
		info.signerValidUntil = ValueOrUnavailable(info.signerValidUntil);

		info.account = ValueOrUnavailable(GetAccount());
		if (!account_identity::GetCurrentUserSID(info.sid))
			info.sid.clear();
		info.sid = ValueOrUnavailable(info.sid);
		info.elevation = ValueOrUnavailable(GetElevation());
		info.configurationPath = ValueOrUnavailable(GetConfigurationPath());

		wchar_t configuredLogDirectory[32768] = {};
		const std::wstring unavailable =
			localization::GetText(localization::TextId::ValueNotAvailable);
		if (info.configurationPath != unavailable)
		{
			GetPrivateProfileStringW(L"Logging", L"ClientLogDirectory", L"",
				configuredLogDirectory, ARRAYSIZE(configuredLogDirectory),
				info.configurationPath.c_str());
		}
		info.clientLogging = configuredLogDirectory[0]
			? configuredLogDirectory
			: localization::GetText(localization::TextId::StatusDisabled);
		if (!maintenanceScriptPath.empty())
		{
			info.maintenanceScript = maintenanceScriptPath;
		}
		else if (info.configurationPath != unavailable)
		{
			const std::size_t separator =
				info.configurationPath.find_last_of(L"\\/");
			if (separator != std::wstring::npos)
			{
				info.maintenanceScript =
					info.configurationPath.substr(0, separator + 1) +
					L"Install-xucRun.ps1";
			}
		}
		info.maintenanceScript = ValueOrUnavailable(info.maintenanceScript);
		return info;
	}

	std::wstring Format(const RuntimeInfo& info)
	{
		return localization::Format(localization::TextId::InfoCommonContent,
			{ info.version, info.copyright, info.executablePath,
				info.architecture, info.language, info.languageResourcePath,
				info.signerSubject, info.signerSha256Fingerprint,
				info.signerValidFrom, info.signerValidUntil,
				info.account, info.sid, info.elevation,
				info.configurationPath, info.clientLogging,
				info.maintenanceScript });
	}
}
