# Purpose: Installs, updates, verifies, and uninstalls a signed package on a clean CI runner.
[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$PackageDirectory
)

$ErrorActionPreference = 'Stop'
$serviceName = 'xucRunBroker'
$programFilesDirectory = [Environment]::GetFolderPath(
    [Environment+SpecialFolder]::ProgramFiles)
$installDirectory = Join-Path $programFilesDirectory 'xucRun'
$installedClient = Join-Path $installDirectory 'xucRun.exe'
$installedBroker = Join-Path $installDirectory 'xucRunBroker.exe'
$installedApproval = Join-Path $installDirectory 'xucRunApproval.exe'
$installedInstaller = Join-Path $installDirectory 'Install-xucRun.ps1'
$installedLanguageDirectory = Join-Path $installDirectory 'Languages'
$configurationPath = Join-Path $installDirectory 'xucRun.ini'
$parametersPath = "HKLM:\SYSTEM\CurrentControlSet\Services\$serviceName\Parameters"
$eventSourcePath =
    "HKLM:\SYSTEM\CurrentControlSet\Services\EventLog\Application\$serviceName"
$uninstallRegistryPath =
    'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\xucRun'
$trayUninstallRegistryPath =
    'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\xucRun Tray'
$brokerStoreDirectory = Join-Path $env:ProgramData 'xucRun'
$machineEnvironmentPath =
    'HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager\Environment'
$certificateName = "xucRun Local Signing ($env:COMPUTERNAME)"
$certificateSubject = "CN=$certificateName"
$createdCertificateThumbprint = $null
$ownsCleanEnvironment = $false

function Assert-Condition {
    param(
        [Parameter(Mandatory)][bool]$Condition,
        [Parameter(Mandatory)][string]$Message
    )

    if (-not $Condition) {
        throw "Installer smoke test failed: $Message"
    }
}

function Assert-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    Assert-Condition $principal.IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator) `
        'The Windows runner process is not elevated.'
}

function Get-CertificateSha256 {
    param([Parameter(Mandatory)]
        [Security.Cryptography.X509Certificates.X509Certificate2]$Certificate)

    $sha256 = [Security.Cryptography.SHA256]::Create()
    try {
        return ,$sha256.ComputeHash($Certificate.RawData)
    }
    finally {
        $sha256.Dispose()
    }
}

function Test-SystemPathEntry {
    $environmentKey = Get-Item -LiteralPath $machineEnvironmentPath
    $pathValue = [string]$environmentKey.GetValue(
        'Path', '', [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
    return @($pathValue -split ';' | Where-Object {
        -not [string]::IsNullOrWhiteSpace($_) -and
        $_.TrimEnd('\') -ieq $installDirectory.TrimEnd('\')
    }).Count -eq 1
}

function Test-SystemPathValueKind {
    $environmentKey = Get-Item -LiteralPath $machineEnvironmentPath
    return $environmentKey.GetValueKind('Path') -eq
        [Microsoft.Win32.RegistryValueKind]::ExpandString
}

function Remove-SystemPathEntry {
    if (-not (Test-Path -LiteralPath $machineEnvironmentPath)) {
        return
    }
    $environmentKey = Get-Item -LiteralPath $machineEnvironmentPath
    $pathValue = [string]$environmentKey.GetValue(
        'Path', '', [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
    $updated = @($pathValue -split ';' | Where-Object {
        -not [string]::IsNullOrWhiteSpace($_) -and
        $_.TrimEnd('\') -ine $installDirectory.TrimEnd('\')
    }) -join ';'
    Set-ItemProperty -LiteralPath $machineEnvironmentPath -Name Path `
        -Type ExpandString -Value $updated
}

