# Building xucRun Core

**Language:** [Deutsch](../de/BUILDING.md) | English

This document describes the public xucRun Core only.

## Prerequisites

- an x64 Windows build system,
- Visual Studio or Build Tools with MSBuild and the Desktop C++ toolchain,
- a Windows 10 SDK,
- a compiler with C++20 support,
- Windows PowerShell 5.1 for package, signing, and installation scripts.

The projects use the available default MSVC toolset. Only the `Debug` and `Release` x64
configurations are supported.

## Build the Core

```powershell
& MSBuild.exe .\XUCRun.sln `
  /t:Build /p:Configuration=Release /p:Platform=x64 /m
```

## Create the package

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\New-xucRun-CorePackage.ps1 `
  -Configuration Release
```

The package is created under `artifacts\CorePackage\Release`. Before installation, its six
installable files must be signed consistently. The intended local test signing process is
documented in [INSTALLATION.md](INSTALLATION.md).

## Create release assets

After a successful Release build, the following script creates the versioned standalone
installer, complete ZIP archive, and public SHA-256 file:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\New-xucRun-CoreRelease.ps1 `
  -ExpectedVersion 0.9.0.1
```

The files are created under `artifacts\Release\0.9.0.1`:

```text
xucRun-Core-Installer-0.9.0.1-x64.exe
xucRun-Core-0.9.0.1-x64.zip
SHA256SUMS.txt
```

The release script deliberately rejects signed build outputs. Public releases are provided
without a commercial publisher certificate; local signing takes place on the target computer
through the graphical installer or before manual installation.

A tag in the format `v0.9.0.1` starts the GitHub release workflow. It verifies the build, tests,
version, and package and initially creates a draft release for manual review. Once the repository
is public, the workflow also creates a GitHub artifact attestation. GitHub does not provide this
feature for user-owned private repositories.
