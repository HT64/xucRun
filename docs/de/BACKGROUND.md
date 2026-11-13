<!--
Purpose: Beschreibt Motivation und persönliche Entstehungsgeschichte von xucRun.
Approach: Bewahrt die Maintainer-Erzählung getrennt von der aufgabenorientierten Anwenderdokumentation.
-->
# Hintergrund und Entstehung von xucRun

**Sprache:** [English](../en/BACKGROUND.md) | Deutsch

Dieses Dokument bewahrt den zuvor in der Haupt-README enthaltenen persönlichen Hintergrund, ohne den Installations- und Ersteinstiegspfad zu unterbrechen.

## Persönliche Notiz zur Entstehung von xucRun


Der Ausgangspunkt für xucRun war ein über Jahre wiederkehrender Bedarf auf mehreren Rechnern im
privaten Umfeld, von denen einige auch von anderen Personen genutzt wurden. Ich arbeite
grundsätzlich mit Windows-Standardkonten, muss aber dennoch einzelne Programme mit erhöhten
Rechten oder unter einem anderen Konto starten, ohne dafür in eine separate Sitzung zu wechseln
oder bei jedem Start dieselben Anmeldedaten einzugeben. Ein zentraler Anwendungsfall ist das Öffnen
eines Dateimanagers im Kontext eines anderen Kontos aus meiner bestehenden Sitzung heraus; die
daraus hervorgegangenen
[Konfigurationsbeispiele für Total Commander](USAGE.md#integration-in-total-commander) sind weiter oben
dokumentiert. Über die Jahre verband ich dafür frei verfügbare Tools und Windows-eigene Befehle in
zunehmend komplexen Skripten. Als diese Kombination meinen Anforderungen nicht mehr vollständig
gerecht wurde, entwickelte ich eine eigenständige native Lösung in C++.

xucRun ist zwar kein Werkzeug, auf das die Welt gewartet hat, für das flexible Ausführen von
Anwendungen in einem anderen Benutzerkontext – mit oder ohne Rechteerhöhung – jedoch eine Art
Schweizer Taschenmesser. Nachdem mit der Zeit immer mehr Arbeit in xucRun geflossen war, erschien
es mir zu schade, das Werkzeug nur für mich zu behalten. Deshalb entschied ich mich, es als
öffentliches Open-Source-Projekt auf GitHub zu veröffentlichen. Aus dieser Entscheidung ergab sich
jedoch ein erheblicher zusätzlicher Aufwand: Um xucRun für die
Nutzung durch andere aufzubereiten und seinen Einsatz, seine Funktionsweise sowie
seine Grenzen nachvollziehbar zu erklären, mussten insbesondere das Sicherheitsmodell, die
Funktionen zur Verwaltung gespeicherter Anmeldedaten und Programmfreigaben, die Konfigurations-
und Diagnosefunktionen, die Benutzeroberflächen und ihre Dialoge, die Installer, die Lokalisierung,
eine anwendergerechte Dokumentation und die automatisierten Tests systematisch weiterentwickelt
und ausgearbeitet werden.

Ein wesentlicher Teil dieses Aufwands entstand aus dem Ziel, den zusätzlichen Bedienkomfort nicht
durch eine pauschale, weitreichende Freigabe zu erkaufen. Stattdessen sollen Anwender das
Verhältnis zwischen Bedienkomfort und enger Begrenzung für ihren konkreten Anwendungsfall selbst
festlegen und die Wirkung ihrer Auswahl nachvollziehen können. Daraus entwickelte sich unter
anderem der unter
[Freigabedialog, Dauer und Umfang](SECURITY_MODEL.md#freigabedialog-dauer-und-umfang) beschriebene differenzierte
Freigabedialog.

KI-gestützte Entwicklungswerkzeuge haben mir dabei geholfen, diesen zusätzlichen Entwicklungsaufwand
als Einzelentwickler zu bewältigen. Dabei setzte ich sie in zahlreichen interaktiven
Arbeitsschritten unterstützend ein. Die jeweiligen Entwicklungsstände nutzte ich bei meinen
alltäglichen Tätigkeiten am Rechner selbst und entwickelte xucRun auf Grundlage dieser Erfahrungen
weiter. Erst nachdem xucRun auf diese Weise einen aus meiner Sicht angemessenen Reifegrad erreicht
hatte, stellte ich die erste öffentliche Version bereit.

Die fachlichen Entscheidungen und die Verantwortung für das Ergebnis liegen weiterhin bei mir.

Die ausführliche Darstellung der Sicherheitsaspekte kann auf den ersten Blick ungewöhnlich
kritisch wirken, insbesondere im Vergleich mit einer knappen Produktbeschreibung. Sie ist jedoch
eine bewusste Folge der Veröffentlichung: Gerade weil xucRun gespeicherte Anmeldedaten verarbeitet
und einen privilegierten Dienst verwendet, sah ich mich mit der Entscheidung zur Veröffentlichung
in der Verantwortung, die damit verbundenen Sicherheitsfragen, die getroffenen Schutzmaßnahmen
und die verbleibenden Grenzen so ausführlich und nachvollziehbar wie mir möglich darzustellen.
Diese Ausführlichkeit ist weder als Aussage zu verstehen, xucRun sei unsicherer als weniger
ausführlich dokumentierte Werkzeuge, noch als Zusage, alle Risiken erkannt oder ausgeschlossen zu
haben.

Kommerzielle Interessen verfolge ich mit xucRun nicht. Ebenso beabsichtige ich nicht, daraus ein
kommerzielles Produkt zu machen und dafür einen bestimmten Funktionsumfang, Fehlerfreiheit,
dauerhafte Verfügbarkeit, Support oder Weiterentwicklung zuzusagen.

Ich würde mich freuen, wenn xucRun auch für andere Privatanwender so nützlich ist wie für mich.
Rückmeldungen und Anregungen sind willkommen. Hinweise auf mögliche Probleme helfen mir, xucRun
weiter zu verbessern.
