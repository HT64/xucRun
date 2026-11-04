// Purpose: Defines direct program launch under the current user account.
// Approach: Exposes the declarations, constants, and types consumed by the components that provide this behavior.
// Behavior: Prepares batch targets and launches them with the current or elevated token.
#pragma once

#include <Windows.h>

#include <string>

namespace xucrun::same_user_launch
{
	enum class SameUserLaunchMode
	{
		Credentials,
		CurrentToken,
		ElevateCurrentToken
	};

	constexpr bool ShouldUseAutomaticCurrentAdministrator(
		bool currentUserIsAdministrator,
		bool hasSelectedAccount,
		bool selectCredentialWithoutElevation,
		bool selectLocalAccount) noexcept
	{
		return currentUserIsAdministrator && !hasSelectedAccount &&
			!selectCredentialWithoutElevation && !selectLocalAccount;
	}

	constexpr bool ShouldCreateMissingLaunchCredential(
		bool hasSelectedAccount,
		bool requestElevation,
		bool selectCredentialWithoutElevation) noexcept
	{
		return hasSelectedAccount || requestElevation ||
			selectCredentialWithoutElevation;
	}

	static_assert(ShouldUseAutomaticCurrentAdministrator(
		true, false, false, false));
	static_assert(!ShouldUseAutomaticCurrentAdministrator(
		false, false, false, false));
	static_assert(!ShouldUseAutomaticCurrentAdministrator(
		true, true, false, false));
	static_assert(!ShouldUseAutomaticCurrentAdministrator(
		true, false, true, false));
	static_assert(!ShouldUseAutomaticCurrentAdministrator(
		true, false, false, true));
	static_assert(ShouldCreateMissingLaunchCredential(true, false, false));
	static_assert(ShouldCreateMissingLaunchCredential(false, true, false));
	static_assert(ShouldCreateMissingLaunchCredential(false, false, true));
	static_assert(!ShouldCreateMissingLaunchCredential(false, false, false));

	SameUserLaunchMode GetSameUserLaunchMode(
		const std::wstring& accountName,
		bool requestElevation);

	bool LaunchDirectly(
		const std::wstring& programPath,
		const std::wstring& arguments,
		bool elevateCurrentToken,
		bool hiddenLaunch,
		bool monitorHiddenLaunch,
		HANDLE* launchedProcess = nullptr);
}