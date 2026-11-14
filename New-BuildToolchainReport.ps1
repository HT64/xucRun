<#
.SYNOPSIS
Creates a durable report of the toolchain resolved for one C++ project.

.DESCRIPTION
Locates MSBuild, asks it for the evaluated C++ toolchain and search paths of
the specified project, validates the selected Visual Studio installation,
compiler, and Windows SDK, and writes their exact versions together with the
source commit and build configuration to a UTF-8 text file.

.PARAMETER ProjectPath
Path to a C++ project whose evaluated build properties identify the toolchain.
The project must exist and support the requested configuration and platform.

.PARAMETER ProductName
Product label written to the report.

.PARAMETER DestinationPath
Output file path. Its parent directory is created when necessary, and an
existing file is replaced.

.PARAMETER Configuration
Build configuration evaluated by MSBuild. The default is Release.

.PARAMETER Platform
Build platform evaluated by MSBuild. The default is x64.

.PARAMETER MSBuildPath
Optional path to MSBuild.exe. When omitted, MSBuild.exe is resolved from PATH.

.EXAMPLE
.\New-BuildToolchainReport.ps1 `
    -ProjectPath .\xucRun\XUCRun.vcxproj `
    -ProductName xucRun `
    -DestinationPath .\artifacts\Release\0.9.1.8\BUILD-TOOLCHAIN.txt

Evaluates the Release x64 toolchain and writes the release report.

.NOTES
Requires Windows, Git, vswhere, and an MSBuild version that supports
-getProperty JSON output. The script reads installed tool and repository
metadata and writes only DestinationPath. It does not build, sign, install, or
publish software. The selected MSBuild must belong to the same Visual Studio
installation as the resolved C++ tools. Missing or ambiguous versions,
unexpected paths, Git failures, MSBuild failures, and output failures are
terminating errors. A powershell.exe -File invocation returns 0 on success and
nonzero on an unhandled error.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$ProjectPath,

    [Parameter(Mandatory)]
    [string]$ProductName,

    [Parameter(Mandatory)]
    [string]$DestinationPath,

    [string]$Configuration = 'Release',

    [string]$Platform = 'x64',

    [string]$MSBuildPath
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = $PSScriptRoot

function Get-RequiredProperty {
    param(
        [Parameter(Mandatory)]$Properties,
        [Parameter(Mandatory)][string]$Name
    )

    $value = $Properties.$Name
    if ([string]::IsNullOrWhiteSpace($value)) {
        throw "MSBuild did not resolve required property '$Name'."
    }
    return $value
}

$projectFullPath = [IO.Path]::GetFullPath($ProjectPath)
if (-not (Test-Path -LiteralPath $projectFullPath -PathType Leaf)) {
    throw "C++ project was not found: $projectFullPath"
}

if ([string]::IsNullOrWhiteSpace($MSBuildPath)) {
    $msbuildCommand = Get-Command MSBuild.exe -CommandType Application `
        -ErrorAction Stop
    $MSBuildPath = $msbuildCommand.Source
}
$msbuildFullPath = [IO.Path]::GetFullPath($MSBuildPath)
if (-not (Test-Path -LiteralPath $msbuildFullPath -PathType Leaf)) {
    throw "MSBuild was not found: $msbuildFullPath"
}

$propertyNames = @(
    'ExecutablePath',
    'PlatformToolset',
    'VCToolsInstallDir',
    'VCToolsVersion',
    'WindowsSDK_LibraryPath'
)
$propertyOutput = & $msbuildFullPath $projectFullPath `
    -nologo `
    "-property:Configuration=$Configuration" `
    "-property:Platform=$Platform" `
    "-getProperty:$($propertyNames -join ',')" 2>&1
if ($LASTEXITCODE -ne 0) {
    throw "MSBuild property evaluation failed with exit code $LASTEXITCODE.`n" +
        ($propertyOutput | Out-String)
}
$propertyText = ($propertyOutput | Out-String).Trim()
$jsonStart = $propertyText.IndexOf('{')
$jsonEnd = $propertyText.LastIndexOf('}')
if ($jsonStart -lt 0 -or $jsonEnd -le $jsonStart) {
    throw "MSBuild property evaluation did not return JSON.`n$propertyText"
}
$evaluation = $propertyText.Substring(
    $jsonStart, $jsonEnd - $jsonStart + 1) | ConvertFrom-Json
$properties = $evaluation.Properties
if (-not $properties) {
    throw 'MSBuild property evaluation did not return a Properties object.'
}

$vcToolsVersion = Get-RequiredProperty -Properties $properties `
    -Name 'VCToolsVersion'
$vcToolsDirectory = [IO.Path]::GetFullPath(
    (Get-RequiredProperty -Properties $properties -Name 'VCToolsInstallDir')
).TrimEnd('\')
$platformToolset = Get-RequiredProperty -Properties $properties `
    -Name 'PlatformToolset'
$executablePath = Get-RequiredProperty -Properties $properties `
    -Name 'ExecutablePath'
$windowsSdkLibraryPath = Get-RequiredProperty -Properties $properties `
    -Name 'WindowsSDK_LibraryPath'

