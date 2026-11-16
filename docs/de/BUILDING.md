<!--
Purpose: Explains how users build xucRun from source in German.
Approach: Lists prerequisites and reproducible Visual Studio and command-line build steps.
-->
# xucRun bauen

**Sprache:** Deutsch | [English](../en/BUILDING.md)

## Voraussetzungen

- ein x64-Windows-Buildsystem,
- Visual Studio 2022 oder Build Tools 2022 mit MSBuild und der Desktop-C++-Toolchain,
- ein Windows 10 SDK,
- ein Compiler mit C++20-Unterstützung,
- Windows PowerShell 5.1 für Paket-, Signier- und Installationsskripte.

Die Projekte verwenden das jeweils verfügbare Standard-MSVC-Toolset. Unterstützt werden nur die
x64-Konfigurationen `Debug` und `Release`.

Alle Befehle in einer **Developer PowerShell for Visual Studio 2022** oder einer entsprechend
initialisierten Build-Tools-Umgebung im Stammverzeichnis des Repositorys ausführen.

## Debug-Build

```powershell
& MSBuild.exe .\XUCRun.sln `
  /t:Build /p:Configuration=Debug /p:Platform=x64 /m
```

## Release-Build

```powershell
& MSBuild.exe .\XUCRun.sln `
  /t:Build /p:Configuration=Release /p:Platform=x64 /m
```

## Toolchain-Bericht

Für einen Release-Build die tatsächlich verwendeten Versionen von Visual Studio, MSBuild, MSVC und
Windows SDK dauerhaft festhalten:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\New-BuildToolchainReport.ps1 `
  -ProjectPath .\xucRun\XUCRun.vcxproj `
  -ProductName xucRun `
  -Configuration Release `
  -DestinationPath .\artifacts\BUILD-TOOLCHAIN.txt
```

Der Bericht gehört zu den Releaseartefakten, wird aber nicht von `New-xucRun-Package.ps1` in das
Installationspaket aufgenommen.

## Paket erstellen

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\New-xucRun-Package.ps1 `
  -Configuration Release
```

Für ein Debug-Paket `Release` durch `Debug` ersetzen. Das Paket entsteht unter
`artifacts\Package\<Configuration>`.

## Paket prüfen

`New-xucRun-Package.ps1` prüft beim Erstellen die benötigten Buildausgaben und den erwarteten
Paketinhalt. Der vollständige Installer-Smoke-Test installiert, aktualisiert und entfernt xucRun
und ist deshalb nur für ein sauberes, entbehrliches und erhöht ausgeführtes Windows-Testsystem
geeignet:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\Test-Installer.ps1 `
  -PackageDirectory .\artifacts\Package\Release
```

Vor einer normalen Installation müssen die installierbaren xucRun-Komponenten des Pakets
einheitlich lokal signiert werden. Der vorgesehene Ablauf ist unter
[Lokale Signierung und Vertrauen](INSTALLATION.md#lokale-signierung-und-vertrauen) beschrieben.
