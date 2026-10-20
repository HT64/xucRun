# Purpose: Creates a machine-local certificate and signs an xucRun package.
[CmdletBinding()]
param(
    [string]$PackageDirectory,

    [string]$CertificateName = "xucRun Local Signing ($env:COMPUTERNAME)",

    [ValidateRange(1, 20)]
    [int]$CertificateValidityYears = 10,

    [string]$TimestampServer
)

$ErrorActionPreference = 'Stop'

function Assert-WindowsPowerShell {
    if ($PSVersionTable.PSEdition -ne 'Desktop' -or
        $PSVersionTable.PSVersion.Major -ne 5) {
        throw 'Run this script with Windows PowerShell 5.1 (powershell.exe).'
    }
}

function Assert-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    if (-not $principal.IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'Run Windows PowerShell as administrator before signing the package.'
    }
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

function Get-PackageFiles {
    param([Parameter(Mandatory)][string]$Root)

    $requiredRelativePaths = @(
        'xucRun.exe',
        'xucRunBroker.exe',
        'xucRunApproval.exe',
        'Install-xucRun.ps1',
        'Languages\xucRun.de-DE.dll',
        'Languages\xucRun.en-US.dll'
    )
    foreach ($relativePath in $requiredRelativePaths) {
        $path = Join-Path $Root $relativePath
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Package file is missing: $path"
        }
        [pscustomobject]@{
            RelativePath = $relativePath
            Path = (Resolve-Path -LiteralPath $path).ProviderPath
        }
    }
    $installers = @(Get-ChildItem -LiteralPath $Root -File | Where-Object {
        $_.Name -match
            '^xucRun-Installer-(\d+\.\d+\.\d+\.\d+)-x64\.exe$'
    })
    if ($installers.Count -gt 1) {
        throw 'The package contains multiple versioned graphical installers.'
    }
    if ($installers.Count -eq 1) {
        $expectedVersion = [regex]::Match(
            $installers[0].Name,
            '^xucRun-Installer-(\d+\.\d+\.\d+\.\d+)-x64\.exe$'
        ).Groups[1].Value
        if ($installers[0].VersionInfo.ProductVersion -ne $expectedVersion) {
            throw "The graphical installer file name does not match its product version: " +
                $installers[0].Name
        }
        [pscustomobject]@{
            RelativePath = $installers[0].Name
            Path = $installers[0].FullName
        }
    }
}