$visualStudioPath = $vcToolsDirectory
for ($parentIndex = 0; $parentIndex -lt 4; $parentIndex++) {
    $visualStudioPath = Split-Path -Parent $visualStudioPath
}
if ([string]::IsNullOrWhiteSpace($visualStudioPath) -or
    -not (Test-Path -LiteralPath $visualStudioPath -PathType Container)) {
    throw "Could not derive the Visual Studio path from $vcToolsDirectory."
}
if (-not $msbuildFullPath.StartsWith(
        $visualStudioPath + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw "MSBuild and the C++ toolset belong to different installations: " +
        "$msbuildFullPath; $vcToolsDirectory"
}

$vswherePath =
    "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path -LiteralPath $vswherePath -PathType Leaf)) {
    throw "vswhere was not found: $vswherePath"
}
$visualStudioInstances = @(
    (& $vswherePath -products * -format json -utf8 | ConvertFrom-Json)
)
if ($LASTEXITCODE -ne 0) {
    throw "vswhere failed with exit code $LASTEXITCODE."
}
$visualStudioInstance = @($visualStudioInstances | Where-Object {
    [IO.Path]::GetFullPath($_.installationPath).TrimEnd('\').Equals(
        $visualStudioPath.TrimEnd('\'),
        [StringComparison]::OrdinalIgnoreCase)
})
if ($visualStudioInstance.Count -ne 1) {
    throw "Expected one Visual Studio instance for $visualStudioPath; found " +
        "$($visualStudioInstance.Count)."
}
$visualStudioInstance = $visualStudioInstance[0]

$compilerCandidates = @(
    $executablePath -split ';' |
        Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
        ForEach-Object { Join-Path $_ 'cl.exe' } |
        Where-Object {
            (Test-Path -LiteralPath $_ -PathType Leaf) -and
            [IO.Path]::GetFullPath($_).StartsWith(
                $vcToolsDirectory + '\',
                [StringComparison]::OrdinalIgnoreCase)
        } |
        Select-Object -Unique
)
if ($compilerCandidates.Count -lt 1) {
    throw "No compiler was resolved below $vcToolsDirectory."
}
$compilerPath = $compilerCandidates[0]
$compilerVersion = (Get-Item -LiteralPath $compilerPath).VersionInfo.FileVersion
if ([string]::IsNullOrWhiteSpace($compilerVersion)) {
    throw "The compiler has no product version: $compilerPath"
}

$sdkVersions = @(
    [regex]::Matches(
        $windowsSdkLibraryPath,
        '(?i)\\lib\\(?<Version>\d+\.\d+\.\d+\.\d+)\\') |
        ForEach-Object { $_.Groups['Version'].Value } |
        Select-Object -Unique
)
if ($sdkVersions.Count -ne 1) {
    throw "Expected one resolved Windows SDK version; found " +
        "$($sdkVersions.Count) in '$windowsSdkLibraryPath'."
}

$msbuildVersionOutput = & $msbuildFullPath -nologo -version 2>&1
if ($LASTEXITCODE -ne 0) {
    throw "MSBuild version query failed with exit code $LASTEXITCODE."
}
$msbuildVersions = @($msbuildVersionOutput | ForEach-Object {
    $_.ToString().Trim()
} | Where-Object { $_ -match '^\d+(\.\d+){2,3}$' })
if ($msbuildVersions.Count -ne 1) {
    throw "Expected one MSBuild version; found $($msbuildVersions.Count)."
}

$sourceCommitOutput = & git -C $repositoryRoot rev-parse HEAD 2>&1
if ($LASTEXITCODE -ne 0) {
    throw "Git could not determine the source commit.`n" +
        ($sourceCommitOutput | Out-String)
}
$sourceCommit = ($sourceCommitOutput | Out-String).Trim()
if ($sourceCommit -notmatch '^[0-9a-f]{40}$') {
    throw "Git returned an invalid source commit: $sourceCommit"
}

$runnerImage = if ([string]::IsNullOrWhiteSpace($env:ImageOS)) {
    'local-or-unspecified'
}
else {
    $env:ImageOS
}
$runnerImageVersion = if ([string]::IsNullOrWhiteSpace($env:ImageVersion)) {
    'local-or-unspecified'
}
else {
    $env:ImageVersion
}

$reportLines = @(
    'Build toolchain report'
    'Format-Version: 1'
    "Product: $ProductName"
    "Source-Commit: $sourceCommit"
    "Configuration: $Configuration"
    "Platform: $Platform"
    "Runner-Image: $runnerImage"
    "Runner-Image-Version: $runnerImageVersion"
    "Visual-Studio-Name: $($visualStudioInstance.displayName)"
    "Visual-Studio-Version: $($visualStudioInstance.installationVersion)"
    "MSBuild-Version: $($msbuildVersions[0])"
    "Platform-Toolset: $platformToolset"
    "MSVC-Toolset-Version: $vcToolsVersion"
    "MSVC-Compiler-Version: $compilerVersion"
    "Windows-SDK-Version: $($sdkVersions[0])"
)

$destinationFullPath = [IO.Path]::GetFullPath($DestinationPath)
$destinationDirectory = Split-Path -Parent $destinationFullPath
if (-not (Test-Path -LiteralPath $destinationDirectory)) {
    New-Item -ItemType Directory -Path $destinationDirectory -Force | Out-Null
}
[IO.File]::WriteAllText(
    $destinationFullPath,
    ($reportLines -join [Environment]::NewLine) + [Environment]::NewLine,
    [Text.UTF8Encoding]::new($false))
Write-Host "Build toolchain report created: $destinationFullPath"
