<!--
Purpose: Explains how users install xucRun in German.
Approach: Walks through package verification, installer choices, and post-install checks.
-->
# xucRun installieren

**Sprache:** Deutsch | [English](../en/INSTALLATION.md)

Dieses Dokument beschreibt die grafische und die manuelle Installation von xucRun.

## Voraussetzungen

- ein x64-Windows-System,
- ein vollständiges xucRun-Paket,
- Administratorrechte.

## Grafische Installation

xucRun ist ein nicht kommerzielles Open-Source-Projekt. Die bereitgestellte Installer-Datei
besitzt derzeit keine Signatur eines öffentlich vertrauenswürdigen Herausgebers. Windows kann
deshalb beim ersten Start einen unbekannten Herausgeber melden. Laden Sie den Installer
ausschließlich aus dem offiziellen GitHub Release herunter und vergleichen Sie vor dem Start die
dort veröffentlichte SHA-256-Prüfsumme.

Der empfohlene Installationsweg ist ein Doppelklick auf:

```text
xucRun-Installer-<Version>-x64.exe
```

Bei Version 0.9.0.3 heißt die Datei beispielsweise
`xucRun-Installer-0.9.0.3-x64.exe`. Der Dateiname wird bei der Paketerzeugung automatisch aus
der Produktversion der intern weiterhin als `Installer.exe` gebauten Datei erzeugt.

Die lokale SHA-256-Prüfsumme kann beispielsweise so angezeigt werden:

```powershell
Get-FileHash .\xucRun-Installer-0.9.0.3-x64.exe -Algorithm SHA256
```

Der ausgegebene Hash muss vollständig mit dem Eintrag für diese Datei in der zum GitHub Release
gehörenden `SHA256SUMS.txt` übereinstimmen.

Die Windows-Benutzerkontensteuerung fordert die erforderlichen Administratorrechte an. Danach
können Programmsprache und Maschinen-PATH ausgewählt werden.

Bei einer vorhandenen Installation zeigt die Oberfläche deren registrierte Version und den
Zeitpunkt der Installation beziehungsweise letzten Aktualisierung an.

Der Installer sucht automatisch nach einem bereits vorhandenen gültigen lokalen xucRun-
Codesignaturzertifikat mit privatem Schlüssel und verwendet es erneut. Ist keines vorhanden,
erzeugt er ein neues nicht exportierbares, rechnerlokales Zertifikat. Er richtet das Vertrauen
ausschließlich auf diesem Rechner ein und signiert die sechs installierbaren Dateien. Die
Oberfläche zeigt vorab, ob das Zertifikat wiederverwendet oder neu erstellt wird. Bei einem
vorhandenen Zertifikat zeigt sie außerdem „Gültig seit“, „Gültig bis“ und den Fingerabdruck an.

Das Zertifikat ermöglicht Windows und xucRun die Prüfung, dass die installierten Programmteile zu
derselben Installation gehören und nicht verändert oder durch abweichend signierte Dateien
ersetzt wurden. Der Broker akzeptiert sicherheitsrelevante Anfragen nur von passend signierten
xucRun-Komponenten. Das eindeutige selbstsignierte Zertifikat wird lokal erzeugt, weil das
öffentliche Paket kein öffentlich vertrauenswürdiges Herausgeberzertifikat besitzt und keinen
wiederverwendbaren privaten Signaturschlüssel verteilen darf. Das Standardzertifikat wird mit
seinem nicht exportierbaren privaten Schlüssel in `LocalMachine\My` gespeichert und für das lokale
Vertrauen zusätzlich in `LocalMachine\Root` und `LocalMachine\TrustedPublisher` eingetragen. Es
begründet ausschließlich technisches Vertrauen auf diesem Rechner und belegt nicht die Herkunft
des heruntergeladenen Pakets.

Fehlt Microsoft Visual C++ Redistributable v14 für x64, bietet der Installer den Download über den
offiziellen Microsoft-Link oder die Auswahl einer vorhandenen `vc_redist.x64.exe` an. Vor der
Ausführung prüft er Microsoft-Signatur und Originaldateiname.

## Manuelle Installation

Die Skripte ermöglichen Installation und Signierung ohne grafische Oberfläche. Dazu wird Windows
PowerShell 5.1 als Administrator im Paketverzeichnis gestartet.

### Lokale Paketsignierung

Der grafische Installer führt dieselbe lokale Signierung automatisch aus. Für den manuellen
Installationsweg wird sie separat gestartet:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\UserCodeSigning\Sign-xucRunPackage.ps1 `
  -PackageDirectory .
```

Das Skript verwendet ein bereits vorhandenes gültiges lokales xucRun-Codesignaturzertifikat mit
privatem Schlüssel erneut. Ist keines vorhanden, erzeugt es ein neues nicht exportierbares
Zertifikat. Es trägt das Zertifikat auf diesem Rechner als vertrauenswürdig ein, signiert die sechs
installierbaren Dateien und, sofern vorhanden, den versionierten grafischen Installer damit und
aktualisiert `SHA256SUMS.json`.

