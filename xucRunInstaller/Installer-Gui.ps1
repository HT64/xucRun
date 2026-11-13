<#
.SYNOPSIS
Displays the bilingual graphical xucRun installation workflow.

.DESCRIPTION
Builds a German or English Windows Forms installer UI for selecting language
and system-PATH registration. Its initial language follows the Windows language
preferences of the interactive session user, including when UAC uses a different
administrator account. It displays the bundled license, notices, and security
information and requires their acknowledgment for every installation or update.
A README link is opened through the desktop shell. It displays the
Windows-resolved installation and data paths and inspects the local signing certificate,
installed product, optional Tray compatibility, and Microsoft Visual C++ x64 runtime.
Known Tray conflicts disable installation before any runtime or signing action, while
the machine installation script performs the authoritative compatibility check again.
If necessary, it lets
the user download the runtime from Microsoft's fixed URL or select an existing
redistributable, stages and validates its Microsoft signature, original file
name, and minimum version, and installs it. The workflow then invokes the
package signing script and installation script in hidden Windows PowerShell
processes and reports their results.

.PARAMETER PackageDirectory
Specifies the mandatory directory containing the extracted xucRun package,
including LICENSE, NOTICE, the signing and installation scripts, and payloads.
The workflow may also use this protected directory to stage a downloaded redistributable.

.EXAMPLE
.\xucRunInstaller\Installer-Gui.ps1 -PackageDirectory 'C:\ProgramData\xucRun-Installer-1234'

Opens the installer for an extracted package in a protected staging directory.

