# WifiWhirl - Make your LAY-Z-SPA™ Whirlpool Smart!

[![License: GPL v3](https://img.shields.io/badge/License-GPL%20v3-blue.svg?style=flat-square)](https://www.gnu.org/licenses/gpl-3.0)
[![Platform](https://img.shields.io/badge/Platform-ESP8266%20%7C%20ESP32%20(beta)-orange.svg?style=flat-square)](https://www.espressif.com/en/products/socs/esp8266)
[![Documentation](https://img.shields.io/badge/Guide-Documentation-success.svg?style=flat-square)](https://wifiwhirl.de/)
[![Buy](https://img.shields.io/badge/Module-Buy-ff69b4.svg?style=flat-square)](https://wifiwhirl.de/Modul/Kaufen/)

<p align="center">
<img src="./wifiwhirl_logo.png" alt="WifiWhirl Logo" width="80"/>
</p>

**Control and monitor your Bestway® LAY-Z-SPA™ whirlpool conveniently via WiFi with the WifiWhirl software.**

---

## Quick Access

* **Comprehensive Guide (Installation, DIY & Usage):** [wifiwhirl.de](https://wifiwhirl.de/)
* **DIY Guide:** [wifiwhirl.de/Selbstbau/Einfuehrung](https://wifiwhirl.de/Selbstbau/Einfuehrung/) (without cloud functionality)
* **Buy Ready-Made Module:** [wifiwhirl.de/Modul/Kaufen](https://wifiwhirl.de/Modul/Kaufen/) (with cloud functionality)
* **Questions & Discussion (GitHub Issues):** [Issues](https://github.com/WifiWhirl/WifiWhirl-Software/issues)

---

## About the Project

The WifiWhirl software is an open-source software solution for the **ESP8266 microcontroller** (with beta support for the **ESP32**) that enables control and monitoring of **Bestway® LAY-Z-SPA™ whirlpool** pump units via a local WiFi network.

Want to start the heating remotely, automate the filter cycle, or integrate your pool into your smart home applications? With WifiWhirl, this is possible!

The project is largely based on the **software and circuit board layout by [visualapproach](https://github.com/visualapproach/)**. Since his work was published under the **GNU General Public License 3.0 (GPL-3.0)**, this WifiWhirl software adaptation is also under the same license.

This repository contains the **source code** for the adapted firmware, as well as the web frontend (a single-page app built with Preact and Vite that is embedded into the firmware).

**Note on Functionality:** The open-source software published here in the repository focuses on controlling the whirlpool in the **local network** (via web interface) and **MQTT integration** for smart home systems. The separately [available ready-made module](https://wifiwhirl.de/Modul/Kaufen/) additionally offers the **optional WifiWhirl Cloud (PoolLink)**: an encrypted remote connection for monitoring and controlling the whirlpool from anywhere, including weather data and outside temperature for the device location. Cloud-capable devices are provisioned at the factory with a unique key; open-source builds without this key simply hide the cloud features.

---

## Differences from Original Software (visualapproach)

Although the WifiWhirl software is based on the excellent work by [visualapproach](https://github.com/visualapproach/), there are substantial adaptations and extensions in this version:

* **Completely New Web Interface:** A unified single-page app with mobile-first navigation, customizable dashboard (drag & drop widgets), thermostat dial, dark mode, and PWA support.
* **Multilingual:** German and English, plus beta locales for French, Dutch, and Polish with automatic language detection.
* **Setup Assistant:** A guided first-run wizard in the web app replaces the WiFiManager captive portal.
* **Improved Heating Logic:** A frequent problem has been addressed: the **heating now remains active** even when a programmed filter cycle is running simultaneously.
* **Smart Schedule:** Intelligent heating scheduling with automatic calculation of the optimal start time based on current water, target, and ambient temperature - including daily or weekly recurrence.
* **Water Quality Monitoring:** Tracking of pH, chlorine, cyanuric acid, and alkalinity with timestamps and Home Assistant integration.
* **Energy Monitoring & Tariffs:** Real-time monitoring of power consumption and estimated costs, including a configurable night tariff with time window and weekend mode.
* **Expert Mode:** Configurable power values, automatic heat-loss calibration (W/K), and a Prometheus metrics endpoint (`/metrics`).
* **Host Mode for Rentals:** Restrict the physical control panel (buttons, maximum target temperature) for hotels, AirBnB, and holiday rentals while the web UI and MQTT remain fully usable.
* **Firmware Update via Web:** Opt-in self-update - the device fetches signed firmware from the manifest server, checks daily for new versions, and installs on request.
* **Security:** Optional global login for web UI, HTTP API, and WebSocket sessions, persistent "stay signed in" sessions with hashed tokens, and login throttling.
* **REST API:** Webhook endpoints (`/gettemps/`, `/getstates/`) for easy integration with external systems.
* **Optional Cloud Connection (PoolLink):** For users of the [ready-made module](https://wifiwhirl.de/Modul/Kaufen/), an encrypted cloud link enables remote access and provides weather data and outside temperature for exact heating time calculation.
* **Optimized Code:** Modularized codebase, leaner and more maintainable; live updates use HTTP polling instead of WebSocket push for better stability on the ESP8266.

---

## Features

* **Temperature:** Display current water temperature and set the target temperature.
* **Heating:** Activate and deactivate the heating function.
* **Filter Pump:** Turn the filter pump on and off.
* **Bubble Function:** Control bubble massage (AirJet™ and HydroJet™) with configurable timeouts.
* **Status Display:** Overview of all current states (heating on/off, filter on/off, temperature, etc.).
* **Web Interface:** Modern single-page app with customizable dashboard, dark mode, and PWA support - in German, English, and further beta languages.
* **MQTT Integration:** Connection to smart home systems like Home Assistant, ioBroker, etc. with comprehensive auto-discovery.
* **Water Quality:** Monitor pH level, chlorine, cyanuric acid, and alkalinity with timestamps.
* **Smart Schedule:** Intelligent heating scheduling - pool automatically at temperature when you want it, once or recurring.
* **Energy Monitoring:** Track power consumption and estimated costs, with night tariff support and currency selection (EUR, PLN, GBP).
* **Display Brightness:** Configurable brightness level and duration on button press.
* **Automation:** Command queue with global and per-command toggles, quick templates, backup/restore, and a flexible scheduler (once/hourly/daily/weekly/custom).
* **REST API:** Webhook endpoints for easy integration with external systems, with optional Basic Auth.
* **Firmware Update via Web:** Opt-in signed self-update with daily update check.
* **Expert Mode:** Heat-loss calibration, configurable power values, and Prometheus metrics.
* **Host Mode:** Panel restrictions for rental scenarios.
* **Optional Login:** Protect web UI and API with a global password.
* **Beta:** ESP32 support and MSPA pump support (ESP32 only).

---

## Hardware & DIY

With some time, effort, and skill, you can build the WifiWhirl module yourself.

* **Required Components:** ESP8266 (e.g., Wemos D1 Mini), level converter, connectors, optionally a housing.
* **To DIY Guide:** [wifiwhirl.de/Selbstbau/Einfuehrung](https://wifiwhirl.de/Selbstbau/Einfuehrung/)

---

## Flashing Software

To transfer (flash) the WifiWhirl software to an ESP8266, you need:

* An ESP8266 microcontroller (e.g., Wemos D1 Mini or NodeMCU).
* A Micro-USB or USB-C cable to connect to the computer.
* Flashing software: [PlatformIO](https://platformio.org/) with [Visual Studio Code](https://code.visualstudio.com/).
* Possibly drivers for your ESP8266's USB-to-serial chip (usually CH340 or CP210x).

**Basic Steps (see guide for details!):**

1.  **Clone or download repository:**
    ```bash
    git clone https://github.com/WifiWhirl/WifiWhirl-Software.git
    ```
2.  **Open project:** Open the `Code` folder in the project directory in VS Code with PlatformIO.
3.  **Install dependencies:** PlatformIO should automatically download required libraries.
4.  **Adjust configuration:** Rename the file `config.cpp.dist` in the `src` folder to `config.cpp` and check the settings in the file.
5.  **Connect ESP8266:** Connect the ESP8266 to your computer via USB.
6.  **Compile & Upload:** Start the build and upload process via PlatformIO (`Upload` button). The web frontend is built automatically and embedded into the firmware - a separate filesystem upload is not needed.

**Detailed Flashing Guide:** [https://wifiwhirl.de/Selbstbau/Software/](https://wifiwhirl.de/Selbstbau/Software/)

---

## Usage & Setup

After the software has been successfully flashed:

1.  **Connect Module:** Connect the WifiWhirl module to your LAY-Z-SPA™ control unit according to the guide. **Attention:** Only work with the pump disconnected from the power supply!  
    **Connection Guide:** [Model S100101](https://wifiwhirl.de/Modul/Montage-S100101/) or [Model S200102](https://wifiwhirl.de/Modul/Montage-S200102/)
2.  **First Start & Setup Assistant:**
    * On first start (or after a reset), the module creates its own WiFi access point (default name: `wifiwhirl`; configured in `config.cpp` via `DEVICE_NAME`).
    * Connect to this WiFi (default password: `wifiwhirl`; configured in `config.cpp` via `wmApPassword`).
    * Open a web browser and go to `http://192.168.4.1`.
    * The guided setup assistant walks you through connecting the module to your WiFi and completing the initial configuration.
3.  **Access the Web Interface:**
    * After a successful WiFi connection, the module receives an IP address from your router.
    * You can often find the IP address through your router's web interface. Alternatively, you can find the module via mDNS under the configured `DEVICE_NAME`. This allows you to reach the module at http://[DEVICE_NAME].local (default: [wifiwhirl.local](http://wifiwhirl.local)).
    * Alternatively, enter the IP address in your browser to access the WifiWhirl web interface and control your pool.

**Details on WiFi Connection:** [wifiwhirl.de/Modul/WLAN](https://wifiwhirl.de/Modul/WLAN/)  
**Details on Setup:** [wifiwhirl.de/Modul/Einrichtung](https://wifiwhirl.de/Modul/Einrichtung/)

---

## Technology Stack

* Microcontroller: **ESP8266** (beta: **ESP32**)
* Framework: **Arduino** ([PlatformIO](https://platformio.org/))
* Programming Languages: **Firmware:** C++, **Frontend:** TypeScript (Preact), HTML, CSS
* Frontend Build: **Vite** with **Preact** - built automatically and embedded into the firmware as hashed, immutable-cached bundles
* Live Updates: **HTTP polling** (`/getpolldata/`, `/sendcommand/`) - more stable than WebSocket push on the ESP8266
* Important Libraries:
    * **ArduinoJson:** Processing JSON data.
    * **PubSubClient:** For MQTT integration into smart home systems.
    * **EspSoftwareSerial:** Software emulation of serial interfaces.
    * **PoolLink:** Encrypted connection to the optional WifiWhirl Cloud.

---

## Contributing

Want to help improve WifiWhirl? Contributions are welcome!

1.  **Fork** the repository.
2.  Create a new **branch** (`git checkout -b feature/YourFeature`).
3.  **Implement** your changes.
4.  **Commit** your changes (`git commit -m 'feat: Add YourFeature'`).
5.  **Push** to the branch (`git push origin feature/YourFeature`).
6.  Open a **Pull Request**.

For larger changes or new features, please first open an [Issue](https://github.com/WifiWhirl/WifiWhirl-Software/issues) to discuss the idea.

---

## License

This project is under the **GNU General Public License v3.0**. You can find the details in the [LICENSE](LICENSE) file.

As mentioned, this project is based on the work by [visualapproach](https://github.com/visualapproach/), which was also published under GPL-3.0.

---

## Contact & Support

* **Main Hub & Documentation:** [wifiwhirl.de](https://wifiwhirl.de/)
* **Buy Ready-Made Module:** [wifiwhirl.de/Modul/Kaufen](https://wifiwhirl.de/Modul/Kaufen/)
* **Problems, Bugs or Feature Requests:** Please create a [GitHub Issue](https://github.com/WifiWhirl/WifiWhirl-Software/issues). If you bought a module, follow the instructions at [wifiwhirl.de/Hilfe](https://wifiwhirl.de/Hilfe/)
* **Project Repository:** [github.com/WifiWhirl/WifiWhirl-Software](https://github.com/WifiWhirl/WifiWhirl-Software)

---

*LAY-Z-SPA™ is a registered trademark of Bestway Inflatables & Material Corp. This project is not affiliated or associated with Bestway®.*

## Deutsch
Die deutsche Version dieser README findest du in der Datei README_DE.md: [https://github.com/WifiWhirl/WifiWhirl-Software/blob/master/README_DE.md](https://github.com/WifiWhirl/WifiWhirl-Software/blob/master/README_DE.md)

If you are searching for the original software please visit the visualapproach repo: [https://github.com/visualapproach/WiFi-remote-for-Bestway-Lay-Z-SPA](https://github.com/visualapproach/WiFi-remote-for-Bestway-Lay-Z-SPA)
