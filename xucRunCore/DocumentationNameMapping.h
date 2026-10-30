// Purpose: Defines optional display-only account and computer name mappings for documentation screenshots.
// Approach: Exposes the declarations, constants, and types consumed by the components that provide this behavior.
// Behavior: Loads an explicitly placed sidecar file and never changes operational account identities.
#pragma once

#include <map>
#include <string>

namespace xucrun::documentation_names
{
	struct CaseInsensitiveLess
	{
		bool operator()(const std::wstring& left, const std::wstring& right) const;
	};

	class NameMapping
	{
	public:
		bool LoadFromFile(const std::wstring& path, std::wstring& error);
		bool LoadFromTrustedFile(const std::wstring& path, std::wstring& error);
		bool IsActive() const;
		std::wstring DisplayAccount(const std::wstring& accountName) const;
		std::wstring DisplayComputer(const std::wstring& computerName) const;
		std::wstring OperationalAccount(const std::wstring& displayedAccountName) const;

	private:
		using Mapping = std::map<std::wstring, std::wstring, CaseInsensitiveLess>;

		bool LoadFromContents(const std::wstring& contents, std::wstring& error);
		Mapping computers_;
		Mapping accounts_;
		Mapping operationalAccounts_;
		bool active_ = false;
	};

	bool Initialize(std::wstring& error);
	bool IsActive();
	std::wstring DisplayAccount(const std::wstring& accountName);
	std::wstring DisplayComputer(const std::wstring& computerName);
	std::wstring OperationalAccount(const std::wstring& displayedAccountName);
}
