// Purpose: Loads configured application language resources and resolves localized strings.
#include "Localization.h"

#include <Windows.h>
#include <wintrust.h>

#include <vector>

#include "ClientImageTrust.h"
#include "ClientUiCommon.h"
#include "ClientResourceIds.h"

namespace
{
	bool g_useGermanResources = false;
	HMODULE g_languageModule = nullptr;
	xucrun::localization::Component g_component =
		xucrun::localization::Component::Core;

	LANGID GetPreferredResourceLanguage()
	{
		return g_useGermanResources
			? MAKELANGID(LANG_GERMAN, SUBLANG_GERMAN)
			: MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US);
	}

	std::wstring LoadStringForLanguage(
		HMODULE module, UINT stringId, LANGID language)
	{
		const UINT blockId = stringId / 16 + 1;
		const UINT index = stringId % 16;
		HRSRC resource = FindResourceExW(module, RT_STRING,
			MAKEINTRESOURCEW(blockId), language);
		if (!resource)
			return {};

		HGLOBAL loadedResource = LoadResource(module, resource);
		const DWORD resourceSize = SizeofResource(module, resource);
		const auto* cursor = static_cast<const wchar_t*>(LockResource(loadedResource));
		const auto* end = cursor + resourceSize / sizeof(wchar_t);
		if (!cursor)
			return {};

		for (UINT current = 0; current < index; ++current)
		{
			if (cursor >= end)
				return {};
			const WORD length = *cursor++;
			if (length > static_cast<size_t>(end - cursor))
				return {};
			cursor += length;
		}
		if (cursor >= end)
			return {};
		const WORD length = *cursor++;
		if (length > static_cast<size_t>(end - cursor))
			return {};
		return std::wstring(cursor, length);
	}

	std::wstring GetLanguagePath(bool useGermanResources, DWORD& windowsError)
	{
		wchar_t programFiles[MAX_PATH] = {};
		const DWORD pathLength = GetEnvironmentVariableW(
			L"ProgramFiles", programFiles, ARRAYSIZE(programFiles));
		if (pathLength == 0 || pathLength >= ARRAYSIZE(programFiles))
		{
			windowsError = pathLength == 0 ? GetLastError() : ERROR_INSUFFICIENT_BUFFER;
			if (windowsError == ERROR_SUCCESS)
				windowsError = ERROR_PATH_NOT_FOUND;
			return {};
		}
		windowsError = ERROR_SUCCESS;
		const wchar_t* resourceName =
			g_component == xucrun::localization::Component::Tray
			? L"xucRunTray." : L"xucRun.";
		return std::wstring(programFiles) + L"\\xucRun\\Languages\\" +
			resourceName +
			(useGermanResources ? L"de-DE.dll" : L"en-US.dll");
	}

#ifndef _DEBUG
	bool HasTrustedSignature(const std::wstring& path)
	{
		wchar_t executablePath[32768] = {};
		const DWORD executablePathLength = GetModuleFileNameW(
			nullptr, executablePath, ARRAYSIZE(executablePath));
		if (executablePathLength == 0 ||
			executablePathLength >= ARRAYSIZE(executablePath))
		{
			return false;
		}
		LONG trustStatus = ERROR_SUCCESS;
		std::vector<BYTE> executableSigner;
		std::vector<BYTE> languageSigner;
		return xucrun::image_trust::VerifyWithSignerCertificate(
			executablePath, trustStatus, executableSigner) &&
			xucrun::image_trust::VerifyWithSignerCertificate(
				path, trustStatus, languageSigner) &&
			executableSigner == languageSigner;
	}
#endif
}

