# Purpose: Installs or removes the public xucRun package.
[CmdletBinding(DefaultParameterSetName = 'Install')]
param(
    [Parameter(ParameterSetName = 'Install')]
    [string]$PackageDirectory,

    [Parameter(ParameterSetName = 'Install')]
    [ValidateSet('auto', 'de', 'en')]
    [string]$Language,

    [Parameter(ParameterSetName = 'Install')]
    [switch]$AddToSystemPath,

    [Parameter(Mandatory, ParameterSetName = 'Uninstall')]
    [switch]$Uninstall,

    [Parameter(ParameterSetName = 'Uninstall')]
    [switch]$NoSuccessDialog
)

$ErrorActionPreference = 'Stop'
$serviceName = 'xucRunBroker'
$serviceDisplayName = 'xucRun Privileged Broker'
$programFilesDirectory = [Environment]::GetFolderPath(
    [Environment+SpecialFolder]::ProgramFiles)
if ([string]::IsNullOrWhiteSpace($programFilesDirectory)) {
    throw 'The Windows Program Files directory could not be resolved.'
}
$installDirectory = Join-Path $programFilesDirectory 'xucRun'
$languageDirectory = Join-Path $installDirectory 'Languages'
$installedBroker = Join-Path $installDirectory 'xucRunBroker.exe'
$installedClient = Join-Path $installDirectory 'xucRun.exe'
$installedApproval = Join-Path $installDirectory 'xucRunApproval.exe'
$installedInstaller = Join-Path $installDirectory 'Install-xucRun.ps1'
$configurationPath = Join-Path $installDirectory 'xucRun.ini'
$parametersPath = "HKLM:\SYSTEM\CurrentControlSet\Services\$serviceName\Parameters"
$eventSourcePath = "HKLM:\SYSTEM\CurrentControlSet\Services\EventLog\Application\$serviceName"
$uninstallRegistryPath =
    'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\xucRun'
$brokerStoreDirectory = Join-Path $env:ProgramData 'xucRun'
$installerLogDirectory = Join-Path $brokerStoreDirectory 'InstallerLogs'
$machineEnvironmentPath =
    'HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager\Environment'
$serviceSecurityDescriptor =
    'D:(A;;CCLCSWRPWPDTLOCRRC;;;SY)(A;;CCDCLCSWRPWPDTLOCRSDRCWDWO;;;BA)(A;;CCLCRPRC;;;AU)'
$restrictedServiceSecurityDescriptor =
    'D:(A;;CCLCSWRPWPDTLOCRRC;;;SY)(A;;CCDCLCSWRPWPDTLOCRSDRCWDWO;;;BA)'

function Assert-WindowsPowerShell {
    if ($PSVersionTable.PSEdition -ne 'Desktop' -or
        $PSVersionTable.PSVersion.Major -ne 5) {
        throw 'Run this installer with Windows PowerShell 5.1 (powershell.exe).'
    }
}

function Assert-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    if (-not $principal.IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'Run Windows PowerShell as administrator before starting this installer.'
    }
}

function Send-EnvironmentChanged {
    if (-not ('Native.EnvironmentBroadcast' -as [type])) {
        Add-Type -Namespace Native -Name EnvironmentBroadcast -MemberDefinition @'
[DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
public static extern IntPtr SendMessageTimeout(
    IntPtr hWnd, uint Msg, IntPtr wParam, string lParam,
    uint flags, uint timeout, out IntPtr result);
'@
    }
    $result = [IntPtr]::Zero
    [void][Native.EnvironmentBroadcast]::SendMessageTimeout(
        [IntPtr]0xffff, 0x001a, [IntPtr]::Zero, 'Environment', 2, 5000, [ref]$result)
}

function Assert-NotReparsePoint {
    param([Parameter(Mandatory)][string]$Path)

    $current = [IO.Path]::GetFullPath($Path)
    while ($current) {
        if (Test-Path -LiteralPath $current) {
            $item = Get-Item -LiteralPath $current -Force
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "Installer paths must not contain reparse points: $current"
            }
        }
        $parent = [IO.Path]::GetDirectoryName($current)
        if ([string]::IsNullOrEmpty($parent) -or $parent -eq $current) {
            break
        }
        $current = $parent
    }
}

function Get-CertificateSha256 {
    param([Parameter(Mandatory)]
        [Security.Cryptography.X509Certificates.X509Certificate2]$Certificate)

    $sha256 = [Security.Cryptography.SHA256]::Create()
    try {
        return $sha256.ComputeHash($Certificate.RawData)
    }
    finally {
        $sha256.Dispose()
    }
}

function Get-VerifiedSignerSha256 {
    param([Parameter(Mandatory)][string]$Path)

    $signature = Get-AuthenticodeSignature -LiteralPath $Path
    if ($signature.Status -ne [Management.Automation.SignatureStatus]::Valid -or
        -not $signature.SignerCertificate) {
        throw "A valid trusted Authenticode signature is required: $Path " +
            "($($signature.Status))"
    }
    return ,(Get-CertificateSha256 -Certificate $signature.SignerCertificate)
}

