// Purpose: Persists and validates broker approvals in the LocalSystem credential store.
// Binds entries to owner, account, session, target identity, and validity period.
#include "ApprovalStore.h"

#include "BrokerTargetNaming.h"
#include "CredentialStore.h"
#include "TargetNaming.h"

#include <bcrypt.h>

#include <algorithm>
#include <set>

namespace
{
	constexpr std::size_t MaximumApprovalValueSize = 4096;

	// Determines the machine-bound name prefix for stored approvals.
	bool GetApprovalTargetPrefix(std::wstring& prefix, DWORD& error)
	{
		return xucrun::broker_targets::GetCurrentApprovalPrefix(prefix, error);
	}

	// Appends a single field byte to the canonical target binding.
	void AppendByte(std::vector<std::uint8_t>& value, std::uint8_t byte)
	{
		value.push_back(byte);
	}

	// Appends a complete hash to the canonical target binding.
	void AppendHash(std::vector<std::uint8_t>& value,
		const xucrun::approval::Sha256Hash& hash)
	{
		value.insert(value.end(), hash.begin(), hash.end());
	}

	// Appends a null-terminated UTF-16 string to the canonical target binding.
	void AppendString(std::vector<std::uint8_t>& value, const std::wstring& text)
	{
		const auto* bytes = reinterpret_cast<const std::uint8_t*>(text.data());
		value.insert(value.end(), bytes, bytes + text.size() * sizeof(wchar_t));
		value.push_back(0);
		value.push_back(0);
	}

	// Derives the storage key from all security-relevant approval bindings.
	bool BuildTarget(const xucrun::approval::BrokerApprovalValue& approval,
		std::wstring& target, DWORD& error)
	{
		std::wstring prefix;
		if (!GetApprovalTargetPrefix(prefix, error))
			return false;
		std::vector<std::uint8_t> binding;
		AppendByte(binding, static_cast<std::uint8_t>(approval.programScope));
		AppendByte(binding, static_cast<std::uint8_t>(approval.accountScope));
		AppendHash(binding, approval.targetHash);
		if (approval.programScope ==
			xucrun::approval::BrokerApprovalProgramScope::ExactInvocation)
		{
			AppendByte(binding, approval.visibility);
			AppendHash(binding, approval.argumentHash);
			AppendHash(binding, approval.scriptHash);
		}
		AppendString(binding, approval.ownerUserSid);
		AppendString(binding, approval.targetAccountSid);
		AppendString(binding, approval.targetPath);
		if (approval.programScope ==
			xucrun::approval::BrokerApprovalProgramScope::ExactInvocation)
		{
			AppendString(binding, approval.workingDirectory);
			AppendString(binding, approval.scriptPath);
		}

		xucrun::approval::Sha256Hash digest = {};
		BCRYPT_ALG_HANDLE algorithm = nullptr;
		BCRYPT_HASH_HANDLE hash = nullptr;
		NTSTATUS status = BCryptOpenAlgorithmProvider(
			&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
		if (status == 0)
			status = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0);
		if (status == 0)
			status = BCryptHashData(hash, binding.data(),
				static_cast<ULONG>(binding.size()), 0);
		if (status == 0)
			status = BCryptFinishHash(hash, digest.data(),
				static_cast<ULONG>(digest.size()), 0);
		if (hash)
			BCryptDestroyHash(hash);
		if (algorithm)
			BCryptCloseAlgorithmProvider(algorithm, 0);
		if (status != 0)
		{
			error = ERROR_GEN_FAILURE;
			return false;
		}
		target = prefix + xucrun::targets::BytesToUpperHex(digest);
		error = NO_ERROR;
		return true;
	}

	// Compares Windows identifiers without regard to case.
	bool EqualInsensitive(const std::wstring& left, const std::wstring& right)
	{
		return _wcsicmp(left.c_str(), right.c_str()) == 0;
	}

