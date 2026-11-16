<!--
Purpose: Explains how users manually remove xucRun in German.
Approach: Provides ordered cleanup and verification steps for each installed component.
-->
# Manuelle Entfernung

**Sprache:** Deutsch | [English](../en/MANUAL_REMOVAL.md)

Verwenden Sie dieses Verfahren nur, wenn die [normale Deinstallation](INSTALLATION.md#deinstallieren)
nicht verfügbar ist oder nicht abgeschlossen werden kann. Sie überprüft die Installation und
entfernt die Komponenten in der erforderlichen Reihenfolge; die manuelle Entfernung muss diese
Prüfungen ausdrücklich nachbilden.

Die manuelle Entfernung löscht gespeicherte Anmeldeinformationen, Freigaben, Sicherungen,
Protokolle und die Konfiguration unwiderruflich. Sichern Sie zuvor alles, was erhalten bleiben soll.

Bereits geschriebene Windows-Ereignisse und externe Kopien werden nicht entfernt. Löschen Sie nicht
das gemeinsame Windows-Anwendungsprotokoll, nur um vorhandene xucRun-Ereignisse zu entfernen.

## Artefaktinventar

| Typ | Pfad oder Name | Hinweise |
|---|---|---|
| Programmverzeichnis | `%ProgramFiles%\xucRun` | Wird mit dem separat installierten optionalen [xucRun-Tray-Add-on](https://github.com/HT64/xucRun-Tray/blob/main/docs/de/MANUAL_REMOVAL.md) gemeinsam genutzt, solange es installiert ist |
| Dienst | `xucRunBroker` | Einschließlich Dienstkonfiguration und Autorisierungsrichtlinie |
| Ereignisquelle | `HKLM\SYSTEM\CurrentControlSet\Services\EventLog\Application\xucRunBroker` | Vorhandene Ereignisse bleiben im gemeinsamen Anwendungsprotokoll |
| Deinstallationseintrag | `HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\xucRun` | Rechnerweit |
| Rechner-`PATH` | `%ProgramFiles%\xucRun` | Nur vorhanden, wenn bei der Installation ausgewählt |
| Produktdaten | `%ProgramData%\xucRun` | Enthält Metadaten, verschlüsselte Sicherungen, Wiederherstellungsjournale und Installerprotokolle |
| Broker-Daten | xucRun-Ziele für Anmeldeinformationen, Freigaben und Authentizitätsmarker | Über die Windows-Anmeldeinformationsverwaltung im LocalSystem-Kontext gespeichert |
| Signaturzertifikat | Normalerweise `CN=xucRun Local Signing (<COMPUTERNAME>)` | Kann in `LocalMachine\My`, `LocalMachine\Root` und `LocalMachine\TrustedPublisher` liegen; `My` enthält den privaten Schlüssel |
| Client-Protokolle | `xucRun_YYYYMMDD.log` | Nur bei konfiguriertem `[Logging] ClientLogDirectory`; das Verzeichnis kann beliebig liegen, Umgebungsvariablen werden je Benutzerkontext aufgelöst |

## Verfahren zur manuellen Entfernung

Starten Sie eine 64-Bit-Windows-PowerShell 5.1 als Administrator und führen Sie darin die
folgenden Schritte vollständig in der angegebenen Reihenfolge aus. Ermitteln Sie in diesem Fenster
zuerst die vertrauenswürdigen Windows-Verzeichnisse über die Windows-APIs statt über geerbte
Umgebungsvariablen:

```powershell
if (-not [Environment]::Is64BitProcess) {
    throw 'Die manuelle Entfernung erfordert eine 64-Bit-PowerShell.'
}
$programFilesDirectory = [Environment]::GetFolderPath(
    [Environment+SpecialFolder]::ProgramFiles)
$programDataDirectory = [Environment]::GetFolderPath(
    [Environment+SpecialFolder]::CommonApplicationData)
$systemDirectory = [Environment]::SystemDirectory

if ([string]::IsNullOrWhiteSpace($programFilesDirectory) -or
    [string]::IsNullOrWhiteSpace($programDataDirectory) -or
    [string]::IsNullOrWhiteSpace($systemDirectory)) {
    throw 'Die erforderlichen Windows-Verzeichnisse konnten nicht ermittelt werden.'
}
```

### 1. Zuerst xucRun Tray entfernen

Ist xucRun Tray installiert, entfernen Sie es vor dem Core. Verwenden Sie die
[Anleitung zur manuellen Entfernung](https://github.com/HT64/xucRun-Tray/blob/main/docs/de/MANUAL_REMOVAL.md)
des Tray-Projekts, wenn auch dessen normale Deinstallation nicht verfügbar ist. Wird der Core
zuerst entfernt, können laufende Tray-Prozesse und geplante Aufgaben zurückbleiben, die auf Dateien
im gemeinsamen Programmverzeichnis verweisen.

### 2. Nicht standardmäßige Pfade und verwaltetes Zertifikat erfassen

Führen Sie diesen Schritt vor dem Löschen der Dienstkonfiguration, von `xucRun.ini` oder der
Programmdateien aus. Die Zertifikatsprüfung verwendet dieselbe SHA-256-Bindung wie die normale
Deinstallation.

```powershell
$installDirectory = Join-Path $programFilesDirectory 'xucRun'
$configurationPath = Join-Path $installDirectory 'xucRun.ini'
$brokerPath = Join-Path $installDirectory 'xucRunBroker.exe'
$clientPath = Join-Path $installDirectory 'xucRun.exe'
$parametersPath = 'Registry::HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Services\xucRunBroker\Parameters'
$machineEnvironmentPath =
    'Registry::HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\Session Manager\Environment'

$clientLogDirectory = $null
if (Test-Path -LiteralPath $configurationPath -PathType Leaf) {
    $clientLogDirectory = (Get-Content -LiteralPath $configurationPath |
        Where-Object { $_ -match '^\s*ClientLogDirectory\s*=' } |
        Select-Object -First 1) -replace '^\s*ClientLogDirectory\s*=\s*', ''
}

function Get-CertificateSha256 {
    param(
        [Parameter(Mandatory)]
        [Security.Cryptography.X509Certificates.X509Certificate2]$Certificate
    )

    $sha256 = [Security.Cryptography.SHA256]::Create()
    try {
        return ,$sha256.ComputeHash($Certificate.RawData)
    }
    finally {
        $sha256.Dispose()
    }
}

function Get-VerifiedSigner {
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][byte[]]$ExpectedSha256
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Die erforderliche xucRun-Datei fehlt: $Path"
    }
    $signature = Get-AuthenticodeSignature -LiteralPath $Path
    if ($signature.Status -ne [Management.Automation.SignatureStatus]::Valid -or
        -not $signature.SignerCertificate) {
        throw "Die xucRun-Signatur ist nicht gültig: $Path ($($signature.Status))"
    }
    $actualSha256 = Get-CertificateSha256 -Certificate $signature.SignerCertificate
    if ([Convert]::ToBase64String($actualSha256) -ne
        [Convert]::ToBase64String($ExpectedSha256)) {
        throw "Der Signierer entspricht nicht der Broker-Autorisierungsrichtlinie: $Path"
    }
    return $signature.SignerCertificate
}

if (-not (Test-Path -LiteralPath $parametersPath)) {
    throw 'Die Broker-Autorisierungsrichtlinie fehlt.'
}
$parameters = Get-Item -LiteralPath $parametersPath
$expectedSignerSha256 = $parameters.GetValue('SensitiveClientSignerSha256', $null)
if ($expectedSignerSha256 -isnot [byte[]] -or $expectedSignerSha256.Count -ne 32) {
    throw 'Die Broker-Autorisierungsrichtlinie enthält keinen gültigen Signierer.'
}

$brokerSigner = Get-VerifiedSigner -Path $brokerPath -ExpectedSha256 $expectedSignerSha256
$null = Get-VerifiedSigner -Path $clientPath -ExpectedSha256 $expectedSignerSha256

$expectedSubject = "CN=xucRun Local Signing ($env:COMPUTERNAME)"
$managedSigner = if ($brokerSigner.Subject -eq $expectedSubject -and
    $brokerSigner.Issuer -eq $expectedSubject) {
    $brokerSigner
}
else {
    $null
    Write-Warning (
        'Der verifizierte Signierer ist nicht das standardmäßig verwaltete xucRun-Zertifikat. ' +
        'Das Zertifikat wird nicht automatisch entfernt.')
}

$brokerSigner | Format-List Subject, Issuer, Thumbprint, NotBefore, NotAfter
```

Bricht die Prüfung ab, führen Sie keine vorhandene xucRun-Programmdatei aus und löschen Sie kein
Zertifikat. Installieren Sie dieselbe vertrauenswürdige xucRun-Version erneut und beginnen Sie
danach von vorn. Ein individuell benannter oder anderweitig verwendeter Signierer bleibt erhalten.

Der konfigurierte Client-Protokollpfad kann Umgebungsvariablen enthalten. Lösen Sie ihn für jedes
Windows-Konto, das xucRun verwendet hat, separat auf. Löschen Sie nur Dateien im Format
`xucRun_YYYYMMDD.log`, nicht ein vollständiges gemeinsam genutztes Protokollverzeichnis.

### 3. Vom Broker verwaltete Geheimnisse löschen

Dieser Schritt ist für die vollständige Entfernung der rechnergebundenen
xucRun-Anmeldeinformationen und -Freigaben erforderlich:

```powershell
& (Join-Path $programFilesDirectory 'xucRun\xucRun.exe') /clear-all
if ($LASTEXITCODE -ne 0) {
    throw "xucRun /clear-all ist mit Exitcode $LASTEXITCODE fehlgeschlagen."
}
```

Bestätigen Sie die Löschung. Der Befehl löscht Anmeldeinformationen, Freigaben,
Authentizitätsmarker und Metadaten zu Anmeldeinformationen für alle Windows-Benutzer und stoppt den
Broker.

Ist der Broker deaktiviert, führen Sie zuerst `xucRun.exe /enable-broker` aus und wiederholen
anschließend `/clear-all`. Fehlen Programmdateien oder Dienst, ersetzen Sie diesen Schritt nicht
durch direkte Eingriffe in die Anmeldeinformationsverwaltung. Installieren Sie dieselbe
vertrauenswürdige xucRun-Version erneut und verwenden Sie danach `/clear-all` oder die normale
Deinstallation. Ohne erfolgreichen Broker-Löschvorgang können verbliebene LocalSystem-Ziele der
Anmeldeinformationsverwaltung nicht ausgeschlossen werden.

### 4. xucRun-Prozesse beenden und Dienst löschen

```powershell
$installDirectory = Join-Path $programFilesDirectory 'xucRun'
$processNames = 'xucRun.exe', 'xucRunBroker.exe', 'xucRunApproval.exe'

Get-CimInstance Win32_Process |
    Where-Object {
        $_.Name -in $processNames -and
        $_.ExecutablePath -and
        $_.ExecutablePath.StartsWith(
            $installDirectory + '\',
            [StringComparison]::OrdinalIgnoreCase)
    } |
    ForEach-Object {
        Stop-Process -Id $_.ProcessId -Force -ErrorAction Stop
    }

$service = Get-Service -Name 'xucRunBroker' -ErrorAction SilentlyContinue
if ($service) {
    if ($service.Status -ne [ServiceProcess.ServiceControllerStatus]::Stopped) {
        Stop-Service -Name 'xucRunBroker' -Force -ErrorAction Stop
    }
    & (Join-Path $systemDirectory 'sc.exe') delete 'xucRunBroker'
    if ($LASTEXITCODE -ne 0) {
        throw "Der Dienst konnte nicht gelöscht werden. sc.exe-Exitcode: $LASTEXITCODE"
    }
}
```

`sc.exe` kann melden, dass der Dienst zum Löschen markiert ist, solange ein anderer Prozess noch
ein Handle geöffnet hält. Starten Sie Windows in diesem Fall neu, bevor Sie fortfahren. Löschen
Sie den Dienst-Registry-Schlüssel nicht manuell, solange der Dienst noch registriert ist.

### 5. Optionalen Eintrag aus dem Rechner-`PATH` entfernen

Dieser Code liest und schreibt den nicht expandierten Registry-Wert. Dadurch bleiben Einträge wie
`%SystemRoot%` unverändert:

```powershell
$xucRunPath = (Join-Path $programFilesDirectory 'xucRun').TrimEnd('\')
$environmentKey = Get-Item -LiteralPath $machineEnvironmentPath
$machinePath = [string]$environmentKey.GetValue(
    'Path', '', [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
$pathEntries = @(
    $machinePath -split ';' |
        Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
)
$remaining = @(
    $pathEntries |
        Where-Object {
            $_.Trim().TrimEnd('\') -ine $xucRunPath
        }
)
if ($remaining.Count -ne $pathEntries.Count) {
    Set-ItemProperty -LiteralPath $machineEnvironmentPath -Name Path `
        -Type ExpandString -Value ($remaining -join ';')

    if (-not ('Native.EnvironmentBroadcast' -as [type])) {
        Add-Type -Namespace Native -Name EnvironmentBroadcast -MemberDefinition @'
[DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
public static extern IntPtr SendMessageTimeout(
    IntPtr hWnd, uint Msg, IntPtr wParam, string lParam,
    uint flags, uint timeout, out IntPtr result);
'@
    }
    $broadcastResult = [IntPtr]::Zero
    [void][Native.EnvironmentBroadcast]::SendMessageTimeout(
        [IntPtr]0xffff, 0x001a, [IntPtr]::Zero, 'Environment',
        2, 5000, [ref]$broadcastResult)
}
```

Neue Prozesse übernehmen den geänderten Rechner-`PATH`; bereits laufende Prozesse behalten ihre
bisherige Umgebung. Ist kein exakter xucRun-Eintrag vorhanden, bleibt der Wert unverändert.

### 6. Registry-Einträge entfernen

```powershell
$uninstallPath =
    'Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\xucRun'
$eventSourcePath =
    'Registry::HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Services\EventLog\Application\xucRunBroker'

foreach ($path in $uninstallPath, $eventSourcePath) {
    if (Test-Path -LiteralPath $path) {
        Remove-Item -LiteralPath $path -Recurse -Force -ErrorAction Stop
        if (Test-Path -LiteralPath $path) {
            throw "Der Registry-Schlüssel konnte nicht vollständig entfernt werden: $path"
        }
    }
}
```

Der Dienstschlüssel `xucRunBroker` einschließlich `Parameters` wird durch `sc.exe delete` entfernt.
Ist er nach einem Neustart noch vorhanden, prüfen Sie zuerst, dass der Dienst nicht mehr in
`Get-Service` erscheint, bevor Sie den verbliebenen Schlüssel entfernen.

### 7. Dateien und Produktdaten entfernen

```powershell
function Remove-xucRunDirectory {
    param([Parameter(Mandatory)][string]$Path)

    if (-not (Test-Path -LiteralPath $Path)) {
        return
    }
    $item = Get-Item -LiteralPath $Path -Force -ErrorAction Stop
    if (-not $item.PSIsContainer -or
        ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Das xucRun-Verzeichnis ist kein normales Verzeichnis: $Path"
    }
    Remove-Item -LiteralPath $Path -Recurse -Force -ErrorAction Stop
    if (Test-Path -LiteralPath $Path) {
        throw "Das xucRun-Verzeichnis konnte nicht vollständig entfernt werden: $Path"
    }
}

Remove-xucRunDirectory -Path (Join-Path $programFilesDirectory 'xucRun')
Remove-xucRunDirectory -Path (Join-Path $programDataDirectory 'xucRun')
```

Prüfen Sie einen gemeldeten Zugriffs-, Freigabe- oder Reparse-Point-Fehler, statt den Besitz an
fremden Dateien zu übernehmen oder einem unerwarteten Verweis zu folgen. Starten Sie Windows neu,
wenn eine überprüfte xucRun-Datei noch verwendet wird.

Entfernen Sie konfigurierte Client-Protokolldateien an dem in Schritt 2 erfassten Ort. Löschen Sie
nur Namen im exakten Format `xucRun_YYYYMMDD.log`.

### 8. Überprüftes verwaltetes Zertifikat entfernen

Der folgende Code entfernt nur das in Schritt 2 über die Broker-Autorisierungsrichtlinie
verifizierte Standardzertifikat. Ein individuell benannter oder anderweitig verwendeter Signierer
bleibt erhalten.

```powershell
$managedSignerSha256 = if ($managedSigner) {
    $expectedSignerSha256
}
else {
    $null
}

if ($managedSignerSha256) {
    $expectedHash = [Convert]::ToBase64String($managedSignerSha256)
    foreach ($storeName in 'TrustedPublisher', 'Root', 'My') {
        $matches = @(Get-ChildItem "Cert:\LocalMachine\$storeName" |
            Where-Object {
                [Convert]::ToBase64String(
                    (Get-CertificateSha256 -Certificate $_)) -eq $expectedHash
            })
        foreach ($match in $matches) {
            $path = "Cert:\LocalMachine\$storeName\$($match.Thumbprint)"
            if ($storeName -eq 'My') {
                Remove-Item -LiteralPath $path -DeleteKey -Force -ErrorAction Stop
            }
            else {
                Remove-Item -LiteralPath $path -Force -ErrorAction Stop
            }
        }
    }
}
```

Ein beibehaltenes individuelles Zertifikat kann außerhalb von xucRun weiter benötigt werden. Seine
Entfernung gehört deshalb nicht zu diesem Verfahren.

### 9. Bedingte Reste und Spuren abgebrochener Vorgänge prüfen

Prüfen Sie nach dem Ende aller xucRun-Prozesse folgende Orte:

- `%TEMP%` jedes Kontos und geeignete Laufwerke namens `RamDisk` auf einen exakten Namen
  `xucRun_wrapper_<32 hexadezimale Zeichen>.bat`;
- `%ProgramData%` auf Verzeichnisse namens
  `xucRun-Installer-<32 hexadezimale Zeichen>` und
  `xucRun-Signing-<32 hexadezimale Zeichen>`;
- `%TEMP%` des Kontos, das einen Installer gestartet hat, auf
  `xucRun-Installer-<32 hexadezimale Zeichen>.stdout.log` und die entsprechende
  `.stderr.log`-Datei.

Diese Artefakte werden normalerweise automatisch gelöscht und sollten nur nach Abbruch oder Fehler
vorhanden sein. Prüfen Sie vollständigen Namen und Inhalt, bevor Sie einen einzelnen Rest löschen.
Verwenden Sie keine rekursive Wildcard-Löschung gegen `%ProgramData%`, `%TEMP%`, ein `RamDisk` oder
ein Benutzerprofil.

## Überprüfung

```powershell
$xucRunPath = (Join-Path $programFilesDirectory 'xucRun').TrimEnd('\')
$environmentKey = Get-Item -LiteralPath $machineEnvironmentPath
$machinePath = [string]$environmentKey.GetValue(
    'Path', '', [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
$machinePathEntryPresent = @(
    $machinePath -split ';' |
        Where-Object { $_.Trim().TrimEnd('\') -ieq $xucRunPath }
).Count -ne 0

$managedCertificatePresent = $false
if ($managedSignerSha256) {
    $expectedHash = [Convert]::ToBase64String($managedSignerSha256)
    foreach ($storeName in 'My', 'Root', 'TrustedPublisher') {
        if (@(Get-ChildItem "Cert:\LocalMachine\$storeName" |
            Where-Object {
                [Convert]::ToBase64String(
                    (Get-CertificateSha256 -Certificate $_)) -eq $expectedHash
            }).Count -ne 0) {
            $managedCertificatePresent = $true
        }
    }
}

[pscustomobject]@{
    BrokerServicePresent = $null -ne (
        Get-Service -Name 'xucRunBroker' -ErrorAction SilentlyContinue)
    ProgramDirectoryPresent = Test-Path -LiteralPath (
        Join-Path $programFilesDirectory 'xucRun')
    ProductDataPresent = Test-Path -LiteralPath (
        Join-Path $programDataDirectory 'xucRun')
    UninstallEntryPresent = Test-Path -LiteralPath $uninstallPath
    EventSourcePresent = Test-Path -LiteralPath $eventSourcePath
    MachinePathEntryPresent = $machinePathEntryPresent
    ManagedCertificateWasApplicable = $null -ne $managedSignerSha256
    ManagedCertificatePresent = $managedCertificatePresent
}
```

Bei erfolgreicher Entfernung sind alle Eigenschaften mit dem Suffix `Present` gleich `False`.
`ManagedCertificateWasApplicable` ist nur dann `True`, wenn das standardmäßig verwaltete Zertifikat
entfernt werden sollte. Prüfen Sie außerdem die ausdrücklich erfassten Client-Protokollorte und
alle auf diesem Computer zutreffenden bedingten Reste.
