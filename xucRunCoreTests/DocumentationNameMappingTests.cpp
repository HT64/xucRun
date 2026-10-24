// Purpose: Tests strict loading and reversible account mappings for documentation screenshots.
#include "DocumentationNameMapping.h"

#include <Windows.h>

#include <filesystem>
#include <fstream>
#include <iostream>

namespace
{
	int failures = 0;

	std::filesystem::path WriteConfiguration(const char* contents)
	{
		wchar_t temporaryPath[MAX_PATH] = {};
		wchar_t temporaryFile[MAX_PATH] = {};
		if (GetTempPathW(MAX_PATH, temporaryPath) == 0 ||
			GetTempFileNameW(temporaryPath, L"xnm", 0, temporaryFile) == 0)
		{
			return {};
		}
		std::ofstream file(temporaryFile, std::ios::binary | std::ios::trunc);
		file << contents;
		return temporaryFile;
	}

	void ExpectValidConfiguration()
	{
		const std::filesystem::path path = WriteConfiguration(
			"\xef\xbb\xbf# documentation names\n"
			"[Computers]\n"
			"REAL-PC=PC-DEMO\n"
			"[Accounts]\n"
			"REAL-PC\\Administrator=PC-DEMO\\Admin\n"
			"REAL-PC\\Harald=PC-DEMO\\User01\n");
		xucrun::documentation_names::NameMapping mapping;
		std::wstring error;
		const bool loaded = !path.empty() &&
			mapping.LoadFromFile(path.wstring(), error);
		DeleteFileW(path.c_str());
		if (!loaded || !mapping.IsActive() ||
			mapping.DisplayComputer(L"real-pc") != L"PC-DEMO" ||
			mapping.DisplayAccount(L"real-pc\\administrator") != L"PC-DEMO\\Admin" ||
			mapping.DisplayAccount(L"OTHER\\User") != L"[unmapped account]" ||
			mapping.DisplayComputer(L"OTHER-PC") != L"[unmapped computer]" ||
			mapping.OperationalAccount(L"pc-demo\\admin") !=
				L"REAL-PC\\Administrator" ||
			mapping.OperationalAccount(L"OTHER\\User") != L"OTHER\\User")
		{
			std::cerr << "valid documentation name mapping failed\n";
			failures++;
		}
	}

	void ExpectRejectedConfiguration(const char* contents, const char* name)
	{
		const std::filesystem::path path = WriteConfiguration(contents);
		xucrun::documentation_names::NameMapping mapping;
		std::wstring error;
		const bool loaded = !path.empty() &&
			mapping.LoadFromFile(path.wstring(), error);
		DeleteFileW(path.c_str());
		if (loaded || error.empty() || mapping.IsActive())
		{
			std::cerr << "invalid documentation mapping accepted: " << name << "\n";
			failures++;
		}
	}

	void ExpectUntrustedConfigurationRejected()
	{
		const std::filesystem::path path = WriteConfiguration(
			"[Computers]\nREAL-PC=PC-DEMO\n"
			"[Accounts]\nREAL-PC\\User=PC-DEMO\\User\n");
		xucrun::documentation_names::NameMapping mapping;
		std::wstring error;
		const bool loaded = !path.empty() &&
			mapping.LoadFromTrustedFile(path.wstring(), error);
		DeleteFileW(path.c_str());
		if (loaded || error.empty() || mapping.IsActive())
		{
			std::cerr << "untrusted documentation mapping accepted\n";
			failures++;
		}
	}
}

int RunDocumentationNameMappingTests()
{
	ExpectValidConfiguration();
	ExpectRejectedConfiguration("", "empty");
	ExpectRejectedConfiguration("[Unknown]\na=b\n", "unknown section");
	ExpectRejectedConfiguration(
		"[Accounts]\nREAL\\One=DEMO\\One\n", "missing computers");
	ExpectRejectedConfiguration(
		"[Accounts]\nREAL\\One=DEMO\\User\nREAL\\Two=DEMO\\User\n",
		"ambiguous reverse account");
	ExpectRejectedConfiguration(
		"[Accounts]\nREAL\\One=DEMO\\One\nreal\\one=DEMO\\Two\n",
		"duplicate source account");
	ExpectUntrustedConfigurationRejected();
	return failures;
}
