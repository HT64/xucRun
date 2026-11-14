<!--
Purpose: Explains how users build xucRun from source in English.
Approach: Lists prerequisites and reproducible Visual Studio and command-line build steps.
-->
# Building xucRun

**Language:** [Deutsch](../de/BUILDING.md) | English

This document describes how to build xucRun.

## Prerequisites

- an x64 Windows build system,
- Visual Studio or Build Tools with MSBuild and the Desktop C++ toolchain,
- a Windows 10 SDK,
- a compiler with C++20 support,
- Windows PowerShell 5.1 for package, signing, and installation scripts.

The projects use the available default MSVC toolset. Only the `Debug` and `Release` x64
configurations are supported.

## Build xucRun

```powershell
& MSBuild.exe .\XUCRun.sln `
  /t:Build /p:Configuration=Release /p:Platform=x64 /m
```

## Create the package

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\New-xucRun-Package.ps1 `
  -Configuration Release
```

The package is created under `artifacts\Package\Release`. Before installation, its six
installable files must be signed consistently. The intended local test signing process is
documented in [INSTALLATION.md](INSTALLATION.md).

## Create release assets

After a successful Release build, the following script creates the versioned standalone
installer, complete ZIP archive, and public SHA-256 file:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\New-xucRun-Release.ps1 `
  -ExpectedVersion 0.9.1.7
```

The files are created under `artifacts\Release\0.9.1.7`:

```text
xucRun-Installer-0.9.1.7-x64.exe
xucRun-0.9.1.7-x64.zip
SHA256SUMS.txt
```

The tag-triggered GitHub release workflow adds `BUILD-TOOLCHAIN.txt`. This file identifies the
source commit and the Visual Studio, MSBuild, MSVC, and Windows SDK versions actually resolved for
the build. Its SHA-256 checksum is also added to `SHA256SUMS.txt`.

The release script deliberately rejects signed build outputs. Public releases are provided
without a commercial publisher certificate; local signing takes place on the target computer
through the graphical installer or before manual installation.
