# Changelog

**Language:** English | [Deutsch](CHANGELOG.de.md)

This changelog covers only changes relevant to users of xucRun. Internal development, export,
and publication processes are not listed.

## [Unreleased]

## [0.9.1.5] - 2026-11-09

### Added

- Added the `/trustedinstaller` launch mode. It starts a local application, or replaces the
  invoking parent shell, with `LocalSystem` privileges and the effective
  `NT SERVICE\TrustedInstaller` service identity. Like `/system`, it is available only to members
  of the local Administrators group, uses no stored credentials or approvals, and requires a
  separate non-storable confirmation on the protected private desktop for every invocation. The
  confirmation dialog shows a specific warning about changes to protected Windows files and
  registry entries. If the TrustedInstaller identity cannot be established, the launch fails
  without falling back to a normal `/system` launch.
- The broker now reports the validated effective identity of tracked elevated processes
  (elevated administrator account, `LocalSystem`, or TrustedInstaller) so that xucRun Tray can
  display it.

### Changed

- When an elevated cross-account launch is blocked because Admin Approval Mode is disabled for
  the target account, the error dialog and the notice in the local account selection now explain
  how to enable the mode and no longer show a misleading Windows error code.
- The README now describes the `LocalSystem` and TrustedInstaller launch modes in one section
  with screenshots of both confirmation dialogs and explains why these modes use neither stored
  credentials nor approvals.

## [0.9.1.4] - 2026-11-07

### Added

- Added and updated demonstration videos in the README.

### Fixed

- When Windows rejects entered credentials, xucRun now shows the reason reported by Windows and
  reopens the protected credential dialog instead of ending the current operation.
- The protected credential prompt now separates the authenticity marker from the credential-entry
  instruction correctly.

### Security

- xucRun now rejects target arguments containing non-printing, invisible, or bidirectional control
  characters before they can be displayed in approval or LocalSystem confirmation dialogs. This
  prevents those characters from visually altering the displayed launch details.

## [0.9.1.3] - 2026-11-04

### Added

- Account displays in credential, approval, administrative-status, and local-account-selection
  views now mark membership in the local Administrators group as `[A]`, standard accounts as
  `[S]`, and undetermined membership as `[?]`. The affected dialogs explain these markers.
- Local account selection now warns when Windows UAC is disabled or when Admin Approval Mode is
  unavailable for the built-in `Administrator` account and would prevent cross-account elevation.

### Changed

- Credential capture for opening the protected approval dialog now uses role-specific wording and
  distinguishes this helper account from the target account. The dialog explains that the
  credentials are stored in xucRun while, for the current launch, they are used to open the
  protected approval dialog.
- Information, security-diagnostics, administrative-status, local-account-selection, and
  approval-dialog titles now include the computer name. Local accounts are displayed without a
  redundant local-computer prefix.
- Credential-management notes now adapt their height to the localized text and current window
  width.
- The documentation has been reorganized and expanded to clarify account selection, elevation,
  deliberately restricted target accounts, profile and resource behavior, approvals, Windows
  UAC, Sudo for Windows, and `runas.exe`.

## [0.9.1.1] - 2026-11-01

### Added

- In `/showcred`, double-clicking a target account with approvals now opens the approvals that
  apply to that account. The context menu provides the same filtered view and a copyable credential
  detail view. The filtered list includes approvals bound to the selected account and profile-wide
  approvals that apply to all target accounts.
- `/showapprovals` and `/admin-status` now provide copyable row details through double-click and
  **Show details...** in the context menu. Credential details and approvals tied to a specific
  target account include its SID. Approval details include the complete target hash when present.

### Changed

- Target hashes are no longer shown in the approval and administrative-status tables. They remain
  available in the corresponding details. Detail views omit fields that are empty or do not apply
  to the selected entry.

## [0.9.1.0] - 2026-11-01

### Added

