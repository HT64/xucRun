# Änderungsprotokoll

**Sprache:** [English](CHANGELOG.md) | Deutsch

Dieses Änderungsprotokoll beschreibt ausschließlich Änderungen, die für Anwender von xucRun
relevant sind. Interne Entwicklungs-, Export- und Veröffentlichungsabläufe werden nicht
aufgeführt.

## [Unveröffentlicht]

## [0.9.1.6] - 2026-11-10

Diese Version enthält sicherheitsrelevante Korrekturen, die alle früheren Versionen betreffen.
Die behobenen Schwächen wurden bei einer internen Sicherheitsüberprüfung gefunden; Angriffe sind
nicht bekannt. Sie lassen sich nur von Schadcode ausnutzen, der bereits auf demselben Rechner
ausgeführt wird. Eine Aktualisierung wird empfohlen; Installer früherer Versionen sollten nicht
mehr verwendet werden.

### Sicherheit

- Die temporären Arbeitskopien des Installers und des Skripts zum Signieren eines Pakets sind
  jetzt nur noch für Administratoren und SYSTEM zugänglich.
- Ein Weg wurde geschlossen, über den Schadcode im Konto des Benutzers ohne Freigabe eine
  Konsole unter einem gespeicherten Zielkonto öffnen konnte.
- Broker, Client und Installer wurden zusätzlich gegen manipulierte lokale Umgebungen gehärtet.

## [0.9.1.5] - 2026-11-09

### Hinzugefügt

- Der Startmodus `/trustedinstaller` wurde ergänzt. Er startet eine lokale Anwendung oder ersetzt
  die aufrufende Eltern-Shell mit `LocalSystem`-Rechten und der wirksamen Dienstidentität
  `NT SERVICE\TrustedInstaller`. Wie `/system` steht er nur Mitgliedern der lokalen
  Administratorengruppe zur Verfügung, verwendet weder gespeicherte Anmeldedaten noch Freigaben
  und verlangt bei jedem Aufruf eine eigene, nicht speicherbare Bestätigung auf dem geschützten
  privaten Desktop. Der Bestätigungsdialog warnt ausdrücklich vor Änderungen geschützter
  Windows-Dateien und Registrierungseinträge. Kann die TrustedInstaller-Identität nicht
  hergestellt werden, schlägt der Start fehl; es gibt keinen Rückfall auf einen normalen
  `/system`-Start.
- Der Broker meldet jetzt die validierte wirksame Identität verfolgter erhöhter Prozesse
  (erhöhtes Administratorkonto, `LocalSystem` oder TrustedInstaller), damit xucRun Tray sie
  anzeigen kann.

### Geändert

- Wird ein erhöhter kontenübergreifender Start blockiert, weil für das Zielkonto der
  Administratorbestätigungsmodus deaktiviert ist, erklären der Fehlerdialog und der Hinweis in
  der lokalen Kontoauswahl jetzt, wie der Modus aktiviert wird, und zeigen keinen irreführenden
  Windows-Fehlercode mehr an.
- Die README beschreibt die Startmodi für `LocalSystem` und TrustedInstaller jetzt in einem
  gemeinsamen Abschnitt mit Screenshots beider Bestätigungsdialoge und erklärt, warum diese Modi
  weder gespeicherte Anmeldedaten noch Freigaben verwenden.

## [0.9.1.4] - 2026-11-07

### Hinzugefügt

- In der README wurden Demonstrationsvideos ergänzt und angepasst.

### Behoben

- Wenn Windows eingegebene Anmeldedaten zurückweist, zeigt xucRun jetzt den von Windows gemeldeten
  Grund an und öffnet den geschützten Anmeldedatendialog erneut, statt den laufenden Vorgang zu
  beenden.
- Im geschützten Anmeldedatendialog werden das Sicherheitsmerkmal und der Hinweis zur Eingabe der
  Anmeldedaten jetzt korrekt voneinander getrennt.

### Sicherheit

- xucRun weist jetzt Zielargumente mit nicht druckbaren, unsichtbaren oder bidirektionalen
  Steuerzeichen zurück, bevor sie in Freigabe- oder LocalSystem-Bestätigungsdialogen angezeigt
  werden können. Dadurch können diese Zeichen die dargestellten Startdetails nicht optisch
  verändern.