function Get-OrCreateLocalCertificate {
    param(
        [Parameter(Mandatory)][string]$Name,
        [Parameter(Mandatory)][int]$ValidityYears
    )

    if ($Name.Length -gt 120 -or
        $Name -notmatch '^[A-Za-z0-9 ._()\-]+$') {
        throw 'The certificate name contains invalid characters or is too long.'
    }
    $subject = "CN=$Name"
    $now = Get-Date
    $certificates = @(Get-ChildItem Cert:\LocalMachine\My -CodeSigningCert |
        Where-Object {
            $_.Subject -eq $subject -and $_.HasPrivateKey -and
            $_.NotBefore -le $now -and $_.NotAfter -gt $now -and
            (Test-CodeSigningEku -Certificate $_)
        })
    if ($certificates.Count -gt 1) {
        throw "Multiple matching local certificates found: " +
            ($certificates.Thumbprint -join ', ')
    }
    if ($certificates.Count -eq 1) {
        Write-Host "Using local certificate: $($certificates[0].Thumbprint)"
        return $certificates[0]
    }

    $certificate = New-SelfSignedCertificate `
        -Type CodeSigningCert `
        -Subject $subject `
        -FriendlyName 'xucRun Local Code Signing' `
        -CertStoreLocation 'Cert:\LocalMachine\My' `
        -Provider 'Microsoft Enhanced RSA and AES Cryptographic Provider' `
        -KeySpec Signature `
        -KeyExportPolicy NonExportable `
        -KeyAlgorithm RSA `
        -KeyLength 3072 `
        -HashAlgorithm SHA256 `
        -NotAfter $now.AddYears($ValidityYears)
    Write-Host "Local certificate created: $($certificate.Thumbprint)"
    return $certificate
}

function Add-LocalCertificateTrust {
    param([Parameter(Mandatory)]
        [Security.Cryptography.X509Certificates.X509Certificate2]$Certificate)

    foreach ($storeName in @('Root', 'TrustedPublisher')) {
        $store = New-Object Security.Cryptography.X509Certificates.X509Store(
            $storeName, 'LocalMachine')
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

function Assert-LocalSignature {
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)]
        [Security.Cryptography.X509Certificates.X509Certificate2]$Certificate,
        [Parameter(Mandatory)][bool]$RequireTimestamp
    )

    $signature = Get-AuthenticodeSignature -LiteralPath $Path
    if ($signature.Status -ne [Management.Automation.SignatureStatus]::Valid -or
        -not $signature.SignerCertificate -or
        $signature.SignerCertificate.Thumbprint -ne $Certificate.Thumbprint) {
        throw "Local signature validation failed: $Path ($($signature.Status))"
    }
    if ($RequireTimestamp -and -not $signature.TimeStamperCertificate) {
        throw "The signature does not have a verifiable timestamp: $Path"
    }
}

Assert-WindowsPowerShell
Assert-Administrator
if ([string]::IsNullOrWhiteSpace($PackageDirectory)) {
    $PackageDirectory = Split-Path -Parent $PSScriptRoot
}
$packageRoot = (Resolve-Path -LiteralPath $PackageDirectory).ProviderPath
$packageFiles = @(Get-PackageFiles -Root $packageRoot)
$certificate = Get-OrCreateLocalCertificate `
    -Name $CertificateName `
    -ValidityYears $CertificateValidityYears
Add-LocalCertificateTrust -Certificate $certificate

$stagingDirectory = Join-Path ([IO.Path]::GetTempPath()) (
    'xucRun-Signing-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $stagingDirectory | Out-Null
try {
    foreach ($file in $packageFiles) {
        $stagedPath = Join-Path $stagingDirectory $file.RelativePath
        $stagedParent = Split-Path -Parent $stagedPath
        if (-not (Test-Path -LiteralPath $stagedParent)) {
            New-Item -ItemType Directory -Path $stagedParent -Force | Out-Null
        }
        Copy-Item -LiteralPath $file.Path -Destination $stagedPath
        $signatureArguments = @{
            FilePath = $stagedPath
            Certificate = $certificate
            HashAlgorithm = 'SHA256'
            IncludeChain = 'All'
        }
        if (-not [string]::IsNullOrWhiteSpace($TimestampServer)) {
            $signatureArguments.TimestampServer = $TimestampServer
        }
        $result = Set-AuthenticodeSignature @signatureArguments
        if ($result.Status -ne [Management.Automation.SignatureStatus]::Valid) {
            throw "Signing failed: $($file.RelativePath) ($($result.Status))"
        }
        Assert-LocalSignature -Path $stagedPath -Certificate $certificate `
            -RequireTimestamp (-not [string]::IsNullOrWhiteSpace($TimestampServer))
    }

    foreach ($file in $packageFiles) {
        $stagedPath = Join-Path $stagingDirectory $file.RelativePath
        Copy-Item -LiteralPath $stagedPath -Destination $file.Path -Force
        Assert-LocalSignature -Path $file.Path -Certificate $certificate `
            -RequireTimestamp (-not [string]::IsNullOrWhiteSpace($TimestampServer))
    }
}
finally {
    if (Test-Path -LiteralPath $stagingDirectory) {
        Remove-Item -LiteralPath $stagingDirectory -Recurse -Force
    }
}

$hashEntries = @(Get-ChildItem -LiteralPath $packageRoot -Recurse -File |
    Where-Object { $_.Name -ne 'SHA256SUMS.json' } |
    ForEach-Object {
        [pscustomobject]@{
            Path = $_.FullName.Substring($packageRoot.Length).TrimStart('\')
            SHA256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
        }
    } |
    Sort-Object Path)
$hashEntries | ConvertTo-Json |
    Set-Content -LiteralPath (Join-Path $packageRoot 'SHA256SUMS.json') -Encoding UTF8
Write-Host 'The xucRun package was signed with the machine-local certificate.'