	// Returns the current system time as a Windows file time value.
	std::uint64_t GetCurrentFileTimeValue()
	{
		FILETIME fileTime = {};
		GetSystemTimeAsFileTime(&fileTime);
		ULARGE_INTEGER value = {};
		value.LowPart = fileTime.dwLowDateTime;
		value.HighPart = fileTime.dwHighDateTime;
		return value.QuadPart;
	}

	// Validates the owner, account, lifetime, and program binding of a stored approval.
	bool IsMatch(const xucrun::approval::BrokerApprovalValue& stored,
		const xucrun::approval::BrokerApprovalValue& context, std::uint64_t now)
	{
		if (!EqualInsensitive(stored.ownerUserSid, context.ownerUserSid) ||
			stored.elevationMode != context.elevationMode ||
			(stored.accountScope == xucrun::approval::BrokerApprovalAccountScope::CurrentAccount &&
				!EqualInsensitive(stored.targetAccountSid, context.targetAccountSid)) ||
			(stored.lifetime == xucrun::approval::BrokerApprovalLifetime::Until &&
				stored.expiresAt <= now) ||
			(stored.lifetime == xucrun::approval::BrokerApprovalLifetime::LogonSession &&
				!EqualInsensitive(stored.sessionLogonSid, context.sessionLogonSid)))
		{
			return false;
		}
		if (stored.programScope == xucrun::approval::BrokerApprovalProgramScope::General)
			return true;
		if (stored.targetHash != context.targetHash ||
			!EqualInsensitive(stored.targetPath, context.targetPath))
		{
			return false;
		}
		return stored.programScope !=
			xucrun::approval::BrokerApprovalProgramScope::ExactInvocation ||
			(stored.visibility == context.visibility &&
			 stored.argumentHash == context.argumentHash &&
			 stored.scriptHash == context.scriptHash &&
			stored.workingDirectory == context.workingDirectory &&
			 EqualInsensitive(stored.scriptPath, context.scriptPath));
	}

	// Identifies an account-bound approval for the same client profile and target account.
	bool IsForAccount(const xucrun::approval::BrokerApprovalValue& approval,
		const std::wstring& ownerUserSid, const std::wstring& targetAccountSid)
	{
		return approval.accountScope ==
			xucrun::approval::BrokerApprovalAccountScope::CurrentAccount &&
			EqualInsensitive(approval.ownerUserSid, ownerUserSid) &&
			EqualInsensitive(approval.targetAccountSid, targetAccountSid);
	}

	// Determines whether an approval is affected by the reported program update exactly.
	bool IsAffected(const xucrun::approval::BrokerApprovalValue& approval,
		const xucrun::approval_store::ProgramUpdateInfo& update)
	{
		return approval.programScope !=
			xucrun::approval::BrokerApprovalProgramScope::General &&
			EqualInsensitive(approval.targetPath, update.changedPath) &&
			approval.targetHash == update.oldHash;
	}

	// Applies a confirmed path and hash change to an affected approval.
	void ApplyUpdate(xucrun::approval::BrokerApprovalValue& approval,
		const xucrun::approval_store::ProgramUpdateInfo& update)
	{
		if (EqualInsensitive(approval.targetPath, update.changedPath) &&
			approval.targetHash == update.oldHash)
		{
			approval.targetPath = update.changedPath;
			approval.targetHash = update.newHash;
		}
	}
}

namespace xucrun::approval_store
{
	// Serializes and stores an approval under its fully bound target name.
	bool Write(const xucrun::approval::BrokerApprovalValue& approval, DWORD& error)
	{
		const std::vector<std::uint8_t> value =
			xucrun::approval::SerializeBrokerApproval(approval, MaximumApprovalValueSize);
		std::wstring target;
		if (value.empty() || !BuildTarget(approval, target, error))
		{
			if (value.empty())
				error = ERROR_INVALID_DATA;
			return false;
		}
		return xucrun::broker_store::Write(target, value, error);
	}

