# Purpose: Provides the bilingual graphical workflow for installing xucRun.
param(
    [Parameter(Mandatory)][string]$PackageDirectory,
    [Parameter(Mandatory)][string]$InstallerPath
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
        PathCheck = 'C:\Program Files\xucRun zum systemweiten PATH hinzufügen'
        CertificateGroup = 'Vertrauen und Codesignierung'
        PublisherCertificate = 'Vorhandene Paketsignaturen verwenden'
        LocalCertificate = 'Lokales Zertifikat erstellen und Paketdateien signieren'
        CertificateWarning = 'Für selbst erstellte Pakete wird das lokale Zertifikat empfohlen. Sein privater Schlüssel ist nicht exportierbar und bleibt auf diesem Computer.'
        UpdateNotice = 'Eine vorhandene xucRun-Installation wird mit den gewählten Einstellungen aktualisiert.'
        InstallButton = 'Installieren'
        CancelButton = 'Abbrechen'
        StatusReady = 'Bereit.'
        StatusCheckingRuntime = 'Microsoft Visual C++ Runtime wird geprüft ...'
        StatusInstallingRuntime = 'Microsoft Visual C++ Runtime wird installiert ...'
        StatusCheckingSignatures = 'Paketsignaturen werden geprüft ...'
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
        InstallerSignatureInvalid = 'Installer.exe besitzt keine gültige prüfbare Signatur.'
        PackageSignatureMismatch = 'Die Paketsignatur stimmt nicht mit Installer.exe überein: {0}'
        ProcessFailed = '{0} (Exitcode {1}).'
        LocalSigningFailed = 'Die lokale Signierung ist fehlgeschlagen'
        InstallationFailed = 'Die Installation ist fehlgeschlagen'
        InstallationSuccess = 'xucRun wurde erfolgreich installiert.'
        Details = @'
INSTALLATION

1. Der Installer benötigt Administratorrechte und entpackt die eingebetteten xucRun-Dateien vorübergehend in ein geschütztes Verzeichnis unter %ProgramData%. Nur SYSTEM und Administratoren erhalten Zugriff. Nach Abschluss oder Abbruch wird das Verzeichnis entfernt.

2. xucRun benötigt Microsoft Visual C++ Redistributable x64. Fehlt es, kann der Installer die Datei vom offiziellen Microsoft-Link herunterladen oder eine vorhandene vc_redist.x64.exe verwenden. Vor dem Start werden Microsoft-Signatur und Originaldateiname geprüft.

3. Für selbst erstellte Pakete kann der Installer ein lokales Codesignaturzertifikat mit nicht exportierbarem privaten Schlüssel erzeugen, ihm auf diesem Computer vertrauen und die sechs installierbaren Dateien signieren.

4. Bei einem vom Herausgeber signierten Paket kann der Installer stattdessen prüfen, dass Installer.exe und alle eingebetteten Dateien vom selben Zertifikat signiert wurden. Dieses Zertifikat wird anschließend als vertrauenswürdig eingerichtet.

5. Das Installationsskript prüft alle Signaturen erneut, installiert Client, Broker, Approval-Helper und Sprachdateien nach C:\Program Files\xucRun und richtet den Broker-Dienst sowie den Deinstallationseintrag ein.

6. Die gewählte Sprache wird in xucRun.ini eingetragen. Der systemweite PATH-Eintrag wird entsprechend der Auswahl gesetzt oder entfernt.

AKTUALISIERUNG

Eine vorhandene xucRun-Installation wird aktualisiert. Bereits gespeicherte xucRun-Anmeldedaten, Freigaben und nicht vom Installer verwaltete INI-Einstellungen bleiben erhalten.

DEINSTALLATION

Die Deinstallation erfolgt über „Installierte Apps“ beziehungsweise „Programme und Features“. Alternativ kann Install-xucRun.ps1 im Installationsverzeichnis mit -Uninstall ausgeführt werden. Gespeicherte xucRun-Daten unter %ProgramData%\xucRun bleiben absichtlich erhalten.
'@
    }
    en = @{
        WindowTitle = 'xucRun Installer'
        Heading = 'Install xucRun'
        Description = 'Installs xucRun and its broker in C:\Program Files\xucRun.'
        SettingsTab = 'Settings'
        DetailsTab = 'Process and uninstallation'
        LanguageLabel = 'Application language'
        PathCheck = 'Add C:\Program Files\xucRun to the system-wide PATH'
        CertificateGroup = 'Trust and code signing'
        PublisherCertificate = 'Use the existing package signatures'
        LocalCertificate = 'Create a local certificate and sign the package files'
        CertificateWarning = 'The local certificate is recommended for self-built packages. Its private key is non-exportable and remains on this computer.'
        UpdateNotice = 'An existing xucRun installation will be updated with the selected settings.'
        InstallButton = 'Install'
        CancelButton = 'Cancel'
        StatusReady = 'Ready.'
        StatusCheckingRuntime = 'Checking Microsoft Visual C++ Runtime ...'
        StatusInstallingRuntime = 'Installing Microsoft Visual C++ Runtime ...'
        StatusCheckingSignatures = 'Checking package signatures ...'
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
        InstallerSignatureInvalid = 'Installer.exe does not have a valid verifiable signature.'
        PackageSignatureMismatch = 'The package signature does not match Installer.exe: {0}'
        ProcessFailed = '{0} (exit code {1}).'
        LocalSigningFailed = 'Local signing failed'
        InstallationFailed = 'Installation failed'
        InstallationSuccess = 'xucRun was installed successfully.'
        Details = @'
INSTALLATION

1. The installer requires administrator rights and temporarily extracts the embedded xucRun files to a protected directory below %ProgramData%. Only SYSTEM and administrators receive access. The directory is removed after completion or cancellation.

2. xucRun requires Microsoft Visual C++ Redistributable x64. If it is missing, the installer can download it from the official Microsoft link or use an existing vc_redist.x64.exe. The Microsoft signature and original file name are checked before execution.

3. For self-built packages, the installer can create a local code-signing certificate with a non-exportable private key, trust it on this computer, and sign the six installable files.

4. For a publisher-signed package, the installer can instead verify that Installer.exe and all embedded files were signed by the same certificate. It then configures that certificate as trusted.

5. The installation script checks all signatures again, installs the client, broker, approval helper, and language files in C:\Program Files\xucRun, and configures the broker service and uninstall entry.

6. The selected language is written to xucRun.ini. The system-wide PATH entry is added or removed according to the selection.

UPDATE

An existing xucRun installation is updated. Stored xucRun credentials, approvals, and INI settings not managed by the installer are preserved.

UNINSTALLATION

Uninstall through “Installed apps” or “Programs and Features”. Alternatively, run Install-xucRun.ps1 in the installation directory with -Uninstall. Stored xucRun data below %ProgramData%\xucRun is intentionally preserved.
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

function Add-CertificateTrust {
    param([Parameter(Mandatory)]
        [Security.Cryptography.X509Certificates.X509Certificate2]$Certificate)
    foreach ($storeName in @('Root', 'TrustedPublisher')) {
        $store = [Security.Cryptography.X509Certificates.X509Store]::new(
            $storeName, [Security.Cryptography.X509Certificates.StoreLocation]::LocalMachine)
        try {
            $store.Open([Security.Cryptography.X509Certificates.OpenFlags]::ReadWrite)
            if (-not @($store.Certificates | Where-Object {
                $_.Thumbprint -eq $Certificate.Thumbprint
            })) {
                $store.Add($Certificate)
            }
        }
        finally {
            $store.Close()
        }
    }
}

function Test-InstallablePackageSignature {
    param([Parameter(Mandatory)]$Signature)

    if (-not $Signature.SignerCertificate) {
        return $false
    }
    if ($Signature.Status -eq [Management.Automation.SignatureStatus]::Valid) {
        return $true
    }
    if ($Signature.Status -ne [Management.Automation.SignatureStatus]::UnknownError -or
        $Signature.SignerCertificate.Subject -ne
        $Signature.SignerCertificate.Issuer) {
        return $false
    }

    $chain = [Security.Cryptography.X509Certificates.X509Chain]::new()
    try {
        $chain.ChainPolicy.ApplicationPolicy.Add(
            [Security.Cryptography.Oid]::new('1.3.6.1.5.5.7.3.3'))
        $chain.ChainPolicy.RevocationMode =
            [Security.Cryptography.X509Certificates.X509RevocationMode]::NoCheck
        $chain.ChainPolicy.VerificationFlags =
            [Security.Cryptography.X509Certificates.X509VerificationFlags]::
                AllowUnknownCertificateAuthority
        return $chain.Build($Signature.SignerCertificate) -and
            $chain.ChainElements.Count -eq 1 -and
            @($chain.ChainStatus | Where-Object {
                $_.Status -ne
                [Security.Cryptography.X509Certificates.X509ChainStatusFlags]::
                    UntrustedRoot
            }).Count -eq 0
    }
    finally {
        $chain.Dispose()
    }
}

function Install-PublisherCertificate {
    $installerSignature = Get-AuthenticodeSignature -LiteralPath $InstallerPath
    if (-not (Test-InstallablePackageSignature -Signature $installerSignature)) {
        throw (Get-Text -Key InstallerSignatureInvalid)
    }
    $expectedThumbprint = $installerSignature.SignerCertificate.Thumbprint
    foreach ($relativePath in @(
        'xucRun.exe',
        'xucRunBroker.exe',
        'xucRunApproval.exe',
        'Install-xucRun.ps1',
        'Languages\xucRun.de-DE.dll',
        'Languages\xucRun.en-US.dll'
    )) {
        $signature = Get-AuthenticodeSignature -LiteralPath (
            Join-Path $PackageDirectory $relativePath)
        if (-not (Test-InstallablePackageSignature -Signature $signature) -or
            $signature.SignerCertificate.Thumbprint -ne $expectedThumbprint) {
            throw (Get-Text -Key PackageSignatureMismatch -Arguments $relativePath)
        }
    }
    Add-CertificateTrust -Certificate $installerSignature.SignerCertificate
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

$form = [Windows.Forms.Form]::new()
$form.ClientSize = [Drawing.Size]::new(620, 600)
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
$tabs.Size = [Drawing.Size]::new(562, 350)
$form.Controls.Add($tabs)
$settingsTab = [Windows.Forms.TabPage]::new()
$detailsTab = [Windows.Forms.TabPage]::new()
$tabs.TabPages.AddRange(@($settingsTab, $detailsTab))

$languageLabel = [Windows.Forms.Label]::new()
$languageLabel.Location = [Drawing.Point]::new(14, 20)
$languageLabel.Size = [Drawing.Size]::new(180, 24)
$settingsTab.Controls.Add($languageLabel)
$languageCombo = [Windows.Forms.ComboBox]::new()
$languageCombo.DropDownStyle = 'DropDownList'
$languageCombo.Location = [Drawing.Point]::new(210, 17)
$languageCombo.Size = [Drawing.Size]::new(326, 24)
[void]$languageCombo.Items.Add('Deutsch')
[void]$languageCombo.Items.Add('English')
$languageCombo.SelectedIndex = if ($script:language -eq 'de') { 0 } else { 1 }
$settingsTab.Controls.Add($languageCombo)

$pathCheck = [Windows.Forms.CheckBox]::new()
$pathCheck.Location = [Drawing.Point]::new(14, 60)
$pathCheck.Size = [Drawing.Size]::new(522, 28)
$pathCheck.Checked = $true
$settingsTab.Controls.Add($pathCheck)

$certificateGroup = [Windows.Forms.GroupBox]::new()
$certificateGroup.Location = [Drawing.Point]::new(14, 102)
$certificateGroup.Size = [Drawing.Size]::new(522, 155)
$settingsTab.Controls.Add($certificateGroup)
$publisherRadio = [Windows.Forms.RadioButton]::new()
$publisherRadio.Location = [Drawing.Point]::new(18, 28)
$publisherRadio.Size = [Drawing.Size]::new(486, 24)
$certificateGroup.Controls.Add($publisherRadio)
$localRadio = [Windows.Forms.RadioButton]::new()
$localRadio.Location = [Drawing.Point]::new(18, 60)
$localRadio.Size = [Drawing.Size]::new(486, 24)
$localRadio.Checked = $true
$certificateGroup.Controls.Add($localRadio)
$certificateWarning = [Windows.Forms.Label]::new()
$certificateWarning.Location = [Drawing.Point]::new(18, 91)
$certificateWarning.Size = [Drawing.Size]::new(486, 54)
$certificateGroup.Controls.Add($certificateWarning)

$updateNotice = [Windows.Forms.Label]::new()
$updateNotice.Location = [Drawing.Point]::new(14, 273)
$updateNotice.Size = [Drawing.Size]::new(522, 42)
$settingsTab.Controls.Add($updateNotice)

$details = [Windows.Forms.TextBox]::new()
$details.Location = [Drawing.Point]::new(10, 12)
$details.Size = [Drawing.Size]::new(534, 298)
$details.Multiline = $true
$details.ReadOnly = $true
$details.ScrollBars = 'Vertical'
$details.BackColor = [Drawing.SystemColors]::Window
$detailsTab.Controls.Add($details)

$statusLabel = [Windows.Forms.Label]::new()
$statusLabel.Location = [Drawing.Point]::new(28, 472)
$statusLabel.Size = [Drawing.Size]::new(562, 18)
$form.Controls.Add($statusLabel)
$progress = [Windows.Forms.ProgressBar]::new()
$progress.Location = [Drawing.Point]::new(28, 494)
$progress.Size = [Drawing.Size]::new(562, 18)
$progress.Style = 'Marquee'
$progress.Visible = $false
$form.Controls.Add($progress)
$installButton = [Windows.Forms.Button]::new()
$installButton.Location = [Drawing.Point]::new(378, 536)
$installButton.Size = [Drawing.Size]::new(102, 34)
$form.AcceptButton = $installButton
$form.Controls.Add($installButton)
$cancelButton = [Windows.Forms.Button]::new()
$cancelButton.Location = [Drawing.Point]::new(488, 536)
$cancelButton.Size = [Drawing.Size]::new(102, 34)
$cancelButton.DialogResult = 'Cancel'
$form.CancelButton = $cancelButton
$form.Controls.Add($cancelButton)

function Update-Language {
    $form.Text = Get-Text -Key WindowTitle
    $heading.Text = Get-Text -Key Heading
    $description.Text = Get-Text -Key Description
    $settingsTab.Text = Get-Text -Key SettingsTab
    $detailsTab.Text = Get-Text -Key DetailsTab
    $languageLabel.Text = Get-Text -Key LanguageLabel
    $pathCheck.Text = Get-Text -Key PathCheck
    $certificateGroup.Text = Get-Text -Key CertificateGroup
    $publisherRadio.Text = Get-Text -Key PublisherCertificate
    $localRadio.Text = Get-Text -Key LocalCertificate
    $certificateWarning.Text = Get-Text -Key CertificateWarning
    $updateNotice.Text = Get-Text -Key UpdateNotice
    $details.Text = (Get-Text -Key Details).Trim()
    $statusLabel.Text = Get-Text -Key $script:statusKey `
        -Arguments $script:statusArguments
    $installButton.Text = Get-Text -Key InstallButton
    $cancelButton.Text = Get-Text -Key CancelButton
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
        Confirm-VcRuntime
        if ($publisherRadio.Checked) {
            Set-Status -Key StatusCheckingSignatures
            Install-PublisherCertificate
        }
        else {
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
            (Get-Text -Key InstallationSuccess),
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
