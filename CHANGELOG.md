# Changelog

**Language:** English | [Deutsch](CHANGELOG.de.md)

This changelog covers only changes relevant to users of xucRun. Internal development, export,
and publication processes are not listed.

## [Unreleased]

## [0.9.0.3] - 2026-10-29

### Added

- Added `/system [<Application> [Arguments...]]` for starting a local application as
  `LocalSystem` in the caller's interactive session.
- Added parameterless `/system` shell replacement. The replacement preserves the working
  directory and closes the previous shell only after the new shell started successfully.

### Security

- Every `/system` launch requires a separate, non-storable confirmation in the signed xucRun
  approval helper on the protected private desktop. The confirmation shows the exact executable,
  arguments, and working directory and is bound to a short-lived single-use challenge.
- The broker locks and revalidates the resolved executable and its path and holds the locks until
  the `LocalSystem` process has been created.

## [0.9.0.2] - 2026-10-23

### Note

- This is the initial public release of xucRun.
- Microsoft Visual C++ Redistributable v14 for x64 version `14.51.36247.0` or later is required.
  The graphical installer checks this requirement and offers the official Microsoft download if
  necessary.

[Unreleased]: https://github.com/HT64/xucRun/compare/v0.9.0.3...HEAD
[0.9.0.3]: https://github.com/HT64/xucRun/releases/tag/v0.9.0.3
[0.9.0.2]: https://github.com/HT64/xucRun/releases/tag/v0.9.0.2
