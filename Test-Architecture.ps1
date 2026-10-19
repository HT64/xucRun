# Purpose: Enforces the standalone public Core repository boundary.
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repositoryRoot = $PSScriptRoot

function Assert-PublicArchitecture([bool]$Condition, [string]$Message) {
    if (-not $Condition) {
        throw "Public Core architecture contract failed: $Message"
    }
}

$solutionPath = Join-Path $repositoryRoot 'XUCRun.sln'
Assert-PublicArchitecture (Test-Path -LiteralPath $solutionPath -PathType Leaf) `
    'The public Core solution is missing.'
$solution = [IO.File]::ReadAllText($solutionPath)
$projects = @([regex]::Matches(
    $solution,
    '(?m)^Project\("[^"]+"\) = "([^"]+)", "([^"]+)", "\{[^"]+\}"\r?$'))
$expectedProjects = [ordered]@{
    XUCRun = 'xucRun\XUCRun.vcxproj'
    xucRunCore = 'xucRunCore\xucRunCore.vcxproj'
    xucRunCoreTests = 'xucRunCoreTests\xucRunCoreTests.vcxproj'
    xucRunBroker = 'xucRunBroker\xucRunBroker.vcxproj'
    xucRunApproval = 'xucRunApproval\xucRunApproval.vcxproj'
    xucRunLanguageEnUs = 'xucRunLanguageEnUs\xucRunLanguageEnUs.vcxproj'
    xucRunLanguageDeDe = 'xucRunLanguageDeDe\xucRunLanguageDeDe.vcxproj'
    xucRunClientCommon = 'xucRunClientCommon\xucRunClientCommon.vcxproj'
}
Assert-PublicArchitecture ($projects.Count -eq $expectedProjects.Count) `
    'The public Core solution must contain exactly eight projects.'
foreach ($expected in $expectedProjects.GetEnumerator()) {
    $match = @($projects | Where-Object {
        $_.Groups[1].Value -eq $expected.Key -and
        $_.Groups[2].Value -eq $expected.Value
    })
    Assert-PublicArchitecture ($match.Count -eq 1) `
        "The public Core solution is missing project '$($expected.Key)'."
    Assert-PublicArchitecture (Test-Path -LiteralPath `
        (Join-Path $repositoryRoot $expected.Value) -PathType Leaf) `
        "The public Core project file is missing: $($expected.Value)"
}

Assert-PublicArchitecture ($solution -notmatch '(?i)xucRunTray') `
    'The public Core solution must not reference Tray projects.'
Assert-PublicArchitecture ($solution -notmatch '(?i)xucRunInstaller') `
    'The combined private installer must not be part of the public Core solution.'
Assert-PublicArchitecture ($solution -notmatch '(?i)(Win32|ARM)') `
    'The public Core solution must expose only x64 configurations.'

