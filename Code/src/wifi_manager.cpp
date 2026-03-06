#include "main.h"

static sWifi_info _cachedWifi;
static bool _wifiCacheValid = false;

/**
 * Start a Wi-Fi access point, and try to connect to some given access points.
 * Then wait for either an AP or STA connection
 */
void startWiFi()
{
    // WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.persistent(true);
    WiFi.hostname(netHostname);
    sWifi_info wifi_info;
    wifi_info = loadWifi();

    if (wifi_info.enableStaticIp4)
    {
        Serial.println(F("Setting static IP"));
        IPAddress ip4Address;
        IPAddress ip4Gateway;
        IPAddress ip4Subnet;
        IPAddress ip4DnsPrimary;
        IPAddress ip4DnsSecondary;
        ip4Address.fromString(wifi_info.ip4Address_str);
        ip4Gateway.fromString(wifi_info.ip4Gateway_str);
        ip4Subnet.fromString(wifi_info.ip4Subnet_str);
        ip4DnsPrimary.fromString(wifi_info.ip4DnsPrimary_str);
        ip4DnsSecondary.fromString(wifi_info.ip4DnsSecondary_str);
        // Serial.println("WiFi > using static IP \"" + ip4Address.toString() + "\" on gateway \"" + ip4Gateway.toString() + "\"");
        WiFi.config(ip4Address, ip4Gateway, ip4Subnet, ip4DnsPrimary, ip4DnsSecondary);
    }

    if (wifi_info.enableAp)
    {
        Serial.print(F("WiFi > using WiFi configuration with SSID \""));
        Serial.println(wifi_info.apSsid + "\"");

        WiFi.begin(wifi_info.apSsid.c_str(), wifi_info.apPwd.c_str());

        Serial.print(F("WiFi > Trying to connect ..."));
        int maxTries = 10;
        int tryCount = 0;

        while (WiFi.status() != WL_CONNECTED)
        {
            delay(1000);
            bwc->loop();
            tryCount++;

            if (tryCount >= maxTries)
            {
                if (wifi_info.enableWmApFallback)
                {
                    startWiFiConfigPortal(wifi_info.apSsid, wifi_info.apPwd);
                }
                break;
            }
        }
    }
    else
    {
        startWiFiConfigPortal();
    }

    if (WiFi.status() == WL_CONNECTED)
    {
        wifi_info.enableAp = true;
        wifi_info.apSsid = WiFi.SSID();
        wifi_info.apPwd = WiFi.psk();
        saveWifi(wifi_info);

        wifiConnected = true;

        // Serial.println(F("WiFi > Connected."));
        // Serial.println(" SSID: \"" + WiFi.SSID() + "\"");
        // Serial.println(" IP: \"" + WiFi.localIP().toString() + "\"");
    }
    else
    {
        // Serial.println(F("WiFi > Connection failed. Retrying in a while ..."));
    }
}

/**
 * start WiFiManager configuration portal
 */
void startWiFiConfigPortal(const String &storedSsid, const String &storedPwd)
{
    Serial.println(F("WiFi > Using WiFiManager Config Portal"));
    ESP_WiFiManager wm;

    WiFiManagerParameter custom_text("<p><strong>Willkommen zur Einrichtung deines WifiWhirl WLAN-Moduls!</strong></p><p>Verbinde dich hier mit deinem WLAN, um mit der Einrichtung zu beginnen.</p>");

    wm.setClass("invert");             // WM Dark Mode
    wm.setShowInfoErase(false);        // WM Disable Erase Button
    wm.addParameter(&custom_text);     // WM Show WifiWhirl Text
    wm.setConfigPortalBlocking(false); // WM non-blocking mode so we can update display

    // Display "net" on pump while in AP mode
    bwc->printStatic("net");

    wm.autoConnect(wmApName, wmApPassword);

    unsigned long lastReconnectAttempt = 0;
    const unsigned long reconnectInterval = 10000;
    bool hasStoredCredentials = (storedSsid.length() > 0);

    while (WiFi.status() != WL_CONNECTED)
    {
        wm.process();
        bwc->loop();
        delay(100);

        if (hasStoredCredentials && (millis() - lastReconnectAttempt >= reconnectInterval))
        {
            lastReconnectAttempt = millis();
            Serial.println(F("WiFi > Config Portal: Retrying stored network..."));
            WiFi.begin(storedSsid.c_str(), storedPwd.c_str());

            unsigned long connectStart = millis();
            while (WiFi.status() != WL_CONNECTED && (millis() - connectStart) < 15000)
            {
                wm.process();
                bwc->loop();
                delay(100);
            }

            if (WiFi.status() == WL_CONNECTED)
            {
                Serial.println(F("WiFi > Reconnected to stored network"));
            }
        }
    }

    bwc->clearStatic();
}

