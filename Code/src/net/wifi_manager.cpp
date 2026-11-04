#include "net/net.h"
#include "api/api.h"
#include "web/web.h"

static sWifi_info _cachedWifi;
static bool _wifiCacheValid = false;

// SoftAP captive-portal state. apSetupMode is read by handleNotFound (to steer
// every request to the SPA) and handleAuthStatus (so the SPA opens the wizard
// on its WiFi step).
bool apSetupMode = false;
static DNSServer _setupDns;

bool isValidHostnameOrIp(const String &host, bool allowEmpty)
{
    String s = host;
    s.trim();
    if (s.length() == 0)
        return allowEmpty;
    if (s.length() > 253)
        return false;

    IPAddress ip;
    if (ip.fromString(s))
        return true;

    uint8_t labelLen = 0;
    char prev = 0;
    for (size_t i = 0; i < s.length(); i++)
    {
        char c = s[i];
        bool ok = (c >= 'A' && c <= 'Z') ||
                  (c >= 'a' && c <= 'z') ||
                  (c >= '0' && c <= '9') ||
                  c == '-' || c == '.';
        if (!ok)
            return false;
        if (c == '.')
        {
            if (labelLen == 0 || prev == '-')
                return false;
            labelLen = 0;
        }
        else
        {
            if (labelLen == 0 && c == '-')
                return false;
            labelLen++;
            if (labelLen > 63)
                return false;
        }
        prev = c;
    }
    return labelLen > 0 && prev != '-';
}

static bool isValidIpv4(const String &value)
{
    String s = value;
    s.trim();
    IPAddress ip;
    return s.length() > 0 && ip.fromString(s);
}

static bool validateWifiInfo(const sWifi_info &wifi_info, String &error)
{
    if (wifi_info.enableAp)
    {
        if (wifi_info.apSsid.length() == 0 || wifi_info.apSsid.length() > 32)
        {
            error = F("Invalid WiFi SSID");
            return false;
        }
        if (wifi_info.apPwd.length() > 0 &&
            (wifi_info.apPwd.length() < 8 || wifi_info.apPwd.length() > 63))
        {
            error = F("Invalid WiFi password length");
            return false;
        }
    }

    if (wifi_info.enableStaticIp4)
    {
        if (!isValidIpv4(wifi_info.ip4Address_str) ||
            !isValidIpv4(wifi_info.ip4Gateway_str) ||
            !isValidIpv4(wifi_info.ip4Subnet_str) ||
            !isValidIpv4(wifi_info.ip4DnsPrimary_str) ||
            (wifi_info.ip4DnsSecondary_str.length() > 0 && !isValidIpv4(wifi_info.ip4DnsSecondary_str)))
        {
            error = F("Invalid static IPv4 configuration");
            return false;
        }
    }

    if (!isValidHostnameOrIp(wifi_info.ip4NTP_str, true))
    {
        error = F("Invalid NTP server");
        return false;
    }

    return true;
}

/**
 * Start a Wi-Fi access point, and try to connect to some given access points.
 * Then wait for either an AP or STA connection
 */
