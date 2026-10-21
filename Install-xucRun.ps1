# Purpose: Installs or removes the public xucRun package.
[CmdletBinding(DefaultParameterSetName = 'Install')]
param(
    [Parameter(ParameterSetName = 'Install')]
    [string]$PackageDirectory,

    [Parameter(ParameterSetName = 'Install')]
    [ValidateSet('de', 'en')]
    [string]$Language,

    [Parameter(ParameterSetName = 'Install')]
    [switch]$AddToSystemPath,

    [Parameter(Mandatory, ParameterSetName = 'Uninstall')]
    [switch]$Uninstall
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

    $lines = if (Test-Path -LiteralPath $configurationPath -PathType Leaf) {
        @([IO.File]::ReadAllLines($configurationPath))
    }
    else {
        @()
    }
    $updated = @()
    $languageWritten = $false
    foreach ($line in $lines) {
        if ($line -match '^\s*Language\s*=') {
            if (-not $languageWritten) {
                $updated += "Language=$SelectedLanguage"
                $languageWritten = $true
            }
        }
        else {
            $updated += $line
        }
    }
    if (-not $languageWritten) {
        $updated += "Language=$SelectedLanguage"
    }
    [IO.File]::WriteAllLines(
        $configurationPath,
        $updated,
        [Text.UTF8Encoding]::new($true))
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
    Set-ItemProperty -LiteralPath $machineEnvironmentPath -Name Path -Value $newValue
    [Environment]::SetEnvironmentVariable('Path', $newValue, 'Machine')
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
        if (Test-Path -LiteralPath $trayUninstallPath) {
            throw 'The optional xucRun Tray add-on is installed. Remove xucRun Tray first, ' +
                'then update or remove xucRun and install the exactly matching Tray version.'
        }
        throw 'A full xucRun Distribution installation is present. Use its installer instead.'
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
    Write-Host 'xucRun and its stored product data were uninstalled.'
}

Assert-WindowsPowerShell
Assert-Administrator
Assert-NotReparsePoint -Path $installDirectory

if ($Uninstall) {
    Uninstall-xucRun
    return
}

Assert-NoDistributionInstallation
if ([string]::IsNullOrWhiteSpace($PackageDirectory)) {
    $PackageDirectory = $PSScriptRoot
}
$packageRoot = (Resolve-Path -LiteralPath $PackageDirectory).ProviderPath
Assert-NotReparsePoint -Path $packageRoot
$packageFiles = @(Get-PackageFiles -Root $packageRoot)
$signerSha256 = Assert-ConsistentPackageSignatures -Files $packageFiles
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
    if ($existingService) {
        $serviceConfiguration = Get-CimInstance -ClassName Win32_Service `
            -Filter "Name='$serviceName'"
        $existingServiceWasDisabled = $serviceConfiguration.StartMode -eq 'Disabled'
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
    New-ItemProperty -Path $uninstallRegistryPath -Name Publisher `
        -PropertyType String -Value 'HT64' -Force | Out-Null
    New-ItemProperty -Path $uninstallRegistryPath -Name InstallLocation `
        -PropertyType String -Value $installDirectory -Force | Out-Null
    New-ItemProperty -Path $uninstallRegistryPath -Name DisplayIcon `
        -PropertyType String -Value $installedClient -Force | Out-Null
    New-ItemProperty -Path $uninstallRegistryPath -Name UninstallString `
        -PropertyType String -Value (
            'powershell.exe -NoProfile -ExecutionPolicy Bypass -File "' +
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

    Write-Host "xucRun was installed in $installDirectory."
}
finally {
    if (Test-Path -LiteralPath $stagingDirectory) {
        Remove-PathCompletely -Path $stagingDirectory
    }
}