/**
 * start NTP sync
 */
void startNTP()
{
    sWifi_info wifi_info;
    wifi_info = loadWifi();
    Serial.println(F("start NTP"));
    if (wifi_info.ip4NTP_str.length() > 0)
    {
        static char ntpServer[64];
        strlcpy(ntpServer, wifi_info.ip4NTP_str.c_str(), sizeof(ntpServer));
        configTime(0, 0, ntpServer);
        Serial.print(F("NTP server: "));
        Serial.println(ntpServer);
    }
    else
    {
        configTime(0, 0, "ptbtime1.ptb.de", "ptbtime2.ptb.de", "ptbtime3.ptb.de");
    }
    time_t now = time(nullptr);
    int count = 0;
    while (now < 8 * 3600 * 2)
    {
        delay(500);
        Serial.print(".");
        now = time(nullptr);
        if (count++ > 10)
            return;
    }
    Serial.println();
    struct tm timeinfo;
    gmtime_r(&now, &timeinfo);
    // Serial.print("Current time: ");
    // Serial.print(asctime(&timeinfo));

    time_t boot_timestamp = getBootTime();
    tm *boot_time_tm = gmtime(&boot_timestamp);
    char boot_time_str[64];
    strftime(boot_time_str, 64, "%F %T", boot_time_tm);
    bwc->reboot_time_str = String(boot_time_str);
    bwc->reboot_time_t = boot_timestamp;
}

/**
 * load WiFi json configuration from "wifi.json"
 */
sWifi_info loadWifi()
{
    if (_wifiCacheValid)
        return _cachedWifi;

    sWifi_info wifi_info;
    File file = LittleFS.open("/wifi.json", "r");
    if (!file)
    {
        return wifi_info;
    }

    StaticJsonDocument<512> doc;

    DeserializationError error = deserializeJson(doc, file);
    file.close();
    if (error)
    {
        return wifi_info;
    }

    wifi_info.enableAp = doc[F("enableAp")];
    if (doc.containsKey("enableWM"))
        wifi_info.enableWmApFallback = doc[F("enableWM")];
    wifi_info.apSsid = doc[F("apSsid")].as<String>();
    wifi_info.apPwd = doc[F("apPwd")].as<String>();

    wifi_info.enableStaticIp4 = doc[F("enableStaticIp4")];
    wifi_info.ip4Address_str = doc[F("ip4Address")].as<String>();
    wifi_info.ip4Gateway_str = doc[F("ip4Gateway")].as<String>();
    wifi_info.ip4Subnet_str = doc[F("ip4Subnet")].as<String>();
    wifi_info.ip4DnsPrimary_str = doc[F("ip4DnsPrimary")].as<String>();
    wifi_info.ip4DnsSecondary_str = doc[F("ip4DnsSecondary")].as<String>();
    wifi_info.ip4NTP_str = doc[F("ip4NTP")].as<String>();

    _cachedWifi = wifi_info;
    _wifiCacheValid = true;

    return wifi_info;
}

/**
 * save WiFi json configuration to "wifi.json"
 */
void saveWifi(const sWifi_info &wifi_info)
{
    File file = LittleFS.open("/wifi.json", "w");
    if (!file)
    {
        return;
    }

    StaticJsonDocument<512> doc;

    doc[F("enableAp")] = wifi_info.enableAp;
    doc[F("enableWM")] = wifi_info.enableWmApFallback;
    doc[F("apSsid")] = wifi_info.apSsid;
    doc[F("apPwd")] = wifi_info.apPwd;
    doc[F("enableStaticIp4")] = wifi_info.enableStaticIp4;
    doc[F("ip4Address")] = wifi_info.ip4Address_str;
    doc[F("ip4Gateway")] = wifi_info.ip4Gateway_str;
    doc[F("ip4Subnet")] = wifi_info.ip4Subnet_str;
    doc[F("ip4DnsPrimary")] = wifi_info.ip4DnsPrimary_str;
    doc[F("ip4DnsSecondary")] = wifi_info.ip4DnsSecondary_str;
    doc[F("ip4NTP")] = wifi_info.ip4NTP_str;

    if (serializeJson(doc, file) == 0)
    {
    }
    file.close();

    _cachedWifi = wifi_info;
    _wifiCacheValid = true;
}