function Remove-InstalledSigningCertificate {
    param(
        [Parameter(Mandatory)]
        [Security.Cryptography.X509Certificates.X509Certificate2]$Certificate,
        [Parameter(Mandatory)][byte[]]$ExpectedSha256
    )

    $expectedSubject = "CN=xucRun Local Signing ($env:COMPUTERNAME)"
    if ($Certificate.Subject -ne $expectedSubject -or
        $Certificate.Issuer -ne $expectedSubject -or
        [Convert]::ToBase64String(
            (Get-CertificateSha256 -Certificate $Certificate)) -ne
        [Convert]::ToBase64String($ExpectedSha256)) {
        throw 'The installed signer is not the managed local xucRun certificate.'
    }

    $expectedHash = [Convert]::ToBase64String($ExpectedSha256)
    $removedCount = 0
    foreach ($storeName in @('My', 'Root', 'TrustedPublisher')) {
        $matches = @(Get-ChildItem "Cert:\LocalMachine\$storeName" |
            Where-Object {
                [Convert]::ToBase64String(
                    (Get-CertificateSha256 -Certificate $_)) -eq $expectedHash
            })
        foreach ($match in $matches) {
            $certificatePath =
                "Cert:\LocalMachine\$storeName\$($match.Thumbprint)"
            if ($storeName -eq 'My') {
                Remove-Item -LiteralPath $certificatePath -DeleteKey -Force
            }
            else {
                Remove-Item -LiteralPath $certificatePath -Force
            }
            $removedCount++
        }
    }

    foreach ($storeName in @('My', 'Root', 'TrustedPublisher')) {
        $remaining = @(Get-ChildItem "Cert:\LocalMachine\$storeName" |
            Where-Object {
                [Convert]::ToBase64String(
                    (Get-CertificateSha256 -Certificate $_)) -eq $expectedHash
            })
        if ($remaining.Count -ne 0) {
            throw "The xucRun certificate remains in LocalMachine\$storeName."
        }
    }
    return $removedCount
}

function Confirm-Uninstall {
    $german =
        [Globalization.CultureInfo]::CurrentUICulture.TwoLetterISOLanguageName -eq 'de'
    if ($german) {
        $privateKey = 'privatem Schl' + [char]0x00FC + 'ssel'
        $trustEntries = 'Vertrauenseintr' + [char]0x00E4 + 'gen'
        $message = @"
Bei der Deinstallation werden unwiderruflich entfernt:

- xucRun Core, Brokerdienst und Programmdateien
- alle gegebenenfalls vorhandenen gespeicherten Anmeldedaten und Freigaben
- lokale Sicherungen, Sicherheitsmerkmale und Metadaten
- das lokale xucRun-Standardzertifikat samt $privateKey und $trustEntries

xucRun jetzt deinstallieren?
"@
        $title = 'xucRun deinstallieren'
    }
    else {
        $message = @'
Uninstalling permanently removes:

- xucRun Core, the broker service, and program files
- all stored credentials and approvals, if present
- local backups, security markers, and metadata
- the standard local xucRun certificate, including its private key and trust entries

Uninstall xucRun now?
'@
        $title = 'Uninstall xucRun'
    }
    Add-Type -AssemblyName System.Windows.Forms
    return [Windows.Forms.MessageBox]::Show(
        $message.Trim(), $title, 'YesNo', 'Warning', 'Button2') -eq 'Yes'
}

function Show-UninstallSuccess {
    param(
        [Parameter(Mandatory)]
        [Security.Cryptography.X509Certificates.X509Certificate2]$Certificate,
        [Parameter(Mandatory)][int]$RemovedCount
    )

    $german =
        [Globalization.CultureInfo]::CurrentUICulture.TwoLetterISOLanguageName -eq 'de'
    $culture = [Globalization.CultureInfo]::GetCultureInfo(
        $(if ($german) { 'de-DE' } else { 'en-US' }))
    $name = $Certificate.GetNameInfo(
        [Security.Cryptography.X509Certificates.X509NameType]::SimpleName, $false)
    if ($german) {
        $valid = 'G' + [char]0x00FC + 'ltig'
        $heading = if ($RemovedCount -gt 0) {
            'Das lokale xucRun-Zertifikat wurde aus den Zertifikatspeichern entfernt.'
        } else {
            'Das lokale xucRun-Zertifikat war bereits aus den Zertifikatspeichern entfernt.'
        }
        $message = "xucRun wurde erfolgreich deinstalliert.`r`n`r`n" +
            "Entfernt wurden:`r`n" +
            "- xucRun Core, Brokerdienst und Programmdateien`r`n" +
            "- alle gegebenenfalls vorhandenen gespeicherten Anmeldedaten und Freigaben`r`n" +
            "- lokale Sicherungen, Sicherheitsmerkmale und Metadaten`r`n" +
            "- das lokale xucRun-Standardzertifikat`r`n`r`n$heading" +
            "`r`n`r`nName: $name" +
            "`r`n$valid seit: $($Certificate.NotBefore.ToString('g', $culture))" +
            "`r`n$valid bis: $($Certificate.NotAfter.ToString('g', $culture))" +
            "`r`nFingerabdruck: $($Certificate.Thumbprint)"
        $title = 'xucRun deinstallieren'
    }
    else {
        $heading = if ($RemovedCount -gt 0) {
            'The local xucRun certificate was removed from the certificate stores.'
        } else {
            'The local xucRun certificate had already been removed from the certificate stores.'
        }
        $message = "xucRun was uninstalled successfully.`r`n`r`n" +
            "Removed:`r`n" +
            "- xucRun Core, the broker service, and program files`r`n" +
            "- all stored credentials and approvals, if present`r`n" +
            "- local backups, security markers, and metadata`r`n" +
            "- the standard local xucRun certificate`r`n`r`n$heading" +
            "`r`n`r`nName: $name" +
            "`r`nValid from: $($Certificate.NotBefore.ToString('g', $culture))" +
            "`r`nValid until: $($Certificate.NotAfter.ToString('g', $culture))" +
            "`r`nThumbprint: $($Certificate.Thumbprint)"
        $title = 'Uninstall xucRun'
    }
    Add-Type -AssemblyName System.Windows.Forms
    [Windows.Forms.MessageBox]::Show(
        $message, $title, 'OK', 'Information') | Out-Null
}

