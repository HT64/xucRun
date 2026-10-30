<!--
Purpose: Catalogs every tracked and unignored repository file and its purpose.
Approach: Uses one sorted table row per Git-visible file, with validation enforced by the repository test.
-->
# File register

This register covers every file reported by Git as tracked or unignored in this repository. Each
path appears exactly once. The repository test checks that the table stays complete,
duplicate-free, and sorted.

Commentable text files carry concise `Purpose` and `Approach` metadata in their file heads.
PowerShell scripts use their existing comment-based help: `.SYNOPSIS` states what the script does,
and `.DESCRIPTION` explains how it does it. Generated commentable files retain metadata through
their generators. MSBuild XML files (`.vcxproj`, `.props`, and `.targets`) use one comment block
after the optional XML declaration, with `<!--`, `Purpose: ...`, `Approach: ...`, and `-->` each
on its own line. Formats that cannot safely carry comments--such as the Visual Studio solution,
images and icons, legal texts, and plain semantic build data--are documented only here. The
existing UTF-16 Windows Registry export is also kept byte-stable and documented here because Git
treats that encoding as binary.

| File | Class | Purpose |
|---|---|---|
| `.gitattributes` | Repository configuration | Defines text normalization and binary handling for repository files. |
| `.github/FUNDING.yml` | YAML configuration | Links xucRun to its optional Patreon funding page. |
| `.github/workflows/ci.yml` | YAML configuration | Builds and validates xucRun on a clean Windows runner. |
| `.github/workflows/release.yml` | YAML configuration | Builds and attests unsigned xucRun release assets and stages unpublished releases as drafts. |
| `.gitignore` | Repository configuration | Excludes xucRun build outputs, IDE state, and generated packages. |
| `CHANGELOG.de.md` | Documentation | Documents public xucRun releases and notable changes in German; its conventional title and version structure replace a metadata head. |
| `CHANGELOG.md` | Documentation | Documents public xucRun releases and notable changes in English; its conventional title and version structure replace a metadata head. |
| `CONTRIBUTING.de.md` | Documentation | Explains the current contribution policy in German. |
| `CONTRIBUTING.md` | Documentation | Explains the current contribution policy in English. |
| `Directory.Build.targets` | MSBuild targets | Ensures changes to shared client resources are embedded in every dependent binary. |
| `docs/de/BUILDING.md` | Documentation | Explains how users build xucRun from source in German. |
| `docs/de/images/administrative-status.png` | Documentation image | German documentation screenshot showing administrative status. |
| `docs/de/images/approval-dialog.png` | Documentation image | German documentation screenshot showing approval dialog. |
| `docs/de/images/approval-helper-uac.png` | Documentation image | German documentation screenshot showing approval helper uac. |
| `docs/de/images/approvals.png` | Documentation image | German documentation screenshot showing approvals. |
| `docs/de/images/broad-interpreter-approval-confirmation.png` | Documentation image | German documentation screenshot showing broad interpreter approval confirmation. |
| `docs/de/images/credential-entry.png` | Documentation image | German documentation screenshot showing credential entry. |
| `docs/de/images/credentials.png` | Documentation image | German documentation screenshot showing credentials. |
| `docs/de/images/credential-stored.png` | Documentation image | German documentation screenshot showing credential stored. |
| `docs/de/images/cross-account-elevation-blocked.png` | Documentation image | German documentation screenshot showing cross account elevation blocked. |
| `docs/de/images/data-backups.png` | Documentation image | German documentation screenshot showing data backups. |
| `docs/de/images/delete-credential.png` | Documentation image | German documentation screenshot showing delete credential. |
| `docs/de/images/installation-success.png` | Documentation image | German documentation screenshot showing installation success. |
| `docs/de/images/installer.png` | Documentation image | German documentation screenshot showing installer. |
| `docs/de/images/installer-process-and-uninstallation.png` | Documentation image | German documentation screenshot showing installer process and uninstallation. |
| `docs/de/images/program-hash-change-approval.png` | Documentation image | German documentation screenshot showing program hash change approval. |
| `docs/de/images/reset-approvals.png` | Documentation image | German documentation screenshot showing reset approvals. |
| `docs/de/images/runtime-information.png` | Documentation image | German documentation screenshot showing runtime information. |
| `docs/de/images/security-audit.png` | Documentation image | German documentation screenshot showing security audit. |
| `docs/de/images/target-account-selection.png` | Documentation image | German documentation screenshot showing target account selection. |
| `docs/de/images/target-user-selection.png` | Documentation image | German documentation screenshot showing target user selection. |
| `docs/de/images/uninstallation-success.png` | Documentation image | German documentation screenshot showing uninstallation success. |
| `docs/de/images/uninstall-blocked-by-tray.png` | Documentation image | German documentation screenshot showing uninstall blocked by tray. |
| `docs/de/images/uninstall-confirmation.png` | Documentation image | German documentation screenshot showing uninstall confirmation. |
| `docs/de/INSTALLATION.md` | Documentation | Explains how users install xucRun in German. |
| `docs/de/MANUAL_REMOVAL.md` | Documentation | Explains how users manually remove xucRun in German. |
| `docs/en/BUILDING.md` | Documentation | Explains how users build xucRun from source in English. |
| `docs/en/images/administrative-status.png` | Documentation image | English documentation screenshot showing administrative status. |
| `docs/en/images/approval-dialog.png` | Documentation image | English documentation screenshot showing approval dialog. |
| `docs/en/images/approval-helper-uac.png` | Documentation image | English documentation screenshot showing approval helper uac. |
| `docs/en/images/approvals.png` | Documentation image | English documentation screenshot showing approvals. |
| `docs/en/images/broad-interpreter-approval-confirmation.png` | Documentation image | English documentation screenshot showing broad interpreter approval confirmation. |
| `docs/en/images/credential-entry.png` | Documentation image | English documentation screenshot showing credential entry. |
| `docs/en/images/credentials.png` | Documentation image | English documentation screenshot showing credentials. |
| `docs/en/images/credential-stored.png` | Documentation image | English documentation screenshot showing credential stored. |
| `docs/en/images/cross-account-elevation-blocked.png` | Documentation image | English documentation screenshot showing cross account elevation blocked. |
| `docs/en/images/data-backups.png` | Documentation image | English documentation screenshot showing data backups. |
| `docs/en/images/delete-credential.png` | Documentation image | English documentation screenshot showing delete credential. |
| `docs/en/images/installation-success.png` | Documentation image | English documentation screenshot showing installation success. |
| `docs/en/images/installer.png` | Documentation image | English documentation screenshot showing installer. |
| `docs/en/images/installer-process-and-uninstallation.png` | Documentation image | English documentation screenshot showing installer process and uninstallation. |
| `docs/en/images/program-hash-change-approval.png` | Documentation image | English documentation screenshot showing program hash change approval. |
| `docs/en/images/reset-approvals.png` | Documentation image | English documentation screenshot showing reset approvals. |
| `docs/en/images/runtime-information.png` | Documentation image | English documentation screenshot showing runtime information. |
| `docs/en/images/security-audit.png` | Documentation image | English documentation screenshot showing security audit. |
| `docs/en/images/target-account-selection.png` | Documentation image | English documentation screenshot showing target account selection. |
| `docs/en/images/target-user-selection.png` | Documentation image | English documentation screenshot showing target user selection. |
| `docs/en/images/uninstallation-success.png` | Documentation image | English documentation screenshot showing uninstallation success. |
| `docs/en/images/uninstall-blocked-by-tray.png` | Documentation image | English documentation screenshot showing uninstall blocked by tray. |
| `docs/en/images/uninstall-confirmation.png` | Documentation image | English documentation screenshot showing uninstall confirmation. |
| `docs/en/INSTALLATION.md` | Documentation | Explains how users install xucRun in English. |
| `docs/en/MANUAL_REMOVAL.md` | Documentation | Explains how users manually remove xucRun in English. |
| `ExplorerKontextMenue.reg` | Registry script | Adds xucRun commands to Windows Explorer context menus for supported files, folders, folder backgrounds, and drives. |
| `FILE_REGISTER.md` | Documentation | Catalogs every tracked and unignored repository file and its purpose. |
| `Install-xucRun.ps1` | PowerShell script | Installs, updates, or uninstalls xucRun on the local Windows computer. |
| `LICENSE` | Legal text | Contains the Apache License 2.0 terms governing xucRun. |
| `New-xucRun-Package.ps1` | PowerShell script | Creates an unsigned xucRun package from compiled public build outputs. |
| `New-xucRun-Release.ps1` | PowerShell script | Creates unsigned xucRun release assets from a Release build. |
| `New-xucRun-ReleaseNotes.ps1` | PowerShell script | Creates bilingual release notes for one xucRun version. |
| `NOTICE` | Legal notice | Provides attribution and copyright notices for xucRun. |
| `README.de.md` | Documentation | Introduces xucRun, its security model, use, and documentation in German. |
| `README.md` | Documentation | Introduces xucRun, its security model, use, and documentation in English. |
| `SECURITY.de.md` | Documentation | Explains supported versions and vulnerability reporting in German. |
| `SECURITY.md` | Documentation | Explains supported versions and vulnerability reporting in English. |
| `Test-Installer.ps1` | PowerShell script | Runs the destructive xucRun installer smoke test on a clean Windows runner. |
| `Test-InstallerGuiSecurity.ps1` | PowerShell script | Validates repository file documentation and installer runtime staging. |
| `UserCodeSigning/Sign-xucRunPackage.ps1` | PowerShell script | Signs an xucRun package with a trusted machine-local certificate. |
| `XUCRun.sln` | Visual Studio solution | Groups the xucRun projects and build configurations for Visual Studio and MSBuild. |
| `xucRun/ClientAdministrativeCommands.cpp` | C++ source | Dispatches administrative client commands to status, credential, approval, backup, language, and audit workflows. |
| `xucRun/ClientAdministrativeCommands.h` | C++ header | Defines dispatch for the client's public administrative commands. |
| `xucRun/ClientApprovalManagement.cpp` | C++ source | Implements displaying and resetting stored broker approvals. |
| `xucRun/ClientApprovalManagement.h` | C++ header | Defines display and reset of stored broker approvals. |
| `xucRun/ClientApprovalSelectionDialog.cpp` | C++ source | Implements multi-selection of broker approvals to reset. |
| `xucRun/ClientApprovalSelectionDialog.h` | C++ header | Defines multi-selection of broker approvals to reset. |
| `xucRun/ClientConsoleReplacement.cpp` | C++ source | Implements console replacement for invocations without a target program. |
| `xucRun/ClientConsoleReplacement.h` | C++ header | Defines console replacement for invocations without a target program. |
| `xucRun/ClientConsoleSession.cpp` | C++ source | Implements console-faithful launch under a target account and parent console replacement. |
| `xucRun/ClientConsoleSession.h` | C++ header | Defines detection, replacement, and closure of the invoking console session. |
| `xucRun/ClientCredentialPrompt.cpp` | C++ source | Implements protected client-side input of Windows credentials. |
| `xucRun/ClientCredentialPrompt.h` | C++ header | Defines protected client-side input of Windows credentials. |
| `xucRun/ClientElevatedWrapperLaunch.cpp` | C++ source | Launches the broker-prepared elevated wrapper process and reports its final result securely. |
| `xucRun/ClientElevatedWrapperLaunch.h` | C++ header | Declares launching of a validated broker-bound elevated wrapper process. |
| `xucRun/ClientElevatedWrapperParsing.cpp` | C++ source | Parses and validates the internal broker-bound elevated-wrapper command line. |
| `xucRun/ClientElevatedWrapperParsing.h` | C++ header | Defines parsing of the internal command line for the elevated wrapper. |
| `xucRun/ClientElevatedWrapperPreparation.cpp` | C++ source | Prepares normalized target, argument, and working-directory data for elevated wrapper launch. |
| `xucRun/ClientElevatedWrapperPreparation.h` | C++ header | Defines launch preparation for internal elevated wrapper mode. |
| `xucRun/ClientElevatedWrapperReporter.cpp` | C++ source | Reports exactly one elevated-wrapper launch result through broker-provided handles. |
| `xucRun/ClientElevatedWrapperReporter.h` | C++ header | Defines reporting of an elevated wrapper launch back to the broker. |
| `xucRun/ClientHiddenLaunchModifiers.cpp` | C++ source | Parses hidden-launch modifiers and removes them from the target command line. |
| `xucRun/ClientHiddenLaunchModifiers.h` | C++ header | Declares parsing of hidden and monitored-hidden launch modifiers. |
| `xucRun/ClientInfoCommand.cpp` | C++ source | Implements the public client information command. |
| `xucRun/ClientInfoCommand.h` | C++ header | Defines the public client information command. |
| `xucRun/ClientInternalBrokerTests.cpp` | C++ source | Implements internal end-to-end probes for broker protocol and authorization behavior. |
| `xucRun/ClientInternalBrokerTests.h` | C++ header | Defines handling of internal broker test switches in the client. |
| `xucRun/ClientLanguageChange.cpp` | C++ source | Coordinates elevated language changes without granting Broker launch rights to the Tray. |
| `xucRun/ClientLanguageChange.h` | C++ header | Coordinates a machine-wide language change through the xucRun client. |
| `xucRun/ClientMainArguments.inl` | C++ inline source | Internal command-line helpers for the xucRun client entry point. |
| `xucRun/ClientMainExecution.inl` | C++ inline source | Internal normal-launch implementation for the xucRun client entry point. |
| `xucRun/ClientMainModes.inl` | C++ inline source | Internal standalone-mode helpers for the xucRun client entry point. |
| `xucRun/ClientProcessMonitoring.cpp` | C++ source | Implements startup and execution of the internal monitor for hidden target processes. |
| `xucRun/ClientProcessMonitoring.h` | C++ header | Defines monitoring of hidden-started target processes via an xucRun helper process. |
| `xucRun/ClientProgramHashUpdateDialog.cpp` | C++ source | Implements the selection dialog for program hash update scope. |
| `xucRun/ClientProgramHashUpdateDialog.h` | C++ header | Defines the selection dialog for scope of a detected program hash update. |
| `xucRun/ClientSameUserLaunch.cpp` | C++ source | Starts targets with the current user token while preserving requested elevation semantics. |
| `xucRun/ClientSameUserLaunch.h` | C++ header | Defines direct program launch under the current user account. |
| `xucRun/ClientSecurityAudit.cpp` | C++ source | Checks the running client for tampering and injection indicators. |
| `xucRun/ClientSecurityAudit.h` | C++ header | Defines the explicit client environment audit invoked by /audit. |
| `xucRun/ClientSensitiveString.cpp` | C++ source | Implements a secure lifecycle for short-lived sensitive strings. |
| `xucRun/ClientSensitiveString.h` | C++ header | Defines exclusive storage for short-lived sensitive strings. |
| `xucRun/main.cpp` | C++ source | Implements the xucRun client entry point with broker integration and file logging. |
| `xucRun/XUCRun.vcxproj` | MSBuild project | Builds the xucRun command-line client executable. |
| `xucRun/xucRun-1024.jpg` | Project image | High-resolution xucRun application artwork. |
| `xucRunApproval/main.cpp` | C++ source | Implements the secure-desktop approval helper that displays and answers broker challenges. |
| `xucRunApproval/resource.h` | C++ header | Defines resource identifiers for the xucRun approval helper. |
| `xucRunApproval/xucRunApproval.ico` | Application icon | Windows icon used by the xucRunApproval target. |
| `xucRunApproval/xucRunApproval.rc` | Windows resource script | Defines icon and version resources for the xucRun approval helper executable. |
| `xucRunApproval/xucRunApproval.vcxproj` | MSBuild project | Builds the secure-desktop approval helper executable. |
| `xucRunBroker/AccountIdentity.cpp` | C++ source | Resolves Windows account SIDs and validates the returned account type. |
| `xucRunBroker/AccountIdentity.h` | C++ header | Defines the canonical identity of a Windows account. |
| `xucRunBroker/ApprovalStore.cpp` | C++ source | Persists and validates broker approvals in the LocalSystem credential store. |
| `xucRunBroker/ApprovalStore.h` | C++ header | Declares the protected broker store for time- and program-bound approvals. |
| `xucRunBroker/AuthenticityMarkerStore.cpp` | C++ source | Stores a random per-user marker that only the broker reveals to authorized helpers. |
| `xucRunBroker/AuthenticityMarkerStore.h` | C++ header | Declares the per-user authenticity marker stored exclusively by the broker. |
| `xucRunBroker/BrokerBackupStore.cpp` | C++ source | Creates, validates, restores, and removes encrypted broker backup archives. |
| `xucRunBroker/BrokerBackupStore.h` | C++ header | Declares protected backup and restore operations for broker-managed product data. |
| `xucRunBroker/BrokerDirectoryOperations.cpp` | C++ source | Implements broker-owned directory validation, creation, traversal, and deletion helpers. |
| `xucRunBroker/BrokerDirectoryOperations.h` | C++ header | Declares secure filesystem operations used by broker backup, restore, and purge workflows. |
| `xucRunBroker/BrokerPipeApprovalHandlers.inl` | C++ inline source | Internal request-handler implementation fragment for BrokerPipeServer.cpp. |
| `xucRunBroker/BrokerPipeControlHandlers.inl` | C++ inline source | Internal request-handler implementation fragment for BrokerPipeServer.cpp. |
| `xucRunBroker/BrokerPipeCredentialHandlers.inl` | C++ inline source | Internal request-handler implementation fragment for BrokerPipeServer.cpp. |
| `xucRunBroker/BrokerPipeProcessHandlers.inl` | C++ inline source | Internal request-handler implementation fragment for BrokerPipeServer.cpp. |
| `xucRunBroker/BrokerPipeRequestDecoder.inl` | C++ inline source | Internal request decoding for BrokerPipeServer.cpp. |
| `xucRunBroker/BrokerPipeRequestDispatcher.inl` | C++ inline source | Internal typed request dispatch for BrokerPipeServer.cpp. |
| `xucRunBroker/BrokerPipeServer.cpp` | C++ source | Authenticates pipe clients, enforces broker request policy, and dispatches privileged operations. |
| `xucRunBroker/BrokerPipeServer.h` | C++ header | Declares the authenticated named pipe server of the broker service. |
| `xucRunBroker/BrokerRequestPolicy.h` | C++ header | Defines every broker request kind and its sensitive-client policy. |
| `xucRunBroker/BrokerTargetNaming.cpp` | C++ source | Generates machine-bound target names for sensitive broker storage entries. |
| `xucRunBroker/BrokerTargetNaming.h` | C++ header | Declares machine-bound, opaque target names for broker stores. |
| `xucRunBroker/BrokerTemporaryBatch.cpp` | C++ source | Creates and validates broker-owned temporary batch wrappers for approved command execution. |
| `xucRunBroker/BrokerTemporaryBatch.h` | C++ header | Declares lifecycle management for broker-owned temporary batch wrappers. |
| `xucRunBroker/ClientImageIdentity.cpp` | C++ source | Determines and verifies the path, SHA-256 hash, and signature of a broker client. |
| `xucRunBroker/ClientImageIdentity.h` | C++ header | Defines the verified image identity of a connected client process. |
| `xucRunBroker/CredentialMetadataStore.cpp` | C++ source | Persists protected credential metadata in the broker context. |
| `xucRunBroker/CredentialMetadataStore.h` | C++ header | Declares the protected metadata store for broker-managed account credentials. |
| `xucRunBroker/CredentialStore.cpp` | C++ source | Stores passwords exclusively in the broker's LocalSystem credential store. |
| `xucRunBroker/CredentialStore.h` | C++ header | Declares the LocalSystem store for client- and account-bound passwords. |
| `xucRunBroker/CredentialStoreSelfTest.cpp` | C++ source | Tests the credential store with temporary, uniquely named test entries. |
| `xucRunBroker/CredentialStoreSelfTest.h` | C++ header | Declares the isolated round-trip test of the broker-internal credential store. |
| `xucRunBroker/CredentialValidator.cpp` | C++ source | Validates credentials through Windows logon and matches the token account against the SID. |
| `xucRunBroker/CredentialValidator.h` | C++ header | Declares server-side validation of a password for an expected account SID. |
| `xucRunBroker/main.cpp` | C++ source | Runs xucRunBroker as a hardened LocalSystem service and diagnostic program. |
| `xucRunBroker/resource.h` | C++ header | Defines resource identifiers for the xucRun broker service. |
| `xucRunBroker/SecurityAclAudit.cpp` | C++ source | Audits broker files, directories, and registry entries for unexpected writers. |
| `xucRunBroker/SecurityAclAudit.h` | C++ header | Declares the ACL audit of security-critical broker resources. |
| `xucRunBroker/SensitiveClientAuthorization.cpp` | C++ source | Loads and validates the installed policy for sensitive broker clients. |
| `xucRunBroker/SensitiveClientAuthorization.h` | C++ header | Declares the installed authorization policy for sensitive broker clients. |
| `xucRunBroker/SessionLaunchExecution.inl` | C++ inline source | Internal implementation fragment for SessionTestLauncher.cpp. |
| `xucRunBroker/SessionLaunchOperations.inl` | C++ inline source | Internal implementation fragment for SessionTestLauncher.cpp. |
| `xucRunBroker/SessionLaunchPreparation.inl` | C++ inline source | Internal implementation fragment for SessionTestLauncher.cpp. |
| `xucRunBroker/SessionTestLauncher.cpp` | C++ source | Launches controlled processes and secure-desktop hosts in the authenticated client session. |
| `xucRunBroker/SessionTestLauncher.h` | C++ header | Declares controlled process launches and secure-desktop hosts in the client session. |
| `xucRunBroker/TargetImageIdentity.cpp` | C++ source | Determines the immutable identity of a requested launch file. |
| `xucRunBroker/TargetImageIdentity.h` | C++ header | Defines the handle-based verified identity of a requested launch file. |
| `xucRunBroker/xucRunBroker.ico` | Application icon | Windows icon used by the xucRunBroker target. |
| `xucRunBroker/xucRunBroker.rc` | Windows resource script | Defines icon and version resources for the xucRun broker service executable. |
| `xucRunBroker/xucRunBroker.vcxproj` | MSBuild project | Builds the LocalSystem broker service executable. |
| `xucRunClientCommon/ClientAccountIdentity.cpp` | C++ source | Implements account, SID, and administrator checks for client workflows. |
| `xucRunClientCommon/ClientAccountIdentity.h` | C++ header | Defines client-side resolution and checks for Windows account identities. |
| `xucRunClientCommon/ClientAdministrativeStatus.cpp` | C++ source | Launches and displays the broker-backed administrative status inventory. |
| `xucRunClientCommon/ClientAdministrativeStatus.h` | C++ header | Defines the elevated, read-only status view across Windows user profiles. |
| `xucRunClientCommon/ClientBackupManagement.cpp` | C++ source | Implements client commands for broker-backed backup, restore, listing, and deletion. |
| `xucRunClientCommon/ClientBackupManagement.h` | C++ header | Declares client-side backup and restore command handling. |
| `xucRunClientCommon/ClientBrokerCredentialManagement.cpp` | C++ source | Implements creation and deletion of stored broker credentials. |
| `xucRunClientCommon/ClientBrokerCredentialManagement.h` | C++ header | Defines creation and deletion of stored broker credentials. |
| `xucRunClientCommon/ClientBrokerCredentialSelection.cpp` | C++ source | Implements retrieval and selection of stored broker credentials. |
| `xucRunClientCommon/ClientBrokerCredentialSelection.h` | C++ header | Defines retrieval and selection of stored broker credentials. |
| `xucRunClientCommon/ClientBrokerLaunch.cpp` | C++ source | Implements client-side process launch via stored broker credentials. |
| `xucRunClientCommon/ClientBrokerLaunch.h` | C++ header | Defines client-side process launch through stored broker credentials. |
| `xucRunClientCommon/ClientBrokerStatus.cpp` | C++ source | Queries and formats broker service health for client status surfaces. |
| `xucRunClientCommon/ClientBrokerStatus.h` | C++ header | Orchestrates broker service status and administrative commands. |
| `xucRunClientCommon/ClientConsoleOutput.cpp` | C++ source | Reliably routes client text to an available Windows output target. |
| `xucRunClientCommon/ClientConsoleOutput.h` | C++ header | Defines output of longer client text to console, redirection, or dialog. |
| `xucRunClientCommon/ClientCredentialFormatting.cpp` | C++ source | Implements consistent text formatting for credential and approval workflows. |
| `xucRunClientCommon/ClientCredentialFormatting.h` | C++ header | Defines localized display text for credentials, approvals, and broker errors. |
| `xucRunClientCommon/ClientCredentialInfo.h` | C++ header | Defines client-side metadata for credentials reported by the broker. |
| `xucRunClientCommon/ClientCredentialSelectionDialog.cpp` | C++ source | Implements the client-side selection dialog for stored credentials. |
| `xucRunClientCommon/ClientCredentialSelectionDialog.h` | C++ header | Defines the client-side selection dialog for stored credentials. |
| `xucRunClientCommon/ClientImageTrust.cpp` | C++ source | Adapts shared Authenticode verification for client-facing trust details. |
| `xucRunClientCommon/ClientImageTrust.h` | C++ header | Defines trust verification for executable Windows files. |
| `xucRunClientCommon/ClientLaunchDetection.cpp` | C++ source | Detects PowerShell launches that explicitly request hidden windows. |
| `xucRunClientCommon/ClientLaunchDetection.h` | C++ header | Defines stateless detection rules for special client process launches. |
| `xucRunClientCommon/ClientLocalAccountEnumeration.cpp` | C++ source | Implements enumeration of enabled local Windows accounts. |
| `xucRunClientCommon/ClientLocalAccountEnumeration.h` | C++ header | Defines client-side enumeration of enabled local Windows accounts. |
| `xucRunClientCommon/ClientLocalAccountSelectionDialog.cpp` | C++ source | Implements the client-side selection dialog for local Windows accounts. |
| `xucRunClientCommon/ClientLocalAccountSelectionDialog.h` | C++ header | Defines the client-side selection dialog for local Windows accounts. |
| `xucRunClientCommon/ClientLogging.cpp` | C++ source | Implements dated, process-safe client file logging. |
| `xucRunClientCommon/ClientLogging.h` | C++ header | Defines initialization and writing of the shared client log. |
| `xucRunClientCommon/ClientTableDialog.cpp` | C++ source | Implements a reusable read-only table dialog for client information views. |
| `xucRunClientCommon/ClientTableDialog.h` | C++ header | Defines a reusable read-only table dialog for client information views. |
| `xucRunClientCommon/ClientTextDialog.cpp` | C++ source | Implements console-aware display of extensive informational and error text. |
| `xucRunClientCommon/ClientTextDialog.h` | C++ header | Defines a unified display path for longer read-only client text. |
| `xucRunClientCommon/ClientUiCommon.cpp` | C++ source | Implements central rendering and notification functions for the client UI. |
| `xucRunClientCommon/ClientUiCommon.h` | C++ header | Defines shared Win32 helpers for client windows and notifications. |
| `xucRunClientCommon/ClientWindowActivation.cpp` | C++ source | Implements safe window lookup, foreground activation, and Total Commander refresh. |
| `xucRunClientCommon/ClientWindowActivation.h` | C++ header | Defines activation of visible windows for launched or broker-tracked processes. |
| `xucRunClientCommon/ClientWindowsError.cpp` | C++ source | Implements central formatting of client Win32 and pipe errors. |
| `xucRunClientCommon/ClientWindowsError.h` | C++ header | Defines a unified, localized representation of Windows error codes. |
| `xucRunClientCommon/Localization.cpp` | C++ source | Loads configured application language resources and resolves localized strings. |
| `xucRunClientCommon/Localization.h` | C++ header | Declares language selection, resource loading, and localized string lookup. |
| `xucRunClientCommon/xucRunClientCommon.vcxproj` | MSBuild project | Builds shared xucRun client logic. |
| `xucRunCore/ApplicationIcon.cpp` | C++ source | Implements shared color transformations for application icons. |
| `xucRunCore/ApplicationIcon.h` | C++ header | Defines shared color transformations for application icons. |
| `xucRunCore/ApprovalSerialization.cpp` | C++ source | Serializes and deserializes program approvals in a canonical binary format. |
| `xucRunCore/ApprovalSerialization.h` | C++ header | Defines the versioned binary format for stored program approvals. |
| `xucRunCore/AuthenticodeTrust.cpp` | C++ source | Verifies embedded signatures and Windows catalog signatures uniformly. |
| `xucRunCore/AuthenticodeTrust.h` | C++ header | Defines shared embedded and catalog Authenticode verification. |
| `xucRunCore/BrokerBackupArchive.cpp` | C++ source | Serializes and validates the versioned binary archive shared by broker backup workflows. |
| `xucRunCore/BrokerBackupArchive.h` | C++ header | Declares the bounded, versioned broker backup archive format and validation API. |
| `xucRunCore/BrokerContracts.cpp` | C++ source | Implements shared construction and validation of broker contract data. |
| `xucRunCore/BrokerContracts.h` | C++ header | Defines the shared request and result types of the broker protocol. |
| `xucRunCore/BrokerPipeClient.cpp` | C++ source | Transmits broker requests through the service's authenticated Named Pipe. |
| `xucRunCore/BrokerPipeClient.h` | C++ header | Provides the client API for authenticated requests to the broker service. |
| `xucRunCore/BrokerSerialization.cpp` | C++ source | Serializes and deserializes broker protocol messages. |
| `xucRunCore/BrokerSerialization.h` | C++ header | Declares the binary representation of all broker protocol messages. |
| `xucRunCore/BrokerSerializationAdministration.inl` | C++ inline source | Internal protocol implementation fragment for BrokerSerialization.cpp. |
| `xucRunCore/BrokerSerializationApprovals.inl` | C++ inline source | Internal protocol implementation fragment for BrokerSerialization.cpp. |
| `xucRunCore/BrokerSerializationCore.inl` | C++ inline source | Internal protocol implementation fragment for BrokerSerialization.cpp. |
| `xucRunCore/BrokerSerializationCredentials.inl` | C++ inline source | Internal protocol implementation fragment for BrokerSerialization.cpp. |
| `xucRunCore/ClientCommandLine.cpp` | C++ source | Implements canonical processing of client arguments and /user options. |
| `xucRunCore/ClientCommandLine.h` | C++ header | Defines the UI-independent rules for parsing and generating client command lines. |
| `xucRunCore/ClientProcessContext.cpp` | C++ source | Determines the process context from which xucRun was invoked. |
| `xucRunCore/ClientProcessContext.h` | C++ header | Defines process queries and classification of supported parent shells. |
| `xucRunCore/DocumentationNameMapping.cpp` | C++ source | Implements the optional display-only name mapping used for documentation screenshots. |
| `xucRunCore/DocumentationNameMapping.h` | C++ header | Defines optional display-only account and computer name mappings for documentation screenshots. |
| `xucRunCore/FileIdentity.cpp` | C++ source | Determines final file paths and SHA-256 hashes through open handles. |
| `xucRunCore/FileIdentity.h` | C++ header | Declares handle-based resolution and hashing for file identities. |
| `xucRunCore/SecureDesktopHost.cpp` | C++ source | Hosts sensitive UI on a private desktop isolated from lower-integrity processes. |
| `xucRunCore/SecureDesktopHost.h` | C++ header | Runs a helper on an isolated high-integrity desktop and restores the user desktop. |
| `xucRunCore/SensitiveString.cpp` | C++ source | Implements protected memory management for passwords and other secrets. |
| `xucRunCore/SensitiveString.h` | C++ header | Defines a non-copyable storage container for sensitive strings. |
| `xucRunCore/TargetNaming.cpp` | C++ source | Implements canonical generation of opaque broker target names. |
| `xucRunCore/TargetNaming.h` | C++ header | Declares canonical inputs and target names for broker store entries. |
| `xucRunCore/xucRunCore.vcxproj` | MSBuild project | Builds shared protocol, serialization, and security primitives. |
| `xucRunCoreTests/BrokerContractsTests.cpp` | C++ source | Tests contracts, serialization, identity helpers, and sensitive storage primitives. |
| `xucRunCoreTests/DocumentationNameMappingTests.cpp` | C++ source | Tests strict loading and reversible account mappings for documentation screenshots. |
| `xucRunCoreTests/xucRunCoreTests.vcxproj` | MSBuild project | Builds unit tests for the shared xucRun library. |
| `xucRunInstaller/Installer.rc` | Windows resource script | Embeds the xucRun package and version metadata into Installer.exe. |
| `xucRunInstaller/Installer-Gui.ps1` | PowerShell script | Displays the bilingual graphical xucRun installation workflow. |
| `xucRunInstaller/main.cpp` | C++ source | Extracts the embedded xucRun package securely and launches its graphical installer. |
| `xucRunInstaller/resource.h` | C++ header | Defines resource identifiers for the xucRun bootstrap installer. |
| `xucRunInstaller/xucRunInstaller.vcxproj` | MSBuild project | Builds the self-contained xucRun bootstrap installer. |
| `xucRunLanguageDeDe/xucRunLanguageDeDe.vcxproj` | MSBuild project | Builds the German xucRun language resource DLL. |
| `xucRunLanguageEnUs/xucRunLanguageEnUs.vcxproj` | MSBuild project | Builds the English xucRun language resource DLL. |
| `xucRunSharedResources/buildcounter.txt` | Build data | Stores the current generated build number. |
| `xucRunSharedResources/BuildVersion.h` | C++ header | Provides the auto-generated product version macros shared by all build targets. |
| `xucRunSharedResources/ClientResourceIds.h` | C++ header | Defines the stable resource identifiers of the xucRun Windows application. |
| `xucRunSharedResources/ClientResources.rc` | Windows resource script | Defines shared localized dialogs, strings, icons, and version resources. |
| `xucRunSharedResources/Create-AppIcon.ps1` | PowerShell script | Creates ICO and JPEG artwork for an xucRun application variant. |
| `xucRunSharedResources/Increment-BuildNumber.ps1` | PowerShell script | Updates the shared xucRun build counter and generated version header. |
| `xucRunSharedResources/version.txt` | Version data | Stores the semantic product version used by the build. |
| `xucRunSharedResources/xucRun.ico` | Application icon | Windows icon used by the xucRun target. |
