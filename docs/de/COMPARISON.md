<!--
Purpose: Vergleicht xucRun mit Windows-Funktionen und anderen Lösungsansätzen.
Approach: Ordnet Kontowechsel, Rechteerhöhung, Anmeldedaten und Freigaben produktneutral ein.
-->
# Vergleich mit Windows-Funktionen und anderen Lösungen

**Sprache:** [English](../en/COMPARISON.md) | Deutsch

Dieses Dokument vergleicht xucRun mit Windows-Funktionen und anderen Lösungsansätzen, die einzelne
Teilaspekte von xucRun abdecken. Sie unterscheiden sich darin, ob und wie sie Kontowechsel,
Sitzungen, Anmeldedaten und Freigaben behandeln.

## Schneller Benutzerwechsel

Beim schnellen Benutzerwechsel bleibt die bisherige Windows-Sitzung mit ihren geöffneten Programmen
erhalten, während sich der Anwender unter einem anderen Benutzerkonto in einer separaten Sitzung
anmeldet oder in dessen bereits geöffnete Sitzung wechselt. Anschließend kann er in die bisherige
Sitzung zurückkehren. Dieser Weg ist auch dann sinnvoll, wenn eine vollständige interaktive
Arbeitsumgebung des anderen Benutzers benötigt wird.

Gegenüber dem schnellen Benutzerwechsel bleibt der Anwender mit xucRun in seiner aktuellen Sitzung
und startet nur das benötigte Programm unter dem gewählten anderen Benutzerkonto, mit oder ohne
erhöhte Rechte.

## Windows-UAC und Windows Sudo

