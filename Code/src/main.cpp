#include "main.h"
#include "web_files.h"

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

command_que_item parseCommandFromJson(const JsonVariantConst &src)
{
    command_que_item item;
    item.cmd = src[F("CMD")];
    item.val = src[F("VALUE")];
    item.xtime = src[F("XTIME")] | (int64_t)std::time(nullptr);
    item.interval = src[F("INTERVAL")] | (int64_t)0;
    item.text = src[F("TXT")] | "";
    item.force = src[F("FORCE")] | false;
    return item;
}

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

        resetWiFi();
        delay(3000);
        ESP.reset();
        delay(3000);
    }
    // handleAUX();
}

/**
 * Send status data to web client in JSON format (because it is easy to decode on the other side)
 */
void sendWS()
{
    if (webSocket->connectedClients() == 0)
        return;
    // HeapSelectIram ephemeral;
    // Serial.printf("IRamheap %d\n", ESP.getFreeHeap());
    // send states
    String json;
    json.reserve(320);

    bwc->getJSONStates(json);
    webSocket->broadcastTXT(json);
    // send times
    json.clear();
    bwc->getJSONTimes(json);
    webSocket->broadcastTXT(json);
    // send other info
    json.clear();
    getOtherInfo(json);
    webSocket->broadcastTXT(json);
    // send smart schedule
    json.clear();
    bwc->getJSONSmartSchedule(json);
    webSocket->broadcastTXT(json);
    // json = bwc->getDebugData();
    // webSocket->broadcastTXT(json);
    // time_t now = time(nullptr);
    // struct tm timeinfo;
    // gmtime_r(&now, &timeinfo);
    // Serial.print("Current time: ");
    // Serial.print(asctime(&timeinfo));
}

void getOtherInfo(String &rtn)
{
    StaticJsonDocument<512> doc;
    // Set the values in the document
    doc[F("CONTENT")] = F("OTHER");
    doc[F("MQTT")] = mqttClient ? mqttClient->state() : -1;
    /*TODO: add these:*/
    //   doc[F("PressedButton")] = bwc->getPressedButton();
    doc[F("HASJETS")] = bwc->hasjets;
    doc[F("HASGOD")] = bwc->hasgod;
    doc[F("MODEL")] = bwc->getModel();
    doc[F("WEATHER")] = bwc->getWeather();
    doc[F("RSSI")] = WiFi.RSSI();
    doc[F("IP")] = WiFi.localIP().toString();
    doc[F("SSID")] = WiFi.SSID();
    doc[F("FW")] = FW_VERSION;
    doc[F("loopfq")] = bwc->loop_count;
    bwc->loop_count = 0;

    // Serialize JSON to string
    if (serializeJson(doc, rtn) == 0)
    {
        rtn = F("{\"error\": \"Failed to serialize other\"}");
    }
}

/**
 * HTTP polling endpoint: returns a JSON array of [STATES, TIMES, OTHER]
 * Used as a fallback when WebSocket connections are not available (e.g. iOS 26 Safari Browser).
 * The client polls this endpoint at regular intervals instead of using WebSocket.
 */
void handleGetPollData()
{
    // Build response as JSON array containing all three data objects
    String json;
    json.reserve(1200);
    json = F("[");

    // Append STATES JSON object
    String part;
    part.reserve(320);
    bwc->getJSONStates(part);
    json += part;
    json += F(",");

    // Append TIMES JSON object
    part.clear();
    bwc->getJSONTimes(part);
    json += part;
    json += F(",");

    // Append OTHER JSON object
    part.clear();
    getOtherInfo(part);
    json += part;

    json += F("]");

    // Send the combined JSON array response
    server->send(200, F("application/json"), json);
}

/**
 * HTTP command endpoint: accepts the same JSON command format as WebSocket.
 * Used as a fallback when WebSocket connections are not available.
 * Expects POST body: {"CMD":n,"VALUE":v,"XTIME":t,"INTERVAL":i,"TXT":"","FORCE":bool}
 */
