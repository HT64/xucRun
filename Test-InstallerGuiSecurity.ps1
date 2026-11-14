<#
.SYNOPSIS
Validates repository file documentation and installer runtime staging.

.DESCRIPTION
Enumerates every tracked or unignored repository file, verifies exact
duplicate-free FILE_REGISTER.md coverage, and validates structured Purpose and
Approach metadata for each supported commentable format except the two
explicitly path-bound changelog exceptions. For PowerShell it preserves the
stronger comment-based-help contract and checks that documented parameters
match declared parameters. It then parses Installer-Gui.ps1 and checks that a
user-selected Visual C++ redistributable is copied into protected staging,
validated there, and only then executed. It checks that installation remains
disabled until license and security information are acknowledged, and during
installation or when the certificate is ambiguous. Finally it checks that the machine
installer remains authoritative for Tray compatibility during both graphical and manual
installation, while the graphical installer disables installation for an incompatible Tray
before runtime installation or package signing. Finally it checks that the machine
installer embeds the Apache license and notice without additional selling
restrictions, that the translated license-tab introduction matches the
license declaration in each README and fits
above the full license text, and that the GUI connects the
acknowledgment control to the installation gate. It also verifies that both
localized checkbox labels distinguish agreement to the license terms from
confirming that the security information was read and understood. The displayed security and
status notices are compared with both README languages, and GUI text formatting
is checked with non-default Windows paths and a runtime version. It checks that the machine
Core /info resources identify the project as open source under Apache License
2.0 in both languages. It also checks that the machine
installer and the package signer create their staging directories together
with the restricting access control list, avoid environment-derived and
temporary staging paths, and keep their verification steps in order, that
neither the graphical installer nor the C++ sources locate product or system
directories through environment variables, and that the broker decodes its
diagnostic pipe contracts only in Debug builds.
It also tests Core legal-file destinations and migration in an isolated
temporary directory.

.EXAMPLE
.\Test-InstallerGuiSecurity.ps1

Runs the repository file-documentation and installer staging contract
checks.

.NOTES
Requires Windows PowerShell, Windows Forms, and Git in a working tree of this repository. Commentable
formats without a structural validator and non-PowerShell script extensions
fail explicitly. Generated BuildVersion.h metadata is also checked in its generator. The
existing UTF-16 registry export is treated as a byte-stable format exception
because Git handles it as binary. Raster images, icons, and MPEG-4
documentation videos are validated through FILE_REGISTER.md because their
formats cannot carry the structured metadata head; SVG graphics carry it as an
XML comment. CHANGELOG.md and CHANGELOG.de.md are the
only commentable files exempt from structured metadata heads. The test creates
and removes an isolated temporary directory for legal-file migration; it
performs no installation, signing, network, service, registry, or certificate
operation. Documentation, parser, parameter, and
staging-contract violations are terminating errors and yield a nonzero host
exit code. Success returns exit code 0.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

function Get-RepositoryVisibleFiles {
    $files = @(& git -C $PSScriptRoot ls-files --cached --others `
        --exclude-standard)
    if ($LASTEXITCODE -ne 0) {
        throw "Git file enumeration failed with exit code $LASTEXITCODE."
    }
    if ($files.Count -eq 0) {
        throw 'No tracked or unignored repository files were found.'
    }
    return @($files | Sort-Object -CaseSensitive)
}

function Assert-FileRegister {
    param([string[]]$RepositoryFiles)

    $registerPath = Join-Path $PSScriptRoot 'FILE_REGISTER.md'
    if (-not (Test-Path -LiteralPath $registerPath -PathType Leaf)) {
        throw 'FILE_REGISTER.md is missing.'
    }

    $entries = @(
        foreach ($line in (Get-Content -LiteralPath $registerPath)) {
            if ($line -match
                '^\| `(?<Path>[^`]+)` \| (?<Class>[^|]*) \| (?<Purpose>[^|]*) \|$') {
                [pscustomobject]@{
                    Path = $Matches.Path
                    Class = $Matches.Class.Trim()
                    Purpose = $Matches.Purpose.Trim()
                }
            }
        }
    )
    if ($entries.Count -eq 0) {
        throw 'FILE_REGISTER.md contains no file entries.'
    }

    $duplicates = @($entries | Group-Object Path | Where-Object Count -gt 1)
    if ($duplicates.Count -ne 0) {
        throw 'FILE_REGISTER.md contains duplicate paths: ' +
            (($duplicates.Name | Sort-Object) -join ', ')
    }
    $emptyEntries = @($entries | Where-Object {
        [string]::IsNullOrWhiteSpace($_.Class) -or
        [string]::IsNullOrWhiteSpace($_.Purpose)
    })
    if ($emptyEntries.Count -ne 0) {
        throw 'FILE_REGISTER.md contains empty class or purpose values: ' +
            (($emptyEntries.Path | Sort-Object) -join ', ')
    }

    $registeredPaths = @($entries.Path)
    $missing = @($RepositoryFiles | Where-Object {
        $_ -notin $registeredPaths
    })
    $extra = @($registeredPaths | Where-Object {
        $_ -notin $RepositoryFiles
    })
    if ($missing.Count -ne 0 -or $extra.Count -ne 0) {
        throw 'FILE_REGISTER.md coverage differs from Git-visible files. ' +
            "Missing: $($missing -join ', '); extra: $($extra -join ', ')."
    }

    $sortedPaths = @($registeredPaths | Sort-Object -CaseSensitive)
    if (($registeredPaths -join "`n") -cne ($sortedPaths -join "`n")) {
        throw 'FILE_REGISTER.md entries must use case-sensitive path order.'
    }
}