	// Deletes exactly the approval identified by all binding fields.
	bool DeleteExact(const xucrun::approval::BrokerApprovalValue& approval, DWORD& error)
	{
		std::wstring target;
		return BuildTarget(approval, target, error) &&
			xucrun::broker_store::Delete(target, error);
	}

	// Deletes a listed approval only if the owner SID and content match.
	bool DeleteListed(const std::wstring& ownerUserSid,
		const xucrun::approval::BrokerApprovalValue& approval,
		bool& deleted, DWORD& error)
	{
		deleted = false;
		if (ownerUserSid.empty() ||
			!EqualInsensitive(approval.ownerUserSid, ownerUserSid))
		{
			error = ERROR_ACCESS_DENIED;
			return false;
		}
		const std::vector<std::uint8_t> expected =
			xucrun::approval::SerializeBrokerApproval(
				approval, MaximumApprovalValueSize);
		std::vector<std::wstring> targets;
		std::wstring prefix;
		if (expected.empty() ||
			!GetApprovalTargetPrefix(prefix, error) ||
			!xucrun::broker_store::EnumerateTargets(
				prefix, targets, error))
		{
			if (expected.empty())
				error = ERROR_INVALID_DATA;
			return false;
		}
		for (const std::wstring& target : targets)
		{
			std::vector<std::uint8_t> value;
			if (!xucrun::broker_store::Read(target, value, error))
				return false;
			if (value != expected)
				continue;
			if (!xucrun::broker_store::Delete(target, error) &&
				error != ERROR_NOT_FOUND)
			{
				return false;
			}
			deleted = error != ERROR_NOT_FOUND;
			error = NO_ERROR;
			return true;
		}
		error = NO_ERROR;
		return true;
	}

	// Deletes all account-bound approvals for a target account; profile-wide approvals remain.
	bool DeleteForAccount(const std::wstring& ownerUserSid,
		const std::wstring& targetAccountSid, std::uint32_t& deletedCount,
		DWORD& error)
	{
		deletedCount = 0;
		if (ownerUserSid.empty() || targetAccountSid.empty())
		{
			error = ERROR_INVALID_PARAMETER;
			return false;
		}

		std::vector<std::wstring> targets;
		std::wstring prefix;
		if (!GetApprovalTargetPrefix(prefix, error) ||
			!xucrun::broker_store::EnumerateTargets(prefix, targets, error))
		{
			return false;
		}
		for (const std::wstring& target : targets)
		{
			std::vector<std::uint8_t> value;
			xucrun::approval::BrokerApprovalValue approval;
			if (!xucrun::broker_store::Read(target, value, error) ||
				!xucrun::approval::DeserializeBrokerApproval(value, approval))
			{
				error = ERROR_INVALID_DATA;
				return false;
			}
			if (!IsForAccount(approval, ownerUserSid, targetAccountSid))
				continue;
			if (!xucrun::broker_store::Delete(target, error) &&
				error != ERROR_NOT_FOUND)
			{
				return false;
			}
			if (error != ERROR_NOT_FOUND)
				deletedCount++;
		}
		error = NO_ERROR;
		return true;
	}

