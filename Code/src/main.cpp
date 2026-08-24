#include "main.h"
#include "net/net.h"
#include "web/web.h"
#include "sys/sys.h"
#include "api/api.h"
#include "cloud_task.h"
#include "cloud_psk_storage.h"
#ifdef CLOUD_SELFTEST
#include "cloud_crypto.h"
#endif

BWC *bwc;
char *stack_start;
uint32_t heap_water_mark;

Ticker bootlogTimer;
Ticker periodicTimer;
Ticker startComplete;
bool periodicTimerFlag = false;
int periodicTimerInterval = 60;
bool wifiConnected = false;

WebServerT *server;

WiFiClient *aWifiClient;
PubSubClient *mqttClient;
int mqtt_connect_count;
String prevButtonName = "";
Ticker updateMqttTimer;
bool sendMQTTFlag = false;
bool enableMqtt = false;
bool haDiscoveryInProgress = false;
unsigned long haDiscoveryLastCompleted = 0;
bool haDiscoveryHasRunOnce = false;

#ifdef ESP8266
HTTPUpdateServerT httpUpdater;
#endif

/**
 * Arduino entry point: one-time initialization
 * Mounts LittleFS (formats on failure), constructs the BWC controller, loads
 * config, attaches the periodic timers, and starts WiFi/NTP/OTA/HTTP/WS/MQTT
 */
void setup()
{

    // init record of stack
    char stack;
    stack_start = &stack;

    // Pull the previous crash's backtrace out of RTC memory before anything else
    // touches it (survives reset; readable via /support).
    crashtrace_load();

    // On the ESP-07S field hardware the UART0 TX pin doubles as a pump-comms line
    // and the constant pump bit-bang ISRs starve the UART TX drain, so the Serial
    // ring buffer fills and Serial.write() blocks in cbuf::write until the soft
    // watchdog reboots. Serial is debug-only (nothing reads it), so leave the UART
    // uninitialised on field builds: every Serial.print becomes a fast no-op
    // (HardwareSerial::write returns 0 when the UART isn't enabled).
#ifndef DISABLE_DEBUG_SERIAL
    Serial.begin(115200);
    Serial.println(F("\nStart"));
#endif
    if (!LittleFS.begin())
    {
        Serial.println(F("CRITICAL: LittleFS mount failed - formatting..."));
        LittleFS.format();
        if (!LittleFS.begin())
        {
            Serial.println(F("CRITICAL: LittleFS mount failed after format - flash may be damaged"));
        }
    }
    {
#ifdef ESP8266
        HeapSelectIram ephemeral; // park the next allocations in IRAM heap (ESP8266 only)
#endif
        Serial.printf("IRamheap %d\n", ESP.getFreeHeap());
        bwc = new BWC;
    }
    bwc->setup();
    bwc->loadCommandQueue();
    bwc->loop();
#ifdef CLOUD_SELFTEST
    Serial.printf("[Cloud] crypto self-test: %s\n",
                  CloudCrypto::selfTest() ? "PASS" : "FAIL");
#endif
    periodicTimer.attach(periodicTimerInterval, []
                         { periodicTimerFlag = true; });
    // delayed mqtt start (suppressed in cloud mode - the cloud link replaces MQTT)
    startComplete.attach(20, []
                         { if(useMqtt && !cloudEnabled) enableMqtt = true; startComplete.detach(); });
    // when NTP time is valid we save bootlog.txt and this timer stops
    bootlogTimer.attach(5, []
                        { if(time(nullptr)>57600) {bwc->saveRebootInfo(); bootlogTimer.detach();} });
    // loadWifi();
    // Must precede the loads: on a -DSEED_DEVICE_CONFIG build this overwrites the
    // flash-persisted identity/cloud/PSK files from the compiled config.cpp while
    // the globals still hold those compiled values. No-op otherwise.
    seedDeviceConfig();
    loadDevice();
    loadWebConfig();
    loadCloudConfig();
    startWiFi();
    startNTP();
    startOTA();
    startHttpServer();
    if (!cloudEnabled)
        startMqtt();

    // Migrate a seeded PSK (cloudPskHex) into /cloudpsk on first boot, regardless
    // of cloudEnabled - so the cloud menu/pairing reflect that the unit is
    // provisioned and the user can enable the link (which would otherwise be
    // hidden). Open-source units have no seed → readPSK() stays false → cloud off.
    uint8_t psk[32];
    bool havePsk = CloudPSKStorage::ensurePSK(psk, cloudPskHex.c_str());

    // Cloud mode: bring up the encrypted PoolLink link (MQTT stays off; local
    // control still works via HTTP polling). begin() only stores config - the
    // actual connect happens in CloudTask::tick() once WiFi is up. Cloud identity
    // is the preseeded (factory hostname, PSK) pair; present the factory hostname
    // (never the user-renamable deviceName) so it matches the backend's record.
    if (cloudEnabled)
    {
        String hn = cloudHostname();
        if (hn.length() == 0)
            Serial.println(F("[Cloud] Not provisioned (no factory hostname) - link inactive"));
        else if (havePsk)
        {
            CloudTask::setDeviceInfo(FW_VERSION, PIO_ENV_NAME);
            CloudTask::begin(hn.c_str(), psk,
                             cloudHost.c_str(), (uint16_t)cloudPort);
        }
        else
            Serial.println(F("[Cloud] No PSK available - link inactive"));
    }

    Serial.println(WiFi.localIP().toString());
    bwc->print("    ip adresse ");
    bwc->print(WiFi.localIP().toString());
    bwc->print("   ");
    bwc->print(FW_VERSION);
    Serial.println(F("End of setup()"));
    heap_water_mark = ESP.getFreeHeap();
    Serial.println(ESP.getFreeHeap()); // 26216
}

