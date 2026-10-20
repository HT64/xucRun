# xucRun

**Language:** [Deutsch](README.md) | English

**Documentation:** [Changelog](CHANGELOG.en.md)

**xucRun** stands for **eXecute Under User Context**.

> **Status:** xucRun is under development. The source can be built and installed with a local
> test certificate.

## Motivation and purpose

The program is intended for Windows users who occasionally need to administer their own computer
while remaining signed in with a standard user account, either interactively or through their own
scripts, without signing in separately with an administrator account or entering a password again
for every operation. The required permission for administrative tasks should be grantable from a
standard user account with as little as a single mouse click.

A typical use case arises when a user deliberately remains signed in with a standard user account,
tries to perform an action in an application or file manager, and reaches a permission boundary.
The action can then continue only under another user account, such as an administrator account.
xucRun integrates the native Windows mechanisms required for this into the same workflow.

The user does not need to configure every prerequisite before the first invocation. The requested
program can be started with xucRun immediately. If a suitable account, stored credentials, or an
approval is still missing, xucRun interactively guides the user through the necessary steps and
requests the required information and decisions as part of the current workflow. Existing
credentials and approvals are reused for later invocations when they match the request, so that
only a single confirmation click is normally required afterward.

The basic operations of starting programs under another user account and requesting administrative
rights are also available through Windows itself. xucRun combines these capabilities into a more
convenient workflow and adds controlled approvals for stored credentials. This convenience creates
additional security risks because xucRun stores credentials and reuses them for later invocations.
Users who do not want to accept these risks should use native Windows alternatives such as Fast
User Switching instead.

## Comparison with Windows features and other solutions

Fast User Switching keeps the current Windows session and its open programs running while the user
signs in to a separate session under another account or switches to that account's existing
session. The user can then return to the previous session. This approach is also useful when the
complete interactive working environment of the other user is required.

In contrast to Fast User Switching, xucRun keeps the user in the current session and starts only
the required program under the selected other account, with or without elevated privileges.
Windows User Account Control (UAC) is the Windows security feature through which a user explicitly
confirms a start with administrator privileges. An account switch with xucRun requires UAC
confirmation on every invocation: without elevation, it only confirms the user's presence; with
elevation, the user also confirms the administrator privileges for the target program.

Another built-in Windows option with more similarity to xucRun is `runas.exe`. It can start
individual programs under another account. With `/savecred`, the credentials used for this are
stored in the signed-in user's Windows Credential Manager after the password is entered once.
Afterward, `runas.exe` can repeatedly start programs under the other account without asking for
the password again. However, `runas.exe` has several limitations:

- The target account must be specified with `/user:<Account>` on every invocation. Depending on
  usage, its name therefore appears in scripts, shortcuts, or the command line.
- `runas.exe` does not request elevation through UAC. With UAC enabled, an administrator account
  normally starts with restricted privileges unless elevation is requested separately.
- After `/savecred`, the account whose credentials were stored can be used for further program
  starts without another confirmation. There is no mandatory approval and no binding to
  particular programs, file hashes, arguments, or working directories.
- `runas.exe` does not automatically select a suitable account whose credentials were stored with
  `/savecred` and provides no integrated management of approvals with different scopes or
  expiration periods.
- `runas.exe` is sensitive to complex program paths, arguments, and special characters. When it
  is used in scripts or shortcuts, an additional shell or elevation tool can add further parsing
  and quoting layers. This increases the risk of errors, especially when the elevated program
  needs complex arguments or arguments containing special characters.

xucRun adopts the idea of stored and reusable credentials from `runas.exe` and combines it with
controlled approvals and more convenient use in scripts, desktop shortcuts, other fixed
invocations, and interactive command-line use. Starting a program under another account, stored
credentials, and interactive UAC elevation when required are combined in one tool. When suitable
credentials are stored, the invocation needs neither the target account nor its password. Program
paths, arguments, working directories, batch files, and common shell special characters are
handled in a controlled manner across the required process boundaries.

