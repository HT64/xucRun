<!--
Purpose: Explains how users install xucRun in English.
Approach: Walks through package verification, installer choices, and post-install checks.
-->
# Installing xucRun

**Language:** [Deutsch](../de/INSTALLATION.md) | English

This document describes graphical and manual installation of xucRun.

## Prerequisites

- an x64 Windows system,
- a complete xucRun package,
- administrator rights.

## Graphical installation

xucRun is a non-commercial open-source project. The provided installer is currently not
signed by a publicly trusted publisher. Windows may therefore report an unknown publisher when
it is first started. Download the installer only from the official GitHub Release and compare its
SHA-256 checksum with the checksum published there before starting it.

The recommended installation method is to double-click:

```text
xucRun-Installer-<Version>-x64.exe
```

For version 0.9.0.3, for example, the file is named
`xucRun-Installer-0.9.0.3-x64.exe`. Package creation derives the name automatically from the
product version of the file that is still built internally as `Installer.exe`.

Display the local SHA-256 checksum, for example, with:

```powershell
Get-FileHash .\xucRun-Installer-0.9.0.3-x64.exe -Algorithm SHA256
```

The complete hash must match the entry for that file in `SHA256SUMS.txt` from the corresponding
GitHub Release.

Windows User Account Control requests the required administrator rights. The application language
and machine PATH behavior can then be selected.

For an existing installation, the interface shows its registered version and the time it was
installed or last updated.

The installer automatically looks for and reuses an existing valid local xucRun code-signing
certificate with its private key. If none exists, it creates a new non-exportable machine-local
certificate. It configures trust only on that computer and signs the six installable files. The
interface shows in advance whether the certificate will be reused or created. For an existing
certificate, it also displays “Valid from”, “Valid until”, and the thumbprint.

The certificate lets Windows and xucRun verify that the installed program components belong to the
same installation and have not been changed or replaced with differently signed files. The broker
accepts security-sensitive requests only from matching signed xucRun components. A unique
self-signed certificate is generated locally because the public package has no publicly trusted
publisher certificate and must not distribute a reusable private signing key. The standard
certificate is stored in `LocalMachine\My` with its non-exportable private key and added to
`LocalMachine\Root` and `LocalMachine\TrustedPublisher` for local trust. It establishes technical
trust only on this computer and does not prove the origin of the downloaded package.

If Microsoft Visual C++ Redistributable v14 for x64 is missing, the installer offers either the
official Microsoft download or selection of an existing `vc_redist.x64.exe`. It verifies the
Microsoft signature and original file name before execution.

## Manual installation

The scripts support installation and signing without the graphical interface. Start Windows
PowerShell 5.1 as an administrator in the package directory.

### Local package signing

The graphical installer runs the same local signing workflow automatically. For manual
installation, run it separately:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\UserCodeSigning\Sign-xucRunPackage.ps1 `
  -PackageDirectory .
```

The script reuses an existing valid local xucRun code-signing certificate with its private key. If
none exists, it creates a new non-exportable certificate. It trusts the certificate on that
machine, signs all six installable files and the versioned graphical installer when present with
it, and updates `SHA256SUMS.json`.

Do not distribute the local certificate to other computers. During uninstallation, xucRun
removes its verified standard machine-local certificate, including its private key, from the
`My`, `Root`, and `TrustedPublisher` certificate stores. Separately created test certificates
with custom names are not removed indiscriminately.

### Install

German with a machine PATH entry:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\Install-xucRun.ps1 `
  -PackageDirectory . `
  -Language de `
  -AddToSystemPath
```

Replace `de` with `en` for English. Without `-Language`, German is selected for a German Windows
UI language and English otherwise. Without `-AddToSystemPath`, the machine PATH is unchanged.

The installation script revalidates the package and signer, installs to
`C:\Program Files\xucRun`, configures the demand-start broker service, and binds sensitive client
requests to the installed client path and signer.

## Update

Install a newer package by running its versioned graphical installer. The existing valid local
xucRun certificate is reused. For manual installation, run local package signing first and then
use the same installation command. Reinstalling the same version does not create duplicate PATH
entries.

When reinstalling the same xucRun version, the separately installed optional
[xucRun Tray add-on](https://github.com/HT64/xucRun-Tray) can remain installed if its registered
version, file version, and signers still match the Core package exactly. Remove it through its
separate Windows uninstall entry before changing versions. After the xucRun update, install a
Tray release with the exact same four-part version from the public Tray repository.
If the broker service was running before reinstallation, the installer starts it again after
replacing the files, restoring its previous running state. A broker service that was previously
stopped or disabled remains stopped or disabled.

## Uninstall

Use **Installed apps** or:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File 'C:\Program Files\xucRun\Install-xucRun.ps1' `
  -Uninstall
```

Use the [manual removal instructions](MANUAL_REMOVAL.md) only if the normal uninstaller is
unavailable or cannot complete. They document the xucRun-owned artifacts covered by the procedure
and preserve the required order for purging broker-managed secrets before deleting the service and
files.

> **Warning:** Uninstall permanently deletes all credentials and approvals stored by xucRun for
> every Windows user, together with all local xucRun backups, security markers, and associated
> metadata.

Uninstall also removes the service, installed files, uninstall registration, a PATH entry added by
the installer, and the verified standard machine-local xucRun certificate, including its private
key and trust entries. Uninstall requires explicit confirmation before its first change. Cancelling
leaves the installation and data unchanged. The success confirmation lists the removed components,
all stored data that may have been present, and the removed certificate's name, validity period,
and thumbprint. Separately created test certificates with custom names are not removed.

An uninstall failure remains visible in a dialog even when uninstall was started through
**Installed apps**. Its technical diagnostic log is stored under
`%ProgramData%\xucRun\InstallerLogs`; the dialog shows the full path of the specific file in a
copyable field and can open the folder directly. The log contains error, script, and system
information, but no stored passwords. The path shown in the dialog is authoritative. A later
successful Core uninstall also removes these logs.

Remove the optional Tray add-on before uninstalling xucRun. Repair installation of the same Core
version is allowed with a completely matching Tray; version changes or differing Tray signers stop
with an explanatory message.
If uninstall is started while Tray is still installed, no Core components or stored data are
removed. A visible error dialog instructs the user to uninstall **xucRun Tray** through
**Installed apps** first and then start the xucRun uninstall again.