void handleSendCommand()
{
    // Only accept POST requests
    if (server->method() != HTTP_POST)
    {
        server->send(405, F("text/plain"), F("Method Not Allowed"));
        return;
    }

    // Parse the JSON command from the request body
    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, server->arg("plain"));
    if (error)
    {
        server->send(400, F("text/plain"), F("Error deserializing command"));
        return;
    }

    command_que_item item = parseCommandFromJson(doc);
    if (!bwc->add_command(item))
    {
        server->send(409, F("text/plain"), F("Warteschlange voll (max. 20 Befehle)"));
        return;
    }

    server->send(200, F("text/plain"), String(item.cmd) + " " + String(item.val) + " " + String(item.xtime));
}

void startOTA()
{
    ArduinoOTA.setHostname(OTAName);
    ArduinoOTA.setPassword(OTAPassword);

    ArduinoOTA.onStart([]()
                       {
        // Serial.println(F("OTA > Start"));
        stopall(); });
    ArduinoOTA.onEnd([]()
                     {
                         // Serial.println(F("OTA > End"));
                     });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total)
                          {
                              // Serial.printf("OTA > Progress: %u%%\r\n", (progress / (total / 100)));
                          });
    ArduinoOTA.onError([](ota_error_t error)
                       {
                           // Serial.printf("OTA > Error[%u]: ", error);
                           // if (error == OTA_AUTH_ERROR) Serial.println(F("Auth Failed"));
                           // else if (error == OTA_BEGIN_ERROR) Serial.println(F("Begin Failed"));
                           // else if (error == OTA_CONNECT_ERROR) Serial.println(F("Connect Failed"));
                           // else if (error == OTA_RECEIVE_ERROR) Serial.println(F("Receive Failed"));
                           // else if (error == OTA_END_ERROR) Serial.println(F("End Failed"));
                       });
    ArduinoOTA.begin();
    // Serial.println(F("OTA > ready"));
}

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

/*pause: action=true cont: action=false*/
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

void startWebSocket()
{
    HeapSelectIram ephemeral;
    Serial.printf("WS IRamheap %d\n", ESP.getFreeHeap());

    if (webSocket)
    {
        webSocket->close();
        delete webSocket;
    }
    webSocket = new WebSocketsServer(81);
    webSocket->begin();
    webSocket->enableHeartbeat(3000, 3000, 1);
    webSocket->onEvent(webSocketEvent);
    // Serial.println(F("WebSocket > server started"));
}

/**
 * handle web socket events
 */
void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t len)
{
    // When a WebSocket message is received
    switch (type)
    {
    // if the websocket is disconnected
    case WStype_DISCONNECTED:
        // Serial.printf("WebSocket > [%u] Disconnected!\r\n", num);
        break;

    // if a new websocket connection is established
    case WStype_CONNECTED:
    {
        // IPAddress ip = webSocket->remoteIP(num);
        // Serial.printf("WebSocket > [%u] Connected from %d.%d.%d.%d url: %s\r\n", num, ip[0], ip[1], ip[2], ip[3], payload);
        sendWS();
    }
    break;

    // if new text data is received
    case WStype_TEXT:
    {
        // Serial.printf("WebSocket > [%u] get Text: %s\r\n", num, payload);
        StaticJsonDocument<256> doc;
        DeserializationError error = deserializeJson(doc, payload);
        if (error)
        {
            Serial.println(F("WebSocket > JSON command failed"));
            return;
        }

        command_que_item item = parseCommandFromJson(doc);
        if (!bwc->add_command(item))
        {
            webSocket->sendTXT(num, "{\"ERR\":\"Warteschlange voll (max. 20 Befehle)\"}");
        }
    }
    break;

    default:
        break;
    }
}

/**
 * start a HTTP server with a file read and upload handler
 */
