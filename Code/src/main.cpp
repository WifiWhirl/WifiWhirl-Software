#include "main.h"
#include "net/net.h"
#include "web/web.h"
#include "sys/sys.h"
#include "api/api.h"

BWC *bwc;
char *stack_start;
uint32_t heap_water_mark;

Ticker bootlogTimer;
Ticker periodicTimer;
Ticker startComplete;
bool periodicTimerFlag = false;
int periodicTimerInterval = 60;
bool wifiConnected = false;

#if defined(ESP8266)
ESP8266WebServer *server;
#elif defined(ESP32)
WebServer server(80);
#endif

WebSocketsServer *webSocket;
Ticker updateWSTimer;
bool sendWSFlag = false;

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

uint64_t ambExpires = 0;

ESP8266HTTPUpdateServer httpUpdater;

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

    Serial.begin(115200);
    Serial.println(F("\nStart"));
    if (!LittleFS.begin())
    {
        Serial.println(F("CRITICAL: LittleFS mount failed — formatting..."));
        LittleFS.format();
        if (!LittleFS.begin())
        {
            Serial.println(F("CRITICAL: LittleFS mount failed after format — flash may be damaged"));
        }
    }
    {
        HeapSelectIram ephemeral;
        Serial.printf("IRamheap %d\n", ESP.getFreeHeap());
        bwc = new BWC;
    }
    bwc->setup();
    bwc->loadCommandQueue();
    bwc->loop();
    periodicTimer.attach(periodicTimerInterval, []
                         { periodicTimerFlag = true; });
    // delayed mqtt start
    startComplete.attach(20, []
                         { if(useMqtt) enableMqtt = true; startComplete.detach(); });
    // update webpage every 2 seconds. (will also be updated on state changes)
    updateWSTimer.attach(2.0, []
                         { sendWSFlag = true; });
    // when NTP time is valid we save bootlog.txt and this timer stops
    bootlogTimer.attach(5, []
                        { if(time(nullptr)>57600) {bwc->saveRebootInfo(); bootlogTimer.detach();} });
    // loadWifi();
    loadDevice();
    loadWebConfig();
    startWiFi();
    startNTP();
    startOTA();
    startHttpServer();
    startWebSocket();
    startMqtt();

    if (bwc->enableWeather)
    {
        queryAmbientTemperature();
    }

    // Keeping for later integrations
    // if(bwc->hasTempSensor)
    // {
    //     oneWire.begin(bwc->tempSensorPin);
    //     tempSensors.begin();
    // }
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
        if (newData)
        {
            // Serial.println(">>> MAIN LOOP: newData=true from pump");
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
        // listen for websocket events
        // webSocket->loop();
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

        // web socket
        if (newData || sendWSFlag)
        {
            if (!haDiscoveryInProgress)
            {
                // Serial.println(">>> WS: sendWS() called");
                sendWSFlag = false;
                sendWS();
            }
            else
            {
                Serial.println(F(">>> WS: sendWS() BLOCKED by discovery"));
            }
        }

        // run once after connection was established
        if (!wifiConnected)
        {
            // Serial.println(F("WiFi > Connected"));
            // Serial.println(" SSID: \"" + WiFi.SSID() + "\"");
            // Serial.println(" IP: \"" + WiFi.localIP().toString() + "\"");
            startOTA();
            startHttpServer();
            startWebSocket();
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
            webSocket->close();
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

            // Weather query requires 10-18KB heap - pause MQTT to prevent crashes
            if (bwc->enableWeather && ambExpires < (uint64_t)time(nullptr))
            {
                // Pre-flight memory check - require at least 12KB free
                uint32_t freeHeapBefore = ESP.getFreeHeap();
                Serial.print(F("Weather: Free heap before query: "));
                Serial.println(freeHeapBefore);

                if (freeHeapBefore < 12000)
                {
                    Serial.println(F("Weather: SKIPPED - insufficient memory"));
                }
                else
                {
                    // Execute weather query (function handles MQTT pause/resume internally)
                    queryAmbientTemperature();

                    uint32_t freeHeapAfter = ESP.getFreeHeap();
                    Serial.print(F("Weather: Free heap after query: "));
                    Serial.println(freeHeapAfter);
                }
            }
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
        ESP.reset();
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
    updateWSTimer.detach();
    Serial.println(F("stopping FS"));
    LittleFS.end();
    Serial.println(F("stopping server"));
    server->stop();
    Serial.println(F("stopping ws"));
    webSocket->close();
    Serial.println(F("stopping mqtt"));
    if (enableMqtt)
        mqttClient->disconnect();
    Serial.println(F("end stopall"));
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
        updateWSTimer.detach();
        bootlogTimer.detach();
    }
    else
    {
        periodicTimer.attach(periodicTimerInterval, []
                             { periodicTimerFlag = true; });
        startComplete.attach(60, []
                             { if(useMqtt) enableMqtt = true; startComplete.detach(); });
        updateWSTimer.attach(2.0, []
                             { sendWSFlag = true; });
        // bootlogTimer.attach(5, []{ if(DateTime.isTimeValid()) {bwc->saveRebootInfo(); bootlogTimer.detach();} });
    }
    bwc->pause_all(action);
}