Starting Device Manager from a standard user account provides a practical example. With exactly
one administrator account for which xucRun credentials are stored, this invocation is sufficient:

```cmd
xucRun.exe "%windir%\System32\mmc.exe" "%windir%\System32\devmgmt.msc"
```

With `runas.exe /savecred` and a separate elevation tool, a schematic equivalent could look as
follows if that tool supports the syntax `elevate.exe <Program> [Arguments ...]`:

```cmd
runas.exe /savecred /user:Administrator "elevate.exe %windir%\System32\mmc.exe %windir%\System32\devmgmt.msc"
```

The exact syntax depends on the selected elevation tool. In this tool chain, the account name must
appear in the invocation, and the complete downstream command line must be passed to `runas.exe`
as one argument. In its main use case, xucRun does not require an account name and keeps the
program and its arguments separate within one invocation. UAC confirmation remains interactive in
both variants.

A more complex example creates a firewall rule for a program whose path contains spaces:

```cmd
xucRun.exe "%windir%\System32\netsh.exe" advfirewall firewall add rule name="Local example server" dir=in action=allow program="C:\Program Files\ExampleServer\server.exe" enable=yes profile=private
```

Here, `netsh.exe` receives several key-value arguments as well as a rule name and a program path
containing spaces. In a tool chain consisting of `runas.exe` and a separate elevation tool, these
quotation marks would also have to survive the outer command line for `runas.exe` and the
elevation tool's parsing unchanged. The required notation again depends on the particular
elevation tool.

After the password is entered once, `runas.exe /savecred /user:Administrator` can use the same
account whose credentials were stored for other programs without requesting the password or a
mandatory confirmation again. xucRun instead requires interactive presence confirmation through
UAC for every such account switch. If no suitable approval exists, an explicit xucRun approval
decision is also required.

`runas.exe /savecred` and xucRun use separate, incompatible credential storage. Before xucRun uses
an account for which xucRun credentials are stored, it requires an explicit approval. That
approval can be bound closely to the calling user account, the target account, the participating
programs and their file hashes, and it can be time-limited. If one of the bound programs changes,
its SHA-256 hash no longer matches the approval. xucRun requires an explicit decision before
transferring the approval to the changed program version.

In addition to built-in Windows features, other vendors provide solutions for account switching,
elevation, and administrative access management. They emphasize different aspects depending on
the use case.

## Approvals and UAC in xucRun

A stored approval alone does not allow a standard user to start a program under an administrator
account without any dialogs. If the target program is to run without elevated administrator
privileges, Windows displays UAC once for the xucRun helper on every invocation. This prompt only
confirms the user's presence; the target program then starts with the stored administrator
account's non-elevated privileges. If the target program is to run with elevated administrator
privileges, Windows displays the UAC prompt when starting the target program. Only this
confirmation grants the requested elevated privileges to the target program.

Starting a program under another standard account can also require UAC presence confirmation. If
xucRun uses an administrator account for this purpose, that account is used only to open the
xucRun dialog securely; the target program continues to run under the selected standard account.

If no suitable approval is active, the xucRun approval dialog appears on a protected private
desktop. Normal, non-elevated programs cannot read or control this dialog. If xucRun cannot open
the dialog there securely, the operation is aborted.

For an executable that is to be started elevated but is writable by the caller, xucRun requires a
protected risk confirmation for every start despite an existing approval. This consent applies
once only and is not stored. If the executable's directory is also writable by the caller, the
elevated start is blocked.

xucRun's central use case is a signed-in standard user for whom exactly one administrator account
has been stored in xucRun and programs are to be started elevated under that account when needed.
After the one-time setup, an invocation without account name and password is sufficient:

```powershell
xucRun.exe regedit.exe
```

xucRun automatically selects the only administrator account for which xucRun credentials are
stored and requests UAC confirmation for the elevated program start. The password does not need
to be entered again. Neither the account name nor the password has to appear in the xucRun
invocation, scripts, or shortcuts.