void startHttpServer()
{
    if (server)
    {
        server->stop();
        delete server;
    }
    server = new ESP8266WebServer(80);
    server->on(F("/getconfig/"), handleGetConfig);
    server->on(F("/setconfig/"), handleSetConfig);
    server->on(F("/getcommands/"), handleGetCommandQueue);
    server->on(F("/addcommand/"), handleAddCommand);
    server->on(F("/editcommand/"), handleEditCommand);
    server->on(F("/delcommand/"), handleDelCommand);
    server->on(F("/getwebconfig/"), handleGetWebConfig);
    server->on(F("/setwebconfig/"), handleSetWebConfig);
    server->on(F("/getwifi/"), handleGetWifi);
    server->on(F("/setwifi/"), handleSetWifi);
    server->on(F("/scanwifi/"), handleScanWifi);
    server->on(F("/resetwifi/"), handleResetWifi);
    server->on(F("/getmqtt/"), handleGetMqtt);
    server->on(F("/setmqtt/"), handleSetMqtt);
    server->on(F("/getweather/"), handleGetWeather);
    server->on(F("/getstates/"), handleGetStates);
    server->on(F("/gettemps/"), handleGetTemps);
    server->on(F("/restart/"), handleRestart);
    server->on(F("/metrics"), handlePrometheusMetrics); // prometheus metrics
    server->on(F("/info/"), handleESPInfo);
    server->on(F("/support/"), handleSupportPackage);
    server->on(F("/sethardware/"), handleSetHardware);
    server->on(F("/gethardware/"), handleGetHardware);
    server->on(F("/debug-on/"), []()
               {if(!server->authenticate("debug", OTAPassword)) { return server->requestAuthentication(); } bwc->BWC_DEBUG = true; server->send(200, F("text/plain"), "ok"); });
    server->on(F("/debug-off/"), []()
               {if(!server->authenticate("debug", OTAPassword)) { return server->requestAuthentication(); } bwc->BWC_DEBUG = false; server->send(200, F("text/plain"), "ok"); });
    server->on(F("/cmdq_file/"), handle_cmdq_file);
    server->on(F("/hook/"), handleWebhook);
    server->on(F("/getsmartschedule/"), handleGetSmartSchedule);
    server->on(F("/setsmartschedule/"), handleSetSmartSchedule);
    server->on(F("/updatesmartschedule/"), handleUpdateSmartSchedule);
    server->on(F("/cancelsmartschedule/"), handleCancelSmartSchedule);
    server->on(F("/getpolldata/"), handleGetPollData); // Polling fallback for WebSocket data
    server->on(F("/sendcommand/"), handleSendCommand); // Polling fallback for WebSocket commands
    // server->on(F("/getfiles/"), updateFiles);

    server->on(F("/update"), HTTP_GET, []()
               {
      if(!server->authenticate("update", OTAPassword)) { return server->requestAuthentication(); } handleUpdate(); });

    // handle Update from web
    httpUpdater.setup(server, update_path, "update", OTAPassword);

    // if someone requests any other file or page, go to function 'handleNotFound'
    // and check if the file exists
    server->onNotFound(handleNotFound);
    // start the HTTP server
    server->begin();
    // Serial.println(F("HTTP > server started"));
}

void handleGetHardware()
{
    // if (!checkHttpPost(server->method()))
    //     return;
    File file = LittleFS.open("hwcfg.json", "r");
    if (!file)
    {
        Serial.println(F("FS: Failed to open hwcfg.json for read"));
        server->send(404, F("text/plain"), F("not found"));
        return;
    }
    server->send(200, F("text/plain"), file.readString());
    file.close();
}

