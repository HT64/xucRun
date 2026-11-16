<!--
Purpose: Explains how users manually remove xucRun in English.
Approach: Provides ordered cleanup and verification steps for each installed component.
-->
# Manual removal

**Language:** [Deutsch](../de/MANUAL_REMOVAL.md) | English

Use this procedure only if [normal uninstallation](INSTALLATION.md#uninstall) is unavailable or
cannot complete. Normal uninstallation validates the installation and removes components in the
required order; manual removal must explicitly reproduce those checks.

Manual removal permanently deletes stored credentials, approvals, backups, logs, and
configuration. Back up anything that must be retained before continuing.

Existing Windows events and external copies are not removed. Do not clear the shared Windows
Application event log merely to remove existing xucRun events.

## Artifact inventory

| Type | Path or name | Notes |
|---|---|---|
| Program directory | `%ProgramFiles%\xucRun` | Shared with the separately installed optional [xucRun Tray add-on](https://github.com/HT64/xucRun-Tray/blob/main/docs/en/MANUAL_REMOVAL.md) while it is installed |
| Service | `xucRunBroker` | Includes service configuration and authorization policy |
| Event source | `HKLM\SYSTEM\CurrentControlSet\Services\EventLog\Application\xucRunBroker` | Existing events remain in the shared Application log |
| Uninstall entry | `HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\xucRun` | Machine-wide |
| Machine `PATH` | `%ProgramFiles%\xucRun` | Present only if selected during installation |
| Product data | `%ProgramData%\xucRun` | Includes metadata, encrypted backups, restore journals, and installer logs |
| Broker data | xucRun credential, approval, and authenticity-marker targets | Stored through Windows Credential Manager in the LocalSystem context |
| Signing certificate | Normally `CN=xucRun Local Signing (<COMPUTERNAME>)` | May exist in `LocalMachine\My`, `LocalMachine\Root`, and `LocalMachine\TrustedPublisher`; `My` includes its private key |
| Client logs | `xucRun_YYYYMMDD.log` | Only when `[Logging] ClientLogDirectory` is configured; the directory may be anywhere and environment variables are expanded in each user's context |

## Manual removal procedure

Start 64-bit Windows PowerShell 5.1 as an administrator and complete the following steps in the
given order. First resolve the trusted Windows directories in that window through Windows APIs
rather than inherited environment variables:

```powershell
if (-not [Environment]::Is64BitProcess) {
    throw 'Manual removal requires 64-bit PowerShell.'
}
$programFilesDirectory = [Environment]::GetFolderPath(
    [Environment+SpecialFolder]::ProgramFiles)
$programDataDirectory = [Environment]::GetFolderPath(
    [Environment+SpecialFolder]::CommonApplicationData)
$systemDirectory = [Environment]::SystemDirectory

if ([string]::IsNullOrWhiteSpace($programFilesDirectory) -or
    [string]::IsNullOrWhiteSpace($programDataDirectory) -or
    [string]::IsNullOrWhiteSpace($systemDirectory)) {
    throw 'The required Windows directories could not be resolved.'
}
```

### 1. Remove xucRun Tray first

If xucRun Tray is installed, remove it before removing the Core. Follow the xucRun Tray project's
[manual removal instructions](https://github.com/HT64/xucRun-Tray/blob/main/docs/en/MANUAL_REMOVAL.md)
if its normal uninstaller is also unavailable. Removing the Core first can leave running Tray
processes and scheduled tasks that refer to files in the shared program directory.

### 2. Record non-default locations and the managed certificate

Complete this step before deleting the service configuration, `xucRun.ini`, or the program files.
The certificate check uses the same SHA-256 binding as normal uninstallation.

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
        throw "The required xucRun file is missing: $Path"
    }
    $signature = Get-AuthenticodeSignature -LiteralPath $Path
    if ($signature.Status -ne [Management.Automation.SignatureStatus]::Valid -or
        -not $signature.SignerCertificate) {
        throw "The xucRun signature is invalid: $Path ($($signature.Status))"
    }
    $actualSha256 = Get-CertificateSha256 -Certificate $signature.SignerCertificate
    if ([Convert]::ToBase64String($actualSha256) -ne
        [Convert]::ToBase64String($ExpectedSha256)) {
        throw "The signer does not match the broker authorization policy: $Path"
    }
    return $signature.SignerCertificate
}

if (-not (Test-Path -LiteralPath $parametersPath)) {
    throw 'The broker authorization policy is missing.'
}
$parameters = Get-Item -LiteralPath $parametersPath
$expectedSignerSha256 = $parameters.GetValue('SensitiveClientSignerSha256', $null)
if ($expectedSignerSha256 -isnot [byte[]] -or $expectedSignerSha256.Count -ne 32) {
    throw 'The broker authorization policy does not contain a valid signer.'
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
        'The verified signer is not the standard managed xucRun certificate. ' +
        'The certificate will not be removed automatically.')
}

