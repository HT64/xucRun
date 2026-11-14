<!--
Purpose: Führt in xucRun ein und leitet durch Installation, erste Nutzung und weiterführende Dokumentation.
Approach: Zeigt den kürzesten sicheren Weg zum ersten Start und verweist danach auf kanonische Fachanleitungen.
-->
# xucRun

**Sprache:** [English](README.md) | Deutsch

**Download:** [Aktuelle Version](https://github.com/HT64/xucRun/releases/latest)

**Dokumentation:** [Installation](docs/de/INSTALLATION.md) |
[xucRun verwenden](docs/de/USAGE.md) | [Referenz](docs/de/REFERENCE.md) |
[Sicherheitsmodell](docs/de/SECURITY_MODEL.md) | [Build-Anleitung](docs/de/BUILDING.md)

**xucRun** steht für **eXecute Under User Context**.

> **Status:** xucRun wird aktiv weiterentwickelt und ist auf Windows-10- und Windows-11-Systemen
> des Autors praktisch erprobt. Weitere Änderungen sind möglich, und Fehler können nicht
> ausgeschlossen werden.

## Überblick

xucRun führt ein Programm, einen Befehl oder ein Skript unter dem dafür ausgewählten Benutzerkonto
aus, während der Anwender in seiner aktuellen Windows-Sitzung bleibt. Das Zielkonto kann ein
Standardkonto sein oder der lokalen Administratorengruppe angehören; im zweiten Fall ist optional
ein Start mit erhöhten Rechten möglich. Der zentrale Anwendungsfall ist ein erfahrener
Windows-Anwender, der normalerweise mit einem Standardkonto arbeitet und eine bestimmte
administrative Aktion ausführen möchte, ohne in eine separate Administratorsitzung zu wechseln
oder bei jedem passenden Aufruf Kontoname und Kennwort erneut einzugeben.

![Mit xucRun unter dem Zielkonto gestartetes Programm in der Windows-Sitzung des angemeldeten Standardbenutzers](docs/de/images/overview.svg)

xucRun hält das angemeldete Konto und das gewählte Zielkonto getrennt. Es macht einen
Standardbenutzer nicht vorübergehend zum Administrator. Programme verwenden Identität,
Berechtigungen und Benutzerprofil des gewählten Zielkontos. Kontowahl und Rechteerhöhung bleiben
getrennte Entscheidungen.

Eine gesonderte Ersteinrichtung ist nicht erforderlich. xucRun fragt Zielkonto, Anmeldedaten,
UAC-Bestätigung und Freigabe ab, sobald sie erstmals benötigt werden. Sind passende Freigaben
vorhanden, genügen im Alltag meist Programm und Argumente. Das
[Sicherheitsmodell](docs/de/SECURITY_MODEL.md) erklärt Kontentrennung, Autorisierungsmodell,
Schutzmaßnahmen und verbleibende Risiken.

## Wann xucRun sinnvoll ist

Wer bereits mit einem Mitglied der lokalen Administratorengruppe arbeitet und Programme nur unter
demselben Konto erhöhen möchte, kann normalerweise Windows-UAC oder Windows Sudo verwenden. xucRun
ist vor allem sinnvoll, wenn das angemeldete Standardkonto und das Konto für administrative
Aktionen getrennt bleiben sollen und wiederkehrende Starts ohne erneute Kennworteingabe möglich
sein sollen, der Anwender aber weiterhin entscheiden möchte, ob nur genau derselbe Aufruf oder
beispielsweise auch andere Argumente erlaubt sind. Kontowahl und Rechteerhöhung bleiben dabei
getrennte Entscheidungen.

xucRun richtet sich an erfahrene Windows-Anwender, die Windows-Konten, UAC und Berechtigungen
verstehen. Der
[Vergleich und die Entscheidungshilfe](docs/de/SECURITY_MODEL.md#vergleich-mit-windows-funktionen-und-anderen-lösungen)
erklären die Unterschiede zu schnellem Benutzerwechsel, UAC, Windows Sudo, `runas.exe /savecred`
und anderen Ansätzen.

## xucRun installieren

1. Das aktuelle x64-Paket oder den grafischen Installer aus der
   [offiziellen Veröffentlichung](https://github.com/HT64/xucRun/releases/latest) herunterladen.
2. Den SHA-256-Hash der heruntergeladenen Datei mit `SHA256SUMS.txt` aus der Veröffentlichung
   vergleichen.
3. `xucRun-Installer-<Version>-x64.exe` starten und die Windows-UAC-Anforderung bestätigen.
4. Optional xucRun durch den Installer in den maschinenweiten PATH aufnehmen lassen, damit
   `xucRun.exe` in interaktiven Befehlen ohne absoluten Pfad funktioniert.

Windows kann beim ersten Start einen unbekannten Herausgeber melden, weil der öffentliche
Installer derzeit keine Signatur eines öffentlich vertrauenswürdigen Herausgebers besitzt. Der
Installer erzeugt oder verwendet ein geschütztes rechnerlokales Zertifikat und stellt damit das
Vertrauen zwischen den installierten xucRun-Komponenten her. xucRun ist nicht portabel: Die
Installation richtet einen Windows-Dienst, geschützte Zugriffsrechte und ein rechnerlokales
Zertifikat für das Vertrauen zwischen den Komponenten ein.

![Grafischer xucRun-Installer](docs/de/images/installer.png)

Die [Installationsanleitung](docs/de/INSTALLATION.md) beschreibt Paketprüfung, Voraussetzungen,
grafische und skriptbasierte Installation, Aktualisierung, Tray-Kompatibilität und Deinstallation.

## Erster Start

Nach der Installation PowerShell oder Eingabeaufforderung öffnen. Der kürzeste typische Aufruf
lautet:

```powershell
xucRun.exe
```

Ohne Argumente ersetzt xucRun eine unterstützte Eltern-Shell durch denselben Shelltyp im
automatisch gewählten Benutzer- und Berechtigungskontext und behält das Arbeitsverzeichnis bei.
Der Hauptanwendungsfall ist der schnelle Wechsel von der aktuellen Shell zu demselben Shelltyp mit
erhöhten Rechten. Das funktioniert mit der Eingabeaufforderung (`cmd.exe`), Windows PowerShell
(`powershell.exe`) und PowerShell 7 (`pwsh.exe`). Der Aufruf enthält weder den Namen eines
Zielkontos noch ein Kennwort.

[Kurzes Demonstrationsvideo öffnen oder herunterladen](docs/media/xucRun-simple-elevation.mp4): Es
zeigt den Wechsel von einer Shell ohne erhöhte Rechte zu demselben Shelltyp mit erhöhten Rechten
sowie den erhöhten Start eines einzelnen Programms.

Programme und ihre Argumente können direkt gestartet werden:

```powershell
xucRun.exe powershell.exe -NoExit
xucRun.exe regedit.exe
```

Fehlen noch erforderliche Angaben oder Bestätigungen, fordert xucRun sie bei Bedarf an:

1. ein aktiviertes lokales Zielkonto auswählen;
2. dessen Anmeldedaten auf dem getrennten geschützten xucRun-Desktop eingeben;
3. Windows-UAC bestätigen, wenn erhöhte Rechte angefordert werden;
4. Dauer und Umfang der xucRun-Freigabe auswählen.

Bei späteren passenden Aufrufen dürfen gespeicherte Anmeldedaten ohne erneute Kennworteingabe
verwendet werden, wenn die geltende Freigabe dies erlaubt.

| Zielkonto auswählen | Anmeldedaten eingeben | Speicherung bestätigen |
|:---:|:---:|:---:|
| ![Lokales Zielkonto auswählen](docs/de/images/target-user-selection.png) | ![Anmeldedaten eingeben](docs/de/images/credential-entry.png) | ![Gespeicherte Anmeldedaten bestätigen](docs/de/images/credential-stored.png) |

Kontokennzeichnungen unterscheiden Mitglieder der Administratorengruppe (`[A]`), Standardkonten
(`[S]`) und Konten mit unbekannter Mitgliedschaft (`[?]`). Den vollständigen Ablauf und alle
Schalter zur Kontowahl beschreibt
[xucRun verwenden](docs/de/USAGE.md#kontowahl-und-rechtemodus).

## Weitere Beispiele

Die folgenden Beispiele zeigen, wie xucRun Kontowahl, optionale Rechteerhöhung, gespeicherte
Anmeldedaten, Freigaben und Argumentübergabe in einem Aufruf verbindet. Viele der einzeiligen
`cmd`-Beispiele eignen sich mit absoluten Programmpfaden auch gut als Ziele von
Desktopverknüpfungen.

Windows-Verwaltungswerkzeuge aus einer Standardbenutzersitzung mit erhöhten Rechten öffnen:

```cmd
xucRun.exe /user-elevated "C:\Windows\System32\mmc.exe" "C:\Windows\System32\devmgmt.msc"
xucRun.exe /user-elevated "C:\Windows\System32\mmc.exe" "C:\Windows\System32\diskmgmt.msc"
```

Der erste Aufruf öffnet den Gerätemanager, der zweite die Windows-Datenträgerverwaltung. Beide
Aufrufe enthalten weder das Administratorkonto noch dessen Kennwort. `/user-elevated` wählt ein
geeignetes Konto aus der Administratorengruppe und fordert erhöhte Rechte an; xucRun behandelt die
jeweilige `.msc`-Datei als Argument der ausdrücklich angegebenen `mmc.exe`. Der Schalter ist nicht
erforderlich, wenn die automatische Kontowahl ohnehin eindeutig ein geeignetes Konto aus der
Administratorengruppe bestimmt und erhöht startet.

PowerShell unter einem für diesen Start ausgewählten Konto öffnen:

```cmd
"C:\Program Files\xucRun\xucRun.exe" /user:* "C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe" -NoExit
```

`/user:*` öffnet immer die Auswahl aktivierter lokaler Konten und startet PowerShell ohne
zusätzliche Rechteerhöhung durch xucRun unter dem gewählten Konto. Fehlende Anmeldedaten werden vor
dem Start angefordert.

[Thunderbird](https://www.thunderbird.net/de/) aus PowerShell mit Profil und Identität des
Windows-Kontos `MailUser` ohne
angeforderte Erhöhung starten:

```powershell
& 'C:\Program Files\xucRun\xucRun.exe' `
    /user:MailUser `
    'C:\Program Files\Mozilla Thunderbird\thunderbird.exe'
```

Dieses Beispiel wechselt bewusst das Windows-Konto, ohne eine Rechteerhöhung anzufordern.
Thunderbird verwendet Profil, Einstellungen und gespeicherte Anwendungsanmeldungen des Zielkontos.

Mehrere strukturierte Argumente einschließlich Werten und eines Pfads mit Leerzeichen direkt an
`netsh.exe` übergeben:

```cmd
xucRun.exe /user-elevated "C:\Windows\System32\netsh.exe" advfirewall firewall add rule name="Lokaler Beispielserver" dir=in action=allow program="C:\Program Files\Beispielserver\server.exe" enable=yes profile=private
```

xucRun hält das Zielprogramm und seine Argumente über die erforderlichen Prozessgrenzen hinweg
getrennt; der Aufruf benötigt keine verschachtelte Befehlszeile für eine Werkzeugkette aus
Kontowechsel und Rechteerhöhung.

Auch grafische Programme mit mehreren strukturierten Parametern können direkt gestartet werden,
beispielsweise [Total Commander](https://www.ghisler.com/deutsch.htm) mit vorgegebenen
Panelpfaden:

```cmd
xucRun.exe /user-elevated "C:\totalcmd\TOTALCMD64.EXE" /L="C:\Windows\System32\config" /R="C:\Program Files" /P=L
```

Dasselbe Programm kann stattdessen ohne angeforderte Rechteerhöhung unter dem Windows-Konto
`Robert` mit dessen `%LOCALAPPDATA%`-Verzeichnis geöffnet werden:

```cmd
xucRun.exe /user:Robert "C:\totalcmd\TOTALCMD64.EXE" /L=^%LOCALAPPDATA^% /R="C:\Program Files" /P=L
```

Die Carets verhindern, dass `cmd.exe` `%LOCALAPPDATA%` bereits im Kontext des aufrufenden Kontos
auflöst; xucRun reicht die Variable unverändert durch, und das gestartete Programm wertet sie erst
nach dem Kontowechsel aus. Dieser Aufruf ist für die interaktive Eingabe in `cmd.exe` bestimmt.

Interaktive Befehle können Programmnamen verwenden. Skripte, Verknüpfungen, geplante Aufgaben und
andere fest konfigurierte Aufrufe sollten die tatsächlichen absoluten Pfade von `xucRun.exe` und
dem Zielprogramm verwenden, um Risiken durch die Windows-Programmsuche zu verringern. Weitere
Beispiele, Shell-Ersetzung, Batchdateien, Explorer, Desktopverknüpfungen und eine mögliche
Integration über benutzerdefinierte Befehle in Total Commander stehen unter
[xucRun verwenden](docs/de/USAGE.md). Der
[Vergleich mit anderen Ansätzen](docs/de/SECURITY_MODEL.md#vergleich-mit-windows-funktionen-und-anderen-lösungen)
erklärt die Unterschiede, ohne vorauszusetzen, dass eine Lösung für jeden Anwendungsfall die beste
ist.

## Anmeldedaten und Freigaben

xucRun speichert Anmeldedaten über Windows-APIs und verwendet sie nur, wenn eine passende Freigabe
den angeforderten Start erlaubt. Freigaben können Zielkonto, Programm, Dateihash, Argumente,
Arbeitsverzeichnis und weitere Merkmale mit unterschiedlichen Umfängen und Laufzeiten binden.
Windows-UAC bleibt, soweit erforderlich, ein eigener interaktiver Schritt.

`/showcred` zeigt gespeicherte Zielkonten, `/showapprovals` die aktiven Freigaben. Den vollständigen
Verwaltungsablauf beschreibt
[xucRun verwenden](docs/de/USAGE.md#anmeldedaten-und-freigaben-verwalten), das Autorisierungsmodell
das [Sicherheitsmodell](docs/de/SECURITY_MODEL.md#freigaben-und-windows-uac).

## Wichtige Befehle

| Befehl | Zweck |
|---|---|
| `xucRun.exe [<Programm> [Argumente...]]` | Programm starten oder eine unterstützte Eltern-Shell ersetzen |
| `xucRun.exe /addcred [<Konto>]` | Anmeldedaten optional vor dem ersten Programmstart speichern |
| `xucRun.exe /showcred` | Konten mit gespeicherten xucRun-Anmeldedaten anzeigen |
| `xucRun.exe /showapprovals` | aktive Freigaben anzeigen |
| `xucRun.exe /backup` | lokale Datensicherungen verwalten |
| `xucRun.exe /info` | Version, Signierung, Broker und Konfiguration anzeigen |
| `xucRun.exe /audit` | Sicherheitsdiagnose ausführen |
| `xucRun.exe /help` | vollständige integrierte Hilfe anzeigen |

Die [Befehls- und Konfigurationsreferenz](docs/de/REFERENCE.md) dokumentiert alle Schalter,
erweiterte Starts als LocalSystem oder TrustedInstaller, Exitcodes, Datensicherungen, Konfiguration
und Diagnose.

## Voraussetzungen und Sicherheitshinweise

xucRun unterstützt x64-Ausgaben von Windows 10 und Windows 11 und benötigt Microsoft Visual C++
Redistributable v14 für x64. Es ist für vertrauenswürdige, selbst verwaltete Systeme ohne erhöhten
Schutzbedarf vorgesehen. Nach Möglichkeit sollten dedizierte Zielkonten nur die tatsächlich
benötigten Berechtigungen besitzen.

> **Wichtige Sicherheitshinweise:** xucRun verwendet Windows-APIs zur Speicherung von
> Anmeldedaten. Damit xucRun ein Programm unter einem anderen
> Benutzerkonto starten kann, muss xucRun die benötigten Anmeldedaten jedoch vorübergehend
> verarbeiten. Ein vollständiger Schutz vor dem Auslesen oder Missbrauch durch ausreichend
> privilegierte Malware oder ein bereits kompromittiertes System kann deshalb nicht gewährleistet
> werden. Der Quellcode von xucRun wird
> offengelegt, damit die eingesetzten technischen Maßnahmen, ihre Grenzen und die
> verbleibenden Risiken nachvollzogen und unabhängig geprüft werden können. Die Offenlegung allein
> ist jedoch weder eine Sicherheitsgarantie noch der Nachweis einer unabhängigen
> Sicherheitsprüfung. Der Einsatz erfolgt eigenverantwortlich. xucRun sollte nur auf
> vertrauenswürdigen, selbst verwalteten Systemen ohne erhöhten Schutzbedarf sowie
> möglichst mit dedizierten, minimal berechtigten Zielkonten eingesetzt werden.
> Skripte, Verknüpfungen, geplante Aufgaben und Konfigurationsdateien, die xucRun aufrufen oder
> seine Parameter vorgeben, müssen ebenfalls vertrauenswürdig und vor unbefugten Änderungen
> geschützt sein.
> Diese Sicherheitshinweise erheben keinen Anspruch auf Vollständigkeit; nicht alle Risiken und
> Sicherheitslücken können vorausgesehen oder ausgeschlossen werden.
> Soweit gesetzlich zulässig, übernehmen der Autor und die Mitwirkenden keine Haftung für Schäden,
> Datenverluste oder sonstige Folgen, die aus der Nutzung oder fehlerhaften Anwendung von xucRun,
> aus Softwarefehlern oder Sicherheitslücken sowie durch Malware oder ein kompromittiertes System
> entstehen. Dies umfasst insbesondere den Verlust, die Offenlegung oder den Missbrauch von
> Anmeldedaten. Maßgeblich sind der Gewährleistungsausschluss und die Haftungsbegrenzung der
> [Apache License 2.0](LICENSE).

Das vollständige [Sicherheitsmodell](docs/de/SECURITY_MODEL.md) dokumentiert die
Broker-Vertrauensgrenze, Prüfung von Aufrufer und Startziel, Schutz der Anmeldedaten, Risiken fest
konfigurierter Aufrufe und verbleibende Risiken.

## Optionales Tray

Das separat installierte [xucRun-Tray-Add-on](https://github.com/HT64/xucRun-Tray) bietet
Statusinformationen und schnellen Zugriff auf häufige Verwaltungsfunktionen. xucRun und sein
Broker bleiben ohne installiertes oder laufendes Tray vollständig nutzbar. Core- und Tray-Version
müssen exakt übereinstimmen.

## Projekthintergrund

[Hintergrund und Entstehung](docs/de/BACKGROUND.md) beschreiben, warum xucRun entwickelt wurde,
welche praktischen Anforderungen es geprägt haben und welche Bedeutung die Sicherheitsaspekte
haben.

## Projekt unterstützen

Die Entwicklung kann freiwillig über [Patreon](https://www.patreon.com/xucRun) unterstützt werden.
Eine Unterstützung ist keine Voraussetzung für Download oder Nutzung von xucRun und gewährt keine
exklusive Software, Funktionen, Veröffentlichungen, Downloads, persönlichen Support oder bestimmte
Reaktionszeiten.

## Schwachstellen melden

Ungepatchte oder vermutete Schwachstellen dürfen nicht öffentlich gemeldet werden. Der
vertrauliche Meldeweg und die unterstützten Versionen stehen in [SECURITY.de.md](SECURITY.de.md).

## Selbst bauen und installieren

Die [Buildanleitung](docs/de/BUILDING.md) beschreibt die Erzeugung eines xucRun-Pakets aus dem
Quellcode. Die [Installationsanleitung](docs/de/INSTALLATION.md) behandelt dessen Installation. Ist
die normale Deinstallation nicht verfügbar, gilt die Anleitung zur
[manuellen Entfernung](docs/de/MANUAL_REMOVAL.md).

## Lizenz

xucRun ist Open Source und wird unter der [Apache License 2.0](LICENSE) angeboten. [NOTICE](NOTICE)
enthält Urheberhinweise.

Externe Codebeiträge werden derzeit nicht angenommen; Einzelheiten stehen in
[CONTRIBUTING.de.md](CONTRIBUTING.de.md).
