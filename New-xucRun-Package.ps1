<#
.SYNOPSIS
Creates an unsigned xucRun package from compiled public build outputs.

.DESCRIPTION
Validates the selected build's versioned graphical installer and all required
client, broker, approval, language, installation, and signing files. It then
cleans only known package outputs from a safe, non-reparse-point destination,
copies the required files, and writes SHA256SUMS.json for the resulting
package. The graphical installer is renamed to include its product version.

.PARAMETER Configuration
Selects the build configuration to package. Allowed values are Debug and
Release; the default is Debug.

.PARAMETER DestinationDirectory
Specifies the output directory. If omitted or blank, the default is
artifacts\Package\<Configuration> below the repository root. An existing
directory and all existing descendants must not be reparse points and may
contain only recognized package outputs.

.EXAMPLE
.\New-xucRun-Package.ps1 -Configuration Release -DestinationDirectory .\artifacts\Package\Release

Creates a Release package in the standard Release package directory.

.NOTES
Run on Windows after building the selected x64 configuration. The script
replaces recognized package files and removes obsolete recognized installer
files in the destination, but refuses unexpected files or reparse points. It
does not sign any file; use UserCodeSigning\Sign-xucRunPackage.ps1 afterward.
Failures such as missing build outputs, invalid versions, unsafe destinations,
or file-system errors are terminating errors and produce a nonzero host exit
code when invoked with powershell.exe -File. Success returns exit code 0.
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',

    [string]$DestinationDirectory
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($DestinationDirectory)) {
    $DestinationDirectory = Join-Path $repositoryRoot "artifacts\Package\$Configuration"
}
$destination = [IO.Path]::GetFullPath($DestinationDirectory)
$installerSource = "xucRun\x64\$Configuration\Installer.exe"
$installerSourcePath = Join-Path $repositoryRoot $installerSource
if (-not (Test-Path -LiteralPath $installerSourcePath -PathType Leaf)) {
    throw "Build output is missing: $installerSourcePath"
}
$installerProductVersion =
    (Get-Item -LiteralPath $installerSourcePath).VersionInfo.ProductVersion
if ($installerProductVersion -notmatch '^\d+\.\d+\.\d+\.\d+$') {
    throw "Installer.exe has an invalid product version: $installerProductVersion"
}
$publishedInstallerName =
    "xucRun-Installer-$installerProductVersion-x64.exe"
$sourceFiles = [ordered]@{}
$sourceFiles[$publishedInstallerName] = $installerSource
$sourceFiles += [ordered]@{
    'xucRun.exe' = "xucRun\x64\$Configuration\xucRun.exe"
    'xucRunBroker.exe' = "xucRunBroker\x64\$Configuration\xucRunBroker.exe"
    'xucRunApproval.exe' = "xucRun\x64\$Configuration\xucRunApproval.exe"
    'Install-xucRun.ps1' = 'Install-xucRun.ps1'
    'UserCodeSigning\Sign-xucRunPackage.ps1' =
        'UserCodeSigning\Sign-xucRunPackage.ps1'
    'Languages\xucRun.de-DE.dll' =
        "xucRun\x64\$Configuration\Languages\xucRun.de-DE.dll"
    'Languages\xucRun.en-US.dll' =
        "xucRun\x64\$Configuration\Languages\xucRun.en-US.dll"
}

foreach ($relativeSource in $sourceFiles.Values) {
    $source = Join-Path $repositoryRoot $relativeSource
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Build output is missing: $source"
    }
}

if (Test-Path -LiteralPath $destination) {
    $item = Get-Item -LiteralPath $destination -Force
    if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "The package destination must not be a reparse point: $destination"
    }
    $existingItems = @(Get-ChildItem -LiteralPath $destination -Recurse -Force)
    if (@($existingItems | Where-Object {
        ($_.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0
    }).Count -ne 0) {
        throw "The package destination contains a reparse point: $destination"
    }
    $allowedExistingFiles = @($sourceFiles.Keys) + 'SHA256SUMS.json'
    $unexpectedFiles = @($existingItems | Where-Object {
        $relativePath =
            $_.FullName.Substring($destination.Length).TrimStart('\')
        -not $_.PSIsContainer -and
        $relativePath -notin $allowedExistingFiles -and
        $relativePath -ne 'Installer.exe' -and
        $relativePath -notmatch
            '^xucRun-Installer-\d+\.\d+\.\d+\.\d+-x64\.exe$'
    })
    if ($unexpectedFiles.Count -ne 0) {
        throw "The package destination contains unexpected files: " +
            ($unexpectedFiles.FullName -join ', ')
    }
    foreach ($relativePath in $allowedExistingFiles) {
        $existingPath = Join-Path $destination $relativePath
        if (Test-Path -LiteralPath $existingPath -PathType Leaf) {
            Remove-Item -LiteralPath $existingPath -Force
        }
    }
    $obsoleteInstallers = @(Get-ChildItem -LiteralPath $destination -File |
        Where-Object {
            $_.Name -eq 'Installer.exe' -or
            $_.Name -match
                '^xucRun-Installer-\d+\.\d+\.\d+\.\d+-x64\.exe$'
        })
    foreach ($obsoleteInstaller in $obsoleteInstallers) {
        Remove-Item -LiteralPath $obsoleteInstaller.FullName -Force
    }
}
else {
    New-Item -ItemType Directory -Path $destination | Out-Null
}

foreach ($entry in $sourceFiles.GetEnumerator()) {
    $source = Join-Path $repositoryRoot $entry.Value
    $target = Join-Path $destination $entry.Key
    $targetDirectory = Split-Path -Parent $target
    if (-not (Test-Path -LiteralPath $targetDirectory)) {
        New-Item -ItemType Directory -Path $targetDirectory -Force | Out-Null
    }
    Copy-Item -LiteralPath $source -Destination $target
}

$packageFiles = @(Get-ChildItem -LiteralPath $destination -Recurse -File |
    ForEach-Object {
        [pscustomobject]@{
            Path = $_.FullName.Substring($destination.Length).TrimStart('\')
            SHA256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
        }
    } |
    Sort-Object Path)
$manifestPath = Join-Path $destination 'SHA256SUMS.json'
$packageFiles | ConvertTo-Json | Set-Content -LiteralPath $manifestPath -Encoding UTF8
Write-Host "Unsigned xucRun package created: $destination"
Write-Host "Graphical installer: $publishedInstallerName"
Write-Host 'Sign it before installation with UserCodeSigning\Sign-xucRunPackage.ps1.'