.NOTES
Requires elevated Windows PowerShell 5.1, Windows Forms, System.Drawing, and
access to Windows certificate, registry, service, and runtime information.
Completing installation can download or install the Microsoft Visual C++ x64
runtime, create and trust a nonexportable machine-local certificate, sign the
package, install xucRun, configure its service and registry entries, and change
the system PATH when selected. Selected local redistributables are copied into
protected staging before validation and execution; reparse-point inputs are
rejected. Temporary process logs and runtime staging are removed. Exit code 0
means installation completed. Closing or cancelling the UI returns 1223.
Operational failures are shown in the UI and leave it open for retry or
cancellation. Opening the README uses the user's desktop shell and requires
an available desktop and internet connection.
#>
param(
    [Parameter(Mandatory)][string]$PackageDirectory
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
[Windows.Forms.Application]::EnableVisualStyles()

$minimumVcRuntimeVersion = [Version]'14.51.36247.0'
$vcRuntimeDownloadUri = 'https://aka.ms/vs/18/release/vc_redist.x64.exe'

function Get-WtsSessionValue {
    param(
        [Parameter(Mandatory)][int]$SessionId,
        [Parameter(Mandatory)][int]$InformationClass
    )

    if (-not ('Native.WtsSession' -as [type])) {
        Add-Type -Namespace Native -Name WtsSession -MemberDefinition @'
[DllImport("Wtsapi32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
public static extern bool WTSQuerySessionInformation(
    IntPtr server, int sessionId, int informationClass,
    out IntPtr buffer, out int bytesReturned);

[DllImport("Wtsapi32.dll")]
public static extern void WTSFreeMemory(IntPtr buffer);
'@
    }

    $buffer = [IntPtr]::Zero
    $bytesReturned = 0
    if (-not [Native.WtsSession]::WTSQuerySessionInformation(
        [IntPtr]::Zero, $SessionId, $InformationClass,
        [ref]$buffer, [ref]$bytesReturned)) {
        $errorCode = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
        throw [ComponentModel.Win32Exception]::new(
            $errorCode, 'The interactive Windows session could not be queried.')
    }
    try {
        if ($buffer -eq [IntPtr]::Zero -or $bytesReturned -le 2) {
            return ''
        }
        return [Runtime.InteropServices.Marshal]::PtrToStringUni($buffer)
    }
    finally {
        if ($buffer -ne [IntPtr]::Zero) {
            [Native.WtsSession]::WTSFreeMemory($buffer)
        }
    }
}

function Get-CurrentSessionUserSid {
    $sessionId = [Diagnostics.Process]::GetCurrentProcess().SessionId
    $userName = Get-WtsSessionValue -SessionId $sessionId -InformationClass 5
    if ([string]::IsNullOrWhiteSpace($userName)) {
        return $null
    }
    $domainName = Get-WtsSessionValue -SessionId $sessionId -InformationClass 7
    $accountName = if ([string]::IsNullOrWhiteSpace($domainName)) {
        $userName
    }
    else {
        "$domainName\$userName"
    }
    return [Security.Principal.NTAccount]::new($accountName).Translate(
        [Security.Principal.SecurityIdentifier]).Value
}

function Get-InitialInstallerLanguage {
    $sessionUserSid = Get-CurrentSessionUserSid
    if ($sessionUserSid) {
        foreach ($preference in @(
            @{
                SubKey = "$sessionUserSid\Control Panel\Desktop"
                Value = 'PreferredUILanguages'
            },
            @{
                SubKey = "$sessionUserSid\Control Panel\International\User Profile"
                Value = 'Languages'
            }
        )) {
            $key = [Microsoft.Win32.Registry]::Users.OpenSubKey($preference.SubKey)
            if (-not $key) {
                continue
            }
            try {
                $preferredLanguages = @($key.GetValue($preference.Value))
            }
            finally {
                $key.Dispose()
            }
            foreach ($cultureName in $preferredLanguages) {
                if (-not [string]::IsNullOrWhiteSpace($cultureName)) {
                    $culture = [Globalization.CultureInfo]::GetCultureInfo($cultureName)
                    return $(if ($culture.TwoLetterISOLanguageName -eq 'de') {
                        'de'
                    } else {
                        'en'
                    })
                }
            }
        }
    }

    return $(if (
        [Globalization.CultureInfo]::CurrentUICulture.TwoLetterISOLanguageName -eq 'de') {
        'de'
    } else {
        'en'
    })
}
$script:language = Get-InitialInstallerLanguage
$script:statusKey = 'StatusReady'
$script:statusArguments = @()
$script:installInProgress = $false

$texts = @{
    de = @{
        WindowTitle = 'xucRun Installer'
        Heading = 'xucRun installieren'
        Description = 'Installiert xucRun und den Broker nach {0}.'
        SettingsTab = 'Einstellungen'
        DetailsTab = 'Ablauf und Deinstallation'
        SecurityTab = 'Sicherheitshinweise'
        LicenseTab = 'Lizenz'
        LicenseIntroduction = 'xucRun ist Open Source und steht unter der Apache License 2.0. Der vollständige Lizenztext und die Urheberhinweise werden unten angezeigt.'
        Acknowledgment = 'Ich stimme den Lizenzbedingungen zu und habe die Sicherheitshinweise verstanden.'
        ReadmeLink = 'Anwenderdokumentation auf GitHub öffnen'
        ReadmeUrl = 'https://github.com/HT64/xucRun/blob/main/README.de.md'
        ReadmeOpenFailed = 'Die README konnte nicht über den Windows-Desktop geöffnet werden. Öffnen Sie diese Adresse in Ihrem Browser:'
        LicenseReadFailed = 'Lizenztext oder Hinweise konnten nicht gelesen werden. Die Installation kann nicht fortgesetzt werden.'
        KnownFolderFailed = 'Die Windows-Ordner für Programme oder Programmdaten konnten nicht ermittelt werden. Die Installation kann nicht fortgesetzt werden.'
        StatusNotice = @'
xucRun wird aktiv weiterentwickelt. Die Dokumentation beschreibt das vorgesehene Verhalten. Der aktuelle Stand kann Fehler enthalten, davon abweichen und sich ändern.
'@
        SecurityNotice = @'
xucRun verwendet Windows-APIs zur Speicherung von Anmeldedaten. Damit xucRun ein Programm unter einem anderen Benutzerkonto starten kann, muss xucRun die benötigten Anmeldedaten jedoch vorübergehend verarbeiten. Ein vollständiger Schutz vor dem Auslesen oder Missbrauch durch ausreichend privilegierte Malware oder ein bereits kompromittiertes System kann deshalb nicht gewährleistet werden. Der Quellcode von xucRun wird offengelegt, damit die eingesetzten technischen Maßnahmen, ihre Grenzen und die verbleibenden Risiken nachvollzogen und unabhängig geprüft werden können. Die Offenlegung allein ist jedoch weder eine Sicherheitsgarantie noch der Nachweis einer unabhängigen Sicherheitsprüfung. Der Einsatz erfolgt eigenverantwortlich. xucRun sollte nur auf vertrauenswürdigen, selbst verwalteten Systemen ohne erhöhten Schutzbedarf sowie möglichst mit dedizierten, minimal berechtigten Zielkonten eingesetzt werden. Skripte, Verknüpfungen, geplante Aufgaben und Konfigurationsdateien, die xucRun aufrufen oder seine Parameter vorgeben, müssen ebenfalls vertrauenswürdig und vor unbefugten Änderungen geschützt sein.

Diese Sicherheitshinweise erheben keinen Anspruch auf Vollständigkeit; nicht alle Risiken und Sicherheitslücken können vorausgesehen oder ausgeschlossen werden.

Soweit gesetzlich zulässig, übernehmen der Autor und die Mitwirkenden keine Haftung für Schäden, Datenverluste oder sonstige Folgen, die aus der Nutzung oder fehlerhaften Anwendung von xucRun, aus Softwarefehlern oder Sicherheitslücken sowie durch Malware oder ein kompromittiertes System entstehen. Dies umfasst insbesondere den Verlust, die Offenlegung oder den Missbrauch von Anmeldedaten. Maßgeblich sind der Gewährleistungsausschluss und die Haftungsbegrenzung der Apache License 2.0.
'@
        LanguageLabel = 'Programmsprache'
        LanguageHint = 'Später über xucRun.ini, mit dem optionalen Tray-Add-on auch direkt in dessen Menü änderbar.'
        PathCheck = '{0} zum systemweiten PATH hinzufügen'
        RuntimeCompatible = 'Erkannte Version der Microsoft Visual C++ Runtime x64: {0} (kompatibel)'
        RuntimeTooOld = 'Erkannte Version der Microsoft Visual C++ Runtime x64: {0} (zu alt)'
        RuntimeMissing = 'Microsoft Visual C++ Runtime x64: nicht gefunden'
        CertificateGroup = 'Vertrauen und Codesignierung'
        CertificateExisting = "Vorhandenes lokales xucRun-Zertifikat wird wiederverwendet.`r`nGültig seit: {0}   Gültig bis: {1}`r`nFingerabdruck: {2}"
        CertificateNew = 'Bei der Installation wird ein neues lokales xucRun-Zertifikat erstellt.'
        CertificateExpired = 'Das vorhandene lokale xucRun-Zertifikat ist abgelaufen. Bei der Installation wird ein neues Zertifikat erstellt.'
        CertificateAmbiguous = 'Es wurden mehrere verwendbare lokale xucRun-Zertifikate gefunden. Die Installation kann nicht eindeutig fortgesetzt werden.'
        CertificateWarning = 'Der private Schlüssel ist nicht exportierbar und bleibt auf diesem Computer.'
        RepairNotice = "Installierte Version: {0}`r`nInstalliert/zuletzt aktualisiert: {1}`r`nDie Installation wird mit den gewählten Einstellungen repariert."
        UpdateNotice = "Installierte Version: {0}`r`nInstalliert/zuletzt aktualisiert: {1}`r`nDie Installation wird mit den gewählten Einstellungen aktualisiert."
        NewInstallNotice = 'Es wurde keine vorhandene xucRun-Installation erkannt. xucRun wird mit den gewählten Einstellungen installiert.'
        UnknownInstallTime = 'nicht verfügbar'
        TrayNotInstalled = 'xucRun Tray: nicht installiert.'
        TrayCompatible = 'xucRun Tray: Version {0} ist kompatibel und kann bei dieser Core-Installation beibehalten werden.'
        TrayLegacyInstallation = 'xucRun Tray wurde ohne separaten Deinstallationseintrag gefunden. Verwenden Sie den Installer der vollständigen Distribution.'
        TrayVersionMismatch = "xucRun Tray ist nicht mit diesem Core-Paket kompatibel.`r`nRegistrierte Tray-Version: {0}   Tray-Dateiversion: {1}   Core-Paketversion: {2}`r`nDeinstallieren Sie zuerst xucRun Tray."
        TrayIncomplete = 'Die installierte xucRun-Tray-Version ist unvollständig: {0}. Deinstallieren Sie zuerst xucRun Tray.'
        TraySignerUnavailable = 'Das installierte xucRun Tray kann nicht beibehalten werden, weil das bisherige lokale xucRun-Zertifikat nicht eindeutig wiederverwendet werden kann. Deinstallieren Sie zuerst xucRun Tray.'
        TraySignatureInvalid = 'Die installierte xucRun-Tray-Datei besitzt keine gültige vertrauenswürdige Signatur: {0} ({1}). Deinstallieren Sie zuerst xucRun Tray.'
        TraySignerMismatch = 'Die installierte xucRun-Tray-Datei verwendet nicht das lokale Zertifikat, mit dem dieses Core-Paket signiert wird: {0}. Deinstallieren Sie zuerst xucRun Tray.'
        InstallButton = 'Installieren'
        CancelButton = 'Abbrechen'
        StatusReady = 'Bereit.'
        StatusCheckingRuntime = 'Microsoft Visual C++ Runtime wird geprüft ...'
        StatusInstallingRuntime = 'Microsoft Visual C++ Runtime wird installiert ...'
        StatusSigning = 'Lokales Zertifikat und Paketsignaturen werden eingerichtet ...'
        StatusInstalling = 'xucRun und Broker werden installiert ...'
        StatusSuccess = 'Installation erfolgreich abgeschlossen.'
        StatusFailed = 'Installation fehlgeschlagen.'
        InvalidMicrosoftSignature = 'Die ausgewählte Datei besitzt keine gültige Microsoft-Signatur.'
        InvalidRedistributableFile = 'Die ausgewählte Datei ist nicht vc_redist.x64.exe.'
        InvalidRedistributableVersion = "Die ausgewählte Microsoft Visual C++ Runtime x64 ist Version {0}. xucRun benötigt mindestens Version {1}. Wählen Sie eine neuere vc_redist.x64.exe aus."
        UnreadableRedistributableVersion = 'Die Version der ausgewählten Microsoft Visual C++ Runtime x64 konnte nicht ermittelt werden.'
        RuntimeInstallFailed = 'Die Microsoft Visual C++ Runtime konnte nicht installiert werden (Exitcode {0}).'
        RuntimeMissingAfterInstall = 'Die Microsoft Visual C++ Runtime wurde nach der Installation nicht gefunden.'
        RuntimeTooOldAfterInstall = 'Die Microsoft Visual C++ Runtime x64 ist nach der Installation weiterhin Version {0}. xucRun benötigt mindestens Version {1}.'
        RuntimeRestartMessage = 'Die Runtime wurde installiert. Windows benötigt einen Neustart, bevor xucRun verwendet wird.'
        RuntimeRestartTitle = 'Neustart erforderlich'
        RuntimeInstallPrompt = "xucRun benötigt Microsoft Visual C++ Redistributable x64 mindestens in Version {0}.`r`n`r`nJa: Von Microsoft herunterladen und installieren`r`nNein: Vorhandene vc_redist.x64.exe auswählen`r`nAbbrechen: Installation beenden"
        RuntimeUpdatePrompt = "Die installierte Microsoft Visual C++ Runtime x64 muss auf mindestens Version {0} aktualisiert werden.`r`n`r`nJa: Von Microsoft herunterladen und installieren`r`nNein: Vorhandene vc_redist.x64.exe auswählen`r`nAbbrechen: Installation beenden"
        RuntimeMissingTitle = 'Microsoft Visual C++ Runtime fehlt'
        RuntimeUpdateTitle = 'Microsoft Visual C++ Runtime aktualisieren'
        RuntimeDownloadFailed = "Das Redistributable konnte nicht heruntergeladen werden.`r`n{0}"
        RuntimeDialogTitle = 'Microsoft Visual C++ x64 Redistributable auswählen'
        RuntimeDialogFilter = 'VC_redist.x64.exe|vc_redist.x64.exe|Programme (*.exe)|*.exe'
        RuntimeNotSelected = 'Es wurde kein Microsoft Visual C++ Redistributable ausgewählt.'
        ProcessFailed = '{0} (Exitcode {1}).'
        LocalSigningFailed = 'Die lokale Signierung ist fehlgeschlagen'
        InstallationFailed = 'Die Installation ist fehlgeschlagen'
        InstallationSuccessNew = "xucRun und das lokale Zertifikat wurden erfolgreich installiert.`r`n`r`nZertifikat:`r`nName: {0}`r`nGültig seit: {1}`r`nGültig bis: {2}`r`nFingerabdruck: {3}"
        InstallationSuccessExisting = "xucRun wurde erfolgreich installiert. Das vorhandene lokale Zertifikat wurde wiederverwendet.`r`n`r`nZertifikat:`r`nName: {0}`r`nGültig seit: {1}`r`nGültig bis: {2}`r`nFingerabdruck: {3}"
        RuntimeInstalledSuccess = "Microsoft Visual C++ Runtime x64 wurde installiert.`r`nVersion: {0}"
        RuntimeUpdatedSuccess = "Microsoft Visual C++ Runtime x64 wurde aktualisiert.`r`nVersion: {0}"
        Details = @'
INSTALLATION

1. Der Installer benötigt Administratorrechte und entpackt die eingebetteten xucRun-Dateien vorübergehend in ein geschütztes Verzeichnis unter {0}. Nur SYSTEM und Administratoren erhalten Zugriff. Nach Abschluss oder Abbruch versucht der Installer, das Verzeichnis zu entfernen; schlägt dies fehl, zeigt er eine Warnung.

2. xucRun benötigt Microsoft Visual C++ Redistributable x64 mindestens in Version {3}. Fehlt eine passende Version oder ist sie zu alt, kann der Installer die Datei vom offiziellen Microsoft-Link herunterladen oder eine vorhandene vc_redist.x64.exe verwenden. Vor dem Start werden Microsoft-Signatur, Originaldateiname und Version geprüft.

3. Der Installer verwendet ein bereits vorhandenes gültiges lokales xucRun-Codesignaturzertifikat mit privatem Schlüssel. Ist keines vorhanden, erzeugt er ein neues Zertifikat mit nicht exportierbarem privaten Schlüssel. Er richtet das Vertrauen auf diesem Computer ein und signiert die sechs installierbaren Dateien.

4. Das Installationsskript prüft alle Signaturen erneut, installiert Client, Broker, Approval-Helper und Sprachdateien nach {1} und richtet den Broker-Dienst sowie den Deinstallationseintrag ein.

5. Die gewählte Sprache wird in xucRun.ini eingetragen. Der Installationsordner kann zum systemweiten PATH hinzugefügt werden. Bei Abwahl wird nur ein zuvor vom Installer hinzugefügter Eintrag entfernt.

AKTUALISIERUNG

Eine vorhandene xucRun-Installation wird aktualisiert. Bereits gespeicherte xucRun-Anmeldedaten, Freigaben und nicht vom Installer verwaltete INI-Einstellungen bleiben erhalten. Das separat installierte optionale xucRun-Tray-Add-on kann bei einer erneuten Installation derselben Version bestehen bleiben, wenn Version und Signierer weiterhin exakt passen. Vor einem Versionswechsel muss das Tray entfernt werden.

War der Brokerdienst vor der Aktualisierung aktiv, wird er nach dem Dateiaustausch automatisch wieder gestartet. Ein zuvor bewusst gestoppter oder deaktivierter Brokerdienst bleibt gestoppt beziehungsweise deaktiviert.

DEINSTALLATION

Die Deinstallation erfolgt über „Installierte Apps“ beziehungsweise „Programme und Features“. Alternativ kann Install-xucRun.ps1 im Installationsverzeichnis mit -Uninstall ausgeführt werden.

Ein installiertes xucRun Tray muss zuvor separat deinstalliert werden. Beim Start über Installierte Apps bleibt das PowerShell-Fenster verborgen; ein manueller Skriptaufruf behält seine Konsolenausgabe.

Vor der ersten Änderung verlangt die normale Deinstallation eine ausdrückliche Bestätigung. Bei Abbruch bleiben Installation und Daten unverändert. Beim manuellen Aufruf mit -NoSuccessDialog entfallen Bestätigung sowie Erfolgs- und Fehlerdialoge.

ACHTUNG: Die Deinstallation löscht unwiderruflich alle gegebenenfalls vorhandenen xucRun-Anmeldedaten und Freigaben aller Windows-Benutzer sowie sämtliche lokalen xucRun-Sicherungen, Sicherheitsmerkmale und zugehörigen Metadaten. Außerdem entfernt sie Core, Brokerdienst, Programmdateien und das lokale xucRun-Standardzertifikat samt privatem Schlüssel und Vertrauenseinträgen.

Bei der normalen Deinstallation führt die Erfolgsbestätigung entfernte Komponenten, Daten und Zertifikatsdetails auf. Bei einem Fehler wird nach Möglichkeit ein Diagnoseprotokoll unter {2} geschrieben. Der Fehlerdialog zeigt den vollständigen Dateipfad und kann den Protokollordner öffnen; kann das Protokoll nicht geschrieben werden, meldet der Dialog dies. Das Protokoll enthält keine gespeicherten Kennwörter.
'@
    }
    en = @{
        WindowTitle = 'xucRun Installer'
        Heading = 'Install xucRun'
        Description = 'Installs xucRun and its broker in {0}.'
        SettingsTab = 'Settings'
        DetailsTab = 'Process and uninstallation'
        SecurityTab = 'Security information'
        LicenseTab = 'License'
        LicenseIntroduction = 'xucRun is open source and is licensed under the Apache License 2.0. The complete license text and attribution notices are shown below.'
        Acknowledgment = 'I agree to the license terms and understand the security information.'
        ReadmeLink = 'Open user documentation on GitHub'
        ReadmeUrl = 'https://github.com/HT64/xucRun/blob/main/README.md'
        ReadmeOpenFailed = 'The README could not be opened through the Windows desktop. Open this address in your browser:'
        LicenseReadFailed = 'The license text or notices could not be read. Installation cannot continue.'
        KnownFolderFailed = 'The Windows program or program data directory could not be determined. Installation cannot continue.'
        StatusNotice = @'
xucRun is actively maintained and continues to evolve. The documentation describes the intended behavior. The current state may contain defects, differ from that behavior, and change.
'@
        SecurityNotice = @'
xucRun uses Windows APIs to store credentials. For xucRun to start a program under another user account, however, xucRun must process the required credentials temporarily. Complete protection against reading or misuse by sufficiently privileged malware or an already compromised system therefore cannot be guaranteed. The source code of xucRun is disclosed so that the implemented technical measures, their limits, and the remaining risks can be understood and independently reviewed. Disclosure alone is neither a security guarantee nor evidence of an independent security audit. Use xucRun at your own responsibility. xucRun should be used only on trusted, self-managed systems that do not require an elevated level of protection, and preferably with dedicated target accounts having the minimum required privileges. Scripts, shortcuts, scheduled tasks, and configuration files that invoke xucRun or provide its parameters must also be trusted and protected against unauthorized modification.

These security notices are not exhaustive; not all risks and security vulnerabilities can be anticipated or ruled out.

To the extent permitted by law, the author and contributors accept no liability for damage, data loss, or other consequences arising from the use or incorrect use of xucRun, software defects or security vulnerabilities, malware, or a compromised system. This includes, in particular, the loss, disclosure, or misuse of credentials. The disclaimer of warranty and limitation of liability in the Apache License 2.0 govern.
'@
        LanguageLabel = 'Application language'
        LanguageHint = 'Can be changed later in xucRun.ini or, with the optional Tray add-on, directly in its menu.'
        PathCheck = 'Add {0} to the system-wide PATH'
        RuntimeCompatible = 'Detected Microsoft Visual C++ Runtime x64 version: {0} (compatible)'
        RuntimeTooOld = 'Detected Microsoft Visual C++ Runtime x64 version: {0} (too old)'
        RuntimeMissing = 'Microsoft Visual C++ Runtime x64: not found'
        CertificateGroup = 'Trust and code signing'
        CertificateExisting = "The existing local xucRun certificate will be reused.`r`nValid from: {0}   Valid until: {1}`r`nThumbprint: {2}"
        CertificateNew = 'A new local xucRun certificate will be created during installation.'
        CertificateExpired = 'The existing local xucRun certificate has expired. A new certificate will be created during installation.'
        CertificateAmbiguous = 'Multiple usable local xucRun certificates were found. Installation cannot continue unambiguously.'
        CertificateWarning = 'The private key is non-exportable and remains on this computer.'
        RepairNotice = "Installed version: {0}`r`nInstalled/last updated: {1}`r`nThe installation will be repaired with the selected settings."
        UpdateNotice = "Installed version: {0}`r`nInstalled/last updated: {1}`r`nThe installation will be updated with the selected settings."
        NewInstallNotice = 'No existing xucRun installation was detected. xucRun will be installed with the selected settings.'
        UnknownInstallTime = 'not available'
        TrayNotInstalled = 'xucRun Tray: not installed.'
        TrayCompatible = 'xucRun Tray: version {0} is compatible and can remain installed during this Core installation.'
        TrayLegacyInstallation = 'xucRun Tray was found without a separate uninstall entry. Use the full Distribution installer.'
        TrayVersionMismatch = "xucRun Tray is not compatible with this Core package.`r`nRegistered Tray version: {0}   Tray file version: {1}   Core package version: {2}`r`nUninstall xucRun Tray first."
        TrayIncomplete = 'The installed xucRun Tray version is incomplete: {0}. Uninstall xucRun Tray first.'
        TraySignerUnavailable = 'The installed xucRun Tray cannot remain because the previous local xucRun certificate cannot be reused unambiguously. Uninstall xucRun Tray first.'
        TraySignatureInvalid = 'The installed xucRun Tray file does not have a valid trusted signature: {0} ({1}). Uninstall xucRun Tray first.'
        TraySignerMismatch = 'The installed xucRun Tray file does not use the local certificate that will sign this Core package: {0}. Uninstall xucRun Tray first.'
        InstallButton = 'Install'
        CancelButton = 'Cancel'
        StatusReady = 'Ready.'
        StatusCheckingRuntime = 'Checking Microsoft Visual C++ Runtime ...'
        StatusInstallingRuntime = 'Installing Microsoft Visual C++ Runtime ...'
        StatusSigning = 'Configuring the local certificate and package signatures ...'
        StatusInstalling = 'Installing xucRun and the broker ...'
        StatusSuccess = 'Installation completed successfully.'
        StatusFailed = 'Installation failed.'
        InvalidMicrosoftSignature = 'The selected file does not have a valid Microsoft signature.'
        InvalidRedistributableFile = 'The selected file is not vc_redist.x64.exe.'
        InvalidRedistributableVersion = "The selected Microsoft Visual C++ Runtime x64 is version {0}. xucRun requires version {1} or later. Select a newer vc_redist.x64.exe."
        UnreadableRedistributableVersion = 'The version of the selected Microsoft Visual C++ Runtime x64 could not be determined.'
        RuntimeInstallFailed = 'Microsoft Visual C++ Runtime could not be installed (exit code {0}).'
        RuntimeMissingAfterInstall = 'Microsoft Visual C++ Runtime was not found after installation.'
        RuntimeTooOldAfterInstall = 'Microsoft Visual C++ Runtime x64 is still version {0} after installation. xucRun requires version {1} or later.'
        RuntimeRestartMessage = 'The runtime was installed. Windows must be restarted before xucRun can be used.'
        RuntimeRestartTitle = 'Restart required'
        RuntimeInstallPrompt = "xucRun requires Microsoft Visual C++ Redistributable x64 version {0} or later.`r`n`r`nYes: Download and install it from Microsoft`r`nNo: Select an existing vc_redist.x64.exe`r`nCancel: Stop installation"
        RuntimeUpdatePrompt = "The installed Microsoft Visual C++ Runtime x64 must be updated to version {0} or later.`r`n`r`nYes: Download and install it from Microsoft`r`nNo: Select an existing vc_redist.x64.exe`r`nCancel: Stop installation"
        RuntimeMissingTitle = 'Microsoft Visual C++ Runtime is missing'
        RuntimeUpdateTitle = 'Update Microsoft Visual C++ Runtime'
        RuntimeDownloadFailed = "The redistributable could not be downloaded.`r`n{0}"
        RuntimeDialogTitle = 'Select Microsoft Visual C++ x64 Redistributable'
        RuntimeDialogFilter = 'VC_redist.x64.exe|vc_redist.x64.exe|Applications (*.exe)|*.exe'
        RuntimeNotSelected = 'No Microsoft Visual C++ Redistributable was selected.'
        ProcessFailed = '{0} (exit code {1}).'
        LocalSigningFailed = 'Local signing failed'
        InstallationFailed = 'Installation failed'
        InstallationSuccessNew = "xucRun and the local certificate were installed successfully.`r`n`r`nCertificate:`r`nName: {0}`r`nValid from: {1}`r`nValid until: {2}`r`nThumbprint: {3}"
        InstallationSuccessExisting = "xucRun was installed successfully. The existing local certificate was reused.`r`n`r`nCertificate:`r`nName: {0}`r`nValid from: {1}`r`nValid until: {2}`r`nThumbprint: {3}"
        RuntimeInstalledSuccess = "Microsoft Visual C++ Runtime x64 was installed.`r`nVersion: {0}"
        RuntimeUpdatedSuccess = "Microsoft Visual C++ Runtime x64 was updated.`r`nVersion: {0}"
        Details = @'
INSTALLATION

1. The installer requires administrator rights and temporarily extracts the embedded xucRun files to a protected directory below {0}. Only SYSTEM and administrators receive access. After completion or cancellation, it attempts to remove the directory and shows a warning if removal fails.

2. xucRun requires Microsoft Visual C++ Redistributable x64 version {3} or later. If a compatible version is missing or too old, the installer can download it from the official Microsoft link or use an existing vc_redist.x64.exe. The Microsoft signature, original file name, and version are checked before execution.

3. The installer reuses an existing valid local xucRun code-signing certificate with its private key. If none exists, it creates a new certificate with a non-exportable private key. It configures trust on this computer and signs the six installable files.

4. The installation script checks all signatures again, installs the client, broker, approval helper, and language files in {1}, and configures the broker service and uninstall entry.

5. The selected language is written to xucRun.ini. The installation directory can be added to the system-wide PATH. If the option is cleared, only an entry previously added by the installer is removed.

UPDATE

An existing xucRun installation is updated. Stored xucRun credentials, approvals, and INI settings not managed by the installer are preserved. The separately installed optional xucRun Tray add-on can remain during reinstallation of the same version when its version and signer still match exactly. Remove Tray before changing versions.

If the broker service was running before the update, it is restarted automatically after the files have been replaced. A broker service that was deliberately stopped or disabled remains stopped or disabled.

UNINSTALLATION

Uninstall through “Installed apps” or “Programs and Features”. Alternatively, run Install-xucRun.ps1 in the installation directory with -Uninstall.

An installed xucRun Tray must be uninstalled separately first. When started through Installed apps, the PowerShell window remains hidden; a manual script invocation retains its console output.

Normal uninstallation requires explicit confirmation before making its first change. Cancelling leaves the installation and its data unchanged. A manual invocation with -NoSuccessDialog suppresses confirmation as well as success and error dialogs.

WARNING: Uninstall permanently deletes all credentials and approvals that may be stored by xucRun for every Windows user, together with all local xucRun backups, security markers, and associated metadata. It also removes Core, the broker service, program files, and the standard local xucRun certificate, including its private key and trust entries.

During normal uninstallation, the success confirmation lists removed components, data, and certificate details. On failure, a diagnostic log is written under {2} when possible. The error dialog shows the full file path and can open the log folder; if the log cannot be written, the dialog reports this. The log contains no stored passwords.
'@
    }
}

function Get-Text {
    param([Parameter(Mandatory)][string]$Key, [object[]]$Arguments = @())
    $value = $texts[$script:language][$Key]
    if ($Arguments.Count -ne 0) {
        return $value -f $Arguments
    }
    return $value
}

$programFilesDirectory = [Environment]::GetFolderPath(
    [Environment+SpecialFolder]::ProgramFiles)
$programDataDirectory = [Environment]::GetFolderPath(
    [Environment+SpecialFolder]::CommonApplicationData)
if ([string]::IsNullOrWhiteSpace($programFilesDirectory) -or
    [string]::IsNullOrWhiteSpace($programDataDirectory)) {
    [Windows.Forms.MessageBox]::Show(
        (Get-Text -Key KnownFolderFailed), (Get-Text -Key WindowTitle),
        'OK', 'Error') | Out-Null
    exit 1
}
$installDirectory = Join-Path $programFilesDirectory 'xucRun'
$languageDirectory = Join-Path $installDirectory 'Languages'
$installerLogDirectory = Join-Path $programDataDirectory 'xucRun\InstallerLogs'

try {
    $licenseText = [IO.File]::ReadAllText(
        (Join-Path $PackageDirectory 'LICENSE'), [Text.Encoding]::UTF8)
    $noticeText = [IO.File]::ReadAllText(
        (Join-Path $PackageDirectory 'NOTICE'), [Text.Encoding]::UTF8)
}
catch {
    [Windows.Forms.MessageBox]::Show(
        ((Get-Text -Key LicenseReadFailed) + "`r`n`r`n" + $_.Exception.Message),
        (Get-Text -Key WindowTitle), 'OK', 'Error') | Out-Null
    exit 1
}

function Test-CodeSigningEku {
    param([Parameter(Mandatory)]
        [Security.Cryptography.X509Certificates.X509Certificate2]$Certificate)

    foreach ($extension in $Certificate.Extensions) {
        if ($extension -is
            [Security.Cryptography.X509Certificates.X509EnhancedKeyUsageExtension]) {
            foreach ($usage in $extension.EnhancedKeyUsages) {
                if ($usage.Value -eq '1.3.6.1.5.5.7.3.3') {
                    return $true
                }
            }
        }
    }
    return $false
}

function Get-LocalCertificateState {
    $subject = "CN=xucRun Local Signing ($env:COMPUTERNAME)"
    $now = Get-Date
    $matching = @(Get-ChildItem Cert:\LocalMachine\My -CodeSigningCert |
        Where-Object {
            $_.Subject -eq $subject -and $_.HasPrivateKey -and
            (Test-CodeSigningEku -Certificate $_)
        })
    $valid = @($matching | Where-Object {
        $_.NotBefore -le $now -and $_.NotAfter -gt $now
    })
    if ($valid.Count -gt 1) {
        return [pscustomobject]@{ Kind = 'Ambiguous'; Certificate = $null }
    }
    if ($valid.Count -eq 1) {
        return [pscustomobject]@{
            Kind = 'Existing'
            Certificate = $valid[0]
        }
    }
    return [pscustomobject]@{
        Kind = if ($matching.Count -gt 0) { 'Expired' } else { 'New' }
        Certificate = $null
    }
}

function Get-CertificateSha256 {
    param([Parameter(Mandatory)]
        [Security.Cryptography.X509Certificates.X509Certificate2]$Certificate)

    $sha256 = [Security.Cryptography.SHA256]::Create()
    try {
        return $sha256.ComputeHash($Certificate.RawData)
    }
    finally {
        $sha256.Dispose()
    }
}

function Get-TrayCompatibilityState {
    param(
        [Parameter(Mandatory)][string]$PackageProductVersion,
        [Parameter(Mandatory)]$CertificateState
    )

    $trayPath = Join-Path $installDirectory 'xucRunTray.exe'
    if (-not (Test-Path -LiteralPath $trayPath -PathType Leaf)) {
        return [pscustomobject]@{
            Kind = 'NotInstalled'
            TextKey = 'TrayNotInstalled'
            Arguments = @()
        }
    }

    $trayUninstallPath =
        'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\xucRun Tray'
    if (-not (Test-Path -LiteralPath $trayUninstallPath)) {
        return [pscustomobject]@{
            Kind = 'Incompatible'
            TextKey = 'TrayLegacyInstallation'
            Arguments = @()
        }
    }

    $registeredTrayVersion = [string](
        Get-Item -LiteralPath $trayUninstallPath).GetValue('DisplayVersion', '')
    $trayFileVersion = [string](
        Get-Item -LiteralPath $trayPath).VersionInfo.ProductVersion
    if ($registeredTrayVersion -ne $PackageProductVersion -or
        $trayFileVersion -ne $PackageProductVersion) {
        return [pscustomobject]@{
            Kind = 'Incompatible'
            TextKey = 'TrayVersionMismatch'
            Arguments = @(
                $registeredTrayVersion,
                $trayFileVersion,
                $PackageProductVersion)
        }
    }

    $trayFiles = @(
        $trayPath,
        (Join-Path $languageDirectory 'xucRunTray.de-DE.dll'),
        (Join-Path $languageDirectory 'xucRunTray.en-US.dll')
    )
    foreach ($path in $trayFiles) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            return [pscustomobject]@{
                Kind = 'Incompatible'
                TextKey = 'TrayIncomplete'
                Arguments = @($path)
            }
        }
    }

    if ($CertificateState.Kind -ne 'Existing') {
        return [pscustomobject]@{
            Kind = 'Incompatible'
            TextKey = 'TraySignerUnavailable'
            Arguments = @()
        }
    }

    $expectedSigner = [Convert]::ToBase64String(
        (Get-CertificateSha256 -Certificate $CertificateState.Certificate))
    foreach ($path in $trayFiles) {
        $signature = Get-AuthenticodeSignature -LiteralPath $path
        if ($signature.Status -ne
            [Management.Automation.SignatureStatus]::Valid -or
            -not $signature.SignerCertificate) {
            return [pscustomobject]@{
                Kind = 'Incompatible'
                TextKey = 'TraySignatureInvalid'
                Arguments = @($path, [string]$signature.Status)
            }
        }
        $actualSigner = [Convert]::ToBase64String(
            (Get-CertificateSha256 -Certificate $signature.SignerCertificate))
        if ($actualSigner -ne $expectedSigner) {
            return [pscustomobject]@{
                Kind = 'Incompatible'
                TextKey = 'TraySignerMismatch'
                Arguments = @($path)
            }
        }
    }

    return [pscustomobject]@{
        Kind = 'Compatible'
        TextKey = 'TrayCompatible'
        Arguments = @($PackageProductVersion)
    }
}

function Get-TrayCompatibilityStatusText {
    return Get-Text -Key $script:trayCompatibilityState.TextKey `
        -Arguments $script:trayCompatibilityState.Arguments
}

function Get-CertificateStatusText {
    $state = $script:certificateState
    if ($state.Kind -eq 'Existing') {
        $cultureName = if ($script:language -eq 'de') { 'de-DE' } else { 'en-US' }
        $culture = [Globalization.CultureInfo]::GetCultureInfo($cultureName)
        return Get-Text -Key CertificateExisting -Arguments @(
            $state.Certificate.NotBefore.ToString('d', $culture),
            $state.Certificate.NotAfter.ToString('d', $culture),
            $state.Certificate.Thumbprint)
    }
    return Get-Text -Key ("Certificate$($state.Kind)")
}

function Get-InstallationSuccessText {
    param(
        [Parameter(Mandatory)]
        [Security.Cryptography.X509Certificates.X509Certificate2]$Certificate,
        [Parameter(Mandatory)][bool]$Reused,
        [AllowNull()][string]$RuntimeAction
    )

    $cultureName = if ($script:language -eq 'de') { 'de-DE' } else { 'en-US' }
    $culture = [Globalization.CultureInfo]::GetCultureInfo($cultureName)
    $name = $Certificate.GetNameInfo(
        [Security.Cryptography.X509Certificates.X509NameType]::SimpleName, $false)
    $key = if ($Reused) {
        'InstallationSuccessExisting'
    } else {
        'InstallationSuccessNew'
    }
    $text = Get-Text -Key $key -Arguments @(
        $name,
        $Certificate.NotBefore.ToString('g', $culture),
        $Certificate.NotAfter.ToString('g', $culture),
        $Certificate.Thumbprint)
    if (-not [string]::IsNullOrWhiteSpace($RuntimeAction)) {
        $runtimeKey = if ($RuntimeAction -eq 'Updated') {
            'RuntimeUpdatedSuccess'
        }
        else {
            'RuntimeInstalledSuccess'
        }
        $text += "`r`n`r`n" + (Get-Text -Key $runtimeKey `
            -Arguments $script:vcRuntimeState.Version)
    }
    return $text
}

