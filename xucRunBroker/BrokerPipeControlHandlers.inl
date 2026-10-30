// Purpose: Internal request-handler implementation fragment for BrokerPipeServer.cpp.
// Approach: Provides implementation helpers included by the owning translation unit to keep the related flow grouped.
//
// Inclusion contract:
// - This file is included exactly once, from inside BrokerPipeServer.cpp's anonymous namespace.
// - It deliberately uses the server's private identity, replay, challenge, tracking, and I/O helpers.
// - It is not a standalone translation unit and must not expose broker-internal state in public headers.
// - Handler behavior and protocol responses remain defined by the existing broker contracts.
// Authorizes a service stop request and sends the bound result to the client.
	bool HandleBrokerStopRequest(HANDLE pipe, HANDLE stopEvent,
		const ClientImageIdentity& imageIdentity,
		const xucrun::broker::BrokerStopRequest& request, DWORD& transferred)
	{
		xucrun::broker::BrokerStopResult result;
		result.requestId = request.requestId;
		DWORD authorizationError = NO_ERROR;
		if (!IsSensitiveClientAuthorized(imageIdentity, authorizationError))
		{
			result.status = xucrun::broker::BrokerStopStatus::InvalidRequest;
			result.windowsError = authorizationError;
		}
		else
		{
			SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
			SC_HANDLE service = manager ? OpenServiceW(manager,
				xucrun::broker::BrokerServiceName, SERVICE_CHANGE_CONFIG) : nullptr;
			const bool disabled = service && ChangeServiceConfigW(service,
				SERVICE_NO_CHANGE, SERVICE_DISABLED, SERVICE_NO_CHANGE,
				nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
			result.windowsError = disabled ? NO_ERROR : GetLastError();
			result.status = disabled
				? xucrun::broker::BrokerStopStatus::Accepted
				: xucrun::broker::BrokerStopStatus::Failed;
			if (service)
				CloseServiceHandle(service);
			if (manager)
				CloseServiceHandle(manager);
		}

		std::vector<std::uint8_t> response =
			xucrun::broker::SerializeBrokerStopResult(result);
		const bool responseWritten = RunOverlappedIo(pipe, stopEvent, true,
			response.data(), static_cast<DWORD>(response.size()), transferred) &&
			transferred == response.size();
		return result.status == xucrun::broker::BrokerStopStatus::Accepted &&
			responseWritten;
	}

// Performs the authorized ACL audit and bounds the report to the protocol contract.
	void HandleSecurityAuditRequest(HANDLE pipe, HANDLE stopEvent,
		const ClientImageIdentity& imageIdentity,
		const std::wstring& clientSid,
		const xucrun::broker::SecurityAuditRequest& request,
		BrokerLogCallback log, DWORD& transferred)
	{
		xucrun::broker::SecurityAuditResult result;
		result.requestId = request.requestId;
		DWORD authorizationError = NO_ERROR;
		if (!IsSensitiveClientAuthorized(imageIdentity, authorizationError))
		{
			result.status = xucrun::broker::SecurityAuditStatus::InvalidRequest;
			result.windowsError = authorizationError;
		}
		else
		{
			const bool german = request.language ==
				xucrun::broker::SecurityAuditRequest::Language::German;
			SecurityAclAuditCoverage coverage;
			const std::vector<SecurityAclFinding> findings =
				AuditBrokerSecurityAcls(german, &coverage, true);
			if (findings.size() > xucrun::broker::MaximumSecurityAuditFindings)
			{
				result.status = xucrun::broker::SecurityAuditStatus::Failed;
				result.windowsError = ERROR_INSUFFICIENT_BUFFER;
			}
			else
			{
				result.status = xucrun::broker::SecurityAuditStatus::Succeeded;
				result.findingCount = static_cast<std::uint32_t>(findings.size());
				result.report = findings.empty()
					? (german ? L"Keine Sicherheitsabweichungen gefunden."
						: L"No security deviations found.")
					: (german ? L"Unerwartete Schreibberechtigungen oder ACL-Probleme gefunden:"
						: L"Unexpected write permissions or ACL problems found:");
				const auto& context = coverage.credentialStoreContext;
				result.report += german ? L"\n\nCredential-Store-Kontext:"
					: L"\n\nCredential store context:";
				result.report += german ? L"\n- Brokerprozess: " : L"\n- Broker process: ";
				result.report += !context.contextInspected
					? (german ? L"Prüfung nicht möglich" : L"inspection unavailable")
					: (context.processIsLocalSystem ? L"LocalSystem"
						: (german ? L"nicht LocalSystem" : L"not LocalSystem"));
				result.report += german ? L"\n- Thread-Impersonation: " : L"\n- Thread impersonation: ";
				result.report += !context.contextInspected
					? (german ? L"Prüfung nicht möglich" : L"inspection unavailable")
					: (context.noThreadImpersonation ? (german ? L"keine" : L"none")
						: (german ? L"aktiv" : L"active"));
				result.report += german ? L"\n- Konfiguriertes Dienstkonto: "
					: L"\n- Configured service account: ";
				result.report += !context.serviceConfigurationRead
					? (german ? L"Prüfung nicht möglich" : L"inspection unavailable")
					: context.serviceAccount + (context.serviceAccountIsLocalSystem
						? L" (LocalSystem)"
						: (german ? L" (nicht LocalSystem)" : L" (not LocalSystem)"));
				result.report += german ? L"\n- Credential-Store-Zugriffsschutz: "
					: L"\n- Credential store access guard: ";
				result.report += !context.contextInspected
					? (german ? L"Prüfung nicht möglich" : L"inspection unavailable")
					: (context.accessPermitted
						? (german ? L"aktiv; aktueller Kontext zulässig"
							: L"active; current context permitted")
						: (german ? L"aktiv; aktueller Kontext abgelehnt"
							: L"active; current context denied"));
				result.report += german ? L"\n\nÜberprüfte Installationsobjekte:"
					: L"\n\nInspected installation objects:";
				for (const std::wstring& object : coverage.installationObjects)
					result.report += L"\n- " + object;
				result.report += german ? L"\n\nZertifikatsprüfung der Installationsdateien:"
					: L"\n\nCertificate inspection of installation files:";
				for (const SecurityAclSignatureInspection& inspection :
					coverage.signatureInspections)
				{
					result.report += L"\n- " + inspection.path + L": ";
					if (!inspection.identityRead)
					{
						result.report += (german ? L"Prüfung nicht möglich (Fehler "
							: L"inspection unavailable (error ") +
							std::to_wstring(inspection.windowsError) + L")";
					}
					else if (!inspection.signatureTrusted)
					{
						result.report += german ? L"Signatur ungültig oder nicht vertrauenswürdig"
							: L"signature invalid or not trusted";
					}
					else if (!inspection.signerMatches)
					{
						result.report += german ? L"Signatur gültig, aber Signierzertifikat unerwartet"
							: L"signature valid, but signing certificate unexpected";
					}
					else
					{
						result.report += german
							? L"Signatur gültig und vertrauenswürdig; erwartetes Signierzertifikat"
							: L"signature valid and trusted; expected signing certificate";
					}
				}
				result.report += german ? L"\n\nÜberprüfte Store-Verzeichnisse:"
					: L"\n\nInspected store directories:";
				for (const std::wstring& directory : coverage.storeDirectories)
					result.report += L"\n- " + directory;
				for (const SecurityAclFinding& finding : findings)
				{
					result.report += (german ? L"\n\nObjekt: " : L"\n\nObject: ") +
						finding.objectName;
					result.report += L"\nProblem: " + finding.problem;
					result.report += (german ? L"\nVorschlag zur Behebung: "
						: L"\nSuggested remediation: ") + finding.remediation;
				}
				if (result.report.size() >
					xucrun::broker::MaximumSecurityAuditReportLength)
				{
					result.status = xucrun::broker::SecurityAuditStatus::Failed;
					result.windowsError = ERROR_INSUFFICIENT_BUFFER;
					result.findingCount = 0;
					result.report.clear();
				}
			}
		}
		if (log)
		{
			const std::wstring context = L" Request ID " +
				std::to_wstring(request.requestId) + L", client SID " + clientSid;
			if (result.status == xucrun::broker::SecurityAuditStatus::Succeeded)
			{
				log(result.findingCount == 0
					? EVENTLOG_INFORMATION_TYPE
					: EVENTLOG_WARNING_TYPE,
					L"Security audit completed." + context + L", findings " +
					std::to_wstring(result.findingCount) + L".");
			}
			else if (result.status == xucrun::broker::SecurityAuditStatus::InvalidRequest)
			{
				log(EVENTLOG_WARNING_TYPE,
					L"Security audit request rejected." + context + L", error " +
					std::to_wstring(result.windowsError) + L".");
			}
			else
			{
				log(EVENTLOG_ERROR_TYPE,
					L"Security audit failed." + context + L", status " +
					std::to_wstring(static_cast<int>(result.status)) + L", error " +
					std::to_wstring(result.windowsError) + L".");
			}
		}

		std::vector<std::uint8_t> response =
			xucrun::broker::SerializeSecurityAuditResult(result);
		RunOverlappedIo(pipe, stopEvent, true, response.data(),
			static_cast<DWORD>(response.size()), transferred);
	}

	// Builds the cross-profile inventory only for an authenticated high-integrity viewer.
	void HandleAdministrativeInventoryRequest(HANDLE pipe, HANDLE stopEvent,
		HANDLE clientProcess, const ClientImageIdentity& imageIdentity,
		const xucrun::broker::AdministrativeInventoryRequest& request,
		DWORD& transferred)
	{
		xucrun::broker::AdministrativeInventoryResult result;
		result.requestId = request.requestId;
		DWORD error = NO_ERROR;
		if (!IsSensitiveClientAuthorized(imageIdentity, error) ||
			!HasHighIntegrity(clientProcess, error) ||
			!HasEnabledAdministratorsGroup(clientProcess, error))
		{
			result.status =
				xucrun::broker::AdministrativeInventoryStatus::InvalidRequest;
			result.windowsError = error == NO_ERROR ? ERROR_ACCESS_DENIED : error;
		}
		else
		{
			std::vector<StoredCredentialMetadata> credentials;
			std::wstring authenticityMarkerPrefix;
			std::vector<std::wstring> authenticityMarkerTargets;
			if (!ListAllStoredCredentialMetadata(credentials, error) ||
				!xucrun::approval_store::ListAllForAdministration(
					result.approvals, error) ||
				!xucrun::broker_targets::GetMachineScopedPrefix(
					xucrun::broker_targets::TargetKind::AuthenticityMarker,
					authenticityMarkerPrefix, error) ||
				!xucrun::broker_store::EnumerateTargets(
					authenticityMarkerPrefix, authenticityMarkerTargets, error) ||
				authenticityMarkerTargets.size() > UINT32_MAX)
			{
				result.status =
					xucrun::broker::AdministrativeInventoryStatus::Failed;
				result.windowsError = error == NO_ERROR ? ERROR_GEN_FAILURE : error;
				result.approvals.clear();
			}
			else
			{
				result.credentials.reserve(credentials.size());
				for (StoredCredentialMetadata& credential : credentials)
				{
					result.credentials.push_back({
						std::move(credential.ownerUserSid),
						std::move(credential.accountSid),
						std::move(credential.canonicalAccountName),
						credential.lastWritten });
				}
				result.authenticityMarkerCount = static_cast<std::uint32_t>(
					authenticityMarkerTargets.size());
				result.status =
					xucrun::broker::AdministrativeInventoryStatus::Succeeded;
			}
		}

		std::vector<std::uint8_t> response =
			xucrun::broker::SerializeAdministrativeInventoryResult(result);
		RunOverlappedIo(pipe, stopEvent, true, response.data(),
			static_cast<DWORD>(response.size()), transferred);
	}

	// Applies an administrative deletion only after validating viewer integrity and ownership scope.
	void HandleAdministrativeDeleteRequest(HANDLE pipe, HANDLE stopEvent,
		HANDLE clientProcess, const ClientImageIdentity& imageIdentity,
		const xucrun::broker::AdministrativeDeleteRequest& request,
		DWORD& transferred)
	{
		xucrun::broker::AdministrativeDeleteResult result;
		result.requestId = request.requestId;
		DWORD error = NO_ERROR;
		PSID ownerSid = nullptr;
		if (!IsSensitiveClientAuthorized(imageIdentity, error) ||
			!HasHighIntegrity(clientProcess, error) ||
			!HasEnabledAdministratorsGroup(clientProcess, error) ||
			!ConvertStringSidToSidW(request.ownerUserSid.c_str(), &ownerSid))
		{
			if (error == NO_ERROR)
				error = GetLastError();
			result.status = xucrun::broker::AdministrativeDeleteStatus::InvalidRequest;
			result.windowsError = error == NO_ERROR ? ERROR_ACCESS_DENIED : error;
		}
		else
		{
			LocalFree(ownerSid);
			ownerSid = nullptr;
			std::lock_guard storeLock(xucrun::broker_store::TransactionMutex());
			bool deleted = false;
			if (request.kind == xucrun::broker::AdministrativeDeleteKind::Approval)
			{
				if (!xucrun::approval_store::DeleteListed(
					request.ownerUserSid, request.approval, deleted, error))
				{
					result.status = xucrun::broker::AdministrativeDeleteStatus::Failed;
					result.windowsError = error;
				}
			}
			else
			{
				AccountIdentity accountIdentity;
				std::wstring credentialTarget;
				std::uint32_t deletedApprovalCount = 0;
				if (!ResolveUserAccountIdentity(
					request.accountSid, accountIdentity, error) ||
					!ResolveCredentialTarget(request.ownerUserSid,
						accountIdentity.sid, credentialTarget, error))
				{
					result.status = xucrun::broker::AdministrativeDeleteStatus::InvalidRequest;
					result.windowsError = error;
				}
				else if (!xucrun::approval_store::DeleteForAccount(
					request.ownerUserSid, accountIdentity.sid,
					deletedApprovalCount, error))
				{
					result.status = xucrun::broker::AdministrativeDeleteStatus::Failed;
					result.windowsError = error;
				}
				else
				{
					deleted = xucrun::broker_store::Delete(credentialTarget, error);
					const bool missing = !deleted && error == ERROR_NOT_FOUND;
					if ((deleted || missing) &&
						!DeleteCredentialMetadataBinding(
							request.ownerUserSid, accountIdentity.sid, error))
					{
						result.status = xucrun::broker::AdministrativeDeleteStatus::Failed;
						result.windowsError = error;
					}
				}
			}
			if (result.windowsError == NO_ERROR)
			{
				result.status = deleted
					? xucrun::broker::AdministrativeDeleteStatus::Succeeded
					: xucrun::broker::AdministrativeDeleteStatus::NotFound;
				result.windowsError = deleted ? NO_ERROR : ERROR_NOT_FOUND;
			}
		}
		if (ownerSid)
			LocalFree(ownerSid);
		std::vector<std::uint8_t> response =
			xucrun::broker::SerializeAdministrativeDeleteResult(result);
		RunOverlappedIo(pipe, stopEvent, true, response.data(),
			static_cast<DWORD>(response.size()), transferred);
	}

	// Executes backup maintenance only for the authenticated elevated maintenance client.
	void HandleBackupRequest(HANDLE pipe, HANDLE stopEvent, HANDLE clientProcess,
		const ClientImageIdentity& imageIdentity,
		const xucrun::broker::BackupRequest& request, DWORD& transferred)
	{
		xucrun::broker::BackupResult result;
		result.requestId = request.requestId;
		DWORD error = NO_ERROR;
		if (!IsSensitiveClientAuthorized(imageIdentity, error) ||
			!HasHighIntegrity(clientProcess, error) ||
			!HasEnabledAdministratorsGroup(clientProcess, error))
		{
			result.status = xucrun::broker::BackupStatus::InvalidRequest;
			result.windowsError = error == NO_ERROR ? ERROR_ACCESS_DENIED : error;
		}
		else
		{
			bool success = false;
			switch (request.action)
			{
			case xucrun::broker::BackupAction::Create:
			{
				xucrun::broker::BackupEntry entry;
				success = xucrun::backup_store::Create(
					request.description, entry, error);
				if (success) result.entries.push_back(std::move(entry));
				break;
			}
			case xucrun::broker::BackupAction::List:
				success = xucrun::backup_store::List(result.entries, error);
				break;
			case xucrun::broker::BackupAction::Restore:
				success = xucrun::backup_store::Restore(
					request.backupId, request.createFallbackBackup,
					result.fallbackBackupId, error);
				break;
			case xucrun::broker::BackupAction::Delete:
				success = xucrun::backup_store::Delete(request.backupId, error);
				break;
			case xucrun::broker::BackupAction::DeleteAll:
				success = xucrun::backup_store::DeleteAll(error);
				break;
			default:
				error = ERROR_INVALID_PARAMETER;
				break;
			}
			result.status = success ? xucrun::broker::BackupStatus::Succeeded
				: error == ERROR_FILE_NOT_FOUND
					? xucrun::broker::BackupStatus::NotFound
					: xucrun::broker::BackupStatus::Failed;
			result.windowsError = success ? NO_ERROR
				: error == NO_ERROR ? ERROR_GEN_FAILURE : error;
			if (!success)
			{
				result.entries.clear();
				result.fallbackBackupId.clear();
			}
		}
		const auto response = xucrun::broker::SerializeBackupResult(result);
		RunOverlappedIo(pipe, stopEvent, true, const_cast<std::uint8_t*>(response.data()),
			static_cast<DWORD>(response.size()), transferred);
	}