In general, xucRun supports starting programs under any user account for which valid xucRun
credentials are stored, including both members of the Administrators group and standard users.
Accounts can be selected through a parameter or a dialog. Consecutive user-context switches are
also possible by explicitly starting xucRun as the target program of another xucRun invocation.
Shell-internal commands must be invoked through the corresponding shell.

This makes xucRun particularly suitable for scripts, batch files, and recurring shortcuts. UAC
deliberately remains interactive; **xucRun is not a mechanism for unattended elevation.**

If xucRun credentials are stored for several administrator accounts, xucRun lets the user select
one. A particular account can also be specified directly. The user can choose whether the target
program is to start with or without elevated administrator privileges. If xucRun cannot establish
an explicitly requested elevated start, no program is started.

If a standard user has no administrator-account credentials stored by xucRun, xucRun instead
considers standard accounts for which xucRun credentials are stored and starts the target program
without elevated administrator privileges. If the required credentials are completely missing,
xucRun guides the user through account selection and password entry and then continues the
original invocation.

For each invocation, xucRun guides the user through all required account, credential, and approval
steps. If a target account, stored credentials, or a suitable approval is missing, xucRun requests
the required selection, input, or confirmation directly in the current workflow and then
continues the original invocation. Account selection, management, information, and error dialogs
appear on the normal user desktop. Password entry, approval decisions, and required risk
confirmations instead appear on xucRun's protected private desktop. These dialogs run with
elevated administrator privileges and are isolated from normal, non-elevated programs on the
regular user desktop. Windows UAC remains separate and uses the UAC desktop configured by Windows.

For the first such dialog, xucRun creates a random xucRun security identifier in the format
`XXXX-XXXX` for the calling Windows user account. Password, approval, and risk dialogs on the
private desktop then display this identifier unchanged. After a trusted installation, the user
should remember or securely record it during the first deliberately initiated dialog and confirm
later xucRun dialogs only when it matches. This initial display establishes trust and cannot yet
be compared with an earlier value. xucRun never asks the user to enter this identifier. The
separate Windows UAC window does not display it.

Repeated starts without entering the password again require xucRun to store the target account
credentials. xucRun protects them with Windows security mechanisms. Starting a program under
another account nevertheless requires the credentials to be processed temporarily.

A matching identifier indicates that the dialog originates from the xucRun helper, but it does
not prove that the displayed operation is safe or was initiated by the user. An attacker can still
trigger legitimate dialogs and hope that the user confirms them without sufficient attention.

These measures do not provide complete protection on a compromised system. An attacker can
trigger deceptive or legitimate dialogs and hope that the user confirms them without sufficient
attention. The security identifier helps only if the user actually compares it. xucRun should
therefore be used only as described in the security notice below.

## xucRun scope

xucRun contains:

- the `xucRun.exe` client,
- the demand-start `xucRunBroker` service, which manages credentials protected by xucRun and
  starts programs under the selected user account,
- the Approval Helper,
- shared protocol, serialization, and security components,
- German and English language resources.

xucRun can be built, installed, and used independently.

## Requirements and intended use

xucRun supports Windows 10 and Windows 11 on x64 systems. It requires the current Microsoft
Visual C++ Redistributable v14 for x64.

xucRun is intended for experienced private users and developers on self-managed computers and in
personal development and test systems. It is not a solution for unattended elevation, centralized
fleet management, or systems requiring a particularly high level of protection.

> **Important security notice:** xucRun protects stored credentials using Windows
> security mechanisms. For Windows to start a program under another user account, however, the
> required credentials must be processed temporarily. Complete protection against reading or
> misuse by sufficiently privileged malware or an already compromised system therefore cannot be
> guaranteed. The source code of xucRun is
> disclosed so that the implemented protections, their technical limits, and the remaining
> risks can be understood and independently reviewed. Disclosure alone is neither a security
> guarantee nor evidence of an independent security audit. Use xucRun at your own responsibility.
> xucRun should be used only on trusted, self-managed systems that do not require a particularly
> high level of protection, and preferably with dedicated target accounts having the minimum
> required privileges. To the extent permitted by law, the author and contributors accept
> no liability for damage, data loss, or other consequences arising from the use or incorrect use
> of xucRun, software defects or security vulnerabilities, malware, or a compromised system. This
> includes, in particular, the loss, disclosure, or misuse of credentials. The disclaimer of
> warranty and limitation of liability in the [Apache License 2.0](LICENSE) govern.