function Write-UninstallDiagnostic {
    param([Parameter(Mandatory)][Management.Automation.ErrorRecord]$ErrorRecord)

    New-Item -ItemType Directory -Path $installerLogDirectory -Force | Out-Null
    Set-InstallDirectoryAcl -Path $installerLogDirectory
    $logPath = Join-Path $installerLogDirectory (
        'xucRun-Uninstall-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '.log')
    $lines = @(
        "TimestampUtc=$([DateTimeOffset]::UtcNow.ToString('o'))",
        'Component=xucRun Core uninstall',
        "Computer=$env:COMPUTERNAME",
        "PowerShell=$($PSVersionTable.PSVersion)",
        '',
        'ExceptionType:',
        $ErrorRecord.Exception.GetType().FullName,
        '',
        'Message:',
        $ErrorRecord.Exception.Message,
        '',
        'Category:',
        [string]$ErrorRecord.CategoryInfo,
        '',
        'FullyQualifiedErrorId:',
        $ErrorRecord.FullyQualifiedErrorId,
        '',
        'Position:',
        $ErrorRecord.InvocationInfo.PositionMessage,
        '',
        'ScriptStackTrace:',
        $ErrorRecord.ScriptStackTrace
    )
    Set-Content -LiteralPath $logPath -Value $lines -Encoding UTF8
    return $logPath
}

function Show-UninstallFailure {
    param(
        [Parameter(Mandatory)][string]$Details,
        [string]$LogPath,
        [string]$LogError
    )

    Add-Type -AssemblyName System.Windows.Forms
    Add-Type -AssemblyName System.Drawing
    $german =
        [Globalization.CultureInfo]::CurrentUICulture.TwoLetterISOLanguageName -eq 'de'
    if ($german) {
        $title = 'xucRun deinstallieren'
        $headingText = 'Die Deinstallation ist fehlgeschlagen.'
        $detailsText = 'Fehlerdetails:'
        $logText = 'Diagnoseprotokoll:'
        $openText = 'Protokollordner ' + [char]0x00F6 + 'ffnen'
        $closeText = 'Schlie' + [char]0x00DF + 'en'
        $logUnavailable = 'Das Diagnoseprotokoll konnte nicht geschrieben werden.'
        $openFailed = 'Der Protokollordner konnte nicht ge' +
            [char]0x00F6 + 'ffnet werden.'
    }
    else {
        $title = 'Uninstall xucRun'
        $headingText = 'Uninstall failed.'
        $detailsText = 'Error details:'
        $logText = 'Diagnostic log:'
        $openText = 'Open log folder'
        $closeText = 'Close'
        $logUnavailable = 'The diagnostic log could not be written.'
        $openFailed = 'The log folder could not be opened.'
    }

    $form = [Windows.Forms.Form]::new()
    $form.Text = $title
    $form.ClientSize = [Drawing.Size]::new(680, 330)
    $form.FormBorderStyle = 'FixedDialog'
    $form.MaximizeBox = $false
    $form.MinimizeBox = $false
    $form.StartPosition = 'CenterScreen'

    $heading = [Windows.Forms.Label]::new()
    $heading.AutoSize = $true
    $heading.Font = [Drawing.Font]::new(
        $form.Font, [Drawing.FontStyle]::Bold)
    $heading.Location = [Drawing.Point]::new(20, 18)
    $heading.Text = $headingText
    $form.Controls.Add($heading)

    $detailsLabel = [Windows.Forms.Label]::new()
    $detailsLabel.SetBounds(20, 52, 640, 20)
    $detailsLabel.Text = $detailsText
    $form.Controls.Add($detailsLabel)
    $detailsBox = [Windows.Forms.TextBox]::new()
    $detailsBox.SetBounds(20, 74, 640, 86)
    $detailsBox.Multiline = $true
    $detailsBox.ReadOnly = $true
    $detailsBox.ScrollBars = 'Vertical'
    $detailsBox.Text = $Details
    $form.Controls.Add($detailsBox)

    $logLabel = [Windows.Forms.Label]::new()
    $logLabel.SetBounds(20, 176, 640, 20)
    $logLabel.Text = $logText
    $form.Controls.Add($logLabel)
    $logBox = [Windows.Forms.TextBox]::new()
    $logBox.SetBounds(20, 198, 640, 24)
    $logBox.ReadOnly = $true
    $logBox.Text = if ($LogPath) {
        $LogPath
    } else {
        "$logUnavailable $LogError".Trim()
    }
    $form.Controls.Add($logBox)

    $openButton = [Windows.Forms.Button]::new()
    $openButton.SetBounds(378, 274, 180, 34)
    $openButton.Text = $openText
    $openButton.Enabled = -not [string]::IsNullOrWhiteSpace($LogPath)
    $openButton.Add_Click({
        try {
            [void][Diagnostics.Process]::Start(
                'explorer.exe',
                ('"{0}"' -f [IO.Path]::GetDirectoryName($LogPath)))
        }
        catch {
            [Windows.Forms.MessageBox]::Show(
                "$openFailed`r`n`r`n$($_.Exception.Message)",
                $title, 'OK', 'Error') | Out-Null
        }
    })
    $form.Controls.Add($openButton)

    $closeButton = [Windows.Forms.Button]::new()
    $closeButton.SetBounds(566, 274, 94, 34)
    $closeButton.Text = $closeText
    $closeButton.DialogResult = 'OK'
    $form.AcceptButton = $closeButton
    $form.CancelButton = $closeButton
    $form.Controls.Add($closeButton)

    [void]$form.ShowDialog()
}

function Get-PackageFiles {
    param([Parameter(Mandatory)][string]$Root)

    $relativePaths = @(
        'xucRun.exe',
        'xucRunBroker.exe',
        'xucRunApproval.exe',
        'Install-xucRun.ps1',
        'Languages\xucRun.de-DE.dll',
        'Languages\xucRun.en-US.dll'
    )
    foreach ($relativePath in $relativePaths) {
        $path = Join-Path $Root $relativePath
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Package file is missing: $path"
        }
        [pscustomobject]@{
            RelativePath = $relativePath
            Path = (Resolve-Path -LiteralPath $path).ProviderPath
        }
    }
}

