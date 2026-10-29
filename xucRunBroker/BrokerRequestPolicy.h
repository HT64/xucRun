// Purpose: Defines every broker request kind and its sensitive-client policy.
#pragma once

#include <cstddef>
#include <string_view>

#define XUCRUN_BROKER_REQUEST_KIND_LIST(X) \
	X(Invalid, false) \
	X(CredentialCaptureCommit, false) \
	X(Health, true) \
	X(Launch, false) \
	X(CredentialMetadata, false) \
	X(SessionTest, false) \
	X(CredentialStoreTest, false) \
	X(CredentialSave, false) \
	X(CredentialDelete, false) \
	X(CredentialList, true) \
	X(CredentialUseTest, false) \
	X(ApprovalChallenge, false) \
	X(ApprovalConfirmation, false) \
	X(ApprovalHelper, false) \
	X(ApprovalHelperDecision, false) \
	X(ApprovalHelperLease, false) \
	X(ApprovalList, true) \
	X(ApprovalReset, false) \
	X(SecurityAudit, false) \
	X(TrackedProcessList, true) \
	X(TrackedProcessRegistration, false) \
	X(StatusIconList, true) \
	X(BrokerStop, true) \
	X(CredentialCaptureChallenge, false) \
	X(CredentialCaptureHelper, false) \
	X(CredentialCaptureSubmit, false) \
	X(AdministrativeInventory, false) \
	X(AdministrativeDelete, false) \
	X(Backup, false) \
	X(SystemLaunch, false)

enum class BrokerRequestKind
{
#define XUCRUN_DECLARE_BROKER_REQUEST_KIND(name, trayAllowed) name,
	XUCRUN_BROKER_REQUEST_KIND_LIST(XUCRUN_DECLARE_BROKER_REQUEST_KIND)
#undef XUCRUN_DECLARE_BROKER_REQUEST_KIND
	Count
};

constexpr bool IsKnownBrokerRequestKind(BrokerRequestKind kind) noexcept
{
	return kind > BrokerRequestKind::Invalid &&
		kind < BrokerRequestKind::Count;
}

constexpr bool IsTrayRequestAllowed(BrokerRequestKind kind) noexcept
{
	switch (kind)
	{
#define XUCRUN_CLASSIFY_TRAY_REQUEST(name, trayAllowed) \
	case BrokerRequestKind::name: return trayAllowed;
	XUCRUN_BROKER_REQUEST_KIND_LIST(XUCRUN_CLASSIFY_TRAY_REQUEST)
#undef XUCRUN_CLASSIFY_TRAY_REQUEST
	default: return false;
	}
}

constexpr const wchar_t* GetBrokerRequestKindName(
	BrokerRequestKind kind) noexcept
{
	switch (kind)
	{
#define XUCRUN_WIDEN_REQUEST_NAME_INNER(value) L##value
#define XUCRUN_WIDEN_REQUEST_NAME(value) XUCRUN_WIDEN_REQUEST_NAME_INNER(value)
#define XUCRUN_NAME_BROKER_REQUEST(name, trayAllowed) \
	case BrokerRequestKind::name: return XUCRUN_WIDEN_REQUEST_NAME(#name);
	XUCRUN_BROKER_REQUEST_KIND_LIST(XUCRUN_NAME_BROKER_REQUEST)
#undef XUCRUN_NAME_BROKER_REQUEST
#undef XUCRUN_WIDEN_REQUEST_NAME
#undef XUCRUN_WIDEN_REQUEST_NAME_INNER
	default: return L"Unknown";
	}
}

constexpr bool IsSensitiveClientRequestAllowed(BrokerRequestKind kind,
	bool coreClientAuthorized, bool trayClientAuthorized) noexcept
{
	if (!IsKnownBrokerRequestKind(kind))
		return false;
	if (coreClientAuthorized)
		return true;
	return trayClientAuthorized && IsTrayRequestAllowed(kind);
}

constexpr bool RunBrokerRequestPolicySelfTest() noexcept
{
	std::size_t trayAllowedCount = 0;
	for (std::size_t value = 0;
		value < static_cast<std::size_t>(BrokerRequestKind::Count); ++value)
	{
		const auto kind = static_cast<BrokerRequestKind>(value);
		if (kind == BrokerRequestKind::Invalid)
		{
			if (IsSensitiveClientRequestAllowed(kind, true, true))
				return false;
			continue;
		}
		if (!IsSensitiveClientRequestAllowed(kind, true, false) ||
			IsSensitiveClientRequestAllowed(kind, false, false))
		{
			return false;
		}
		if (IsTrayRequestAllowed(kind))
		{
			++trayAllowedCount;
			if (!IsSensitiveClientRequestAllowed(kind, false, true))
				return false;
		}
		else if (IsSensitiveClientRequestAllowed(kind, false, true))
		{
			return false;
		}
	}
	return trayAllowedCount == 6;
}

static_assert(static_cast<std::size_t>(BrokerRequestKind::Count) == 30);
static_assert(RunBrokerRequestPolicySelfTest());
static_assert(IsTrayRequestAllowed(BrokerRequestKind::Health));
static_assert(IsTrayRequestAllowed(BrokerRequestKind::CredentialList));
static_assert(IsTrayRequestAllowed(BrokerRequestKind::ApprovalList));
static_assert(IsTrayRequestAllowed(BrokerRequestKind::TrackedProcessList));
static_assert(IsTrayRequestAllowed(BrokerRequestKind::StatusIconList));
static_assert(IsTrayRequestAllowed(BrokerRequestKind::BrokerStop));
static_assert(!IsTrayRequestAllowed(BrokerRequestKind::Launch));
static_assert(!IsTrayRequestAllowed(BrokerRequestKind::CredentialSave));
static_assert(!IsTrayRequestAllowed(BrokerRequestKind::AdministrativeInventory));
static_assert(!IsTrayRequestAllowed(BrokerRequestKind::SystemLaunch));
static_assert(std::wstring_view(
	GetBrokerRequestKindName(BrokerRequestKind::ApprovalChallenge)) ==
	L"ApprovalChallenge");

#undef XUCRUN_BROKER_REQUEST_KIND_LIST