## Installation overview

Installation requires a complete xucRun package. All installable package files must be signed
with the same trusted Authenticode certificate. For self-built packages, xucRun includes a script
for local test signing.

### Graphical installation

xucRun is a non-commercial open-source project. The provided installer is currently not
signed by a publicly trusted publisher. Windows may therefore report an unknown publisher when
it is first started.

The easiest installation method is
`xucRun-Installer-<Version>-x64.exe`, for example
`xucRun-Installer-0.9.0.1-x64.exe`. Before starting it, obtain the file only from the trusted
release source, compare its SHA-256 checksum with the checksum published there, and verify any
available Authenticode signature.

When started, Windows User Account Control requests the required administrator rights. The
graphical interface then offers a choice of application language, machine PATH behavior, and
trust model:

- For a package fully signed by the publisher, use the existing package signatures.
- For a self-built package, the installer can configure a non-exportable machine-local
  code-signing certificate and use it to sign the installable files.

The installer also checks Microsoft Visual C++ Runtime and, if necessary, offers either a verified
Microsoft download or an existing `vc_redist.x64.exe`. It installs the client, broker, approval
helper, and language files under `%ProgramFiles%\xucRun`. It also configures the demand-start
broker service and uninstall entry.

Internally, the graphical installer uses the same included PowerShell installation script as the
supported manual installation method. The script verifies the package files and signatures and
sets the selected language. The installation directory can optionally be added to the global
machine PATH.

### Update and uninstall

To update xucRun, start the versioned graphical installer from a fully signed newer package.
Alternatively, run the PowerShell installation script from that package again. Stored xucRun
credentials, approvals, and INI settings not managed by the installer are preserved.

Uninstall through **Installed apps**. Alternatively, no separate uninstallation script is needed:
run the installed `Install-xucRun.ps1` under `%ProgramFiles%\xucRun` with `-Uninstall`:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File "$env:ProgramFiles\xucRun\Install-xucRun.ps1" `
  -Uninstall
```

Both installation methods and the local test-signing instructions are described in detail in the
[installation guide](docs/en/INSTALLATION.md).

The following examples use the short name `xucRun.exe`. This works when
`%ProgramFiles%\xucRun` was added to the global machine PATH during installation. Select this
option in the graphical installer or specify `-AddToSystemPath` during manual installation.
Without the PATH entry, invoke xucRun by its full path, for example
`"%ProgramFiles%\xucRun\xucRun.exe"`.

## Quick start

After installation, xucRun credentials for a target account can be stored explicitly:

```powershell
xucRun.exe /addcred Administrator
```

If the account name is omitted, xucRun first opens a dialog for selecting an enabled local
account. A protected password-entry dialog then appears regardless of whether the account was
specified or selected. Specify `DOMAIN\UserName` for a domain account.

Run programs by specifying their normal path followed by their arguments:

```powershell
xucRun.exe notepad.exe
xucRun.exe regedit.exe
xucRun.exe powershell.exe -NoExit
xucRun.exe cmd.exe /c "net stop MyService"
```

If a suitable account, stored credentials, or an applicable approval is missing, xucRun guides
the user through the required steps during the current invocation.

## Usage examples

Open Device Manager from a standard user account:

```cmd
xucRun.exe "%windir%\System32\mmc.exe" "%windir%\System32\devmgmt.msc"
```

Start an administrative PowerShell script:

```powershell
xucRun.exe powershell.exe -File "C:\Scripts\AdminScript.ps1"
```

Stop a Windows service through `cmd.exe`:

```cmd
xucRun.exe cmd.exe /c "net stop MyService"
```