namespace xucrun::localization
{
	// Loads the configured external language resource before first UI output.
	bool Initialize(std::wstring& errorMessage, Component component)
	{
		errorMessage.clear();
		if (g_languageModule)
			return g_component == component;
		g_component = component;

		wchar_t programFiles[MAX_PATH] = {};
		const DWORD pathLength = GetEnvironmentVariableW(
			L"ProgramFiles", programFiles, ARRAYSIZE(programFiles));
		if (pathLength == 0 || pathLength >= ARRAYSIZE(programFiles))
		{
			errorMessage = L"The ProgramFiles directory could not be determined.";
			return false;
		}

		const std::wstring configPath = std::wstring(programFiles) +
			L"\\xucRun\\xucRun.ini";
		const DWORD configAttributes = GetFileAttributesW(configPath.c_str());
		if (configAttributes == INVALID_FILE_ATTRIBUTES ||
			(configAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
		{
			const DWORD error = configAttributes == INVALID_FILE_ATTRIBUTES
				? GetLastError()
				: ERROR_DIRECTORY;
			errorMessage = L"The xucRun configuration file is missing or is not a "
				L"regular file.\n\nFile: " + configPath +
				L"\nError: " + std::to_wstring(error);
			return false;
		}

		wchar_t language[16] = {};
		GetPrivateProfileStringW(L"xucRun", L"Language", L"",
			language, ARRAYSIZE(language), configPath.c_str());
		if (language[0] == L'\0')
			g_useGermanResources = false;
		else if (_wcsicmp(language, L"de") == 0)
			g_useGermanResources = true;
		else if (_wcsicmp(language, L"en") == 0)
			g_useGermanResources = false;
		else
		{
			errorMessage = L"The configured UI language is invalid. Expected 'de' or "
				L"'en'.\n\nConfiguration file: " + configPath;
			return false;
		}

		DWORD languagePathError = NO_ERROR;
		const std::wstring languagePath =
			GetLanguagePath(g_useGermanResources, languagePathError);
#ifndef _DEBUG
		if (!HasTrustedSignature(languagePath))
		{
			errorMessage = L"The language resource is missing, is not trusted, or was not "
				L"signed with the same certificate as xucRun.exe.\n\nFile: " + languagePath;
			return false;
		}
#endif
		g_languageModule = LoadLibraryExW(languagePath.c_str(), nullptr,
			LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
		if (!g_languageModule)
		{
			errorMessage = L"The language resource could not be loaded.\n\nFile: " +
				languagePath + L"\nError: " + std::to_wstring(GetLastError());
			return false;
		}
		return true;
	}

	bool ValidateLanguageResourceForSwitch(
		const wchar_t* language, DWORD& windowsError)
	{
		windowsError = ERROR_SUCCESS;
		if (!g_languageModule || !language)
		{
			windowsError = ERROR_INVALID_STATE;
			return false;
		}
		const bool useGermanResources = _wcsicmp(language, L"de") == 0;
		if (!useGermanResources && _wcsicmp(language, L"en") != 0)
		{
			windowsError = ERROR_INVALID_PARAMETER;
			return false;
		}

		const std::wstring languagePath =
			GetLanguagePath(useGermanResources, windowsError);
		if (languagePath.empty())
			return false;
		if (GetFileAttributesW(languagePath.c_str()) == INVALID_FILE_ATTRIBUTES)
		{
			windowsError = GetLastError();
			return false;
		}

#ifndef _DEBUG
		wchar_t executablePath[32768] = {};
		const DWORD executablePathLength = GetModuleFileNameW(
			nullptr, executablePath, ARRAYSIZE(executablePath));
		if (executablePathLength == 0 ||
			executablePathLength >= ARRAYSIZE(executablePath))
		{
			windowsError = executablePathLength == 0
				? GetLastError() : ERROR_INSUFFICIENT_BUFFER;
			return false;
		}

		LONG trustStatus = ERROR_SUCCESS;
		std::vector<BYTE> executableSigner;
		std::vector<BYTE> languageSigner;
		if (!image_trust::VerifyWithSignerCertificate(
			executablePath, trustStatus, executableSigner) ||
			!image_trust::VerifyWithSignerCertificate(
				languagePath, trustStatus, languageSigner))
		{
			windowsError = static_cast<DWORD>(trustStatus);
			return false;
		}
		if (executableSigner != languageSigner)
		{
			windowsError = static_cast<DWORD>(TRUST_E_SUBJECT_NOT_TRUSTED);
			return false;
		}
#endif

		HMODULE candidateModule = LoadLibraryExW(languagePath.c_str(), nullptr,
			LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
		if (!candidateModule)
		{
			windowsError = GetLastError();
			return false;
		}

		const LANGID currentLanguage = GetPreferredResourceLanguage();
		const LANGID candidateLanguage = useGermanResources
			? MAKELANGID(LANG_GERMAN, SUBLANG_GERMAN)
			: MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US);
		bool complete = true;
		for (UINT resourceId = IDS_UI_ERROR_TITLE;
			resourceId <= IDS_LAST_LOCALIZED_STRING; ++resourceId)
		{
			if (!LoadStringForLanguage(
				g_languageModule, resourceId, currentLanguage).empty() &&
				LoadStringForLanguage(
					candidateModule, resourceId, candidateLanguage).empty())
			{
				complete = false;
				break;
			}
		}
		FreeLibrary(candidateModule);
		if (!complete)
		{
			windowsError = ERROR_RESOURCE_DATA_NOT_FOUND;
			return false;
		}
		return true;
	}

	bool UsesGermanResources()
	{
		return PRIMARYLANGID(GetPreferredResourceLanguage()) == LANG_GERMAN;
	}

	std::wstring GetTextByResourceId(unsigned int resourceId)
	{
		std::wstring text = LoadStringForLanguage(
			g_languageModule, static_cast<UINT>(resourceId),
			GetPreferredResourceLanguage());
		if (!text.empty())
			return text;

		ui::ShowMessageBox(nullptr, L"The selected language resource is incomplete.",
			L"xucRun - Localization error", MB_OK | MB_ICONERROR);
		ExitProcess(ERROR_RESOURCE_DATA_NOT_FOUND);
	}

	std::wstring FormatResource(unsigned int resourceId,
		std::initializer_list<std::wstring> arguments)
	{
		std::wstring result = GetTextByResourceId(resourceId);
		size_t index = 0;
		for (const std::wstring& argument : arguments)
		{
			const std::wstring placeholder = L"{" + std::to_wstring(index++) + L"}";
			size_t position = 0;
			while ((position = result.find(placeholder, position)) != std::wstring::npos)
			{
				result.replace(position, placeholder.size(), argument);
				position += argument.size();
			}
		}
		return result;
	}

	std::wstring GetText(TextId id)
	{
		return GetTextByResourceId(static_cast<unsigned int>(id));
	}

	std::wstring Format(TextId id, std::initializer_list<std::wstring> arguments)
	{
		return FormatResource(static_cast<unsigned int>(id), arguments);
	}
}