$brokerSigner | Format-List Subject, Issuer, Thumbprint, NotBefore, NotAfter
```

If verification stops, do not run any existing xucRun binary and do not remove a certificate.
Reinstall the same trusted xucRun version and then start again. A custom-named or otherwise used
signer is retained.

The configured client log path may contain environment variables. Resolve it separately for every
Windows account that used xucRun. Remove only files matching `xucRun_YYYYMMDD.log`, not an entire
shared log directory.

### 3. Purge broker-managed secrets

This step is required for complete removal of xucRun's machine-bound credentials and approvals:

```powershell
& (Join-Path $programFilesDirectory 'xucRun\xucRun.exe') /clear-all
if ($LASTEXITCODE -ne 0) {
    throw "xucRun /clear-all failed with exit code $LASTEXITCODE."
}
```

Confirm the deletion when prompted. The command deletes credentials, approvals, authenticity
markers, and credential metadata for all Windows users and stops the broker.

If the broker is disabled, run `xucRun.exe /enable-broker` first and then repeat `/clear-all`. If
the binaries or service are missing, do not substitute direct Credential Manager manipulation.
Reinstall the same trusted xucRun version, then use `/clear-all` or the normal uninstaller. Without
a successful broker purge, remaining LocalSystem Credential Manager targets cannot be ruled out.

### 4. Stop xucRun processes and delete the service

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
        throw "The service could not be deleted. sc.exe exit code: $LASTEXITCODE"
    }
}
```

`sc.exe` may report that the service is marked for deletion while another process still has an
open handle. In that case, restart Windows before continuing. Do not manually delete the service
registry key while the service is still registered.

### 5. Remove the optional machine `PATH` entry

This code reads and writes the unexpanded registry value so that entries such as `%SystemRoot%`
remain unchanged:

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

New processes use the changed machine `PATH`; already running processes retain their existing
environment. If there is no exact xucRun entry, the value remains unchanged.

### 6. Remove registry entries

```powershell
$uninstallPath =
    'Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\xucRun'
$eventSourcePath =
    'Registry::HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Services\EventLog\Application\xucRunBroker'

foreach ($path in $uninstallPath, $eventSourcePath) {
    if (Test-Path -LiteralPath $path) {
        Remove-Item -LiteralPath $path -Recurse -Force -ErrorAction Stop
        if (Test-Path -LiteralPath $path) {
            throw "The registry key could not be removed completely: $path"
        }
    }
}
```

The `xucRunBroker` service key, including `Parameters`, is removed by `sc.exe delete`. If it still
exists after a restart, check that the service no longer appears in `Get-Service` before removing
the leftover key.

### 7. Remove files and product data

```powershell
function Remove-xucRunDirectory {
    param([Parameter(Mandatory)][string]$Path)

    if (-not (Test-Path -LiteralPath $Path)) {
        return
    }
    $item = Get-Item -LiteralPath $Path -Force -ErrorAction Stop
    if (-not $item.PSIsContainer -or
        ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "The xucRun directory is not a regular directory: $Path"
    }
    Remove-Item -LiteralPath $Path -Recurse -Force -ErrorAction Stop
    if (Test-Path -LiteralPath $Path) {
        throw "The xucRun directory could not be removed completely: $Path"
    }
}

Remove-xucRunDirectory -Path (Join-Path $programFilesDirectory 'xucRun')
Remove-xucRunDirectory -Path (Join-Path $programDataDirectory 'xucRun')
```

Inspect a reported access, sharing, or reparse-point error instead of taking ownership of unrelated
files or following an unexpected link. Restart Windows if a verified xucRun file is still in use.

Remove configured client log files from the location recorded in step 2. Only delete names in the
exact `xucRun_YYYYMMDD.log` format.

### 8. Remove the verified managed certificate

The following code removes only the standard certificate verified against the broker authorization
policy in step 2. A custom-named or otherwise used signer is retained.

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

A retained custom certificate may still be required outside xucRun. Its removal is therefore not
part of this procedure.

### 9. Inspect conditional and interrupted-operation remnants

After all xucRun processes have ended, inspect the following locations:

- each account's `%TEMP%` and any suitable volume named `RamDisk` for an exact
  `xucRun_wrapper_<32 hexadecimal characters>.bat` name;
- `%ProgramData%` for `xucRun-Installer-<32 hexadecimal characters>` and
  `xucRun-Signing-<32 hexadecimal characters>` directories;
- the `%TEMP%` directory of the account that started an installer for
  `xucRun-Installer-<32 hexadecimal characters>.stdout.log` and its corresponding `.stderr.log`
  file.

These artifacts are normally deleted automatically and should exist only after interruption or
failure. Verify the full name and contents before deleting an individual remnant. Do not use a
recursive wildcard deletion against `%ProgramData%`, `%TEMP%`, a `RamDisk`, or a user profile.

## Verification

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

After successful removal, every property with the suffix `Present` is `False`.
`ManagedCertificateWasApplicable` is `True` only if the standard managed certificate was expected
to be removed. Also verify the explicitly recorded client log locations and any conditional
remnants applicable to this computer.
