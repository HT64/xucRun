# Manual removal

**Language:** [Deutsch](../de/MANUAL_REMOVAL.md) | English

This document lists the artifacts created by xucRun and describes their manual removal. Use this
procedure only if the normal uninstaller cannot be used or does not complete successfully. The
normal uninstaller is safer because it validates the installation, asks for confirmation, purges
broker-managed secrets in the LocalSystem context, and removes the managed signing certificate
only after verifying it.

Manual removal permanently deletes stored credentials, approvals, backups, logs, and
configuration. Back up anything that must be retained before continuing.

## Scope

The procedure removes xucRun-owned installed files and settings. It does not erase general Windows
history such as already written Application events, Service Control Manager events, Prefetch,
Amcache, compatibility or recent-item data, restore points, external backups, or records maintained
by security and management software. Do not clear the shared Windows Application event log merely
to remove xucRun events.

## Artifact inventory

| Type | Path or name | Notes |
|---|---|---|
| Program directory | `%ProgramFiles%\xucRun` | Shared with the separately installed optional [xucRun Tray add-on](https://github.com/HT64/xucRun-Tray) while it is installed |
| Core executables | `xucRun.exe`, `xucRunBroker.exe`, `xucRunApproval.exe` | In the program directory |
| Installer script | `Install-xucRun.ps1` | In the program directory |
| Configuration | `xucRun.ini` | In the program directory |
| Language files | `Languages\xucRun.de-DE.dll`, `Languages\xucRun.en-US.dll` | In the program directory |
| Service | `xucRunBroker` | Includes the service `Parameters` registry subkey |
| Service registry key | `HKLM\SYSTEM\CurrentControlSet\Services\xucRunBroker` | Removed by deleting the service |
| Event source | `HKLM\SYSTEM\CurrentControlSet\Services\EventLog\Application\xucRunBroker` | Existing events remain in the shared Application log |
| Uninstall entry | `HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\xucRun` | Machine-wide |
| Machine `PATH` | `%ProgramFiles%\xucRun` | Present only if selected during installation |
| Product data | `%ProgramData%\xucRun` | Includes credential metadata in `BrokerStore\v1`, encrypted archives and restore journals in `BrokerBackups\v3-a8`, and `InstallerLogs` |
| Broker secrets | xucRun credential, approval, and authenticity-marker targets | Stored through Windows Credential Manager in the LocalSystem context |
| Signing certificate | Normally `CN=xucRun Local Signing (<COMPUTERNAME>)` | May exist in `LocalMachine\My`, `LocalMachine\Root`, and `LocalMachine\TrustedPublisher`; `My` includes its private key |
| Client logs | `xucRun_YYYYMMDD.log` | Only when `[Logging] ClientLogDirectory` is configured; the directory may be anywhere and environment variables are expanded in each user's context |
| Temporary wrappers | `xucRun_wrapper_<32 hexadecimal characters>.bat` | Created only for certain batch-file names; normally deleted from a suitable volume named `RamDisk` or the target account's `%TEMP%` |
| Failed-install remnants | `%ProgramData%\xucRun-Installer-<32 hexadecimal characters>` | Protected staging directories normally removed automatically |
| Failed-signing remnants | `%TEMP%\xucRun-Signing-<32 hexadecimal characters>` | Created only while signing a package and normally removed automatically |
| Installer output remnants | `%TEMP%\xucRun-Installer-<32 hexadecimal characters>.stdout.log` and `.stderr.log` | Normally removed automatically |

The certificate subject above applies to the certificate generated with the standard name. If
manual signing deliberately used a different `CertificateName`, do not identify that certificate
by a partial name. Record and verify its exact thumbprint before removing it.

## Manual removal procedure

Run the commands below in an elevated Windows PowerShell 5.1 window.

### 1. Remove xucRun Tray first

If xucRun Tray is installed, remove it before removing the Core. Follow the xucRun Tray project's
[manual removal instructions](https://github.com/HT64/xucRun-Tray/blob/main/docs/en/MANUAL_REMOVAL.md)
if its normal uninstaller is also unavailable. Removing the Core first can leave running Tray
processes and scheduled tasks that refer to files in the shared program directory.

### 2. Record non-default locations and the signer

Do this before deleting `xucRun.ini` or `xucRunBroker.exe`.

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

Keep the displayed subject, issuer, and complete thumbprint. If `$signer` is empty, do not guess
which certificate belongs to xucRun. A certificate must not be removed unless its identity is
known.

The configured client log path may contain environment variables. Resolve it separately for every
Windows account that used xucRun. Remove only files matching `xucRun_YYYYMMDD.log`, not an entire
shared log directory.

### 3. Purge broker-managed secrets

This step is required for complete removal of xucRun's machine-bound credentials and approvals:

```powershell
& (Join-Path $env:ProgramFiles 'xucRun\xucRun.exe') /clear-all
if ($LASTEXITCODE -ne 0) {
    throw "xucRun /clear-all failed with exit code $LASTEXITCODE."
}
```

Confirm the deletion when prompted. The command deletes credentials, approvals, authenticity
markers, and credential metadata for all Windows users and stops the broker.

If the broker is disabled, run `xucRun.exe /enable-broker` first and then repeat `/clear-all`. If
the binaries or service are missing, damaged, or no longer trusted, do not substitute direct
Credential Manager manipulation. Reinstall the same trusted xucRun version, then use `/clear-all`
or the normal uninstaller. Without a successful broker purge, remaining LocalSystem Credential
Manager targets cannot be ruled out.

### 4. Stop xucRun processes and delete the service

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

`sc.exe` may report that the service is marked for deletion while another process still has an
open handle. In that case, restart Windows before continuing. Do not manually delete the service
registry key while the service is still registered.

### 5. Remove the optional machine `PATH` entry

This removes only the exact xucRun directory and preserves every other entry:

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

New processes see the changed machine `PATH`; already running processes retain their existing
environment.

### 6. Remove registry entries

```powershell
Remove-Item -LiteralPath `
    'Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\xucRun' `
    -Recurse -Force -ErrorAction SilentlyContinue

Remove-Item -LiteralPath `
    'Registry::HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Services\EventLog\Application\xucRunBroker' `
    -Recurse -Force -ErrorAction SilentlyContinue
```

The `xucRunBroker` service key, including `Parameters`, is removed by `sc.exe delete`. If it still
exists after a restart, check that the service no longer appears in `Get-Service` before removing
the leftover key.

### 7. Remove files and product data

```powershell
Remove-Item -LiteralPath (Join-Path $env:ProgramFiles 'xucRun') `
    -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath (Join-Path $env:ProgramData 'xucRun') `
    -Recurse -Force -ErrorAction SilentlyContinue
```

If either directory remains, inspect the reported access or sharing error instead of taking
ownership of unrelated files. Restart Windows if a verified xucRun file is still in use.

Remove configured client log files from the location recorded in step 2. Only delete names in the
exact `xucRun_YYYYMMDD.log` format.

### 8. Remove the verified managed certificate

Proceed only with the exact signer recorded in step 2. For the standard generated certificate,
both `Subject` and `Issuer` must equal `CN=xucRun Local Signing (<COMPUTERNAME>)`. A certificate
created with a custom name can also sign other software and must not be removed unless that
consequence is understood.

```powershell
if (-not $signer) {
    throw 'No verified xucRun signer was recorded. Do not remove a certificate by name alone.'
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

If this is not the installer-generated managed certificate, retaining it is safer than deleting a
certificate whose other uses are unknown.

### 9. Inspect conditional and interrupted-operation remnants

After all xucRun processes have ended, inspect the following locations:

- each account's `%TEMP%` and any suitable volume named `RamDisk` for an exact
  `xucRun_wrapper_<32 hexadecimal characters>.bat` name;
- `%ProgramData%` for `xucRun-Installer-<32 hexadecimal characters>` directories;
- the account used for package signing for `%TEMP%\xucRun-Signing-<32 hexadecimal characters>`;
- the account that started an installer for matching `.stdout.log` and `.stderr.log` files listed
  in the inventory.

These artifacts are normally deleted automatically and should exist only after interruption or
failure. Verify the full name and contents before deleting an individual remnant. Do not use a
recursive wildcard deletion against `%ProgramData%`, `%TEMP%`, a `RamDisk`, or a user profile.

## Verification

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

Successful verification produces no service, registry, `PATH`, or certificate output, and both
`Test-Path` calls return `False`. Also verify the explicitly recorded client log locations and any
conditional remnants applicable to this computer.
