# Änderungsprotokoll

**Sprache:** [English](CHANGELOG.md) | Deutsch

Dieses Änderungsprotokoll beschreibt ausschließlich Änderungen, die für Anwender von xucRun
relevant sind. Interne Entwicklungs-, Export- und Veröffentlichungsabläufe werden nicht
aufgeführt.

## [Unveröffentlicht]

## [0.9.0.3] - 2026-10-29

### Hinzugefügt

- `/system [<Anwendung> [Argumente...]]` startet eine lokale Anwendung als `LocalSystem` in der
  interaktiven Sitzung des Aufrufers.
- Der parameterlose `/system`-Aufruf ersetzt eine TCC-, Eingabeaufforderungs-, Windows-PowerShell-
  oder PowerShell-Core-Shell. Der Ersatz übernimmt das Arbeitsverzeichnis und schließt die
  bisherige Shell erst nach erfolgreichem Start der neuen Shell.
- Ein Beispiel für die Total-Commander-`usercmd.ini` startet Total Commander als `LocalSystem` und
  übernimmt beide Panelpfade sowie das aktive Panel.

### Sicherheit

- Jeder `/system`-Start erfordert eine separate, nicht speicherbare Bestätigung im signierten
  xucRun-Freigabehelper auf dem geschützten privaten Desktop. Die Bestätigung zeigt die genaue
  Programmdatei, alle Argumente und das Arbeitsverzeichnis und ist an eine kurzlebige
  Einmal-Challenge gebunden.
- Der Broker sperrt und validiert die aufgelöste Programmdatei und ihren Pfad erneut und hält die
  Sperren, bis der `LocalSystem`-Prozess erzeugt wurde.

## [0.9.0.2] - 2026-10-23

### Hinweis

- Dies ist die erste öffentliche Veröffentlichung von xucRun.
- Erforderlich ist Microsoft Visual C++ Redistributable v14 für x64 ab Version `14.51.36247.0`.
  Der grafische Installer prüft diese Voraussetzung und bietet bei Bedarf den offiziellen
  Microsoft-Download an.

[Unveröffentlicht]: https://github.com/HT64/xucRun/compare/v0.9.0.3...HEAD
[0.9.0.3]: https://github.com/HT64/xucRun/releases/tag/v0.9.0.3
[0.9.0.2]: https://github.com/HT64/xucRun/releases/tag/v0.9.0.2