/**
 * response for /scanwifi/
 * Scans for available WiFi networks and returns them as JSON
 */
void handleScanWifi()
{
    // Synchronous scan; typically completes in 2-5 seconds
    int n = WiFi.scanNetworks(false, false);

    DynamicJsonDocument doc(2048);
    JsonArray networks = doc.createNestedArray(F("networks"));

    for (int i = 0; i < n && i < 20; i++)
    {
        // Skip empty SSIDs (hidden networks)
        if (WiFi.SSID(i).length() == 0)
            continue;

        // Skip duplicate SSIDs (keep the one with stronger signal)
        bool duplicate = false;
        for (size_t j = 0; j < networks.size(); j++)
        {
            if (networks[j]["ssid"].as<String>() == WiFi.SSID(i))
            {
                duplicate = true;
                break;
            }
        }
        if (duplicate)
            continue;

        JsonObject net = networks.createNestedObject();
        net[F("ssid")] = WiFi.SSID(i);
        net[F("rssi")] = WiFi.RSSI(i);
        net[F("enc")] = (WiFi.encryptionType(i) != ENC_TYPE_NONE);
    }

    WiFi.scanDelete();

    String json;
    if (serializeJson(doc, json) == 0)
    {
        json = F("{\"networks\":[]}");
    }
    server->send(200, F("application/json"), json);
}

/**
 * response for /getwifi/
 * web server prints a json document
 */
void handleGetWifi()
{
    if (!checkHttpPost(server->method()))
        return;

    StaticJsonDocument<512> doc;

    sWifi_info wifi_info;
    wifi_info = loadWifi();

    doc[F("enableAp")] = wifi_info.enableAp;
    doc[F("enableWM")] = wifi_info.enableWmApFallback;
    doc[F("apSsid")] = wifi_info.apSsid;
    doc[F("apPwd")] = F("<Passwort eingeben>");
    if (!hidePasswords)
    {
        doc[F("apPwd")] = wifi_info.apPwd;
    }

    doc[F("enableStaticIp4")] = wifi_info.enableStaticIp4;
    doc[F("ip4Address")] = wifi_info.ip4Address_str;
    doc[F("ip4Gateway")] = wifi_info.ip4Gateway_str;
    doc[F("ip4Subnet")] = wifi_info.ip4Subnet_str;
    doc[F("ip4DnsPrimary")] = wifi_info.ip4DnsPrimary_str;
    doc[F("ip4DnsSecondary")] = wifi_info.ip4DnsSecondary_str;
    doc[F("ip4NTP")] = wifi_info.ip4NTP_str;
    doc[F("wmApName")] = wmApName;
    String json;
    if (serializeJson(doc, json) == 0)
    {
        json = F("{\"error\": \"Failed to serialize message\"}");
    }
    server->send(200, F("application/json"), json);
}

/**
 * response for /setwifi/
 * web server writes a json document
 */
