# Purpose: Creates the unsigned standalone installer, ZIP archive, and public checksums for a Core release.
[CmdletBinding()]
param(
    [ValidateSet('Release')]
    [string]$Configuration = 'Release',

    [string]$ExpectedVersion,

    [string]$DestinationDirectory
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = $PSScriptRoot
$builtInstaller = Join-Path $repositoryRoot `
    "xucRun\x64\$Configuration\Installer.exe"
if (-not (Test-Path -LiteralPath $builtInstaller -PathType Leaf)) {
    throw "Build output is missing: $builtInstaller"
}

$version = (Get-Item -LiteralPath $builtInstaller).VersionInfo.ProductVersion
if ($version -notmatch '^\d+\.\d+\.\d+\.\d+$') {
    throw "Installer.exe has an invalid product version: $version"
}
if (-not [string]::IsNullOrWhiteSpace($ExpectedVersion) -and
    $version -ne $ExpectedVersion) {
    throw "Built product version '$version' does not match expected version " +
        "'$ExpectedVersion'."
}

if ([string]::IsNullOrWhiteSpace($DestinationDirectory)) {
    $DestinationDirectory = Join-Path $repositoryRoot "artifacts\Release\$version"
}
$destination = [IO.Path]::GetFullPath($DestinationDirectory)
$packageDirectory =
    Join-Path $repositoryRoot "artifacts\CorePackage\$Configuration"
$installerName = "xucRun-Core-Installer-$version-x64.exe"
$archiveName = "xucRun-Core-$version-x64.zip"
$checksumName = 'SHA256SUMS.txt'
$expectedAssets = @($installerName, $archiveName, $checksumName)

if (Test-Path -LiteralPath $destination) {
    $destinationItem = Get-Item -LiteralPath $destination -Force
    if (($destinationItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "The release destination must not be a reparse point: $destination"
    }
    $existingItems = @(Get-ChildItem -LiteralPath $destination -Force)
    $unexpectedItems = @($existingItems | Where-Object {
        $_.PSIsContainer -or $_.Name -notin $expectedAssets
    })
    if ($unexpectedItems.Count -ne 0) {
        throw "The release destination contains unexpected items: " +
            ($unexpectedItems.FullName -join ', ')
    }
    foreach ($assetName in $expectedAssets) {
        $assetPath = Join-Path $destination $assetName
        if (Test-Path -LiteralPath $assetPath -PathType Leaf) {
            Remove-Item -LiteralPath $assetPath -Force
        }
    }
}
else {
    New-Item -ItemType Directory -Path $destination | Out-Null
}

& powershell.exe -NoProfile -ExecutionPolicy Bypass `
    -File (Join-Path $repositoryRoot 'New-xucRun-CorePackage.ps1') `
    -Configuration $Configuration `
    -DestinationDirectory $packageDirectory
if ($LASTEXITCODE -ne 0) {
    throw "Core package creation failed with exit code $LASTEXITCODE."
}

$packageInstaller = Join-Path $packageDirectory $installerName
if (-not (Test-Path -LiteralPath $packageInstaller -PathType Leaf)) {
    throw "The package does not contain the expected installer: $packageInstaller"
}

$unsignedFiles = @(
    $packageInstaller,
    (Join-Path $packageDirectory 'xucRun.exe'),
    (Join-Path $packageDirectory 'xucRunBroker.exe'),
    (Join-Path $packageDirectory 'xucRunApproval.exe'),
    (Join-Path $packageDirectory 'Install-xucRun-Core.ps1'),
    (Join-Path $packageDirectory 'Languages\xucRun.de-DE.dll'),
    (Join-Path $packageDirectory 'Languages\xucRun.en-US.dll')
)
foreach ($unsignedFile in $unsignedFiles) {
    $signature = Get-AuthenticodeSignature -LiteralPath $unsignedFile
    if ($signature.Status -ne [Management.Automation.SignatureStatus]::NotSigned) {
        throw "The unsigned public release contains a signed file: $unsignedFile " +
            "($($signature.Status))"
    }
}

$standaloneInstaller = Join-Path $destination $installerName
Copy-Item -LiteralPath $packageInstaller -Destination $standaloneInstaller

$archivePath = Join-Path $destination $archiveName
Compress-Archive -LiteralPath (
    Get-ChildItem -LiteralPath $packageDirectory -Force |
        ForEach-Object { $_.FullName }
) -DestinationPath $archivePath -CompressionLevel Optimal

$checksumLines = foreach ($assetPath in @($standaloneInstaller, $archivePath)) {
    $hash = (Get-FileHash -LiteralPath $assetPath -Algorithm SHA256).Hash
    "$hash  $([IO.Path]::GetFileName($assetPath))"
}
$checksumPath = Join-Path $destination $checksumName
[IO.File]::WriteAllLines(
    $checksumPath,
    $checksumLines,
    [Text.UTF8Encoding]::new($false))

Write-Host "Unsigned xucRun Core release created: $destination"
Write-Host "Version: $version"
foreach ($assetName in $expectedAssets) {
    Write-Host "Asset: $assetName"
}