function Assert-ConsistentPackageSignatures {
    param([Parameter(Mandatory)][object[]]$Files)

    $expected = $null
    foreach ($file in $Files) {
        $signer = Get-VerifiedSignerSha256 -Path $file.Path
        $encodedSigner = [Convert]::ToBase64String($signer)
        if (-not $expected) {
            $expected = $encodedSigner
        }
        elseif ($encodedSigner -ne $expected) {
            throw 'All package files must have the same trusted signer.'
        }
    }
    return ,([Convert]::FromBase64String($expected))
}

function Invoke-ServiceControl {
    param([Parameter(Mandatory)][string[]]$Arguments)

    $output = & "$env:SystemRoot\System32\sc.exe" @Arguments 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "sc.exe failed with exit code $LASTEXITCODE`: $($output -join ' ')"
    }
}

function Wait-ServiceState {
    param(
        [Parameter(Mandatory)][string]$Name,
        [Parameter(Mandatory)][string]$Status
    )

    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    do {
        $service = Get-Service -Name $Name -ErrorAction SilentlyContinue
        if ($service -and $service.Status.ToString() -eq $Status) {
            return
        }
        Start-Sleep -Milliseconds 200
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "Service '$Name' did not reach state '$Status'."
}

function Set-InstallDirectoryAcl {
    param([Parameter(Mandatory)][string]$Path)

    $inheritance = [Security.AccessControl.InheritanceFlags]'ContainerInherit, ObjectInherit'
    $propagation = [Security.AccessControl.PropagationFlags]::None
    $allow = [Security.AccessControl.AccessControlType]::Allow
    $systemSid = [Security.Principal.SecurityIdentifier]::new('S-1-5-18')
    $administratorsSid =
        [Security.Principal.SecurityIdentifier]::new('S-1-5-32-544')
    $usersSid = [Security.Principal.SecurityIdentifier]::new('S-1-5-32-545')
    $acl = [Security.AccessControl.DirectorySecurity]::new()
    $acl.SetAccessRuleProtection($true, $false)
    $acl.SetOwner($administratorsSid)
    $acl.AddAccessRule([Security.AccessControl.FileSystemAccessRule]::new(
        $systemSid, 'FullControl', $inheritance, $propagation, $allow))
    $acl.AddAccessRule([Security.AccessControl.FileSystemAccessRule]::new(
        $administratorsSid, 'FullControl', $inheritance, $propagation, $allow))
    $acl.AddAccessRule([Security.AccessControl.FileSystemAccessRule]::new(
        $usersSid, 'ReadAndExecute', $inheritance, $propagation, $allow))
    Set-Acl -LiteralPath $Path -AclObject $acl
}

function Set-ConfigurationLanguage {
    param([Parameter(Mandatory)][string]$SelectedLanguage)

    $createConfiguration = -not (
        Test-Path -LiteralPath $configurationPath -PathType Leaf)
    if (-not $createConfiguration) {
        $existingLines = @([IO.File]::ReadAllLines($configurationPath))
        $meaningfulLines = @($existingLines | Where-Object {
            -not [string]::IsNullOrWhiteSpace($_) -and
            -not $_.TrimStart().StartsWith(';')
        })
        $createConfiguration = $meaningfulLines.Count -eq 1 -and
            $meaningfulLines[0] -match '^\s*Language\s*='
    }

    if ($createConfiguration) {
        $configurationLines = if (
            $SelectedLanguage -eq 'de' -or
            ($SelectedLanguage -eq 'auto' -and
                [Globalization.CultureInfo]::CurrentUICulture.TwoLetterISOLanguageName -eq 'de')) {
            @(
                '; Diese Datei konfiguriert xucRun fuer alle Benutzer dieses Computers.'
                '; Fehlt diese Datei, startet xucRun nicht.'
                '; Laufende Prozesse lesen Aenderungen nicht automatisch neu ein.'
                '; Language und ClientLogDirectory gelten fuer jeden neu gestarteten Client.'
                '; BrokerInformationEvents wird beim Start des Broker-Dienstes gelesen.'
                ''
                '[xucRun]'
                '; Legt die Sprache der Clientoberflaeche fest.'
                '; Zulaessige Werte: Language=auto, Language=de oder Language=en'
                '; Language=auto folgt der Windows-Anzeigesprache des jeweiligen Benutzers.'
                '; Fehlt der Wert oder ist er leer, wird Englisch verwendet.'
                '; Bei einem anderen Wert startet der Client nicht.'
                "Language=$SelectedLanguage"
                ''
                '[Logging]'
                '; Bestimmt das Verzeichnis des Client-Dateilogs.'
                '; Ein fehlender oder leerer Wert deaktiviert das Client-Dateilog.'
                '; Umgebungsvariablen werden im Kontext jedes startenden Clients expandiert.'
                '; Der Pfad muss fuer jedes Client- und Wrapperkonto erreichbar sein.'
                '; Dort sind Aenderungsrechte zum Erstellen, Anhaengen und Loeschen erforderlich.'
                '; xucRun veraendert die Zugriffsrechte des Verzeichnisses nicht.'
                '; Bei gemeinsamen Pfaden legt der Administrator Leser und Schreiber fest.'
                '; Bei Netzwerkpfaden muessen Freigabe- und Dateisystemrechte dies erlauben.'
                '; Gemappte Laufwerke muessen in jedem Kontokontext verfuegbar sein.'
                '; Fuer gemeinsam genutzte Netzwerkpfade sind UNC-Pfade eindeutiger.'
                '; Zum Aktivieren einen Wert ohne Kommentarzeichen eintragen, zum Beispiel:'
                '; ClientLogDirectory=%LOCALAPPDATA%\xucRun\Logs'
                '; ClientLogDirectory='
                '; Steuert zusaetzliche Informationsereignisse des Broker-Dienstes.'
                '; Nur true aktiviert sie; Gross- und Kleinschreibung werden ignoriert.'
                '; Ein fehlender, leerer oder anderer Wert deaktiviert sie.'
                '; Warnungen, Fehler und grundlegende Dienstereignisse bleiben aktiv.'
                '; BrokerInformationEvents=true'
                '; BrokerInformationEvents=false'
            )
        }
        else {
            @(
                '; This file configures xucRun for all users of this computer.'
                '; xucRun does not start if this file is missing.'
                '; Running processes do not reload changes automatically.'
                '; Language and ClientLogDirectory apply to each newly started client.'
                '; BrokerInformationEvents is read when the broker service starts.'
                ''
                '[xucRun]'
                '; Sets the language of the client user interface.'
                '; Valid values: Language=auto, Language=de, or Language=en'
                '; Language=auto follows each user''s Windows display language.'
                '; English is used if the value is missing or empty.'
                '; The client does not start for any other value.'
                "Language=$SelectedLanguage"
                ''
                '[Logging]'
                '; Sets the directory for client log files.'
                '; A missing or empty value disables client file logging.'
                '; Environment variables are expanded in each starting client context.'
                '; The path must be accessible to every client and wrapper account.'
                '; Modify access is required to create, append to, and delete log files.'
                '; xucRun does not change the directory access permissions.'
                '; For shared paths, the administrator defines the readers and writers.'
                '; Network paths require matching share and file system permissions.'
                '; Mapped drives must be available in every account context.'
                '; UNC paths are less ambiguous for shared network locations.'
                '; To enable it, enter an uncommented value, for example:'
                '; ClientLogDirectory=%LOCALAPPDATA%\xucRun\Logs'
                '; ClientLogDirectory='
                '; Controls additional broker service information events.'
                '; Only true enables them; matching is case-insensitive.'
                '; A missing, empty, or other value disables them.'
                '; Warnings, errors, and basic service events remain enabled.'
                '; BrokerInformationEvents=true'
                '; BrokerInformationEvents=false'
            )
        }
        Set-Content -LiteralPath $configurationPath -Encoding Ascii `
            -Value $configurationLines
        return
    }

    if (-not ('Native.IniFile' -as [type])) {
        Add-Type -Namespace Native -Name IniFile -MemberDefinition @'
[DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
[return: MarshalAs(UnmanagedType.Bool)]
public static extern bool WritePrivateProfileString(
    string section, string key, string value, string fileName);
'@
    }
    if (-not [Native.IniFile]::WritePrivateProfileString(
        'xucRun', 'Language', $SelectedLanguage, $configurationPath)) {
        throw "The language could not be written to $configurationPath."
    }
}

function Set-SystemPathRegistration {
    param([Parameter(Mandatory)][bool]$Enabled)

    $environmentKey = Get-Item -LiteralPath $machineEnvironmentPath
    $currentValue = [string]$environmentKey.GetValue(
        'Path', '', [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
    $parts = @($currentValue -split ';' | Where-Object {
        -not [string]::IsNullOrWhiteSpace($_) -and
        $_.TrimEnd('\') -ine $installDirectory.TrimEnd('\')
    })
    if ($Enabled) {
        $parts += $installDirectory
    }
    $newValue = $parts -join ';'
    Set-ItemProperty -LiteralPath $machineEnvironmentPath -Name Path `
        -Type ExpandString -Value $newValue
    Send-EnvironmentChanged
    return $Enabled
}

function Remove-PathCompletely {
    param([Parameter(Mandatory)][string]$Path)

    if (-not (Test-Path -LiteralPath $Path)) {
        return
    }
    Remove-Item -LiteralPath $Path -Recurse -Force
    if (Test-Path -LiteralPath $Path) {
        throw "Path could not be removed completely: $Path"
    }
}

function Assert-NoDistributionInstallation {
    $trayPath = Join-Path $installDirectory 'xucRunTray.exe'
    if (Test-Path -LiteralPath $trayPath -PathType Leaf) {
        $trayUninstallPath =
            'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\xucRun Tray'
        $german =
            [Globalization.CultureInfo]::CurrentUICulture.TwoLetterISOLanguageName -eq 'de'
        if (Test-Path -LiteralPath $trayUninstallPath) {
            if ($german) {
                throw "xucRun Tray ist noch installiert.`r`n`r`n" +
                    'Deinstallieren Sie zuerst xucRun Tray und starten Sie danach ' +
                    'die Deinstallation von xucRun erneut.'
            }
            throw "xucRun Tray is still installed.`r`n`r`n" +
                'Uninstall xucRun Tray first and then start the xucRun uninstall again.'
        }
        if ($german) {
            throw 'xucRun wurde als Teil einer Distribution installiert. ' +
                'Verwenden Sie zu ihrer Deinstallation den Installer der Distribution.'
        }
        throw 'A full xucRun Distribution installation is present. Use its installer instead.'
    }
}

function Assert-TrayCompatibleWithCorePackage {
    param(
        [Parameter(Mandatory)][string]$ProductVersion,
        [Parameter(Mandatory)][byte[]]$SignerSha256
    )

    $trayPath = Join-Path $installDirectory 'xucRunTray.exe'
    if (-not (Test-Path -LiteralPath $trayPath -PathType Leaf)) {
        return
    }

    $trayUninstallPath =
        'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\xucRun Tray'
    if (-not (Test-Path -LiteralPath $trayUninstallPath)) {
        throw 'A full xucRun Distribution installation is present. Use its installer instead.'
    }
    $registeredTrayVersion = [string](
        Get-Item -LiteralPath $trayUninstallPath).GetValue('DisplayVersion', '')
    $trayFileVersion = (Get-Item -LiteralPath $trayPath).VersionInfo.ProductVersion
    if ($registeredTrayVersion -ne $ProductVersion -or
        $trayFileVersion -ne $ProductVersion) {
        throw "The installed xucRun Tray version '$registeredTrayVersion' does not " +
            "match Core package version '$ProductVersion'. Remove xucRun Tray first, " +
            'then update xucRun and install the exactly matching Tray version.'
    }

    foreach ($path in @(
        $trayPath,
        (Join-Path $languageDirectory 'xucRunTray.de-DE.dll'),
        (Join-Path $languageDirectory 'xucRunTray.en-US.dll')
    )) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "The matching xucRun Tray installation is incomplete: $path"
        }
        $traySigner = Get-VerifiedSignerSha256 -Path $path
        if ([Convert]::ToBase64String($traySigner) -ne
            [Convert]::ToBase64String($SignerSha256)) {
            throw 'The installed xucRun Tray does not use the Core package signer. ' +
                'Remove xucRun Tray before reinstalling xucRun.'
        }
    }
}