## [0.9.1.3] - 2026-11-04

### Hinzugefügt

- Kontoanzeigen in der Anmeldedaten-, Freigaben- und administrativen Statusverwaltung sowie in der
  lokalen Kontoauswahl kennzeichnen jetzt die Mitgliedschaft in der lokalen Administratorengruppe
  mit `[A]`, Standardkonten mit `[S]` und eine nicht ermittelbare Mitgliedschaft mit `[?]`. Die
  betroffenen Dialoge erläutern diese Kennzeichnungen.
- Die lokale Kontoauswahl warnt jetzt, wenn Windows-UAC deaktiviert ist oder der
  Administratorbestätigungsmodus für das integrierte Konto `Administrator` nicht verfügbar ist
  und dadurch eine kontenübergreifende Erhöhung verhindert würde.

### Geändert

- Die Erfassung von Anmeldedaten zum Öffnen des geschützten Freigabedialogs verwendet jetzt eine
  rollenspezifische Formulierung und unterscheidet dieses Hilfskonto vom Zielkonto. Der Dialog
  erklärt, dass die Anmeldedaten in xucRun gespeichert und beim aktuellen Start zum Öffnen des
  geschützten Freigabedialogs verwendet werden.
- Die Titel der Informations-, Sicherheitsdiagnose-, administrativen Status-, lokalen
  Kontoauswahl- und Freigabedialoge enthalten jetzt den Computernamen. Lokale Konten werden ohne
  redundanten lokalen Rechnerpräfix angezeigt.
- Hinweise in der Anmeldedatenverwaltung passen ihre Höhe jetzt an den lokalisierten Text und die
  aktuelle Fensterbreite an.
- Die Dokumentation wurde neu gegliedert und erweitert, um Kontowahl, Rechteerhöhung, bewusst
  eingeschränkte Zielkonten, Profil- und Ressourcenverhalten, Freigaben, Windows-UAC, Windows Sudo
  und `runas.exe` genauer zu erklären.

## [0.9.1.1] - 2026-11-01

### Hinzugefügt

- In `/showcred` öffnet ein Doppelklick auf ein Zielkonto mit Freigaben jetzt die für dieses Konto
  geltenden Freigaben. Das Kontextmenü bietet dieselbe gefilterte Ansicht und eine kopierbare
  Detailansicht der Anmeldedaten. Die gefilterte Liste enthält Freigaben für das ausgewählte Konto
  sowie profilweite Freigaben, die für alle Zielkonten gelten.
- `/showapprovals` und `/admin-status` bieten jetzt kopierbare Zeilendetails per Doppelklick und
  über **Details anzeigen...** im Kontextmenü. Anmeldedatendetails und Freigaben für ein bestimmtes
  Zielkonto enthalten dessen SID. Freigabedetails enthalten den vollständigen Zielhash, sofern
  vorhanden.

### Geändert

- Zielhashes werden nicht mehr in den Tabellen der Freigaben- und administrativen Statusansicht
  angezeigt. Sie bleiben in den zugehörigen Details verfügbar. Die Detailansichten lassen Felder
  weg, die leer sind oder für den ausgewählten Eintrag nicht gelten.

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

[Unveröffentlicht]: https://github.com/HT64/xucRun/compare/v0.9.1.6...HEAD
[0.9.1.6]: https://github.com/HT64/xucRun/releases/tag/v0.9.1.6
[0.9.1.5]: https://github.com/HT64/xucRun/releases/tag/v0.9.1.5
[0.9.1.4]: https://github.com/HT64/xucRun/releases/tag/v0.9.1.4
[0.9.1.3]: https://github.com/HT64/xucRun/releases/tag/v0.9.1.3
[0.9.1.1]: https://github.com/HT64/xucRun/releases/tag/v0.9.1.1
[0.9.1.0]: https://github.com/HT64/xucRun/releases/tag/v0.9.1.0
[0.9.0.3]: https://github.com/HT64/xucRun/releases/tag/v0.9.0.3
[0.9.0.2]: https://github.com/HT64/xucRun/releases/tag/v0.9.0.2
