# xucRun Core installieren

**Sprache:** Deutsch | [English](../en/INSTALLATION.md)

Dieses Dokument beschreibt die grafische und die manuelle Installation von xucRun Core.

## Voraussetzungen

- ein x64-Windows-System,
- ein vollständiges Core-Paket,
- Administratorrechte,
- dieselbe vertrauenswürdige Authenticode-Signatur auf allen sechs installierbaren Paketdateien.

## Grafische Installation

xucRun Core ist ein nicht kommerzielles Open-Source-Projekt. Die bereitgestellte Installer-Datei
besitzt derzeit keine Signatur eines öffentlich vertrauenswürdigen Herausgebers. Windows kann
deshalb beim ersten Start einen unbekannten Herausgeber melden. Laden Sie den Installer
ausschließlich aus dem offiziellen GitHub Release herunter und vergleichen Sie vor dem Start die
dort veröffentlichte SHA-256-Prüfsumme.

Der empfohlene Installationsweg ist ein Doppelklick auf:

```text
xucRun-Core-Installer-<Version>-x64.exe
```

Bei Version 0.9.0.1 heißt die Datei beispielsweise
`xucRun-Core-Installer-0.9.0.1-x64.exe`. Der Dateiname wird bei der Paketerzeugung automatisch aus
der Produktversion der intern weiterhin als `Installer.exe` gebauten Datei erzeugt.

Die lokale SHA-256-Prüfsumme kann beispielsweise so angezeigt werden:

```powershell
Get-FileHash .\xucRun-Core-Installer-0.9.0.1-x64.exe -Algorithm SHA256
```

Der ausgegebene Hash muss vollständig mit dem Eintrag für diese Datei in der zum GitHub Release
gehörenden `SHA256SUMS.txt` übereinstimmen.

Die Windows-Benutzerkontensteuerung fordert die erforderlichen Administratorrechte an. Danach
können Programmsprache, Maschinen-PATH und Vertrauensmodell ausgewählt werden.

Für ein selbst erstelltes Paket bleibt **Lokales Zertifikat erstellen und Paketdateien
signieren** ausgewählt. Der Installer erzeugt oder verwendet ein nicht exportierbares,
rechnerlokales Codesignaturzertifikat, richtet das Vertrauen ausschließlich auf diesem Rechner
ein und signiert die sechs installierbaren Core-Dateien.

**Vorhandene Paketsignaturen verwenden** ist für ein vollständig vom Herausgeber signiertes Paket
bestimmt. Dabei müssen der versionierte grafische Installer und alle sechs eingebetteten
installierbaren Dateien vom selben gültigen Zertifikat signiert sein.

Fehlt Microsoft Visual C++ Redistributable v14 für x64, bietet der Installer den Download über den
offiziellen Microsoft-Link oder die Auswahl einer vorhandenen `vc_redist.x64.exe` an. Vor der
Ausführung prüft er Microsoft-Signatur und Originaldateiname.

## Manuelle Installation

Die Skripte ermöglichen Installation und Signierung ohne grafische Oberfläche. Dazu wird Windows
PowerShell 5.1 als Administrator im Paketverzeichnis gestartet.

### Lokale Testsignierung

Die mitgelieferte lokale Signierung ist für selbst erzeugte Builds und Tests bestimmt:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\UserCodeSigning\Sign-xucRunCorePackage.ps1 `
  -PackageDirectory .
```

Das Skript erzeugt standardmäßig ein nicht exportierbares, rechnerlokales Codesignaturzertifikat,
trägt es auf diesem Rechner als vertrauenswürdig ein, signiert die sechs installierbaren Dateien
und, sofern vorhanden, den versionierten grafischen Installer mit demselben Zertifikat und
aktualisiert `SHA256SUMS.json`.

Das lokale Zertifikat darf nicht auf andere Rechner verteilt werden. Die Deinstallation von
xucRun Core entfernt dieses separat erzeugte Zertifikat nicht.

### Installieren

Deutsch mit Maschinen-PATH:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\Install-xucRun-Core.ps1 `
  -PackageDirectory . `
  -Language de `
  -AddToSystemPath
```

Für Englisch ist `de` durch `en` zu ersetzen. Ohne `-Language` wird Deutsch bei einer deutschen
Windows-Anzeigesprache gewählt, andernfalls Englisch. Ohne `-AddToSystemPath` bleibt der
Maschinen-PATH unverändert.

Das Installationsskript prüft Paket und Signierer erneut, installiert nach
`C:\Program Files\xucRun`, konfiguriert den bedarfsgestarteten Brokerdienst und bindet dessen
sensible Clientanforderungen an Pfad und Signierer des installierten Clients.

## Aktualisieren

Ein vollständig signiertes neueres Paket wird erneut mit seinem versionierten grafischen Installer
oder demselben manuellen Installationsbefehl installiert. Auch eine erneute Installation derselben
Version erzeugt keine doppelten PATH-Einträge.

## Deinstallieren

Über **Installierte Apps** oder:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File 'C:\Program Files\xucRun\Install-xucRun-Core.ps1' `
  -Uninstall
```

Die Deinstallation entfernt Dienst, installierte Dateien, Uninstall-Eintrag, einen vom Installer
hinzugefügten PATH-Eintrag und aktive Brokerdaten. Separat erzeugte Testzertifikate werden nicht
entfernt.
