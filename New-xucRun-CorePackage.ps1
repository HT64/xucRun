# Purpose: Creates an unsigned xucRun Core package from public build outputs.
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',

    [string]$DestinationDirectory
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($DestinationDirectory)) {
    $DestinationDirectory = Join-Path $repositoryRoot "artifacts\CorePackage\$Configuration"
}
$destination = [IO.Path]::GetFullPath($DestinationDirectory)
$sourceFiles = [ordered]@{
    'xucRun.exe' = "xucRun\x64\$Configuration\xucRun.exe"
    'xucRunBroker.exe' = "xucRunBroker\x64\$Configuration\xucRunBroker.exe"
    'xucRunApproval.exe' = "xucRun\x64\$Configuration\xucRunApproval.exe"
    'Install-xucRun-Core.ps1' = 'Install-xucRun-Core.ps1'
    'UserCodeSigning\Sign-xucRunCorePackage.ps1' =
        'UserCodeSigning\Sign-xucRunCorePackage.ps1'
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
        -not $_.PSIsContainer -and
        $_.FullName.Substring($destination.Length).TrimStart('\') -notin
            $allowedExistingFiles
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
Write-Host "Unsigned xucRun Core package created: $destination"
Write-Host 'Sign it before installation with UserCodeSigning\Sign-xucRunCorePackage.ps1.'
