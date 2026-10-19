<!-- Purpose: Introduces the future public xucRun Core repository. -->
# xucRun

**xucRun** steht für **eXecute Under User Context**.

> **Status:** xucRun Core befindet sich in Vorbereitung. Dieser Entwicklungsstand ist noch keine
> Veröffentlichungsfreigabe. Release-Signierung, Sicherheitsdokumentation und vollständige
> Buildanleitung müssen vor der ersten öffentlichen Veröffentlichung abschließend festgelegt und
> geprüft werden.

## Motivation und Zweck

Das Programm richtet sich an Windows-Anwender, die ihren eigenen Rechner gelegentlich aus einem
Standardbenutzerkonto heraus administrieren möchten, interaktiv oder mithilfe eigener Skripte,
ohne dafür eine separate Anmeldung mit einem Administratorkonto oder bei jedem Vorgang erneut ein
Kennwort zu benötigen. Die erforderliche Berechtigung für administrative Aufgaben soll sich aus
einem Standardbenutzerkonto heraus möglichst mit einem einzigen Mausklick erteilen lassen.

Ein typischer Anwendungsfall entsteht, wenn der Anwender bewusst mit einem Standardbenutzerkonto
angemeldet bleibt, mit einem Programm oder über einen Dateimanager bestimmte Aktionen ausführen
möchte und dabei an Berechtigungsgrenzen stößt, sodass er die betreffende Aktion nur unter einem
anderen Benutzerkonto, beispielsweise einem Administratorkonto, fortsetzen kann. xucRun bindet
dafür die nativen Windows-Mechanismen in denselben Arbeitsablauf ein.

Der Anwender muss die Voraussetzungen dafür nicht vor dem ersten Aufruf vollständig einrichten. Er
kann den gewünschten Programmaufruf unmittelbar mit xucRun starten. Fehlen für diesen Aufruf noch
ein geeignetes Konto, gespeicherte Anmeldedaten oder eine Freigabe, führt xucRun interaktiv durch
die jeweils notwendigen Schritte und fragt die benötigten Angaben und Entscheidungen im laufenden
Arbeitsablauf ab. Bereits vorhandene Anmeldedaten und Freigaben werden bei späteren Aufrufen
wiederverwendet, soweit sie zum jeweiligen Aufruf passen, sodass anschließend in der Regel nur noch
eine einzige Bestätigung per Mausklick erforderlich ist.

Die grundlegenden Aktionen, Programme unter einem anderen Benutzerkonto zu starten und
administrative Rechte anzufordern, lassen sich auch mit Windows-eigenen Mitteln ausführen. xucRun
verbindet diese Möglichkeiten in einem komfortableren Ablauf und ergänzt sie um kontrollierte
Freigaben für gespeicherte Anmeldedaten. Dieser Komfort ist jedoch mit zusätzlichen
Sicherheitsrisiken verbunden, weil Anmeldedaten von xucRun gespeichert und für spätere Aufrufe
wiederverwendet werden. Wer diese Risiken nicht eingehen möchte, sollte besser Windows-eigene
Möglichkeiten wie z. B. den schnellen Benutzerwechsel nutzen.

## Umfang des öffentlichen Core

Der öffentliche Core enthält die eigenständig nutzbaren Grundfunktionen:

- den Client `xucRun.exe`,
- den Brokerdienst `xucRunBroker`,
- den Approval-Helper,
- gemeinsame Protokoll-, Serialisierungs- und Sicherheitskomponenten,
- deutsche und englische Core-Sprachressourcen,
- öffentliche Tests und reproduzierbare Builddateien.

Der öffentliche Core ist eigenständig baubar, installierbar und nutzbar.

## Sicherheit

xucRun verarbeitet sicherheitsrelevante Konto-, Freigabe- und Prozessinformationen. Deshalb
gehören eine verständliche Beschreibung des Sicherheitsmodells, bekannter Grenzen, der Meldewege
für Schwachstellen und reproduzierbarer Prüfungen zum Projekt.

Ungepatchte oder vermutete Schwachstellen dürfen nicht öffentlich gemeldet werden. Der
vertrauliche Meldeweg und die unterstützten Versionen sind in `SECURITY.md` beschrieben.

## Build und Installation

Die `XUCRun.sln` enthält die acht Projekte des Core. Debug und Release werden für x64 gebaut und
getestet. Das folgende Beispiel erzeugt einen Debug-Build:

```powershell
& MSBuild.exe .\XUCRun.sln `
  /t:Build /p:Configuration=Debug /p:Platform=x64 /m
```

Danach erstellt das folgende Skript ein noch unsigniertes Core-Paket:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\New-xucRun-CorePackage.ps1 `
  -Configuration Debug
```

Für einen optimierten unsigned Release-Build und das zugehörige Paket wird stattdessen
`Configuration=Release` beziehungsweise `-Configuration Release` verwendet. Normale Builds
verändern weder Versionsdateien noch andere getrackte Quellen.

Das Paket enthält Client, Broker, Approval-Helper, die beiden Core-Sprachressourcen und den
Core-Installer. Vor der Installation müssen die Paketdateien einheitlich signiert sein. Für lokale
Quell-Builds kann eine als Administrator gestartete Windows PowerShell 5.1 ein nicht
exportierbares, rechnerlokales Zertifikat erstellen, diesem auf dem Rechner vertrauen und das
Paket damit signieren:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\artifacts\CorePackage\Debug\UserCodeSigning\Sign-xucRunCorePackage.ps1
```

Anschließend wird der Core aus derselben erhöhten Windows PowerShell installiert:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\artifacts\CorePackage\Debug\Install-xucRun-Core.ps1 `
  -AddToSystemPath
```

Der Installer prüft alle sechs installierbaren Dateien auf einen gemeinsamen vertrauenswürdigen
Authenticode-Signierer, kopiert sie geschützt nach `C:\Program Files\xucRun`, richtet den
Brokerdienst ein und bindet ihn an genau diesen Client-Signierer.

Die Deinstallation erfolgt über „Installierte Apps“ oder ausdrücklich:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File 'C:\Program Files\xucRun\Install-xucRun-Core.ps1' `
  -Uninstall
```

Die GitHub-CI baut und testet Debug und Release unabhängig und stellt beide unsignierten Pakete als
kurzlebige Buildartefakte bereit. Das Release-Paket wird außerdem auf einem frischen Windows-Runner
mit einem temporären lokalen Testzertifikat signiert. Anschließend prüft die CI automatisiert die
Installation, den installierten Dienst und seine Signiererbindung, den Aufruf von `/info`, eine
erneute Installation als Update sowie die vollständige Deinstallation und Bereinigung.

Diese automatisierte technische Prüfung ersetzt keine Veröffentlichungsfreigabe. Die Signierung mit
dem vorgesehenen Veröffentlichungszertifikat, ein abschließender interaktiver Test auf einer sauberen
Windows-VM und die endgültige Freigabe der Sicherheits- und Release-Dokumentation stehen noch aus.

## Lizenz

Der öffentliche Core steht unter der Apache License 2.0. Maßgeblich sind `LICENSE` und `NOTICE`.

Externe Codebeiträge werden während der ersten Veröffentlichungs- und Stabilisierungsphase nicht
angenommen; Einzelheiten stehen in `CONTRIBUTING.md`.