void handleSetWifi()
{
    if (!checkHttpPost(server->method()))
        return;

    StaticJsonDocument<512> doc;
    String message = server->arg(0);
    DeserializationError error = deserializeJson(doc, message);
    if (error)
    {
        // Serial.println(F("Failed to read config file"));
        server->send(400, F("text/plain"), F("Error deserializing message"));
        return;
    }

    // Load existing settings first to support partial updates.
    // Each section on the frontend sends only its own fields,
    // so missing fields must retain their previous values.
    sWifi_info old_info = loadWifi();
    sWifi_info wifi_info = old_info;

    if (doc.containsKey(F("enableAp")))
        wifi_info.enableAp = doc[F("enableAp")];
    if (doc.containsKey(F("enableWM")))
        wifi_info.enableWmApFallback = doc[F("enableWM")];
    if (doc.containsKey(F("apSsid")))
        wifi_info.apSsid = doc[F("apSsid")].as<String>();
    if (doc.containsKey(F("apPwd")))
        wifi_info.apPwd = doc[F("apPwd")].as<String>();

    if (doc.containsKey(F("enableStaticIp4")))
        wifi_info.enableStaticIp4 = doc[F("enableStaticIp4")];
    if (doc.containsKey(F("ip4Address")))
        wifi_info.ip4Address_str = doc[F("ip4Address")].as<String>();
    if (doc.containsKey(F("ip4Gateway")))
        wifi_info.ip4Gateway_str = doc[F("ip4Gateway")].as<String>();
    if (doc.containsKey(F("ip4Subnet")))
        wifi_info.ip4Subnet_str = doc[F("ip4Subnet")].as<String>();
    if (doc.containsKey(F("ip4DnsPrimary")))
        wifi_info.ip4DnsPrimary_str = doc[F("ip4DnsPrimary")].as<String>();
    if (doc.containsKey(F("ip4DnsSecondary")))
        wifi_info.ip4DnsSecondary_str = doc[F("ip4DnsSecondary")].as<String>();
    if (doc.containsKey(F("ip4NTP")))
        wifi_info.ip4NTP_str = doc[F("ip4NTP")].as<String>();

    // Detect what changed: NTP can be applied live, everything else needs a reboot
    bool ntpChanged = (wifi_info.ip4NTP_str != old_info.ip4NTP_str);
    bool networkChanged = (wifi_info.enableAp != old_info.enableAp) ||
                          (wifi_info.enableWmApFallback != old_info.enableWmApFallback) ||
                          (wifi_info.apSsid != old_info.apSsid) ||
                          (wifi_info.apPwd != old_info.apPwd) ||
                          (wifi_info.enableStaticIp4 != old_info.enableStaticIp4) ||
                          (wifi_info.ip4Address_str != old_info.ip4Address_str) ||
                          (wifi_info.ip4Gateway_str != old_info.ip4Gateway_str) ||
                          (wifi_info.ip4Subnet_str != old_info.ip4Subnet_str) ||
                          (wifi_info.ip4DnsPrimary_str != old_info.ip4DnsPrimary_str) ||
                          (wifi_info.ip4DnsSecondary_str != old_info.ip4DnsSecondary_str);

    if (!ntpChanged && !networkChanged)
    {
        // Nothing changed, no need to save or restart
        Serial.println(F("WiFi: No changes detected"));
        server->send(200, F("application/json"), F("{\"restart\":false}"));
        return;
    }

    saveWifi(wifi_info);

    if (ntpChanged && !networkChanged)
    {
        // Only NTP changed - apply live without reboot
        Serial.println(F("WiFi: NTP server changed, applying live"));
        startNTP();
        server->send(200, F("application/json"), F("{\"restart\":false,\"saved\":true}"));
        return;
    }

    // Network settings changed - restart required to apply
    Serial.println(F("WiFi: Network config changed, restarting..."));

    String response = F("{\"restart\":true,\"reason\":\"Netzwerkeinstellungen geändert. WifiWhirl startet neu.\"}");
    server->send(200, F("application/json"), response);

    // Give server time to send response
    delay(500);

    // Save all settings
    bwc->saveSettings();

    // Stop all services gracefully
    periodicTimer.detach();
    updateMqttTimer.detach();
    updateWSTimer.detach();
    if (mqttClient)
    {
        mqttClient->disconnect();
    }

    delay(1000);

    // Restart ESP to apply new network configuration
    ESP.restart();
}

/*
 * response for /resetwifi/
 * do this before giving away the device (be aware of other credentials e.g. MQTT)
 * a complete flash erase should do the job but remember to upload the filesystem as well.
 */
void handleResetWifi()
{
    server->send(200, F("text/html"), F("WLAN Einstellungen werden gelöscht ..."));
    // Serial.println(F("WiFi connection reset (erase) ..."));
    resetWiFi();

    // server->send(200, F("text/html"), F("WLAN Einstellungen wurden gelöscht ..."));
// Serial.println(F("WiFi connection reset (erase) ... done."));
// Serial.println(F("ESP reset ..."));
#if defined(ESP8266)
    ESP.reset();
#else
    ESP.restart();
#endif
}

void resetWiFi()
{
    sWifi_info wifi_info;
    wifi_info.enableAp = false;
    wifi_info.enableWmApFallback = true;
    wifi_info.apSsid = "";
    wifi_info.apPwd = "";
    saveWifi(wifi_info);

    WiFi.disconnect(true);
    WiFi.softAPdisconnect(true);
    delay(500);

    periodicTimer.detach();
    updateMqttTimer.detach();
    updateWSTimer.detach();
    bwc->stop();
    bwc->saveSettings();
    delay(1000);
    //     delay(1000);
}