void startWiFi()
{
#ifdef ESP8266
    // No modem sleep, from the start: it was only switched off when
    // the Home Assistant discovery ran, so in cloud mode or without MQTT the
    // module kept the SDK default. Mains powered, and sleep adds latency and
    // lost frames on weak links. TX power (20.5 dBm) and PHY mode (11n, b/g
    // allowed) stay at the SDK defaults, which are already the best choice.
    WiFi.setSleepMode(WIFI_NONE_SLEEP);
#endif
    // WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.persistent(true);
    WiFi.hostname(netHostname.c_str());
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
        if (ip4Address.fromString(wifi_info.ip4Address_str) &&
            ip4Gateway.fromString(wifi_info.ip4Gateway_str) &&
            ip4Subnet.fromString(wifi_info.ip4Subnet_str) &&
            ip4DnsPrimary.fromString(wifi_info.ip4DnsPrimary_str) &&
            (wifi_info.ip4DnsSecondary_str.length() == 0 || ip4DnsSecondary.fromString(wifi_info.ip4DnsSecondary_str)))
        {
            WiFi.config(ip4Address, ip4Gateway, ip4Subnet, ip4DnsPrimary, ip4DnsSecondary);
        }
        else
        {
            Serial.println(F("WiFi: Invalid static IP config, using DHCP"));
        }
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
            handleResetSequence();
            tryCount++;

            if (tryCount >= maxTries)
            {
                if (wifi_info.enableWmApFallback)
                {
                    startSetupPortal(wifi_info.apSsid, wifi_info.apPwd);
                }
                break;
            }
        }
    }
    else
    {
        // Always hand over whatever is stored. A portal without credentials can
        // never call WiFi.begin(), so the device would sit in the AP forever with
        // no way back through the UI.
        startSetupPortal(wifi_info.apSsid, wifi_info.apPwd);
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
 * Setup Assistant captive portal (replaces WiFiManager).
 *
 * Brings up our own SoftAP + DNS hijack and serves the embedded SPA as the
 * portal UI: joining the AP pops the OS "sign in to network" sheet, which loads
 * the SPA's onboarding wizard (WiFi step). The user picks a network in the SPA
 * and POSTs /setwifi/, which reboots the device to connect with the new
 * credentials. Meanwhile we keep retrying any stored network so a transient
 * outage recovers without user action (mimics WiFiManager). Blocks until
 * WL_CONNECTED, then tears the AP down so boot continues in STA mode.
 */
void startSetupPortal(const String &storedSsid, const String &storedPwd)
{
    Serial.println(F("WiFi > Starting Setup Assistant captive portal"));
    apSetupMode = true;

    // Start the portal from a clean, disconnected STA. WIFI_AP_STA keeps the STA
    // interface live, and with auto-reconnect on the SDK would silently rejoin a
    // still-persisted network in the background, exit the wait loop immediately,
    // and let startWiFi re-save those credentials - which after the button reset
    // sequence would re-adopt the old network ("wifi stays connected"). We retry
    // a known-good stored network explicitly below instead.
    WiFi.setAutoReconnect(false);
    WiFi.mode(WIFI_AP_STA);
    WiFi.disconnect();
    WiFi.softAP(wmApName.c_str(), wmApPassword.c_str());
    delay(500);

    // DNS hijack: answer every query with our SoftAP IP so the captive-portal
    // detector on phones/laptops opens the SPA.
    _setupDns.start(53, "*", WiFi.softAPIP());

    // Serve the embedded SPA (handleNotFound steers all probes here while
    // apSetupMode is set). setup() recreates the server again in STA mode after
    // we return, which is harmless.
    startHttpServer();

    // Display "net" on pump while in AP mode
    bwc->printStatic("net");

    unsigned long lastReconnectAttempt = 0;
    const unsigned long reconnectInterval = 10000;
    bool hasStoredCredentials = (storedSsid.length() > 0);

    while (WiFi.status() != WL_CONNECTED)
    {
        _setupDns.processNextRequest();
        server->handleClient();
        bwc->loop();
        handleResetSequence();
        delay(10);

        if (hasStoredCredentials && (millis() - lastReconnectAttempt >= reconnectInterval))
        {
            lastReconnectAttempt = millis();
            Serial.println(F("WiFi > Setup portal: retrying stored network..."));
            WiFi.begin(storedSsid.c_str(), storedPwd.c_str());

            unsigned long connectStart = millis();
            while (WiFi.status() != WL_CONNECTED && (millis() - connectStart) < 15000)
            {
                _setupDns.processNextRequest();
                server->handleClient();
                bwc->loop();
                handleResetSequence();
                delay(10);
            }

            if (WiFi.status() == WL_CONNECTED)
            {
                Serial.println(F("WiFi > Reconnected to stored network"));
            }
        }
    }

    // Connected: tear down the captive portal and return to normal STA boot.
    _setupDns.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true); // restore normal background reconnection
    apSetupMode = false;
    bwc->clearStatic();
}

/**
 * start NTP sync
 */
void startNTP(bool wait)
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
    // configTime(0, 0, ...) resets the time zone to UTC: local times (Smart
    // Schedule from HA, night tariff, daily repeats across DST) were off by
    // the UTC offset since boot.
    bwc->applyTimezone();
    // The retry from loop() must not wait: with an unreachable NTP server it
    // blocked the main loop ~6 s every minute.
    if (!wait)
        return;
    time_t now = time(nullptr);
    int count = 0;
    while (now < 8 * 3600 * 2)
    {
        delay(500);
        Serial.print(F("."));
        now = time(nullptr);
        if (count++ > 10)
            return; // SNTP keeps trying; main.cpp's bootlogTimer sets the boot time later
    }
    Serial.println();
    setBootTimeFromClock();
}