Create a firewall rule for a program whose path contains spaces:

```cmd
xucRun.exe "%windir%\System32\netsh.exe" advfirewall firewall add rule name="Local example server" dir=in action=allow program="C:\Program Files\ExampleServer\server.exe" enable=yes profile=private
```

Edit a file with a program running under the selected target account:

```powershell
xucRun.exe notepad.exe "C:\Windows\System32\drivers\etc\hosts"
```

Replace the current PowerShell with a PowerShell running in the automatically selected user and
privilege context:

```powershell
PS D:\Example\Project> xucRun.exe
```

For this invocation without parameters, xucRun detects the direct parent shell, closes the
existing console after a successful start, and reopens the same shell in the original working
directory. If the existing PowerShell runs under a standard account and credentials for exactly
one administrator account are stored in xucRun, xucRun selects that account automatically,
requests Windows UAC confirmation, and starts the new PowerShell elevated under that account back
in `D:\Example\Project`. This is the typical primary use case for shell replacement.

If xucRun credentials are stored for several administrator accounts, an account selection dialog
appears instead. If the current account is itself an administrator, xucRun uses that account and
either reuses an already elevated token or requests UAC. Only when no administrator-account
credentials are stored by xucRun for a standard account does xucRun consider standard accounts
for which xucRun credentials are stored and start the replacement shell without elevation. The
working directory is preserved in every case.

The invocations shown above that specify a target program can also be used in scripts and desktop
shortcuts. If xucRun can determine the target account unambiguously and automatically, the
invocation does not need to name an account.

Shell replacement without parameters, in contrast, is intended for interactive invocation from a
supported shell. It should be used in a script only when closing the existing console and
interactively starting a replacement shell is explicitly intended. An invocation without
parameters is not suitable for a normal desktop shortcut.

Supported parent shells are `cmd.exe`, `powershell.exe`, `pwsh.exe`, and `tcc.exe`.

### Desktop shortcut for an elevated Explorer

A desktop shortcut can open an Explorer window for a fixed directory directly under a specific
administrator account for which xucRun credentials are stored and with elevated privileges. For
example, enter the following in the shortcut's **Target** field:

```text
"%ProgramFiles%\xucRun\xucRun.exe" /user-elevated:Administrator "%WINDIR%\explorer.exe" "/root,"D:\Example\TargetFolder""
```

Use `/user-elevated:Administrator` when the shortcut must always select one particular
administrator account for which xucRun credentials are stored and explicitly start Explorer with
elevated privileges. Omit the switch when xucRun should determine the suitable account
automatically. If Explorer should run under a specific different account without elevation being
enforced by xucRun, use `/user:<Account>` instead.

Replace `Administrator` with the required administrator account for which xucRun credentials are
stored and the example path with the directory to open. `/root` makes this directory the root of
the new Explorer window. The nested quotation marks must remain unchanged. Stored credentials do
not replace the interactive Windows UAC confirmation.

### Integrate with Total Commander

xucRun can start a new Total Commander instance under another configured Windows account without
that account already being signed in interactively. The invocation preserves the left path, right
path, and active file panel. This allows file operations to be performed in another account
context from a standard user session. For elevated administrative file operations, the target
account must have administrator privileges and the Windows UAC prompt must be confirmed.

Create a user-defined command in `usercmd.ini`. Replace `<Account>` with the configured account
name and keep the Total Commander variables and quoting unchanged:

```ini
[em_TCStart_Account]
button=%COMMANDER_EXE%
cmd="%ProgramFiles%\xucRun\xucRun.exe"
param="/hidden-monitor "%|ComSpec|" /v:on /c SET "P=%P." & SET "L=%X%P." & SET "R=%X%T." & SET Active=L & (IF "!P!" == "!R!" SET Active=R) & "%|ProgramFiles|\xucRun\xucRun.exe" /user:<Account> "%|COMMANDER_EXE|" "/L=!L!" "/R=!R!" "/P=!Active!""
menu=Launch Total Commander as <Account>
iconic=1
```

