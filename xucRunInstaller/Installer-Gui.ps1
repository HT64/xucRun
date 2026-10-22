# Purpose: Provides the bilingual graphical workflow for installing xucRun.
param(
    [Parameter(Mandatory)][string]$PackageDirectory
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
[Windows.Forms.Application]::EnableVisualStyles()

$minimumVcRuntimeVersion = [Version]'14.51.36247.0'
$vcRuntimeDownloadUri = 'https://aka.ms/vs/18/release/vc_redist.x64.exe'
$script:language = if (
    [Globalization.CultureInfo]::CurrentUICulture.TwoLetterISOLanguageName -eq 'de') {
    'de'
} else {
    'en'
}
$script:statusKey = 'StatusReady'
$script:statusArguments = @()

$texts = @{
    de = @{
        WindowTitle = 'xucRun Installer'
        Heading = 'xucRun installieren'
        Description = 'Installiert xucRun und den Broker nach C:\Program Files\xucRun.'
        SettingsTab = 'Einstellungen'
        DetailsTab = 'Ablauf und Deinstallation'
        LanguageLabel = 'Programmsprache'
        LanguageHint = 'Später über xucRun.ini, mit dem optionalen Tray-Add-on auch direkt in dessen Menü änderbar.'
        PathCheck = 'C:\Program Files\xucRun zum systemweiten PATH hinzufügen'
        CertificateGroup = 'Vertrauen und Codesignierung'
        CertificateExisting = "Vorhandenes lokales xucRun-Zertifikat wird wiederverwendet.`r`nGültig seit: {0}   Gültig bis: {1}`r`nFingerabdruck: {2}"
        CertificateNew = 'Bei der Installation wird ein neues lokales xucRun-Zertifikat erstellt.'
        CertificateExpired = 'Das vorhandene lokale xucRun-Zertifikat ist abgelaufen. Bei der Installation wird ein neues Zertifikat erstellt.'
        CertificateAmbiguous = 'Es wurden mehrere verwendbare lokale xucRun-Zertifikate gefunden. Die Installation kann nicht eindeutig fortgesetzt werden.'
        CertificateWarning = 'Der private Schlüssel ist nicht exportierbar und bleibt auf diesem Computer.'
        UpdateNotice = "Installierte Version: {0}`r`nInstalliert/zuletzt aktualisiert: {1}`r`nDie Installation wird mit den gewählten Einstellungen aktualisiert."
        NewInstallNotice = 'Es wurde keine vorhandene xucRun-Installation erkannt. xucRun wird mit den gewählten Einstellungen installiert.'
        UnknownInstallTime = 'nicht verfügbar'
        InstallButton = 'Installieren'
        CancelButton = 'Abbrechen'
        StatusReady = 'Bereit.'
        StatusCheckingRuntime = 'Microsoft Visual C++ Runtime wird geprüft ...'
        StatusInstallingRuntime = 'Microsoft Visual C++ Runtime wird installiert ...'
        StatusSigning = 'Lokales Zertifikat und Paketsignaturen werden eingerichtet ...'
        StatusInstalling = 'xucRun und Broker werden installiert ...'
        StatusSuccess = 'Installation erfolgreich abgeschlossen.'
        StatusFailed = 'Installation fehlgeschlagen.'
        InvalidMicrosoftSignature = 'Die ausgewählte Datei besitzt keine gültige Microsoft-Signatur.'
        InvalidRedistributableFile = 'Die ausgewählte Datei ist nicht vc_redist.x64.exe.'
        RuntimeInstallFailed = 'Die Microsoft Visual C++ Runtime konnte nicht installiert werden (Exitcode {0}).'
        RuntimeMissingAfterInstall = 'Die Microsoft Visual C++ Runtime wurde nach der Installation nicht gefunden.'
        RuntimeRestartMessage = 'Die Runtime wurde installiert. Windows benötigt einen Neustart, bevor xucRun verwendet wird.'
        RuntimeRestartTitle = 'Neustart erforderlich'
        RuntimePrompt = "xucRun benötigt Microsoft Visual C++ Redistributable x64.`r`n`r`nJa: Von Microsoft herunterladen und installieren`r`nNein: Vorhandene vc_redist.x64.exe auswählen`r`nAbbrechen: Installation beenden"
        RuntimeMissingTitle = 'Microsoft Visual C++ Runtime fehlt'
        RuntimeDownloadFailed = "Das Redistributable konnte nicht heruntergeladen werden.`r`n{0}"
        RuntimeDialogTitle = 'Microsoft Visual C++ x64 Redistributable auswählen'
        RuntimeDialogFilter = 'VC_redist.x64.exe|vc_redist.x64.exe|Programme (*.exe)|*.exe'
        RuntimeNotSelected = 'Es wurde kein Microsoft Visual C++ Redistributable ausgewählt.'
        ProcessFailed = '{0} (Exitcode {1}).'
        LocalSigningFailed = 'Die lokale Signierung ist fehlgeschlagen'
        InstallationFailed = 'Die Installation ist fehlgeschlagen'
        InstallationSuccessNew = "xucRun und das lokale Zertifikat wurden erfolgreich installiert.`r`n`r`nZertifikat:`r`nName: {0}`r`nGültig seit: {1}`r`nGültig bis: {2}`r`nFingerabdruck: {3}"
        InstallationSuccessExisting = "xucRun wurde erfolgreich installiert. Das vorhandene lokale Zertifikat wurde wiederverwendet.`r`n`r`nZertifikat:`r`nName: {0}`r`nGültig seit: {1}`r`nGültig bis: {2}`r`nFingerabdruck: {3}"
        Details = @'
INSTALLATION

1. Der Installer benötigt Administratorrechte und entpackt die eingebetteten xucRun-Dateien vorübergehend in ein geschütztes Verzeichnis unter %ProgramData%. Nur SYSTEM und Administratoren erhalten Zugriff. Nach Abschluss oder Abbruch wird das Verzeichnis entfernt.

2. xucRun benötigt Microsoft Visual C++ Redistributable x64. Fehlt es, kann der Installer die Datei vom offiziellen Microsoft-Link herunterladen oder eine vorhandene vc_redist.x64.exe verwenden. Vor dem Start werden Microsoft-Signatur und Originaldateiname geprüft.

3. Der Installer verwendet ein bereits vorhandenes gültiges lokales xucRun-Codesignaturzertifikat mit privatem Schlüssel. Ist keines vorhanden, erzeugt er ein neues Zertifikat mit nicht exportierbarem privaten Schlüssel. Er richtet das Vertrauen auf diesem Computer ein und signiert die sechs installierbaren Dateien.

4. Das Installationsskript prüft alle Signaturen erneut, installiert Client, Broker, Approval-Helper und Sprachdateien nach C:\Program Files\xucRun und richtet den Broker-Dienst sowie den Deinstallationseintrag ein.

5. Die gewählte Sprache wird in xucRun.ini eingetragen. Der systemweite PATH-Eintrag wird entsprechend der Auswahl gesetzt oder entfernt.

AKTUALISIERUNG

Eine vorhandene xucRun-Installation wird aktualisiert. Bereits gespeicherte xucRun-Anmeldedaten, Freigaben und nicht vom Installer verwaltete INI-Einstellungen bleiben erhalten. Das separat installierte optionale xucRun-Tray-Add-on kann bei einer erneuten Installation derselben Version bestehen bleiben, wenn Version und Signierer weiterhin exakt passen. Vor einem Versionswechsel muss das Tray entfernt werden.

War der Brokerdienst vor der Aktualisierung aktiv, wird er nach dem Dateiaustausch automatisch wieder gestartet. Ein zuvor bewusst gestoppter oder deaktivierter Brokerdienst bleibt gestoppt beziehungsweise deaktiviert.

DEINSTALLATION

Die Deinstallation erfolgt über „Installierte Apps“ beziehungsweise „Programme und Features“. Alternativ kann Install-xucRun.ps1 im Installationsverzeichnis mit -Uninstall ausgeführt werden.

Ein installiertes xucRun Tray muss zuvor separat deinstalliert werden. Beim Start über Installierte Apps bleibt das PowerShell-Fenster verborgen; ein manueller Skriptaufruf behält seine Konsolenausgabe.

Vor der ersten Änderung verlangt die Deinstallation eine ausdrückliche Bestätigung. Bei Abbruch bleiben Installation und Daten unverändert.

ACHTUNG: Die Deinstallation löscht unwiderruflich alle gegebenenfalls vorhandenen xucRun-Anmeldedaten und Freigaben aller Windows-Benutzer sowie sämtliche lokalen xucRun-Sicherungen, Sicherheitsmerkmale und zugehörigen Metadaten. Außerdem entfernt sie Core, Brokerdienst, Programmdateien und das lokale xucRun-Standardzertifikat samt privatem Schlüssel und Vertrauenseinträgen.

Die Erfolgsbestätigung führt entfernte Komponenten, Daten und Zertifikatsdetails auf. Ein Fehler bleibt in einem sichtbaren Dialog erhalten. Das technische Diagnoseprotokoll liegt unter %ProgramData%\xucRun\InstallerLogs; der Dialog zeigt den vollständigen Dateipfad in einem kopierbaren Feld und kann den Protokollordner öffnen. Das Protokoll enthält keine gespeicherten Kennwörter.
'@
    }
    en = @{
        WindowTitle = 'xucRun Installer'
        Heading = 'Install xucRun'
        Description = 'Installs xucRun and its broker in C:\Program Files\xucRun.'
        SettingsTab = 'Settings'
        DetailsTab = 'Process and uninstallation'
        LanguageLabel = 'Application language'
        LanguageHint = 'Can be changed later in xucRun.ini or, with the optional Tray add-on, directly in its menu.'
        PathCheck = 'Add C:\Program Files\xucRun to the system-wide PATH'
        CertificateGroup = 'Trust and code signing'
        CertificateExisting = "The existing local xucRun certificate will be reused.`r`nValid from: {0}   Valid until: {1}`r`nThumbprint: {2}"
        CertificateNew = 'A new local xucRun certificate will be created during installation.'
        CertificateExpired = 'The existing local xucRun certificate has expired. A new certificate will be created during installation.'
        CertificateAmbiguous = 'Multiple usable local xucRun certificates were found. Installation cannot continue unambiguously.'
        CertificateWarning = 'The private key is non-exportable and remains on this computer.'
        UpdateNotice = "Installed version: {0}`r`nInstalled/last updated: {1}`r`nThe installation will be updated with the selected settings."
        NewInstallNotice = 'No existing xucRun installation was detected. xucRun will be installed with the selected settings.'
        UnknownInstallTime = 'not available'
        InstallButton = 'Install'
        CancelButton = 'Cancel'
        StatusReady = 'Ready.'
        StatusCheckingRuntime = 'Checking Microsoft Visual C++ Runtime ...'
        StatusInstallingRuntime = 'Installing Microsoft Visual C++ Runtime ...'
        StatusSigning = 'Configuring the local certificate and package signatures ...'
        StatusInstalling = 'Installing xucRun and the broker ...'
        StatusSuccess = 'Installation completed successfully.'
        StatusFailed = 'Installation failed.'
        InvalidMicrosoftSignature = 'The selected file does not have a valid Microsoft signature.'
        InvalidRedistributableFile = 'The selected file is not vc_redist.x64.exe.'
        RuntimeInstallFailed = 'Microsoft Visual C++ Runtime could not be installed (exit code {0}).'
        RuntimeMissingAfterInstall = 'Microsoft Visual C++ Runtime was not found after installation.'
        RuntimeRestartMessage = 'The runtime was installed. Windows must be restarted before xucRun can be used.'
        RuntimeRestartTitle = 'Restart required'
        RuntimePrompt = "xucRun requires Microsoft Visual C++ Redistributable x64.`r`n`r`nYes: Download and install it from Microsoft`r`nNo: Select an existing vc_redist.x64.exe`r`nCancel: Stop installation"
        RuntimeMissingTitle = 'Microsoft Visual C++ Runtime is missing'
        RuntimeDownloadFailed = "The redistributable could not be downloaded.`r`n{0}"
        RuntimeDialogTitle = 'Select Microsoft Visual C++ x64 Redistributable'
        RuntimeDialogFilter = 'VC_redist.x64.exe|vc_redist.x64.exe|Applications (*.exe)|*.exe'
        RuntimeNotSelected = 'No Microsoft Visual C++ Redistributable was selected.'
        ProcessFailed = '{0} (exit code {1}).'
        LocalSigningFailed = 'Local signing failed'
        InstallationFailed = 'Installation failed'
        InstallationSuccessNew = "xucRun and the local certificate were installed successfully.`r`n`r`nCertificate:`r`nName: {0}`r`nValid from: {1}`r`nValid until: {2}`r`nThumbprint: {3}"
        InstallationSuccessExisting = "xucRun was installed successfully. The existing local certificate was reused.`r`n`r`nCertificate:`r`nName: {0}`r`nValid from: {1}`r`nValid until: {2}`r`nThumbprint: {3}"
        Details = @'
INSTALLATION

1. The installer requires administrator rights and temporarily extracts the embedded xucRun files to a protected directory below %ProgramData%. Only SYSTEM and administrators receive access. The directory is removed after completion or cancellation.

2. xucRun requires Microsoft Visual C++ Redistributable x64. If it is missing, the installer can download it from the official Microsoft link or use an existing vc_redist.x64.exe. The Microsoft signature and original file name are checked before execution.

3. The installer reuses an existing valid local xucRun code-signing certificate with its private key. If none exists, it creates a new certificate with a non-exportable private key. It configures trust on this computer and signs the six installable files.

4. The installation script checks all signatures again, installs the client, broker, approval helper, and language files in C:\Program Files\xucRun, and configures the broker service and uninstall entry.

5. The selected language is written to xucRun.ini. The system-wide PATH entry is added or removed according to the selection.

UPDATE

An existing xucRun installation is updated. Stored xucRun credentials, approvals, and INI settings not managed by the installer are preserved. The separately installed optional xucRun Tray add-on can remain during reinstallation of the same version when its version and signer still match exactly. Remove Tray before changing versions.

If the broker service was running before the update, it is restarted automatically after the files have been replaced. A broker service that was deliberately stopped or disabled remains stopped or disabled.

UNINSTALLATION

Uninstall through “Installed apps” or “Programs and Features”. Alternatively, run Install-xucRun.ps1 in the installation directory with -Uninstall.

An installed xucRun Tray must be uninstalled separately first. When started through Installed apps, the PowerShell window remains hidden; a manual script invocation retains its console output.

Uninstall requires explicit confirmation before making its first change. Cancelling leaves the installation and its data unchanged.

WARNING: Uninstall permanently deletes all credentials and approvals that may be stored by xucRun for every Windows user, together with all local xucRun backups, security markers, and associated metadata. It also removes Core, the broker service, program files, and the standard local xucRun certificate, including its private key and trust entries.

The success confirmation lists removed components, data, and certificate details. A failure remains visible in a dialog. Its technical diagnostic log is stored under %ProgramData%\xucRun\InstallerLogs; the dialog shows the full file path in a copyable field and can open the log folder. The log contains no stored passwords.
'@
    }
}

function Get-Text {
    param([Parameter(Mandatory)][string]$Key, [object[]]$Arguments = @())
    $value = $texts[$script:language][$Key]
    if ($Arguments.Count -ne 0) {
        return $value -f $Arguments
    }
    return $value
}

function Test-CodeSigningEku {
    param([Parameter(Mandatory)]
        [Security.Cryptography.X509Certificates.X509Certificate2]$Certificate)

    foreach ($extension in $Certificate.Extensions) {
        if ($extension -is
            [Security.Cryptography.X509Certificates.X509EnhancedKeyUsageExtension]) {
            foreach ($usage in $extension.EnhancedKeyUsages) {
                if ($usage.Value -eq '1.3.6.1.5.5.7.3.3') {
                    return $true
                }
            }
        }
    }
    return $false
}

function Get-LocalCertificateState {
    $subject = "CN=xucRun Local Signing ($env:COMPUTERNAME)"
    $now = Get-Date
    $matching = @(Get-ChildItem Cert:\LocalMachine\My -CodeSigningCert |
        Where-Object {
            $_.Subject -eq $subject -and $_.HasPrivateKey -and
            (Test-CodeSigningEku -Certificate $_)
        })
    $valid = @($matching | Where-Object {
        $_.NotBefore -le $now -and $_.NotAfter -gt $now
    })
    if ($valid.Count -gt 1) {
        return [pscustomobject]@{ Kind = 'Ambiguous'; Certificate = $null }
    }
    if ($valid.Count -eq 1) {
        return [pscustomobject]@{
            Kind = 'Existing'
            Certificate = $valid[0]
        }
    }
    return [pscustomobject]@{
        Kind = if ($matching.Count -gt 0) { 'Expired' } else { 'New' }
        Certificate = $null
    }
}

function Get-CertificateStatusText {
    $state = $script:certificateState
    if ($state.Kind -eq 'Existing') {
        $cultureName = if ($script:language -eq 'de') { 'de-DE' } else { 'en-US' }
        $culture = [Globalization.CultureInfo]::GetCultureInfo($cultureName)
        return Get-Text -Key CertificateExisting -Arguments @(
            $state.Certificate.NotBefore.ToString('d', $culture),
            $state.Certificate.NotAfter.ToString('d', $culture),
            $state.Certificate.Thumbprint)
    }
    return Get-Text -Key ("Certificate$($state.Kind)")
}

function Get-InstallationSuccessText {
    param(
        [Parameter(Mandatory)]
        [Security.Cryptography.X509Certificates.X509Certificate2]$Certificate,
        [Parameter(Mandatory)][bool]$Reused
    )

    $cultureName = if ($script:language -eq 'de') { 'de-DE' } else { 'en-US' }
    $culture = [Globalization.CultureInfo]::GetCultureInfo($cultureName)
    $name = $Certificate.GetNameInfo(
        [Security.Cryptography.X509Certificates.X509NameType]::SimpleName, $false)
    $key = if ($Reused) {
        'InstallationSuccessExisting'
    } else {
        'InstallationSuccessNew'
    }
    return Get-Text -Key $key -Arguments @(
        $name,
        $Certificate.NotBefore.ToString('g', $culture),
        $Certificate.NotAfter.ToString('g', $culture),
        $Certificate.Thumbprint)
}

function Get-InstalledProductState {
    $uninstallPath =
        'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\xucRun'
    if (-not (Test-Path -LiteralPath $uninstallPath)) {
        return $null
    }

    $key = Get-Item -LiteralPath $uninstallPath
    $version = [string]$key.GetValue('DisplayVersion', '')
    if ([string]::IsNullOrWhiteSpace($version)) {
        return $null
    }

    $installedAt = [DateTimeOffset]::MinValue
    $timestamp = [string]$key.GetValue('InstallTimestampUtc', '')
    if (-not [DateTimeOffset]::TryParse(
        $timestamp,
        [Globalization.CultureInfo]::InvariantCulture,
        [Globalization.DateTimeStyles]::RoundtripKind,
        [ref]$installedAt)) {
        $installLocation = [string]$key.GetValue('InstallLocation', '')
        $clientPath = Join-Path $installLocation 'xucRun.exe'
        $installedAt = if (Test-Path -LiteralPath $clientPath -PathType Leaf) {
            [DateTimeOffset](Get-Item -LiteralPath $clientPath).CreationTime
        } else {
            $null
        }
    }

    return [pscustomobject]@{
        Version = $version
        InstalledAt = $installedAt
    }
}

function Get-InstallNoticeText {
    if (-not $script:installedProductState) {
        return Get-Text -Key NewInstallNotice
    }

    $cultureName = if ($script:language -eq 'de') { 'de-DE' } else { 'en-US' }
    $culture = [Globalization.CultureInfo]::GetCultureInfo($cultureName)
    $installedAtText = if ($script:installedProductState.InstalledAt) {
        $script:installedProductState.InstalledAt.ToLocalTime().ToString(
            'g', $culture)
    } else {
        Get-Text -Key UnknownInstallTime
    }
    return Get-Text -Key UpdateNotice -Arguments @(
        $script:installedProductState.Version,
        $installedAtText)
}

function Set-Status {
    param([Parameter(Mandatory)][string]$Key, [object[]]$Arguments = @())
    $script:statusKey = $Key
    $script:statusArguments = $Arguments
    $statusLabel.Text = Get-Text -Key $Key -Arguments $Arguments
    [Windows.Forms.Application]::DoEvents()
}

function Test-VcRuntime {
    $runtimeKeys = @(
        'HKLM:\SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64',
        'HKLM:\SOFTWARE\WOW6432Node\Microsoft\VisualStudio\14.0\VC\Runtimes\x64'
    )
    foreach ($key in $runtimeKeys) {
        $runtime = Get-ItemProperty -LiteralPath $key -ErrorAction SilentlyContinue
        if ($runtime -and $runtime.Installed -eq 1) {
            try {
                if ([Version]"$($runtime.Major).$($runtime.Minor).$($runtime.Bld).$($runtime.Rbld)" -ge
                    $minimumVcRuntimeVersion) {
                    return $true
                }
            }
            catch {
                continue
            }
        }
    }
    return $false
}

function Assert-MicrosoftRedistributable {
    param([Parameter(Mandatory)][string]$Path)
    $resolvedPath = (Resolve-Path -LiteralPath $Path).ProviderPath
    $signature = Get-AuthenticodeSignature -LiteralPath $resolvedPath
    if ($signature.Status -ne [Management.Automation.SignatureStatus]::Valid -or
        -not $signature.SignerCertificate -or
        $signature.SignerCertificate.Subject -notmatch '(^|, )O=Microsoft Corporation(,|$)') {
        throw (Get-Text -Key InvalidMicrosoftSignature)
    }
    if ((Get-Item -LiteralPath $resolvedPath).VersionInfo.OriginalFilename -ine
        'VC_redist.x64.exe') {
        throw (Get-Text -Key InvalidRedistributableFile)
    }
    return $resolvedPath
}

function Confirm-VcRuntime {
    Set-Status -Key StatusCheckingRuntime
    if (Test-VcRuntime) {
        return
    }
    $choice = [Windows.Forms.MessageBox]::Show(
        (Get-Text -Key RuntimePrompt), (Get-Text -Key RuntimeMissingTitle),
        'YesNoCancel', 'Warning')
    if ($choice -eq 'Cancel') {
        throw (Get-Text -Key RuntimeMissingTitle)
    }
    if ($choice -eq 'Yes') {
        $redistributablePath = Join-Path $PackageDirectory 'vc_redist.x64.exe'
        try {
            Invoke-WebRequest -Uri $vcRuntimeDownloadUri -OutFile $redistributablePath `
                -UseBasicParsing
        }
        catch {
            throw (Get-Text -Key RuntimeDownloadFailed -Arguments $_.Exception.Message)
        }
    }
    else {
        $dialog = [Windows.Forms.OpenFileDialog]::new()
        $dialog.Title = Get-Text -Key RuntimeDialogTitle
        $dialog.Filter = Get-Text -Key RuntimeDialogFilter
        $dialog.FileName = 'vc_redist.x64.exe'
        if ($dialog.ShowDialog() -ne 'OK') {
            throw (Get-Text -Key RuntimeNotSelected)
        }
        $redistributablePath = $dialog.FileName
    }
    $redistributablePath = Assert-MicrosoftRedistributable -Path $redistributablePath
    Set-Status -Key StatusInstallingRuntime
    $process = Start-Process -FilePath $redistributablePath `
        -ArgumentList '/install', '/quiet', '/norestart' -Wait -PassThru
    if ($process.ExitCode -notin @(0, 3010) -and -not (Test-VcRuntime)) {
        throw (Get-Text -Key RuntimeInstallFailed -Arguments $process.ExitCode)
    }
    if (-not (Test-VcRuntime)) {
        throw (Get-Text -Key RuntimeMissingAfterInstall)
    }
    if ($process.ExitCode -eq 3010) {
        [Windows.Forms.MessageBox]::Show(
            (Get-Text -Key RuntimeRestartMessage),
            (Get-Text -Key RuntimeRestartTitle), 'OK', 'Warning') | Out-Null
    }
}

function Invoke-HiddenPowerShell {
    param([Parameter(Mandatory)][string[]]$Arguments)
    $powerShellPath =
        Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
    $id = [Guid]::NewGuid().ToString('N')
    $logDirectory = [IO.Path]::GetTempPath()
    $standardOutputPath =
        Join-Path $logDirectory "xucRun-Installer-$id.stdout.log"
    $standardErrorPath =
        Join-Path $logDirectory "xucRun-Installer-$id.stderr.log"
    try {
        $process = Start-Process -FilePath $powerShellPath -ArgumentList $Arguments `
            -WindowStyle Hidden -RedirectStandardOutput $standardOutputPath `
            -RedirectStandardError $standardErrorPath -Wait -PassThru
        $details = @(
            if (Test-Path -LiteralPath $standardErrorPath) {
                Get-Content -LiteralPath $standardErrorPath -Raw
            }
            if (Test-Path -LiteralPath $standardOutputPath) {
                Get-Content -LiteralPath $standardOutputPath -Raw
            }
        ) | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
        return [pscustomobject]@{
            ExitCode = $process.ExitCode
            Details = ($details -join "`r`n").Trim()
        }
    }
    finally {
        Remove-Item -LiteralPath $standardOutputPath, $standardErrorPath `
            -Force -ErrorAction SilentlyContinue
    }
}

function Get-ProcessFailure {
    param([Parameter(Mandatory)][string]$Heading, [Parameter(Mandatory)]$Result)
    $message = Get-Text -Key ProcessFailed -Arguments $Heading, $Result.ExitCode
    if (-not [string]::IsNullOrWhiteSpace($Result.Details)) {
        $details = $Result.Details
        if ($details.Length -gt 5000) {
            $details = $details.Substring(0, 5000) + "`r`n..."
        }
        $message += "`r`n`r`n$details"
    }
    return $message
}

$script:certificateState = Get-LocalCertificateState
$script:installedProductState = Get-InstalledProductState
$form = [Windows.Forms.Form]::new()
$form.ClientSize = [Drawing.Size]::new(620, 545)
$form.FormBorderStyle = 'FixedDialog'
$form.MaximizeBox = $false
$form.MinimizeBox = $false
$form.StartPosition = 'CenterScreen'

$heading = [Windows.Forms.Label]::new()
$heading.Font = [Drawing.Font]::new('Segoe UI', 17, [Drawing.FontStyle]::Bold)
$heading.Location = [Drawing.Point]::new(28, 20)
$heading.Size = [Drawing.Size]::new(562, 38)
$form.Controls.Add($heading)

$description = [Windows.Forms.Label]::new()
$description.Location = [Drawing.Point]::new(30, 62)
$description.Size = [Drawing.Size]::new(560, 30)
$form.Controls.Add($description)

$tabs = [Windows.Forms.TabControl]::new()
$tabs.Location = [Drawing.Point]::new(28, 100)
$tabs.Size = [Drawing.Size]::new(562, 315)
$form.Controls.Add($tabs)
$settingsTab = [Windows.Forms.TabPage]::new()
$detailsTab = [Windows.Forms.TabPage]::new()
$tabs.TabPages.AddRange(@($settingsTab, $detailsTab))

$languageLabel = [Windows.Forms.Label]::new()
$languageLabel.Location = [Drawing.Point]::new(14, 18)
$languageLabel.Size = [Drawing.Size]::new(180, 24)
$settingsTab.Controls.Add($languageLabel)
$languageCombo = [Windows.Forms.ComboBox]::new()
$languageCombo.DropDownStyle = 'DropDownList'
$languageCombo.Location = [Drawing.Point]::new(210, 15)
$languageCombo.Size = [Drawing.Size]::new(326, 24)
[void]$languageCombo.Items.Add('Deutsch')
[void]$languageCombo.Items.Add('English')
$languageCombo.SelectedIndex = if ($script:language -eq 'de') { 0 } else { 1 }
$settingsTab.Controls.Add($languageCombo)

$languageHint = [Windows.Forms.Label]::new()
$languageHint.AutoSize = $true
$languageHint.Location = [Drawing.Point]::new(14, 45)
$languageHint.MaximumSize = [Drawing.Size]::new(522, 0)
$settingsTab.Controls.Add($languageHint)

$pathCheck = [Windows.Forms.CheckBox]::new()
$pathCheck.Location = [Drawing.Point]::new(14, 68)
$pathCheck.Size = [Drawing.Size]::new(522, 28)
$pathCheck.Checked = $true
$settingsTab.Controls.Add($pathCheck)

$certificateGroup = [Windows.Forms.GroupBox]::new()
$certificateGroup.Location = [Drawing.Point]::new(14, 102)
$certificateGroup.Size = [Drawing.Size]::new(522, 116)
$settingsTab.Controls.Add($certificateGroup)
$localCertificate = [Windows.Forms.Label]::new()
$localCertificate.AutoSize = $true
$localCertificate.Location = [Drawing.Point]::new(18, 24)
$localCertificate.MaximumSize = [Drawing.Size]::new(486, 0)
$certificateGroup.Controls.Add($localCertificate)
$certificateWarning = [Windows.Forms.Label]::new()
$certificateWarning.AutoSize = $true
$certificateWarning.Location = [Drawing.Point]::new(18, 76)
$certificateWarning.MaximumSize = [Drawing.Size]::new(486, 0)
$certificateGroup.Controls.Add($certificateWarning)

$updateNotice = [Windows.Forms.Label]::new()
$updateNotice.AutoSize = $true
$updateNotice.Location = [Drawing.Point]::new(14, 228)
$updateNotice.MaximumSize = [Drawing.Size]::new(522, 0)
$settingsTab.Controls.Add($updateNotice)

$details = [Windows.Forms.TextBox]::new()
$details.Location = [Drawing.Point]::new(10, 12)
$details.Size = [Drawing.Size]::new(534, 244)
$details.Multiline = $true
$details.ReadOnly = $true
$details.ScrollBars = 'Vertical'
$details.BackColor = [Drawing.SystemColors]::Window
$detailsTab.Controls.Add($details)

$statusLabel = [Windows.Forms.Label]::new()
$statusLabel.Location = [Drawing.Point]::new(28, 432)
$statusLabel.Size = [Drawing.Size]::new(562, 18)
$form.Controls.Add($statusLabel)
$progress = [Windows.Forms.ProgressBar]::new()
$progress.Location = [Drawing.Point]::new(28, 454)
$progress.Size = [Drawing.Size]::new(562, 18)
$progress.Style = 'Marquee'
$progress.Visible = $false
$form.Controls.Add($progress)
$installButton = [Windows.Forms.Button]::new()
$installButton.Location = [Drawing.Point]::new(378, 495)
$installButton.Size = [Drawing.Size]::new(102, 34)
$installButton.Enabled = $script:certificateState.Kind -ne 'Ambiguous'
$form.AcceptButton = $installButton
$form.Controls.Add($installButton)
$cancelButton = [Windows.Forms.Button]::new()
$cancelButton.Location = [Drawing.Point]::new(488, 495)
$cancelButton.Size = [Drawing.Size]::new(102, 34)
$cancelButton.DialogResult = 'Cancel'
$form.CancelButton = $cancelButton
$form.Controls.Add($cancelButton)

function Update-SettingsLayout {
    $certificateWarning.Top = $localCertificate.Bottom + 10
    $certificateGroup.Height = $certificateWarning.Bottom + 12
    $updateNotice.Top = $certificateGroup.Bottom + 10

    $tabs.Height = $updateNotice.Bottom + 38
    $details.Height = $tabs.Height - 56
    $statusLabel.Top = $tabs.Bottom + 16
    $progress.Top = $statusLabel.Bottom + 4
    $installButton.Top = $progress.Bottom + 24
    $cancelButton.Top = $installButton.Top
    $form.ClientSize = [Drawing.Size]::new(
        620, $installButton.Bottom + 16)
}

function Update-Language {
    $form.Text = Get-Text -Key WindowTitle
    $heading.Text = Get-Text -Key Heading
    $description.Text = Get-Text -Key Description
    $settingsTab.Text = Get-Text -Key SettingsTab
    $detailsTab.Text = Get-Text -Key DetailsTab
    $languageLabel.Text = Get-Text -Key LanguageLabel
    $languageHint.Text = Get-Text -Key LanguageHint
    $pathCheck.Text = Get-Text -Key PathCheck
    $certificateGroup.Text = Get-Text -Key CertificateGroup
    $localCertificate.Text = Get-CertificateStatusText
    $certificateWarning.Text = Get-Text -Key CertificateWarning
    $updateNotice.Text = Get-InstallNoticeText
    $details.Text = (Get-Text -Key Details).Trim()
    $statusLabel.Text = Get-Text -Key $script:statusKey `
        -Arguments $script:statusArguments
    $installButton.Text = Get-Text -Key InstallButton
    $cancelButton.Text = Get-Text -Key CancelButton
    Update-SettingsLayout
}

$languageCombo.Add_SelectedIndexChanged({
    $script:language = if ($languageCombo.SelectedIndex -eq 0) { 'de' } else { 'en' }
    Update-Language
})

$installButton.Add_Click({
    $installButton.Enabled = $false
    $cancelButton.Enabled = $false
    $progress.Visible = $true
    $form.UseWaitCursor = $true
    [Windows.Forms.Application]::DoEvents()
    try {
        $reusedCertificate = $script:certificateState.Kind -eq 'Existing'
        Confirm-VcRuntime
        Set-Status -Key StatusSigning
        $signScript =
            Join-Path $PackageDirectory 'UserCodeSigning\Sign-xucRunPackage.ps1'
        $signResult = Invoke-HiddenPowerShell -Arguments @(
            '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
            ('"' + $signScript + '"'), '-PackageDirectory',
            ('"' + $PackageDirectory + '"'))
        if ($signResult.ExitCode -ne 0) {
            throw (Get-ProcessFailure -Heading (
                Get-Text -Key LocalSigningFailed) -Result $signResult)
        }
        $script:certificateState = Get-LocalCertificateState
        if ($script:certificateState.Kind -ne 'Existing') {
            throw (Get-Text -Key LocalSigningFailed)
        }

        $machineInstaller = Join-Path $PackageDirectory 'Install-xucRun.ps1'
        $arguments = @(
            '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
            ('"' + $machineInstaller + '"'), '-PackageDirectory',
            ('"' + $PackageDirectory + '"'), '-Language', $script:language)
        if ($pathCheck.Checked) {
            $arguments += '-AddToSystemPath'
        }
        Set-Status -Key StatusInstalling
        $installResult = Invoke-HiddenPowerShell -Arguments $arguments
        if ($installResult.ExitCode -ne 0) {
            throw (Get-ProcessFailure -Heading (
                Get-Text -Key InstallationFailed) -Result $installResult)
        }

        Set-Status -Key StatusSuccess
        [Windows.Forms.MessageBox]::Show(
            (Get-InstallationSuccessText `
                -Certificate $script:certificateState.Certificate `
                -Reused $reusedCertificate),
            (Get-Text -Key WindowTitle), 'OK', 'Information') | Out-Null
        $form.DialogResult = 'OK'
        $form.Close()
    }
    catch {
        Set-Status -Key StatusFailed
        [Windows.Forms.MessageBox]::Show(
            $_.Exception.Message, (Get-Text -Key WindowTitle),
            'OK', 'Error') | Out-Null
        $installButton.Enabled = $true
        $cancelButton.Enabled = $true
        $progress.Visible = $false
        $form.UseWaitCursor = $false
    }
})

Update-Language
$result = $form.ShowDialog()
if ($result -eq 'OK') {
    exit 0
}
exit 1223