Das lokale Zertifikat darf nicht auf andere Rechner verteilt werden. Bei der Deinstallation
entfernt xucRun sein verifiziertes rechnerlokales Standardzertifikat einschließlich privatem
Schlüssel aus den Zertifikatspeichern `My`, `Root` und `TrustedPublisher`. Individuell benannte,
separat erzeugte Testzertifikate werden nicht pauschal gelöscht.

### Installieren

Deutsch mit Maschinen-PATH:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\Install-xucRun.ps1 `
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

Ein neueres Paket wird erneut mit seinem versionierten grafischen Installer installiert. Dabei
wird das vorhandene gültige lokale xucRun-Zertifikat wiederverwendet. Beim manuellen Weg muss
zuerst die lokale Paketsignierung und danach derselbe Installationsbefehl ausgeführt werden. Auch
eine erneute Installation derselben Version erzeugt keine doppelten PATH-Einträge.

Bei einer erneuten Installation derselben xucRun-Version kann das separat installierte optionale
[xucRun-Tray-Add-on](https://github.com/HT64/xucRun-Tray) installiert bleiben, wenn seine
registrierte Version, Dateiversion und Signierer weiterhin exakt zum Core-Paket passen. Vor einem
Versionswechsel muss es über seinen eigenen Windows-Deinstallationseintrag entfernt werden. Nach
dem xucRun-Update kann aus dem öffentlichen Tray-Repository eine Tray-Version mit exakt derselben
vierteiligen Version installiert werden.
War der Brokerdienst vor der erneuten Installation aktiv, startet ihn der Installer nach dem
Dateiaustausch automatisch wieder und stellt damit seinen vorherigen Laufzustand wieder her. Ein
zuvor gestoppter oder deaktivierter Brokerdienst bleibt gestoppt beziehungsweise deaktiviert.

## Deinstallieren

Über **Installierte Apps** oder:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File 'C:\Program Files\xucRun\Install-xucRun.ps1' `
  -Uninstall
```

Verwenden Sie die Anleitung zur [manuellen Entfernung](MANUAL_REMOVAL.md) nur, wenn die normale
Deinstallation nicht verfügbar ist oder nicht abgeschlossen werden kann. Sie dokumentiert die vom
Verfahren erfassten xucRun-eigenen Artefakte und erhält die erforderliche Reihenfolge, in der die
vom Broker verwalteten Geheimnisse vor Dienst und Dateien gelöscht werden.

> **Achtung:** Die Deinstallation löscht unwiderruflich alle von xucRun gespeicherten
> Anmeldedaten und Freigaben aller Windows-Benutzer sowie sämtliche lokalen xucRun-Sicherungen,
> Sicherheitsmerkmale und zugehörigen Metadaten.

Zusätzlich entfernt die Deinstallation Dienst, installierte Dateien, Uninstall-Eintrag, einen vom
Installer hinzugefügten PATH-Eintrag und das verifizierte rechnerlokale
xucRun-Standardzertifikat samt privatem Schlüssel und Vertrauenseinträgen. Die
Deinstallation verlangt vor der ersten Änderung eine ausdrückliche Bestätigung. Bei Abbruch bleiben
Installation und Daten unverändert. Die Erfolgsbestätigung führt die entfernten Komponenten, alle
gegebenenfalls vorhandenen gespeicherten Daten sowie Name, Gültigkeitszeitraum und Fingerabdruck
des entfernten Zertifikats auf.
Separat erzeugte, individuell benannte Testzertifikate werden nicht entfernt.

Ein Deinstallationsfehler wird auch beim Start über **Installierte Apps** in einem sichtbaren
Dialog angezeigt. Das technische Diagnoseprotokoll liegt unter
`%ProgramData%\xucRun\InstallerLogs`; der Dialog zeigt den vollständigen konkreten Dateipfad in
einem kopierbaren Feld und kann den Ordner direkt öffnen. Das Protokoll enthält Fehler-, Skript-
und Systeminformationen, aber keine gespeicherten Kennwörter. Maßgeblich ist der im Dialog
angezeigte Pfad. Eine später erfolgreiche Core-Deinstallation entfernt auch diese Protokolle.

Vor der xucRun-Deinstallation muss das optionale Tray-Add-on entfernt werden. Eine
Reparaturinstallation derselben Core-Version ist mit einem vollständig passenden xucRun Tray zulässig;
Versionswechsel oder abweichende Tray-Signierer werden mit einem erklärenden Hinweis abgebrochen.
Wird die Deinstallation bei noch installiertem Tray gestartet, werden keine Core-Bestandteile oder
gespeicherten Daten entfernt. Ein sichtbarer Fehlerdialog fordert dazu auf, zuerst **xucRun Tray**
über **Installierte Apps** zu deinstallieren und danach die xucRun-Deinstallation erneut zu starten.
