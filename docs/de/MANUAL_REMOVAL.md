# Manuelle Entfernung

**Sprache:** Deutsch | [English](../en/MANUAL_REMOVAL.md)

Dieses Dokument listet die von xucRun angelegten Artefakte auf und beschreibt deren manuelle
Entfernung. Verwenden Sie dieses Verfahren nur, wenn die normale Deinstallation nicht verwendet
werden kann oder nicht erfolgreich abgeschlossen wird. Die normale Deinstallation ist sicherer:
Sie prüft die Installation, fragt vor Änderungen nach einer Bestätigung, löscht die vom Broker
verwalteten Geheimnisse im LocalSystem-Kontext und entfernt das verwaltete Signaturzertifikat erst
nach dessen Überprüfung.

Die manuelle Entfernung löscht gespeicherte Anmeldeinformationen, Freigaben, Sicherungen,
Protokolle und die Konfiguration unwiderruflich. Sichern Sie zuvor alles, was erhalten bleiben soll.

## Geltungsbereich

Das Verfahren entfernt die von xucRun installierten Dateien und Einstellungen. Es löscht keine
allgemeinen Windows-Historien wie bereits geschriebene Anwendungs- oder
Dienststeuerungsereignisse, Prefetch, Amcache, Kompatibilitäts- oder Zuletzt-verwendet-Daten,
Wiederherstellungspunkte, externe Sicherungen oder Aufzeichnungen von Sicherheits- und
Verwaltungssoftware. Löschen Sie nicht das gemeinsame Windows-Anwendungsprotokoll, nur um
xucRun-Ereignisse zu entfernen.

## Artefaktinventar

