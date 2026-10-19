// Purpose: Defines direct program launch under the current user account.
// Behavior: Prepares batch targets and launches them with the current or elevated token.
#pragma once

#include <string>

namespace xucrun::same_user_launch
{
	enum class SameUserLaunchMode
	{
		Credentials,
		CurrentToken,
		ElevateCurrentToken
	};

	SameUserLaunchMode GetSameUserLaunchMode(
		const std::wstring& accountName,
		bool requestElevation);

	bool LaunchDirectly(
		const std::wstring& programPath,
		const std::wstring& arguments,
		bool elevateCurrentToken,
		bool hiddenLaunch,
		bool monitorHiddenLaunch);
}