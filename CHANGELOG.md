# Changelog

**Language:** English | [Deutsch](CHANGELOG.de.md)

This changelog covers only changes relevant to users of xucRun. Internal development, export,
and publication processes are not listed.

## [Unreleased]

## [0.9.0.2] - 2026-10-23

### Note

- This is the initial public release of xucRun.

### Changed

- For universal command and script interpreters, the approval dialog now warns directly within the
  existing window that reusable approvals are not bound to arguments and recommends careful
  verification and a one-time approval whenever possible. For PowerShell, it also identifies
  `AllSigned` as limited additional hardening.
- The installer now displays the detected Microsoft Visual C++ Runtime x64 version and its
  compatibility. If installation or an update is required, it states the minimum version in the
  prompt, rejects an older selected redistributable before execution, and reports a Runtime change
  with the resulting version in the success confirmation.
- The graphical installer now always creates or reuses the machine-local xucRun certificate and
  no longer offers an inapplicable publisher-signature choice.
- Reinstalling the same Core version no longer requires removing a matching Tray installation.
- The installer, `/info`, and `/audit` now identify the local signing certificate and its validity
  period; command diagnostics use its SHA-256 fingerprint.
- For an existing installation, the installer now shows its registered version and the time it
  was installed or last updated.
- The broker, approval helper, and both language DLLs now include complete Windows file details
  with descriptions, product versions, and original file names.
- Installation and uninstallation now confirm the used or removed local certificate with its
  name, validity period, and thumbprint. Uninstall removes the verified standard xucRun
  certificate, including its private key and trust entries.
- If uninstall is blocked by an installed Tray add-on or another error, it now displays a visible
  localized error dialog instead of ending without user-visible feedback.
- Repair installation now restores a previously running broker service after replacing its files.
- The installer uses a more compact settings layout and explains where its application language
  can be changed later.
- Interactive uninstall now requires confirmation before deleting stored product data and lists
  removed components, data, and certificate details on success.
- Uninstall from Installed apps now hides the PowerShell host. Failures remain visible in a
  localized dialog that identifies the persistent diagnostic log and can open its folder.
- New English and German manual-removal documentation inventories xucRun-owned files, service and
  registry entries, broker data, credentials, optional logs, certificate stores, and interrupted
  operation remnants, with a safe removal and verification procedure.

[Unreleased]: https://github.com/HT64/xucRun/compare/v0.9.0.2...HEAD
[0.9.0.2]: https://github.com/HT64/xucRun/releases/tag/v0.9.0.2
