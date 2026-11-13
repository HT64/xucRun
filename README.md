<!--
Purpose: Introduces xucRun and guides users through installation, first use, and further documentation.
Approach: Presents the shortest safe path to a first launch, then links to canonical task-specific guides.
-->
# xucRun

**Language:** English | [Deutsch](README.de.md)

**Download:** [Latest release](https://github.com/HT64/xucRun/releases/latest)

**Documentation:** [Installation](docs/en/INSTALLATION.md) |
[Using xucRun](docs/en/USAGE.md) | [Reference](docs/en/REFERENCE.md) |
[Security model](docs/en/SECURITY_MODEL.md) | [Build instructions](docs/en/BUILDING.md)

**xucRun** stands for **eXecute Under User Context**.

> **Status:** xucRun is actively maintained and continues to evolve. It has been proven in
> practical use on the author's Windows 10 and Windows 11 systems. Further changes are possible,
> and defects cannot be ruled out.

## Overview

xucRun runs a program, command, or script under the user account selected for that purpose while
the user remains in the current Windows session. The target account can be a standard account or
belong to the local Administrators group; in the latter case, the launch can optionally use
elevated privileges. Its central use case is an experienced Windows user who normally works from
a standard account and wants to perform a specific administrative action without switching to a
separate administrator session or entering the account name and password again for every matching
invocation.

![Program launched with xucRun under the target account within the signed-in standard user's Windows session](docs/en/images/overview.svg)

xucRun keeps the signed-in account and the selected target account separate. It does not
temporarily turn a standard user into an administrator. Programs use the identity, permissions,
and user profile of the selected target account. Account selection and elevation remain separate
decisions.

No separate initial setup is required. xucRun requests a target account, credentials, UAC
confirmation, and an approval when they are first needed. Once suitable approvals exist, normal
use usually requires only the program and its arguments. The
[security model](docs/en/SECURITY_MODEL.md) explains the account separation, authorization model,
safeguards, and remaining risks.

## When xucRun is useful

A user who already works from an account in the local Administrators group and only wants to
occasionally elevate programs under that same account can normally use Windows UAC or Sudo for
Windows. xucRun is primarily useful when the signed-in standard account and the account used for
administrative actions remain separate and recurring launches should work without entering the
password again each time, while the user can still decide whether only the exact same invocation
or, for example, different arguments are allowed. Account selection and elevation remain separate
decisions.

xucRun is intended for experienced Windows users who understand Windows accounts, UAC, and
permissions. The
[comparison and decision guidance](docs/en/SECURITY_MODEL.md#comparison-with-windows-features-and-other-solutions)
explains how it differs from Fast User Switching, UAC, Sudo for Windows, `runas.exe /savecred`,
and other approaches.

## Install xucRun

1. Download the current x64 package or graphical installer from the
   [official release](https://github.com/HT64/xucRun/releases/latest).
2. Compare the downloaded file's SHA-256 hash with `SHA256SUMS.txt` from the release.
3. Run `xucRun-Installer-<Version>-x64.exe` and approve the Windows UAC request.
4. Optionally let the installer add xucRun to the machine-wide PATH so that `xucRun.exe` works
   without an absolute path in interactive commands.

Windows may report an unknown publisher on first launch because the public installer currently
has no signature from a publicly trusted publisher. The installer creates or reuses a protected
machine-local certificate and uses it to establish trust between the installed xucRun components.
The package is not portable; copying individual executables is not a complete installation.

![Graphical xucRun installer](docs/en/images/installer.png)

The [installation guide](docs/en/INSTALLATION.md) contains package verification, prerequisites,
graphical and script-based installation, updates, Tray compatibility, and uninstallation.

## First launch

After installation, open PowerShell or Command Prompt. For example:

```powershell
xucRun.exe
xucRun.exe powershell.exe -NoExit
xucRun.exe regedit.exe
```

Without arguments, xucRun replaces a supported parent shell with the same shell in the
automatically selected account and privilege context. With a program, it starts that program and
passes along the remaining arguments.

If no suitable data exists yet, xucRun performs the required steps during this invocation:

1. select an enabled local target account;
2. enter that account's credentials on the separate protected xucRun desktop;
3. confirm Windows UAC when elevated privileges are requested;
4. choose the duration and scope of the xucRun approval.

Storing credentials in advance is not required. xucRun requests them automatically when a program
is first launched under a target account. `/addcred` remains available when credentials should be
stored before the first program launch.

| Select target account | Enter credentials | Confirm storage |
|:---:|:---:|:---:|
| ![Local target-user selection](docs/en/images/target-user-selection.png) | ![Credential entry](docs/en/images/credential-entry.png) | ![Stored credential confirmation](docs/en/images/credential-stored.png) |

Account markers identify Administrators-group members (`[A]`), standard accounts (`[S]`), and
accounts whose membership could not be determined (`[?]`). The detailed first-use flow and all
account-selection switches are documented in
[Using xucRun](docs/en/USAGE.md#account-selection-and-privilege-mode).

## More examples

Open Device Manager from a standard-user session:

```cmd
xucRun.exe "C:\Windows\System32\mmc.exe" "C:\Windows\System32\devmgmt.msc"
```

Create a desktop shortcut for an elevated PowerShell by entering the following in its
**Target** field:

```text
"C:\Program Files\xucRun\xucRun.exe" /user-elevated "C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe" -NoExit
```

Run Thunderbird with the profile and identity of the `MailUser` Windows account without requesting
elevation:

```powershell
& 'C:\Program Files\xucRun\xucRun.exe' `
    /user:MailUser `
    'C:\Program Files\Mozilla Thunderbird\thunderbird.exe'
```

Interactive commands may use executable short names. Scripts, shortcuts, scheduled tasks, and
other fixed invocations should use the actual absolute paths of both `xucRun.exe` and the target
program to reduce executable-search redirection risks. More examples, shell replacement, batch
files, Explorer, desktop shortcuts, and Total Commander are covered in
[Using xucRun](docs/en/USAGE.md).

## Credentials and approvals

xucRun stores credentials through Windows APIs and uses them only when a matching approval permits
the requested launch. Approvals can bind the target account, program, file hash, arguments,
working directory, and other characteristics with different scopes and durations. Windows UAC
remains a separate interactive step where required.

Use `/showcred` and `/showapprovals` to inspect stored target accounts and active approvals. The
complete management workflow is described in
[Using xucRun](docs/en/USAGE.md#managing-credentials-and-approvals); the authorization model is
described in the [security model](docs/en/SECURITY_MODEL.md#approvals-and-windows-uac).

## Important commands

| Command | Purpose |
|---|---|
| `xucRun.exe [<program> [arguments...]]` | launch a program or replace a supported parent shell |
| `xucRun.exe /addcred [<account>]` | store credentials in advance |
| `xucRun.exe /showcred` | show accounts with stored xucRun credentials |
| `xucRun.exe /showapprovals` | show active approvals |
| `xucRun.exe /backup` | manage local backups |
| `xucRun.exe /info` | show version, signing, broker, and configuration information |
| `xucRun.exe /audit` | run the security diagnostics |
| `xucRun.exe /help` | show the complete built-in help |

The [command and configuration reference](docs/en/REFERENCE.md) documents all switches, advanced
LocalSystem and TrustedInstaller launches, exit codes, backups, configuration, and diagnostics.

## Requirements and security information

xucRun supports x64 editions of Windows 10 and Windows 11 and requires the Microsoft Visual C++
Redistributable v14 for x64. It is intended for trusted, self-managed systems without elevated
protection requirements. Use dedicated target accounts with only the permissions they need where
practical.

> **Important security notices:** xucRun uses Windows APIs to store credentials. For xucRun to
> start a program under another user account, however, xucRun
> must process the required credentials temporarily. Complete protection against reading or
> misuse by sufficiently privileged malware or an already compromised system therefore cannot be
> guaranteed. The source code of xucRun is
> disclosed so that the implemented technical measures, their limits, and the remaining
> risks can be understood and independently reviewed. Disclosure alone is neither a security
> guarantee nor evidence of an independent security audit. Use xucRun at your own responsibility.
> xucRun should be used only on trusted, self-managed systems that do not require an elevated level
> of protection, and preferably with dedicated target accounts having the minimum
> required privileges. Scripts, shortcuts, scheduled tasks, and configuration files that invoke
> xucRun or provide its parameters must also be trusted and protected against unauthorized
> modification.
> These security notices are not exhaustive; not all risks and security vulnerabilities can be
> anticipated or ruled out.
> To the extent permitted by law, the author and contributors accept
> no liability for damage, data loss, or other consequences arising from the use or incorrect use
> of xucRun, software defects or security vulnerabilities, malware, or a compromised system. This
> includes, in particular, the loss, disclosure, or misuse of credentials. The disclaimer of
> warranty and limitation of liability in the [Apache License 2.0](LICENSE) govern.

The complete [security model](docs/en/SECURITY_MODEL.md) documents the broker trust boundary,
caller and launch-target validation, credential protection, fixed-invocation risks, and residual
risks.

## Optional Tray

The separately installed [xucRun Tray add-on](https://github.com/HT64/xucRun-Tray) provides status
information and convenient access to common management functions. xucRun and its broker remain
fully usable without an installed or running Tray. Core and Tray versions must match exactly.

## Project background

The [background and origins](docs/en/BACKGROUND.md) describe why xucRun was created, the practical
needs that shaped it, and why its security considerations are documented in detail.

## Support the project

Development can be supported voluntarily through [Patreon](https://www.patreon.com/xucRun).
Support is not required to download or use xucRun and does not provide exclusive software,
features, releases, downloads, personal support, or response times.

## Reporting vulnerabilities

Do not report unpatched or suspected vulnerabilities publicly. [SECURITY.md](SECURITY.md) describes
the confidential reporting path and supported versions.

## Build and install

The [build instructions](docs/en/BUILDING.md) describe how to create an xucRun package from source.
The [installation guide](docs/en/INSTALLATION.md) covers installation of that package. If normal
uninstallation is unavailable, follow the [manual removal guide](docs/en/MANUAL_REMOVAL.md).

## License

xucRun is open source under the [Apache License 2.0](LICENSE). [NOTICE](NOTICE) contains attribution
notices.

External code contributions are not currently accepted; see [CONTRIBUTING.md](CONTRIBUTING.md).
