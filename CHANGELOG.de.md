# Änderungsprotokoll

**Sprache:** [English](CHANGELOG.md) | Deutsch

Dieses Änderungsprotokoll beschreibt ausschließlich Änderungen, die für Anwender von xucRun
relevant sind. Interne Entwicklungs-, Export- und Veröffentlichungsabläufe werden nicht
aufgeführt.

## [Unveröffentlicht]

## [0.9.1.0] - 2026-11-01

### Hinzugefügt

- `/user:*` und `/user-elevated:*` zeigen immer die Auswahl lokaler Konten. Der Dialog kennzeichnet
  das aktuelle Konto und Konten mit gespeicherten xucRun-Anmeldedaten; bei der Auswahl eines
  anderen Kontos ohne gespeicherte Anmeldedaten wird der ursprüngliche Start nach der
  Passworteingabe auf einem separaten Windows-Desktop fortgesetzt.
- Der Freigabedialog fasst jetzt relevante vorhandene Freigaben für dasselbe Ziel zusammen und
  nennt abweichende Bindungen wie Programmdatei, Zielkonto, Erhöhungsmodus, Argumente,
  Arbeitsverzeichnis, Sichtbarkeit oder direktes Skript. Die Wirkung der aktuellen Auswahl erklärt,
  ob sie eine Freigabe ergänzt, erweitert, ersetzt oder weiterhin von einer vorhandenen Freigabe
  überlagert wird.

### Geändert

- `/user-elevated` verwendet jetzt das aktuelle Konto, wenn es bereits ein Administrator ist, und
  fordert die UAC-Erhöhung nur bei Bedarf an. Andernfalls wählt xucRun ein Administratorkonto mit
  gespeicherten Anmeldedaten aus oder bietet die Erfassung der Anmeldedaten auf einem separaten
  Windows-Desktop an.
- Der Freigabedialog trennt jetzt den vom Broker ermittelten Client- und Kontokontext von den
  scrollbaren Startdetails. Für das Zielprogramm und ein direkt erkanntes Skript zeigt er
  ausführliche Authenticode-Informationen einschließlich Signaturart, Signierer, Aussteller,
  Zertifikatfingerabdruck und -gültigkeit sowie vorhandener Zeitstempelinformationen.
- Die Bestätigung einer geänderten Programmdatei überträgt jetzt sowohl Zielprogrammfreigaben als
  auch Freigaben für den genauen Aufruf auf den neuen Programmhash. Die Bindungen des genauen
  Aufrufs an Argumente, Arbeitsverzeichnis, Sichtbarkeit und direktes Skript bleiben unverändert.
- `/info` und die Informationsansicht des Trays verwenden jetzt ein einheitliches Format für
  Laufzeitinformationen zu Programmidentität, Architektur, Sprachressourcen, Signierzertifikat,
  Konto und Erhöhung, Konfiguration, Wartungsskripten und Clientprotokollierung.

### Sicherheit

- Die Authenticode-Diagnose unterscheidet jetzt eine fehlende Signaturzuordnung von Fehlern
  eingebetteter Signaturen, Kataloge, Zertifikatsketten, Gültigkeit, Sperrprüfung und Richtlinien.
  Die Signaturinformationen ergänzen die bestehenden Prüfungen von finalem Pfad und SHA-256. Diese
  Diagnose stellt nicht fest, dass ein signiertes oder unsigniertes Ziel frei von schädlichem
  Verhalten ist.
- `/audit` meldet jetzt die Ergebnisse von Prüfungen der konfigurierten Windows-Schutzmechanismen
  des laufenden Brokerprozesses sowie von ACL-Schutz, Authenticode-Vertrauen und erwartetem
  Signierer der festgelegten Core- und Tray-Wartungsskripte. Ein erfolgreicher Bericht beschreibt
  den während der Prüfung beobachteten Zustand und ist keine Garantie gegen Fehler oder spätere
  Änderungen.
- Für wiederverwendbare Freigaben eines genauen Aufrufs mit direkt erkanntem Skript darf die
  ACL-Prüfung des Brokers keine unzulässigen Schreib- oder Löschrechte des Clients in der
  Skriptpfadkette feststellen. Die Freigabe bindet finalen Skriptpfad und SHA-256-Hash; der Broker
  validiert und sperrt die gebundene Programm- und Skriptdatei erneut bis zur Prozesserzeugung.
  Später durch den freigegebenen Prozess geladene Programme, Skripte, Module oder andere Dateien
  sind nicht Teil dieser Bindung.

## [0.9.0.3] - 2026-10-29

### Hinzugefügt

- `/system [<Anwendung> [Argumente...]]` startet eine lokale Anwendung als `LocalSystem` in der
  interaktiven Sitzung des Aufrufers.
- Der parameterlose `/system`-Aufruf ersetzt die aktuelle Shell. Der Ersatz übernimmt das
  Arbeitsverzeichnis und schließt die bisherige Shell erst nach erfolgreichem Start der neuen
  Shell.

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

[Unveröffentlicht]: https://github.com/HT64/xucRun/compare/v0.9.1.0...HEAD
[0.9.1.0]: https://github.com/HT64/xucRun/releases/tag/v0.9.1.0
[0.9.0.3]: https://github.com/HT64/xucRun/releases/tag/v0.9.0.3
[0.9.0.2]: https://github.com/HT64/xucRun/releases/tag/v0.9.0.2
