# Changelog

**Language:** English | [Deutsch](CHANGELOG.de.md)

This changelog covers only changes relevant to users of xucRun. Internal development, export,
and publication processes are not listed.

## [Unreleased]

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

[Unreleased]: https://github.com/HT64/xucRun/compare/v0.9.1.0...HEAD
[0.9.1.0]: https://github.com/HT64/xucRun/releases/tag/v0.9.1.0
[0.9.0.3]: https://github.com/HT64/xucRun/releases/tag/v0.9.0.3
[0.9.0.2]: https://github.com/HT64/xucRun/releases/tag/v0.9.0.2