function Wait-ServiceAbsent {
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    do {
        if (-not (Get-Service -Name $serviceName -ErrorAction SilentlyContinue)) {
            return
        }
        Start-Sleep -Milliseconds 250
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "Service '$serviceName' still exists after the uninstall."
}

function Invoke-InfoCheck {
    $process = Start-Process -FilePath $installedClient `
        -ArgumentList '/info' `
        -PassThru `
        -WindowStyle Hidden
    if (-not $process.WaitForExit(30000)) {
        Stop-Process -Id $process.Id -Force
        throw 'xucRun.exe /info did not exit within 30 seconds.'
    }
    $process.WaitForExit()
    $process.Refresh()
    $exitCode = $process.ExitCode
    Assert-Condition ($null -ne $exitCode) `
        'xucRun.exe /info did not expose an exit code.'
    Assert-Condition ($exitCode -eq 0) `
        "xucRun.exe /info failed with exit code $exitCode."
}

function Assert-InstalledState {
    foreach ($requiredPath in @(
        $installedClient,
        $installedBroker,
        $installedApproval,
        $installedInstaller,
        (Join-Path $installedLanguageDirectory 'xucRun.de-DE.dll'),
        (Join-Path $installedLanguageDirectory 'xucRun.en-US.dll'),
        $configurationPath
    )) {
        Assert-Condition (Test-Path -LiteralPath $requiredPath -PathType Leaf) `
            "Installed file is missing: $requiredPath"
    }

    $metadata = @(
        @{ Path = $installedBroker; OriginalFilename = 'xucRunBroker.exe' },
        @{ Path = $installedApproval; OriginalFilename = 'xucRunApproval.exe' },
        @{
            Path = Join-Path $installedLanguageDirectory 'xucRun.de-DE.dll'
            OriginalFilename = 'xucRun.de-DE.dll'
        },
        @{
            Path = Join-Path $installedLanguageDirectory 'xucRun.en-US.dll'
            OriginalFilename = 'xucRun.en-US.dll'
        }
    )
    foreach ($entry in $metadata) {
        $versionInfo = (Get-Item -LiteralPath $entry.Path).VersionInfo
        Assert-Condition (
            $versionInfo.CompanyName -eq 'HT64' -and
            $versionInfo.FileVersion -eq '0.9.0.2' -and
            $versionInfo.ProductVersion -eq '0.9.0.2' -and
            -not [string]::IsNullOrWhiteSpace($versionInfo.FileDescription) -and
            $versionInfo.OriginalFilename -eq $entry.OriginalFilename) `
            "Installed file metadata is incomplete: $($entry.Path)"
    }

    $service = Get-CimInstance -ClassName Win32_Service `
        -Filter "Name='$serviceName'"
    Assert-Condition ($null -ne $service) 'The broker service is not registered.'
    Assert-Condition (
        [IO.Path]::GetFullPath($service.PathName.Trim('"')) -ieq
        [IO.Path]::GetFullPath($installedBroker)) `
        "The broker service image path is unexpected: $($service.PathName)"

    Assert-Condition (Test-Path -LiteralPath $parametersPath) `
        'The broker authorization registry key is missing.'
    $parameters = Get-Item -LiteralPath $parametersPath
    $authorizedClient = [string]$parameters.GetValue(
        'SensitiveClientImagePath', '')
    $authorizedSigner = $parameters.GetValue(
        'SensitiveClientSignerSha256', $null)
    Assert-Condition (
        [IO.Path]::GetFullPath($authorizedClient) -ieq
        [IO.Path]::GetFullPath($installedClient)) `
        "The authorized client path is unexpected: $authorizedClient"
    Assert-Condition (
        $authorizedSigner -is [byte[]] -and $authorizedSigner.Count -eq 32) `
        'The authorized client signer hash is invalid.'

    $clientSignature = Get-AuthenticodeSignature -LiteralPath $installedClient
    Assert-Condition (
        $clientSignature.Status -eq [Management.Automation.SignatureStatus]::Valid -and
        $null -ne $clientSignature.SignerCertificate) `
        "The installed client signature is invalid: $($clientSignature.Status)"
    $actualSigner = Get-CertificateSha256 `
        -Certificate $clientSignature.SignerCertificate
    Assert-Condition (
        [Convert]::ToBase64String($actualSigner) -eq
        [Convert]::ToBase64String($authorizedSigner)) `
        'The installed client signer does not match the broker authorization.'

    Assert-Condition (Test-Path -LiteralPath $uninstallRegistryPath) `
        'The uninstall registry entry is missing.'
    $uninstallKey = Get-Item -LiteralPath $uninstallRegistryPath
    Assert-Condition (
        [string]$uninstallKey.GetValue('DisplayName', '') -eq 'xucRun') `
        'The uninstall DisplayName is not xucRun.'
    Assert-Condition (
        [string]$uninstallKey.GetValue('DisplayVersion', '') -eq '0.9.0.2') `
        'The uninstall DisplayVersion is not 0.9.0.2.'
    Assert-Condition (
        [string]$uninstallKey.GetValue('InstallDate', '') -match '^\d{8}$') `
        'The uninstall InstallDate is missing or invalid.'
    $installTimestamp = [DateTimeOffset]::MinValue
    Assert-Condition (
        [DateTimeOffset]::TryParse(
            [string]$uninstallKey.GetValue('InstallTimestampUtc', ''),
            [Globalization.CultureInfo]::InvariantCulture,
            [Globalization.DateTimeStyles]::RoundtripKind,
            [ref]$installTimestamp)) `
        'The uninstall InstallTimestampUtc is missing or invalid.'
    Assert-Condition (
        [int]$uninstallKey.GetValue('AddedToSystemPath', 0) -eq 1) `
        'The uninstall entry does not record the PATH registration.'
    Assert-Condition (
        [string]$uninstallKey.GetValue('UninstallString', '') -match
        '(?i)powershell\.exe .*?-WindowStyle Hidden .*?-Uninstall') `
        'The uninstall entry does not hide its PowerShell host.'
    Assert-Condition (Test-SystemPathEntry) `
        'The installation directory is not registered exactly once in the system PATH.'
    Assert-Condition (Test-SystemPathValueKind) `
        'The system PATH registry value is not REG_EXPAND_SZ.'

    $configuration = [IO.File]::ReadAllText($configurationPath)
    Assert-Condition ($configuration -match '(?m)^Language=en\r?$') `
        'The installed configuration does not select English.'
}

function Remove-TestCertificate {
    $thumbprints = @()
    if (-not [string]::IsNullOrWhiteSpace($createdCertificateThumbprint)) {
        $thumbprints += $createdCertificateThumbprint
    }
    $thumbprints += @(Get-ChildItem Cert:\LocalMachine\My |
        Where-Object { $_.Subject -eq $certificateSubject } |
        ForEach-Object { $_.Thumbprint })
    foreach ($thumbprint in @($thumbprints | Select-Object -Unique)) {
        foreach ($storeName in @('My', 'Root', 'TrustedPublisher')) {
            $certificatePath =
                "Cert:\LocalMachine\$storeName\$thumbprint"
            if (Test-Path -LiteralPath $certificatePath) {
                Remove-Item -LiteralPath $certificatePath -Force
            }
        }
    }
}

function Invoke-BestEffortCleanup {
    if (-not $ownsCleanEnvironment) {
        return
    }
    if (Test-Path -LiteralPath $installedInstaller -PathType Leaf) {
        try {
            & $installedInstaller -Uninstall -NoSuccessDialog
        }
        catch {
            Write-Warning "Installed uninstaller cleanup failed: $_"
        }
    }
    $service = Get-Service -Name $serviceName -ErrorAction SilentlyContinue
    if ($service) {
        try {
            if ($service.Status -ne 'Stopped') {
                Stop-Service -Name $serviceName -Force
            }
            & "$env:SystemRoot\System32\sc.exe" delete $serviceName | Out-Null
        }
        catch {
            Write-Warning "Broker service cleanup failed: $_"
        }
    }
    foreach ($path in @(
        $eventSourcePath,
        $uninstallRegistryPath,
        $brokerStoreDirectory,
        $installDirectory
    )) {
        if (Test-Path -LiteralPath $path) {
            try {
                Remove-Item -LiteralPath $path -Recurse -Force
            }
            catch {
                Write-Warning "Path cleanup failed for '$path': $_"
            }
        }
    }
    try {
        Remove-SystemPathEntry
    }
    catch {
        Write-Warning "System PATH cleanup failed: $_"
    }
    try {
        Remove-TestCertificate
    }
    catch {
        Write-Warning "Certificate cleanup failed: $_"
    }
}

Assert-Condition (
    $PSVersionTable.PSEdition -eq 'Desktop' -and
    $PSVersionTable.PSVersion.Major -eq 5) `
    'The test must run with Windows PowerShell 5.1.'
Assert-Administrator
$packageRoot = (Resolve-Path -LiteralPath $PackageDirectory).ProviderPath
$signer = Join-Path $packageRoot 'UserCodeSigning\Sign-xucRunPackage.ps1'
$installer = Join-Path $packageRoot 'Install-xucRun.ps1'
Assert-Condition (Test-Path -LiteralPath $signer -PathType Leaf) `
    "Package signer is missing: $signer"
Assert-Condition (Test-Path -LiteralPath $installer -PathType Leaf) `
    "Installer is missing: $installer"
$installerSource = [IO.File]::ReadAllText($installer)
foreach ($requiredContract in @(
    'function Confirm-Uninstall',
    "'YesNo', 'Warning', 'Button2'",
    'Write-UninstallDiagnostic',
    'Show-UninstallFailure',
    'InstallerLogs',
    'Open log folder',
    'alle gegebenenfalls vorhandenen gespeicherten Anmeldedaten und Freigaben',
    'all stored credentials and approvals, if present',
    'Entfernt wurden:',
    'Removed:'
)) {
    Assert-Condition $installerSource.Contains($requiredContract) `
        "Installer uninstall confirmation contract is missing '$requiredContract'."
}

Assert-Condition (-not (Get-Service -Name $serviceName -ErrorAction SilentlyContinue)) `
    'The runner already has an xucRunBroker service.'
foreach ($path in @(
    $installDirectory,
    $uninstallRegistryPath,
    $brokerStoreDirectory
)) {
    Assert-Condition (-not (Test-Path -LiteralPath $path)) `
        "The runner is not clean: $path already exists."
}
$preexistingCertificate = @(Get-ChildItem Cert:\LocalMachine\My |
    Where-Object { $_.Subject -eq $certificateSubject })
Assert-Condition ($preexistingCertificate.Count -eq 0) `
    "The runner already contains the test certificate: $certificateSubject"
$ownsCleanEnvironment = $true

$testError = $null
try {
    & $signer `
        -PackageDirectory $packageRoot `
        -CertificateName $certificateName `
        -CertificateValidityYears 1

    $createdCertificates = @(Get-ChildItem Cert:\LocalMachine\My |
        Where-Object { $_.Subject -eq $certificateSubject })
    Assert-Condition ($createdCertificates.Count -eq 1) `
        'The local signing workflow did not create exactly one test certificate.'
    $createdCertificateThumbprint = $createdCertificates[0].Thumbprint

    & $signer `
        -PackageDirectory $packageRoot `
        -CertificateName $certificateName `
        -CertificateValidityYears 1
    $reusedCertificates = @(Get-ChildItem Cert:\LocalMachine\My |
        Where-Object { $_.Subject -eq $certificateSubject })
    Assert-Condition (
        $reusedCertificates.Count -eq 1 -and
        $reusedCertificates[0].Thumbprint -eq $createdCertificateThumbprint) `
        'The local signing workflow did not reuse its existing valid certificate.'

    & $installer `
        -PackageDirectory $packageRoot `
        -Language en `
        -AddToSystemPath
    Assert-InstalledState
    Invoke-InfoCheck

    Start-Service -Name $serviceName
    (Get-Service -Name $serviceName).WaitForStatus(
        [System.ServiceProcess.ServiceControllerStatus]::Running,
        [TimeSpan]::FromSeconds(30))
    & $installer `
        -PackageDirectory $packageRoot `
        -Language en `
        -AddToSystemPath
    Assert-InstalledState
    Assert-Condition (
        (Get-Service -Name $serviceName).Status -eq
        [System.ServiceProcess.ServiceControllerStatus]::Running) `
        'Repair installation did not restore the previously running broker service.'

    $installedTray = Join-Path $installDirectory 'xucRunTray.exe'
    Set-Content -LiteralPath $installedTray -Encoding Ascii -Value 'test'
    New-Item -Path $trayUninstallRegistryPath -Force | Out-Null
    $trayBlockedUninstall = $null
    try {
        & $installedInstaller -Uninstall -NoSuccessDialog
    }
    catch {
        $trayBlockedUninstall = $_
    }
    finally {
        Remove-Item -LiteralPath $installedTray -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath $trayUninstallRegistryPath -Recurse -Force `
            -ErrorAction SilentlyContinue
    }
    Assert-Condition ($null -ne $trayBlockedUninstall) `
        'Uninstall was not blocked while the optional Tray add-on was installed.'
    Assert-Condition (
        $trayBlockedUninstall.Exception.Message -match
        'xucRun Tray (ist noch|is still) installed') `
        'Blocked uninstall did not explain that xucRun Tray must be removed first.'
    Assert-InstalledState

    & $installedInstaller -Uninstall -NoSuccessDialog
    Wait-ServiceAbsent
    Assert-Condition (-not (Test-Path -LiteralPath $installDirectory)) `
        'The install directory remains after uninstall.'
    Assert-Condition (-not (Test-Path -LiteralPath $uninstallRegistryPath)) `
        'The uninstall registry entry remains after uninstall.'
    Assert-Condition (-not (Test-SystemPathEntry)) `
        'The system PATH entry remains after uninstall.'
    Assert-Condition (Test-SystemPathValueKind) `
        'The system PATH registry value is not REG_EXPAND_SZ after uninstall.'
    Assert-Condition (-not (Test-Path -LiteralPath $brokerStoreDirectory)) `
        'Broker product data remains after uninstall.'
    foreach ($storeName in @('My', 'Root', 'TrustedPublisher')) {
        Assert-Condition (
            -not (Test-Path -LiteralPath (
                "Cert:\LocalMachine\$storeName\$createdCertificateThumbprint"))) `
            "The signing certificate remains in LocalMachine\$storeName."
    }

    Write-Host 'xucRun installer smoke test passed.'
}
catch {
    $testError = $_
}
finally {
    Invoke-BestEffortCleanup
}

if ($testError) {
    throw $testError
}