function Get-InstalledProductState {
    $uninstallPath =
        'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\xucRun'
    if (-not (Test-Path -LiteralPath $uninstallPath)) {
        return $null
    }

    $key = Get-Item -LiteralPath $uninstallPath
    $version = [string]$key.GetValue('DisplayVersion', '')
    if ([string]::IsNullOrWhiteSpace($version)) {
        return $null
    }

    $installedAt = [DateTimeOffset]::MinValue
    $timestamp = [string]$key.GetValue('InstallTimestampUtc', '')
    if (-not [DateTimeOffset]::TryParse(
        $timestamp,
        [Globalization.CultureInfo]::InvariantCulture,
        [Globalization.DateTimeStyles]::RoundtripKind,
        [ref]$installedAt)) {
        $installLocation = [string]$key.GetValue('InstallLocation', '')
        $clientPath = Join-Path $installLocation 'xucRun.exe'
        $installedAt = if (Test-Path -LiteralPath $clientPath -PathType Leaf) {
            [DateTimeOffset](Get-Item -LiteralPath $clientPath).CreationTime
        } else {
            $null
        }
    }

    return [pscustomobject]@{
        Version = $version
        InstalledAt = $installedAt
    }
}

function Get-InstallNoticeText {
    if (-not $script:installedProductState) {
        return Get-Text -Key NewInstallNotice
    }

    $cultureName = if ($script:language -eq 'de') { 'de-DE' } else { 'en-US' }
    $culture = [Globalization.CultureInfo]::GetCultureInfo($cultureName)
    $installedAtText = if ($script:installedProductState.InstalledAt) {
        $script:installedProductState.InstalledAt.ToLocalTime().ToString(
            'g', $culture)
    } else {
        Get-Text -Key UnknownInstallTime
    }
    $noticeKey = if (
        [Version]$script:installedProductState.Version -eq
            [Version]$packageProductVersion) {
        'RepairNotice'
    } else {
        'UpdateNotice'
    }
    return Get-Text -Key $noticeKey -Arguments @(
        $script:installedProductState.Version,
        $installedAtText)
}