/**
 * Arduino main loop
 * Tracks the heap low-water mark, services the pump (paused during HA discovery),
 * handles HTTP/OTA/MQTT/WebSocket clients, reconnects WiFi, runs periodic NTP/
 * weather tasks, and triggers a WiFi reset on the button lock-out sequence
 */
void loop()
{
    if (cloudEnabled)
        CloudTask::tick();

    uint32_t freeheap = ESP.getFreeHeap();
    if (freeheap < heap_water_mark)
        heap_water_mark = freeheap;

    // Pause pump communication during HA discovery
    // Pump communication allocates memory (Strings, parsing) that conflicts with discovery
    bool newData = false;

    static bool lastDiscoveryState = false;
    if (haDiscoveryInProgress != lastDiscoveryState)
    {
        // Discovery state changed - log it
        if (haDiscoveryInProgress)
        {
            Serial.println(F(">>> MAIN LOOP: Discovery started - bwc->loop() PAUSED"));
            Serial.print(F(">>> MAIN LOOP: Heap at discovery start: "));
            Serial.println(ESP.getFreeHeap());
        }
        else
        {
            Serial.println(F(">>> MAIN LOOP: Discovery ended - bwc->loop() RESUMED"));
            Serial.print(F(">>> MAIN LOOP: Heap after discovery: "));
            Serial.println(ESP.getFreeHeap());
        }
        lastDiscoveryState = haDiscoveryInProgress;
    }

    if (!haDiscoveryInProgress)
    {
        // Normal operation: process pump data
        newData = bwc->newData();
        if (newData && cloudEnabled)
        {
            // Push the fresh state to the cloud (deduped inside CloudTask).
            CloudTask::notifyStateChanged();
        }
        bwc->loop();
    }
    else
    {
        // During discovery: PAUSE pump communication
        // newData stays false, bwc->loop() is NOT called
    }

    // run only when a wifi connection is established
    if (WiFi.status() == WL_CONNECTED)
    {
        // listen for webserver events
        server->handleClient();
        // listen for OTA events
        ArduinoOTA.handle();

        // MQTT
        // Check if client exists (may be deleted after HA discovery for clean reset)
        if (enableMqtt && mqttClient && mqttClient->loop())
        {
            // Block MQTT publishing during HA discovery to prevent memory corruption
            if (!haDiscoveryInProgress)
            {
                String msg;
                msg.reserve(32);
                bwc->getButtonName(msg);
                // publish pretty button name if display button is pressed (or NOBTN if released)
                if (!msg.equals(prevButtonName))
                {
                    Serial.println(F(">>> MQTT: Publishing button name change"));
                    mqttClient->publish(getMqttTopicButton().c_str(), msg.c_str(), true);
                    prevButtonName = msg;
                }

                if (newData || sendMQTTFlag)
                {
                    Serial.print(F(">>> MQTT: sendMQTT() called (newData="));
                    Serial.print(newData);
                    Serial.print(F(", flag="));
                    Serial.print(sendMQTTFlag);
                    Serial.println(F(")"));
                    sendMQTT();
                    sendMQTTFlag = false;
                }
            }
            else
            {
                // During discovery - log if we're blocking
                if (newData || sendMQTTFlag)
                {
                    Serial.println(F(">>> MQTT: sendMQTT() BLOCKED by discovery"));
                }
            }
        }

        // Live data reaches the SPA via HTTP polling (/getpolldata); no push here.

        // run once after connection was established
        if (!wifiConnected)
        {
            // Serial.println(F("WiFi > Connected"));
            // Serial.println(" SSID: \"" + WiFi.SSID() + "\"");
            // Serial.println(" IP: \"" + WiFi.localIP().toString() + "\"");
            startOTA();
            startHttpServer();
        }

        // reset marker
        wifiConnected = true;
    }

    // run only when the wifi connection got lost
    if (WiFi.status() != WL_CONNECTED)
    {
        // run once after connection was lost
        if (wifiConnected)
        {
            // Serial.println(F("WiFi > Lost connection. Trying to reconnect ..."));
            server->stop();
        }
        // set marker
        wifiConnected = false;
    }

    // run every X seconds
    if (periodicTimerFlag)
    {
        periodicTimerFlag = false;
        if (WiFi.status() != WL_CONNECTED)
        {
            bwc->print(F("  no net connection  "));
            WiFi.reconnect();
        }
        if (WiFi.status() == WL_CONNECTED)
        {
            // could be interesting to display the IP
            // bwc->print(WiFi.localIP().toString());

            if (time(nullptr) < 57600)
            {
                // Serial.println(F("NTP > Start synchronisation"));
                startNTP();
            }

            // Check if MQTT client exists (may be deleted after HA discovery for clean reset)
            if (enableMqtt && (!mqttClient || !mqttClient->loop()))
            {
                // Serial.println(F("MQTT > Not connected"));
                mqttConnect();
            }

            // Look for newer firmware
            updateCheckTick();
        }
        // Keep for later integrations
        // // Leverage the pre-existing periodicTimerFlag to also set temperature, if enabled
        // setTemperatureFromSensor();
    }

    // Only do this if locked out! (by pressing POWER - LOCK - TIMER - POWER)
    if (bwc->getBtnSeqMatch())
    {

        resetDeviceConfig();
        resetWiFi();
        delay(3000);
#ifdef ESP8266
        ESP.reset();
#else
        ESP.restart();
#endif
        delay(3000);
    }
    // handleAUX();
}

