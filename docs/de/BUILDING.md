# xucRun bauen

**Sprache:** Deutsch | [English](../en/BUILDING.md)

Dieses Dokument beschreibt den Build von xucRun.

## Voraussetzungen

- ein x64-Windows-Buildsystem,
- Visual Studio oder Build Tools mit MSBuild und der Desktop-C++-Toolchain,
- ein Windows 10 SDK,
- ein Compiler mit C++20-Unterstützung,
- Windows PowerShell 5.1 für Paket-, Signier- und Installationsskripte.

Die Projekte verwenden das jeweils verfügbare Standard-MSVC-Toolset. Unterstützt werden nur die
x64-Konfigurationen `Debug` und `Release`.

## xucRun bauen

```powershell
& MSBuild.exe .\XUCRun.sln `
  /t:Build /p:Configuration=Release /p:Platform=x64 /m
```

## Paket erstellen

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\New-xucRun-Package.ps1 `
  -Configuration Release
```

Das Paket entsteht unter `artifacts\Package\Release`. Vor der Installation müssen seine sechs
installierbaren Dateien einheitlich signiert werden. Die dafür vorgesehene lokale Testsignierung
ist in [INSTALLATION.md](INSTALLATION.md) beschrieben.

## Release-Dateien erstellen

Nach einem erfolgreichen Release-Build erzeugt das folgende Skript den versionierten
Einzelinstaller, das vollständige ZIP-Archiv und die öffentliche SHA-256-Datei:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\New-xucRun-Release.ps1 `
  -ExpectedVersion 0.9.0.1
```

Die Dateien entstehen unter `artifacts\Release\0.9.0.1`:

```text
xucRun-Installer-0.9.0.1-x64.exe
xucRun-0.9.0.1-x64.zip
SHA256SUMS.txt
```

Das Release-Skript lehnt signierte Buildausgaben bewusst ab. Öffentliche Releases werden ohne
kommerzielles Herausgeberzertifikat bereitgestellt; die lokale Signierung erfolgt auf dem
Zielrechner über den grafischen Installer oder vor der manuellen Installation.

Ein Tag im Format `v0.9.0.1` startet den GitHub-Release-Workflow. Er prüft Build, Tests, Version und
Paket und legt zunächst einen Draft Release zur manuellen Kontrolle an. Sobald das Repository
öffentlich ist, erzeugt der Workflow zusätzlich eine GitHub-Artefaktattestierung. GitHub bietet
diese Funktion für benutzereigene private Repositories nicht an.

Vor dem Tag müssen `CHANGELOG.md` und `CHANGELOG.de.md` einen nicht leeren Abschnitt für dieselbe
Version enthalten. Der Workflow übernimmt beide Abschnitte in die Release-Beschreibung und bricht
bei einer fehlenden oder abweichenden Version ab. Ein bereits vorhandener Draft Release wird
aktualisiert; ein bereits veröffentlichter Release derselben Version wird nicht überschrieben.
