// Purpose: Internal implementation fragment for SessionTestLauncher.cpp.
//
// Inclusion contract:
// - This file is included exactly once by SessionTestLauncher.cpp.
// - It shares that translation unit's private token, desktop, profile, and process helpers.
// - It is not a standalone translation unit and must not expose internal security state.
// - Token validation, handle ownership, cleanup order, and failure-stage reporting are preserved.

// Enables a required process privilege and does not accept a partially successful adjustment.
	bool EnablePrivilege(const wchar_t* name, DWORD& error)
	{
		HANDLE token = nullptr;
		if (!OpenProcessToken(GetCurrentProcess(),
			TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token))
		{
			error = GetLastError();
			return false;
		}
		TOKEN_PRIVILEGES privileges = {};
		privileges.PrivilegeCount = 1;
		if (!LookupPrivilegeValueW(nullptr, name, &privileges.Privileges[0].Luid))
		{
			error = GetLastError();
			CloseHandle(token);
			return false;
		}
		privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
		SetLastError(NO_ERROR);
		const BOOL adjusted = AdjustTokenPrivileges(
			token, FALSE, &privileges, 0, nullptr, nullptr);
		error = GetLastError();
		CloseHandle(token);
		return adjusted && error == NO_ERROR;
	}

	class UserProfileCache
	{
	private:
		struct Entry
		{
			std::wstring sid;
			HANDLE token = nullptr;
			HANDLE profile = nullptr;
		};

		std::mutex mutex;
		std::vector<Entry> entries;

	public:
		// Unloads cached profiles and closes the associated token handles.
		~UserProfileCache()
		{
			for (auto entry = entries.rbegin(); entry != entries.rend(); ++entry)
			{
				if (entry->profile)
					UnloadUserProfile(entry->token, entry->profile);
				if (entry->token)
					CloseHandle(entry->token);
			}
		}

		// Loads a user profile at most once per SID and retains a dedicated token handle for it.
		bool EnsureLoaded(HANDLE token, const AccountIdentity& identity, DWORD& error)
		{
			std::lock_guard<std::mutex> lock(mutex);
			for (const Entry& entry : entries)
			{
				if (_wcsicmp(entry.sid.c_str(), identity.sid.c_str()) == 0)
					return true;
			}

			if (!EnablePrivilege(SE_BACKUP_NAME, error) ||
				!EnablePrivilege(SE_RESTORE_NAME, error))
			{
				return false;
			}

			PROFILEINFOW profileInfo = {};
			profileInfo.dwSize = sizeof(profileInfo);
			profileInfo.dwFlags = PI_NOUI;
			profileInfo.lpUserName = const_cast<LPWSTR>(identity.userName.c_str());
			if (!LoadUserProfileW(token, &profileInfo))
			{
				error = GetLastError();
				return false;
			}

			HANDLE retainedToken = nullptr;
			if (!DuplicateHandle(GetCurrentProcess(), token, GetCurrentProcess(),
				&retainedToken, 0, FALSE, DUPLICATE_SAME_ACCESS))
			{
				error = GetLastError();
				UnloadUserProfile(token, profileInfo.hProfile);
				return false;
			}

			entries.push_back({ identity.sid, retainedToken, profileInfo.hProfile });
			return true;
		}
	};

	// Returns the process-wide synchronized user profile cache.
	UserProfileCache& GetUserProfileCache()
	{
		static UserProfileCache cache;
		return cache;
	}

	// Reads the user SID and session ID together from the same token.
	bool ReadTokenIdentity(HANDLE token, std::wstring& sid, DWORD& sessionId, DWORD& error)
	{
		DWORD requiredSize = 0;
		GetTokenInformation(token, TokenUser, nullptr, 0, &requiredSize);
		if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || requiredSize == 0)
		{
			error = GetLastError();
			return false;
		}
		std::vector<std::uint8_t> buffer(requiredSize);
		if (!GetTokenInformation(token, TokenUser,
			buffer.data(), requiredSize, &requiredSize))
		{
			error = GetLastError();
			return false;
		}
		wchar_t* sidValue = nullptr;
		const auto* user = reinterpret_cast<const TOKEN_USER*>(buffer.data());
		if (!ConvertSidToStringSidW(user->User.Sid, &sidValue))
		{
			error = GetLastError();
			return false;
		}
		sid = sidValue;
		LocalFree(sidValue);

		DWORD sessionSize = sizeof(sessionId);
		if (!GetTokenInformation(token, TokenSessionId,
			&sessionId, sessionSize, &sessionSize))
		{
			error = GetLastError();
			return false;
		}
		return true;
	}

	// Duplicates a source token with the minimum required rights as the primary launch token.
	bool DuplicatePrimaryLaunchToken(HANDLE sourceToken, HANDLE& launchToken,
		DWORD& error)
	{
		launchToken = nullptr;
		if (!DuplicateTokenEx(sourceToken,
			TOKEN_ASSIGN_PRIMARY | TOKEN_DUPLICATE | TOKEN_QUERY |
			TOKEN_ADJUST_DEFAULT | TOKEN_ADJUST_SESSIONID,
			nullptr, SecurityImpersonation, TokenPrimary, &launchToken))
		{
			error = GetLastError();
			return false;
		}
		return true;
	}

	bool IsHighIntegrityToken(HANDLE token, DWORD& error)
	{
		DWORD requiredSize = 0;
		GetTokenInformation(token, TokenIntegrityLevel, nullptr, 0, &requiredSize);
		if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || requiredSize == 0)
		{
			error = GetLastError();
			return false;
		}
		std::vector<std::uint8_t> buffer(requiredSize);
		if (!GetTokenInformation(token, TokenIntegrityLevel,
			buffer.data(), requiredSize, &requiredSize))
		{
			error = GetLastError();
			return false;
		}
		const auto* label = reinterpret_cast<const TOKEN_MANDATORY_LABEL*>(buffer.data());
		PSID sid = label->Label.Sid;
		const UCHAR count = IsValidSid(sid) ? *GetSidSubAuthorityCount(sid) : 0;
		if (count == 0)
		{
			error = ERROR_INVALID_SID;
			return false;
		}
		if (*GetSidSubAuthority(sid, count - 1) < SECURITY_MANDATORY_HIGH_RID)
		{
			error = ERROR_ELEVATION_REQUIRED;
			return false;
		}
		return true;
	}

	// Extracts the logon SID from a token to bind session and object access.
	bool ReadLogonSid(HANDLE token, std::vector<std::uint8_t>& sid, DWORD& error)
	{
		DWORD requiredSize = 0;
		GetTokenInformation(token, TokenGroups, nullptr, 0, &requiredSize);
		if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || requiredSize == 0)
		{
			error = GetLastError();
			return false;
		}
		std::vector<std::uint8_t> groupsBuffer(requiredSize);
		if (!GetTokenInformation(token, TokenGroups,
			groupsBuffer.data(), requiredSize, &requiredSize))
		{
			error = GetLastError();
			return false;
		}
		const auto* groups = reinterpret_cast<const TOKEN_GROUPS*>(groupsBuffer.data());
		for (DWORD index = 0; index < groups->GroupCount; index++)
		{
			if ((groups->Groups[index].Attributes & SE_GROUP_LOGON_ID) == SE_GROUP_LOGON_ID)
			{
				const DWORD sidLength = GetLengthSid(groups->Groups[index].Sid);
				sid.resize(sidLength);
				if (!CopySid(sidLength, sid.data(), groups->Groups[index].Sid))
				{
					error = GetLastError();
					sid.clear();
					return false;
				}
				return true;
			}
		}
		error = ERROR_NO_SUCH_LOGON_SESSION;
		return false;
	}

	// Checks whether a DACL already grants the specified SID at least the required access.
	bool HasAllowedAce(PACL acl, PSID sid, ACCESS_MASK requiredAccess,
		BYTE requiredFlags)
	{
		if (!acl)
			return false;
		for (DWORD index = 0; index < acl->AceCount; index++)
		{
			void* rawAce = nullptr;
			if (!GetAce(acl, index, &rawAce))
				return false;
			const auto* header = static_cast<const ACE_HEADER*>(rawAce);
			if (header->AceType != ACCESS_ALLOWED_ACE_TYPE ||
				(header->AceFlags & requiredFlags) != requiredFlags)
			{
				continue;
			}
			const auto* ace = static_cast<const ACCESS_ALLOWED_ACE*>(rawAce);
			if (EqualSid(const_cast<DWORD*>(&ace->SidStart), sid) &&
				(ace->Mask & requiredAccess) == requiredAccess)
			{
				return true;
			}
		}
		return false;
	}

	// Adds exactly one required access ACE without replacing existing broader rights.
	bool GrantUserObjectAccess(HANDLE object, PSID sid,
		ACCESS_MASK accessMask, DWORD inheritance, DWORD& error)
	{
		SECURITY_INFORMATION securityInformation = DACL_SECURITY_INFORMATION;
		DWORD requiredSize = 0;
		GetUserObjectSecurity(object, &securityInformation, nullptr, 0, &requiredSize);
		if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || requiredSize == 0)
		{
			error = GetLastError();
			return false;
		}
		std::vector<std::uint8_t> descriptorBuffer(requiredSize);
		if (!GetUserObjectSecurity(object, &securityInformation,
			descriptorBuffer.data(), requiredSize, &requiredSize))
		{
			error = GetLastError();
			return false;
		}
		PACL existingAcl = nullptr;
		BOOL daclPresent = FALSE;
		BOOL daclDefaulted = FALSE;
		if (!GetSecurityDescriptorDacl(descriptorBuffer.data(),
			&daclPresent, &existingAcl, &daclDefaulted) || !daclPresent)
		{
			error = GetLastError();
			return false;
		}
		if (HasAllowedAce(existingAcl, sid, accessMask,
			static_cast<BYTE>(inheritance)))
		{
			return true;
		}

		EXPLICIT_ACCESSW explicitAccess = {};
		explicitAccess.grfAccessPermissions = accessMask;
		explicitAccess.grfAccessMode = GRANT_ACCESS;
		explicitAccess.grfInheritance = inheritance;
		explicitAccess.Trustee.TrusteeForm = TRUSTEE_IS_SID;
		explicitAccess.Trustee.TrusteeType = TRUSTEE_IS_USER;
		explicitAccess.Trustee.ptstrName = static_cast<LPWSTR>(sid);
		PACL updatedAcl = nullptr;
		error = SetEntriesInAclW(1, &explicitAccess, existingAcl, &updatedAcl);
		if (error != ERROR_SUCCESS)
			return false;

		SECURITY_DESCRIPTOR updatedDescriptor = {};
		const bool success = InitializeSecurityDescriptor(&updatedDescriptor,
			SECURITY_DESCRIPTOR_REVISION) == TRUE &&
			SetSecurityDescriptorDacl(&updatedDescriptor, TRUE, updatedAcl, FALSE) == TRUE &&
			SetUserObjectSecurity(object, &securityInformation, &updatedDescriptor) == TRUE;
		if (!success)
			error = GetLastError();
		LocalFree(updatedAcl);
		return success;
	}

	// Grants the validated logon SID the rights to BaseNamedObjects required for process launches.
	bool GrantBaseNamedObjectsAccess(PSID logonSid, DWORD& error)
	{
		HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
		DWORD sessionId = 0;
		if (!ntdll || !ProcessIdToSessionId(GetCurrentProcessId(), &sessionId))
		{
			error = ntdll ? GetLastError() : ERROR_MOD_NOT_FOUND;
			return false;
		}
		const auto ntOpenDirectoryObject = reinterpret_cast<NtOpenDirectoryObjectFunction>(
			GetProcAddress(ntdll, "NtOpenDirectoryObject"));
		const auto rtlNtStatusToDosError = reinterpret_cast<RtlNtStatusToDosErrorFunction>(
			GetProcAddress(ntdll, "RtlNtStatusToDosError"));
		if (!ntOpenDirectoryObject || !rtlNtStatusToDosError)
		{
			error = ERROR_PROC_NOT_FOUND;
			return false;
		}

		std::wstring directoryName = L"\\Sessions\\" + std::to_wstring(sessionId) +
			L"\\BaseNamedObjects";
		NativeUnicodeString unicodeName = {
			static_cast<USHORT>(directoryName.size() * sizeof(wchar_t)),
			static_cast<USHORT>((directoryName.size() + 1) * sizeof(wchar_t)),
			directoryName.data()
		};
		NativeObjectAttributes attributes = {
			sizeof(attributes), nullptr, &unicodeName, 0x40, nullptr, nullptr
		};
		HANDLE directory = nullptr;
		const LONG status = ntOpenDirectoryObject(&directory,
			READ_CONTROL | WRITE_DAC, &attributes);
		if (status < 0)
		{
			error = rtlNtStatusToDosError(status);
			return false;
		}

		PACL existingAcl = nullptr;
		PSECURITY_DESCRIPTOR descriptor = nullptr;
		error = GetSecurityInfo(directory, SE_KERNEL_OBJECT,
			DACL_SECURITY_INFORMATION, nullptr, nullptr, &existingAcl, nullptr,
			&descriptor);
		if (error != ERROR_SUCCESS)
		{
			CloseHandle(directory);
			return false;
		}
		if (HasAllowedAce(existingAcl, logonSid, BaseNamedObjectsAccess, 0))
		{
			LocalFree(descriptor);
			CloseHandle(directory);
			return true;
		}

		EXPLICIT_ACCESSW explicitAccess = {};
		explicitAccess.grfAccessPermissions = BaseNamedObjectsAccess;
		explicitAccess.grfAccessMode = GRANT_ACCESS;
		explicitAccess.grfInheritance = NO_INHERITANCE;
		explicitAccess.Trustee.TrusteeForm = TRUSTEE_IS_SID;
		explicitAccess.Trustee.TrusteeType = TRUSTEE_IS_USER;
		explicitAccess.Trustee.ptstrName = static_cast<LPWSTR>(logonSid);
		PACL updatedAcl = nullptr;
		error = SetEntriesInAclW(1, &explicitAccess, existingAcl, &updatedAcl);
		if (error == ERROR_SUCCESS)
		{
			error = SetSecurityInfo(directory, SE_KERNEL_OBJECT,
				DACL_SECURITY_INFORMATION, nullptr, nullptr, updatedAcl, nullptr);
		}
		LocalFree(updatedAcl);
		LocalFree(descriptor);
		CloseHandle(directory);
		return error == ERROR_SUCCESS;
	}

	// Grants the required window station and desktop rights only to the validated logon SID.
	bool GrantInteractiveDesktopAccess(PSID logonSid, DWORD& error)
	{
		HWINSTA interactiveWindowStation = OpenWindowStationW(
			L"winsta0", FALSE, READ_CONTROL | WRITE_DAC | WINSTA_ENUMDESKTOPS);
		if (!interactiveWindowStation)
		{
			error = GetLastError();
			return false;
		}

		HWINSTA originalWindowStation = GetProcessWindowStation();
		HDESK desktop = nullptr;
		if (SetProcessWindowStation(interactiveWindowStation))
		{
			desktop = OpenDesktopW(L"default", 0, FALSE,
				READ_CONTROL | WRITE_DAC | DESKTOP_READOBJECTS | DESKTOP_WRITEOBJECTS);
			error = desktop ? NO_ERROR : GetLastError();
			if (!SetProcessWindowStation(originalWindowStation) && error == NO_ERROR)
				error = GetLastError();
		}
		else
		{
			error = GetLastError();
		}
		const bool success = desktop && error == NO_ERROR &&
			GrantUserObjectAccess(interactiveWindowStation, logonSid,
				GENERIC_ALL, CONTAINER_INHERIT_ACE | OBJECT_INHERIT_ACE |
				INHERIT_ONLY_ACE, error) &&
			GrantUserObjectAccess(interactiveWindowStation, logonSid,
				WindowStationAccess, NO_PROPAGATE_INHERIT_ACE, error) &&
			GrantUserObjectAccess(desktop, logonSid, DesktopAccess, 0, error);
		if (desktop)
			CloseDesktop(desktop);
		CloseWindowStation(interactiveWindowStation);
		return success;
	}

	// Validates the helper's SID and target session before modifying interactive object ACLs.
	bool GrantInteractiveSidAccessInSession(const std::wstring& sidValue,
		DWORD sessionId, DWORD& error, std::wstring& failedStage)
	{
		wchar_t modulePath[32768] = {};
		const DWORD modulePathLength = GetModuleFileNameW(nullptr, modulePath,
			static_cast<DWORD>(std::size(modulePath)));
		if (modulePathLength == 0 || modulePathLength == std::size(modulePath))
		{
			error = modulePathLength == 0 ? GetLastError() : ERROR_INSUFFICIENT_BUFFER;
			return false;
		}
		std::wstring commandLine = L"\"" + std::wstring(modulePath) +
			L"\" /grant-desktop-access-helper " + sidValue;
		std::vector<wchar_t> mutableCommandLine(commandLine.begin(), commandLine.end());
		mutableCommandLine.push_back(L'\0');

		HANDLE processToken = nullptr;
		HANDLE sessionToken = nullptr;
		failedStage = L"desktop helper system token";
		if (!OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE | TOKEN_QUERY,
			&processToken) ||
			!DuplicateTokenEx(processToken,
				TOKEN_ASSIGN_PRIMARY | TOKEN_DUPLICATE | TOKEN_QUERY |
				TOKEN_ADJUST_DEFAULT | TOKEN_ADJUST_SESSIONID,
				nullptr, SecurityImpersonation, TokenPrimary, &sessionToken))
		{
			error = GetLastError();
			if (processToken)
				CloseHandle(processToken);
			return false;
		}
		CloseHandle(processToken);
		if (!SetTokenInformation(sessionToken, TokenSessionId,
			&sessionId, sizeof(sessionId)))
		{
			error = GetLastError();
			CloseHandle(sessionToken);
			return false;
		}

		STARTUPINFOW startup = {};
		startup.cb = sizeof(startup);
		PROCESS_INFORMATION helper = {};
		failedStage = L"desktop helper start";
		const BOOL started = CreateProcessAsUserW(sessionToken, modulePath,
			mutableCommandLine.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
			nullptr, nullptr, &startup, &helper);
		error = started ? NO_ERROR : GetLastError();
		CloseHandle(sessionToken);
		if (!started)
			return false;
		CloseHandle(helper.hThread);

		failedStage = L"desktop helper wait";
		const DWORD waitResult = WaitForSingleObject(helper.hProcess, 10000);
		DWORD exitCode = ERROR_GEN_FAILURE;
		const bool completed = waitResult == WAIT_OBJECT_0 &&
			GetExitCodeProcess(helper.hProcess, &exitCode) == TRUE;
		CloseHandle(helper.hProcess);
		if (!completed || exitCode != NO_ERROR)
		{
			error = waitResult == WAIT_TIMEOUT ? ERROR_TIMEOUT : exitCode;
			return false;
		}
		return true;
	}

	// Launches the ACL helper in the token session and binds the request to its logon SID.
	bool GrantInteractiveDesktopAccessInSession(HANDLE logonToken, DWORD sessionId,
		DWORD& error, std::wstring& failedStage)
	{
		std::vector<std::uint8_t> logonSid;
		failedStage = L"interactive desktop logon SID";
		if (!ReadLogonSid(logonToken, logonSid, error))
			return false;
		wchar_t* logonSidValue = nullptr;
		if (!ConvertSidToStringSidW(logonSid.data(), &logonSidValue))
		{
			error = GetLastError();
			return false;
		}
		const std::wstring sidValue(logonSidValue);
		LocalFree(logonSidValue);
		return GrantInteractiveSidAccessInSession(
			sidValue, sessionId, error, failedStage);
	}

	// Validates target access and selects an accessible working-directory path for the launch token.
	bool ValidateLaunchObjectAccess(HANDLE token,
		const std::wstring& executablePath,
		const std::wstring& workingDirectory,
		std::wstring& effectiveWorkingDirectory,
		DWORD& error, std::wstring& failedStage)
	{
		effectiveWorkingDirectory.clear();
		failedStage = L"target access impersonation";
		if (!ImpersonateLoggedOnUser(token))
		{
			error = GetLastError();
			return false;
		}

		failedStage = L"target executable access";
		HANDLE executable = CreateFileW(executablePath.c_str(),
			FILE_EXECUTE | FILE_READ_ATTRIBUTES,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			nullptr, OPEN_EXISTING, 0, nullptr);
		bool success = executable != INVALID_HANDLE_VALUE;
		if (!success)
			error = GetLastError();
		else
			CloseHandle(executable);

		if (success && !workingDirectory.empty())
		{
			failedStage = L"working directory access";
			std::filesystem::path candidate = workingDirectory;
			while (true)
			{
				HANDLE directory = CreateFileW(candidate.c_str(),
					FILE_TRAVERSE | FILE_READ_ATTRIBUTES,
					FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
					nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
				if (directory != INVALID_HANDLE_VALUE)
				{
					CloseHandle(directory);
					effectiveWorkingDirectory = candidate.native();
					break;
				}
				error = GetLastError();
				const std::filesystem::path parent = candidate.parent_path();
				if (error != ERROR_ACCESS_DENIED || parent.empty() || parent == candidate)
				{
					success = false;
					break;
				}
				candidate = parent;
			}
		}

		if (!RevertToSelf() && success)
		{
			failedStage = L"target access revert";
			error = GetLastError();
			success = false;
		}
		return success;
	}
