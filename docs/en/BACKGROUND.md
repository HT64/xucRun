<!--
Purpose: Describes the motivation and personal history behind xucRun.
Approach: Preserves the maintainer narrative separately from the task-focused user documentation.
-->
# Background and origins of xucRun

**Language:** English | [Deutsch](../de/BACKGROUND.md)

## A personal note

The starting point for xucRun was a recurring need over many years across several computers in a
private setting, some of which were also used by other people. I generally work with standard
Windows accounts but still want to start individual programs with elevated privileges or under
another account without switching to a separate session or entering the same credentials for
every launch. One central use case is opening a file manager in another account's context from my
existing session; this use case led to the configuration examples for Total Commander in the
[user guide](USAGE.md#total-commander-integration). Over the years, I combined freely available
utilities and built-in Windows commands in increasingly complex scripts. But all these solutions
involved compromises. So I decided to address the matter from the ground up and developed a
dedicated native solution in C++.

As more and more work went into xucRun, it eventually seemed a shame to keep the tool to myself. I
therefore decided to publish it as a public open-source project on GitHub. Preparing it for other
users required additional work on security, usability, installation, and documentation.

A significant part of this effort followed from the aim of not achieving the additional
convenience through a blanket, broad approval. Instead, users should be able to determine the
balance between convenience and narrow scope for their specific use case and understand the
effect of their selection. This led, among other things, to the
[differentiated approval dialog with selectable duration and scope](SECURITY_MODEL.md#approval-dialog-duration-and-scope).

AI-assisted development tools helped me manage this additional effort as a sole developer. I used
successive development versions in my everyday computer work and continued developing xucRun based
on that experience. Only after xucRun had reached what I considered an appropriate level of
maturity in this way did I make the first public version available.

xucRun may not be a tool the world has been waiting for, but for launching applications in a
different user context—with or without elevation—it is something of a Swiss Army knife.

Because xucRun processes stored credentials and uses a privileged service, clearly documenting its
security objectives, safeguards, and remaining limitations was particularly important to me.

I develop xucRun as a public open-source project without commercial intent.

I would be glad if xucRun proved as useful to other individual users as it has been to me.
Feedback and suggestions about its use are welcome.

— HT64