	// Lists only valid approvals for the requested owner and current logon.
	bool List(const std::wstring& ownerUserSid, const std::wstring& currentLogonSid,
		std::vector<xucrun::approval::BrokerApprovalValue>& approvals, DWORD& error)
	{
		approvals.clear();
		if (ownerUserSid.empty())
		{
			error = ERROR_INVALID_PARAMETER;
			return false;
		}
		std::vector<std::wstring> targets;
		std::wstring prefix;
		if (!GetApprovalTargetPrefix(prefix, error) ||
			!xucrun::broker_store::EnumerateTargets(prefix, targets, error))
			return false;
		const std::uint64_t now = GetCurrentFileTimeValue();
		for (const std::wstring& target : targets)
		{
			std::vector<std::uint8_t> value;
			xucrun::approval::BrokerApprovalValue approval;
			if (!xucrun::broker_store::Read(target, value, error) ||
				!xucrun::approval::DeserializeBrokerApproval(value, approval))
			{
				error = ERROR_INVALID_DATA;
				return false;
			}
			if (EqualInsensitive(approval.ownerUserSid, ownerUserSid))
			{
				if (xucrun::approval::IsBrokerApprovalExpired(
					approval, now, currentLogonSid))
				{
					DWORD deleteError = NO_ERROR;
					if (!xucrun::broker_store::Delete(target, deleteError) &&
						deleteError != ERROR_NOT_FOUND)
					{
						error = deleteError;
						return false;
					}
					continue;
				}
				if (approvals.size() >= MaximumApprovalCount)
				{
					error = ERROR_BUFFER_OVERFLOW;
					return false;
				}
				approvals.push_back(std::move(approval));
			}
		}
		error = NO_ERROR;
		return true;
	}

	bool ListAllForAdministration(
		std::vector<xucrun::approval::BrokerApprovalValue>& approvals, DWORD& error)
	{
		approvals.clear();
		std::vector<std::wstring> targets;
		std::wstring prefix;
		if (!GetApprovalTargetPrefix(prefix, error) ||
			!xucrun::broker_store::EnumerateTargets(prefix, targets, error))
		{
			return false;
		}
		const std::uint64_t now = GetCurrentFileTimeValue();
		for (const std::wstring& target : targets)
		{
			std::vector<std::uint8_t> value;
			xucrun::approval::BrokerApprovalValue approval;
			if (!xucrun::broker_store::Read(target, value, error) ||
				!xucrun::approval::DeserializeBrokerApproval(value, approval))
			{
				error = ERROR_INVALID_DATA;
				return false;
			}
			if (approval.lifetime ==
				xucrun::approval::BrokerApprovalLifetime::Until &&
				approval.expiresAt <= now)
			{
				DWORD deleteError = NO_ERROR;
				if (!xucrun::broker_store::Delete(target, deleteError) &&
					deleteError != ERROR_NOT_FOUND)
				{
					error = deleteError;
					return false;
				}
				continue;
			}
			if (approvals.size() >= MaximumApprovalCount)
			{
				error = ERROR_BUFFER_OVERFLOW;
				return false;
			}
			approvals.push_back(std::move(approval));
		}
		error = NO_ERROR;
		return true;
	}

	bool DeleteObsoleteApprovals(std::uint32_t& deletedCount, DWORD& error)
	{
		deletedCount = 0;
		std::wstring prefix;
		if (!xucrun::broker_targets::GetMachineScopedPrefix(
				xucrun::broker_targets::TargetKind::Approval, prefix, error))
		{
			return false;
		}
		prefix += L"8-";
		std::vector<std::wstring> targets;
		if (!xucrun::broker_store::EnumerateTargets(prefix, targets, error))
			return false;
		for (const std::wstring& target : targets)
		{
			if (!xucrun::broker_store::Delete(target, error) &&
				error != ERROR_NOT_FOUND)
			{
				return false;
			}
			if (error != ERROR_NOT_FOUND)
				deletedCount++;
		}
		error = NO_ERROR;
		return true;
	}

	// Finds a still valid approval that fully matches the current security context.
	bool HasMatching(const xucrun::approval::BrokerApprovalValue& context,
		std::uint64_t now, bool& matches, DWORD& error)
	{
		matches = false;
		std::vector<xucrun::approval::BrokerApprovalValue> approvals;
		if (!List(context.ownerUserSid, context.sessionLogonSid, approvals, error))
			return false;
		matches = std::any_of(approvals.begin(), approvals.end(),
			[&context, now](const auto& approval) {
				return IsMatch(approval, context, now);
			});
		error = NO_ERROR;
		return true;
	}

