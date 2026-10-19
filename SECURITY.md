<!-- Purpose: Defines security support and confidential vulnerability reporting for xucRun Core. -->
# Security Policy

## Supported versions

After the first public xucRun Core release, security fixes are provided for the latest released
Core version only. Older versions are not supported unless the maintainer explicitly announces
otherwise.

No public Core version is supported before the first release.

## Reporting a vulnerability

Do not report an unpatched or suspected vulnerability in a public issue, discussion, pull request
or other public channel.

Use GitHub Private Vulnerability Reporting for the `HT64/xucRun` repository:

1. Open the repository's **Security** tab.
2. Select **Report a vulnerability**.
3. Provide a clear description, affected version or commit, reproduction steps, expected impact
   and any known mitigations.

Include only the sensitive data required to understand the problem. Do not submit real passwords,
private keys, access tokens or unrelated personal data.

The maintainer will review the report and coordinate further communication through the private
report. Response and remediation times depend on reproducibility, severity and implementation
risk; this policy does not promise a fixed service level or a vulnerability bounty.

Please avoid public disclosure until a fix or coordinated advisory is available. If the report
does not affect the public Core but a separate distribution component, the reporter will be
directed to the appropriate private process.

## Scope

This policy applies to source and release artifacts published from `HT64/xucRun`. Third-party
software, unsupported modified builds and proprietary distribution components are outside this
repository's public support scope.