void handleSetHardware()
{
    if (!checkHttpPost(server->method()))
        return;

    String message = server->arg(0);

    // Check if MODEL changed - requires restart for proper reinitialization
    String oldModel = bwc->getModel();

    // Parse new config to extract the cio enum for comparison
    StaticJsonDocument<384> doc;
    DeserializationError error = deserializeJson(doc, message);
    String newModel = "";
    if (!error && doc.containsKey(F("cio")))
    {
        int cioEnum = doc[F("cio")].as<int>();
        switch (cioEnum)
        {
        case PRE2021:
            newModel = F("PRE2021");
            break;
        case MIAMI2021:
            newModel = F("MIAMI2021");
            break;
        case MALDIVES2021:
            newModel = F("MALDIVES2021");
            break;
        default:
            newModel = "";
            break;
        }
    }

    // Save hardware config
    File file = LittleFS.open("hwcfg.json", "w");
    if (!file)
    {
        Serial.println(F("HW: Failed to save hwcfg.json"));
        server->send(500, F("text/plain"), F("Fehler beim Speichern"));
        return;
    }
    file.print(message);
    file.close();

    // Check if model changed
    bool modelChanged = (newModel.length() > 0 && newModel != oldModel);

    if (modelChanged)
    {
        Serial.println(F("========================================"));
        Serial.print(F("HW: Model changed: "));
        Serial.print(oldModel);
        Serial.print(F(" -> "));
        Serial.println(newModel);
        Serial.println(F("HW: WifiWhirl restarting for hardware initialization..."));
        Serial.println(F("========================================"));

        // Send response with restart notification
        String response = "{\"restart\": true, \"reason\": \"Hardware-Modell geändert. WifiWhirl startet neu um das neue Modell zu initialisieren.\"}";
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

        // Restart ESP - after restart, new model will be loaded
        ESP.restart();
    }
    else
    {
        // No model change, just normal save
        Serial.println(F("HW: Hardware config saved (no restart needed)"));
        server->send(200, F("text/plain"), "ok");
    }
}

void handleNotFound()
{
    // check if the file exists in the flash memory (LittleFS), if so, send it
    if (!handleFileRead(server->uri()))
    {
        server->send(404, F("text/plain"), F("404: File Not Found"));
    }
}

String getContentType(const String &filename)
{
    if (filename.endsWith(".html"))
        return F("text/html");
    else if (filename.endsWith(".css"))
        return F("text/css");
    else if (filename.endsWith(".js"))
        return F("application/javascript");
    else if (filename.endsWith(".ico"))
        return F("image/x-icon");
    else if (filename.endsWith(".png"))
        return F("image/png");
    else if (filename.endsWith(".svg"))
        return F("image/svg+xml");
    else if (filename.endsWith(".eot"))
        return F("application/vnd.ms-fontobject");
    else if (filename.endsWith(".woff"))
        return F("application/font-woff");
    else if (filename.endsWith(".gz"))
        return F("application/x-gzip");
    else if (filename.endsWith(".json"))
        return F("application/json");
    return F("text/plain");
}

/**
 * Serve an embedded file from PROGMEM
 * Handles gzip content-encoding and chunked transfer to avoid watchdog resets
 * @param file Pointer to EmbeddedFile structure
 * @return true if file was served successfully
 */
bool serveEmbeddedFile(const EmbeddedFile *file)
{
    // Read file metadata from PROGMEM
    char contentTypeBuf[48];
    strncpy_P(contentTypeBuf, (PGM_P)pgm_read_ptr(&file->contentType), sizeof(contentTypeBuf) - 1);
    contentTypeBuf[sizeof(contentTypeBuf) - 1] = '\0';

    size_t fileSize = pgm_read_dword(&file->size);
    bool isGzipped = pgm_read_byte(&file->isGzipped);
    const uint8_t *data = (const uint8_t *)pgm_read_ptr(&file->data);

    // Set cache header for static assets
    server->sendHeader(F("Cache-Control"), F("max-age=3600"));

    // Set gzip content-encoding if file is compressed
    if (isGzipped)
    {
        server->sendHeader(F("Content-Encoding"), F("gzip"));
    }

    // Send response with chunked transfer to avoid memory issues
    // For large files, we send in chunks to feed watchdog
    const size_t CHUNK_SIZE = 1024;

    server->setContentLength(fileSize);
    server->send(200, (const char *)contentTypeBuf, (const char *)"");

    size_t bytesSent = 0;
    while (bytesSent < fileSize)
    {
        size_t chunkLen = min(CHUNK_SIZE, fileSize - bytesSent);

        // Copy chunk from PROGMEM to RAM buffer
        uint8_t buffer[CHUNK_SIZE];
        memcpy_P(buffer, data + bytesSent, chunkLen);

        server->sendContent_P((const char *)buffer, chunkLen);
        bytesSent += chunkLen;

        // Feed watchdog during large transfers
        yield();
    }

    Serial.print(F("HTTP > embedded file sent: "));
    Serial.print(fileSize);
    Serial.println(F(" bytes"));

    return true;
}

