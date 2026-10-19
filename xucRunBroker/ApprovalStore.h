// Purpose: Declares the protected broker store for time- and program-bound approvals.
// Provides reading, writing, listing, deletion, and controlled path updates.
#pragma once

#include "ApprovalSerialization.h"

#include <Windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace xucrun::approval_store
{
	inline constexpr std::size_t MaximumApprovalCount = 256;

	struct ProgramUpdateInfo
	{
		bool available = false;
		bool affectsAllAccounts = false;
		std::wstring changedPath;
		xucrun::approval::Sha256Hash oldHash = {};
		xucrun::approval::Sha256Hash newHash = {};
		std::uint32_t affectedApprovalCount = 0;
		std::uint32_t affectedAccountCount = 0;
	};

	bool Write(const xucrun::approval::BrokerApprovalValue& approval, DWORD& error);
	bool DeleteExact(const xucrun::approval::BrokerApprovalValue& approval, DWORD& error);
	bool DeleteListed(const std::wstring& ownerUserSid,
		const xucrun::approval::BrokerApprovalValue& approval,
		bool& deleted, DWORD& error);
	bool DeleteForAccount(const std::wstring& ownerUserSid,
		const std::wstring& targetAccountSid, std::uint32_t& deletedCount,
		DWORD& error);
	bool List(const std::wstring& ownerUserSid, const std::wstring& currentLogonSid,
		std::vector<xucrun::approval::BrokerApprovalValue>& approvals, DWORD& error);
	bool ListAllForAdministration(
		std::vector<xucrun::approval::BrokerApprovalValue>& approvals, DWORD& error);
	bool HasMatching(const xucrun::approval::BrokerApprovalValue& context,
		std::uint64_t now, bool& matches, DWORD& error);
	bool RunApprovalMatchSelfTest();
	bool FindProgramUpdate(const xucrun::approval::BrokerApprovalValue& context,
		std::uint64_t now, ProgramUpdateInfo& update, DWORD& error);
	bool ApplyProgramUpdate(const xucrun::approval::BrokerApprovalValue& context,
		const ProgramUpdateInfo& update, bool allAccounts,
		std::uint32_t& updatedCount, DWORD& error);
}