function Set-Status {
    param([Parameter(Mandatory)][string]$Key, [object[]]$Arguments = @())
    $script:statusKey = $Key
    $script:statusArguments = $Arguments
    $statusLabel.Text = Get-Text -Key $Key -Arguments $Arguments
    [Windows.Forms.Application]::DoEvents()
}

function Get-VcRuntimeState {
    $runtimeKeys = @(
        'HKLM:\SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64',
        'HKLM:\SOFTWARE\WOW6432Node\Microsoft\VisualStudio\14.0\VC\Runtimes\x64'
    )
    $detectedVersion = $null
    foreach ($key in $runtimeKeys) {
        $runtime = Get-ItemProperty -LiteralPath $key -ErrorAction SilentlyContinue
        if ($runtime -and $runtime.Installed -eq 1) {
            try {
                $version = [Version](
                    "$($runtime.Major).$($runtime.Minor).$($runtime.Bld).$($runtime.Rbld)")
                if (-not $detectedVersion -or $version -gt $detectedVersion) {
                    $detectedVersion = $version
                }
            }
            catch {
                continue
            }
        }
    }
    return [pscustomobject]@{
        Installed = $null -ne $detectedVersion
        Version = $detectedVersion
        MeetsMinimum = $null -ne $detectedVersion -and
            $detectedVersion -ge $minimumVcRuntimeVersion
    }
}

