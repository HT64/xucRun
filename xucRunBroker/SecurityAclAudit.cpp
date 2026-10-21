// Purpose: Audits broker files, directories, and registry entries for unexpected writers.
// Accepts only the intended privileged security principals.
#include "SecurityAclAudit.h"

#include "BrokerBackupArchive.h"
#include "BrokerPipeClient.h"
#include "ClientImageIdentity.h"
#include "CredentialMetadataStore.h"
#include "CredentialStore.h"
#include "SensitiveClientAuthorization.h"

#include <Windows.h>
#include <aclapi.h>
#include <sddl.h>
#include <shlobj.h>
#include <taskschd.h>

#include <algorithm>
#include <array>
#include <iomanip>
#include <sstream>
#include <vector>

namespace
{
	constexpr wchar_t TrustedInstallerSid[] =
		L"S-1-5-80-956008885-3418522649-1831038044-1853292631-2271478464";
	constexpr wchar_t TrayUninstallRegistryPath[] =
		L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\xucRun Tray";
	constexpr wchar_t TrayTaskFolder[] = L"\\xucRun";
	constexpr wchar_t TrayTaskPrefix[] = L"BrokerStatus-";

	template<typename T>
	class ComPtr
	{
	public:
		~ComPtr()
		{
			if (value_)
				value_->Release();
		}

		ComPtr(const ComPtr&) = delete;
		ComPtr& operator=(const ComPtr&) = delete;
		ComPtr() = default;

		T* operator->() const
		{
			return value_;
		}

		T** Put()
		{
			return &value_;
		}

		void Reset()
		{
			if (value_)
			{
				value_->Release();
				value_ = nullptr;
			}
		}

		explicit operator bool() const
		{
			return value_ != nullptr;
		}

	private:
		T* value_ = nullptr;
	};

	class LocalBstr
	{
	public:
		LocalBstr() = default;

		explicit LocalBstr(const wchar_t* value)
			: value_(SysAllocString(value))
		{
		}

		~LocalBstr()
		{
			SysFreeString(value_);
		}

		LocalBstr(const LocalBstr&) = delete;
		LocalBstr& operator=(const LocalBstr&) = delete;

		BSTR Get() const
		{
			return value_;
		}

		BSTR* Put()
		{
			return &value_;
		}

		std::wstring ToString() const
		{
			return value_ ? std::wstring(value_, SysStringLen(value_)) : std::wstring();
		}

	private:
		BSTR value_ = nullptr;
	};

	const wchar_t* SelectAuditText(bool german, const wchar_t* english,
		const wchar_t* germanText)
	{
		return german ? germanText : english;
	}

	void AppendUniquePath(std::vector<std::wstring>& paths, const std::wstring& path)
	{
		const auto existing = std::find_if(paths.begin(), paths.end(),
			[&path](const std::wstring& candidate) {
				return CompareStringOrdinal(candidate.c_str(), -1, path.c_str(), -1, TRUE) ==
					CSTR_EQUAL;
			});
		if (existing == paths.end())
			paths.push_back(path);
	}

	std::wstring FormatTrustStatus(LONG status)
	{
		std::wostringstream stream;
		stream << L"0x" << std::uppercase << std::hex << std::setw(8) <<
			std::setfill(L'0') << static_cast<unsigned long>(status);
		return stream.str();
	}

	struct LocalSid
	{
		PSID value = nullptr;

		// Releases a locally allocated SID when leaving scope.
		~LocalSid()
		{
			if (value)
				LocalFree(value);
		}

		// Prevents double release by copying the locally allocated SID.
		LocalSid(const LocalSid&) = delete;
		// Prevents assignment of a second owner for the same SID.
		LocalSid& operator=(const LocalSid&) = delete;
		// Initializes an empty SID owner for later Windows allocation.
		LocalSid() = default;
	};