Die Windows-Benutzerkontensteuerung (User Account Control, UAC) ist die Windows-Sicherheitsfunktion,
über die ein Benutzer den Start mit Administratorrechten ausdrücklich bestätigt. Abhängig vom
konkreten Startpfad und der Windows-Konfiguration kann auch ein Kontowechsel mit xucRun eine
UAC-Bestätigung erfordern; das Zusammenspiel beschreibt der Abschnitt
[Verhältnis zwischen xucRun-Freigaben und Windows-UAC](SECURITY_MODEL.md#verhältnis-zwischen-xucrun-freigaben-und-windows-uac).

[Windows Sudo](https://learn.microsoft.com/windows/advanced-settings/sudo/) startet aus einer
nicht erhöhten Konsole einen einzelnen Prozess über Windows-UAC mit Administratorrechten. Die
aktuelle Implementierung setzt dafür ein bereits administratives Konto voraus; ein
Standardbenutzer kann Windows Sudo nicht verwenden, und ein anderes Zielkonto lässt sich derzeit
nicht auswählen. Das verbreitete Unix/Linux-`sudo` kann mit der `sudoers`-Policy erfolgreiche
Authentifizierungen abhängig von
[`timestamp_timeout` und `timestamp_type`](https://www.sudo.ws/docs/man/sudoers.man/#User_Authentication)
zeitlich begrenzt wiederverwenden. Microsoft dokumentiert für Windows Sudo derzeit keinen
entsprechenden Timestamp-Mechanismus; jeder Aufruf führt über UAC.

Für die einmalige Erhöhung aus einer nicht erhöhten Konsole unter einem vorhandenen Konto, das
Mitglied der lokalen Administratorengruppe ist, ist Windows Sudo die Windows-eigene Möglichkeit.
xucRun behandelt zusätzlich den Anwendungsfall, Programme aus einem Standardkonto unter einem
anderen lokalen Konto mit gespeicherten Anmeldedaten und einmaligen oder zeitlich begrenzbaren
Freigaben zu starten. Wenn Windows für den Zielprogrammstart eine UAC-Bestätigung verlangt, bleibt
diese interaktiv.

## `runas.exe /savecred`

Eine weitere Windows-eigene Möglichkeit, die mehr Ähnlichkeit mit xucRun hat, ist `runas.exe`.
Damit lassen sich einzelne Programme gezielt unter einem anderen Benutzerkonto starten. Mit dem
Parameter `/savecred` werden die dafür verwendeten Anmeldedaten nach der ersten Kennworteingabe in
der Windows-Anmeldeinformationsverwaltung des angemeldeten Benutzers gespeichert. Danach kann
`runas.exe` Programme wiederholt unter einem anderen Benutzerkonto starten, ohne das Kennwort
erneut abzufragen. Bei der Verwendung von `runas.exe` bestehen jedoch einige Einschränkungen:

- Das Zielkonto muss bei jedem Aufruf mit `/user:<Konto>` angegeben werden. Sein Name steht damit
  je nach Verwendung in Skripten, Verknüpfungen oder auf der Kommandozeile.
- `runas.exe` fordert keine Erhöhung über UAC an. Abhängig von der Windows-UAC-Konfiguration kann
  ein Konto, das Mitglied der lokalen Administratorengruppe ist, nur mit eingeschränkten Rechten
  starten, solange die Erhöhung nicht gesondert angefordert wird.
- Nach `/savecred` kann das Konto, dessen Anmeldedaten gespeichert wurden, ohne erneute Bestätigung
  für weitere Programmstarts verwendet werden. Dabei gibt es keine obligatorische Freigabe und
  keine Bindung an bestimmte Programme, Dateihashes, Argumente oder Arbeitsverzeichnisse. xucRun
  verlangt dagegen vor der Verwendung gespeicherter Anmeldedaten eine passende ausdrückliche
  Freigabe.
- `runas.exe` wählt nicht automatisch ein geeignetes Konto aus, dessen Anmeldedaten mit
  `/savecred` gespeichert wurden, und bietet keine integrierte Verwaltung unterschiedlich
  weitreichender oder zeitlich begrenzter Freigaben.
- `runas.exe` reagiert empfindlich auf komplexe Programmpfade, Argumente und Sonderzeichen. Bei der
  Verwendung in Skripten oder Verknüpfungen können durch eine zusätzliche Shell oder ein
  Elevationswerkzeug weitere Parsing- und Quoting-Ebenen hinzukommen. Dadurch steigt die
  Fehleranfälligkeit besonders dann, wenn dem erhöht auszuführenden Programm komplexe Argumente
  oder Argumente mit Sonderzeichen übergeben werden müssen.

xucRun greift die Idee gespeicherter und wiederverwendbarer Anmeldedaten von `runas.exe` auf und
ergänzt sie um kontrollierte Freigaben. Es lässt sich in Skripten, Desktop-Verknüpfungen und
anderen fest konfigurierten Aufrufen ebenso wie bei interaktiven Aufrufen über die Kommandozeile
verwenden. Der Start unter einem anderen Benutzerkonto, gespeicherte Anmeldedaten und eine bei
Bedarf weiterhin interaktive Erhöhung über UAC werden dabei in einem Werkzeug zusammengeführt.
Sind passende Anmeldedaten gespeichert, muss der Aufruf weder Zielkonto noch Kennwort enthalten.
Programmpfade, Argumente, Arbeitsverzeichnisse, Batchdateien und typische Shell-Sonderzeichen
werden kontrolliert über die notwendigen Prozessgrenzen hinweg behandelt.

Für den unter [Anwendungsbeispiele](USAGE.md#anwendungsbeispiele) gezeigten Gerätemanager-Aufruf
könnte das schematische Gegenstück mit `runas.exe /savecred` und einem separaten
Elevationswerkzeug beispielsweise so aussehen, wenn dieses Werkzeug die Syntax
`elevate.exe <Programm> [Argumente ...]` unterstützt:

```cmd
runas.exe /savecred /user:Administrator "elevate.exe C:\Windows\System32\mmc.exe C:\Windows\System32\devmgmt.msc"
```

Die genaue Syntax hängt vom gewählten Elevationswerkzeug ab. In der Werkzeugkette muss der
Kontoname im Aufruf stehen und die gesamte nachgelagerte Befehlszeile als ein Argument an
`runas.exe` übergeben werden. xucRun benötigt im Hauptanwendungsfall keinen Kontonamen und hält
Programm und Argumente in einem gemeinsamen Aufruf getrennt.

Ein Beispiel mit komplexeren Argumenten ist das Anlegen einer Firewallregel für ein Programm,
dessen Pfad Leerzeichen enthält:

```cmd
xucRun.exe /user-elevated "C:\Windows\System32\netsh.exe" advfirewall firewall add rule name="Lokaler Beispielserver" dir=in action=allow program="C:\Program Files\Beispielserver\server.exe" enable=yes profile=private
```

Dabei erhält `netsh.exe` mehrere Schlüssel-Wert-Argumente sowie einen Regelnamen und einen
Programmpfad mit Leerzeichen. Eine Werkzeugkette aus `runas.exe` und einem separaten
Elevationswerkzeug wäre daher deutlich komplizierter; ihre zuverlässige Verwendung mit
Sonderzeichen hinge außerdem vom konkreten Elevationswerkzeug und dessen Quoting-Regeln ab.

`runas.exe /savecred` und xucRun verwenden voneinander getrennte, nicht kompatible Speicherungen
für Anmeldedaten. Eine xucRun-Freigabe kann eng an das aufrufende Benutzerkonto, das Zielkonto, die
beteiligten Programme und deren Dateihashes gebunden sowie zeitlich begrenzt werden. Ändert sich
eines der gebundenen Programme, passt dessen SHA-256-Hash nicht mehr zur Freigabe. Vor einer
Übernahme der Freigabe auf die geänderte Programmversion verlangt xucRun eine ausdrückliche
Entscheidung.

## Lösungen anderer Anbieter und Wahl des Ansatzes

Neben den Windows-eigenen Möglichkeiten gibt es Lösungen anderer Anbieter für Benutzerwechsel,
Rechteerhöhungen und die Verwaltung administrativer Zugriffe. Sie setzen je nach Anwendungsfall
andere Schwerpunkte und können zum Beispiel für zentrale Administration, verwaltete
Unternehmensumgebungen oder unbeaufsichtigte Automatisierung besser geeignet sein.
Eine Aufzählung einzelner Produkte wäre zwangsläufig unvollständig und könnte
durch die Auswahl unbeabsichtigt Schwerpunkte setzen. Zudem wäre sie rasch veraltet und wegen
unterschiedlicher Einsatz- und Sicherheitsmodelle nur eingeschränkt vergleichbar.

Die passende Lösung richtet sich nach den jeweiligen Anforderungen.

Wer aus einem Standardbenutzerkonto heraus Programme unter einem anderen Benutzerkonto starten
möchte und bereit ist, dessen Anmeldedaten bei jedem Start erneut einzugeben, kann dafür die
Windows-eigenen Mechanismen verwenden.

Wer dagegen wiederholte Kennworteingaben vermeiden und genauer als mit `runas /savecred`
kontrollieren möchte, welche Programme gespeicherte Anmeldedaten und administrative Rechte
verwenden dürfen, für den bietet xucRun eine frei verfügbare Lösung.