- Added `/user:*` and `/user-elevated:*` to always show the local account selection. The dialog
  identifies the current account and accounts with stored xucRun credentials; selecting another
  account without stored credentials continues through password entry on a separate Windows
  desktop.
- The approval dialog now summarizes relevant existing approvals for the same target and names
  differing bindings such as the program file, target account, elevation mode, arguments, working
  directory, visibility, or direct script. The effect of the current selection explains whether
  it adds, broadens, replaces, or remains overlaid by an existing approval.

### Changed

- `/user-elevated` now reuses the current account when it is already an administrator and requests
  UAC elevation only when necessary. Otherwise, xucRun selects an administrator account with
  stored credentials or offers credential capture on a separate Windows desktop.
- The approval dialog now presents the client and account context determined by the broker
  separately from the scrollable launch details. It shows detailed Authenticode information for
  the target program and a directly recognized script, including signature type, signer, issuer,
  certificate fingerprint and validity, and available timestamp information.
- Confirming a changed executable transfers both target-program and exact-invocation approvals to
  the new program hash. Exact-invocation argument, working-directory, visibility, and direct-script
  bindings remain unchanged.
- `/info` and the Tray information view now share a consistent runtime-information format covering
  program identity, architecture, language resources, signer certificate, account and elevation,
  configuration, maintenance scripts, and client logging.

### Security

- Authenticode diagnostics now distinguish a missing signature association from embedded-signature,
  catalog, certificate-chain, validity, revocation, and policy failures. Signature information
  supplements the existing final-path and SHA-256 checks. These diagnostics do not establish that
  a signed or unsigned target is free of harmful behavior.
- `/audit` now reports the results of checks for the configured Windows protections of the running
  broker process and for ACL protection, Authenticode trust, and the expected signer of the fixed
  Core and Tray maintenance scripts. A successful report describes the state observed during the
  check and is not a guarantee against defects or later changes.
- Reusable exact-invocation approvals for directly recognized scripts now require the broker's ACL
  check to find no prohibited client write or delete access in the script path. They bind the final
  script path and SHA-256 hash; the broker revalidates and locks the bound executable and script
  through process creation. The binding does not cover programs, scripts, modules, or other files
  loaded later by the approved process.

## [0.9.0.3] - 2026-10-29

### Added

- Added `/system [<Application> [Arguments...]]` for starting a local application as
  `LocalSystem` in the caller's interactive session.
- Added parameterless `/system` shell replacement. The replacement preserves the working
  directory and closes the previous shell only after the new shell started successfully.

### Security

- Every `/system` launch requires a separate, non-storable confirmation in the signed xucRun
  approval helper on the protected private desktop. The confirmation shows the exact executable,
  arguments, and working directory and is bound to a short-lived single-use challenge.
- The broker locks and revalidates the resolved executable and its path and holds the locks until
  the `LocalSystem` process has been created.

## [0.9.0.2] - 2026-10-23

### Note

- This is the initial public release of xucRun.
- Microsoft Visual C++ Redistributable v14 for x64 version `14.51.36247.0` or later is required.
  The graphical installer checks this requirement and offers the official Microsoft download if
  necessary.

[Unreleased]: https://github.com/HT64/xucRun/compare/v0.9.1.5...HEAD
[0.9.1.5]: https://github.com/HT64/xucRun/releases/tag/v0.9.1.5
[0.9.1.4]: https://github.com/HT64/xucRun/releases/tag/v0.9.1.4
[0.9.1.3]: https://github.com/HT64/xucRun/releases/tag/v0.9.1.3
[0.9.1.1]: https://github.com/HT64/xucRun/releases/tag/v0.9.1.1
[0.9.1.0]: https://github.com/HT64/xucRun/releases/tag/v0.9.1.0
[0.9.0.3]: https://github.com/HT64/xucRun/releases/tag/v0.9.0.3
[0.9.0.2]: https://github.com/HT64/xucRun/releases/tag/v0.9.0.2
