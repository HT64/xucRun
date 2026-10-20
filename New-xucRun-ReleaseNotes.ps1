# Purpose: Creates bilingual GitHub release notes from the matching changelog sections and checksums.
[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidatePattern('^\d+\.\d+\.\d+\.\d+$')]
    [string]$Version,

    [Parameter(Mandatory)]
    [string]$ChecksumPath,

    [Parameter(Mandatory)]
    [string]$DestinationPath
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = $PSScriptRoot

function Get-ChangelogVersionBody {
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][string]$ReleaseVersion
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Changelog is missing: $Path"
    }
    $text = [IO.File]::ReadAllText($Path)
    $pattern = '(?ms)^##\s+\[' + [regex]::Escape($ReleaseVersion) +
        '\]\s+-\s+\d{4}-\d{2}-\d{2}\s*\r?\n(?<Body>.*?)(?=^##\s+\[|^\[[^\]]+\]:|\z)'
    $match = [regex]::Match($text, $pattern)
    if (-not $match.Success) {
        throw "Changelog section for version $ReleaseVersion is missing: $Path"
    }
    $body = $match.Groups['Body'].Value.Trim()
    if ([string]::IsNullOrWhiteSpace($body)) {
        throw "Changelog section for version $ReleaseVersion is empty: $Path"
    }
    return $body
}

if (-not (Test-Path -LiteralPath $ChecksumPath -PathType Leaf)) {
    throw "Release checksum file is missing: $ChecksumPath"
}
$checksums = [IO.File]::ReadAllText(
    (Resolve-Path -LiteralPath $ChecksumPath).ProviderPath).Trim()
$escapedVersion = [regex]::Escape($Version)
$installerChecksumPattern =
    '(?m)^[0-9A-F]{64}\s{2}xucRun-Installer-' +
    $escapedVersion + '-x64\.exe\r?$'
$archiveChecksumPattern =
    '(?m)^[0-9A-F]{64}\s{2}xucRun-' +
    $escapedVersion + '-x64\.zip\r?$'
if ($checksums -notmatch $installerChecksumPattern -or
    $checksums -notmatch $archiveChecksumPattern) {
    throw 'Release checksums do not contain the expected installer and ZIP archive.'
}

$englishChanges = Get-ChangelogVersionBody `
    -Path (Join-Path $repositoryRoot 'CHANGELOG.md') `
    -ReleaseVersion $Version
$germanChanges = Get-ChangelogVersionBody `
    -Path (Join-Path $repositoryRoot 'CHANGELOG.de.md') `
    -ReleaseVersion $Version

$notes = @"
## Important notice / Wichtiger Hinweis

xucRun is a non-commercial open-source project. The provided installer is not signed by a
publicly trusted publisher. Windows may therefore report an unknown publisher when it is first
started. Download the files only from this GitHub Release and compare the SHA-256 checksum before
starting the installer.

xucRun ist ein nicht kommerzielles Open-Source-Projekt. Die bereitgestellte Installer-Datei
besitzt keine Signatur eines öffentlich vertrauenswürdigen Herausgebers. Windows kann deshalb beim
ersten Start einen unbekannten Herausgeber melden. Laden Sie die Dateien ausschließlich aus diesem
GitHub Release herunter und vergleichen Sie vor dem Start die SHA-256-Prüfsumme.

## Changes

$englishChanges

Full [changelog](https://github.com/HT64/xucRun/blob/v$Version/CHANGELOG.md)

## Änderungen

$germanChanges

Vollständiges [Änderungsprotokoll](https://github.com/HT64/xucRun/blob/v$Version/CHANGELOG.de.md)

## SHA-256

~~~text
$checksums
~~~
"@

$destinationFullPath = [IO.Path]::GetFullPath($DestinationPath)
$destinationDirectory = Split-Path -Parent $destinationFullPath
if (-not (Test-Path -LiteralPath $destinationDirectory)) {
    New-Item -ItemType Directory -Path $destinationDirectory -Force | Out-Null
}
[IO.File]::WriteAllText(
    $destinationFullPath,
    $notes.Trim() + [Environment]::NewLine,
    [Text.UTF8Encoding]::new($false))
Write-Host "Release notes created: $destinationFullPath"