The command deliberately invokes xucRun twice. The outer invocation starts the `cmd.exe` hidden
by `/hidden-monitor` according to xucRun's default strategy. The inner invocation processes
`/user:<Account>` and starts the visible Total Commander instance under the target account.
Elevation of the outer intermediate stage is not transferred to the target account; its privileges
remain unchanged.

The intermediate `cmd.exe /v:on /c` first stores the paths supplied by Total Commander in
environment variables and then passes them to the inner xucRun invocation through delayed
variable expansion. This also handles paths containing spaces and special characters reliably.
Comparing the paths additionally determines whether the left or right panel is active.

For example, the command can be added to a custom menu:

```text
POPUP "User"
  MENUITEM "Start Total Commander for <Account>", em_TCStart_Account
END_POPUP
```

Additional accounts can be added in the same way. For each additional account, you can create a
separate command in `usercmd.ini` and a corresponding `MENUITEM`.

## Account selection and privileges

Without a user switch, xucRun uses a suitable current user context or an administrator account
for which xucRun credentials are stored. If exactly one account with matching xucRun
credentials is available, it is selected automatically; if several match, a selection dialog is
shown.

| Invocation | Behavior |
|---|---|
| `xucRun.exe <Program> [Arguments ...]` | determine the account automatically and request elevation when required |
| `xucRun.exe /user <Program> [Arguments ...]` | select an account for which xucRun credentials are stored, without additional elevation by xucRun |
| `xucRun.exe /user:<Account> <Program> [Arguments ...]` | use a specific account without additional elevation by xucRun |
| `xucRun.exe /user-elevated <Program> [Arguments ...]` | select an administrator account for which xucRun credentials are stored and start elevated |
| `xucRun.exe /user-elevated:<Account> <Program> [Arguments ...]` | use a specific account and start elevated |

Stored credentials do not bypass Windows UAC. When elevation is required, UAC confirmation remains
interactive. For a new approval involving an elevated start, the protected approval dialog may
appear first, followed by the UAC dialog for the target program.

## Approvals

Before a start that has not yet been approved, xucRun can request a one-time or reusable approval.
The available durations are:

- this start only,
- 10 minutes,
- one day,
- until sign-out,
- permanently.

An approval can be bound to one program or all programs and to one target account or all stored
accounts of the current Windows user. Program-specific approvals include the SHA-256 hash of the
executable. If the file changes, transferring the approval to the new version requires explicit
confirmation.

An approval does not restrict what the started program can do. General or permanent approvals for
universal interpreters such as `cmd.exe`, PowerShell, or `wscript.exe` are therefore particularly
powerful.

## Programs, arguments, and special characters

The first argument that is not an xucRun switch is the target program. All following arguments
belong exclusively to that program:

```powershell
xucRun.exe cmd.exe /c dir
xucRun.exe /user:Administrator app.exe /config:value
```

Here, `/c` and `/config:value` are not interpreted by xucRun. Shell-internal commands must be run
through their shell, for example `cmd.exe /c dir` or
`powershell.exe -Command Get-Process`.

Enclose program paths and individual arguments containing spaces in quotation marks as usual.
xucRun also supports batch files and carries common shell special characters across the additional
process boundaries. Characters that `cmd.exe`, PowerShell, or another calling shell evaluates
before xucRun starts remain subject to that shell's rules.

## Useful commands

| Command | Purpose |
|---|---|
| `xucRun.exe /addcred [<Account>]` | store credentials |
| `xucRun.exe /deletecred [<Account>]` | select and delete stored credentials |
| `xucRun.exe /showcred` | show accounts for which xucRun credentials are stored |
| `xucRun.exe /showapprovals` | show stored approvals |
| `xucRun.exe /resetapprovals[:<Account>]` | reset approvals |
| `xucRun.exe /info` | show versions, broker status, and current configuration |
| `xucRun.exe /admin-status` | show and manage data for all Windows users with elevation |
| `xucRun.exe /backup` | manage local backups |
| `xucRun.exe /audit` | run security diagnostics |
| `xucRun.exe /enable-broker` | re-enable a disabled broker |
| `xucRun.exe /clear-all` | delete all stored xucRun data |
| `xucRun.exe /help` | show complete help |