/**
 * send the right file to the client (if it exists)
 * First checks embedded files in PROGMEM, then falls back to LittleFS
 */
bool handleFileRead(String path)
{
    // Serial.println("HTTP > request: " + path);
    // If a folder is requested, send the index file
    if (path.endsWith("/"))
    {
        path += F("index.html");
    }
    // deny reading credentials
    if (path.equalsIgnoreCase("/mqtt.json") || path.equalsIgnoreCase("/wifi.json"))
    {
        server->send(403, F("text/plain"), F("Permission denied."));
        // Serial.println(F("HTTP > file reading denied (credentials)."));
        return false;
    }

    // First, check if file is embedded in firmware (PROGMEM)
    const EmbeddedFile *embeddedFile = findEmbeddedFile(path);
    if (embeddedFile != nullptr)
    {
        return serveEmbeddedFile(embeddedFile);
    }

    // Fall back to LittleFS for config files and user uploads
    String contentType = getContentType(path); // Get the MIME type
    String pathWithGz = path + ".gz";
    if (LittleFS.exists(pathWithGz) || LittleFS.exists(path))
    {                                         // If the file exists, either as a compressed archive, or normal
        if (LittleFS.exists(pathWithGz))      // If there's a compressed version available
            path += ".gz";                    // Use the compressed version
        File file = LittleFS.open(path, "r"); // Open the file
        size_t fsize = file.size();

        // send cache header for static files
        if (path.endsWith(".css.gz") || path.endsWith(".css") || path.endsWith(".png.gz") || path.endsWith(".ico.gz") || path.endsWith(".js.gz") || path.endsWith(".eot.gz") || path.endsWith(".woff.gz") || path.endsWith(".html.gz"))
            server->sendHeader(F("Cache-Control"), F("max-age=3600"));

        size_t sent = server->streamFile(file, contentType); // Send it to the client

        file.close(); // Close the file again
        Serial.println(F("File size: ") + String(fsize));
        Serial.println(F("HTTP > LittleFS file sent: ") + path + F(" (") + sent + F(" bytes)"));
        return true;
    }
    // Serial.println("HTTP > file not found: " + path);   // If the file doesn't exist, return false
    return false;
}

/**
 * checks the method to be a POST
 */
bool checkHttpPost(HTTPMethod method)
{
    if (method != HTTP_POST)
    {
        server->send(405, F("text/plain"), F("Method not allowed."));
        return false;
    }
    return true;
}

/**
 * checks the method to be a GET
 */
bool checkHttpGet(HTTPMethod method)
{
    if (method != HTTP_GET)
    {
        server->send(405, F("text/plain"), F("Method not allowed."));
        return false;
    }
    return true;
}

/**
 * response for /getconfig/
 * web server prints a json document
 */
void handleGetConfig()
{
    if (!checkHttpPost(server->method()))
        return;

    String json;
    json.reserve(320);
    bwc->getJSONSettings(json);
    server->send(200, F("text/plain"), json);
}

/**
 * response for /setconfig/
 * web server writes a json document
 */
void handleSetConfig()
{
    if (!checkHttpPost(server->method()))
        return;

    String message = server->arg(0);
    bwc->setJSONSettings(message);

    if (bwc->enableWeather)
    {
        queryAmbientTemperature();
    }

    server->send(200, F("text/plain"), "");
}

/**
 * response for /getcommands/
 * web server prints a json document
 */
void handleGetCommandQueue()
{
    if (!checkHttpPost(server->method()))
        return;

    String json;
    bwc->getJSONCommandQueue(json);
    server->send(200, F("application/json"), json);
}