function Assert-StructuredFileHeaders {
    param([string[]]$RepositoryFiles)

    $nonCommentableExtensions = @(
        '.ico', '.jpg', '.mp4', '.png', '.sln', '.txt'
    )
    $nonCommentableNames = @('LICENSE', 'NOTICE')
    $headerExceptionPaths = @('CHANGELOG.de.md', 'CHANGELOG.md')
    foreach ($relativePath in $RepositoryFiles) {
        $extension = [IO.Path]::GetExtension($relativePath).ToLowerInvariant()
        $fileName = [IO.Path]::GetFileName($relativePath)
        $path = Join-Path $PSScriptRoot (
            $relativePath.Replace('/', [IO.Path]::DirectorySeparatorChar))
        if ($relativePath -in $headerExceptionPaths) {
            continue
        }
        if ($extension -eq '.ps1') {
            continue
        }
        if ($extension -eq '.reg') {
            $registryBytes = [IO.File]::ReadAllBytes($path)
            if ($registryBytes.Length -lt 2 -or
                $registryBytes[0] -ne 0xFF -or
                $registryBytes[1] -ne 0xFE) {
                throw "Registry format exception must remain UTF-16 LE: " +
                    $relativePath
            }
            continue
        }
        if ($extension -in $nonCommentableExtensions -or
            $fileName -in $nonCommentableNames) {
            continue
        }

        $text = [IO.File]::ReadAllText($path)
        $pattern = switch ($extension) {
            { $_ -in @('.cpp', '.h', '.inl', '.rc') } {
                '\A// Purpose: (?<Purpose>[^\r\n]+)\r?\n' +
                    '// Approach: (?<Approach>[^\r\n]+)'
                break
            }
            '.md' {
                '\A<!--\r?\nPurpose: (?<Purpose>[^\r\n]+)\r?\n' +
                    'Approach: (?<Approach>[^\r\n]+)\r?\n-->'
                break
            }
            { $_ -in @('.gitattributes', '.gitignore', '.yml') } {
                '\A# Purpose: (?<Purpose>[^\r\n]+)\r?\n' +
                    '# Approach: (?<Approach>[^\r\n]+)'
                break
            }
            { $_ -in @('.props', '.svg', '.targets', '.vcxproj') } {
                '\A(?:<\?xml[^\r\n]+\?>\r?\n)?' +
                    '<!--\r?\n' +
                    'Purpose: (?<Purpose>[^\r\n]+)\r?\n' +
                    'Approach: (?<Approach>[^\r\n]+)\r?\n' +
                    '-->'
                break
            }
            default {
                throw "Text or unknown file type has no structural header " +
                    "validator: $relativePath"
            }
        }

        $match = [regex]::Match($text, $pattern)
        if (-not $match.Success -or
            [string]::IsNullOrWhiteSpace($match.Groups['Purpose'].Value) -or
            [string]::IsNullOrWhiteSpace($match.Groups['Approach'].Value)) {
            throw "File head lacks integrated Purpose and Approach metadata: " +
                $relativePath
        }
    }

    $generatorPath = Join-Path $PSScriptRoot `
        'xucRunSharedResources\Increment-BuildNumber.ps1'
    $generatorText = [IO.File]::ReadAllText($generatorPath)
    $generatedHeaderLines = @(
        '// Purpose: Provides the auto-generated product version macros shared by all build targets.',
        '// Approach: Generated by Increment-BuildNumber.ps1 from version.txt and the persisted build counter; do not edit manually.'
    )
    foreach ($line in $generatedHeaderLines) {
        if (-not $generatorText.Contains($line)) {
            throw "BuildVersion.h generator omits metadata: $line"
        }
    }
}

function Assert-ScriptHeaderDocumentation {
    $trackedScripts = @(& git -C $PSScriptRoot ls-files --cached --others `
        --exclude-standard -- `
        '*.ps1' '*.bat' '*.cmd' '*.py')
    if ($LASTEXITCODE -ne 0) {
        throw "git ls-files failed with exit code $LASTEXITCODE."
    }
    if ($trackedScripts.Count -eq 0) {
        throw 'No tracked scripts were found.'
    }

    foreach ($relativePath in $trackedScripts) {
        $extension = [IO.Path]::GetExtension($relativePath)
        if ($extension -ine '.ps1') {
            throw "Tracked script type '$extension' has no structural " +
                "documentation validator: $relativePath"
        }

        $scriptPath = Join-Path $PSScriptRoot (
            $relativePath.Replace('/', [IO.Path]::DirectorySeparatorChar))
        $scriptTokens = $null
        $scriptErrors = $null
        $scriptAst = [Management.Automation.Language.Parser]::ParseFile(
            $scriptPath, [ref]$scriptTokens, [ref]$scriptErrors)
        if ($scriptErrors.Count -ne 0) {
            throw "PowerShell parser error in '$relativePath': " +
                ($scriptErrors.Message -join '; ')
        }

        $help = $scriptAst.GetHelpContent()
        if (-not $help) {
            throw "Script has no PowerShell comment-based help: $relativePath"
        }
        if ([string]::IsNullOrWhiteSpace($help.Synopsis) -or
            [string]::IsNullOrWhiteSpace($help.Description) -or
            [string]::IsNullOrWhiteSpace($help.Notes) -or
            $help.Examples.Count -lt 1) {
            throw "Script help is incomplete: $relativePath"
        }

        $declaredParameters = @(
            if ($scriptAst.ParamBlock) {
                $scriptAst.ParamBlock.Parameters |
                    ForEach-Object { $_.Name.VariablePath.UserPath }
            }
        )
        $documentedParameters = @($help.Parameters.Keys)
        $missingParameters = @($declaredParameters | Where-Object {
            $_ -notin $documentedParameters
        })
        $extraParameters = @($documentedParameters | Where-Object {
            $_ -notin $declaredParameters
        })
        $emptyParameters = @($documentedParameters | Where-Object {
            [string]::IsNullOrWhiteSpace($help.Parameters[$_])
        })
        if ($missingParameters.Count -ne 0 -or
            $extraParameters.Count -ne 0 -or
            $emptyParameters.Count -ne 0) {
            throw "Script help parameters do not match '$relativePath'. " +
                "Missing: $($missingParameters -join ', '); " +
                "extra: $($extraParameters -join ', '); " +
                "empty: $($emptyParameters -join ', ')."
        }
    }
}

$repositoryFiles = @(Get-RepositoryVisibleFiles)
Assert-FileRegister -RepositoryFiles $repositoryFiles
Assert-StructuredFileHeaders -RepositoryFiles $repositoryFiles
Assert-ScriptHeaderDocumentation

$path = Join-Path $PSScriptRoot 'xucRunInstaller\Installer-Gui.ps1'
$tokens = $null
$errors = $null
$guiSource = [IO.File]::ReadAllText($path)
$ast = [Management.Automation.Language.Parser]::ParseInput(
    $guiSource, [ref]$tokens, [ref]$errors)
if ($errors.Count -ne 0) {
    throw "PowerShell parser error: $($errors.Message -join '; ')"
}
foreach ($contract in @(
    '$script:language = Get-InitialInstallerLanguage',
    '$sessionUserSid\Control Panel\Desktop',
    '$sessionUserSid\Control Panel\International\User Profile',
    'PreferredUILanguages',
    '$acknowledgment.Checked = $false',
    '$acknowledgment.Add_CheckedChanged({ Update-InstallAvailability })',
    '$script:installInProgress = $true',
    '$script:trayCompatibilityState = Get-TrayCompatibilityState',
    '$script:trayCompatibilityState.Kind -ne ''Incompatible''',
    '$licenseDetails.Text = (($licenseText',
    '$securityTab.Controls.Add($securityDetails)',
    '$licenseTab.Controls.Add($licenseIntroduction)',
    '$licenseTab.Controls.Add($licenseDetails)',
    '$licenseIntroduction.Text = Get-Text -Key LicenseIntroduction',
    '$readmeLink.Add_LinkClicked({'
)) {
    if (-not $guiSource.Contains($contract)) {
        throw "License and security acknowledgment contract is missing: $contract"
    }
}
$textsAssignment = $ast.Find({
    param($node)
    $node -is [Management.Automation.Language.AssignmentStatementAst] -and
    $node.Left.Extent.Text -eq '$texts'
}, $true)
if (-not $textsAssignment) {
    throw 'Installer translations were not found.'
}
. ([scriptblock]::Create($textsAssignment.Extent.Text))
$expectedAcknowledgments = @{
    de = 'Ich stimme den Lizenzbedingungen zu und habe die Sicherheitshinweise verstanden.'
    en = 'I agree to the license terms and understand the security information.'
}
foreach ($language in @('de', 'en')) {
    if ($texts[$language].Acknowledgment -cne $expectedAcknowledgments[$language]) {
        throw "The $language installer acknowledgment label is incorrect."
    }
}
$getTextFunction = $ast.Find({
    param($node)
    $node -is [Management.Automation.Language.FunctionDefinitionAst] -and
    $node.Name -eq 'Get-Text'
}, $true)
if (-not $getTextFunction) {
    throw 'Installer translation formatter was not found.'
}
. ([scriptblock]::Create($getTextFunction.Extent.Text))
foreach ($language in @('de', 'en')) {
    $readmeName = if ($language -eq 'de') { 'README.de.md' } else { 'README.md' }
    $heading = if ($language -eq 'de') {
        'Wichtige Sicherheitshinweise'
    } else {
        'Important security notices'
    }
    $completenessPattern = if ($language -eq 'de') {
        'Diese Sicherheitshinweise erheben keinen Anspruch auf Vollst\u00e4ndigkeit; nicht alle Risiken und Sicherheitsl\u00fccken k\u00f6nnen vorausgesehen oder ausgeschlossen werden\.'
    } else {
        'These security notices are not exhaustive; not all risks and security vulnerabilities can be anticipated or ruled out\.'
    }
    if ($texts[$language].SecurityNotice -notmatch $completenessPattern) {
        throw "Core installer completeness notice is missing for '$language'."
    }
    $statusPattern = if ($language -eq 'de') {
        'Die Dokumentation beschreibt das vorgesehene Verhalten\. Der aktuelle Stand kann Fehler enthalten, davon abweichen und sich \u00e4ndern\.'
    } else {
        'The documentation describes the intended behavior\. The current state may contain defects, differ from that behavior, and change\.'
    }
    if ($texts[$language].StatusNotice -notmatch $statusPattern) {
        throw "Core installer documentation status notice is missing for '$language'."
    }
    $readme = [IO.File]::ReadAllText((Join-Path $PSScriptRoot $readmeName))
    $changelogName = if ($language -eq 'de') {
        'CHANGELOG.de.md'
    } else {
        'CHANGELOG.md'
    }
    $changelog = [IO.File]::ReadAllText((Join-Path $PSScriptRoot $changelogName))
    $versionStatus = if ($language -eq 'de') {
        'Die Versionskennzeichnung `0.x.x.x` ist keine Qualit'
    } else {
        'The `0.x.x.x` version designation is not a quality'
    }
    if (-not $changelog.Contains($versionStatus)) {
        throw "$changelogName is missing the 0.x.x.x version status explanation."
    }
    $licenseHeading = if ($language -eq 'de') { 'Lizenz' } else { 'License' }
    $licenseSection = [regex]::Match($readme,
        '(?ms)^## ' + $licenseHeading + '\s*\r?\n(?<body>.*?)(?=^## |\z)')
    if (-not $licenseSection.Success -or
        $texts[$language].LicenseIntroduction -notmatch 'Apache License 2\.0' -or
        $licenseSection.Groups['body'].Value -notmatch 'Apache License 2\.0') {
        throw "Installer license introduction differs from $readmeName."
    }
    foreach ($entry in @(
        @{ Heading = $heading; Text = $texts[$language].SecurityNotice; Exact = $true }
    )) {
        $pattern = '(?ms)^> \*\*' + [regex]::Escape($entry.Heading) +
            ':\*\* (?<body>.*?)(?:\r?\n\r?\n|\z)'
        $match = [regex]::Match($readme, $pattern)
        if (-not $match.Success) {
            throw "$readmeName is missing the $($entry.Heading) block."
        }
        $expected = $match.Groups['body'].Value -replace '(?m)^> ?', '' `
            -replace '\[([^\]]+)\]\([^)]+\)', '$1' -replace '\s+', ' '
        $actual = $entry.Text -replace '\s+', ' '
        $expected = $expected.Trim()
        $actual = $actual.Trim()
        if (($entry.Exact -and $expected -cne $actual) -or
            (-not $entry.Exact -and -not $expected.StartsWith($actual))) {
            throw "Installer $($entry.Heading) text differs from $readmeName."
        }
        $script:language = $language
        $customData = 'R:\Custom Data'
        $customInstall = 'S:\Custom Programs\xucRun'
        $customLogs = 'R:\Custom Data\xucRun\InstallerLogs'
        $detailsText = Get-Text -Key Details -Arguments @(
            $customData, $customInstall, $customLogs, '99.2.3.4')
        foreach ($expected in @(
            $customData, $customInstall, $customLogs, '99.2.3.4', '-NoSuccessDialog'
        )) {
            if (-not $detailsText.Contains($expected)) {
                throw "Installer details for '$language' omit '$expected'."
            }
        }
        if ($detailsText -match '\{\d+\}' -or
            -not (Get-Text -Key Description -Arguments @($customInstall)).
                Contains($customInstall) -or
            -not (Get-Text -Key PathCheck -Arguments @($customInstall)).
                Contains($customInstall)) {
            throw "Installer paths are not formatted correctly for '$language'."
        }
        $readmeFile = if ($language -eq 'de') { 'README.de.md' } else { 'README.md' }
        if ($texts[$language].ReadmeUrl -cne
            "https://github.com/HT64/xucRun/blob/main/$readmeFile") {
            throw "Installer documentation link is incorrect for '$language'."
        }
    }
}
$getInstallNoticeFunction = $ast.Find({
    param($node)
    $node -is [Management.Automation.Language.FunctionDefinitionAst] -and
    $node.Name -eq 'Get-InstallNoticeText'
}, $true)
if (-not $getInstallNoticeFunction) {
    throw 'Installer operation notice formatter was not found.'
}
. ([scriptblock]::Create($getInstallNoticeFunction.Extent.Text))
$script:installedProductState = [pscustomobject]@{
    Version = '0.9.1.8'
    InstalledAt = [DateTimeOffset]'2026-11-12T22:59:00+01:00'
}
$packageProductVersion = '0.9.1.8'
foreach ($language in @('de', 'en')) {
    $script:language = $language
    $repairText = Get-InstallNoticeText
    $repairTerm = if ($language -eq 'de') { 'repariert' } else { 'repaired' }
    if (-not $repairText.Contains($repairTerm)) {
        throw "Installer repair notice is incorrect for '$language'."
    }
    $packageProductVersion = '0.9.1.8'
    $updateText = Get-InstallNoticeText
    $updateTerm = if ($language -eq 'de') { 'aktualisiert' } else { 'updated' }
    if (-not $updateText.Contains($updateTerm)) {
        throw "Installer update notice is incorrect for '$language'."
    }
    $packageProductVersion = '0.9.1.8'
}
$installerResources = [IO.File]::ReadAllText(
    (Join-Path $PSScriptRoot 'xucRunInstaller\Installer.rc'))
