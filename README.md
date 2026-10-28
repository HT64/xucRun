# xucRun

**Language:** English | [Deutsch](README.de.md)

**Documentation:** [Changelog](CHANGELOG.md) |
[Manual removal](docs/en/MANUAL_REMOVAL.md)

**Download:** [Latest release](https://github.com/HT64/xucRun/releases/latest)

**xucRun** stands for **eXecute Under User Context**.

> **Status:** xucRun is under development. The documentation describes the intended behavior. The
> current state may contain defects, differ from that behavior, and change. The source can be built
> and installed with a local test certificate.

## Motivation and purpose

xucRun covers several use cases in which a program needs to run with elevated privileges or under
another user account, optionally with elevated privileges.

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
rights are also available through Windows itself. xucRun combines these capabilities in one
workflow and adds controlled approvals for stored credentials. Storing and reusing credentials
creates additional security risks. Users who do not want to accept these risks should use native
Windows alternatives such as Fast User Switching instead.

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

xucRun adopts the idea of stored and reusable credentials from `runas.exe` and adds controlled
approvals. It can be used in scripts, desktop shortcuts, other fixed invocations, and interactive
command-line use. Starting a program under another account, stored credentials, and interactive
UAC elevation when required are combined in one tool. When suitable credentials are stored, the
invocation needs neither the target account nor its password. Program paths, arguments, working
directories, batch files, and common shell special characters are handled in a controlled manner
across the required process boundaries.

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
containing spaces. A toolchain consisting of `runas.exe` and a separate elevation tool would
therefore be considerably more complex; its reliable use with special characters would also
depend on the specific elevation tool and its quoting rules.

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
the use case. A list of individual products would inevitably be incomplete and could
unintentionally emphasize particular solutions through the selection itself. It would also
quickly become outdated and allow only limited comparison because their usage and security models
differ.

## Approvals and UAC in xucRun

If the current Windows account is a member of the Administrators group, xucRun uses this account
for the started program by default. If the account is not yet elevated, Windows requests consent
through User Account Control (UAC). In this case, xucRun does not display its own approval dialog
and consent is handled exclusively by Windows UAC.

xucRun program approvals apply to starts using another account's stored credentials. They do not
allow a completely unattended account switch: without elevation, UAC confirms the user's
presence through the xucRun helper on every invocation, after which the target program runs with
the target account's non-elevated privileges. With elevation, UAC instead confirms the target
program start and grants it the requested administrator privileges.

![Windows UAC presence confirmation for the xucRun Approval Helper](docs/en/images/approval-helper-uac.png)

Starting a program under another standard account can also require UAC presence confirmation. If
xucRun uses an administrator account for this purpose, that account is used only to open the
xucRun dialog on the separate Windows desktop; the target program continues to run under the
selected standard account.

If no suitable approval is active, the xucRun approval dialog appears on a separate Windows
desktop. In the intended workflow, Windows separates this desktop from normal, non-elevated
programs. If xucRun cannot open it there, the operation is aborted. An elevated executable
writable by the caller requires a one-time risk confirmation for every start; if its directory is
also writable, xucRun blocks the elevated start.

xucRun's central use case is a signed-in standard user for whom exactly one administrator account
has been stored in xucRun and programs are to be started elevated under that account when needed.
After the one-time setup, an invocation without account name and password is sufficient:

```powershell
xucRun.exe regedit.exe
```

xucRun selects this account automatically and requests UAC confirmation for the elevated start.
The password does not need to be entered again, and neither it nor the account name has to appear
in the invocation, scripts, or shortcuts. Multiple or explicitly specified target accounts and
starts without elevation are described under
[Account selection and privileges](#account-selection-and-privileges). UAC remains interactive;
**xucRun is not a mechanism for unattended elevation.**

If a target account, stored credentials, or a suitable approval is missing, xucRun requests the
required selection, input, or confirmation during the current invocation. Account selection,
management, information, and error dialogs appear on the normal user desktop. Password entry,
approval decisions, and risk confirmations appear on a separate Windows desktop; Windows UAC
separately uses the configured UAC desktop.

For the first such dialog, xucRun creates a random security identifier in the format `XXXX-XXXX`.
Password, approval, and risk dialogs then display it unchanged. After verifying the installation,
the user should remember or securely record it during the first deliberately initiated dialog and
confirm later dialogs only when it matches. The identifier indicates an xucRun helper dialog but
does not prove that an operation is safe or user-initiated. It does not provide complete
protection on a compromised system and helps only when the user actually compares it.

## xucRun scope

xucRun contains:

- the `xucRun.exe` client,
- the `xucRunBroker` service, which starts automatically when needed, manages credentials stored by
  xucRun in the background, and starts programs under the selected user account,
- the Approval Helper,
- shared protocol, serialization, and security components,
- German and English language resources.

xucRun can be built, installed, and used independently.

## Requirements and intended use

xucRun supports Windows 10 and Windows 11 on x64 systems. It requires the current Microsoft
Visual C++ Redistributable v14 for x64. The minimum version required for a release is listed in
the corresponding [changelog](CHANGELOG.md) entry.

For elevated cross-account launches, Windows Admin Approval Mode must be enabled for the selected
administrator account. This particularly affects the built-in `Administrator` account, for which
this mode may be disabled. Otherwise, xucRun blocks the launch because UAC confirmation would not
be possible.

xucRun is intended for experienced private users and developers on self-managed computers and in
personal development and test systems. It is not a solution for unattended elevation, centralized
privilege management, or systems requiring an elevated level of protection.

> **Important security notice:** xucRun uses Windows APIs to store credentials. For xucRun to
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
> To the extent permitted by law, the author and contributors accept
> no liability for damage, data loss, or other consequences arising from the use or incorrect use
> of xucRun, software defects or security vulnerabilities, malware, or a compromised system. This
> includes, in particular, the loss, disclosure, or misuse of credentials. The disclaimer of
> warranty and limitation of liability in the [Apache License 2.0](LICENSE) govern.

## Broker service security model

One possible alternative to a broker service would be for the normal client alone to store the
credentials under cryptographic protection. Without a separate privileged component, decryption
and use would then have to take place in the process and security context of the currently signed-
in user. This could protect the stored data against direct extraction. However, the ability to
decrypt it, the temporarily required clear-text password, and the permission Windows creates
after logon to start processes would be in the same security context as the normal client.

A compromise of that process or its user context could then attempt to misuse the legitimate
decryption function, manipulate the program flow, or read the password or logon permission from
process memory. Cryptographic storage alone does not solve this problem: if the normal client can
decrypt the data, the capability required to do so must be available in its security context. The
responsible component must also reliably decide which user may start which program under which
account, with which arguments, and with which privileges.

xucRun therefore separates this security-sensitive capability into a privileged broker service.
Only the broker stores, decrypts, and uses credentials. It verifies the actual caller and binds
credential use to the user, Windows session, target account, target program and its file hash,
arguments, working directory, and whether the program is to start with or without elevated
administrator privileges. The encrypted stored credentials and their internal associations are
also not provided to the normal client. They reside in storage protected by Windows for the
system account and are additionally bound to that account on the local computer. A normal user
process therefore cannot decrypt them in its own security context.

The broker then performs the Windows logon and permitted program start itself. The normal client
receives neither the stored password nor a permission with which it could start programs under
the target account itself. If a required check fails, no program is started with the stored
credentials; there is no fallback to client-side decryption.

A service running under the Windows `LocalSystem` account has extensive privileges and therefore
itself increases the security-relevant attack surface. xucRun does not use this architecture
because a background service is secure by itself, but to separate use of stored credentials and
decisions about permitted program starts from the normal user process. This additional security
boundary in turn requires systematic protection of the broker.

The broker does not trust identity, path, or approval information merely asserted by a client. It
derives the calling process, user, Windows session, and privilege level from the actual local
connection and from Windows itself. For security-sensitive requests, it checks, among other
properties, the protected installation location, actual program path, file hash, and Windows
digital signature. It also restricts which functions each xucRun component may use. The client in
turn verifies that the other end of its local connection belongs to the registered xucRun
service. Immediately before starting the program, the broker checks the target program and working
directory again. It determines the actual program path and its file hash as well as the actual
working directory itself and keeps the verified Windows objects open until the start. This is
intended to prevent a previously checked target from being replaced unnoticed between approval
and program start.

For each sensitive operation, the broker creates a short-lived security proof that can be used
only once. It applies only to the specific calling process, user, Windows session, target account,
target program and its hash, arguments, working directory, and the requested start with or without
elevated administrator privileges. A copied identifier or a process identity merely asserted by
the client is not sufficient. Different xucRun components also receive only the functions they
require. For example, the optional Tray can retrieve status and list data, but it cannot start
programs, change credentials, or make approval decisions.

The broker and its installed files are additionally hardened against replacement and unintended
extension. Before entering productive operation, the broker checks, among other properties, its
own signature, the authorized client, protected installation paths, and security-relevant file
and directory permissions. It restricts the directories from which additional program components
may be loaded and prohibits certain ways of introducing foreign code at runtime. It also uses
built-in Windows protections that make exploitation of programming errors more difficult. Among
other measures, they prevent data from being executed as program code without further checks,
make the arrangement of process memory less predictable, and make manipulation of the intended
program flow more difficult. Program components from network locations and less trusted security
contexts are blocked. If a programming error causes the broker to access a closed or invalid
Windows object, Windows terminates it immediately instead of allowing it to continue in a
potentially unsafe state. Invalid, incomplete, oversized, or unauthorized requests from an xucRun
component are rejected without continuing the operation. If the broker cannot enable and then
verify one of its required Windows protections, it does not enter service operation.

The broker does not keep stored passwords permanently in clear text. It decrypts a password only
after the checks and required approvals for a specific operation and uses it immediately for the
Windows logon. Memory areas containing the clear-text password and controlled by xucRun are locked
against paging to disk, are not copied unnecessarily, and are overwritten after use. Other memory
areas used by xucRun for sensitive contents are also cleared afterward. If xucRun cannot establish
the required memory protection, the operation is aborted. Automatic Windows Error Reporting
memory dumps are disabled for the broker so that a crash does not unnecessarily place sensitive
memory contents in such a diagnostic file.

When a password is entered for the first time, it must necessarily be present briefly in the
memory of the Windows credential dialog and the verified xucRun helper. The helper runs separately
from the normal client with elevated privileges on its own Windows desktop. In the intended
workflow, Windows separates this desktop from ordinary, non-elevated programs. The helper sends
the password directly to the broker over the authenticated local connection and then overwrites
its own memory areas containing the password. The normal xucRun client does not receive the
password in this workflow either.

The broker is configured as a demand-start Windows service and is not started for status queries
alone. After actual use, it remains active until it is explicitly stopped, Windows shuts down, or
the service fails. While idle, it keeps no stored passwords in clear text and uses practically no
CPU time. An automatic idle stop would not further protect the encrypted stored credentials but
would create additional synchronization and restart paths. The broker can be explicitly stopped
or disabled; re-enabling it requires Windows UAC.

Taken together, these measures reduce the attack surface, make manipulation of the service more
difficult, and limit the consequences of a compromised normal user process. They cannot
completely exclude software defects, unknown vulnerabilities, or system compromise. In
particular, they do not provide a reliable security boundary against local administrators, other
compromised processes already running with system privileges, attacks on the Windows kernel, or
compromised infrastructure used to digitally sign the programs. xucRun therefore uses the broker
as a deliberately chosen and hardened security boundary, not as an absolute security guarantee.

## Installation overview

Installation requires a complete xucRun package. The graphical installer signs all installable
package files consistently on the target computer with a local xucRun certificate.

### Local signing certificate

xucRun uses the certificate so that Windows and xucRun can verify that the installed program
components belong to the same installation and have not been changed or replaced with differently
signed files. The broker accepts security-sensitive requests only from matching signed xucRun
components.

The public package does not contain a shared private key and is not signed with a commercially
issued, publicly trusted publisher certificate. Such a key would allow every recipient to create
files that appear to have the same identity. Instead, the installer creates a unique self-signed
certificate on the target computer or reuses its existing valid xucRun certificate. The private
key is non-exportable. This establishes only local technical trust and does not prove the public
package's origin; therefore, verify the release checksum before starting the installer.

The standard certificate is named `CN=xucRun Local Signing (<COMPUTERNAME>)`. It is stored in
`LocalMachine\My` with its private key and added to `LocalMachine\Root` and
`LocalMachine\TrustedPublisher` for trust on that computer. The verified standard certificate,
including its private key and trust entries, is removed when xucRun is uninstalled.

### Graphical installation

xucRun is a non-commercial open-source project. The provided installer is currently not
signed by a publicly trusted publisher. Windows may therefore report an unknown publisher when
it is first started.

The easiest installation method is
`xucRun-Installer-<Version>-x64.exe`, for example
`xucRun-Installer-0.9.0.2-x64.exe`. Before starting it, obtain the file only from the
[official xucRun releases](https://github.com/HT64/xucRun/releases), compare its SHA-256 checksum
with the checksum published there, and verify any available Authenticode signature.

When started, Windows User Account Control requests the required administrator rights. The
graphical interface then offers a choice of application language and machine PATH behavior. The
installer reuses the existing valid local xucRun certificate or creates a new one and displays its
validity period and fingerprint.

![xucRun graphical installer](docs/en/images/installer.png)

The installer checks the Microsoft Visual C++ Runtime x64. If no compatible version is present,
it offers a verified Microsoft download or an existing `vc_redist.x64.exe` and rejects versions
that are too old. It then installs the client, broker, approval helper, and language files under
`%ProgramFiles%\xucRun` and configures the broker service and uninstall entry. The manual
installation method uses the same included PowerShell script.

### Update and uninstall

xucRun and the separately installed optional
[xucRun Tray add-on](https://github.com/HT64/xucRun-Tray) use an exact four-part version match.
Before changing xucRun versions or uninstalling it, remove xucRun Tray through its own Windows
uninstall entry; afterward, install the Tray release with the same new version. Tray can remain
installed when reinstalling the same matching version.

To update xucRun, start the versioned graphical installer from the newer package. It reuses the
existing valid local xucRun certificate. Stored credentials, approvals, and INI settings not
managed by the installer are preserved.

Uninstall through **Installed apps**. Alternatively, no separate uninstallation script is needed:
run the installed `Install-xucRun.ps1` under `%ProgramFiles%\xucRun` with `-Uninstall`:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File "$env:ProgramFiles\xucRun\Install-xucRun.ps1" `
  -Uninstall
```

> **Warning:** Uninstall permanently deletes all credentials and approvals stored by xucRun for
> every Windows user, together with all local xucRun backups, security markers, and associated
> metadata. It also removes the standard local xucRun certificate, including its private key and
> trust entries.

Before making the first change, uninstall requires explicit confirmation of this permanent
deletion. Cancelling leaves the installation and its data unchanged.

If uninstall fails, xucRun writes a technical diagnostic log under
`%ProgramData%\xucRun\InstallerLogs`; the error dialog shows the specific file path and can open
the log folder. The log contains no stored passwords.

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
account. A password-entry dialog on the separate Windows desktop then appears regardless of
whether the account was specified or selected. Specify `DOMAIN\UserName` for a domain account.

| Credential entry on separate desktop | Stored credential confirmation |
|:---:|:---:|
| ![Credential entry on separate desktop](docs/en/images/credential-entry.png) | ![Stored credential confirmation](docs/en/images/credential-stored.png) |

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
either uses its existing elevated privileges directly or requests UAC. Only when no
administrator-account credentials are stored by xucRun for a standard account does xucRun
consider standard accounts for which xucRun credentials are stored and start the replacement
shell without elevation. The working directory is preserved in every case.

The invocations shown above that specify a target program can also be used in scripts and desktop
shortcuts. If xucRun can determine the target account unambiguously and automatically, the
invocation does not need to name an account.

Scripts, shortcuts, scheduled tasks, and configuration files that contain an xucRun invocation or
determine its target, arguments, or working directory must themselves be trusted and protected
against unauthorized modification. xucRun cannot generally verify their integrity. They should be
writable only by trusted users and should not be stored in shared or insufficiently protected
writable locations.

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

### Explorer context-menu integration

The optional [ExplorerKontextMenue.reg](ExplorerKontextMenue.reg) file contains a complete example
that adds xucRun commands for supported files, folders, folder backgrounds, and drives. For
example, its command entries for executable files and folder backgrounds follow these patterns:

```reg
[HKEY_LOCAL_MACHINE\SOFTWARE\Classes\exefile\shell\xucRun\command]
@="\"C:\\Program Files\\xucRun\\xucRun.exe\" /user-elevated \"%1\""

[HKEY_LOCAL_MACHINE\SOFTWARE\Classes\Directory\Background\shell\xucRunAdmin\command]
@="\"C:\\Program Files\\xucRun\\xucRun.exe\" /user-elevated \"C:\\Windows\\explorer.exe\" \"/root,\"%V\"\""
```

The file is an example rather than part of the installer. It first removes the nine entries it
manages and then recreates them under `HKEY_LOCAL_MACHINE`, so they apply to all users. Importing
it requires administrator rights, assumes that xucRun is installed under
`C:\Program Files\xucRun`, and makes every included xucRun command explicitly require an elevated
administrator account through `/user-elevated`.

These entries use the classic Win32 Explorer context-menu registration under
`Software\Classes\...\shell`. In the standard compact context menu on Windows 11, they normally
appear under **Show more options**. When the classic context menu is used instead, they appear
directly.

### Total Commander integration

The following examples were tested with Total Commander 11.58 (64-bit). They may also work with
other versions, but their compatibility has not been verified. For general information about
Total Commander, visit the [official Total Commander website](https://www.ghisler.com/).

xucRun can start a new elevated Total Commander instance according to its automatic account
strategy. Members of the Administrators group retain their account and are elevated through UAC
when necessary. For a standard user, xucRun uses a stored administrator account. The invocation
preserves the left path, right path, and active file panel:

```ini
[em_TCStart_Elevated]
button=%COMMANDER_PATH%\icons\Tcmadm64000.ico
cmd=%comspec% /v:on /c
param=SET "P=%P." & SET "L=%X%P." & SET "R=%X%T." & SET Active=L & (IF "!P!" == "!R!" SET Active=R) & "%|ProgramFiles|\xucRun\xucRun.exe" "%|COMMANDER_PATH|\TOTALCMD64.EXE" /L="!L!" /R="!R!" /P=!Active!
menu=Start TC elevated through xucRun; administrator members retain their account, standard users switch to an administrator
iconic=1

[em_TCReStart_Elevated]
button=%COMMANDER_PATH%\icons\Tcmadm64000.ico
cmd=cm_Exit,em_TCStart_Elevated
param=
menu=Restart TC with elevated privileges
iconic=1
```

The second command closes the current Total Commander instance and then invokes the first command.
For a standard-user account switch, at least one administrator account must be stored in xucRun.
If several matching administrator accounts are stored, the account selection dialog is shown. Any
required Windows UAC prompt must be confirmed.

xucRun can also start Total Commander under an explicitly selected different Windows account
without that account already being signed in interactively. This allows file operations to be
performed in another account context from a standard user session. For elevated administrative
file operations, the target account must have administrator privileges and the Windows UAC prompt
must be confirmed.

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

The command deliberately invokes xucRun twice so that an administrator account can act as the
central authorized principal for the credentials of additional target accounts. This cascade is
not required for the launch itself; this is a specific design decision about how the credentials
are stored. The example also shows how xucRun can be used in a nested process chain with complex
path and command-line arguments. The outer invocation first starts the `cmd.exe` hidden by
`/hidden-monitor` under that administrator account according to xucRun's default strategy. The
inner xucRun invocation therefore runs with the administrator's SID and can use the credentials
that administrator stored for `<Account>`. This avoids storing the credentials of every additional
target account separately for every standard user; each standard user needs only the credentials
stored for the central administrator account. A single xucRun invocation by the standard user
could instead use only that user's own SID-bound credentials. The inner invocation processes
`/user:<Account>` and starts the visible Total Commander instance under the target account.
Elevation of the outer intermediate stage is not transferred to the target account; its
privileges remain unchanged.

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

| Select an account with stored credentials | Select a local target user |
|:---:|:---:|
| ![Account selection for stored credentials](docs/en/images/target-account-selection.png) | ![Local target user selection](docs/en/images/target-user-selection.png) |

| Invocation | Behavior |
|---|---|
| `xucRun.exe <Program> [Arguments ...]` | determine the account automatically and request elevation when required |
| `xucRun.exe /user <Program> [Arguments ...]` | select an account for which xucRun credentials are stored, without additional elevation by xucRun |
| `xucRun.exe /user:<Account> <Program> [Arguments ...]` | use a specific account without additional elevation by xucRun |
| `xucRun.exe /user-elevated <Program> [Arguments ...]` | select an administrator account for which xucRun credentials are stored and start elevated |
| `xucRun.exe /user-elevated:<Account> <Program> [Arguments ...]` | use a specific account and start elevated |

If no credentials for a suitable administrator account are stored on the first invocation with
`/user-elevated`, xucRun automatically offers administrator-account selection and protected
password entry. After the credentials are stored successfully, the original launch request
continues. `/addcred` remains available for storing credentials before a separate launch request.

Stored credentials do not bypass Windows UAC. When elevation is required, UAC confirmation remains
interactive. For a new approval involving an elevated start, the approval dialog on the separate
desktop may appear first, followed by the UAC dialog for the target program.
For elevated cross-account launches, Windows Admin Approval Mode must be enabled for the selected
administrator account. If Windows instead already provides a fully elevated token, xucRun blocks
the launch because UAC confirmation would not be possible. This can particularly affect the
built-in `Administrator` account.

## Approvals

Before a start that has not yet been approved, xucRun can request a one-time or reusable approval.
The dialog also shows the target program's signature status; xucRun recognizes both embedded
Authenticode signatures and trusted Windows catalog signatures. The available durations are:

- this start only,
- 10 minutes,
- one day,
- until sign-out,
- permanently.

For "This start only," approval scope and target accounts do not apply because the approval applies only to
the currently displayed launch. The corresponding controls are therefore disabled. A reusable
approval can be bound to one program or all programs and to one target account or all stored
accounts of the current Windows user. Program-specific approvals include the SHA-256 hash of the
executable. If the file changes, transferring the approval to the new version requires explicit
confirmation. On the right, the dialog explains the effect of the current selection separately for
approval duration, approval scope, and target accounts.

![Approval after a program hash has changed](docs/en/images/program-hash-change-approval.png)

With **"Exactly this invocation,"** a reusable approval can additionally be bound to the exact
arguments, the working directory, and whether the program is started visibly or hidden. Any change
to these values then requires a new approval. The arguments are not stored in clear text; only
their SHA-256 hash is retained as the binding.

![xucRun approval dialog on separate desktop](docs/en/images/approval-dialog.png)

An approval does not restrict what the started program can do. General or permanent approvals for
universal interpreters such as `cmd.exe`, PowerShell, or `wscript.exe` are therefore particularly
powerful.

For `cmd.exe`, `powershell.exe`, `pwsh.exe`, TCC, and Windows Script Host, the approval dialog
therefore warns about the interpreter's broader capabilities. A normal program approval does not
bind the displayed arguments. Therefore, "Exactly this invocation" is preselected for reusable
approvals involving these shells and interpreters.

If a reusable approval for one of these interpreters is nevertheless changed to "Only for this
target program" or "For all programs," xucRun requires a second explicit confirmation on the
protected desktop. It explains that arguments, script paths, and script contents are not part of
the stored approval and that different commands or new or modified scripts can later start without
another xucRun approval dialog. Returning to the selection is the default. An intentionally stored
broad approval retains this broad effect when it matches a later invocation.

![Confirmation before storing a broad interpreter approval](docs/en/images/broad-interpreter-approval-confirmation.png)

If xucRun recognizes a directly specified PowerShell, batch, TCC, or Windows Script Host script, it
additionally binds the script's fully resolved path and SHA-256 hash, provided that the calling
account cannot modify a folder relevant to replacing the script. The script file itself may be
writable: if its contents change later, its hash no longer matches and xucRun requires a new
approval. Immediately before every launch, the broker rechecks the final path and hash and keeps
the verified file locked against modification and replacement until the program starts. An active
script binding is visible when the direct script's path and SHA-256 hash are shown at the top of
the approval dialog. For PowerShell, a reusable "Exactly this invocation" approval is available
only with such a safely bound direct script. In particular, `-Command`, interactive invocations,
and other forms not recognized as direct `-File` execution can be approved only once or
deliberately with a broader approval scope.

For other interpreters, if no direct script with its path and SHA-256 hash is shown,
"Exactly this invocation" still binds the program file, arguments, working directory, and the
setting for a visible or hidden launch, but no additional program or script contents. Programs,
scripts, and other files named in the command or
invoked by it directly or indirectly can be replaced or modified without invalidating the
approval. Examples include free CMD command text using `SET` and `CALL` and programs subsequently
started by a bound target program or script.

If a folder relevant to replacement can be modified, or if the path or contents cannot be verified
safely, a reusable approval for "Exactly this invocation" is not available. The dialog explains
why and keeps the one-time approval as the default. The user can instead deliberately select a
reusable approval for the interpreter or for all programs. Such a broad approval does not bind
arguments, the script path, or script contents and therefore requires the separate explicit
confirmation described above. Before each individual launch, the broker still rechecks and locks
a recognized direct script; this launch-time check does not bind later invocations to the earlier
script contents. For PowerShell, an administratively enforced `AllSigned` policy can additionally
protect script files, but does not protect against direct or interactive commands.

Calling scripts, shortcuts, and configuration files from which the xucRun invocation is first
generated are still not generally hashed. The approval binds the resulting xucRun invocation, not
the script, shortcut, or configuration file from which it originated. Dynamically loaded scripts,
modules, configuration files, plugins, program inputs, and downstream programs are likewise not
automatically included. Such programs and files must therefore remain trusted and protected
against unauthorized modification. The other broker checks, particularly those for the account,
requested privileges, visibility, target path, and target-program hash, remain effective.

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

For a broker-mediated launch of a `.bat` or `.cmd` file whose file name contains one of the CMD
metacharacters `& | ( ) ! % ^`, the broker creates a temporary wrapper named
`xucRun_wrapper_<128-bit-random-value>.bat`. It stores the wrapper preferably on a suitable volume
named `RamDisk`, or otherwise in the target account's `%TEMP%` directory. The broker protects the
wrapper until the launched process exits and then attempts to delete it, or does so during error
cleanup if the launch fails. If a deletion error or unexpected broker termination leaves it
behind, a local administrator can remove it after it has been released. There is no later
automatic cleanup; the wrapper contains no credentials, stored arguments, or reusable
authorization.

## Useful commands

| Command | Purpose |
|---|---|
| `xucRun.exe /addcred [<Account>]` | store credentials |
| `xucRun.exe /deletecred [<Account>]` | select and delete stored credentials |
| `xucRun.exe /showcred` | show accounts for which xucRun credentials are stored |
| `xucRun.exe /showapprovals` | show stored approvals |
| `xucRun.exe /resetapprovals[:<Account>]` | reset approvals |
| `xucRun.exe /info` | show version, copyright, signing certificate, broker status, and current configuration |
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

`/showcred` lists target accounts for which xucRun credentials are stored. `/showapprovals` lists
active general, target-program, and exact-invocation approvals, with separate columns for scope,
target, invocation binding, and target hash. Stored passwords are neither displayed nor returned
to the client. In each graphical list, the selected row can be deleted directly. The credential
view can also delete all displayed credentials; their account-bound approvals are removed with
them, while profile-wide approvals remain stored. The approval view can delete all displayed
approvals. Each deletion requires a separate confirmation, and the selected-row action remains
disabled until a row is selected.

| Stored credentials | Active approvals |
|:---:|:---:|
| ![Stored credentials](docs/en/images/credentials.png) | ![Active program approvals](docs/en/images/approvals.png) |

Without an account name, `/deletecred` presents a selection and asks for confirmation before
deletion. Deleting an account also removes approvals bound to that account. Profile-wide approvals
for all accounts remain in place.

Use `/resetapprovals` to select and remove individual or multiple approvals. `/clear-all` is the
administrative full cleanup command. It must be run elevated and, after explicit confirmation,
deletes all xucRun credentials, approvals, security identifiers, and associated metadata for every
Windows user on the computer.

| Delete stored credentials | Reset approvals |
|:---:|:---:|
| ![Confirmation before deleting stored credentials](docs/en/images/delete-credential.png) | ![Confirmation before resetting approvals](docs/en/images/reset-approvals.png) |

For a computer-wide overview, `/admin-status` displays the stored xucRun data of all Windows users
and provides the available administrative cleanup actions.

![Administrative status for all users](docs/en/images/administrative-status.png)

## Backups

`xucRun.exe /backup` opens the interactive local backup manager. The following commands are also
available:

![Local backup manager](docs/en/images/data-backups.png)

```text
xucRun.exe /backup-create [<Description>]
xucRun.exe /backup-list
xucRun.exe /backup-restore
xucRun.exe /backup-restore:<Backup-ID>
xucRun.exe /backup-delete-all
```

A backup contains stored credentials, approvals, security identifiers, and associated metadata.
Backups are stored separately from the active credentials and approvals under
`%ProgramData%\xucRun\BrokerBackups`. xucRun uses Windows APIs for storage and binds the backups to
the computer. xucRun restores them only on the same computer; they are not a portable password
export.

Uninstalling xucRun also removes locally stored xucRun backups.

## Troubleshooting

`xucRun.exe /info` shows information including the client version, copyright, signing certificate name,
SHA-256 fingerprint and validity period, broker status, current account, and effective
configuration. `xucRun.exe /audit` checks security-relevant properties of the running installation
and also identifies the verified certificate when the client signature is valid. If xucRun Tray
is installed separately, the audit also checks its installed
files and signatures, uninstall registration, and per-user autostart tasks. If the broker service
was disabled,
`xucRun.exe /enable-broker` enables it again. `xucRun.exe /help` displays the complete local
command overview.

If xucRun reports that an elevated launch was blocked because UAC confirmation was unavailable,
Windows Admin Approval Mode is not effective for the selected different administrator account.
This can particularly affect the built-in `Administrator` account.

![Elevated cross-account launch blocked because UAC confirmation is unavailable](docs/en/images/cross-account-elevation-blocked.png)

| Runtime information | Security audit |
|:---:|:---:|
| ![xucRun runtime information](docs/en/images/runtime-information.png) | ![xucRun security audit](docs/en/images/security-audit.png) |

For additional diagnostics, `ClientLogDirectory` can be configured to enable client log files
named `xucRun_YYYYMMDD.log`. Broker warnings and errors are always written to the Windows
Application event log under the `xucRunBroker` source and can be viewed in Windows Event Viewer
under **Windows Logs → Application**. See
[Configuration file and language](#configuration-file-and-language) for configuration details.

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

Comment lines begin with a semicolon (`;`). Comments should be placed on separate lines rather
than appended to a setting.

- `Language=de` selects German and `Language=en` selects English. The change applies to newly
  started xucRun processes.
- A missing or empty `ClientLogDirectory` disables client file logging. Specifying a path enables
  it. Environment variables are expanded in the context of each starting client. The path must
  be accessible to every client and wrapper account in use, with the permissions required to
  create, append to, and delete log files. Changes apply to newly started client processes. For
  each resolved log directory, xucRun appends to one dated log file per day but does not
  automatically rotate or delete client logs. These logs can contain program paths, account names,
  command lines, and target arguments. Protect the directory accordingly, and do not pass
  passwords or other secrets as command-line arguments.
- The broker always writes warnings, errors, and service and ACL startup messages to the Windows
  Application event log under the `xucRunBroker` source. `BrokerInformationEvents=true`
  additionally logs routine information about pipe request processing there. If the setting is
  missing, empty, `false`, or has any other value, only these additional information events remain
  disabled. Changes take effect after the broker service is restarted.

A missing configuration file, invalid language value, or missing or incorrectly signed language
file causes startup to fail for security reasons instead of silently falling back to another
language.

## Optional Tray

The separately installed optional
[xucRun Tray add-on](https://github.com/HT64/xucRun-Tray) provides a status and control
interface whose icon appears in the notification area of the Windows taskbar next to the clock. It
is a separate project that displays information from xucRun and provides access to existing xucRun
management functions. It does not replace xucRun.

## A personal note on the origins of xucRun

The starting point for xucRun was a recurring need over many years across several computers in a
private setting, some of which were also used by other members of my family. For most tasks on
Windows computers, I use standard user accounts that I deliberately keep out of the Administrators
group. Even so, I want to be able to start individual programs with elevated privileges when
needed, without having to switch to a separate administrator session or enter a username and
password each time. At the same time, every such launch should still require explicit
confirmation, with a single mouse click being sufficient. One central use case for me is being
able to open a file manager in another account's user context from within my existing Windows
session, without switching to that account's separate session. The resulting
[specific configuration examples for Total Commander](#total-commander-integration) are
documented in the corresponding section. Over time, new requirements and features were added,
such as desktop shortcuts for fixed elevated program launches, combined with a user switch when
required, and directly replacing a running shell with one in the appropriate user and privilege
context. Over the years, I used various freely available utilities and built-in Windows commands. I
incorporated these into my own scripts, which became increasingly complex over time. However, this
combination did not fully meet my requirements. This led me to develop a dedicated native solution
in C++.

As more and more work went into what had originally been a tool for my own use, it eventually
seemed a shame to keep it to myself. I therefore decided to publish it as a public open-source
project on GitHub. That decision, however, created a substantial amount of additional work:
preparing xucRun for use by others and clearly explaining its intended use, internal workings, and
limitations required systematically developing and refining its security model, the management of
stored credentials and program approvals, configuration and diagnostic functionality, user
interfaces and their dialogs, installers, localization, documentation, and automated tests.

AI-assisted development tools helped me manage this additional workload as a sole developer. I used
them in a supporting role across numerous interactive development steps. Throughout this process, I
used successive development versions in my everyday computer work and continued developing xucRun
based on that experience. Only after xucRun had reached what I considered an appropriate level of
maturity in this way did I make the first public version available. The technical decisions and
responsibility for the result remain mine.

The detailed discussion of security considerations may initially appear unusually critical,
particularly when compared with a brief product description. It is, however, a deliberate
consequence of publication: precisely because xucRun processes stored credentials and uses a
privileged service, I considered it my responsibility, when deciding to publish it, to describe
the associated security considerations, the safeguards implemented, and the remaining
limitations in detail and in a way that can be understood. This level of detail should be
understood neither as a claim that xucRun is less secure than tools with less detailed
documentation nor as an assurance that all risks have been identified or eliminated.

I do not pursue commercial interests with xucRun. Nor do I intend to turn it into a commercial
product and make commitments regarding a particular feature set, absence of defects, continued
availability, support, or future development.

I would be glad if xucRun proved as useful to other individual users as it has been to me. I would
also be open to maintaining and developing xucRun together with others in the future.

## Support the project

Development can be supported voluntarily on
[Patreon](https://www.patreon.com/xucRun). Contributions can also help cover the usage costs of
AI-assisted development tools. Supporting the project is not required to download or use xucRun
and does not provide exclusive software, features, releases, downloads, personal support, or
specific response times.

## Reporting vulnerabilities

Do not disclose an unpatched or suspected vulnerability publicly. The confidential reporting
process and supported versions are documented in [SECURITY.md](SECURITY.md).

## Build and install

The [build instructions](docs/en/BUILDING.md) describe how to create an xucRun package from source.
The [installation instructions](docs/en/INSTALLATION.md) cover local test signing, installation,
updates, and uninstall. If the normal uninstaller is unavailable, the
[manual removal instructions](docs/en/MANUAL_REMOVAL.md) list all xucRun-owned artifacts and the
safe removal order.

## License

xucRun is licensed under the Apache License 2.0. `LICENSE` and `NOTICE` are authoritative.

External code contributions are not accepted at this time. See
[CONTRIBUTING.md](CONTRIBUTING.md) for details.