function Uninstall-xucRun {
    Assert-NoDistributionInstallation
    if (-not (Test-Path -LiteralPath $installedBroker -PathType Leaf)) {
        throw "The installed broker is missing: $installedBroker"
    }
    if (-not (Test-Path -LiteralPath $parametersPath)) {
        throw 'The installed broker authorization policy is missing.'
    }
    $parameters = Get-Item -LiteralPath $parametersPath
    $expectedSigner = $parameters.GetValue('SensitiveClientSignerSha256', $null)
    if ($expectedSigner -isnot [byte[]] -or $expectedSigner.Count -ne 32) {
        throw 'The installed broker authorization signer is invalid.'
    }
    $actualSigner = Get-VerifiedSignerSha256 -Path $installedBroker
    if ([Convert]::ToBase64String($actualSigner) -ne
        [Convert]::ToBase64String($expectedSigner)) {
        throw 'The installed broker signer does not match its authorization policy.'
    }
    $signerCertificate =
        (Get-AuthenticodeSignature -LiteralPath $installedBroker).SignerCertificate

    if (-not $NoSuccessDialog -and -not (Confirm-Uninstall)) {
        Write-Host 'xucRun uninstall was cancelled.'
        return $false
    }

    $service = Get-Service -Name $serviceName -ErrorAction SilentlyContinue
    if (-not $service) {
        $quotedBroker = '"' + $installedBroker + '"'
        Invoke-ServiceControl -Arguments @(
            'create', $serviceName, 'binPath=', $quotedBroker, 'start=', 'demand',
            'obj=', 'LocalSystem', 'DisplayName=', $serviceDisplayName)
        $service = Get-Service -Name $serviceName -ErrorAction Stop
    }
    if ($service.Status -ne 'Stopped') {
        Stop-Service -Name $serviceName -Force
        Wait-ServiceState -Name $serviceName -Status Stopped
    }
    $quotedBroker = '"' + $installedBroker + '"'
    Invoke-ServiceControl -Arguments @(
        'config', $serviceName, 'binPath=', $quotedBroker, 'start=', 'demand',
        'obj=', 'LocalSystem', 'DisplayName=', $serviceDisplayName)
    $purgeCompleted = $false
    try {
        Invoke-ServiceControl -Arguments @(
            'sdset', $serviceName, $restrictedServiceSecurityDescriptor)
        Invoke-ServiceControl -Arguments @('start', $serviceName, 'uninstall-purge')
        Wait-ServiceState -Name $serviceName -Status Stopped
        $stoppedService = Get-CimInstance -ClassName Win32_Service `
            -Filter "Name='$serviceName'"
        if ($stoppedService -and [int]$stoppedService.ExitCode -ne 0) {
            throw "The broker could not purge its stored data (error $($stoppedService.ExitCode))."
        }
        $purgeCompleted = $true
    }
    finally {
        if (-not $purgeCompleted -and
            (Get-Service -Name $serviceName -ErrorAction SilentlyContinue)) {
            Invoke-ServiceControl -Arguments @(
                'sdset', $serviceName, $serviceSecurityDescriptor)
        }
    }
    Invoke-ServiceControl -Arguments @('delete', $serviceName)

    Get-CimInstance -ClassName Win32_Process -Filter "Name='xucRun.exe'" |
        Where-Object {
            $_.ExecutablePath -and
            [IO.Path]::GetFullPath($_.ExecutablePath).StartsWith(
                [IO.Path]::GetFullPath($installDirectory) + '\',
                [StringComparison]::OrdinalIgnoreCase)
        } |
        ForEach-Object { Stop-Process -Id $_.ProcessId -Force }

    $addedToPath = if (Test-Path -LiteralPath $uninstallRegistryPath) {
        [int](Get-Item -LiteralPath $uninstallRegistryPath).GetValue(
            'AddedToSystemPath', 0) -eq 1
    }
    else {
        $false
    }
    if ($addedToPath) {
        [void](Set-SystemPathRegistration -Enabled $false)
    }
    Remove-PathCompletely -Path $eventSourcePath
    Remove-PathCompletely -Path $uninstallRegistryPath
    Remove-PathCompletely -Path $brokerStoreDirectory
    Remove-PathCompletely -Path $installDirectory
    $removedCertificateCount = Remove-InstalledSigningCertificate `
        -Certificate $signerCertificate -ExpectedSha256 $expectedSigner
    Write-Host 'xucRun and its stored product data were uninstalled.'
    Write-Host "The local signing certificate was removed from $removedCertificateCount store entries."
    if (-not $NoSuccessDialog) {
        Show-UninstallSuccess -Certificate $signerCertificate `
            -RemovedCount $removedCertificateCount
    }
    return $true
}

if ($Uninstall) {
    try {
        Assert-WindowsPowerShell
        Assert-Administrator
        Assert-NotReparsePoint -Path $installDirectory
        $uninstalled = Uninstall-xucRun
        if ($uninstalled -eq $false) {
            return
        }
    }
    catch {
        if (-not $NoSuccessDialog) {
            $uninstallError = $_
            $logPath = $null
            $logError = $null
            try {
                $logPath = Write-UninstallDiagnostic -ErrorRecord $uninstallError
            }
            catch {
                $logError = $_.Exception.Message
            }
            Show-UninstallFailure -Details $uninstallError.Exception.Message `
                -LogPath $logPath -LogError $logError
        }
        throw
    }
    return
}

