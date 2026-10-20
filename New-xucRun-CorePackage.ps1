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
    "xucRun-Core-Installer-$installerProductVersion-x64.exe"
$sourceFiles = [ordered]@{}
$sourceFiles[$publishedInstallerName] = $installerSource
$sourceFiles += [ordered]@{
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
        $relativePath =
            $_.FullName.Substring($destination.Length).TrimStart('\')
        -not $_.PSIsContainer -and
        $relativePath -notin $allowedExistingFiles -and
        $relativePath -ne 'Installer.exe' -and
        $relativePath -notmatch
            '^xucRun-Core-Installer-\d+\.\d+\.\d+\.\d+-x64\.exe$'
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
                '^xucRun-Core-Installer-\d+\.\d+\.\d+\.\d+-x64\.exe$'
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
Write-Host "Unsigned xucRun Core package created: $destination"
Write-Host "Graphical installer: $publishedInstallerName"
Write-Host 'Sign it before installation with UserCodeSigning\Sign-xucRunCorePackage.ps1.'
