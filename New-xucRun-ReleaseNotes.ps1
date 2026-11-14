<#
.SYNOPSIS
Creates bilingual release notes for one xucRun version.

.DESCRIPTION
Extracts the requested version section from the English and German changelogs,
validates that the supplied checksum file contains the matching installer and
ZIP entries, and writes UTF-8 release notes containing the open-source status,
signing, pre-installation README, version designation, build provenance, and
Tray update notices, both changelog sections, links, and checksums. GitHub
renders every line break in a release description as a visible line break, so
the hard-wrapped lines of each changelog paragraph and list item are joined
into one line. Headings, list item starts, table rows, blank lines, fenced code
blocks, and explicit Markdown line breaks are kept.

.PARAMETER Version
Specifies the release version. This mandatory value must contain exactly four
dot-separated numeric components.

.PARAMETER ChecksumPath
Specifies the mandatory SHA256SUMS.txt input. It must contain uppercase
64-character SHA-256 hashes for the versioned x64 installer and ZIP archive in
the format produced by New-xucRun-Release.ps1.

.PARAMETER DestinationPath
Specifies the mandatory output file. Its parent directory is created when
necessary, and an existing file is replaced.

.EXAMPLE
.\New-xucRun-ReleaseNotes.ps1 -Version 0.9.1.8 -ChecksumPath .\artifacts\Release\0.9.1.8\SHA256SUMS.txt -DestinationPath .\artifacts\Release\0.9.1.8\RELEASE_NOTES.md

Creates release notes for version 0.9.1.8 from the matching checksums and
changelog sections.

.NOTES
The repository must contain matching nonempty sections in CHANGELOG.md and
CHANGELOG.de.md. The script only writes the requested local output; it does not
create or update a GitHub Release. It validates checksum syntax and asset names
but does not independently hash either asset or establish the checksum file's
origin. The output file can be overwritten. Missing or malformed checksums,
missing or empty changelog sections, invalid paths, and write failures are
terminating errors. A powershell.exe -File invocation returns 0 on success and
nonzero on an unhandled error.
#>
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

function ConvertTo-UnwrappedMarkdown {
    param([Parameter(Mandatory)][string]$Text)

    $result = New-Object 'System.Collections.Generic.List[string]'
    $inFence = $false
    $canJoin = $false
    foreach ($line in ($Text -split '\r?\n')) {
        $trimmed = $line.Trim()
        if ($trimmed -match '^(```|~~~)') {
            $inFence = -not $inFence
            $result.Add($line)
            $canJoin = $false
            continue
        }
        if ($inFence) {
            $result.Add($line)
            continue
        }
        if ($trimmed.Length -eq 0) {
            $result.Add('')
            $canJoin = $false
            continue
        }
        $startsBlock = $trimmed -match '^(#{1,6}\s|[-*+]\s|\d+[.)]\s|>|\|)'
        if ($canJoin -and -not $startsBlock) {
            $result[$result.Count - 1] += ' ' + $trimmed
        }
        else {
            $result.Add($line.TrimEnd())
        }
        # A heading or table row ends its block; two trailing spaces or a
        # backslash are explicit Markdown line breaks.
        $canJoin = $trimmed -notmatch '^(#{1,6}\s|\|)' -and
            $line -notmatch '(\s{2}|\\)$'
    }
    return ($result -join [Environment]::NewLine)
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

$englishChanges = ConvertTo-UnwrappedMarkdown -Text (Get-ChangelogVersionBody `
    -Path (Join-Path $repositoryRoot 'CHANGELOG.md') `
    -ReleaseVersion $Version)
$germanChanges = ConvertTo-UnwrappedMarkdown -Text (Get-ChangelogVersionBody `
    -Path (Join-Path $repositoryRoot 'CHANGELOG.de.md') `
    -ReleaseVersion $Version)

$notes = @"
## Important notice / Wichtiger Hinweis

xucRun is an open-source project. The provided installer is not signed by a publicly trusted publisher. Windows may therefore report an unknown publisher when it is first started. Download the files only from this GitHub Release and compare the SHA-256 checksum before starting the installer.

xucRun ist ein Open-Source-Projekt. Die bereitgestellte Installer-Datei besitzt keine Signatur eines öffentlich vertrauenswürdigen Herausgebers. Windows kann deshalb beim ersten Start einen unbekannten Herausgeber melden. Laden Sie die Dateien ausschließlich aus diesem GitHub Release herunter und vergleichen Sie vor dem Start die SHA-256-Prüfsumme.

## Before installing / Vor der Installation

xucRun is a powerful tool. Using it involves risks that the user must understand. Before installing, therefore, be sure to read the [README](https://github.com/HT64/xucRun/blob/v$Version/README.md), in particular its [security notices](https://github.com/HT64/xucRun/blob/v$Version/README.md#requirements-and-intended-use).

xucRun ist ein mächtiges Werkzeug. Seine Nutzung birgt Risiken, die der Anwender verstanden haben muss. Lesen Sie deshalb vor der Installation unbedingt die [README](https://github.com/HT64/xucRun/blob/v$Version/README.de.md), insbesondere die [Sicherheitshinweise](https://github.com/HT64/xucRun/blob/v$Version/README.de.md#voraussetzungen-und-einsatzbereich).

## Version designation / Versionskennzeichnung

The 0.x.x.x version designation is not intended as a quality rating. It will be retained until more practical experience and feedback from other user environments are available.

Die Versionskennzeichnung 0.x.x.x dient nicht als Qualitätsbewertung. Sie wird beibehalten, bis mehr praktische Erfahrungen und Rückmeldungen aus anderen Benutzerumgebungen vorliegen.

## Build provenance / Build-Herkunft

The official release files were built and verified automatically from the tagged source on a fresh GitHub Actions runner. `BUILD-TOOLCHAIN.txt` records the build tools actually used. GitHub build provenance attestations are also available for the files.

Die offiziellen Release-Dateien wurden automatisiert aus dem getaggten Quellstand auf einem frischen GitHub-Actions-Runner erstellt und geprüft. `BUILD-TOOLCHAIN.txt` dokumentiert die tatsächlich verwendeten Buildwerkzeuge. Für die Dateien steht außerdem ein GitHub-Herkunftsnachweis zur Verfügung.

## Update with xucRun Tray / Aktualisierung mit xucRun Tray

If xucRun Tray is installed, uninstall it before updating xucRun. After the Core update, install the xucRun Tray release with exactly the same four-part version.

Wenn xucRun Tray installiert ist, deinstallieren Sie es vor der Aktualisierung von xucRun. Installieren Sie nach dem Core-Update die xucRun-Tray-Veröffentlichung mit exakt derselben vierteiligen Version.

## Release information

$englishChanges

Full [changelog](https://github.com/HT64/xucRun/blob/v$Version/CHANGELOG.md)

## Veröffentlichungsinformationen

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
