# WifiWhirl - Mache deinen LAY-Z-SPA™ Whirlpool smart!

[![Lizenz: GPL v3](https://img.shields.io/badge/Lizenz-GPL%20v3-blue.svg?style=flat-square)](https://www.gnu.org/licenses/gpl-3.0)
[![Plattform](https://img.shields.io/badge/Plattform-ESP8266%20%7C%20ESP32%20(Beta)-orange.svg?style=flat-square)](https://www.espressif.com/en/products/socs/esp8266)
[![Anleitung](https://img.shields.io/badge/Anleitung-Dokumentation-success.svg?style=flat-square)](https://wifiwhirl.de/)
[![Kaufen](https://img.shields.io/badge/Modul-Kaufen-ff69b4.svg?style=flat-square)](https://wifiwhirl.de/Modul/Kaufen/)

<p align="center">
<img src="./wifiwhirl_logo.png" alt="WifiWhirl Logo" width="80"/>
</p>

**Steuere und überwache deinen Bestway® LAY-Z-SPA™ Whirlpool bequem über WLAN mit der WifiWhirl Software.**

---

## Schnellzugriff

* **Ausführliche Anleitung (Installation, Selbstbau & Nutzung):** [wifiwhirl.de](https://wifiwhirl.de/)
* **Selbstbau-Anleitung:** [wifiwhirl.de/Selbstbau/Einfuehrung](https://wifiwhirl.de/Selbstbau/Einfuehrung/) (ohne Cloudfunktionalitäten)
* **Fertiges Modul kaufen:** [wifiwhirl.de/Modul/Kaufen](https://wifiwhirl.de/Modul/Kaufen/) (mit Cloudfunktionalitäten)
* **Fragen & Diskussion (GitHub Issues):** [Issues](https://github.com/WifiWhirl/WifiWhirl-Software/issues)

---

## Über das Projekt

Die WifiWhirl Software ist eine Open-Source-Softwarelösung für den **ESP8266 Mikrocontroller** (mit Beta-Unterstützung für den **ESP32**), die es ermöglicht, die Pumpeneinheit von **Bestway® LAY-Z-SPA™ Whirlpools** über ein lokales WLAN-Netzwerk zu steuern und zu überwachen.

Du möchtest die Heizung von unterwegs starten, den Filterzyklus automatisieren oder deinen Pool in deine Smarthome Anwendungen einbinden? Mit WifiWhirl ist das möglich!

Das Projekt basiert zu großen Teilen auf der **Software und dem Platinenlayout von [visualapproach](https://github.com/visualapproach/)**. Da seine Arbeit unter der **GNU General Public License 3.0 (GPL-3.0)** veröffentlicht wurde, steht auch diese WifiWhirl Softwareanpassung unter derselben Lizenz.

Dieses Repository enthält den **Quellcode** für die angepasste Firmware sowie das Webfrontend (eine Single-Page-App auf Basis von Preact und Vite, die in die Firmware eingebettet wird).

**Hinweis zur Funktionalität:** Die hier im Repository veröffentlichte Open-Source-Software konzentriert sich auf die Steuerung des Whirlpools im **lokalen Netzwerk** (via Webinterface) und die **MQTT-Integration** für Smarthome-Systeme. Das separat [erhältliche, fertige Modul](https://wifiwhirl.de/Modul/Kaufen/) bietet darüber hinaus die **optionale WifiWhirl Cloud (PoolLink)**: eine verschlüsselte Fernverbindung, mit der du deinen Whirlpool von überall aus überwachen und steuern kannst - inklusive Wetterdaten und Außentemperatur für den Gerätestandort. Cloud-fähige Geräte werden werksseitig mit einem eindeutigen Schlüssel provisioniert; Open-Source-Builds ohne diesen Schlüssel blenden die Cloud-Funktionen einfach aus.

---

## Unterschiede zur Original-Software (visualapproach)

Obwohl die WifiWhirl Software auf der hervorragenden Arbeit von [visualapproach](https://github.com/visualapproach/) basiert, gibt es umfangreiche Anpassungen und Erweiterungen in dieser Version:

* **Komplett neue Web-Oberfläche:** Einheitliche Single-Page-App mit Mobile-first-Navigation, anpassbarem Dashboard (Widgets per Drag & Drop), Thermostat-Drehregler, Dark Mode und PWA-Unterstützung.
* **Mehrsprachigkeit:** Deutsch und Englisch sowie Beta-Locales für Französisch, Niederländisch und Polnisch mit automatischer Spracherkennung.
* **Einrichtungsassistent:** Ein geführter Ersteinrichtungs-Assistent in der Web-App ersetzt das WiFiManager-Captive-Portal.
* **Verbesserte Heizlogik:** Ein häufiges Problem wurde adressiert: Die **Heizung bleibt nun aktiv**, auch wenn gleichzeitig ein programmierter Filterzyklus läuft.
* **Smart Schedule:** Intelligente Heizplanung mit automatischer Berechnung der optimalen Startzeit basierend auf Wasser-, Ziel- und Umgebungstemperatur - auch täglich oder wöchentlich wiederkehrend.
* **Wasserqualitätsüberwachung:** Tracking von pH-Wert, Chlor, Cyanursäure und Alkalinität mit Zeitstempeln und Home Assistant Integration.
* **Energie-Monitoring & Tarife:** Echtzeit-Überwachung von Stromverbrauch und geschätzten Kosten, inklusive konfigurierbarem Nachttarif mit Zeitfenster und Wochenendmodus.
* **Expertenmodus:** Konfigurierbare Leistungswerte, automatische Wärmeverlust-Kalibrierung (W/K) und ein Prometheus-Metriken-Endpunkt (`/metrics`).
* **Gastmodus für Vermietungen:** Einschränkungen am physischen Bedienpanel (Tasten, maximale Zieltemperatur) für Hotels, AirBnB und Ferienwohnungen - Web-UI und MQTT bleiben uneingeschränkt nutzbar.
* **Firmware-Update per Web:** Opt-in-Selbstupdate - das Gerät lädt signierte Firmware vom Manifest-Server, prüft täglich auf neue Versionen und installiert auf Wunsch.
* **Sicherheit:** Optionale globale Anmeldung für Web-UI und HTTP-API, persistente "Angemeldet bleiben"-Sitzungen mit gehashten Tokens und Login-Drosselung.
* **REST API:** Webhook-Endpunkte (`/gettemps/`, `/getstates/`) für einfache Integration mit externen Systemen.
* **Optionale Cloud-Anbindung (PoolLink):** Für Nutzer des [fertigen Moduls](https://wifiwhirl.de/Modul/Kaufen/) ermöglicht die verschlüsselte Cloud-Verbindung Fernzugriff sowie Wetterdaten und Außentemperatur für eine exakte Heizzeitberechnung.
* **Optimierter Code:** Modularisierte, schlankere und wartbarere Codebasis; Live-Updates laufen über HTTP-Polling statt WebSocket-Push für bessere Stabilität auf dem ESP8266.

---

## Features

* **Temperatur:** Aktuelle Wassertemperatur anzeigen und Ziel-Temperatur einstellen.
* **Heizung:** Heizfunktion aktivieren und deaktivieren.
* **Filterpumpe:** Filterpumpe ein- und ausschalten.
* **Sprudel-Funktion:** Sprudel-Massage (AirJet™ und HydroJet™) steuern (mit konfigurierbaren Timeouts).
* **Statusanzeige:** Übersicht über alle aktuellen Zustände (Heizung an/aus, Filter an/aus, Temperatur etc.).
* **Webinterface:** Moderne Single-Page-App mit anpassbarem Dashboard, Dark Mode und PWA-Unterstützung - auf Deutsch, Englisch und weiteren Beta-Sprachen.
* **MQTT-Integration:** Anbindung an Smart-Home-Systeme wie Home Assistant, ioBroker etc. mit umfassender Auto-Discovery.
* **Wasserqualität:** Überwachung von pH-Wert, Chlor, Cyanursäure und Alkalität mit Zeitstempeln.
* **Smart Schedule:** Intelligente Heizplanung - Pool automatisch zur gewünschten Zeit auf Temperatur, einmalig oder wiederkehrend.
* **Energie-Monitoring:** Überwachung von Stromverbrauch und geschätzten Kosten, mit Nachttarif und Währungsauswahl (EUR, PLN, GBP).
* **Display-Helligkeit:** Konfigurierbare Helligkeitsstufe und Dauer beim Tastendruck.
* **Automatisierung:** Befehlswarteschlange mit globalen und pro-Befehl-Schaltern, Schnellvorlagen, Backup/Wiederherstellung und flexiblem Planer (einmalig/stündlich/täglich/wöchentlich/individuell).
* **REST API:** Webhook-Endpunkte für einfache Integration mit externen Systemen, optional mit Basic Auth.
* **Firmware-Update per Web:** Opt-in-Selbstupdate mit signierter Firmware und täglicher Update-Prüfung.
* **Expertenmodus:** Wärmeverlust-Kalibrierung, konfigurierbare Leistungswerte und Prometheus-Metriken.
* **Gastmodus:** Panel-Einschränkungen für Vermietungsszenarien.
* **Optionale Anmeldung:** Web-UI und API mit einem globalen Passwort schützen.
* **Beta:** ESP32-Unterstützung und MSPA-Pumpenunterstützung (nur ESP32).

---

## Hardware & Selbstbau

Mit etwas Zeit, Aufwand und Geschick kannst du das WifiWhirl-Modul selbst nachbauen.

* **Benötigte Komponenten:** ESP8266 (z.B. Wemos D1 Mini), Pegelwandler, Steckverbinder, optional ein Gehäuse.
* **Zur Selbstbau-Anleitung:** [wifiwhirl.de/Selbstbau/Einfuehrung](https://wifiwhirl.de/Selbstbau/Einfuehrung/)

---

## Software aufspielen (Flashen)

Um die WifiWhirl-Software auf einen ESP8266 zu übertragen (flashen), benötigst du:

* Einen ESP8266 Mikrocontroller (z.B. Wemos D1 Mini oder NodeMCU).
* Ein Micro-USB oder USB-C-Kabel zur Verbindung mit dem Computer.
* Software zum Flashen: [PlatformIO](https://platformio.org/) mit [Visual Studio Code](https://code.visualstudio.com/).
* Ggf. Treiber für den USB-zu-Seriell-Chip deines ESP8266 (meist CH340 oder CP210x).

**Grundlegende Schritte (Details siehe Anleitung!):**

1.  **Repository klonen oder herunterladen:**
    ```bash
    git clone https://github.com/WifiWhirl/WifiWhirl-Software.git
    ```
2.  **Projekt öffnen:** Öffne den Ordner `Code` im Projektordner in VS Code mit PlatformIO.
3.  **Abhängigkeiten installieren:** PlatformIO sollte benötigte Bibliotheken automatisch herunterladen.
4.  **Konfiguration anpassen:** Benenne die Datei `config.cpp.dist` im Ordner `src` in `config.cpp` um und überprüfe die Einstellungen in der Datei.
5.  **ESP8266 anschließen:** Verbinde den ESP8266 per USB mit deinem Computer.
6.  **Kompilieren & Hochladen:** Starte den Build- und Upload-Vorgang über PlatformIO (`Upload`-Button). Das Webfrontend wird automatisch gebaut und in die Firmware eingebettet - ein separater Filesystem-Upload ist nicht nötig.

**Detaillierte Flash-Anleitung:** [https://wifiwhirl.de/Selbstbau/Software/](https://wifiwhirl.de/Selbstbau/Software/)

---

## Benutzung & Einrichtung

Nachdem die Software erfolgreich geflasht wurde:

1.  **Modul anschließen:** Verbinde das WifiWhirl-Modul gemäß der Anleitung mit der Steuereinheit deines LAY-Z-SPA™. **Achtung:** Arbeite nur bei vom Stromnetz getrennter Pumpe!  
    **Anschluss-Anleitung:** [Modell S100101](https://wifiwhirl.de/Modul/Montage-S100101/) oder [Modell S200102](https://wifiwhirl.de/Modul/Montage-S200102/)
2.  **Erster Start & Einrichtungsassistent:**
    * Beim ersten Start (oder nach einem Reset) spannt das Modul einen eigenen WLAN-Access-Point auf (Standardname: `wifiwhirl`; konfiguriert in der Datei `config.cpp` über `DEVICE_NAME`).
    * Verbinde dich mit diesem WLAN (Standardpasswort: `wifiwhirl`; konfiguriert in der Datei `config.cpp` über `wmApPassword`).
    * Öffne einen Webbrowser und gehe zur Adresse `http://192.168.4.1`.
    * Der geführte Einrichtungsassistent führt dich durch die WLAN-Verbindung und die Erstkonfiguration des Moduls.
3.  **Zugriff auf das Webinterface:**
    * Nach erfolgreicher WLAN-Verbindung erhält das Modul eine IP-Adresse von deinem Router.
    * Du findest die IP-Adresse oft über die Weboberfläche deines Routers heraus. Alternativ kannst du das Modul per mDNS unter dem konfigurierten `DEVICE_NAME` finden. Damit erreichst du das Modul unter http://[DEVICE_NAME].local (Standard: [wifiwhirl.local](http://wifiwhirl.local)).
    * Gib alternativ die IP-Adresse in deinem Browser ein, um auf das WifiWhirl-Webinterface zuzugreifen und deinen Pool zu steuern.

**Details zur WLAN Verbindung:** [wifiwhirl.de/Modul/WLAN](https://wifiwhirl.de/Modul/WLAN/)  
**Details zur Einrichtung:** [wifiwhirl.de/Modul/Einrichtung](https://wifiwhirl.de/Modul/Einrichtung/)

---

## Technologie-Stack

* Mikrocontroller: **ESP8266** (Beta: **ESP32**)
* Framework: **Arduino** ([PlatformIO](https://platformio.org/))
* Programmiersprachen: **Firmware:** C++, **Frontend:** TypeScript (Preact), HTML, CSS
* Frontend-Build: **Vite** mit **Preact** - wird automatisch gebaut und als gehashte, immutable-gecachte Bundles in die Firmware eingebettet
* Live-Updates: **HTTP-Polling** (`/getpolldata/`, `/sendcommand/`) - stabiler als WebSocket-Push auf dem ESP8266
* Wichtige Bibliotheken:
    * **ArduinoJson:** Verarbeitung von JSON-Daten.
    * **PubSubClient:** Für die MQTT-Integration in Smarthome-Systeme.
    * **EspSoftwareSerial:** Software-Emulation serieller Schnittstellen.
    * **PoolLink:** Verschlüsselte Verbindung zur optionalen WifiWhirl Cloud.

---

## Mitwirken

Du möchtest helfen, WifiWhirl zu verbessern? Beiträge sind willkommen!

1.  **Forke** das Repository.
2.  Erstelle einen neuen **Branch** (`git checkout -b feature/DeinFeature`).
3.  **Implementiere** deine Änderungen.
4.  **Committe** deine Änderungen (`git commit -m 'feat: Füge DeinFeature hinzu'`).
5.  **Pushe** zum Branch (`git push origin feature/DeinFeature`).
6.  Öffne einen **Pull Request**.

Bei größeren Änderungen oder neuen Features eröffne bitte zuerst ein [Issue](https://github.com/WifiWhirl/WifiWhirl-Software/issues), um die Idee zu diskutieren.

---

## Lizenz

Dieses Projekt steht unter der **GNU General Public License v3.0**. Die Details findest du in der [LICENSE](LICENSE)-Datei.

Wie erwähnt, basiert dieses Projekt auf der Arbeit von [visualapproach](https://github.com/visualapproach/), welche ebenfalls unter GPL-3.0 veröffentlicht wurde.

---

## Kontakt & Support

* **Hauptanlaufstelle & Dokumentation:** [wifiwhirl.de](https://wifiwhirl.de/)
* **Fertiges Modul kaufen:** [wifiwhirl.de/Modul/Kaufen](https://wifiwhirl.de/Modul/Kaufen/)
* **Probleme, Bugs oder Feature-Wünsche:** Bitte erstelle ein [GitHub Issue](https://github.com/WifiWhirl/WifiWhirl-Software/issues). Falls du ein Modul gekauft hast, folge den Anweisungen unter [wifiwhirl.de/Hilfe](https://wifiwhirl.de/Hilfe/)
* **Projekt-Repository:** [github.com/WifiWhirl/WifiWhirl-Software](https://github.com/WifiWhirl/WifiWhirl-Software)

---

*LAY-Z-SPA™ ist eine eingetragene Marke von Bestway Inflatables & Material Corp. Dieses Projekt steht in keiner Verbindung zu Bestway®.*

## English
The English version of this README is the main README.md: [https://github.com/WifiWhirl/WifiWhirl-Software/blob/master/README.md](https://github.com/WifiWhirl/WifiWhirl-Software/blob/master/README.md)

Für die ursprüngliche Software besuche bitte das visualapproach Repository: [https://github.com/visualapproach/WiFi-remote-for-Bestway-Lay-Z-SPA](https://github.com/visualapproach/WiFi-remote-for-Bestway-Lay-Z-SPA)