/**
 * Derive the boot time from the (now valid) clock minus uptime. Called by
 * startNTP() and, if NTP took longer than its wait, by main.cpp's bootlogTimer:
 * before, a slow NTP left reboot_time empty for good.
 */
void setBootTimeFromClock()
{
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
        Serial.println(F("FS: /wifi.json not found, using defaults"));
        return wifi_info;
    }

    StaticJsonDocument<512> doc;

    DeserializationError error = deserializeJson(doc, file);
    file.close();
    if (error)
    {
        return wifi_info;
    }

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

    // Derived, never trusted from the file: the stored flag could latch false
    // while a valid SSID was on record, which sent startWiFi() down the
    // credential-less portal branch with no way back through the UI.
    wifi_info.enableAp = wifi_info.apSsid.length() > 0;

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
        Serial.println(F("FS: Failed to open /wifi.json for write"));
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
#ifdef ESP8266
        net[F("enc")] = (WiFi.encryptionType(i) != ENC_TYPE_NONE);
#else
        net[F("enc")] = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
#endif
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
    // Boolean only, never the key itself: the UI needs to know whether "leave
    // empty to keep the current password" has anything left to keep.
    doc[F("hasApPwd")] = wifi_info.apPwd.length() > 0;
    // Passwords are never sent to the client. The fields are omitted rather
    // than masked with a sentinel string: the UI starts them blank and only
    // submits one when the user types a new value, so there is no magic
    // value for the two sides to agree on - and no UI text in the firmware,
    // which could only ever be in one language.
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
    String message = getRequestBody();
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

    if (doc.containsKey(F("enableWM")))
        wifi_info.enableWmApFallback = doc[F("enableWM")];
    if (doc.containsKey(F("apSsid")))
        wifi_info.apSsid = doc[F("apSsid")].as<String>();
    // Present means "change it" - the UI omits the field unless the user typed
    // something, so an untouched form cannot clobber the stored key. An explicit
    // empty string is still honoured, to set an open network.
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

    // Derived, never taken from the request: the WiFi page has no control for
    // this and used to echo a stored `false` straight back, latching the device
    // into the credential-less portal branch.
    wifi_info.enableAp = wifi_info.apSsid.length() > 0;

    String validationError;
    if (!validateWifiInfo(wifi_info, validationError))
    {
        server->send(400, F("text/plain"), validationError);
        return;
    }

    // Detect what changed: NTP can be applied live, everything else needs a reboot
    bool ntpChanged = (wifi_info.ip4NTP_str != old_info.ip4NTP_str);
    bool networkChanged = (wifi_info.enableWmApFallback != old_info.enableWmApFallback) ||
                          (wifi_info.apSsid != old_info.apSsid) ||
                          (wifi_info.apPwd != old_info.apPwd) ||
                          (wifi_info.enableStaticIp4 != old_info.enableStaticIp4) ||
                          (wifi_info.ip4Address_str != old_info.ip4Address_str) ||
                          (wifi_info.ip4Gateway_str != old_info.ip4Gateway_str) ||
                          (wifi_info.ip4Subnet_str != old_info.ip4Subnet_str) ||
                          (wifi_info.ip4DnsPrimary_str != old_info.ip4DnsPrimary_str) ||
                          (wifi_info.ip4DnsSecondary_str != old_info.ip4DnsSecondary_str);

    if (!ntpChanged && !networkChanged && !apSetupMode)
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
    restartWithReason("net");
}

/*
 * response for /resetwifi/
 * do this before giving away the device (be aware of other credentials e.g. MQTT)
 * a complete flash erase should do the job but remember to upload the filesystem as well.
 */
void handleResetWifi()
{
    // POST only: a plain <img src=".../resetwifi/"> on any page wiped the WiFi
    if (!checkHttpPost(server->method()))
        return;
    server->send(200, F("text/html"), F("Deleting WiFi settings ..."));
    // Serial.println(F("WiFi connection reset (erase) ..."));
    resetWiFi();

    // server->send(200, F("text/html"), F("WiFi settings deleted ..."));
// Serial.println(F("WiFi connection reset (erase) ... done."));
// Serial.println(F("ESP reset ..."));
#if defined(ESP8266)
    ESP.reset();
#else
    ESP.restart();
#endif
}

/**
 * Clear stored WiFi credentials and tear down connectivity
 * Writes empty AP settings, disconnects STA/AP, detaches timers, and persists
 * BWC settings before the caller reboots
 */
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
    bwc->saveSettings();
    bwc->stop();
    delay(1000);
}