/**
 * response for /addcommand/
 * add a command to the queue
 */
void handleAddCommand()
{
    if (!checkHttpPost(server->method()))
        return;

    StaticJsonDocument<256> doc;
    String message = server->arg(0);
    DeserializationError error = deserializeJson(doc, message);
    if (error)
    {
        server->send(400, F("text/plain"), F("Error deserializing message"));
        return;
    }

    command_que_item item = parseCommandFromJson(doc);
    if (!bwc->add_command(item))
    {
        server->send(409, F("text/plain"), F("Warteschlange voll (max. 20 Befehle)"));
        return;
    }

    server->send(200, F("text/plain"), "");
}

/**
 * response for /editcommand/
 * replace a command in the queue with new command
 */
void handleEditCommand()
{
    if (!checkHttpPost(server->method()))
        return;

    StaticJsonDocument<256> doc;
    String message = server->arg(0);
    DeserializationError error = deserializeJson(doc, message);
    if (error)
    {
        server->send(400, F("text/plain"), F("Error deserializing message"));
        return;
    }

    command_que_item item = parseCommandFromJson(doc);
    uint8_t index = doc[F("IDX")];
    bwc->edit_command(index, item);

    server->send(200, F("text/plain"), "");
}

/**
 * response for /delcommand/
 * replace a command in the queue with new command
 */
void handleDelCommand()
{
    if (!checkHttpPost(server->method()))
        return;

    StaticJsonDocument<256> doc;
    String message = server->arg(0);
    DeserializationError error = deserializeJson(doc, message);
    if (error)
    {
        server->send(400, F("text/plain"), F("Error deserializing message"));
        return;
    }

    uint8_t index = doc[F("IDX")];
    bwc->del_command(index);

    server->send(200, F("text/plain"), "");
}

