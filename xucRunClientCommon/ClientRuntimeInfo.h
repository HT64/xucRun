// Purpose: Defines the shared runtime information displayed by xucRun clients.
// Approach: Exposes one collector and formatter used by Core and Tray information commands.
#pragma once

#include <string>

namespace xucrun::runtime_info
{
	struct RuntimeInfo
	{
		std::wstring version;
		std::wstring copyright;
		std::wstring executablePath;
		std::wstring architecture;
		std::wstring language;
		std::wstring languageResourcePath;
		std::wstring signerSubject;
		std::wstring signerSha256Fingerprint;
		std::wstring signerValidFrom;
		std::wstring signerValidUntil;
		std::wstring account;
		std::wstring sid;
		std::wstring elevation;
		std::wstring configurationPath;
		std::wstring clientLogging;
		std::wstring maintenanceScript;
	};

	RuntimeInfo Collect(const std::wstring& maintenanceScriptPath = {});
	std::wstring Format(const RuntimeInfo& info);
}
