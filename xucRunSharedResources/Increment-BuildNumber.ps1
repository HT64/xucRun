<#
.SYNOPSIS
Updates the shared xucRun build counter and generated version header.

.DESCRIPTION
Reads the semantic version from version.txt, compares it with the version in
BuildVersion.h, and chooses the next build number. A semantic-version change
resets the build to zero; otherwise a valid buildcounter.txt or, as a fallback,
BuildVersion.h is incremented. The script writes buildcounter.txt and generates
BuildVersion.h, retrying the header write when another process temporarily
locks it.

.PARAMETER ProjectDir
Specifies the directory containing version.txt, buildcounter.txt, and
BuildVersion.h. The value has no default, must be nonempty, and must identify an
existing path. Surrounding quotation marks and a trailing backslash are
removed.

.EXAMPLE
.\Increment-BuildNumber.ps1 -ProjectDir 'C:\src\xucRun\xucRunSharedResources'

Advances or resets the build counter and regenerates BuildVersion.h in the
shared-resource directory.

.NOTES
Intended for the Visual Studio pre-build event. It modifies buildcounter.txt
and BuildVersion.h; malformed or unreadable version inputs cause warnings and
fallback defaults rather than immediate failure, and a counter write failure
is also only a warning. It performs no network or signing operation. Exit code
0 means the header was written. Exit code 1 is used for a missing or nonexistent
ProjectDir, a non-I/O header write failure, or exhaustion of the six lock
retries.
#>
# Reads the semantic version and generates buildcounter.txt and BuildVersion.h.
param(
	[string]$ProjectDir
)

# --- Sanitize input and normalize path -------------------------------------------------
$ProjectDir = $ProjectDir -as [string]
if ($null -eq $ProjectDir -or $ProjectDir.Trim().Length -eq 0) {
	Write-Error "Error: The ProjectDir parameter is missing or empty."
	exit 1
}

$ProjectDir = $ProjectDir.Trim()
# Remove surrounding quotation marks if present
$ProjectDir = $ProjectDir.Trim('"')
# Remove trailing backslash if present
$ProjectDir = $ProjectDir.TrimEnd('\')

if (-not (Test-Path -LiteralPath $ProjectDir)) {
	Write-Error "Error: ProjectDir does not exist: $ProjectDir"
	exit 1
}

# Set console output encoding to ANSI (system ANSI code page)
# so localized characters are displayed correctly in Visual Studio output
try {
	$ansiCodePage = [System.Globalization.CultureInfo]::InstalledUICulture.TextInfo.ANSICodePage
	$ansiEncoding = [System.Text.Encoding]::GetEncoding($ansiCodePage)
	[Console]::OutputEncoding = $ansiEncoding
} catch {
	# Fall back to the system default
	[Console]::OutputEncoding = [System.Text.Encoding]::Default
}

$headerFile = Join-Path -Path $ProjectDir -ChildPath "BuildVersion.h"
$counterFile = Join-Path -Path $ProjectDir -ChildPath "buildcounter.txt"
$versionFile = Join-Path -Path $ProjectDir -ChildPath "version.txt"
$buildNumber = 1
$versionMajor = 1
$versionMinor = 0
$versionPatch = 0
$semanticVersionChanged = $false

# --- Read version number from version.txt --------------------------------------------
if (Test-Path -LiteralPath $versionFile) {
	try {
		$versionContent = Get-Content -LiteralPath $versionFile -Raw
		if ($versionContent -match '^(\d+)\.(\d+)\.(\d+)') {
			$versionMajor = [int]$matches[1]
			$versionMinor = [int]$matches[2]
			$versionPatch = [int]$matches[3]
		}
	} catch {
		Write-Warning "Warning: version.txt could not be read: $_"
	}
}

# Start a new semantic version at build zero. The generated header records the
# semantic version used by the previous build and therefore makes this reset
# occur exactly once.
if (Test-Path -LiteralPath $headerFile) {
	try {
		$headerContent = Get-Content -LiteralPath $headerFile -Raw
		if ($headerContent -match '#define VERSION_STRING "(\d+)\.(\d+)\.(\d+)\.\d+"') {
			$semanticVersionChanged =
				[int]$matches[1] -ne $versionMajor -or
				[int]$matches[2] -ne $versionMinor -or
				[int]$matches[3] -ne $versionPatch
		}
	} catch {
		Write-Warning "Warning: BuildVersion.h version could not be read: $_"
	}
}

# --- Read current build number (prefer buildcounter.txt) ------------------------------
if ($semanticVersionChanged) {
	$buildNumber = 0
} elseif (Test-Path -LiteralPath $counterFile) {
	try {
		$cnt = Get-Content -LiteralPath $counterFile -Raw
		if ($cnt -match '^(?:\s*)(\d+)(?:\s*)$') {
			$buildNumber = [int]$matches[1] + 1
		}
	} catch {
		Write-Warning "Warning: buildcounter.txt could not be read: $_"
	}

} elseif (Test-Path -LiteralPath $headerFile) {
	try {
		$content = Get-Content -LiteralPath $headerFile -Raw
		if ($content -match '#define BUILD_NUMBER (\d+)') {
			$buildNumber = [int]$matches[1] + 1
		}
	} catch {
		Write-Warning "Warning: BuildVersion.h could not be read: $_"
	}
}

# --- Persist the counter in buildcounter.txt ------------------------------------------
try {
	[System.IO.File]::WriteAllText($counterFile, $buildNumber.ToString(), [System.Text.Encoding]::ASCII)
} catch {
	Write-Warning "Warning: buildcounter.txt could not be written: $_"
}

# --- Write or replace BuildVersion.h --------------------------------------------------
$versionString = "$versionMajor.$versionMinor.$versionPatch.$buildNumber"
$newContent = @"
// Purpose: Provides the auto-generated product version macros shared by all build targets.
// Approach: Generated by Increment-BuildNumber.ps1 from version.txt and the persisted build counter; do not edit manually.
#pragma once

#define VERSION_MAJOR $versionMajor
#define VERSION_MINOR $versionMinor
#define VERSION_PATCH $versionPatch
#define BUILD_NUMBER $buildNumber
#define VERSION_STRING "$versionString"
#define BUILD_DATE __DATE__
#define BUILD_TIME __TIME__
"@

# Write BuildVersion.h with retries if another process has locked the file
$maxAttempts = 6
$attempt = 0
$written = $false
while (-not $written -and $attempt -lt $maxAttempts) {
	$attempt++
	try {
		Set-Content -LiteralPath $headerFile -Value $newContent -Encoding UTF8 -Force
		$written = $true
	} catch [System.IO.IOException] {
		Write-Warning ("Attempt {0}/{1}: BuildVersion.h is locked, waiting..." -f $attempt, $maxAttempts)
		Start-Sleep -Milliseconds (200 * $attempt)
	} catch {
		Write-Error "Error writing BuildVersion.h: $_"
		exit 1
	}
}

if (-not $written) {
	Write-Error "Error: Could not write BuildVersion.h after $maxAttempts attempts."
	exit 1
}

Write-Output "Build number updated to $buildNumber (version $versionString)"

exit 0