$installerMain = [IO.File]::ReadAllText(
    (Join-Path $PSScriptRoot 'xucRunInstaller\main.cpp'))
$infoResources = [IO.File]::ReadAllText(
    (Join-Path $PSScriptRoot 'xucRunSharedResources\ClientResources.rc'))
foreach ($requiredInfoLicense in @(
    'License: Apache License 2.0 (open source)',
    'Lizenz: Apache License 2.0 (Open Source)',
    'Apache License 2.0'
)) {
    if (-not $infoResources.Contains($requiredInfoLicense)) {
        throw "The Core /info resources are missing '$requiredInfoLicense'."
    }
}
foreach ($entry in @(
    @{ Resource = 'IDR_LICENSE RCDATA "..\\LICENSE"'; Payload = '{ IDR_LICENSE, L"LICENSE", false }' },
    @{ Resource = 'IDR_NOTICE RCDATA "..\\NOTICE"'; Payload = '{ IDR_NOTICE, L"NOTICE", false }' }
)) {
    if (-not $installerResources.Contains($entry.Resource) -or
        -not $installerMain.Contains($entry.Payload)) {
        throw "The installer must embed and extract $($entry.Resource)."
    }
}
$licenseText = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'LICENSE'))
$noticeText = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'NOTICE'))
if ($licenseText -notmatch '^\s*Apache License\r?\n\s*Version 2\.0, January 2004' -or
    $licenseText -match '(?i)commons[\s-]+clause' -or
    $noticeText -match '(?i)commons[\s-]+clause') {
    throw 'The Core license and notice must use Apache 2.0 without Commons Clause.'
}
& {
    Add-Type -AssemblyName System.Windows.Forms
    Add-Type -AssemblyName System.Drawing
    $script:language = 'de'
    $script:statusKey = 'StatusReady'
    $script:statusArguments = @()
    $script:installInProgress = $false
    $script:certificateState = [pscustomobject]@{ Kind = 'New' }
    $installDirectory = 'C:\Program Files\xucRun'
    $programDataDirectory = 'C:\ProgramData'
    $installerLogDirectory = 'C:\ProgramData\xucRun\InstallerLogs'
    $minimumVcRuntimeVersion = '14.0.0.0'
    function Get-VcRuntimeStatusText { return 'Runtime' }
    function Get-CertificateStatusText { return 'Certificate' }
    function Get-InstallNoticeText { return 'Installation' }
    function Get-TrayCompatibilityStatusText { return 'Tray' }

    $start = $guiSource.IndexOf('$form = [Windows.Forms.Form]::new()')
    $end = $guiSource.IndexOf('$acknowledgment.Add_CheckedChanged', $start)
    if ($start -lt 0 -or $end -lt 0) {
        throw 'Core installer GUI construction could not be located.'
    }
    . ([scriptblock]::Create($guiSource.Substring($start, $end - $start)))
    try {
        $null = $form.Handle
        $null = $tabs.Handle
        $null = $licenseTab.Handle
        foreach ($language in @('de', 'en')) {
            $script:language = $language
            Update-Language
            if ($licenseIntroduction.Text -cne $texts[$language].LicenseIntroduction -or
                $licenseIntroduction.Parent -ne $licenseTab -or
                $licenseIntroduction.Bottom -ge $licenseDetails.Top -or
                $licenseDetails.Bottom -gt $licenseTab.ClientSize.Height -or
                $licenseDetails.Height -lt 100) {
                throw "Core installer license introduction is clipped or incorrect in $language."
            }
            $expectedLicense = (($licenseText + "`n`nNOTICE`n`n" + $noticeText) `
                -replace "`r?`n", "`r`n")
            if ($licenseDetails.Text -cne $expectedLicense) {
                throw "Core installer license text changed in $language."
            }
        }
    }
    finally {
        $form.Dispose()
    }
}
$availabilityFunction = $ast.Find({
    param($node)
    $node -is [Management.Automation.Language.FunctionDefinitionAst] -and
    $node.Name -eq 'Update-InstallAvailability'
}, $true)
if (-not $availabilityFunction) {
    throw 'Update-InstallAvailability was not found.'
}
. ([scriptblock]::Create($availabilityFunction.Extent.Text))
$acknowledgment = [pscustomobject]@{ Checked = $false }
$installButton = [pscustomobject]@{ Enabled = $true }
$script:certificateState = [pscustomobject]@{ Kind = 'New' }
$script:trayCompatibilityState = [pscustomobject]@{ Kind = 'NotInstalled' }
$script:installInProgress = $false
Update-InstallAvailability
if ($installButton.Enabled) {
    throw 'Installation must be disabled before acknowledgment.'
}
$acknowledgment.Checked = $true
Update-InstallAvailability
if (-not $installButton.Enabled) {
    throw 'Acknowledgment must enable installation with an unambiguous certificate.'
}
$script:certificateState.Kind = 'Ambiguous'
Update-InstallAvailability
if ($installButton.Enabled) {
    throw 'Acknowledgment must not bypass an ambiguous certificate.'
}
$script:certificateState.Kind = 'Existing'
$script:trayCompatibilityState.Kind = 'Incompatible'
Update-InstallAvailability
if ($installButton.Enabled) {
    throw 'Acknowledgment must not bypass an incompatible Tray installation.'
}
$script:trayCompatibilityState.Kind = 'Compatible'
$script:installInProgress = $true
Update-InstallAvailability
if ($installButton.Enabled) {
    throw 'Installation must remain disabled while an installation is in progress.'
}
$script:installInProgress = $false
$acknowledgment.Checked = $false
Update-InstallAvailability
if ($installButton.Enabled) {
    throw 'Withdrawing acknowledgment must disable installation.'
}
& {
    $certificateHashFunction = $ast.Find({
        param($node)
        $node -is [Management.Automation.Language.FunctionDefinitionAst] -and
        $node.Name -eq 'Get-CertificateSha256'
    }, $true)
    $trayStateFunction = $ast.Find({
        param($node)
        $node -is [Management.Automation.Language.FunctionDefinitionAst] -and
        $node.Name -eq 'Get-TrayCompatibilityState'
    }, $true)
    if (-not $certificateHashFunction -or -not $trayStateFunction) {
        throw 'Tray compatibility preflight functions were not found.'
    }
    . ([scriptblock]::Create($trayStateFunction.Extent.Text))

    $installDirectory = 'C:\Program Files\xucRun'
    $languageDirectory = 'C:\Program Files\xucRun\Languages'
    $trayPath = 'C:\Program Files\xucRun\xucRunTray.exe'
    $trayUninstallPath =
        'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\xucRun Tray'
    $trayFiles = @(
        $trayPath,
        'C:\Program Files\xucRun\Languages\xucRunTray.de-DE.dll',
        'C:\Program Files\xucRun\Languages\xucRunTray.en-US.dll'
    )
    $script:trayTestFiles = @{}
    $script:registeredTrayVersion = '1.2.3.4'
    $script:trayFileVersion = '1.2.3.4'
    $script:traySignatureStatus =
        [Management.Automation.SignatureStatus]::Valid
    $script:traySignerHash = [byte[]]@(1)

    function Test-Path {
        param(
            [string]$LiteralPath,
            $PathType
        )
        return $script:trayTestFiles.ContainsKey($LiteralPath)
    }
    function Get-Item {
        param([string]$LiteralPath)
        if ($LiteralPath -eq $trayUninstallPath) {
            $item = [pscustomobject]@{}
            $item | Add-Member -MemberType ScriptMethod -Name GetValue -Value {
                param([string]$Name, $DefaultValue)
                return $script:registeredTrayVersion
            }
            return $item
        }
        return [pscustomobject]@{
            VersionInfo = [pscustomobject]@{
                ProductVersion = $script:trayFileVersion
            }
        }
    }
    function Get-AuthenticodeSignature {
        param([string]$LiteralPath)
        return [pscustomobject]@{
            Status = $script:traySignatureStatus
            SignerCertificate = [pscustomobject]@{ Hash = $script:traySignerHash }
        }
    }
    function Get-CertificateSha256 {
        param($Certificate)
        return [byte[]]$Certificate.Hash
    }

    $certificateState = [pscustomobject]@{
        Kind = 'Existing'
        Certificate = [pscustomobject]@{ Hash = [byte[]]@(1) }
    }
    $state = Get-TrayCompatibilityState -PackageProductVersion '1.2.3.4' `
        -CertificateState $certificateState
    if ($state.Kind -ne 'NotInstalled') {
        throw 'Tray preflight must allow installation when Tray is absent.'
    }

    foreach ($path in @($trayUninstallPath) + $trayFiles) {
        $script:trayTestFiles[$path] = $true
    }
    $script:registeredTrayVersion = '1.2.3.3'
    $state = Get-TrayCompatibilityState -PackageProductVersion '1.2.3.4' `
        -CertificateState $certificateState
    if ($state.TextKey -ne 'TrayVersionMismatch') {
        throw 'Tray preflight did not detect a registered version conflict.'
    }

    $script:registeredTrayVersion = '1.2.3.4'
    $script:trayTestFiles.Remove($trayFiles[2])
    $state = Get-TrayCompatibilityState -PackageProductVersion '1.2.3.4' `
        -CertificateState $certificateState
    if ($state.TextKey -ne 'TrayIncomplete') {
        throw 'Tray preflight did not detect an incomplete Tray installation.'
    }

    $script:trayTestFiles[$trayFiles[2]] = $true
    $newCertificateState = [pscustomobject]@{
        Kind = 'New'
        Certificate = $null
    }
    $state = Get-TrayCompatibilityState -PackageProductVersion '1.2.3.4' `
        -CertificateState $newCertificateState
    if ($state.TextKey -ne 'TraySignerUnavailable') {
        throw 'Tray preflight did not reject replacement of the Tray signer.'
    }

    $script:traySignatureStatus =
        [Management.Automation.SignatureStatus]::NotSigned
    $state = Get-TrayCompatibilityState -PackageProductVersion '1.2.3.4' `
        -CertificateState $certificateState
    if ($state.TextKey -ne 'TraySignatureInvalid') {
        throw 'Tray preflight did not reject an invalid Tray signature.'
    }

    $script:traySignatureStatus =
        [Management.Automation.SignatureStatus]::Valid
    $script:traySignerHash = [byte[]]@(2)
    $state = Get-TrayCompatibilityState -PackageProductVersion '1.2.3.4' `
        -CertificateState $certificateState
    if ($state.TextKey -ne 'TraySignerMismatch') {
        throw 'Tray preflight did not detect a signer conflict.'
    }

    $script:traySignerHash = [byte[]]@(1)
    $state = Get-TrayCompatibilityState -PackageProductVersion '1.2.3.4' `
        -CertificateState $certificateState
    if ($state.Kind -ne 'Compatible') {
        throw 'Tray preflight rejected a complete matching Tray installation.'
    }
}
$function = $ast.Find({
    param($node)
    $node -is [Management.Automation.Language.FunctionDefinitionAst] -and
    $node.Name -eq 'Confirm-VcRuntime'
}, $true)
if (-not $function) {
    throw 'Confirm-VcRuntime was not found.'
}
$text = $function.Extent.Text
$contracts = @(
    '$redistributableStaging = Join-Path $PackageDirectory',
    'Copy-Item -LiteralPath $dialog.FileName',
    'Assert-MicrosoftRedistributable -Path $redistributablePath',
    'Start-Process -FilePath $redistributablePath',
    'Remove-Item -LiteralPath $redistributableStaging'
)
foreach ($contract in $contracts) {
    if (-not $text.Contains($contract)) {
        throw "VC runtime staging contract is missing: $contract"
    }
}
if ($text.Contains('$redistributablePath = $dialog.FileName')) {
    throw 'The selected VC runtime must not be validated or executed in place.'
}
$copyIndex = $text.IndexOf('Copy-Item -LiteralPath $dialog.FileName')
$validationIndex =
    $text.IndexOf('Assert-MicrosoftRedistributable -Path $redistributablePath')
$executionIndex = $text.IndexOf('Start-Process -FilePath $redistributablePath')
if ($copyIndex -ge $validationIndex -or $validationIndex -ge $executionIndex) {
    throw 'The staged VC runtime must be copied, validated, then executed in order.'
}

function Assert-StagingContract {
    param(
        [Parameter(Mandatory)][string]$RelativePath,
        [Parameter(Mandatory)][string[]]$Required,
        [Parameter(Mandatory)][string[]]$Forbidden,
        [Parameter(Mandatory)][string[]]$Ordered
    )

    $source = [IO.File]::ReadAllText((Join-Path $PSScriptRoot $RelativePath))
    foreach ($contract in $Required + $Ordered) {
        if (-not $source.Contains($contract)) {
            throw "Staging contract is missing in ${RelativePath}: $contract"
        }
    }
    foreach ($contract in $Forbidden) {
        if ($source.Contains($contract)) {
            throw "Unprotected staging pattern found in ${RelativePath}: $contract"
        }
    }
    $previousIndex = -1
    foreach ($contract in $Ordered) {
        $index = $source.LastIndexOf($contract, [StringComparison]::Ordinal)
        if ($index -le $previousIndex) {
            throw "Staging steps are out of order in ${RelativePath}: $contract"
        }
        $previousIndex = $index
    }
}

# A staging directory must receive its access control list in the creation
# call. Creating it first and restricting it afterwards lets other accounts
# plant entries they continue to own.
$createdProtected = '[void][IO.Directory]::CreateDirectory($Path, $acl)'
$unprotectedPatterns = @(
    '$env:ProgramData',
    '$env:SystemRoot',
    '$env:TEMP',
    'GetTempPath',
    'New-Item -ItemType Directory -Path $stagingDirectory',
    'New-Item -ItemType Directory -Path $stagedParent',
    'New-Item -ItemType Directory -Path $installerLogDirectory'
)
Assert-StagingContract -RelativePath 'Install-xucRun.ps1' `
    -Required @(
        $createdProtected,
        'Assert-ProtectedDirectory -Path $Path',
        'New-ProtectedDirectory -Path $stagedParent',
        '$verifiedHashes[$file.RelativePath]') `
    -Forbidden $unprotectedPatterns `
    -Ordered @(
        'New-ProtectedDirectory -Path $stagingDirectory',
        'The staged package is not signed by the package signer.',
        'Copy-Item -LiteralPath $file.Path -Destination $destination -Force',
        'The installed files are not signed by the package signer.',
        "'start=', `$startMode")
Assert-StagingContract -RelativePath 'UserCodeSigning\Sign-xucRunPackage.ps1' `
    -Required @(
        $createdProtected,
        'Assert-ProtectedDirectory -Path $Path',
        'New-ProtectedDirectory -Path $stagedParent') `
    -Forbidden $unprotectedPatterns `
    -Ordered @(
        'New-ProtectedDirectory -Path $stagingDirectory',
        'Staged package file does not match its source',
        'Set-AuthenticodeSignature @signatureArguments')

# An elevated process inherits its environment from the caller. Product and
# system paths must therefore come from Windows known folders.
$guiSource = [IO.File]::ReadAllText($path)
foreach ($variable in @('$env:SystemRoot', '$env:ProgramFiles', '$env:ProgramData')) {
    if ($guiSource.Contains($variable)) {
        throw "Installer-Gui.ps1 locates a path through the environment: $variable"
    }
}
$environmentPathPattern =
    'GetEnvironmentVariableW\(\s*L"(ProgramFiles|ProgramData|SystemRoot|windir)"'
foreach ($relativePath in $repositoryFiles) {
    if ([IO.Path]::GetExtension($relativePath) -notin @('.cpp', '.h', '.inl')) {
        continue
    }
    $source = [IO.File]::ReadAllText((Join-Path $PSScriptRoot (
        $relativePath.Replace('/', [IO.Path]::DirectorySeparatorChar))))
    if ($source -match $environmentPathPattern) {
        throw "Source locates a product or system path through the environment: " +
            $relativePath
    }
}

# Diagnostic broker contracts act without the approval flow and must not be
# decoded by a release broker.
$decoderSource = [IO.File]::ReadAllText((Join-Path $PSScriptRoot `
    'xucRunBroker\BrokerPipeRequestDecoder.inl'))
foreach ($contract in @(
    'Launch', 'CredentialMetadata', 'SessionTest', 'CredentialStoreTest',
    'CredentialSave', 'CredentialUseTest'
)) {
    $gated = 'DiagnosticPipeContractsEnabled\s*&&\s*xucrun::broker::Deserialize' +
        $contract + 'Request\('
    $occurrences = [regex]::Matches($decoderSource,
        'Deserialize' + $contract + 'Request\(').Count
    if ($occurrences -ne 1 -or $decoderSource -notmatch $gated) {
        throw "Diagnostic broker contract is decoded in release builds: $contract"
    }
}
$serverSource = [IO.File]::ReadAllText((Join-Path $PSScriptRoot `
    'xucRunBroker\BrokerPipeServer.cpp'))
if ($serverSource -notmatch
    '#ifdef _DEBUG\s+constexpr bool DiagnosticPipeContractsEnabled = true;\s+' +
    '#else\s+constexpr bool DiagnosticPipeContractsEnabled = false;\s+#endif') {
    throw 'Diagnostic broker contracts must be disabled outside Debug builds.'
}

$machineSource = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'Install-xucRun.ps1'))
$machineTokens = $null
$machineErrors = $null
$machineAst = [Management.Automation.Language.Parser]::ParseInput(
    $machineSource, [ref]$machineTokens, [ref]$machineErrors)
if ($machineErrors.Count -ne 0) {
    throw 'The Core machine installer could not be parsed.'
}
foreach ($contract in @(
    'function Assert-TrayCompatibleWithCorePackage',
    'Assert-TrayCompatibleWithCorePackage `',
    '-ProductVersion $packageProductVersion `',
    '-SignerSha256 $signerSha256'
)) {
    if (-not $machineSource.Contains($contract)) {
        throw "The manual installer Tray compatibility contract is missing: $contract"
    }
}
$packageSignatureIndex =
    $machineSource.IndexOf('$signerSha256 = Assert-ConsistentPackageSignatures')
$trayCompatibilityIndex =
    $machineSource.IndexOf('Assert-TrayCompatibleWithCorePackage `')
if ($packageSignatureIndex -lt 0 -or
    $trayCompatibilityIndex -le $packageSignatureIndex) {
    throw 'The manual installer must check Tray compatibility after package signatures.'
}
foreach ($name in @('Assert-NotReparsePoint', 'Get-PackageLegalFiles', 'Remove-LegacyLegalFiles')) {
    $definition = $machineAst.Find({
        param($node)
        $node -is [Management.Automation.Language.FunctionDefinitionAst] -and
            $node.Name -eq $name
    }, $true)
    if (-not $definition) {
        throw "Core machine installer function is missing: $name"
    }
    . ([scriptblock]::Create($definition.Extent.Text))
}
$legalTestRoot = Join-Path ([IO.Path]::GetTempPath()) (
    'xucRun-Core-legal-test-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $legalTestRoot | Out-Null
try {
    $package = Join-Path $legalTestRoot 'package'
    $installed = Join-Path $legalTestRoot 'installed'
    $legal = Join-Path $installed 'Licenses'
    New-Item -ItemType Directory -Path $package, $legal -Force | Out-Null
    foreach ($name in @('LICENSE', 'NOTICE')) {
        [IO.File]::WriteAllText((Join-Path $package $name), 'package')
        [IO.File]::WriteAllText((Join-Path $installed "$name-xucRun.txt"), 'legacy')
        [IO.File]::WriteAllText((Join-Path $installed "$name-xucRun-Tray.txt"), 'tray')
        [IO.File]::WriteAllText((Join-Path $legal "$name-xucRun.txt"), 'new')
    }
    $legalFiles = @(Get-PackageLegalFiles -Root $package)
    foreach ($name in @('LICENSE', 'NOTICE')) {
        $file = @($legalFiles | Where-Object RelativePath -eq $name)
        if ($file.Count -ne 1 -or
            $file[0].DestinationRelativePath -cne "Licenses\$name-xucRun.txt") {
            throw "Core legal-file destination is incorrect for $name."
        }
    }
    Remove-LegacyLegalFiles -Root $installed
    foreach ($name in @('LICENSE', 'NOTICE')) {
        if ((Test-Path -LiteralPath (Join-Path $installed "$name-xucRun.txt")) -or
            [IO.File]::ReadAllText((Join-Path $installed "$name-xucRun-Tray.txt")) -ne 'tray' -or
            [IO.File]::ReadAllText((Join-Path $legal "$name-xucRun.txt")) -ne 'new') {
            throw "Core legal-file migration did not preserve the correct files for $name."
        }
    }
}
finally {
    Remove-Item -LiteralPath $legalTestRoot -Recurse -Force
}

Write-Host 'Repository file documentation and installer staging contracts passed'
