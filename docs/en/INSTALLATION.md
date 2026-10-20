# Installing xucRun Core

**Language:** [Deutsch](../de/INSTALLATION.md) | English

This document describes graphical and manual installation of xucRun Core.

## Prerequisites

- an x64 Windows system,
- a complete Core package,
- administrator rights,
- the same trusted Authenticode signature on all six installable package files.

## Graphical installation

xucRun Core is a non-commercial open-source project. The provided installer is currently not
signed by a publicly trusted publisher. Windows may therefore report an unknown publisher when
it is first started. Download the installer only from the official GitHub Release and compare its
SHA-256 checksum with the checksum published there before starting it.

The recommended installation method is to double-click:

```text
xucRun-Core-Installer-<Version>-x64.exe
```

For version 0.9.0.1, for example, the file is named
`xucRun-Core-Installer-0.9.0.1-x64.exe`. Package creation derives the name automatically from the
product version of the file that is still built internally as `Installer.exe`.

Display the local SHA-256 checksum, for example, with:

```powershell
Get-FileHash .\xucRun-Core-Installer-0.9.0.1-x64.exe -Algorithm SHA256
```

The complete hash must match the entry for that file in `SHA256SUMS.txt` from the corresponding
GitHub Release.

Windows User Account Control requests the required administrator rights. The application
language, machine PATH behavior, and trust model can then be selected.

For a self-built package, leave **Create a local certificate and sign the package files**
selected. The installer creates or reuses a non-exportable machine-local code-signing
certificate, configures trust only on that computer, and signs the six installable Core files.

**Use the existing package signatures** is intended for a package fully signed by the publisher.
The versioned graphical installer and all six embedded installable files must be signed by the
same valid certificate.

If Microsoft Visual C++ Redistributable v14 for x64 is missing, the installer offers either the
official Microsoft download or selection of an existing `vc_redist.x64.exe`. It verifies the
Microsoft signature and original file name before execution.

## Manual installation

The scripts support installation and signing without the graphical interface. Start Windows
PowerShell 5.1 as an administrator in the package directory.

### Local test signing

The included local signing workflow is intended for self-built packages and testing:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\UserCodeSigning\Sign-xucRunCorePackage.ps1 `
  -PackageDirectory .
```

By default, the script creates a non-exportable machine-local code-signing certificate, trusts it
on that machine, signs all six installable files and the versioned graphical installer when
present with the same certificate, and updates `SHA256SUMS.json`.

Do not distribute the local certificate to other computers. Uninstalling xucRun Core does not
remove this separately created certificate.

### Install

German with a machine PATH entry:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\Install-xucRun-Core.ps1 `
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

Install a completely signed newer package by running its versioned graphical installer or by
using the same manual installation command. Reinstalling the same version does not create
duplicate PATH entries.

## Uninstall

Use **Installed apps** or:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File 'C:\Program Files\xucRun\Install-xucRun-Core.ps1' `
  -Uninstall
```

Uninstall removes the service, installed files, uninstall registration, a PATH entry added by the
installer, and active broker data. Separately created test certificates are not removed.
