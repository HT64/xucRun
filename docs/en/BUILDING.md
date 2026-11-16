<!--
Purpose: Explains how users build xucRun from source in English.
Approach: Lists prerequisites and reproducible Visual Studio and command-line build steps.
-->
# Building xucRun

**Language:** [Deutsch](../de/BUILDING.md) | English

## Prerequisites

- an x64 Windows build system,
- Visual Studio 2022 or Build Tools 2022 with MSBuild and the Desktop C++ toolchain,
- a Windows 10 SDK,
- a compiler with C++20 support,
- Windows PowerShell 5.1 for package, signing, and installation scripts.

The projects use the available default MSVC toolset. Only the `Debug` and `Release` x64
configurations are supported.

Run all commands from the repository root in **Developer PowerShell for Visual Studio 2022** or an
equivalently initialized Build Tools environment.

## Debug build

```powershell
& MSBuild.exe .\XUCRun.sln `
  /t:Build /p:Configuration=Debug /p:Platform=x64 /m
```

## Release build

```powershell
& MSBuild.exe .\XUCRun.sln `
  /t:Build /p:Configuration=Release /p:Platform=x64 /m
```

## Toolchain report

For a release build, durably record the exact Visual Studio, MSBuild, MSVC, and Windows SDK
versions used:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\New-BuildToolchainReport.ps1 `
  -ProjectPath .\xucRun\XUCRun.vcxproj `
  -ProductName xucRun `
  -Configuration Release `
  -DestinationPath .\artifacts\BUILD-TOOLCHAIN.txt
```

The report belongs with the release artifacts but is not included in the installation package by
`New-xucRun-Package.ps1`.

## Create the package

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\New-xucRun-Package.ps1 `
  -Configuration Release
```

Replace `Release` with `Debug` to create a Debug package. The package is written to
`artifacts\Package\<Configuration>`.

## Validate the package

`New-xucRun-Package.ps1` checks the required build outputs and expected package contents while
creating the package. The complete installer smoke test installs, updates, and removes xucRun and
is therefore suitable only for a clean, disposable Windows test system running elevated:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\Test-Installer.ps1 `
  -PackageDirectory .\artifacts\Package\Release
```

Before a normal installation, the package's installable xucRun components must be signed
consistently on the local computer. The intended process is documented under
[Local signing and trust](INSTALLATION.md#local-signing-and-trust).
