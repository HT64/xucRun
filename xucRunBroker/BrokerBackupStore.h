// Purpose: Declares protected backup and restore operations for broker-managed product data.
// Approach: Exposes the declarations, constants, and types consumed by the components that provide this behavior.
#pragma once

#include <Windows.h>

#include <string>
#include <vector>

#include "BrokerContracts.h"

namespace xucrun::backup_store
{
	bool RunSecuritySelfTest(DWORD& error);
	bool Create(const std::wstring& description,
		xucrun::broker::BackupEntry& entry, DWORD& error);
	bool List(std::vector<xucrun::broker::BackupEntry>& entries, DWORD& error);
	bool Restore(const std::wstring& id, bool createFallbackBackup,
		std::wstring& fallbackBackupId, DWORD& error);
	bool RecoverPendingRestore(bool& recovered, DWORD& error);
	bool Delete(const std::wstring& id, DWORD& error);
	bool DeleteAll(DWORD& error);
}
