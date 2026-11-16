<!--
Purpose: Compares xucRun with Windows features and other approaches.
Approach: Provides a product-neutral comparison of account switching, elevation, credentials, and approvals.
-->
# Comparison with Windows features and other solutions

**Language:** English | [Deutsch](../de/COMPARISON.md)

This document compares xucRun with Windows features and other approaches that cover individual
aspects of xucRun. They differ in whether and how they handle account switching, sessions,
credentials, and approvals.

## Fast User Switching

Fast User Switching keeps the current Windows session and its open programs running while the user
signs in to a separate session under another account or switches to that account's existing
session. The user can then return to the previous session. This approach is also useful when the
complete interactive working environment of the other user is required.

In contrast to Fast User Switching, xucRun keeps the user in the current session and starts only
the required program under the selected other account, with or without elevated privileges.

## Windows UAC and Sudo for Windows

Windows User Account Control (UAC) is the Windows security feature through which a user explicitly
confirms a start with administrator privileges. Depending on the specific launch path and Windows
configuration, an account switch with xucRun can also require UAC confirmation; the interaction is
described under
[Relationship between xucRun approvals and Windows UAC](SECURITY_MODEL.md#relationship-between-xucrun-approvals-and-windows-uac).

[Sudo for Windows](https://learn.microsoft.com/windows/advanced-settings/sudo/) starts a single
process with administrator privileges from an unelevated console through Windows UAC. Its current
implementation requires the invoking account to already have administrator privileges; a standard
user cannot use Sudo for Windows, and another target account cannot currently be selected. The
commonly used Unix/Linux `sudo` with the `sudoers` policy can reuse successful authentication for
a period controlled by
[`timestamp_timeout` and `timestamp_type`](https://www.sudo.ws/docs/man/sudoers.man/#User_Authentication).
Microsoft currently documents no corresponding timestamp mechanism for Sudo for Windows; every
invocation goes through UAC.

For one-time elevation from an unelevated console under an existing account that is a member of
the local Administrators group, Sudo for Windows is the native Windows option. xucRun additionally
covers starting programs from a standard account under another local account by using stored
credentials and approvals that can be single-use or time-limited. If Windows requires UAC
confirmation for the target launch, that confirmation remains interactive.

## `runas.exe /savecred`

Another built-in Windows option with more similarity to xucRun is `runas.exe`.
It can start individual programs under another account. With `/savecred`, the credentials used for
this are stored in the signed-in user's Windows Credential Manager after the password is entered
once.
Afterward, `runas.exe` can repeatedly start programs under the other account without asking for
the password again. However, `runas.exe` has several limitations:

- The target account must be specified with `/user:<Account>` on every invocation. Depending on
  usage, its name therefore appears in scripts, shortcuts, or the command line.
- `runas.exe` does not request elevation through UAC. Depending on the Windows UAC configuration,
  an account that is a member of the local Administrators group may start with restricted
  privileges unless elevation is requested separately.
- After `/savecred`, the account whose credentials were stored can be used for further program
  starts without another confirmation. There is no mandatory approval and no binding to
  particular programs, file hashes, arguments, or working directories. xucRun instead requires a
  suitable explicit approval before stored credentials are used.
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

For the Device Manager invocation shown under [Usage examples](USAGE.md#usage-examples), a
schematic equivalent using `runas.exe /savecred` and a separate elevation tool could look as
follows if that tool supports the syntax `elevate.exe <Program> [Arguments ...]`:

```cmd
runas.exe /savecred /user:Administrator "elevate.exe C:\Windows\System32\mmc.exe C:\Windows\System32\devmgmt.msc"
```

The exact syntax depends on the selected elevation tool. In this tool chain, the account name must
appear in the invocation, and the complete downstream command line must be passed to `runas.exe`
as one argument. In its main use case, xucRun does not require an account name and keeps the
program and its arguments separate within one invocation.

A more complex example creates a firewall rule for a program whose path contains spaces:

```cmd
xucRun.exe /user-elevated "C:\Windows\System32\netsh.exe" advfirewall firewall add rule name="Local example server" dir=in action=allow program="C:\Program Files\ExampleServer\server.exe" enable=yes profile=private
```

Here, `netsh.exe` receives several key-value arguments as well as a rule name and a program path
containing spaces. A toolchain consisting of `runas.exe` and a separate elevation tool would
therefore be considerably more complex; its reliable use with special characters would also
depend on the specific elevation tool and its quoting rules.

`runas.exe /savecred` and xucRun use separate, incompatible credential storage. An xucRun approval
can be bound closely to the calling user account, the target account, the participating programs
and their file hashes, and it can be time-limited. If one of the bound programs changes,
its SHA-256 hash no longer matches the approval. xucRun requires an explicit decision before
transferring the approval to the changed program version.

## Third-party solutions and choosing an approach

In addition to native Windows options, third-party solutions are available for switching user
accounts, elevating privileges, and managing administrative access. Depending on the use case,
they focus on different priorities and may be better suited, for example, to centralized
administration, managed enterprise environments, or unattended automation. Listing individual
products would inevitably be incomplete and might unintentionally emphasize certain options
through the selection. Such a list would also quickly become outdated and would allow only
limited comparison because the products use different operational and security models.

The appropriate solution depends on the specific requirements.

Users who want to start programs under another user account from a standard account and are
willing to re-enter that account's credentials for every launch can use the native Windows
mechanisms.

For users who instead want to avoid entering passwords repeatedly while retaining more precise
control than `runas /savecred` provides over which programs may use stored credentials and
administrative privileges, xucRun provides a freely available solution.