| Typ | Pfad oder Name | Hinweise |
|---|---|---|
| Programmverzeichnis | `%ProgramFiles%\xucRun` | Wird mit dem separat installierten optionalen [xucRun-Tray-Add-on](https://github.com/HT64/xucRun-Tray) gemeinsam genutzt, solange es installiert ist |
| Core-Programme | `xucRun.exe`, `xucRunBroker.exe`, `xucRunApproval.exe` | Im Programmverzeichnis |
| Installationsskript | `Install-xucRun.ps1` | Im Programmverzeichnis |
| Konfiguration | `xucRun.ini` | Im Programmverzeichnis |
| Sprachdateien | `Languages\xucRun.de-DE.dll`, `Languages\xucRun.en-US.dll` | Im Programmverzeichnis |
| Dienst | `xucRunBroker` | Einschließlich Registry-Unterschlüssel `Parameters` |
| Dienst-Registry | `HKLM\SYSTEM\CurrentControlSet\Services\xucRunBroker` | Wird beim Löschen des Dienstes entfernt |
| Ereignisquelle | `HKLM\SYSTEM\CurrentControlSet\Services\EventLog\Application\xucRunBroker` | Vorhandene Ereignisse bleiben im gemeinsamen Anwendungsprotokoll |
| Deinstallationseintrag | `HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\xucRun` | Rechnerweit |
| Rechner-`PATH` | `%ProgramFiles%\xucRun` | Nur vorhanden, wenn bei der Installation ausgewählt |
| Produktdaten | `%ProgramData%\xucRun` | Enthält Metadaten zu Anmeldeinformationen in `BrokerStore\v1`, verschlüsselte Archive und Wiederherstellungsjournale in `BrokerBackups\v3-a8` sowie `InstallerLogs` |
| Broker-Geheimnisse | xucRun-Ziele für Anmeldeinformationen, Freigaben und Authentizitätsmarker | Über die Windows-Anmeldeinformationsverwaltung im LocalSystem-Kontext gespeichert |
| Signaturzertifikat | Normalerweise `CN=xucRun Local Signing (<COMPUTERNAME>)` | Kann in `LocalMachine\My`, `LocalMachine\Root` und `LocalMachine\TrustedPublisher` liegen; `My` enthält den privaten Schlüssel |
| Client-Protokolle | `xucRun_YYYYMMDD.log` | Nur bei konfiguriertem `[Logging] ClientLogDirectory`; das Verzeichnis kann beliebig liegen, Umgebungsvariablen werden je Benutzerkontext aufgelöst |
| Temporäre Wrapper | `xucRun_wrapper_<32 hexadezimale Zeichen>.bat` | Nur für bestimmte Batch-Dateinamen; normalerweise von einem geeigneten Laufwerk namens `RamDisk` oder aus `%TEMP%` des Zielkontos gelöscht |
| Reste fehlgeschlagener Installationen | `%ProgramData%\xucRun-Installer-<32 hexadezimale Zeichen>` | Geschützte Bereitstellungsverzeichnisse, die normalerweise automatisch gelöscht werden |
| Reste fehlgeschlagener Signierung | `%TEMP%\xucRun-Signing-<32 hexadezimale Zeichen>` | Nur beim Signieren eines Pakets angelegt und normalerweise automatisch gelöscht |
| Reste der Installerausgabe | `%TEMP%\xucRun-Installer-<32 hexadezimale Zeichen>.stdout.log` und `.stderr.log` | Werden normalerweise automatisch gelöscht |

Der genannte Zertifikatsname gilt für ein mit dem Standardnamen erzeugtes Zertifikat. Wurde bei der
manuellen Signierung bewusst ein anderer `CertificateName` verwendet, darf dieses Zertifikat nicht
anhand eines unvollständigen Namens identifiziert werden. Notieren und prüfen Sie vor seiner
Löschung den vollständigen Fingerabdruck.

## Verfahren zur manuellen Entfernung

Führen Sie die folgenden Befehle in einem als Administrator gestarteten Windows PowerShell 5.1 aus.

### 1. Zuerst xucRun Tray entfernen

Ist xucRun Tray installiert, entfernen Sie es vor dem Core. Verwenden Sie die
[Anleitung zur manuellen Entfernung](https://github.com/HT64/xucRun-Tray/blob/main/docs/de/MANUAL_REMOVAL.md)
des Tray-Projekts, wenn auch dessen normale Deinstallation nicht verfügbar ist. Wird der Core
zuerst entfernt, können laufende Tray-Prozesse und geplante Aufgaben zurückbleiben, die auf Dateien
im gemeinsamen Programmverzeichnis verweisen.

### 2. Nicht standardmäßige Pfade und Signierer erfassen

Führen Sie diesen Schritt vor dem Löschen von `xucRun.ini` oder `xucRunBroker.exe` aus.

```powershell
$installDirectory = Join-Path $env:ProgramFiles 'xucRun'
$configurationPath = Join-Path $installDirectory 'xucRun.ini'
$brokerPath = Join-Path $installDirectory 'xucRunBroker.exe'

$clientLogDirectory = $null
if (Test-Path -LiteralPath $configurationPath -PathType Leaf) {
    $clientLogDirectory = (Get-Content -LiteralPath $configurationPath |
        Where-Object { $_ -match '^\s*ClientLogDirectory\s*=' } |
        Select-Object -First 1) -replace '^\s*ClientLogDirectory\s*=\s*', ''
}

$signer = $null
if (Test-Path -LiteralPath $brokerPath -PathType Leaf) {
    $signature = Get-AuthenticodeSignature -LiteralPath $brokerPath
    if ($signature.Status -eq 'Valid') {
        $signer = $signature.SignerCertificate
        $signer | Format-List Subject, Issuer, Thumbprint, NotBefore, NotAfter
    }
}
```

Bewahren Sie den angezeigten Antragstellernamen, Aussteller und vollständigen Fingerabdruck auf.
Ist `$signer` leer, raten Sie nicht, welches Zertifikat zu xucRun gehört. Ein Zertifikat darf nicht
entfernt werden, wenn seine Identität unbekannt ist.

Der konfigurierte Client-Protokollpfad kann Umgebungsvariablen enthalten. Lösen Sie ihn für jedes
Windows-Konto, das xucRun verwendet hat, separat auf. Löschen Sie nur Dateien im Format
`xucRun_YYYYMMDD.log`, nicht ein vollständiges gemeinsam genutztes Protokollverzeichnis.

### 3. Vom Broker verwaltete Geheimnisse löschen

Dieser Schritt ist für die vollständige Entfernung der rechnergebundenen
xucRun-Anmeldeinformationen und -Freigaben erforderlich:

```powershell
& (Join-Path $env:ProgramFiles 'xucRun\xucRun.exe') /clear-all
if ($LASTEXITCODE -ne 0) {
    throw "xucRun /clear-all ist mit Exitcode $LASTEXITCODE fehlgeschlagen."
}
```

Bestätigen Sie die Löschung. Der Befehl löscht Anmeldeinformationen, Freigaben,
Authentizitätsmarker und Metadaten zu Anmeldeinformationen für alle Windows-Benutzer und stoppt den
Broker.

Ist der Broker deaktiviert, führen Sie zuerst `xucRun.exe /enable-broker` aus und wiederholen
anschließend `/clear-all`. Fehlen Programmdateien oder Dienst, sind sie beschädigt oder nicht mehr
vertrauenswürdig, ersetzen Sie diesen Schritt nicht durch direkte Eingriffe in die
Anmeldeinformationsverwaltung. Installieren Sie dieselbe vertrauenswürdige xucRun-Version erneut
und verwenden Sie danach `/clear-all` oder die normale Deinstallation. Ohne erfolgreichen
Broker-Löschvorgang können verbliebene LocalSystem-Ziele der Anmeldeinformationsverwaltung nicht
ausgeschlossen werden.

### 4. xucRun-Prozesse beenden und Dienst löschen

```powershell
$installDirectory = Join-Path $env:ProgramFiles 'xucRun'
$processNames = 'xucRun.exe', 'xucRunBroker.exe', 'xucRunApproval.exe'

Get-CimInstance Win32_Process |
    Where-Object {
        $_.Name -in $processNames -and
        $_.ExecutablePath -and
        $_.ExecutablePath.StartsWith(
            $installDirectory + '\',
            [StringComparison]::OrdinalIgnoreCase)
    } |
    ForEach-Object { Stop-Process -Id $_.ProcessId -Force }

Stop-Service -Name 'xucRunBroker' -Force -ErrorAction SilentlyContinue
& "$env:SystemRoot\System32\sc.exe" delete 'xucRunBroker'
```

`sc.exe` kann melden, dass der Dienst zum Löschen markiert ist, solange ein anderer Prozess noch
ein Handle geöffnet hält. Starten Sie Windows in diesem Fall neu, bevor Sie fortfahren. Löschen
Sie den Dienst-Registry-Schlüssel nicht manuell, solange der Dienst noch registriert ist.

### 5. Optionalen Eintrag aus dem Rechner-`PATH` entfernen

Dieser Befehl entfernt nur das exakte xucRun-Verzeichnis und erhält alle anderen Einträge:

```powershell
$xucRunPath = (Join-Path $env:ProgramFiles 'xucRun').TrimEnd('\')
$machinePath = [Environment]::GetEnvironmentVariable('Path', 'Machine')
$remaining = @(
    $machinePath -split ';' |
        Where-Object {
            $_ -and
            $_.Trim().TrimEnd('\') -ine $xucRunPath
        }
)
[Environment]::SetEnvironmentVariable('Path', ($remaining -join ';'), 'Machine')
```

Neue Prozesse sehen den geänderten Rechner-`PATH`; bereits laufende Prozesse behalten ihre
bisherige Umgebung.

### 6. Registry-Einträge entfernen

```powershell
Remove-Item -LiteralPath `
    'Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\xucRun' `
    -Recurse -Force -ErrorAction SilentlyContinue

Remove-Item -LiteralPath `
    'Registry::HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Services\EventLog\Application\xucRunBroker' `
    -Recurse -Force -ErrorAction SilentlyContinue
```

Der Dienstschlüssel `xucRunBroker` einschließlich `Parameters` wird durch `sc.exe delete` entfernt.
Ist er nach einem Neustart noch vorhanden, prüfen Sie zuerst, dass der Dienst nicht mehr in
`Get-Service` erscheint, bevor Sie den verbliebenen Schlüssel entfernen.

### 7. Dateien und Produktdaten entfernen

```powershell
Remove-Item -LiteralPath (Join-Path $env:ProgramFiles 'xucRun') `
    -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath (Join-Path $env:ProgramData 'xucRun') `
    -Recurse -Force -ErrorAction SilentlyContinue
```

Bleibt eines der Verzeichnisse bestehen, prüfen Sie den gemeldeten Zugriffs- oder Freigabefehler,
statt den Besitz an fremden Dateien zu übernehmen. Starten Sie Windows neu, wenn eine überprüfte
xucRun-Datei noch verwendet wird.

Entfernen Sie konfigurierte Client-Protokolldateien an dem in Schritt 2 erfassten Ort. Löschen Sie
nur Namen im exakten Format `xucRun_YYYYMMDD.log`.

### 8. Überprüftes verwaltetes Zertifikat entfernen

Fahren Sie nur mit dem in Schritt 2 exakt erfassten Signierer fort. Beim standardmäßig erzeugten
Zertifikat müssen Antragsteller und Aussteller
`CN=xucRun Local Signing (<COMPUTERNAME>)` entsprechen. Ein mit individuellem Namen erzeugtes
Zertifikat kann auch andere Software signieren und darf nur entfernt werden, wenn diese Auswirkung
verstanden ist.

```powershell
if (-not $signer) {
    throw 'Es wurde kein verifizierter xucRun-Signierer erfasst. Kein Zertifikat nur nach Namen löschen.'
}

$thumbprint = $signer.Thumbprint
foreach ($storeName in 'TrustedPublisher', 'Root') {
    $path = "Cert:\LocalMachine\$storeName\$thumbprint"
    if (Test-Path -LiteralPath $path) {
        Remove-Item -LiteralPath $path -Force
    }
}

$personalPath = "Cert:\LocalMachine\My\$thumbprint"
if (Test-Path -LiteralPath $personalPath) {
    Remove-Item -LiteralPath $personalPath -DeleteKey -Force
}
```

Handelt es sich nicht um das vom Installer erzeugte verwaltete Zertifikat, ist das Beibehalten
sicherer als das Löschen eines Zertifikats, dessen weitere Verwendungen unbekannt sind.

### 9. Bedingte Reste und Spuren abgebrochener Vorgänge prüfen

Prüfen Sie nach dem Ende aller xucRun-Prozesse folgende Orte:

- `%TEMP%` jedes Kontos und geeignete Laufwerke namens `RamDisk` auf einen exakten Namen
  `xucRun_wrapper_<32 hexadezimale Zeichen>.bat`;
- `%ProgramData%` auf Verzeichnisse namens
  `xucRun-Installer-<32 hexadezimale Zeichen>`;
- beim zum Signieren von Paketen verwendeten Konto `%TEMP%` auf
  `xucRun-Signing-<32 hexadezimale Zeichen>`;
- beim Konto, das einen Installer gestartet hat, auf die im Inventar aufgeführten passenden
  `.stdout.log`- und `.stderr.log`-Dateien.

Diese Artefakte werden normalerweise automatisch gelöscht und sollten nur nach Abbruch oder Fehler
vorhanden sein. Prüfen Sie vollständigen Namen und Inhalt, bevor Sie einen einzelnen Rest löschen.
Verwenden Sie keine rekursive Wildcard-Löschung gegen `%ProgramData%`, `%TEMP%`, ein `RamDisk` oder
ein Benutzerprofil.

## Überprüfung

```powershell
Get-Service -Name 'xucRunBroker' -ErrorAction SilentlyContinue
Test-Path -LiteralPath (Join-Path $env:ProgramFiles 'xucRun')
Test-Path -LiteralPath (Join-Path $env:ProgramData 'xucRun')
Test-Path -LiteralPath `
    'Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\xucRun'

$xucRunPath = (Join-Path $env:ProgramFiles 'xucRun').TrimEnd('\')
([Environment]::GetEnvironmentVariable('Path', 'Machine') -split ';') |
    Where-Object { $_.Trim().TrimEnd('\') -ieq $xucRunPath }

if ($signer) {
    foreach ($storeName in 'My', 'Root', 'TrustedPublisher') {
        Get-Item -LiteralPath "Cert:\LocalMachine\$storeName\$($signer.Thumbprint)" `
            -ErrorAction SilentlyContinue
    }
}
```

Bei erfolgreicher Überprüfung erscheinen keine Ausgaben für Dienst, Registry, `PATH` oder
Zertifikat; beide `Test-Path`-Aufrufe liefern `False`. Prüfen Sie außerdem die ausdrücklich
erfassten Client-Protokollorte und alle auf diesem Computer zutreffenden bedingten Reste.