function Get-VcRuntimeStatusText {
    if ($script:vcRuntimeState.MeetsMinimum) {
        return Get-Text -Key RuntimeCompatible -Arguments $script:vcRuntimeState.Version
    }
    if ($script:vcRuntimeState.Installed) {
        return Get-Text -Key RuntimeTooOld -Arguments $script:vcRuntimeState.Version
    }
    return Get-Text -Key RuntimeMissing
}

function Update-VcRuntimeDisplay {
    $runtimeStatus.Text = Get-VcRuntimeStatusText
    if ($form) {
        Update-SettingsLayout
    }
}

function Assert-MicrosoftRedistributable {
    param([Parameter(Mandatory)][string]$Path)
    $resolvedPath = (Resolve-Path -LiteralPath $Path).ProviderPath
    $signature = Get-AuthenticodeSignature -LiteralPath $resolvedPath
    if ($signature.Status -ne [Management.Automation.SignatureStatus]::Valid -or
        -not $signature.SignerCertificate -or
        $signature.SignerCertificate.Subject -notmatch '(^|, )O=Microsoft Corporation(,|$)') {
        throw (Get-Text -Key InvalidMicrosoftSignature)
    }
    $versionInfo = (Get-Item -LiteralPath $resolvedPath).VersionInfo
    if ($versionInfo.OriginalFilename -ine 'VC_redist.x64.exe') {
        throw (Get-Text -Key InvalidRedistributableFile)
    }
    $versionMatch = [regex]::Match(
        [string]$versionInfo.ProductVersion,
        '(?<!\d)\d+\.\d+\.\d+\.\d+(?!\d)')
    if (-not $versionMatch.Success) {
        throw (Get-Text -Key UnreadableRedistributableVersion)
    }
    $redistributableVersion = [Version]$versionMatch.Value
    if ($redistributableVersion -lt $minimumVcRuntimeVersion) {
        throw (Get-Text -Key InvalidRedistributableVersion -Arguments @(
            $redistributableVersion,
            $minimumVcRuntimeVersion))
    }
    return $resolvedPath
}