void handle_cmdq_file()
{
    if (!checkHttpPost(server->method()))
        return;

    // Check for upload action via query parameter
    String action = server->arg(F("action"));

    if (action.equals("upload"))
    {
        // Upload: body contains raw JSON file content
        String data = server->arg("plain");
        if (data.length() == 0)
        {
            data = server->arg(0);
        }
        if (data.length() == 0)
        {
            server->send(400, F("text/plain"), F("Keine Daten empfangen"));
            return;
        }

        // Validate JSON format - cmdq.json structure:
        // {"LEN":n,"CMD":[...],"VALUE":[...],"XTIME":[...],"INTERVAL":[...],"TXT":[...]}
        size_t validateCapacity = data.length() + JSON_OBJECT_SIZE(6) + (JSON_ARRAY_SIZE(MAXCOMMANDS) * 5) + 512;
        DynamicJsonDocument validateDoc(validateCapacity);
        DeserializationError validateError = deserializeJson(validateDoc, data);
        if (validateError)
        {
            server->send(400, F("text/plain"), F("Ungültiges JSON-Format"));
            return;
        }

        // Check if root is an object
        if (!validateDoc.is<JsonObject>())
        {
            server->send(400, F("text/plain"), F("Datei muss ein JSON-Objekt enthalten"));
            return;
        }

        JsonObject root = validateDoc.as<JsonObject>();

        // Check for required fields
        if (!root.containsKey(F("LEN")) || !root.containsKey(F("CMD")) ||
            !root.containsKey(F("VALUE")) || !root.containsKey(F("XTIME")) ||
            !root.containsKey(F("INTERVAL")))
        {
            server->send(400, F("text/plain"), F("Datei fehlt erforderliche Felder (LEN, CMD, VALUE, XTIME, INTERVAL)"));
            return;
        }

        // Verify arrays have consistent length
        size_t len = root[F("LEN")].as<size_t>();
        if (len > MAXCOMMANDS)
        {
            server->send(400, F("text/plain"), F("Warteschlange enthält zu viele Befehle (max. 20)"));
            return;
        }

        if (!root[F("CMD")].is<JsonArray>() || !root[F("VALUE")].is<JsonArray>() ||
            !root[F("XTIME")].is<JsonArray>() || !root[F("INTERVAL")].is<JsonArray>())
        {
            server->send(400, F("text/plain"), F("CMD, VALUE, XTIME, INTERVAL müssen Arrays sein"));
            return;
        }

        JsonArray cmdArr = root[F("CMD")].as<JsonArray>();
        JsonArray valArr = root[F("VALUE")].as<JsonArray>();
        JsonArray timeArr = root[F("XTIME")].as<JsonArray>();
        JsonArray intArr = root[F("INTERVAL")].as<JsonArray>();

        if (cmdArr.size() != len || valArr.size() != len ||
            timeArr.size() != len || intArr.size() != len)
        {
            server->send(400, F("text/plain"), F("Array-Längen stimmen nicht mit LEN überein"));
            return;
        }

        if (root.containsKey(F("TXT")) && !root[F("TXT")].is<JsonArray>())
        {
            server->send(400, F("text/plain"), F("TXT muss ein Array sein"));
            return;
        }

        if (root.containsKey(F("TXT")) && root[F("TXT")].as<JsonArray>().size() != len)
        {
            server->send(400, F("text/plain"), F("TXT-Länge stimmt nicht mit LEN überein"));
            return;
        }

        // Validation passed - save the file
        File file = LittleFS.open(F("/cmdq.json"), "w");
        if (!file)
        {
            Serial.println(F("FS: Failed to open /cmdq.json for write"));
            server->send(500, F("text/plain"), F("Fehler beim Speichern der Datei"));
            return;
        }
        file.print(data);
        file.close();

        // Reload command queue in BWC
        bwc->reloadCommandQueue();

        server->send(200, F("text/plain"), F("OK"));
        return;
    }

    // Download: parse JSON body for action
    String message = server->arg(0);
    StaticJsonDocument<128> doc;
    DeserializationError error = deserializeJson(doc, message);
    if (error)
    {
        server->send(400, F("text/plain"), F("Fehler beim Verarbeiten der Anfrage"));
        return;
    }

    action = doc[F("ACT")].as<String>();

    if (action.equals("download"))
    {
        // Send cmdq.json content to client for download
        if (LittleFS.exists(F("/cmdq.json")))
        {
            File file = LittleFS.open(F("/cmdq.json"), "r");
            if (file)
            {
                String content = file.readString();
                file.close();
                server->send(200, F("application/json"), content);
                return;
            }
        }
        // Return empty array if file doesn't exist
        server->send(200, F("application/json"), F("[]"));
        return;
    }

    server->send(400, F("text/plain"), F("Unbekannte Aktion"));
}

/**
 * response for /getsmartschedule/
 * web server prints smart schedule status as JSON
 */
void handleGetSmartSchedule()
{
    if (!checkHttpPost(server->method()))
        return;

    String json;
    json.reserve(512);
    bwc->getJSONSmartSchedule(json);
    server->send(200, F("application/json"), json);
}

/**
 * response for /setsmartschedule/
 * set a new smart schedule
 */
void handleSetSmartSchedule()
{
    if (!checkHttpPost(server->method()))
        return;

    StaticJsonDocument<256> doc;
    String message = server->arg(0);
    DeserializationError error = deserializeJson(doc, message);
    if (error)
    {
        server->send(400, F("text/plain"), F("Error deserializing message"));
        return;
    }

    // Extract parameters
    uint64_t target_time = doc[F("TARGETTIME")];
    uint8_t target_temp = doc[F("TARGETTEMP")];
    bool keep_heater_on = doc[F("KEEPON")];

    // Validate and set schedule
    if (bwc->setSmartSchedule(target_time, target_temp, keep_heater_on))
    {
        server->send(200, F("text/plain"), F("Schedule set successfully"));
    }
    else
    {
        server->send(400, F("text/plain"), F("Invalid schedule parameters or NTP not synced"));
    }
}

/**
 * response for /updatesmartschedule/
 * update editable smart schedule options without recreating the schedule
 */