	// Accepts only SYSTEM, Administrators, and TrustedInstaller as security-relevant writers.
	bool IsExpectedWriter(PSID sid)
	{
		if (!sid || !IsValidSid(sid))
			return false;
		std::array<BYTE, SECURITY_MAX_SID_SIZE> systemBuffer = {};
		std::array<BYTE, SECURITY_MAX_SID_SIZE> administratorsBuffer = {};
		DWORD systemSize = static_cast<DWORD>(systemBuffer.size());
		DWORD administratorsSize = static_cast<DWORD>(administratorsBuffer.size());
		LocalSid trustedInstaller;
		return CreateWellKnownSid(WinLocalSystemSid, nullptr,
				systemBuffer.data(), &systemSize) &&
			CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr,
				administratorsBuffer.data(), &administratorsSize) &&
			ConvertStringSidToSidW(TrustedInstallerSid, &trustedInstaller.value) &&
			(EqualSid(sid, systemBuffer.data()) ||
				EqualSid(sid, administratorsBuffer.data()) ||
				EqualSid(sid, trustedInstaller.value));
	}

	// Formats a SID with its resolvable account name for understandable audit findings.
	std::wstring FormatSid(PSID sid, bool german)
	{
		wchar_t* sidText = nullptr;
		if (!sid || !ConvertSidToStringSidW(sid, &sidText))
			return SelectAuditText(german, L"<invalid SID>", L"<ungültige SID>");
		std::wstring result = sidText;
		LocalFree(sidText);

		DWORD nameSize = 0;
		DWORD domainSize = 0;
		SID_NAME_USE use = SidTypeUnknown;
		LookupAccountSidW(nullptr, sid, nullptr, &nameSize, nullptr, &domainSize, &use);
		if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || nameSize == 0)
			return result;
		std::vector<wchar_t> name(nameSize);
		std::vector<wchar_t> domain(domainSize);
		if (LookupAccountSidW(nullptr, sid, name.data(), &nameSize,
			domain.empty() ? nullptr : domain.data(), &domainSize, &use))
		{
			result += L" (";
			if (domainSize > 1)
				result += std::wstring(domain.data()) + L"\\";
			result += std::wstring(name.data()) + L")";
		}
		return result;
	}

	// Determines the SID from supported allow ACE variants.
	PSID GetAllowedAceSid(void* ace)
	{
		const auto* header = static_cast<ACE_HEADER*>(ace);
		if (header->AceType == ACCESS_ALLOWED_ACE_TYPE ||
			header->AceType == ACCESS_ALLOWED_CALLBACK_ACE_TYPE)
			return &static_cast<ACCESS_ALLOWED_ACE*>(ace)->SidStart;
		if (header->AceType == ACCESS_ALLOWED_OBJECT_ACE_TYPE ||
			header->AceType == ACCESS_ALLOWED_CALLBACK_OBJECT_ACE_TYPE)
		{
			auto* objectAce = static_cast<ACCESS_ALLOWED_OBJECT_ACE*>(ace);
			BYTE* sid = reinterpret_cast<BYTE*>(&objectAce->ObjectType);
			if (objectAce->Flags & ACE_OBJECT_TYPE_PRESENT)
				sid += sizeof(GUID);
			if (objectAce->Flags & ACE_INHERITED_OBJECT_TYPE_PRESENT)
				sid += sizeof(GUID);
			return sid;
		}
		return nullptr;
	}

	// Determines the access mask from supported allow ACE variants.
	DWORD GetAllowedAceMask(void* ace)
	{
		const auto* header = static_cast<ACE_HEADER*>(ace);
		if (header->AceType == ACCESS_ALLOWED_ACE_TYPE ||
			header->AceType == ACCESS_ALLOWED_CALLBACK_ACE_TYPE)
			return static_cast<ACCESS_ALLOWED_ACE*>(ace)->Mask;
		if (header->AceType == ACCESS_ALLOWED_OBJECT_ACE_TYPE ||
			header->AceType == ACCESS_ALLOWED_CALLBACK_OBJECT_ACE_TYPE)
			return static_cast<ACCESS_ALLOWED_OBJECT_ACE*>(ace)->Mask;
		return 0;
	}

	// Reports unexpected owners, unprotected DACLs, and write access for foreign SIDs.
	void InspectSecurityDescriptor(const std::wstring& objectName,
		PSECURITY_DESCRIPTOR descriptor, PSID owner, PACL dacl, DWORD writeMask,
		const std::wstring& remediation, std::vector<SecurityAclFinding>& findings,
		bool german, bool requireExpectedOwner = true)
	{
		if (requireExpectedOwner &&
			(!descriptor || !owner || !IsExpectedWriter(owner)))
		{
			findings.push_back({ objectName,
				SelectAuditText(german, L"Unexpected owner: ", L"Unerwarteter Besitzer: ") +
				FormatSid(owner, german), remediation });
		}
		BOOL daclPresent = FALSE;
		BOOL daclDefaulted = FALSE;
		PACL descriptorDacl = nullptr;
		if (!descriptor || !GetSecurityDescriptorDacl(
			descriptor, &daclPresent, &descriptorDacl, &daclDefaulted) ||
			!daclPresent || !descriptorDacl)
		{
			findings.push_back({ objectName,
				SelectAuditText(german, L"Missing or unrestricted DACL.",
					L"Fehlende oder unbeschränkte DACL."), remediation });
			return;
		}
		for (DWORD index = 0; index < dacl->AceCount; index++)
		{
			void* ace = nullptr;
			if (!GetAce(dacl, index, &ace) || !ace)
				continue;
			const auto* header = static_cast<ACE_HEADER*>(ace);
			if ((header->AceFlags & INHERIT_ONLY_ACE) != 0)
				continue;
			PSID sid = GetAllowedAceSid(ace);
			if (sid && (GetAllowedAceMask(ace) & writeMask) != 0 &&
				!IsExpectedWriter(sid))
			{
				findings.push_back({ objectName,
					SelectAuditText(german, L"Unexpected write permission for ",
						L"Unerwartete Schreibberechtigung für ") + FormatSid(sid, german) + L".",
					remediation + SelectAuditText(german,
						L" In particular, remove the write ACE for ",
						L" Entfernen Sie insbesondere die Schreib-ACE für ") +
					FormatSid(sid, german) + L"." });
			}
		}
	}

	// Audits a file system object including reparse point, owner, and DACL validation.
	void AuditFileSystemObject(const std::wstring& label, const std::wstring& path,
		const std::wstring& remediation, std::vector<SecurityAclFinding>& findings,
		bool german)
	{
		const DWORD attributes = GetFileAttributesW(path.c_str());
		if (attributes == INVALID_FILE_ATTRIBUTES)
		{
			findings.push_back({ label + L": " + path,
				SelectAuditText(german, L"Object is missing or inaccessible (error ",
					L"Objekt fehlt oder ist nicht zugreifbar (Fehler ") +
				std::to_wstring(GetLastError()) + L").", remediation });
			return;
		}
		if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
		{
			findings.push_back({ label + L": " + path,
				SelectAuditText(german,
					L"The security-relevant object is a reparse point.",
					L"Das sicherheitsrelevante Objekt ist ein Reparse Point."), remediation });
		}
		PSID owner = nullptr;
		PACL dacl = nullptr;
		PSECURITY_DESCRIPTOR descriptor = nullptr;
		const DWORD status = GetNamedSecurityInfoW(const_cast<LPWSTR>(path.c_str()),
			SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
			&owner, nullptr, &dacl, nullptr, &descriptor);
		if (status != ERROR_SUCCESS)
		{
			findings.push_back({ label + L": " + path,
				SelectAuditText(german, L"ACL could not be read (error ",
					L"ACL konnte nicht gelesen werden (Fehler ") +
				std::to_wstring(status) + L").", remediation });
			return;
		}
		constexpr DWORD FileWriteMask = FILE_WRITE_DATA | FILE_APPEND_DATA |
			FILE_WRITE_EA | FILE_WRITE_ATTRIBUTES | FILE_DELETE_CHILD | DELETE |
			WRITE_DAC | WRITE_OWNER | GENERIC_WRITE | GENERIC_ALL;
		InspectSecurityDescriptor(label + L": " + path, descriptor, owner, dacl,
			FileWriteMask, remediation, findings, german);
		LocalFree(descriptor);
	}

	// Validates the fully defined owner and DACL policy of a broker store directory.
	void AuditBrokerStoreDirectory(const std::wstring& label, const std::wstring& path,
		std::vector<SecurityAclFinding>& findings, bool german)
	{
		const std::wstring repairRemediation = SelectAuditText(german,
			L"Run xucRun /showcred for BrokerStore findings or xucRun /backup-list for "
			L"BrokerBackups findings so the broker reapplies the directory security. Then run "
			L"xucRun /audit again. If the finding remains, reinstall xucRun from an elevated context.",
			L"Führen Sie bei BrokerStore-Befunden xucRun /showcred oder bei "
			L"BrokerBackups-Befunden xucRun /backup-list aus, damit der Broker die "
			L"Verzeichnisrechte erneut setzt. Prüfen Sie danach erneut mit xucRun /audit. "
			L"Bleibt der Befund bestehen, installieren Sie xucRun erhöht neu.");
		const std::wstring manualRemediation = SelectAuditText(german,
			L"Remove the unexpected object or reparse point in an elevated console, then "
			L"reinstall xucRun. The broker intentionally does not follow or repair reparse points.",
			L"Entfernen Sie das unerwartete Objekt oder den Reparse Point in einer erhöhten "
			L"Konsole und installieren Sie xucRun danach neu. Der Broker folgt oder repariert "
			L"Reparse Points absichtlich nicht.");
		BrokerStoreSecurityInspection inspection;
		DWORD error = NO_ERROR;
		if (!InspectBrokerStoreDirectorySecurity(path, inspection, error))
		{
			findings.push_back({ label + L": " + path,
				SelectAuditText(german, L"Security information could not be read (error ",
					L"Sicherheitsinformationen konnten nicht gelesen werden (Fehler ") +
				std::to_wstring(error) + L").", repairRemediation });
			return;
		}
		const std::wstring objectName = label + L": " + path;
		if (!inspection.isDirectory)
			findings.push_back({ objectName, SelectAuditText(german,
				L"The object is not a directory.", L"Das Objekt ist kein Verzeichnis."),
				manualRemediation });
		if (inspection.reparsePoint)
			findings.push_back({ objectName, SelectAuditText(german,
				L"The store directory is a reparse point.",
				L"Das Store-Verzeichnis ist ein Reparse Point."), manualRemediation });
		if (!inspection.isDirectory || inspection.reparsePoint)
			return;
		if (!inspection.ownerIsSystem)
			findings.push_back({ objectName, SelectAuditText(german,
				L"The owner is not SYSTEM.", L"Der Besitzer ist nicht SYSTEM."), repairRemediation });
		if (!inspection.daclPresent)
			findings.push_back({ objectName, SelectAuditText(german,
				L"The DACL is missing or unrestricted.",
				L"Die DACL fehlt oder ist unbeschränkt."), repairRemediation });
		if (!inspection.daclProtected)
			findings.push_back({ objectName, SelectAuditText(german,
				L"The DACL is not protected against inheritance.",
				L"Die DACL ist nicht gegen Vererbung geschützt."), repairRemediation });
		if (inspection.daclPresent && !inspection.aclEntriesMatch)
			findings.push_back({ objectName, SelectAuditText(german,
				L"The access entries do not exactly grant full access to SYSTEM and local administrators.",
				L"Die Zugriffseinträge gewähren nicht exakt SYSTEM und lokalen Administratoren Vollzugriff."),
				repairRemediation });
	}

	void AuditInstallationSignature(const std::wstring& label, const std::wstring& path,
		const std::vector<std::uint8_t>& expectedSigner,
		SecurityAclAuditCoverage* coverage, std::vector<SecurityAclFinding>& findings,
		bool german, const std::wstring& customRemediation = {})
	{
		const std::wstring remediation = customRemediation.empty()
			? SelectAuditText(german,
				L"Verify the machine certificate trust and reinstall xucRun from the signed package.",
				L"Prüfen Sie das Zertifikatvertrauen des Computers und installieren Sie xucRun "
				L"aus dem signierten Paket neu.")
			: customRemediation;
		ClientImageIdentity identity;
		DWORD error = NO_ERROR;
		SecurityAclSignatureInspection inspection;
		inspection.path = path;
		inspection.identityRead = ReadFileImageIdentity(path, identity, error, true);
		inspection.windowsError = error;
		if (inspection.identityRead)
		{
			inspection.trustStatus = identity.trustStatus;
			inspection.signatureTrusted = identity.signatureTrusted;
			inspection.fileIdentityRead = identity.fileIdentityRead;
			inspection.protectedPath = identity.protectedPath;
			inspection.pathProtectionError = identity.pathProtectionError;
			inspection.signerMatches = identity.signerCertificateSha256.size() == 32 &&
				expectedSigner.size() == 32 &&
				std::equal(identity.signerCertificateSha256.begin(),
					identity.signerCertificateSha256.end(), expectedSigner.begin());
		}
		if (coverage)
			coverage->signatureInspections.push_back(inspection);

		if (!inspection.identityRead)
		{
			findings.push_back({ label + L": " + path, SelectAuditText(german,
				L"The image identity could not be read (error ",
				L"Die Abbildidentität konnte nicht gelesen werden (Fehler ") +
				std::to_wstring(error) + L").", remediation });
		}
		else if (!inspection.fileIdentityRead || !inspection.protectedPath)
		{
			findings.push_back({ label + L": " + path, SelectAuditText(german,
				L"The file identity or protected installation path could not be verified (error ",
				L"Die Dateiidentität oder der geschützte Installationspfad konnte nicht geprüft werden (Fehler ") +
				std::to_wstring(inspection.pathProtectionError) + L").", remediation });
		}
		else if (!inspection.signatureTrusted)
		{
			findings.push_back({ label + L": " + path, SelectAuditText(german,
				L"The Authenticode signature is invalid or not trusted (status ",
				L"Die Authenticode-Signatur ist ungültig oder nicht vertrauenswürdig (Status ") +
				FormatTrustStatus(inspection.trustStatus) + L").", remediation });
		}
		else if (!inspection.signerMatches)
		{
			findings.push_back({ label + L": " + path, SelectAuditText(german,
				L"The signing certificate does not match the installed client policy.",
				L"Das Signierzertifikat entspricht nicht der installierten Clientrichtlinie."),
				remediation });
		}
	}

	void AppendTrayTaskFinding(const std::wstring& taskName,
		const std::wstring& problem, std::vector<SecurityAclFinding>& findings,
		bool german)
	{
		findings.push_back({
			SelectAuditText(german, L"Tray autostart task: ", L"Tray-Autostartaufgabe: ") +
				taskName,
			problem,
			SelectAuditText(german,
				L"Remove xucRun Tray and reinstall the matching Tray release for the intended user.",
				L"Entfernen Sie xucRun Tray und installieren Sie die passende "
				L"Tray-Veröffentlichung für den vorgesehenen Benutzer neu.")
			});
	}

	bool ResolveAccountSid(const std::wstring& account, std::vector<BYTE>& sid)
	{
		sid.clear();
		PSID stringSid = nullptr;
		if (ConvertStringSidToSidW(account.c_str(), &stringSid))
		{
			const DWORD size = GetLengthSid(stringSid);
			sid.resize(size);
			const bool copied = CopySid(size, sid.data(), stringSid) == TRUE;
			LocalFree(stringSid);
			if (!copied)
				sid.clear();
			return copied;
		}

		DWORD sidSize = 0;
		DWORD domainSize = 0;
		SID_NAME_USE use = SidTypeUnknown;
		LookupAccountNameW(nullptr, account.c_str(), nullptr, &sidSize,
			nullptr, &domainSize, &use);
		if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || sidSize == 0)
			return false;
		sid.resize(sidSize);
		std::vector<wchar_t> domain(domainSize);
		if (!LookupAccountNameW(nullptr, account.c_str(), sid.data(), &sidSize,
			domain.empty() ? nullptr : domain.data(), &domainSize, &use))
		{
			sid.clear();
			return false;
		}
		return IsValidSid(sid.data()) == TRUE;
	}

	bool AreSameAccountIdentity(
		const std::wstring& left, const std::wstring& right)
	{
		std::vector<BYTE> leftSid;
		std::vector<BYTE> rightSid;
		return ResolveAccountSid(left, leftSid) &&
			ResolveAccountSid(right, rightSid) &&
			EqualSid(leftSid.data(), rightSid.data()) == TRUE;
	}

	bool IsExpectedTrayTaskPrincipal(const std::wstring& taskName,
		const std::wstring& principalUser, TASK_LOGON_TYPE logonType,
		TASK_RUNLEVEL_TYPE runLevel)
	{
		const std::size_t prefixLength = std::size(TrayTaskPrefix) - 1;
		return taskName.size() > prefixLength &&
			_wcsnicmp(taskName.c_str(), TrayTaskPrefix, prefixLength) == 0 &&
			AreSameAccountIdentity(
				taskName.substr(prefixLength), principalUser) &&
			logonType == TASK_LOGON_INTERACTIVE_TOKEN &&
			runLevel == TASK_RUNLEVEL_LUA;
	}

	bool IsExpectedTrayTaskAction(const std::wstring& actionPath,
		const std::wstring& arguments, const std::wstring& workingDirectory,
		const std::wstring& trayPath)
	{
		return _wcsicmp(actionPath.c_str(), trayPath.c_str()) == 0 &&
			arguments.empty() && workingDirectory.empty();
	}

	void AuditTrayTaskSecurity(IRegisteredTask* task, const std::wstring& taskName,
		std::vector<SecurityAclFinding>& findings, bool german)
	{
		LocalBstr securityDescriptor;
		const HRESULT status = task->GetSecurityDescriptor(
			OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
			securityDescriptor.Put());
		if (FAILED(status))
		{
			AppendTrayTaskFinding(taskName, SelectAuditText(german,
				L"The task security descriptor could not be read.",
				L"Der Sicherheitsdeskriptor der Aufgabe konnte nicht gelesen werden."),
				findings, german);
			return;
		}

		PSECURITY_DESCRIPTOR descriptor = nullptr;
		if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
			securityDescriptor.Get(), SDDL_REVISION_1, &descriptor, nullptr))
		{
			AppendTrayTaskFinding(taskName, SelectAuditText(german,
				L"The task security descriptor is invalid.",
				L"Der Sicherheitsdeskriptor der Aufgabe ist ungültig."),
				findings, german);
			return;
		}
		PSID owner = nullptr;
		BOOL ownerDefaulted = FALSE;
		PACL dacl = nullptr;
		BOOL daclPresent = FALSE;
		BOOL daclDefaulted = FALSE;
		if (!GetSecurityDescriptorOwner(
			descriptor, &owner, &ownerDefaulted) ||
			!GetSecurityDescriptorDacl(
				descriptor, &daclPresent, &dacl, &daclDefaulted) ||
			!daclPresent)
		{
			LocalFree(descriptor);
			AppendTrayTaskFinding(taskName, SelectAuditText(german,
				L"The task security descriptor is incomplete.",
				L"Der Sicherheitsdeskriptor der Aufgabe ist unvollständig."),
				findings, german);
			return;
		}
		constexpr DWORD TaskWriteMask = FILE_WRITE_DATA | FILE_APPEND_DATA |
			FILE_WRITE_EA | FILE_WRITE_ATTRIBUTES | DELETE | WRITE_DAC |
			WRITE_OWNER | GENERIC_WRITE | GENERIC_ALL;
		InspectSecurityDescriptor(
			SelectAuditText(german, L"Tray autostart task: ", L"Tray-Autostartaufgabe: ") +
				taskName,
			descriptor, owner, dacl, TaskWriteMask,
			SelectAuditText(german,
				L"Remove xucRun Tray and reinstall the matching Tray release.",
				L"Entfernen Sie xucRun Tray und installieren Sie die passende "
				L"Tray-Veröffentlichung neu."),
			findings, german, false);
		LocalFree(descriptor);
	}

	void AuditTrayTaskDefinition(IRegisteredTask* task, const std::wstring& taskName,
		const std::wstring& trayPath, std::vector<SecurityAclFinding>& findings,
		bool german)
	{
		VARIANT_BOOL enabled = VARIANT_FALSE;
		if (FAILED(task->get_Enabled(&enabled)) || enabled != VARIANT_TRUE)
		{
			AppendTrayTaskFinding(taskName, SelectAuditText(german,
				L"The task is disabled or its state could not be read.",
				L"Die Aufgabe ist deaktiviert oder ihr Status konnte nicht gelesen werden."),
				findings, german);
		}

		ComPtr<ITaskDefinition> definition;
		if (FAILED(task->get_Definition(definition.Put())))
		{
			AppendTrayTaskFinding(taskName, SelectAuditText(german,
				L"The task definition could not be read.",
				L"Die Aufgabendefinition konnte nicht gelesen werden."),
				findings, german);
			return;
		}

		std::wstring principalUser;
		ComPtr<IPrincipal> principal;
		TASK_LOGON_TYPE logonType = TASK_LOGON_NONE;
		TASK_RUNLEVEL_TYPE runLevel = TASK_RUNLEVEL_HIGHEST;
		LocalBstr principalUserId;
		if (FAILED(definition->get_Principal(principal.Put())) ||
			FAILED(principal->get_UserId(principalUserId.Put())) ||
			FAILED(principal->get_LogonType(&logonType)) ||
			FAILED(principal->get_RunLevel(&runLevel)))
		{
			AppendTrayTaskFinding(taskName, SelectAuditText(german,
				L"The task principal could not be read.",
				L"Der Aufgabenprinzipal konnte nicht gelesen werden."),
				findings, german);
		}
		else
		{
			principalUser = principalUserId.ToString();
			if (!IsExpectedTrayTaskPrincipal(
				taskName, principalUser, logonType, runLevel))
			{
				AppendTrayTaskFinding(taskName, SelectAuditText(german,
					L"The task is not bound to its named interactive user with limited privileges.",
					L"Die Aufgabe ist nicht an den in ihrem Namen angegebenen interaktiven "
					L"Benutzer mit eingeschränkten Rechten gebunden."),
					findings, german);
			}
		}

		ComPtr<IActionCollection> actions;
		LONG actionCount = 0;
		ComPtr<IAction> action;
		TASK_ACTION_TYPE actionType = TASK_ACTION_COM_HANDLER;
		ComPtr<IExecAction> execAction;
		LocalBstr actionPath;
		LocalBstr arguments;
		LocalBstr workingDirectory;
		if (FAILED(definition->get_Actions(actions.Put())) ||
			FAILED(actions->get_Count(&actionCount)) || actionCount != 1 ||
			FAILED(actions->get_Item(1, action.Put())) ||
			FAILED(action->get_Type(&actionType)) || actionType != TASK_ACTION_EXEC ||
			FAILED(action->QueryInterface(
				__uuidof(IExecAction), reinterpret_cast<void**>(execAction.Put()))) ||
			FAILED(execAction->get_Path(actionPath.Put())) ||
			FAILED(execAction->get_Arguments(arguments.Put())) ||
			FAILED(execAction->get_WorkingDirectory(workingDirectory.Put())) ||
			!IsExpectedTrayTaskAction(actionPath.ToString(), arguments.ToString(),
				workingDirectory.ToString(), trayPath))
		{
			AppendTrayTaskFinding(taskName, SelectAuditText(german,
				L"The task action does not exclusively start the installed xucRunTray.exe.",
				L"Die Aufgabenaktion startet nicht ausschließlich die installierte "
				L"xucRunTray.exe."),
				findings, german);
		}

		ComPtr<ITriggerCollection> triggers;
		LONG triggerCount = 0;
		ComPtr<ITrigger> trigger;
		TASK_TRIGGER_TYPE2 triggerType = TASK_TRIGGER_EVENT;
		ComPtr<ILogonTrigger> logonTrigger;
		LocalBstr triggerUserId;
		if (FAILED(definition->get_Triggers(triggers.Put())) ||
			FAILED(triggers->get_Count(&triggerCount)) || triggerCount != 1 ||
			FAILED(triggers->get_Item(1, trigger.Put())) ||
			FAILED(trigger->get_Type(&triggerType)) || triggerType != TASK_TRIGGER_LOGON ||
			FAILED(trigger->QueryInterface(
				__uuidof(ILogonTrigger), reinterpret_cast<void**>(logonTrigger.Put()))) ||
			FAILED(logonTrigger->get_UserId(triggerUserId.Put())) ||
			principalUser.empty() ||
			!AreSameAccountIdentity(triggerUserId.ToString(), principalUser))
		{
			AppendTrayTaskFinding(taskName, SelectAuditText(german,
				L"The task does not contain exactly one matching user logon trigger.",
				L"Die Aufgabe enthält nicht genau einen passenden Benutzeranmeldetrigger."),
				findings, german);
		}
	}

	void AuditTrayScheduledTasks(const std::wstring& trayPath,
		std::vector<SecurityAclFinding>& findings, bool german)
	{
		const HRESULT initialization = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		const bool uninitialize = SUCCEEDED(initialization);
		if (FAILED(initialization) && initialization != RPC_E_CHANGED_MODE)
		{
			AppendTrayTaskFinding(L"\\xucRun", SelectAuditText(german,
				L"The Task Scheduler audit could not initialize COM.",
				L"Das Aufgabenplanungs-Audit konnte COM nicht initialisieren."),
				findings, german);
			return;
		}

		ComPtr<ITaskService> service;
		HRESULT status = CoCreateInstance(__uuidof(TaskScheduler), nullptr,
			CLSCTX_INPROC_SERVER, __uuidof(ITaskService),
			reinterpret_cast<void**>(service.Put()));
		VARIANT empty;
		VariantInit(&empty);
		if (SUCCEEDED(status))
			status = service->Connect(empty, empty, empty, empty);
		ComPtr<ITaskFolder> folder;
		LocalBstr folderPath(TrayTaskFolder);
		if (SUCCEEDED(status))
			status = service->GetFolder(folderPath.Get(), folder.Put());
		ComPtr<IRegisteredTaskCollection> tasks;
		if (SUCCEEDED(status))
			status = folder->GetTasks(TASK_ENUM_HIDDEN, tasks.Put());
		LONG taskCount = 0;
		if (SUCCEEDED(status))
			status = tasks->get_Count(&taskCount);
		if (FAILED(status))
		{
			AppendTrayTaskFinding(L"\\xucRun", SelectAuditText(german,
				L"The Tray task folder could not be read.",
				L"Der Tray-Aufgabenordner konnte nicht gelesen werden."),
				findings, german);
			if (uninitialize)
			{
				tasks.Reset();
				folder.Reset();
				service.Reset();
				CoUninitialize();
			}
			return;
		}

		LONG trayTaskCount = 0;
		for (LONG index = 1; index <= taskCount; ++index)
		{
			VARIANT itemIndex;
			VariantInit(&itemIndex);
			itemIndex.vt = VT_I4;
			itemIndex.lVal = index;
			ComPtr<IRegisteredTask> task;
			if (FAILED(tasks->get_Item(itemIndex, task.Put())))
				continue;
			LocalBstr name;
			if (FAILED(task->get_Name(name.Put())))
				continue;
			const std::wstring taskName = name.ToString();
			if (taskName.size() < std::size(TrayTaskPrefix) - 1 ||
				_wcsnicmp(taskName.c_str(), TrayTaskPrefix,
					std::size(TrayTaskPrefix) - 1) != 0)
			{
				continue;
			}
			++trayTaskCount;
			AuditTrayTaskSecurity(task.operator->(), taskName, findings, german);
			AuditTrayTaskDefinition(
				task.operator->(), taskName, trayPath, findings, german);
		}
		if (trayTaskCount == 0)
		{
			AppendTrayTaskFinding(L"\\xucRun\\BrokerStatus-*",
				SelectAuditText(german,
					L"No Tray autostart task is registered.",
					L"Es ist keine Tray-Autostartaufgabe registriert."),
				findings, german);
		}
		if (uninitialize)
		{
			tasks.Reset();
			folder.Reset();
			service.Reset();
			CoUninitialize();
		}
	}

	bool InspectBrokerServiceAccount(std::wstring& account, bool& isLocalSystem,
		DWORD& error)
	{
		account.clear();
		isLocalSystem = false;
		error = NO_ERROR;
		SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
		if (!manager)
		{
			error = GetLastError();
			return false;
		}
		SC_HANDLE service = OpenServiceW(manager, xucrun::broker::BrokerServiceName,
			SERVICE_QUERY_CONFIG);
		if (!service)
		{
			error = GetLastError();
			CloseServiceHandle(manager);
			return false;
		}

		DWORD size = 0;
		QueryServiceConfigW(service, nullptr, 0, &size);
		if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || size == 0)
		{
			error = GetLastError();
			CloseServiceHandle(service);
			CloseServiceHandle(manager);
			return false;
		}
		std::vector<BYTE> buffer(size);
		auto* configuration = reinterpret_cast<QUERY_SERVICE_CONFIGW*>(buffer.data());
		if (!QueryServiceConfigW(service, configuration, size, &size) ||
			!configuration->lpServiceStartName)
		{
			error = GetLastError();
			if (error == NO_ERROR)
				error = ERROR_INVALID_DATA;
			CloseServiceHandle(service);
			CloseServiceHandle(manager);
			return false;
		}
		account = configuration->lpServiceStartName;
		CloseServiceHandle(service);
		CloseServiceHandle(manager);
		isLocalSystem = _wcsicmp(account.c_str(), L"LocalSystem") == 0;
		return true;
	}

	void AuditCredentialStoreContext(SecurityAclAuditCoverage* coverage,
		std::vector<SecurityAclFinding>& findings, bool german)
	{
		SecurityAclAuditCoverage::CredentialStoreContext inspection;
		xucrun::broker_store::AccessContextInspection accessContext;
		inspection.contextInspected =
			xucrun::broker_store::InspectAccessContext(accessContext, inspection.contextError);
		if (inspection.contextInspected)
		{
			inspection.processIsLocalSystem = accessContext.processIsLocalSystem;
			inspection.noThreadImpersonation = accessContext.noThreadImpersonation;
			inspection.accessPermitted = accessContext.accessPermitted;
		}
		inspection.serviceConfigurationRead = InspectBrokerServiceAccount(
			inspection.serviceAccount, inspection.serviceAccountIsLocalSystem,
			inspection.serviceError);
		if (coverage)
			coverage->credentialStoreContext = inspection;

		const std::wstring remediation = SelectAuditText(german,
			L"Reinstall xucRun from an elevated context and restart the broker service.",
			L"Installieren Sie xucRun erhöht neu und starten Sie den Brokerdienst neu.");
		if (!inspection.contextInspected)
		{
			findings.push_back({ L"Credential store context", SelectAuditText(german,
				L"The broker security context could not be inspected (error ",
				L"Der Sicherheitskontext des Brokers konnte nicht geprüft werden (Fehler ") +
				std::to_wstring(inspection.contextError) + L").", remediation });
		}
		else
		{
			if (!inspection.processIsLocalSystem)
				findings.push_back({ L"Credential store context", SelectAuditText(german,
					L"The broker process is not running as LocalSystem.",
					L"Der Brokerprozess läuft nicht als LocalSystem."), remediation });
			if (!inspection.noThreadImpersonation)
				findings.push_back({ L"Credential store context", SelectAuditText(german,
					L"The audit thread is impersonating another security context.",
					L"Der Audit-Thread verwendet einen impersonierten Sicherheitskontext."),
					remediation });
		}
		if (!inspection.serviceConfigurationRead)
		{
			findings.push_back({ L"Broker service account", SelectAuditText(german,
				L"The configured service account could not be read (error ",
				L"Das konfigurierte Dienstkonto konnte nicht gelesen werden (Fehler ") +
				std::to_wstring(inspection.serviceError) + L").", remediation });
		}
		else if (!inspection.serviceAccountIsLocalSystem)
		{
			findings.push_back({ L"Broker service account", SelectAuditText(german,
				L"The service is not configured to run as LocalSystem: ",
				L"Der Dienst ist nicht für LocalSystem konfiguriert: ") +
				inspection.serviceAccount + L".", remediation });
		}
	}

	// Audits the owner and write permissions of a security-relevant registry key.
	void AuditRegistryKey(const std::wstring& label, const wchar_t* subKey,
		std::vector<SecurityAclFinding>& findings, bool german,
		const std::wstring& customRemediation = {})
	{
		const std::wstring remediation = customRemediation.empty()
			? SelectAuditText(german,
				L"Remove foreign write permissions with Get-Acl/Set-Acl in an elevated "
				L"PowerShell, then run Install-xucRun.ps1 again.",
				L"Entfernen Sie fremde Schreibrechte mit Get-Acl/Set-Acl in einer erhöhten "
				L"PowerShell und führen Sie anschließend Install-xucRun.ps1 erneut aus.")
			: customRemediation;
		HKEY key = nullptr;
		const LONG openStatus = RegOpenKeyExW(HKEY_LOCAL_MACHINE, subKey, 0,
			READ_CONTROL, &key);
		if (openStatus != ERROR_SUCCESS)
		{
			findings.push_back({ label, SelectAuditText(german,
				L"ACL could not be read (error ", L"ACL konnte nicht gelesen werden (Fehler ") +
				std::to_wstring(openStatus) + L").", remediation });
			return;
		}
		PSID owner = nullptr;
		PACL dacl = nullptr;
		PSECURITY_DESCRIPTOR descriptor = nullptr;
		const DWORD status = GetSecurityInfo(key, SE_REGISTRY_KEY,
			OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
			&owner, nullptr, &dacl, nullptr, &descriptor);
		RegCloseKey(key);
		if (status != ERROR_SUCCESS)
		{
			findings.push_back({ label, SelectAuditText(german,
				L"ACL could not be read (error ", L"ACL konnte nicht gelesen werden (Fehler ") +
				std::to_wstring(status) + L").", remediation });
			return;
		}
		constexpr DWORD RegistryWriteMask = KEY_SET_VALUE | KEY_CREATE_SUB_KEY |
			DELETE | WRITE_DAC | WRITE_OWNER | GENERIC_WRITE | GENERIC_ALL;
		InspectSecurityDescriptor(label, descriptor, owner, dacl,
			RegistryWriteMask, remediation, findings, german);
		LocalFree(descriptor);
	}
}

