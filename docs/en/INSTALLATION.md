<!--
Purpose: Explains how users install xucRun in English.
Approach: Walks through package verification, installer choices, and post-install checks.
-->
# Installing xucRun

**Language:** [Deutsch](../de/INSTALLATION.md) | English

## Prerequisites

- an x64 Windows system,
- the installer or fully extracted ZIP package from an xucRun release,
- administrator rights.

## Release package

xucRun is an open-source project under the [Apache License 2.0](../../LICENSE).
A public Core release contains:

```text
xucRun-Installer-<Version>-x64.exe
xucRun-<Version>-x64.zip
BUILD-TOOLCHAIN.txt
SHA256SUMS.txt
```

`BUILD-TOOLCHAIN.txt` identifies the source commit and the Visual Studio, MSBuild, MSVC, and
Windows SDK versions actually resolved for the build. Its SHA-256 checksum is also included in
`SHA256SUMS.txt`.

Download the required files only from the same
[xucRun GitHub release](https://github.com/HT64/xucRun/releases/latest). The provided installer is
not Authenticode-signed. Windows may therefore report an unknown publisher when it is first
started.

Display the installer's local SHA-256 checksum, for example, with:

```powershell
Get-FileHash .\xucRun-Installer-*-x64.exe -Algorithm SHA256
```

The complete hash must match the entry for that file in `SHA256SUMS.txt`. If the hashes differ, do
not run the file.

The ZIP package contains the executables, language resources, installation scripts, graphical
installer, `LICENSE`, and `NOTICE`. It is intended for the script-based installation described
below and is not a portable application package.

## Graphical installation

The recommended installation method is to double-click:

```text
xucRun-Installer-<Version>-x64.exe
```

Windows User Account Control requests the required administrator rights. The application language
and machine PATH behavior can then be selected. Every installation and update requires users to
agree to the license terms and confirm that they understand the security information.
The installer provides the license text, notices, and the README's status and security notices
offline on separate tabs. A link opens the README.

For an existing installation, the interface shows its registered version and the time it was
installed or last updated.

If Microsoft Visual C++ Redistributable v14 for x64 is missing, the installer offers either the
official Microsoft download or selection of an existing `vc_redist.x64.exe`. It verifies the
Microsoft signature and original file name before execution.

## Local signing and trust

The graphical installer performs local signing automatically. Manual installation runs the same
process separately before the installation script.

The installer automatically looks for and reuses an existing valid local xucRun code-signing
certificate with its private key. If none exists, it creates a new non-exportable machine-local
certificate. It configures trust only on that computer and consistently signs all installable
xucRun components. The interface shows in advance whether the certificate will be reused or
created. For an existing certificate, it also displays “Valid from”, “Valid until”, and the
thumbprint.

The certificate lets Windows and xucRun verify that the installed program components belong to the
same installation and have not been changed or replaced with differently signed files. The broker
accepts security-sensitive requests only from matching signed xucRun components. A unique
self-signed certificate is created or reused locally during installation and is not included in the
public package. The standard
certificate is stored in `LocalMachine\My` with its non-exportable private key and added to
`LocalMachine\Root` and `LocalMachine\TrustedPublisher` for local trust. It establishes technical
trust only on this computer and does not prove the origin of the downloaded package.

Do not distribute the local certificate to other computers. During uninstallation, xucRun
removes its verified standard machine-local certificate, including its private key, from the
`My`, `Root`, and `TrustedPublisher` certificate stores. Separately created test certificates
with custom names are not removed indiscriminately.

## Not a portable application package

Both installation methods place `LICENSE-xucRun.txt` and `NOTICE-xucRun.txt` in
`%ProgramFiles%\xucRun\Licenses`. These text files are not Authenticode-signed.

Merely copying or directly starting the included EXE and DLL files does not create a complete,
supported xucRun installation. In particular, it omits:

- the local xucRun signing certificate and its trust entries;
- consistently locally signed program files;
- the protected installation and language directories under `%ProgramFiles%\xucRun`;
- the `LocalSystem` broker service, including its service SID and restricted service DACL;
- the event source and broker configuration;
- the binding of sensitive broker requests to the canonical client path and signer;
- `xucRun.ini` and the Windows uninstall registration.

Therefore, either run the graphical installer or use the manual installation below. In this
guide, “manual” means installation through the included PowerShell scripts, not copying the
program files individually.

## Manual installation

The scripts support installation and signing without the graphical interface. Start Windows
PowerShell 5.1 as an administrator in the package directory.

### Local package signing

For manual installation, run local signing separately:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\UserCodeSigning\Sign-xucRunPackage.ps1 `
  -PackageDirectory .
```

The script reuses an existing valid local xucRun code-signing certificate with its private key. If
none exists, it creates the certificate described under
[Local signing and trust](#local-signing-and-trust). It signs the installable xucRun components
and the versioned graphical installer when present and updates `SHA256SUMS.json`.

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

## Verify the installation

After graphical or manual installation, verify the runtime information and security-relevant
properties of the installation:

```powershell
& 'C:\Program Files\xucRun\xucRun.exe' /info
& 'C:\Program Files\xucRun\xucRun.exe' /audit
```

`/info` shows, among other details, the version, signing certificate, broker status, and
configuration. `/audit` checks security-relevant files, signatures, permissions, and Windows
protections of the running installation.

## Update and repair

Install a newer package by running its versioned graphical installer. The existing valid local
xucRun certificate is reused. For manual installation, run local package signing first and then
use the same installation command. Reinstalling the same version does not create duplicate PATH
entries.

When reinstalling the same xucRun version, the separately installed optional
[xucRun Tray add-on](https://github.com/HT64/xucRun-Tray/blob/main/docs/en/INSTALLATION.md) can
remain installed if its registered
version, file version, and signers still match the Core package exactly. Remove it through its
separate Windows uninstall entry before changing versions. After the xucRun update, install a
Tray release with the exact same four-part version by following the linked Tray installation guide.
The graphical installer shows this compatibility before installation begins and disables
installation when it detects a conflict. The installation script checks it again immediately
before every installation, including manual invocation.
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
key and trust entries. Normal uninstallation requires explicit confirmation before its first
change. Cancelling leaves the installation and data unchanged. Separately created test
certificates with custom names are not removed.

An error during normal uninstallation remains visible in a dialog even when it was started through
**Installed apps**. Its technical diagnostic log is written when possible under
`%ProgramData%\xucRun\InstallerLogs`, and the dialog shows its full path. The log contains error,
script, and system information, but no stored passwords. A later successful Core uninstall also
removes these logs.

Remove the optional Tray add-on before uninstalling xucRun. While it remains installed, Core
uninstallation removes no components or stored data and instructs the user to uninstall
**xucRun Tray** through **Installed apps** first.