	// Tests the approval matching rules with positive and negative binding cases.
	bool RunApprovalMatchSelfTest()
	{
		xucrun::approval::BrokerApprovalValue stored;
		stored.programScope = xucrun::approval::BrokerApprovalProgramScope::General;
		stored.accountScope = xucrun::approval::BrokerApprovalAccountScope::AllAccounts;
		stored.lifetime = xucrun::approval::BrokerApprovalLifetime::Forever;
		stored.ownerUserSid = L"S-1-5-21-1";
		stored.elevationMode = 0;

		xucrun::approval::BrokerApprovalValue context = stored;
		if (!IsMatch(stored, context, 1) ||
			IsForAccount(stored, stored.ownerUserSid, L"S-1-5-21-2"))
			return false;
		context.elevationMode = 1;
		if (IsMatch(stored, context, 1))
			return false;

		stored.accountScope =
			xucrun::approval::BrokerApprovalAccountScope::CurrentAccount;
		stored.targetAccountSid = L"S-1-5-21-2";
		if (!IsForAccount(stored, L"s-1-5-21-1", L"s-1-5-21-2") ||
			IsForAccount(stored, L"S-1-5-21-3", stored.targetAccountSid) ||
			IsForAccount(stored, stored.ownerUserSid, L"S-1-5-21-4"))
		{
			return false;
		}

		stored.programScope =
			xucrun::approval::BrokerApprovalProgramScope::TargetProgram;
		stored.targetPath = L"C:\\Windows\\System32\\notepad.exe";
		stored.targetHash[0] = 2;
		context = stored;
		if (!IsMatch(stored, context, 1))
			return false;
		context.targetHash[0] = 3;
		if (IsMatch(stored, context, 1))
			return false;
		context = stored;
		context.targetPath = L"C:\\Windows\\System32\\write.exe";
		if (IsMatch(stored, context, 1))
			return false;
		stored.programScope =
			xucrun::approval::BrokerApprovalProgramScope::ExactInvocation;
		stored.visibility = 1;
		stored.argumentHash[0] = 4;
		stored.workingDirectory = L"C:\\Work";
		stored.scriptPath = L"C:\\Scripts\\task.ps1";
		stored.scriptHash[0] = 5;
		context = stored;
		if (!IsMatch(stored, context, 1))
			return false;
		context.argumentHash[0] = 6;
		if (IsMatch(stored, context, 1))
			return false;
		context = stored;
		context.workingDirectory = L"C:\\Other";
		if (IsMatch(stored, context, 1))
			return false;
		context = stored;
		context.workingDirectory = L"C:\\WORK";
		if (IsMatch(stored, context, 1))
			return false;
		context = stored;
		context.visibility = 2;
		if (IsMatch(stored, context, 1))
			return false;
		context = stored;
		context.scriptPath = L"C:\\Scripts\\other.ps1";
		if (IsMatch(stored, context, 1))
			return false;
		context = stored;
		context.scriptHash[0] = 7;
		if (IsMatch(stored, context, 1))
			return false;
		context = stored;
		context.elevationMode = 1;
		return !IsMatch(stored, context, 1);
	}