`/hidden` hides the directly started target program. `/hidden-monitor` also monitors its exit code
and displays a notice when it fails. Dialogs and child processes are not hidden or monitored by
these options.

## Managing credentials and approvals

`/showcred` lists target accounts for which xucRun credentials are stored, and `/showapprovals`
lists active general and program-specific approvals. Stored passwords are neither displayed nor
returned to the client.

Without an account name, `/deletecred` presents a selection and asks for confirmation before
deletion. Deleting an account also removes approvals bound to that account. Profile-wide approvals
for all accounts remain in place.

Use `/resetapprovals` to select and remove individual or multiple approvals. `/clear-all` is the
administrative full cleanup command. It must be run elevated and, after explicit confirmation,
deletes all xucRun credentials, approvals, security identifiers, and associated metadata for every
Windows user on the computer.

## Backups

`xucRun.exe /backup` opens the interactive local backup manager. The following commands are also
available:

```text
xucRun.exe /backup-create [<Description>]
xucRun.exe /backup-list
xucRun.exe /backup-restore
xucRun.exe /backup-restore:<Backup-ID>
xucRun.exe /backup-delete-all
```

A backup contains stored credentials, approvals, security identifiers, and associated metadata.
Passwords remain protected with DPAPI-NG. A backup can therefore be restored only on the same
computer and is not a portable password export.

Uninstalling xucRun also removes locally stored xucRun backups.

## Troubleshooting

If a problem occurs, `xucRun.exe /info` shows information including the client version, broker
status, current account, and effective configuration. `xucRun.exe /audit` checks security-relevant
properties of the running installation. If the broker service was disabled,
`xucRun.exe /enable-broker` enables it again. `xucRun.exe /help` displays the complete local
command overview.

When `ClientLogDirectory` is configured, the client writes daily files there with names such as
`xucRun_YYYYMMDD.log`. Broker warnings, errors, and basic service events are written to the
Windows Application event log. With `BrokerInformationEvents=true`, additional information events
appear there after the broker service is restarted.

## Configuration file and language

xucRun uses the machine-wide configuration file
`%ProgramFiles%\xucRun\xucRun.ini`. The installer creates it and updates the selected language
during installation or an update without removing the other existing settings. Because the file
is located under `%ProgramFiles%`, administrator privileges are normally required to edit it
manually.

The supported structure is:

```ini
[xucRun]
Language=en

[Logging]
ClientLogDirectory=
BrokerInformationEvents=false
```

- `Language=de` selects German and `Language=en` selects English. The change applies to newly
  started xucRun processes.
- A missing or empty `ClientLogDirectory` disables client file logging. Specifying a path enables
  it. Environment variables are expanded in the context of each starting client. The path must
  be accessible to every client and wrapper account in use, with the permissions required to
  create, append to, and delete log files.
- `BrokerInformationEvents=true` enables additional broker service information events in the
  Windows Application event log. The change applies after the broker service is restarted.
  Warnings, errors, and basic service events remain enabled independently of this setting.

A missing configuration file, invalid language value, or missing or incorrectly signed language
file causes startup to fail for security reasons instead of silently falling back to another
language.

## Reporting vulnerabilities

Do not disclose an unpatched or suspected vulnerability publicly. The confidential reporting
process and supported versions are documented in [SECURITY.md](SECURITY.md).

## Build and install

The [build instructions](docs/en/BUILDING.md) describe how to create an xucRun package from source.
The [installation instructions](docs/en/INSTALLATION.md) cover local test signing, installation,
updates, and uninstall.

## License

xucRun is licensed under the Apache License 2.0. `LICENSE` and `NOTICE` are authoritative.

External code contributions are not accepted at this time. See
[CONTRIBUTING.md](CONTRIBUTING.md) for details.
