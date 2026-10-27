# Purpose: Verifies the graphical installer's protected VC runtime staging contract.
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
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

Write-Host 'Graphical installer VC runtime staging contract passed'
