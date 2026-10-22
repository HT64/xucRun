# Änderungsprotokoll

**Sprache:** [English](CHANGELOG.md) | Deutsch

Dieses Änderungsprotokoll beschreibt ausschließlich Änderungen, die für Anwender von xucRun
relevant sind. Interne Entwicklungs-, Export- und Veröffentlichungsabläufe werden nicht
aufgeführt.

## [Unveröffentlicht]

### Geändert

- Der grafische Installer erstellt oder verwendet nun immer das rechnerlokale xucRun-Zertifikat
  und bietet keine unzutreffende Auswahl für Herausgebersignaturen mehr an.
- Bei erneuter Installation derselben Core-Version muss eine passende Tray-Installation nicht
  mehr entfernt werden.
- Installer, `/info` und `/audit` zeigen nun das lokale Signierzertifikat und seinen
  Gültigkeitszeitraum; die Befehlsdiagnose verwendet dessen SHA-256-Fingerabdruck.
- Bei einer vorhandenen Installation zeigt der Installer nun deren registrierte Version und den
  Zeitpunkt der Installation beziehungsweise letzten Aktualisierung an.
- Broker, Approval-Helper und beide Sprach-DLLs enthalten nun vollständige Windows-Dateidetails
  mit Beschreibung, Produktversion und Originaldateiname.
- Installation und Deinstallation bestätigen nun das verwendete beziehungsweise entfernte lokale
  Zertifikat mit Name, Gültigkeitszeitraum und Fingerabdruck. Die Deinstallation entfernt das
  verifizierte xucRun-Standardzertifikat samt privatem Schlüssel und Vertrauenseinträgen.
- Wenn ein installiertes Tray-Add-on oder ein anderer Fehler die Deinstallation blockiert, zeigt
  sie nun einen sichtbaren lokalisierten Fehlerdialog, statt ohne sichtbare Rückmeldung zu enden.
- Eine Reparaturinstallation stellt einen zuvor laufenden Brokerdienst nach dem Dateiaustausch
  wieder her, sodass ein installiertes Tray automatisch in den aktiven Zustand zurückkehrt.
- Der Installer verwendet eine kompaktere Einstellungsansicht und nennt die Möglichkeiten zur
  späteren Änderung der Programmsprache.
- Die interaktive Deinstallation verlangt nun vor dem Löschen gespeicherter Produktdaten eine
  Bestätigung und führt nach Erfolg entfernte Komponenten, Daten und Zertifikatsdetails auf.
- Die Deinstallation über Installierte Apps blendet nun das PowerShell-Fenster aus. Fehler bleiben
  in einem lokalisierten Dialog mit konkretem Diagnoseprotokoll und Ordner-Schaltfläche sichtbar.
- Eine neue deutsch- und englischsprachige Anleitung zur manuellen Entfernung inventarisiert
  xucRun-eigene Dateien, Dienst- und Registry-Einträge, Brokerdaten, Anmeldeinformationen,
  optionale Protokolle, Zertifikatsspeicher und Reste abgebrochener Vorgänge einschließlich einer
  sicheren Entfernungs- und Kontrollreihenfolge.

## [0.9.0.2] - 2026-10-21

### Hinweis

- Dies ist die erste öffentliche Veröffentlichung von xucRun.

[Unveröffentlicht]: https://github.com/HT64/xucRun/compare/v0.9.0.2...HEAD
[0.9.0.2]: https://github.com/HT64/xucRun/releases/tag/v0.9.0.2
