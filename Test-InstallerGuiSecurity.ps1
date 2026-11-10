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
validated there, and only then executed. Finally it checks that the machine
installer and the package signer create their staging directories together
with the restricting access control list, avoid environment-derived and
temporary staging paths, and keep their verification steps in order, that
neither the graphical installer nor the C++ sources locate product or system
directories through environment variables, and that the broker decodes its
diagnostic pipe contracts only in Debug builds.

.EXAMPLE
.\Test-InstallerGuiSecurity.ps1

Runs the repository file-documentation and installer staging contract
checks.

.NOTES
Requires PowerShell and Git in a working tree of this repository. Commentable
formats without a structural validator and non-PowerShell script extensions
fail explicitly. Generated BuildVersion.h metadata is also checked in its generator. The
existing UTF-16 registry export is treated as a byte-stable format exception
because Git handles it as binary. Raster images, icons, and MPEG-4
documentation videos are validated through FILE_REGISTER.md because their
formats cannot carry the structured metadata head; SVG graphics carry it as an
XML comment. CHANGELOG.md and CHANGELOG.de.md are the
only commentable files exempt from structured metadata heads. The test reads
source files only and performs no installation, signing, network, service,
registry, or certificate operation. Documentation, parser, parameter, and
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
$ast = [Management.Automation.Language.Parser]::ParseFile(
    $path, [ref]$tokens, [ref]$errors)
if ($errors.Count -ne 0) {
    throw "PowerShell parser error: $($errors.Message -join '; ')"
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

Write-Host 'Repository file documentation and installer staging contracts passed'
