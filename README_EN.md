# 🌀 WifiWhirl - Make your LAY-Z-SPA™ Whirlpool Smart! 🌀

[![License: GPL v3](https://img.shields.io/badge/License-GPL%20v3-blue.svg?style=flat-square)](https://www.gnu.org/licenses/gpl-3.0)
[![Platform](https://img.shields.io/badge/Platform-ESP8266-orange.svg?style=flat-square)](https://www.espressif.com/en/products/socs/esp8266)
[![Documentation](https://img.shields.io/badge/Guide-Documentation-success.svg?style=flat-square)](https://wifiwhirl.de/)
[![Buy](https://img.shields.io/badge/Module-Buy-ff69b4.svg?style=flat-square)](https://wifiwhirl.de/Modul/Kaufen/)

<p align="center">
<img src="./wifiwhirl_logo.png" alt="WifiWhirl Logo" width="80"/>
</p>

**Control and monitor your Bestway® LAY-Z-SPA™ whirlpool conveniently via WiFi with the WifiWhirl software.**

---

## 🚀 Quick Access

* **➡️ Comprehensive Guide (Installation, DIY & Usage):** [wifiwhirl.de](https://wifiwhirl.de/)
* **🛠️ DIY Guide:** [wifiwhirl.de/Selbstbau/Einfuehrung](https://wifiwhirl.de/Selbstbau/Einfuehrung/) (without cloud functionalities)
* **🛒 Buy Ready-Made Module:** [wifiwhirl.de/Modul/Kaufen](https://wifiwhirl.de/Modul/Kaufen/) (with cloud functionalities)
* **❓ Questions & Discussion (GitHub Issues):** [Issues](https://github.com/WifiWhirl/WifiWhirl-Software/issues)

---

## 👋 About the Project

The WifiWhirl software is an open-source software solution for the **ESP8266 microcontroller** that enables control and monitoring of **Bestway® LAY-Z-SPA™ whirlpool** pump units via a local WiFi network.

Want to start the heating remotely, automate the filter cycle, or integrate your pool into your smart home applications? With WifiWhirl, this is possible!

The project is largely based on the **software and circuit board layout by [visualapproach](https://github.com/visualapproach/)**. Since his work was published under the **GNU General Public License 3.0 (GPL-3.0)**, this WifiWhirl software adaptation is also under the same license.

This repository contains the **source code** for the adapted ESP8266 firmware, as well as the adapted web frontend.

**Note on Functionality:** The open-source software published here in the repository focuses on controlling the whirlpool in the **local network** (via web interface) and **MQTT integration** for smart home systems. The separately [available ready-made module](https://wifiwhirl.de/Modul/Kaufen/) additionally offers **optional cloud functions**. Currently, these include retrieving weather data for ambient temperature based on a German or Austrian postal code. These cloud-specific extensions are not part of this repository.

---

## ↔️ Differences from Original Software (visualapproach)

Although the WifiWhirl software is based on the excellent work by [visualapproach](https://github.com/visualapproach/), there are some adaptations and extensions in this version:

* 🌐 **German User Interface:** The entire web interface has been completely **translated into German** for intuitive operation in German-speaking regions.
* ⏰ **Optimized Automation:** Dedicated automation page with command queue, backup/restore, and improved configuration.
* 🧹 **Optimized Code:** Functions and code parts from the original software that were not absolutely necessary for pure whirlpool control have been removed to make the **codebase leaner and more maintainable**.
* 🔥 **Improved Heating Logic:** A frequent problem has been addressed: The **heating now remains active** even when a programmed filter cycle is running simultaneously.
* ⏰ **Smart Schedule:** Intelligent heating scheduling with automatic calculation of optimal start time based on current water, target, and ambient temperature.
* 🧪 **Water Quality Monitoring:** Tracking of pH, chlorine, cyanuric acid, and alkalinity with timestamps and Home Assistant integration.
* ⚡ **Energy Monitoring:** Real-time monitoring of power consumption and estimated costs in the dashboard.
* 🔌 **REST API:** Webhook endpoints (`/gettemps/`, `/getstates/`) for easy integration with external systems.
* ☁️ **Optional Cloud Connection:** For users of the [ready-made module](https://wifiwhirl.de/Modul/Kaufen/), an **optional cloud functionality** has been integrated. This enables retrieving weather data for the location (based on postal code for DE/AT) to determine outside temperature and calculate exact heating time (more cloud functions will follow).

---

## ✨ Features

* 🌡️ **Temperature:** Display current water temperature and set target temperature.
* 🔥 **Heating:** Activate and deactivate heating function.
* 💧 **Filter Pump:** Turn filter pump on and off.
* 💨 **Bubble Function:** Control bubble massage (AirJet™ and HydroJet™) with configurable timeouts.
* 📊 **Status Display:** Overview of all current states (heating on/off, filter on/off, temperature, etc.).
* 🌐 **Web Interface:** Easy operation via web interface in the local network with dark mode.
* 📲 **MQTT Integration:** Connection to smart home systems like Home Assistant, ioBroker, etc. with comprehensive auto-discovery.
* 🧪 **Water Quality:** Monitor pH level, chlorine, cyanuric acid, and alkalinity with timestamps.
* ⏰ **Smart Schedule:** Intelligent heating scheduling - pool automatically at temperature when you want it.
* ⚡ **Energy Monitoring:** Track power consumption and estimated costs.
* 📡 **WiFi Scanning:** Automatic detection of available WiFi networks with signal strength display.
* 🔄 **Automation:** Dedicated page for command queue configuration with backup/restore.
* 🔌 **REST API:** Webhook endpoints for easy integration with external systems.
* 🌐 **HTTP Polling Fallback:** Optional polling mode as alternative for WebSocket connection issues.

---

## 🛠️ Hardware & DIY

With some time, effort, and skill, you can build the WifiWhirl module yourself.

* **Required Components:** ESP8266 (e.g., Wemos D1 Mini), level converter, connectors, optionally a housing.
* **➡️ To DIY Guide:** [wifiwhirl.de/Selbstbau/Einfuehrung](https://wifiwhirl.de/Selbstbau/Einfuehrung/)

---

## 💾 Flashing Software

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
4.  **Adjust configuration:** Rename the file `config.h.dist` in the `src` folder to `config.h` and check the settings in the file.
5.  **Connect ESP8266:** Connect the ESP8266 to your computer via USB.
6.  **Compile & Upload:** Start the build and upload process via PlatformIO (`Upload` button). Frontend assets are embedded in the firmware - a separate filesystem upload is no longer needed.

**➡️ Detailed Flashing Guide:** [https://wifiwhirl.de/Selbstbau/Software/](https://wifiwhirl.de/Selbstbau/Software/)

---

## 💡 Usage & Setup

After the software has been successfully flashed to the ESP8266:

1.  **Connect Module:** Connect the WifiWhirl module to your LAY-Z-SPA™ control unit according to the guide. **Attention:** Only work with the pump disconnected from the power supply!  
    **➡️ Connection Guide:** [Model S100101](https://wifiwhirl.de/Modul/Montage-S100101/) or [Model S200102](https://wifiwhirl.de/Modul/Montage-S200102/)
2.  **First Start & WiFi Configuration:**
    * On first start (or after a reset), the module creates its own WiFi access point (default name: `wifiwhirl`. The name is configured in the `config.h` file under `DEVICE_NAME`).
    * Connect to this WiFi (default password: `wifiwhirl-AP`. The password is configured in the `config.h` file under `wmApPassword`).
    * Open a web browser and go to address `http://192.168.4.1`.
    * Follow the instructions to connect the module to your WiFi.
3.  **Access the Web Interface:**
    * After successful WiFi connection, the module receives an IP address from your router.
    * You can often find the IP address through your router's web interface. Alternatively, you can find the module via mDNS under the configured `DEVICE_NAME`. This allows you to reach the module at http://[DEVICE_NAME].local (default: [wifiwhirl.local](http://wifiwhirl.local)).
    * Alternatively, enter the IP address in your browser to access the WifiWhirl web interface and control your pool.

**➡️ Details on WiFi Connection:** [wifiwhirl.de/Modul/WLAN](https://wifiwhirl.de/Modul/WLAN/)  
**➡️ Details on Setup:** [wifiwhirl.de/Modul/Einrichtung](https://wifiwhirl.de/Modul/Einrichtung/)

---

## ⚙️ Technology Stack

* Microcontroller: **ESP8266**
* Framework: **Arduino for ESP8266** ([PlatformIO](https://platformio.org/))
* Programming Languages: **Backend:** C++, **Frontend:** HTML, CSS, JS
* Important Libraries:
    * **ArduinoJson:** Processing JSON data.
    * **WebSockets:** For real-time communication with the frontend.
    * **PubSubClient:** For MQTT integration into smart home systems.
    * **ESPAsyncTCP** & **ESPAsyncWebServer:** Foundation for asynchronous web server and websocket communication.
    * **EspSoftwareSerial:** Software emulation of serial interfaces.
    * **WiFiManager:** For easy configuration of WiFi connection.

---

## 🤝 Contributing

Want to help improve WifiWhirl? Contributions are welcome!

1.  **Fork** the repository.
2.  Create a new **branch** (`git checkout -b feature/YourFeature`).
3.  **Implement** your changes.
4.  **Commit** your changes (`git commit -m 'feat: Add YourFeature'`).
5.  **Push** to the branch (`git push origin feature/YourFeature`).
6.  Open a **Pull Request**.

For larger changes or new features, please first open an [Issue](https://github.com/WifiWhirl/WifiWhirl-Software/issues) to discuss the idea.

**One-time pre-commit hook setup** (secret scanning via [gitleaks](https://github.com/gitleaks/gitleaks) and linting of the Python build scripts via [ruff](https://github.com/astral-sh/ruff)):
```bash
pip install pre-commit
pre-commit install
```
Both checks then run automatically before every commit.

---

## 📄 License

This project is under the **GNU General Public License v3.0**. You can find the details in the [LICENSE](LICENSE) file.

As mentioned, this project is based on the work by [visualapproach](https://github.com/visualapproach/), which was also published under GPL-3.0.

---

## 📫 Contact & Support

* **Main Hub & Documentation:** [wifiwhirl.de](https://wifiwhirl.de/)
* **Buy Ready-Made Module:** [wifiwhirl.de/Modul/Kaufen](https://wifiwhirl.de/Modul/Kaufen/)
* **Problems, Bugs or Feature Requests:** Please create a [GitHub Issue](https://github.com/WifiWhirl/WifiWhirl-Software/issues). If you bought a module, follow the instructions at [wifiwhirl.de/Hilfe](https://wifiwhirl.de/Hilfe/)
* **Project Repository:** [github.com/WifiWhirl/WifiWhirl-Software](https://github.com/WifiWhirl/WifiWhirl-Software)

---

*LAY-Z-SPA™ is a registered trademark of Bestway Inflatables & Material Corp. This project is not affiliated or associated with Bestway®.*

## Deutsch
Wenn du nach der deutschen Version dieser README suchst, findest du diese in der README.md: [https://github.com/WifiWhirl/WifiWhirl-Software/blob/master/README.md](https://github.com/WifiWhirl/WifiWhirl-Software/blob/master/README.md)

Für die ursprüngliche englische Software besuchen Sie bitte das visualapproach Repository: [https://github.com/visualapproach/WiFi-remote-for-Bestway-Lay-Z-SPA](https://github.com/visualapproach/WiFi-remote-for-Bestway-Lay-Z-SPA)