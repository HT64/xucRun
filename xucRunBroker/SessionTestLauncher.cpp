// Purpose: Launches controlled processes and secure-desktop hosts in the authenticated client session.
// Prepares tokens and interactive object access for diagnostics and sensitive helper UI.
#include "SessionTestLauncher.h"

#include "BrokerTemporaryBatch.h"
#include "BrokerContracts.h"
#include "CredentialValidator.h"

#include <aclapi.h>
#include <sddl.h>
#include <userenv.h>

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <vector>

namespace
{
	constexpr ACCESS_MASK WindowStationAccess =
		WINSTA_ENUMDESKTOPS | WINSTA_READATTRIBUTES | WINSTA_ACCESSCLIPBOARD |
		WINSTA_CREATEDESKTOP | WINSTA_WRITEATTRIBUTES | WINSTA_ACCESSGLOBALATOMS |
		WINSTA_EXITWINDOWS | WINSTA_ENUMERATE | WINSTA_READSCREEN |
		STANDARD_RIGHTS_REQUIRED;
	constexpr ACCESS_MASK DesktopAccess =
		DESKTOP_READOBJECTS | DESKTOP_CREATEWINDOW | DESKTOP_CREATEMENU |
		DESKTOP_HOOKCONTROL | DESKTOP_JOURNALRECORD | DESKTOP_JOURNALPLAYBACK |
		DESKTOP_ENUMERATE | DESKTOP_WRITEOBJECTS | DESKTOP_SWITCHDESKTOP |
		STANDARD_RIGHTS_REQUIRED;
	constexpr ACCESS_MASK DirectoryQuery = 0x0001;
	constexpr ACCESS_MASK DirectoryTraverse = 0x0002;
	constexpr ACCESS_MASK DirectoryCreateObject = 0x0004;
	constexpr ACCESS_MASK DirectoryCreateSubdirectory = 0x0008;
	constexpr ACCESS_MASK BaseNamedObjectsAccess =
		DirectoryQuery | DirectoryTraverse | DirectoryCreateObject |
		DirectoryCreateSubdirectory | READ_CONTROL;

	struct NativeUnicodeString
	{
		USHORT length;
		USHORT maximumLength;
		PWSTR buffer;
	};

	struct NativeObjectAttributes
	{
		ULONG length;
		HANDLE rootDirectory;
		NativeUnicodeString* objectName;
		ULONG attributes;
		PVOID securityDescriptor;
		PVOID securityQualityOfService;
	};

	using NtOpenDirectoryObjectFunction = LONG(NTAPI*)(
		PHANDLE, ACCESS_MASK, NativeObjectAttributes*);
	using RtlNtStatusToDosErrorFunction = ULONG(NTAPI*)(LONG);
	bool LaunchFixedTestConsole(HANDLE token, DWORD& processId,
		DWORD& error, std::wstring& failedStage);


	// Preparation validates tokens and grants only the required interactive-object access.
#include "SessionLaunchPreparation.inl"

	// Execution owns process creation plus post-start result and lifetime validation.
#include "SessionLaunchExecution.inl"
}

// Public operations retain the established API while orchestrating the private phases.
#include "SessionLaunchOperations.inl"