	// Finds exactly one plausible old program binding for a controlled approval transition.
	bool FindProgramUpdate(const xucrun::approval::BrokerApprovalValue& context,
		std::uint64_t now, ProgramUpdateInfo& update, DWORD& error)
	{
		(void)now;
		update = {};
		std::vector<xucrun::approval::BrokerApprovalValue> approvals;
		if (!List(context.ownerUserSid, context.sessionLogonSid, approvals, error))
			return false;
		const xucrun::approval::BrokerApprovalValue* trigger = nullptr;
		for (const auto& approval : approvals)
		{
			if (approval.programScope !=
				xucrun::approval::BrokerApprovalProgramScope::TargetProgram ||
				(approval.accountScope ==
					xucrun::approval::BrokerApprovalAccountScope::CurrentAccount &&
				 !EqualInsensitive(approval.targetAccountSid, context.targetAccountSid)) ||
				!EqualInsensitive(approval.targetPath, context.targetPath))
			{
				continue;
			}
			const bool targetMatches = approval.targetHash == context.targetHash;
			if (!targetMatches &&
				(!trigger || approval.createdAt > trigger->createdAt))
			{
				trigger = &approval;
			}
		}
		if (!trigger)
		{
			error = NO_ERROR;
			return true;
		}
		update.available = true;
		update.changedPath = context.targetPath;
		update.oldHash = trigger->targetHash;
		update.newHash = context.targetHash;
		std::set<std::wstring> affectedAccounts;
		for (const auto& approval : approvals)
		{
			if (!IsAffected(approval, update))
				continue;
			update.affectedApprovalCount++;
			if (approval.accountScope ==
				xucrun::approval::BrokerApprovalAccountScope::AllAccounts)
			{
				update.affectsAllAccounts = true;
			}
			else
			{
				affectedAccounts.insert(approval.targetAccountSid);
			}
		}
		update.affectedAccountCount = static_cast<std::uint32_t>(affectedAccounts.size());
		if (update.affectsAllAccounts && update.affectedAccountCount == 0)
			update.affectedAccountCount = 1;
		error = NO_ERROR;
		return true;
	}

	// Updates only affected valid approvals within the confirmed owner and account scope.
	bool ApplyProgramUpdate(const xucrun::approval::BrokerApprovalValue& context,
		const ProgramUpdateInfo& update, bool allAccounts,
		std::uint32_t& updatedCount, DWORD& error)
	{
		updatedCount = 0;
		if (!update.available || update.changedPath.empty() ||
			update.oldHash == update.newHash ||
			!EqualInsensitive(context.targetPath, update.changedPath) ||
			context.targetHash != update.newHash)
		{
			error = ERROR_INVALID_PARAMETER;
			return false;
		}
		std::vector<std::wstring> targets;
		std::wstring prefix;
		if (!GetApprovalTargetPrefix(prefix, error) ||
			!xucrun::broker_store::EnumerateTargets(prefix, targets, error))
			return false;
		const std::uint64_t now = GetCurrentFileTimeValue();
		for (const std::wstring& target : targets)
		{
			std::vector<std::uint8_t> value;
			xucrun::approval::BrokerApprovalValue approval;
			if (!xucrun::broker_store::Read(target, value, error) ||
				!xucrun::approval::DeserializeBrokerApproval(value, approval))
			{
				error = ERROR_INVALID_DATA;
				return false;
			}
			if (!EqualInsensitive(approval.ownerUserSid, context.ownerUserSid) ||
				xucrun::approval::IsBrokerApprovalExpired(
					approval, now, context.sessionLogonSid) ||
				!IsAffected(approval, update))
			{
				continue;
			}
			const bool globalApproval = approval.accountScope ==
				xucrun::approval::BrokerApprovalAccountScope::AllAccounts;
			if (!allAccounts && !globalApproval &&
				!EqualInsensitive(approval.targetAccountSid, context.targetAccountSid))
			{
				continue;
			}
			xucrun::approval::BrokerApprovalValue replacement = approval;
			if (!allAccounts && globalApproval)
			{
				replacement.accountScope =
					xucrun::approval::BrokerApprovalAccountScope::CurrentAccount;
				replacement.targetAccountSid = context.targetAccountSid;
			}
			ApplyUpdate(replacement, update);
			if (!Write(replacement, error))
				return false;
			if ((allAccounts || !globalApproval) &&
				!xucrun::broker_store::Delete(target, error) && error != ERROR_NOT_FOUND)
			{
				return false;
			}
			updatedCount++;
		}
		if (updatedCount == 0)
		{
			error = ERROR_NOT_FOUND;
			return false;
		}
		error = NO_ERROR;
		return true;
	}
}