function Assert-NotReparsePoint {
    param([Parameter(Mandatory)][string]$Path)

    if (Test-Path -LiteralPath $Path) {
        $item = Get-Item -LiteralPath $Path -Force
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Installer paths must not be reparse points: $Path"
        }
    }
}

function Confirm-VcRuntime {
    Set-Status -Key StatusCheckingRuntime
    $script:vcRuntimeState = Get-VcRuntimeState
    Update-VcRuntimeDisplay
    if ($script:vcRuntimeState.MeetsMinimum) {
        return
    }
    $runtimeWasInstalled = $script:vcRuntimeState.Installed
    $promptKey = if ($script:vcRuntimeState.Installed) {
        'RuntimeUpdatePrompt'
    }
    else {
        'RuntimeInstallPrompt'
    }
    $titleKey = if ($script:vcRuntimeState.Installed) {
        'RuntimeUpdateTitle'
    }
    else {
        'RuntimeMissingTitle'
    }
    $choice = [Windows.Forms.MessageBox]::Show(
        (Get-Text -Key $promptKey -Arguments $minimumVcRuntimeVersion),
        (Get-Text -Key $titleKey), 'YesNoCancel', 'Warning')
    if ($choice -eq 'Cancel') {
        throw (Get-Text -Key RuntimeMissingTitle)
    }
    $redistributableStaging = $null
    try {
        if ($choice -eq 'Yes') {
            $redistributablePath = Join-Path $PackageDirectory 'vc_redist.x64.exe'
            try {
                Invoke-WebRequest -Uri $vcRuntimeDownloadUri `
                    -OutFile $redistributablePath -UseBasicParsing
            }
            catch {
                throw (Get-Text -Key RuntimeDownloadFailed `
                    -Arguments $_.Exception.Message)
            }
        }
        else {
            $dialog = [Windows.Forms.OpenFileDialog]::new()
            $dialog.Title = Get-Text -Key RuntimeDialogTitle
            $dialog.Filter = Get-Text -Key RuntimeDialogFilter
            $dialog.FileName = 'vc_redist.x64.exe'
            if ($dialog.ShowDialog() -ne 'OK') {
                throw (Get-Text -Key RuntimeNotSelected)
            }
            Assert-NotReparsePoint -Path $dialog.FileName
            Assert-NotReparsePoint -Path $PackageDirectory
            $redistributableStaging = Join-Path $PackageDirectory (
                '.vc-runtime-' + [Guid]::NewGuid().ToString('N'))
            New-Item -ItemType Directory -Path $redistributableStaging |
                Out-Null
            Assert-NotReparsePoint -Path $redistributableStaging
            $redistributablePath =
                Join-Path $redistributableStaging 'vc_redist.x64.exe'
            Copy-Item -LiteralPath $dialog.FileName `
                -Destination $redistributablePath
            Assert-NotReparsePoint -Path $redistributablePath
        }
        $redistributablePath =
            Assert-MicrosoftRedistributable -Path $redistributablePath
        Set-Status -Key StatusInstallingRuntime
        $process = Start-Process -FilePath $redistributablePath `
            -ArgumentList '/install', '/quiet', '/norestart' -Wait -PassThru
        $script:vcRuntimeState = Get-VcRuntimeState
        Update-VcRuntimeDisplay
        if ($process.ExitCode -notin @(0, 3010) -and
            -not $script:vcRuntimeState.MeetsMinimum) {
            throw (Get-Text -Key RuntimeInstallFailed -Arguments $process.ExitCode)
        }
        if (-not $script:vcRuntimeState.MeetsMinimum) {
            if ($script:vcRuntimeState.Installed) {
                throw (Get-Text -Key RuntimeTooOldAfterInstall -Arguments @(
                    $script:vcRuntimeState.Version,
                    $minimumVcRuntimeVersion))
            }
            throw (Get-Text -Key RuntimeMissingAfterInstall)
        }
        $script:vcRuntimeAction = if ($runtimeWasInstalled) {
            'Updated'
        }
        else {
            'Installed'
        }
        if ($process.ExitCode -eq 3010) {
            [Windows.Forms.MessageBox]::Show(
                (Get-Text -Key RuntimeRestartMessage),
                (Get-Text -Key RuntimeRestartTitle), 'OK', 'Warning') | Out-Null
        }
    }
    finally {
        if ($redistributableStaging) {
            Remove-Item -LiteralPath $redistributableStaging -Recurse -Force `
                -ErrorAction SilentlyContinue
        }
    }
}

function Invoke-HiddenPowerShell {
    param([Parameter(Mandatory)][string[]]$Arguments)
    $systemDirectory = [Environment]::GetFolderPath(
        [Environment+SpecialFolder]::System)
    if ([string]::IsNullOrWhiteSpace($systemDirectory)) {
        throw 'The Windows system directory could not be resolved.'
    }
    $powerShellPath =
        Join-Path $systemDirectory 'WindowsPowerShell\v1.0\powershell.exe'
    $id = [Guid]::NewGuid().ToString('N')
    $logDirectory = [IO.Path]::GetTempPath()
    $standardOutputPath =
        Join-Path $logDirectory "xucRun-Installer-$id.stdout.log"
    $standardErrorPath =
        Join-Path $logDirectory "xucRun-Installer-$id.stderr.log"
    try {
        $process = Start-Process -FilePath $powerShellPath -ArgumentList $Arguments `
            -WindowStyle Hidden -RedirectStandardOutput $standardOutputPath `
            -RedirectStandardError $standardErrorPath -Wait -PassThru
        $details = @(
            if (Test-Path -LiteralPath $standardErrorPath) {
                Get-Content -LiteralPath $standardErrorPath -Raw
            }
            if (Test-Path -LiteralPath $standardOutputPath) {
                Get-Content -LiteralPath $standardOutputPath -Raw
            }
        ) | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
        return [pscustomobject]@{
            ExitCode = $process.ExitCode
            Details = ($details -join "`r`n").Trim()
        }
    }
    finally {
        Remove-Item -LiteralPath $standardOutputPath, $standardErrorPath `
            -Force -ErrorAction SilentlyContinue
    }
}

function Get-ProcessFailure {
    param([Parameter(Mandatory)][string]$Heading, [Parameter(Mandatory)]$Result)
    $message = Get-Text -Key ProcessFailed -Arguments $Heading, $Result.ExitCode
    if (-not [string]::IsNullOrWhiteSpace($Result.Details)) {
        $details = $Result.Details
        if ($details.Length -gt 5000) {
            $details = $details.Substring(0, 5000) + "`r`n..."
        }
        $message += "`r`n`r`n$details"
    }
    return $message
}

$script:certificateState = Get-LocalCertificateState
$script:installedProductState = Get-InstalledProductState
$script:vcRuntimeState = Get-VcRuntimeState
$packageClientPath = Join-Path $PackageDirectory 'xucRun.exe'
$packageProductVersion = [string](
    Get-Item -LiteralPath $packageClientPath).VersionInfo.ProductVersion
if ([string]::IsNullOrWhiteSpace($packageProductVersion)) {
    throw "The Core package version could not be determined: $packageClientPath"
}
$script:trayCompatibilityState = Get-TrayCompatibilityState `
    -PackageProductVersion $packageProductVersion `
    -CertificateState $script:certificateState
$form = [Windows.Forms.Form]::new()
$form.ClientSize = [Drawing.Size]::new(620, 545)
$form.FormBorderStyle = 'FixedDialog'
$form.MaximizeBox = $false
$form.MinimizeBox = $false
$form.StartPosition = 'CenterScreen'

$heading = [Windows.Forms.Label]::new()
$heading.Font = [Drawing.Font]::new('Segoe UI', 17, [Drawing.FontStyle]::Bold)
$heading.Location = [Drawing.Point]::new(28, 20)
$heading.Size = [Drawing.Size]::new(562, 38)
$form.Controls.Add($heading)

$description = [Windows.Forms.Label]::new()
$description.AutoSize = $true
$description.Location = [Drawing.Point]::new(30, 62)
$description.MaximumSize = [Drawing.Size]::new(560, 0)
$form.Controls.Add($description)

$tabs = [Windows.Forms.TabControl]::new()
$tabs.Location = [Drawing.Point]::new(28, 100)
$tabs.Size = [Drawing.Size]::new(562, 315)
$form.Controls.Add($tabs)
$settingsTab = [Windows.Forms.TabPage]::new()
$detailsTab = [Windows.Forms.TabPage]::new()
$securityTab = [Windows.Forms.TabPage]::new()
$licenseTab = [Windows.Forms.TabPage]::new()
$tabs.TabPages.AddRange(@($settingsTab, $detailsTab, $securityTab, $licenseTab))

$languageLabel = [Windows.Forms.Label]::new()
$languageLabel.Location = [Drawing.Point]::new(14, 18)
$languageLabel.Size = [Drawing.Size]::new(180, 24)
$settingsTab.Controls.Add($languageLabel)
$languageCombo = [Windows.Forms.ComboBox]::new()
$languageCombo.DropDownStyle = 'DropDownList'
$languageCombo.Location = [Drawing.Point]::new(210, 15)
$languageCombo.Size = [Drawing.Size]::new(326, 24)
[void]$languageCombo.Items.Add('Deutsch')
[void]$languageCombo.Items.Add('English')
$languageCombo.SelectedIndex = if ($script:language -eq 'de') { 0 } else { 1 }
$settingsTab.Controls.Add($languageCombo)

$languageHint = [Windows.Forms.Label]::new()
$languageHint.AutoSize = $true
$languageHint.Location = [Drawing.Point]::new(14, 45)
$languageHint.MaximumSize = [Drawing.Size]::new(522, 0)
$settingsTab.Controls.Add($languageHint)

$pathCheck = [Windows.Forms.CheckBox]::new()
$pathCheck.AutoSize = $true
$pathCheck.Location = [Drawing.Point]::new(14, 68)
$pathCheck.MaximumSize = [Drawing.Size]::new(522, 0)
$pathCheck.Checked = $true
$settingsTab.Controls.Add($pathCheck)

$runtimeStatus = [Windows.Forms.Label]::new()
$runtimeStatus.AutoSize = $true
$runtimeStatus.Location = [Drawing.Point]::new(14, 102)
$runtimeStatus.MaximumSize = [Drawing.Size]::new(522, 0)
$settingsTab.Controls.Add($runtimeStatus)

$certificateGroup = [Windows.Forms.GroupBox]::new()
$certificateGroup.Location = [Drawing.Point]::new(14, 132)
$certificateGroup.Size = [Drawing.Size]::new(522, 116)
$settingsTab.Controls.Add($certificateGroup)
$localCertificate = [Windows.Forms.Label]::new()
$localCertificate.AutoSize = $true
$localCertificate.Location = [Drawing.Point]::new(18, 24)
$localCertificate.MaximumSize = [Drawing.Size]::new(486, 0)
$certificateGroup.Controls.Add($localCertificate)
$certificateWarning = [Windows.Forms.Label]::new()
$certificateWarning.AutoSize = $true
$certificateWarning.Location = [Drawing.Point]::new(18, 76)
$certificateWarning.MaximumSize = [Drawing.Size]::new(486, 0)
$certificateGroup.Controls.Add($certificateWarning)

$updateNotice = [Windows.Forms.Label]::new()
$updateNotice.AutoSize = $true
$updateNotice.Location = [Drawing.Point]::new(14, 228)
$updateNotice.MaximumSize = [Drawing.Size]::new(522, 0)
$settingsTab.Controls.Add($updateNotice)

$trayCompatibility = [Windows.Forms.Label]::new()
$trayCompatibility.AutoSize = $true
$trayCompatibility.Location = [Drawing.Point]::new(14, 274)
$trayCompatibility.MaximumSize = [Drawing.Size]::new(522, 0)
$settingsTab.Controls.Add($trayCompatibility)

$details = [Windows.Forms.TextBox]::new()
$details.Location = [Drawing.Point]::new(10, 12)
$details.Size = [Drawing.Size]::new(534, 244)
$details.Multiline = $true
$details.ReadOnly = $true
$details.ScrollBars = 'Vertical'
$details.BackColor = [Drawing.SystemColors]::Window
$detailsTab.Controls.Add($details)

$securityDetails = [Windows.Forms.TextBox]::new()
$securityDetails.Location = [Drawing.Point]::new(10, 12)
$securityDetails.Size = [Drawing.Size]::new(522, 190)
$securityDetails.Multiline = $true
$securityDetails.ReadOnly = $true
$securityDetails.ScrollBars = 'Vertical'
$securityDetails.BackColor = [Drawing.SystemColors]::Window
$securityTab.Controls.Add($securityDetails)
$readmeLink = [Windows.Forms.LinkLabel]::new()
$readmeLink.AutoSize = $true
$readmeLink.Location = [Drawing.Point]::new(10, 100)
$securityTab.Controls.Add($readmeLink)
$licenseIntroduction = [Windows.Forms.Label]::new()
$licenseIntroduction.AutoSize = $true
$licenseIntroduction.Location = [Drawing.Point]::new(10, 12)
$licenseIntroduction.MaximumSize = [Drawing.Size]::new(522, 0)
$licenseTab.Controls.Add($licenseIntroduction)
$licenseDetails = [Windows.Forms.TextBox]::new()
$licenseDetails.Location = [Drawing.Point]::new(10, 12)
$licenseDetails.Size = [Drawing.Size]::new(522, 244)
$licenseDetails.Multiline = $true
$licenseDetails.ReadOnly = $true
$licenseDetails.ScrollBars = 'Vertical'
$licenseDetails.BackColor = [Drawing.SystemColors]::Window
$licenseDetails.Text = (($licenseText + "`n`nNOTICE`n`n" + $noticeText) `
    -replace "`r?`n", "`r`n")
$licenseTab.Controls.Add($licenseDetails)

$acknowledgment = [Windows.Forms.CheckBox]::new()
$acknowledgment.AutoSize = $true
$acknowledgment.Location = [Drawing.Point]::new(28, 420)
$acknowledgment.MaximumSize = [Drawing.Size]::new(562, 0)
$acknowledgment.Checked = $false
$form.Controls.Add($acknowledgment)

$statusLabel = [Windows.Forms.Label]::new()
$statusLabel.Location = [Drawing.Point]::new(28, 432)
$statusLabel.Size = [Drawing.Size]::new(562, 18)
$form.Controls.Add($statusLabel)
$progress = [Windows.Forms.ProgressBar]::new()
$progress.Location = [Drawing.Point]::new(28, 454)
$progress.Size = [Drawing.Size]::new(562, 18)
$progress.Style = 'Marquee'
$progress.Visible = $false
$form.Controls.Add($progress)
$installButton = [Windows.Forms.Button]::new()
$installButton.Location = [Drawing.Point]::new(378, 495)
$installButton.Size = [Drawing.Size]::new(102, 34)
$installButton.Enabled = $false
$form.AcceptButton = $installButton
$form.Controls.Add($installButton)
$cancelButton = [Windows.Forms.Button]::new()
$cancelButton.Location = [Drawing.Point]::new(488, 495)
$cancelButton.Size = [Drawing.Size]::new(102, 34)
$cancelButton.DialogResult = 'Cancel'
$form.CancelButton = $cancelButton
$form.Controls.Add($cancelButton)

function Update-SettingsLayout {
    $tabs.Top = [Math]::Max(100, $description.Bottom + 8)
    $pathCheck.Top = $languageHint.Bottom + 8
    $runtimeStatus.Top = $pathCheck.Bottom + 8
    $certificateGroup.Top = $runtimeStatus.Bottom + 10
    $certificateWarning.Top = $localCertificate.Bottom + 10
    $certificateGroup.Height = $certificateWarning.Bottom + 12
    $updateNotice.Top = $certificateGroup.Bottom + 10
    $trayCompatibility.Top = $updateNotice.Bottom + 10

    $tabs.Height = $trayCompatibility.Bottom + 38
    $details.Height = $tabs.Height - 56
    $securityDetails.Height = $tabs.Height - 85
    $readmeLink.Top = $securityDetails.Bottom + 8
    $licenseDetails.Top = $licenseIntroduction.Bottom + 10
    $licenseDetails.Height = $tabs.Height - $licenseDetails.Top - 44
    $acknowledgment.Top = $tabs.Bottom + 12
    $statusLabel.Top = $acknowledgment.Bottom + 12
    $progress.Top = $statusLabel.Bottom + 4
    $installButton.Top = $progress.Bottom + 24
    $cancelButton.Top = $installButton.Top
    $form.ClientSize = [Drawing.Size]::new(
        620, $installButton.Bottom + 16)
}

function Update-Language {
    $form.Text = Get-Text -Key WindowTitle
    $heading.Text = Get-Text -Key Heading
    $description.Text = Get-Text -Key Description -Arguments @($installDirectory)
    $settingsTab.Text = Get-Text -Key SettingsTab
    $detailsTab.Text = Get-Text -Key DetailsTab
    $securityTab.Text = Get-Text -Key SecurityTab
    $licenseTab.Text = Get-Text -Key LicenseTab
    $licenseIntroduction.Text = Get-Text -Key LicenseIntroduction
    $languageLabel.Text = Get-Text -Key LanguageLabel
    $languageHint.Text = Get-Text -Key LanguageHint
    $pathCheck.Text = Get-Text -Key PathCheck -Arguments @($installDirectory)
    $runtimeStatus.Text = Get-VcRuntimeStatusText
    $certificateGroup.Text = Get-Text -Key CertificateGroup
    $localCertificate.Text = Get-CertificateStatusText
    $certificateWarning.Text = Get-Text -Key CertificateWarning
    $updateNotice.Text = Get-InstallNoticeText
    $trayCompatibility.Text = Get-TrayCompatibilityStatusText
    $details.Text = (Get-Text -Key Details -Arguments @(
        $programDataDirectory, $installDirectory, $installerLogDirectory,
        $minimumVcRuntimeVersion)).Trim()
    $securityDetails.Text = (((Get-Text -Key StatusNotice).Trim() +
        "`r`n`r`n" + (Get-Text -Key SecurityNotice).Trim()) `
        -replace "`r?`n", "`r`n")
    $readmeLink.Text = Get-Text -Key ReadmeLink
    $acknowledgment.Text = Get-Text -Key Acknowledgment
    $statusLabel.Text = Get-Text -Key $script:statusKey `
        -Arguments $script:statusArguments
    $installButton.Text = Get-Text -Key InstallButton
    $cancelButton.Text = Get-Text -Key CancelButton
    Update-SettingsLayout
}

function Update-InstallAvailability {
    $installButton.Enabled = $acknowledgment.Checked -and
        $script:certificateState.Kind -ne 'Ambiguous' -and
        $script:trayCompatibilityState.Kind -ne 'Incompatible' -and
        -not $script:installInProgress
}

$acknowledgment.Add_CheckedChanged({ Update-InstallAvailability })
$readmeLink.Add_LinkClicked({
    $url = Get-Text -Key ReadmeUrl
    try {
        $shell = New-Object -ComObject Shell.Application
        $desktopHandle = 0
        $desktop = $shell.Windows().FindWindowSW(0, 0, 8, [ref]$desktopHandle, 1)
        if ($null -eq $desktop) {
            throw 'The Windows desktop shell is unavailable.'
        }
        $desktop.Document.Application.ShellExecute($url, '', '', 'open', 1)
    }
    catch {
        [Windows.Forms.MessageBox]::Show(
            ((Get-Text -Key ReadmeOpenFailed) + "`r`n`r`n" + $url +
                "`r`n`r`n" + $_.Exception.Message),
            (Get-Text -Key WindowTitle), 'OK', 'Error') | Out-Null
    }
})

$languageCombo.Add_SelectedIndexChanged({
    $script:language = if ($languageCombo.SelectedIndex -eq 0) { 'de' } else { 'en' }
    Update-Language
})

$installButton.Add_Click({
    $script:installInProgress = $true
    Update-InstallAvailability
    $acknowledgment.Enabled = $false
    $cancelButton.Enabled = $false
    $progress.Visible = $true
    $form.UseWaitCursor = $true
    [Windows.Forms.Application]::DoEvents()
    try {
        $reusedCertificate = $script:certificateState.Kind -eq 'Existing'
        $script:vcRuntimeAction = $null
        Confirm-VcRuntime
        Set-Status -Key StatusSigning
        $signScript =
            Join-Path $PackageDirectory 'UserCodeSigning\Sign-xucRunPackage.ps1'
        $signResult = Invoke-HiddenPowerShell -Arguments @(
            '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
            ('"' + $signScript + '"'), '-PackageDirectory',
            ('"' + $PackageDirectory + '"'))
        if ($signResult.ExitCode -ne 0) {
            throw (Get-ProcessFailure -Heading (
                Get-Text -Key LocalSigningFailed) -Result $signResult)
        }
        $script:certificateState = Get-LocalCertificateState
        if ($script:certificateState.Kind -ne 'Existing') {
            throw (Get-Text -Key LocalSigningFailed)
        }

        $machineInstaller = Join-Path $PackageDirectory 'Install-xucRun.ps1'
        $arguments = @(
            '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
            ('"' + $machineInstaller + '"'), '-PackageDirectory',
            ('"' + $PackageDirectory + '"'), '-Language', $script:language)
        if ($pathCheck.Checked) {
            $arguments += '-AddToSystemPath'
        }
        Set-Status -Key StatusInstalling
        $installResult = Invoke-HiddenPowerShell -Arguments $arguments
        if ($installResult.ExitCode -ne 0) {
            throw (Get-ProcessFailure -Heading (
                Get-Text -Key InstallationFailed) -Result $installResult)
        }

        Set-Status -Key StatusSuccess
        [Windows.Forms.MessageBox]::Show(
            (Get-InstallationSuccessText `
                -Certificate $script:certificateState.Certificate `
                -Reused $reusedCertificate `
                -RuntimeAction $script:vcRuntimeAction),
            (Get-Text -Key WindowTitle), 'OK', 'Information') | Out-Null
        $form.DialogResult = 'OK'
        $form.Close()
    }
    catch {
        Set-Status -Key StatusFailed
        [Windows.Forms.MessageBox]::Show(
            $_.Exception.Message, (Get-Text -Key WindowTitle),
            'OK', 'Error') | Out-Null
        $script:installInProgress = $false
        $acknowledgment.Enabled = $true
        Update-InstallAvailability
        $cancelButton.Enabled = $true
        $progress.Visible = $false
        $form.UseWaitCursor = $false
    }
})

Update-Language
$result = $form.ShowDialog()
if ($result -eq 'OK') {
    exit 0
}
exit 1223
