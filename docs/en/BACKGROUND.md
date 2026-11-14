<!--
Purpose: Describes the motivation and personal history behind xucRun.
Approach: Preserves the maintainer narrative separately from the task-focused user documentation.
-->
# Background and origins of xucRun

**Language:** English | [Deutsch](../de/BACKGROUND.md)

## A personal note


The starting point for xucRun was a recurring need over many years across several computers in a
private setting, some of which were also used by other people. I generally work with standard
Windows accounts but still need to start individual programs with elevated privileges or under
another account without switching to a separate session or entering the same credentials for
every launch. One central use case is opening a file manager in another account's context from my
existing session; the resulting [configuration examples for Total Commander](USAGE.md#total-commander-integration)
are documented above. Over the years, I combined freely available utilities and built-in Windows
commands in increasingly complex scripts. As this combination no longer fully met my requirements,
I developed a dedicated native solution in C++.

xucRun may not be a tool the world has been waiting for, but for launching applications in a
different user context—with or without elevation—it is something of a Swiss Army knife. As more
and more work went into xucRun, it eventually seemed a shame to keep the tool to myself. I
therefore decided to publish it as a public open-source project on GitHub. That decision, however,
involved substantially more work:
preparing xucRun for use by others and clearly explaining its intended use, internal workings, and
limitations required systematically developing and refining its security model, the management of
stored credentials and program approvals, configuration and diagnostic functionality, user
interfaces and their dialogs, installers, localization, user-oriented documentation, and automated
tests.

A significant part of this effort resulted from the aim of not achieving the additional
convenience through a blanket, broad approval. Instead, users should be able to determine the
balance between convenience and narrow scope for their specific use case and understand the
effect of their selection. This led, among other things, to the differentiated approval dialog
described under [Approval dialog, duration, and scope](SECURITY_MODEL.md#approval-dialog-duration-and-scope).

AI-assisted development tools helped me manage this additional development effort as a sole
developer. I used them in a supporting role across numerous interactive development steps.
Throughout this process, I used successive development versions in my everyday computer work and
continued developing xucRun based on that experience. Only after xucRun had reached what I
considered an appropriate level of maturity in this way did I make the first public version
available. The technical decisions and responsibility for the result remain mine.

The detailed discussion of security considerations may initially appear unusually critical,
particularly when compared with a brief product description. It is, however, a deliberate
consequence of publication: precisely because xucRun processes stored credentials and uses a
privileged service, I considered it my responsibility, when deciding to publish it, to describe
the associated security considerations, the safeguards implemented, and the remaining
limitations as thoroughly and clearly as I can. This level of detail should be
understood neither as a claim that xucRun is less secure than tools with less detailed
documentation nor as an assurance that all risks have been identified or eliminated.

I do not pursue commercial interests with xucRun. Nor do I intend to turn it into a commercial
product and make commitments regarding a particular feature set, absence of defects, continued
availability, support, or future development.

I would be glad if xucRun proved as useful to other individual users as it has been to me.
Feedback and suggestions are welcome. Reports of potential problems help me improve xucRun
further.

— HT64
