<#
.SYNOPSIS
Signs an xucRun package with a trusted machine-local certificate.

.DESCRIPTION
Finds one current machine code-signing certificate with the requested subject
and private key or creates a nonexportable 3072-bit RSA certificate. It adds
that certificate to the LocalMachine Root and TrustedPublisher stores and
copies all required package payloads into a ProgramData staging directory that
is created together with an access control list for administrators and SYSTEM
only. Each staged copy must match its source hash before it is signed and
verified. The package payloads are replaced only after all staged signatures
succeed, and SHA256SUMS.json is regenerated.

.PARAMETER PackageDirectory
Specifies the package to sign. If omitted or blank, the default is the parent
directory of this script's directory. The directory must contain all required
xucRun payloads and no more than one correctly versioned graphical installer.

.PARAMETER CertificateName
Specifies the certificate common name. The default is
xucRun Local Signing (<computer-name>). The value is limited to 120 characters
and may contain only ASCII letters, digits, spaces, periods, underscores,
parentheses, and hyphens.

.PARAMETER CertificateValidityYears
Specifies the validity period for a newly created certificate. The default is
10 years, and the allowed range is 1 through 20. This value does not alter a
reused certificate.

.PARAMETER TimestampServer
Optionally specifies the timestamp server passed to Set-AuthenticodeSignature.
When supplied and nonblank, every resulting signature must expose a verifiable
timestamp.

.EXAMPLE
.\UserCodeSigning\Sign-xucRunPackage.ps1 -PackageDirectory .\artifacts\Package\Release -CertificateValidityYears 5

Creates or reuses the default local certificate, trusts it on this computer,
and signs the Release package.

.NOTES
Requires elevated Windows PowerShell 5.1 and the LocalMachine certificate
providers. Creating or reusing the certificate changes the machine My, Root,
and TrustedPublisher stores; the private key of a new certificate is
nonexportable. Signing uses a temporary directory named
xucRun-Signing-<32 hexadecimal characters> under the ProgramData known folder;
it is accessible to administrators and SYSTEM only from the moment it is
created and is removed on success and failure. Package payloads are replaced
with signed copies and the hash
manifest is rewritten. Trust is local to this computer and is not equivalent
to a publicly trusted publisher. Missing or ambiguous certificates, invalid
names, package errors, signature or timestamp validation failures, and
file-system failures are terminating errors and produce a nonzero host exit
code. Success returns exit code 0.
#>
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

function Assert-ProtectedDirectory {
    param([Parameter(Mandatory)][string]$Path)

    $directory = [IO.DirectoryInfo]::new($Path)
    if (-not $directory.Exists -or
        ($directory.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Staging directory is missing or is a reparse point: $Path"
    }
    $trustedSids = @('S-1-5-18', 'S-1-5-32-544')
    $sidType = [Security.Principal.SecurityIdentifier]
    $acl = [IO.Directory]::GetAccessControl($Path,
        [Security.AccessControl.AccessControlSections]'Owner, Access')
    $rules = @($acl.GetAccessRules($true, $true, $sidType))
    $unexpectedRules = @($rules | Where-Object {
        $_.AccessControlType -ne [Security.AccessControl.AccessControlType]::Allow -or
        $_.IdentityReference.Value -notin $trustedSids
    })
    if ($acl.GetOwner($sidType).Value -ne 'S-1-5-32-544' -or
        -not $acl.AreAccessRulesProtected -or
        $rules.Count -eq 0 -or $unexpectedRules.Count -ne 0) {
        throw "Staging directory is not restricted to administrators and SYSTEM: $Path"
    }
    if (@([IO.Directory]::GetFileSystemEntries($Path)).Count -ne 0) {
        throw "Staging directory was not created empty: $Path"
    }
}

function New-ProtectedDirectory {
    param([Parameter(Mandatory)][string]$Path)

    if (Test-Path -LiteralPath $Path) {
        throw "Staging path already exists: $Path"
    }
    $inheritance = [Security.AccessControl.InheritanceFlags]'ContainerInherit, ObjectInherit'
    $propagation = [Security.AccessControl.PropagationFlags]::None
    $allow = [Security.AccessControl.AccessControlType]::Allow
    $systemSid = [Security.Principal.SecurityIdentifier]::new('S-1-5-18')
    $administratorsSid =
        [Security.Principal.SecurityIdentifier]::new('S-1-5-32-544')
    $acl = [Security.AccessControl.DirectorySecurity]::new()
    $acl.SetAccessRuleProtection($true, $false)
    $acl.SetOwner($administratorsSid)
    $acl.AddAccessRule([Security.AccessControl.FileSystemAccessRule]::new(
        $systemSid, 'FullControl', $inheritance, $propagation, $allow))
    $acl.AddAccessRule([Security.AccessControl.FileSystemAccessRule]::new(
        $administratorsSid, 'FullControl', $inheritance, $propagation, $allow))
    # The descriptor must be part of the creation call. A directory that is
    # restricted only afterwards briefly inherits the ProgramData rights that
    # let any account add entries it continues to own.
    [void][IO.Directory]::CreateDirectory($Path, $acl)
    Assert-ProtectedDirectory -Path $Path
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

$programDataDirectory = [Environment]::GetFolderPath(
    [Environment+SpecialFolder]::CommonApplicationData)
if ([string]::IsNullOrWhiteSpace($programDataDirectory)) {
    throw 'The Windows ProgramData directory could not be resolved.'
}
$stagingDirectory = Join-Path $programDataDirectory (
    'xucRun-Signing-' + [Guid]::NewGuid().ToString('N'))
New-ProtectedDirectory -Path $stagingDirectory
try {
    foreach ($file in $packageFiles) {
        $stagedPath = Join-Path $stagingDirectory $file.RelativePath
        $stagedParent = Split-Path -Parent $stagedPath
        if (-not (Test-Path -LiteralPath $stagedParent)) {
            New-ProtectedDirectory -Path $stagedParent
        }
        if (Test-Path -LiteralPath $stagedPath) {
            throw "Staged package file already exists: $($file.RelativePath)"
        }
        Copy-Item -LiteralPath $file.Path -Destination $stagedPath
        if ((Get-FileHash -LiteralPath $file.Path -Algorithm SHA256).Hash -ne
            (Get-FileHash -LiteralPath $stagedPath -Algorithm SHA256).Hash) {
            throw "Staged package file does not match its source: $($file.RelativePath)"
        }
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