// Enforces the audit's privileged-owner, no-reparse, and writer policy on an open object.
bool IsFileSystemObjectProtectedForBrokerAuthorization(
	HANDLE object, DWORD unexpectedWriteMask, DWORD& error)
{
	error = ERROR_ACCESS_DENIED;
	FILE_ATTRIBUTE_TAG_INFO attributes = {};
	if (!object || object == INVALID_HANDLE_VALUE ||
		!GetFileInformationByHandleEx(object, FileAttributeTagInfo,
			&attributes, sizeof(attributes)))
	{
		error = GetLastError();
		return false;
	}
	if ((attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
	{
		error = ERROR_REPARSE_TAG_INVALID;
		return false;
	}
	PSID owner = nullptr;
	PACL dacl = nullptr;
	PSECURITY_DESCRIPTOR descriptor = nullptr;
	const DWORD status = GetSecurityInfo(object, SE_FILE_OBJECT,
		OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
		&owner, nullptr, &dacl, nullptr, &descriptor);
	if (status != ERROR_SUCCESS)
	{
		error = status;
		return false;
	}
	bool protectedObject = IsExpectedWriter(owner) && dacl != nullptr;
	GENERIC_MAPPING mapping = {
		FILE_GENERIC_READ, FILE_GENERIC_WRITE, FILE_GENERIC_EXECUTE, FILE_ALL_ACCESS
	};
	for (DWORD index = 0; protectedObject && index < dacl->AceCount; ++index)
	{
		void* ace = nullptr;
		if (!GetAce(dacl, index, &ace) || !ace)
		{
			protectedObject = false;
			break;
		}
		const auto* header = static_cast<const ACE_HEADER*>(ace);
		if ((header->AceFlags & INHERIT_ONLY_ACE) != 0)
			continue;
		const bool allowedAce = header->AceType == ACCESS_ALLOWED_ACE_TYPE ||
			header->AceType == ACCESS_ALLOWED_OBJECT_ACE_TYPE ||
			header->AceType == ACCESS_ALLOWED_CALLBACK_ACE_TYPE ||
			header->AceType == ACCESS_ALLOWED_CALLBACK_OBJECT_ACE_TYPE;
		const bool knownNonGrantingAce =
			header->AceType == ACCESS_DENIED_ACE_TYPE ||
			header->AceType == ACCESS_DENIED_OBJECT_ACE_TYPE ||
			header->AceType == ACCESS_DENIED_CALLBACK_ACE_TYPE ||
			header->AceType == ACCESS_DENIED_CALLBACK_OBJECT_ACE_TYPE ||
			header->AceType == SYSTEM_AUDIT_ACE_TYPE ||
			header->AceType == SYSTEM_AUDIT_OBJECT_ACE_TYPE ||
			header->AceType == SYSTEM_AUDIT_CALLBACK_ACE_TYPE ||
			header->AceType == SYSTEM_AUDIT_CALLBACK_OBJECT_ACE_TYPE ||
			header->AceType == SYSTEM_MANDATORY_LABEL_ACE_TYPE ||
			header->AceType == SYSTEM_RESOURCE_ATTRIBUTE_ACE_TYPE ||
			header->AceType == SYSTEM_SCOPED_POLICY_ID_ACE_TYPE ||
			header->AceType == SYSTEM_PROCESS_TRUST_LABEL_ACE_TYPE ||
			header->AceType == SYSTEM_ACCESS_FILTER_ACE_TYPE;
		if (!allowedAce && !knownNonGrantingAce)
		{
			protectedObject = false;
			break;
		}
		if (!allowedAce)
			continue;
		PSID sid = GetAllowedAceSid(ace);
		DWORD mask = GetAllowedAceMask(ace);
		MapGenericMask(&mask, &mapping);
		if (!sid || !IsValidSid(sid) ||
			((mask & unexpectedWriteMask) != 0 &&
			!IsExpectedWriter(sid)))
		{
			protectedObject = false;
		}
	}
	LocalFree(descriptor);
	error = protectedObject ? NO_ERROR : ERROR_ACCESS_DENIED;
	return protectedObject;
}

// Audits all broker files, configuration keys, and store directories for unexpected writers.
std::vector<SecurityAclFinding> AuditBrokerSecurityAcls(bool german,
	SecurityAclAuditCoverage* coverage, bool inspectCredentialStoreContext)
{
	std::vector<SecurityAclFinding> findings;
	if (inspectCredentialStoreContext)
		AuditCredentialStoreContext(coverage, findings, german);
	std::wstring brokerImagePath;
	wchar_t brokerPath[32768] = {};
	const DWORD brokerLength = GetModuleFileNameW(
		nullptr, brokerPath, static_cast<DWORD>(std::size(brokerPath)));
	if (brokerLength == 0 || brokerLength == std::size(brokerPath))
	{
		findings.push_back({ L"Broker", SelectAuditText(german,
			L"Installation path could not be determined.",
			L"Installationspfad konnte nicht ermittelt werden."), SelectAuditText(german,
			L"Reinstall xucRun with Install-xucRun.ps1.",
			L"Installieren Sie xucRun mit Install-xucRun.ps1 neu.") });
	}
	else
	{
		const std::wstring path = brokerPath;
		brokerImagePath = path;
		const std::wstring directory = path.substr(0, path.find_last_of(L"\\/"));
		const std::wstring remediation =
			SelectAuditText(german,
				L"Run Install-xucRun.ps1 again in an elevated PowerShell.",
				L"Führen Sie Install-xucRun.ps1 in einer erhöhten PowerShell erneut aus.");
		if (coverage)
		{
			AppendUniquePath(coverage->installationObjects, directory);
			AppendUniquePath(coverage->installationObjects, path);
		}
		AuditFileSystemObject(SelectAuditText(german, L"Broker directory", L"Brokerverzeichnis"),
			directory, remediation, findings, german);
		AuditFileSystemObject(SelectAuditText(german, L"Broker file", L"Brokerdatei"),
			path, remediation, findings, german);
	}

	SensitiveClientPolicy clientPolicy;
	DWORD policyError = NO_ERROR;
	if (!LoadSensitiveClientPolicy(clientPolicy, policyError))
	{
		findings.push_back({ SelectAuditText(german, L"Authorized client", L"Autorisierter Client"),
			SelectAuditText(german,
				L"Client path could not be read from the service configuration (error ",
				L"Clientpfad konnte nicht aus der Dienstkonfiguration gelesen werden (Fehler ") +
				std::to_wstring(policyError) + L").",
			SelectAuditText(german,
				L"Run Install-xucRun.ps1 again with -ClientBinaryPath.",
				L"Führen Sie Install-xucRun.ps1 mit -ClientBinaryPath erneut aus.") });
	}
	else
	{
		const std::wstring directory = clientPolicy.imagePath.substr(
			0, clientPolicy.imagePath.find_last_of(L"\\/"));
		const std::wstring remediation = SelectAuditText(german,
			L"Remove foreign write permissions with icacls, then deploy the signed client again.",
			L"Entfernen Sie fremde Schreibrechte mit icacls und deployen Sie den signierten Client anschließend erneut.");
		if (coverage)
		{
			AppendUniquePath(coverage->installationObjects, directory);
			AppendUniquePath(coverage->installationObjects, clientPolicy.imagePath);
		}
		AuditFileSystemObject(SelectAuditText(german, L"Client directory", L"Clientverzeichnis"),
			directory, remediation, findings, german);
		AuditFileSystemObject(SelectAuditText(german, L"Client file", L"Clientdatei"),
			clientPolicy.imagePath, remediation, findings, german);
		if (!brokerImagePath.empty())
		{
			AuditInstallationSignature(SelectAuditText(german,
				L"Broker file certificate", L"Zertifikat der Brokerdatei"), brokerImagePath,
				clientPolicy.signerCertificateSha256, coverage, findings, german);
		}
		AuditInstallationSignature(SelectAuditText(german,
			L"Client file certificate", L"Zertifikat der Clientdatei"),
			clientPolicy.imagePath, clientPolicy.signerCertificateSha256,
			coverage, findings, german);

		const std::wstring trayPath = directory + L"\\xucRunTray.exe";
		HKEY trayUninstallKey = nullptr;
		const bool trayRegistryPresent = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
			TrayUninstallRegistryPath, 0, KEY_READ, &trayUninstallKey) == ERROR_SUCCESS;
		if (trayUninstallKey)
			RegCloseKey(trayUninstallKey);
		if (trayRegistryPresent ||
			GetFileAttributesW(trayPath.c_str()) != INVALID_FILE_ATTRIBUTES)
		{
			const std::wstring languageDirectory = directory + L"\\Languages";
			const std::array<std::pair<std::wstring, bool>, 4> trayFiles = {
				std::pair{ trayPath, true },
				std::pair{ languageDirectory + L"\\xucRunTray.de-DE.dll", true },
				std::pair{ languageDirectory + L"\\xucRunTray.en-US.dll", true },
				std::pair{ directory + L"\\Install-xucRunTray.ps1", false }
			};
			const std::wstring trayRemediation = SelectAuditText(german,
				L"Remove xucRun Tray and reinstall the matching Tray release.",
				L"Entfernen Sie xucRun Tray und installieren Sie die passende "
				L"Tray-Veröffentlichung neu.");
			if (coverage)
				AppendUniquePath(coverage->installationObjects, languageDirectory);
			AuditFileSystemObject(SelectAuditText(german,
				L"Tray language directory", L"Tray-Sprachverzeichnis"),
				languageDirectory, trayRemediation, findings, german);
			for (const auto& [path, signedImage] : trayFiles)
			{
				if (coverage)
					AppendUniquePath(coverage->installationObjects, path);
				AuditFileSystemObject(SelectAuditText(german,
					L"Tray file", L"Tray-Datei"), path,
					trayRemediation, findings, german);
				if (signedImage)
				{
					AuditInstallationSignature(SelectAuditText(german,
						L"Tray file certificate", L"Zertifikat der Tray-Datei"),
						path, clientPolicy.signerCertificateSha256,
						coverage, findings, german, trayRemediation);
				}
			}
			if (trayRegistryPresent)
			{
				AuditRegistryKey(SelectAuditText(german,
					L"Tray uninstall registration", L"Tray-Deinstallationsregistrierung"),
					TrayUninstallRegistryPath, findings, german, trayRemediation);
			}
			AuditTrayScheduledTasks(trayPath, findings, german);
		}
	}

	AuditRegistryKey(SelectAuditText(german, L"Service configuration", L"Dienstkonfiguration"),
		xucrun::broker::BrokerServiceRegistryPath.c_str(), findings, german);
	AuditRegistryKey(SelectAuditText(german, L"Broker parameters", L"Brokerparameter"),
		xucrun::broker::BrokerServiceParametersRegistryPath.c_str(), findings, german);

	PWSTR programData = nullptr;
	if (SUCCEEDED(SHGetKnownFolderPath(
		FOLDERID_ProgramData, KF_FLAG_DEFAULT, nullptr, &programData)))
	{
		const std::wstring applicationDirectory =
			std::wstring(programData) + L"\\xucRun";
		const std::wstring storeRoot = applicationDirectory + L"\\BrokerStore";
		const std::wstring backupRoot = applicationDirectory + L"\\BrokerBackups";
		CoTaskMemFree(programData);
		if (GetFileAttributesW(applicationDirectory.c_str()) != INVALID_FILE_ATTRIBUTES)
		{
			if (coverage)
				AppendUniquePath(coverage->storeDirectories, applicationDirectory);
			AuditBrokerStoreDirectory(L"xucRun ProgramData", applicationDirectory,
				findings, german);
			if (GetFileAttributesW(storeRoot.c_str()) != INVALID_FILE_ATTRIBUTES)
			{
				if (coverage)
					AppendUniquePath(coverage->storeDirectories, storeRoot);
				AuditBrokerStoreDirectory(L"Brokerstore", storeRoot,
					findings, german);
				const std::wstring versionDirectory = storeRoot + L"\\v1";
				if (GetFileAttributesW(versionDirectory.c_str()) != INVALID_FILE_ATTRIBUTES)
				{
					if (coverage)
						AppendUniquePath(coverage->storeDirectories, versionDirectory);
					AuditBrokerStoreDirectory(L"Brokerstore v1", versionDirectory,
						findings, german);
				}
			}
			if (GetFileAttributesW(backupRoot.c_str()) != INVALID_FILE_ATTRIBUTES)
			{
				if (coverage)
					AppendUniquePath(coverage->storeDirectories, backupRoot);
				AuditBrokerStoreDirectory(L"Broker backups", backupRoot,
					findings, german);
				const std::wstring backupVersionName =
					xucrun::backup::GetArchiveVersionDirectoryName();
				const std::wstring backupVersionDirectory =
					backupRoot + L"\\" + backupVersionName;
				if (GetFileAttributesW(backupVersionDirectory.c_str()) !=
					INVALID_FILE_ATTRIBUTES)
				{
					if (coverage)
						AppendUniquePath(coverage->storeDirectories,
							backupVersionDirectory);
					AuditBrokerStoreDirectory(L"Broker backups " + backupVersionName,
						backupVersionDirectory, findings, german);
				}
			}
		}
	}
	else
	{
		findings.push_back({ L"Brokerstore",
			SelectAuditText(german, L"ProgramData path could not be determined.",
				L"ProgramData-Pfad konnte nicht ermittelt werden."),
			SelectAuditText(german,
				L"Check the Windows configuration and restart the service.",
				L"Prüfen Sie die Windows-Konfiguration und starten Sie den Dienst neu.") });
	}
	return findings;
}

// Tests ACL evaluation against secure and intentionally insecure test descriptors.
bool RunSecurityAclAuditSelfTest()
{
	const std::wstring testSid = L"S-1-5-21-1-2-3-1001";
	const std::wstring testTaskName = std::wstring(TrayTaskPrefix) + testSid;
	const std::wstring testTrayPath = L"C:\\Program Files\\xucRun\\xucRunTray.exe";
	if (!IsExpectedTrayTaskPrincipal(testTaskName, testSid,
			TASK_LOGON_INTERACTIVE_TOKEN, TASK_RUNLEVEL_LUA) ||
		IsExpectedTrayTaskPrincipal(testTaskName, testSid,
			TASK_LOGON_INTERACTIVE_TOKEN, TASK_RUNLEVEL_HIGHEST) ||
		IsExpectedTrayTaskPrincipal(testTaskName, L"S-1-5-21-1-2-3-1002",
			TASK_LOGON_INTERACTIVE_TOKEN, TASK_RUNLEVEL_LUA) ||
		!IsExpectedTrayTaskAction(
			testTrayPath, L"", L"", testTrayPath) ||
		IsExpectedTrayTaskAction(
			L"C:\\Temp\\xucRunTray.exe", L"", L"", testTrayPath) ||
		IsExpectedTrayTaskAction(
			testTrayPath, L"--unexpected", L"", testTrayPath))
	{
		return false;
	}

	struct DescriptorTestCase
	{
		const wchar_t* sddl;
		size_t expectedFindingCount;
		const wchar_t* expectedProblem;
	};
	const DescriptorTestCase descriptorTests[] = {
		{ L"O:SYD:P(A;;GA;;;SY)(A;;GA;;;BA)", 0, nullptr },
		{ L"O:WDD:P(A;;GA;;;SY)(A;;GA;;;BA)", 1, L"Unexpected owner" },
		{ L"O:SY", 1, L"Missing or unrestricted DACL" },
		{ L"O:SYD:P(A;;GW;;;WD)", 1, L"Unexpected write permission" },
		{ L"O:SYD:P(A;IO;GW;;;WD)", 0, nullptr }
	};
	constexpr DWORD TestWriteMask = FILE_WRITE_DATA | FILE_APPEND_DATA |
		FILE_WRITE_EA | FILE_WRITE_ATTRIBUTES | FILE_DELETE_CHILD | DELETE |
		WRITE_DAC | WRITE_OWNER | GENERIC_WRITE | GENERIC_ALL;
	for (const DescriptorTestCase& test : descriptorTests)
	{
		PSECURITY_DESCRIPTOR descriptor = nullptr;
		if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
			test.sddl, SDDL_REVISION_1, &descriptor, nullptr))
		{
			return false;
		}
		PSID owner = nullptr;
		BOOL ownerDefaulted = FALSE;
		PACL dacl = nullptr;
		BOOL daclPresent = FALSE;
		BOOL daclDefaulted = FALSE;
		const bool descriptorRead =
			GetSecurityDescriptorOwner(descriptor, &owner, &ownerDefaulted) == TRUE &&
			GetSecurityDescriptorDacl(
				descriptor, &daclPresent, &dacl, &daclDefaulted) == TRUE;
		std::vector<SecurityAclFinding> descriptorFindings;
		if (descriptorRead)
		{
			InspectSecurityDescriptor(L"ACL self-test", descriptor, owner, dacl,
				TestWriteMask, L"Self-test remediation", descriptorFindings, false);
		}
		LocalFree(descriptor);
		if (!descriptorRead || descriptorFindings.size() != test.expectedFindingCount ||
			(test.expectedProblem && (descriptorFindings.empty() ||
				descriptorFindings.front().problem.find(test.expectedProblem) ==
					std::wstring::npos)))
		{
			return false;
		}
	}

	wchar_t temporaryDirectory[MAX_PATH] = {};
	wchar_t temporaryFile[MAX_PATH] = {};
	if (!GetTempPathW(static_cast<DWORD>(std::size(temporaryDirectory)),
		temporaryDirectory) ||
		!GetTempFileNameW(temporaryDirectory, L"xua", 0, temporaryFile))
	{
		return false;
	}

	PSECURITY_DESCRIPTOR descriptor = nullptr;
	if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
		L"D:P(A;;GA;;;WD)", SDDL_REVISION_1, &descriptor, nullptr))
	{
		DeleteFileW(temporaryFile);
		return false;
	}
	BOOL daclPresent = FALSE;
	BOOL daclDefaulted = FALSE;
	PACL dacl = nullptr;
	const bool descriptorRead = GetSecurityDescriptorDacl(
		descriptor, &daclPresent, &dacl, &daclDefaulted) == TRUE &&
		daclPresent && dacl;
	const DWORD setStatus = descriptorRead
		? SetNamedSecurityInfoW(temporaryFile, SE_FILE_OBJECT,
			DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
			nullptr, nullptr, dacl, nullptr)
		: ERROR_INVALID_SECURITY_DESCR;
	LocalFree(descriptor);
	if (setStatus != ERROR_SUCCESS)
	{
		DeleteFileW(temporaryFile);
		return false;
	}

	std::vector<SecurityAclFinding> findings;
	AuditFileSystemObject(L"ACL-Selbsttest", temporaryFile,
		L"Selbsttest", findings, true);
	const bool removed = DeleteFileW(temporaryFile) == TRUE;
	return removed && std::any_of(findings.begin(), findings.end(),
		[](const SecurityAclFinding& finding) {
			return finding.problem.find(L"Unerwartete Schreibberechtigung") !=
				std::wstring::npos && !finding.remediation.empty();
		});
}