/**
 * Gracefully stop all services before reboot/OTA
 * Stops BWC, detaches timers, unmounts LittleFS, and closes the HTTP/WS/MQTT servers
 */
void stopall()
{
    bwc->stop();
    Serial.println(F("detaching"));
    updateMqttTimer.detach();
    periodicTimer.detach();
    Serial.println(F("stopping FS"));
    LittleFS.end();
    Serial.println(F("stopping server"));
    server->stop();
    Serial.println(F("stopping mqtt"));
    if (enableMqtt)
        mqttClient->disconnect();
    Serial.println(F("end stopall"));
}

/**
 * Send a restart-notification JSON reply, persist settings, stop services and reboot.
 * Shared by the wifi/mqtt/hardware config handlers that change settings requiring a restart.
 * @param reasonCode stable key (e.g. "net", "mqtt", "hw") the SPA maps to a localized
 *                   message; pass a code, not UI text, so the frontend handles i18n.
 */
void restartWithReason(const char *reasonCode)
{
    String response = String(F("{\"restart\":true,\"reason\":\"")) + reasonCode + F("\"}");
    server->send(200, F("application/json"), response);
    delay(500); // let the HTTP response flush before we tear down the server
    bwc->saveSettings();
    stopall();
    delay(1000);
    ESP.restart();
}

/**
 * Pause or resume background timers and BWC processing
 * @param action true detaches the timers (pause); false re-attaches them (continue)
 */
void pause_all(bool action)
{
    if (action)
    {
        periodicTimer.detach();
        startComplete.detach();
        bootlogTimer.detach();
    }
    else
    {
        periodicTimer.attach(periodicTimerInterval, []
                             { periodicTimerFlag = true; });
        startComplete.attach(60, []
                             { if(useMqtt) enableMqtt = true; startComplete.detach(); });
        // bootlogTimer.attach(5, []{ if(DateTime.isTimeValid()) {bwc->saveRebootInfo(); bootlogTimer.detach();} });
    }
    bwc->pause_all(action);
}