Assert-WindowsPowerShell
Assert-Administrator
Assert-NotReparsePoint -Path $installDirectory

if ([string]::IsNullOrWhiteSpace($PackageDirectory)) {
    $PackageDirectory = $PSScriptRoot
}
$packageRoot = (Resolve-Path -LiteralPath $PackageDirectory).ProviderPath
Assert-NotReparsePoint -Path $packageRoot
$packageFiles = @(Get-PackageFiles -Root $packageRoot)
$signerSha256 = Assert-ConsistentPackageSignatures -Files $packageFiles
$packageClient = $packageFiles | Where-Object RelativePath -eq 'xucRun.exe'
$packageProductVersion =
    (Get-Item -LiteralPath $packageClient.Path).VersionInfo.ProductVersion
Assert-TrayCompatibleWithCorePackage `
    -ProductVersion $packageProductVersion `
    -SignerSha256 $signerSha256
$selectedLanguage = if ($Language) {
    $Language
}
elseif ([Globalization.CultureInfo]::CurrentUICulture.TwoLetterISOLanguageName -eq 'de') {
    'de'
}
else {
    'en'
}

$stagingDirectory = Join-Path $env:ProgramData (
    'xucRun-Installer-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $stagingDirectory | Out-Null
Set-InstallDirectoryAcl -Path $stagingDirectory
try {
    foreach ($file in $packageFiles) {
        $stagedPath = Join-Path $stagingDirectory $file.RelativePath
        $stagedParent = Split-Path -Parent $stagedPath
        if (-not (Test-Path -LiteralPath $stagedParent)) {
            New-Item -ItemType Directory -Path $stagedParent -Force | Out-Null
            Set-InstallDirectoryAcl -Path $stagedParent
        }
        Copy-Item -LiteralPath $file.Path -Destination $stagedPath
        if ((Get-FileHash -LiteralPath $file.Path -Algorithm SHA256).Hash -ne
            (Get-FileHash -LiteralPath $stagedPath -Algorithm SHA256).Hash) {
            throw "Staged package file does not match its source: $($file.RelativePath)"
        }
    }
    $stagedFiles = @(Get-PackageFiles -Root $stagingDirectory)
    [void](Assert-ConsistentPackageSignatures -Files $stagedFiles)

    $existingService = Get-Service -Name $serviceName -ErrorAction SilentlyContinue
    $existingServiceWasDisabled = $false
    $existingServiceWasRunning = $false
    if ($existingService) {
        $serviceConfiguration = Get-CimInstance -ClassName Win32_Service `
            -Filter "Name='$serviceName'"
        $existingServiceWasDisabled = $serviceConfiguration.StartMode -eq 'Disabled'
        $existingServiceWasRunning = $existingService.Status -eq 'Running'
        if ($existingService.Status -ne 'Stopped') {
            Stop-Service -Name $serviceName -Force
            Wait-ServiceState -Name $serviceName -Status Stopped
        }
    }

    New-Item -ItemType Directory -Path $installDirectory -Force | Out-Null
    Set-InstallDirectoryAcl -Path $installDirectory
    New-Item -ItemType Directory -Path $languageDirectory -Force | Out-Null
    Set-InstallDirectoryAcl -Path $languageDirectory

    foreach ($file in $stagedFiles) {
        $destination = Join-Path $installDirectory $file.RelativePath
        Copy-Item -LiteralPath $file.Path -Destination $destination -Force
        if ((Get-FileHash -LiteralPath $file.Path -Algorithm SHA256).Hash -ne
            (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash) {
            throw "Installed package file does not match: $($file.RelativePath)"
        }
    }
    Set-ConfigurationLanguage -SelectedLanguage $selectedLanguage

    $quotedBroker = '"' + $installedBroker + '"'
    if ($existingService) {
        $startMode = if ($existingServiceWasDisabled) { 'disabled' } else { 'demand' }
        Invoke-ServiceControl -Arguments @(
            'config', $serviceName, 'binPath=', $quotedBroker, 'start=', $startMode,
            'obj=', 'LocalSystem', 'DisplayName=', $serviceDisplayName)
    }
    else {
        Invoke-ServiceControl -Arguments @(
            'create', $serviceName, 'binPath=', $quotedBroker, 'start=', 'demand',
            'obj=', 'LocalSystem', 'DisplayName=', $serviceDisplayName)
    }
    Invoke-ServiceControl -Arguments @('sidtype', $serviceName, 'unrestricted')
    Invoke-ServiceControl -Arguments @(
        'sdset', $serviceName, $serviceSecurityDescriptor)
    Invoke-ServiceControl -Arguments @(
        'description', $serviceName,
        'Local privileged credential and process launch broker for xucRun.')

    New-Item -Path $eventSourcePath -Force | Out-Null
    New-ItemProperty -Path $eventSourcePath -Name TypesSupported `
        -PropertyType DWord -Value 7 -Force | Out-Null
    New-ItemProperty -Path $eventSourcePath -Name EventMessageFile `
        -PropertyType ExpandString `
        -Value (Join-Path $env:SystemRoot 'System32\EventCreate.exe') -Force |
        Out-Null

    New-Item -Path $parametersPath -Force | Out-Null
    New-ItemProperty -Path $parametersPath -Name SensitiveClientImagePath `
        -PropertyType String -Value $installedClient -Force | Out-Null
    New-ItemProperty -Path $parametersPath -Name SensitiveClientSignerSha256 `
        -PropertyType Binary -Value $signerSha256 -Force | Out-Null

    $fileVersion = (Get-Item -LiteralPath $installedClient).VersionInfo.ProductVersion
    $pathWasPreviouslyAdded =
        (Test-Path -LiteralPath $uninstallRegistryPath) -and
        [int](Get-Item -LiteralPath $uninstallRegistryPath).GetValue(
            'AddedToSystemPath', 0) -eq 1
    New-Item -Path $uninstallRegistryPath -Force | Out-Null
    New-ItemProperty -Path $uninstallRegistryPath -Name DisplayName `
        -PropertyType String -Value 'xucRun' -Force | Out-Null
    New-ItemProperty -Path $uninstallRegistryPath -Name DisplayVersion `
        -PropertyType String -Value $fileVersion -Force | Out-Null
    New-ItemProperty -Path $uninstallRegistryPath -Name InstallDate `
        -PropertyType String -Value (Get-Date -Format 'yyyyMMdd') -Force |
        Out-Null
    New-ItemProperty -Path $uninstallRegistryPath -Name InstallTimestampUtc `
        -PropertyType String -Value (
            [DateTimeOffset]::UtcNow.ToString(
                'o', [Globalization.CultureInfo]::InvariantCulture)) -Force |
        Out-Null
    New-ItemProperty -Path $uninstallRegistryPath -Name Publisher `
        -PropertyType String -Value 'HT64' -Force | Out-Null
    New-ItemProperty -Path $uninstallRegistryPath -Name InstallLocation `
        -PropertyType String -Value $installDirectory -Force | Out-Null
    New-ItemProperty -Path $uninstallRegistryPath -Name DisplayIcon `
        -PropertyType String -Value $installedClient -Force | Out-Null
    New-ItemProperty -Path $uninstallRegistryPath -Name UninstallString `
        -PropertyType String -Value (
            'powershell.exe -NoProfile -WindowStyle Hidden ' +
            '-ExecutionPolicy Bypass -File "' +
            $installedInstaller + '" -Uninstall') -Force | Out-Null
    New-ItemProperty -Path $uninstallRegistryPath -Name NoModify `
        -PropertyType DWord -Value 1 -Force | Out-Null
    New-ItemProperty -Path $uninstallRegistryPath -Name NoRepair `
        -PropertyType DWord -Value 1 -Force | Out-Null

    $pathWasAdded = $pathWasPreviouslyAdded
    if ($AddToSystemPath -and -not $pathWasPreviouslyAdded) {
        $pathWasAdded = Set-SystemPathRegistration -Enabled $true
    }
    elseif (-not $AddToSystemPath -and $pathWasPreviouslyAdded) {
        [void](Set-SystemPathRegistration -Enabled $false)
        $pathWasAdded = $false
    }
    New-ItemProperty -Path $uninstallRegistryPath -Name AddedToSystemPath `
        -PropertyType DWord -Value ([int]$pathWasAdded) -Force | Out-Null

    if ($existingServiceWasRunning) {
        Start-Service -Name $serviceName
        Wait-ServiceState -Name $serviceName -Status Running
    }

    Write-Host "xucRun was installed in $installDirectory."
}
finally {
    if (Test-Path -LiteralPath $stagingDirectory) {
        Remove-PathCompletely -Path $stagingDirectory
    }
}