void handleUpdateSmartSchedule()
{
    if (!checkHttpPost(server->method()))
        return;

    StaticJsonDocument<128> doc;
    String message = server->arg(0);
    DeserializationError error = deserializeJson(doc, message);
    if (error)
    {
        server->send(400, F("text/plain"), F("Error deserializing message"));
        return;
    }

    if (!doc.containsKey(F("KEEPON")))
    {
        server->send(400, F("text/plain"), F("Missing KEEPON"));
        return;
    }

    bool keep_heater_on = doc[F("KEEPON")];
    if (bwc->updateSmartScheduleKeepHeaterOn(keep_heater_on))
    {
        server->send(200, F("text/plain"), F("Schedule updated successfully"));
    }
    else
    {
        server->send(404, F("text/plain"), F("No active schedule"));
    }
}

/**
 * response for /cancelsmartschedule/
 * cancel active smart schedule
 */
void handleCancelSmartSchedule()
{
    if (!checkHttpPost(server->method()))
        return;

    bwc->cancelSmartSchedule();
    server->send(200, F("text/plain"), F("Schedule cancelled"));
}

/**
 * response for /restart/
 */
void handleRestart()
{
    // Send styled restart page
    String html =
        F("<!DOCTYPE html>"
          "<html>"
          "<head>"
          "<title>WifiWhirl | Neustart</title>"
          "<meta charset='utf-8' />"
          "<meta name='viewport' content='width=device-width, initial-scale=1' />"
          "<style>"
          "body { font-family: sans-serif; text-align: center; padding: 50px; margin: 0; background: #f5f5f5; }"
          "h1 { color: #4051b5; margin-bottom: 20px; }"
          "p { font-size: 18px; margin: 20px; color: #333; }"
          ".info { color: #666; font-size: 16px; }"
          ".btn { display: inline-block; margin-top: 30px; padding: 10px 20px; background: #4051b5; color: white; text-decoration: none; border-radius: 5px; }"
          ".btn:hover { background: #2c3a8f; }"
          "</style>"
          "</head>"
          "<body>"
          "<h1>WifiWhirl startet neu...</h1>"
          "<p>Das Modul wird neu gestartet.</p>"
          "<p class='info'>Bitte warte ca. 30 Sekunden...</p>"
          "<a href='/' class='btn'>Zurück zur Übersicht</a>"
          "<script>"
          "setTimeout(function() { window.location.href = '/'; }, 30000);"
          "</script>"
          "</body>"
          "</html>");

    server->send(200, F("text/html"), html);

    // save all settings
    bwc->saveSettings();

    delay(1000);
    // stop all services
    stopall();
    delay(1000);

    // restart
    Serial.println(F("ESP restart ..."));
    ESP.restart();
    delay(3000);
}

/**
 * response for /update
 */
void handleUpdate()
{
    handleFileRead("/update.html");
}

time_t getBootTime()
{
    time_t seconds = millis() / 1000;
    time_t result = time(nullptr) - seconds;
    return result;
}

void handleESPInfo()
{
#ifdef ESP8266
    char stack;
    uint32_t stacksize = stack_start - &stack;
    size_t const BUFSIZE = 1024;
    char response[BUFSIZE];
    char const *response_template =
        PSTR("Stack size:          %u \n"
             "Free Heap:           %u \n"
             "Min  Heap:           %u \n"
             "Core version:        %s \n"
             "CPU fq:              %u MHz\n"
             "Cycle count:         %u \n"
             "Free cont stack:     %u \n"
             "Sketch size:         %u \n"
             "Free sketch space:   %u \n"
             "Max free block size: %u \n");

    snprintf_P(response, BUFSIZE, response_template,
               stacksize,
               ESP.getFreeHeap(),
               heap_water_mark,
               ESP.getCoreVersion().c_str(),
               ESP.getCpuFreqMHz(),
               ESP.getCycleCount(),
               ESP.getFreeContStack(),
               ESP.getSketchSize(),
               ESP.getFreeSketchSpace(),
               ESP.getMaxFreeBlockSize());
    server->send(200, F("text/plain; charset=utf-8"), response);
#endif
}