foreach ($forbiddenDirectory in @(
    'xucRunTray',
    'xucRunTrayResources',
    'xucRunTrayLanguageDeDe',
    'xucRunTrayLanguageEnUs',
    'CodeSigning',
    'Zertifikate'
)) {
    Assert-PublicArchitecture (-not (Test-Path -LiteralPath `
        (Join-Path $repositoryRoot $forbiddenDirectory))) `
        "Forbidden private directory is present: $forbiddenDirectory"
}
foreach ($requiredFile in @(
    '.gitattributes',
    '.gitignore',
    '.github\workflows\core-ci.yml',
    'README.md',
    'LICENSE',
    'NOTICE',
    'CONTRIBUTING.md',
    'SECURITY.md',
    'Install-xucRun-Core.ps1',
    'New-xucRun-CorePackage.ps1',
    'Test-CoreInstaller.ps1',
    'UserCodeSigning\Sign-xucRunCorePackage.ps1'
)) {
    Assert-PublicArchitecture (Test-Path -LiteralPath `
        (Join-Path $repositoryRoot $requiredFile) -PathType Leaf) `
        "Required public repository file is missing: $requiredFile"
}

$ciWorkflow = [IO.File]::ReadAllText(
    (Join-Path $repositoryRoot '.github\workflows\core-ci.yml'))
foreach ($requiredCiContract in @(
    'XUCRun.sln',
    'configuration: Release',
    'xucRunCoreTests.exe',
    'Test-Architecture.ps1',
    'Test-CoreInstaller.ps1',
    'New-xucRun-CorePackage.ps1',
    '0.9.0.1',
    'git status --porcelain=v1 --untracked-files=no'
)) {
    Assert-PublicArchitecture ($ciWorkflow.Contains($requiredCiContract)) `
        "The Core CI workflow must enforce $requiredCiContract."
}
Assert-PublicArchitecture ($ciWorkflow -match
    'actions/checkout@[0-9a-f]{40}') `
    'The Core CI checkout action must be pinned to a commit SHA.'
Assert-PublicArchitecture ($ciWorkflow -match
    'microsoft/setup-msbuild@[0-9a-f]{40}') `
    'The Core CI MSBuild action must be pinned to a commit SHA.'
Assert-PublicArchitecture ($ciWorkflow -match
    'actions/upload-artifact@[0-9a-f]{40}') `
    'The Core CI artifact action must be pinned to a commit SHA.'
Assert-PublicArchitecture ($ciWorkflow -match
    'actions/download-artifact@[0-9a-f]{40}') `
    'The Core CI download action must be pinned to a commit SHA.'

$projectFiles = @(Get-ChildItem -LiteralPath $repositoryRoot -Recurse `
    -Filter '*.vcxproj' -File)
$projectDefinitions = ($projectFiles | ForEach-Object {
    [IO.File]::ReadAllText($_.FullName)
}) -join "`n"
Assert-PublicArchitecture ($projectDefinitions -notmatch
    '(?i)Sign-Executable|Sign-xucRunPackage|Install-xucRun-Icon|Install-xucRun\.ps1') `
    'Public Core projects must not depend on private signing or installer files.'
Assert-PublicArchitecture ($projectDefinitions -notmatch
    '(?i)Increment-BuildNumber|<PreBuildEvent') `
    'Public Core builds must not modify tracked version files.'

$gitIgnore = [IO.File]::ReadAllText(
    (Join-Path $repositoryRoot '.gitignore'))
foreach ($requiredIgnore in @('.vs/', 'x64/', 'artifacts/')) {
    Assert-PublicArchitecture ($gitIgnore.Contains($requiredIgnore)) `
        "The public Core .gitignore must exclude $requiredIgnore."
}
$gitAttributes = [IO.File]::ReadAllText(
    (Join-Path $repositoryRoot '.gitattributes'))
Assert-PublicArchitecture ($gitAttributes -match '(?m)^\* text=auto\r?$') `
    'The public Core .gitattributes must enable text normalization.'
$version = [IO.File]::ReadAllText(
    (Join-Path $repositoryRoot 'xucRunSharedResources\version.txt')).Trim()
$build = [IO.File]::ReadAllText(
    (Join-Path $repositoryRoot 'xucRunSharedResources\buildcounter.txt')).Trim()
$versionHeader = [IO.File]::ReadAllText(
    (Join-Path $repositoryRoot 'xucRunSharedResources\BuildVersion.h'))
Assert-PublicArchitecture (
    $version -eq '0.9.0' -and
    $build -eq '1' -and
    $versionHeader -match '#define VERSION_STRING "0\.9\.0\.1"') `
    'The public Core version files must consistently identify version 0.9.0.1.'

$installer = [IO.File]::ReadAllText(
    (Join-Path $repositoryRoot 'Install-xucRun-Core.ps1'))
$packageBuilder = [IO.File]::ReadAllText(
    (Join-Path $repositoryRoot 'New-xucRun-CorePackage.ps1'))
$packageSigner = [IO.File]::ReadAllText(
    (Join-Path $repositoryRoot 'UserCodeSigning\Sign-xucRunCorePackage.ps1'))
foreach ($publicInstallerSource in @($installer, $packageBuilder, $packageSigner)) {
    Assert-PublicArchitecture ($publicInstallerSource -notmatch
        '(?i)Sign-Executable|(?:^|[\\/])CodeSigning[\\/]|Zertifikate[\\/]') `
        'The public Core installer must not depend on private signing paths.'
}
foreach ($publicPackageSource in @($packageBuilder, $packageSigner)) {
    Assert-PublicArchitecture ($publicPackageSource -notmatch
        '(?i)Install-xucRun-Icon|xucRunTray\.(exe|dll)') `
        'The public Core package must not include Tray components.'
}
Assert-PublicArchitecture ($installer -match 'SensitiveClientSignerSha256') `
    'The Core installer must pin the authorized client signer.'
Assert-PublicArchitecture ($installer -match '(?i)uninstall-purge') `
    'The Core uninstaller must request broker data purging.'
Assert-PublicArchitecture ($packageBuilder -match
    'UserCodeSigning\\Sign-xucRunCorePackage\.ps1') `
    'The Core package must include the local signing workflow.'

foreach ($script in @(
    'Install-xucRun-Core.ps1',
    'New-xucRun-CorePackage.ps1',
    'Test-CoreInstaller.ps1',
    'UserCodeSigning\Sign-xucRunCorePackage.ps1'
)) {
    $tokens = $null
    $errors = $null
    [void][Management.Automation.Language.Parser]::ParseFile(
        (Join-Path $repositoryRoot $script),
        [ref]$tokens,
        [ref]$errors)
    Assert-PublicArchitecture ($errors.Count -eq 0) `
        "Public installer script has parse errors: $script"
}

$forbiddenFiles = @(Get-ChildItem -LiteralPath $repositoryRoot -Recurse -File |
    Where-Object {
        $_.Name -in @('code-signing-cert.env', 'copy-target-directories.env') -or
        $_.Extension -in @('.pfx', '.p12', '.key', '.env')
    })
Assert-PublicArchitecture ($forbiddenFiles.Count -eq 0) `
    'The public Core repository contains a forbidden secret-bearing file type.'

Write-Host 'xucRun public Core architecture contracts passed'
