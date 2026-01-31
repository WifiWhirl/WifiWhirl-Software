#include "main.h"
#include "web_files.h"

BWC *bwc;

// initial stack
char *stack_start;
uint32_t heap_water_mark;

ESP8266HTTPUpdateServer httpUpdater;

// Setup a oneWire instance to communicate with any OneWire devices
// Setting arbitrarily to 231 since this isn't an actual pin
// Later during "setup" the correct pin will be set, if enabled
// Keeping for later integrations
// OneWire oneWire(231);
// // Pass our oneWire reference to Dallas Temperature sensor
// DallasTemperature tempSensors(&oneWire);

void setup()
{

    // init record of stack
    char stack;
    stack_start = &stack;

    Serial.begin(115200);
    Serial.println(F("\nStart"));
    LittleFS.begin();
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
    extern bool haDiscoveryInProgress;
    bool newData = false; // Default to false
    
    static bool lastDiscoveryState = false;
    if (haDiscoveryInProgress != lastDiscoveryState)
    {
        // Discovery state changed - log it
        if (haDiscoveryInProgress)
        {
            Serial.println(">>> MAIN LOOP: Discovery started - bwc->loop() PAUSED");
            Serial.print(">>> MAIN LOOP: Heap at discovery start: ");
            Serial.println(ESP.getFreeHeap());
        }
        else
        {
            Serial.println(">>> MAIN LOOP: Discovery ended - bwc->loop() RESUMED");
            Serial.print(">>> MAIN LOOP: Heap after discovery: ");
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
                    Serial.println(">>> MQTT: Publishing button name change");
                    mqttClient->publish((String(mqttBaseTopic) + "/button").c_str(), String(msg).c_str(), true);
                    prevButtonName = msg;
                }

                if (newData || sendMQTTFlag)
                {
                    Serial.print(">>> MQTT: sendMQTT() called (newData=");
                    Serial.print(newData);
                    Serial.print(", flag=");
                    Serial.print(sendMQTTFlag);
                    Serial.println(")");
                    sendMQTT();
                    sendMQTTFlag = false;
                }
            }
            else
            {
                // During discovery - log if we're blocking
                if (newData || sendMQTTFlag)
                {
                    Serial.println(">>> MQTT: sendMQTT() BLOCKED by discovery");
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
                Serial.println(">>> WS: sendWS() BLOCKED by discovery");
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
            // Serial.println(F("WiFi > Trying to reconnect ..."));
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
    // DynamicJsonDocument doc(512);
    StaticJsonDocument<512> doc;
    // Set the values in the document
    doc[F("CONTENT")] = F("OTHER");
    doc[F("MQTT")] = mqttClient->state();
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
 * Send STATES and TIMES to MQTT
 * It would be more elegant to send both states and times on the "message" topic
 * and use the "CONTENT" field to distinguish between them
 * but it might break peoples home automation setups, so to keep it backwards
 * compatible I choose to start a new topic "/times"
 * @author 877dev
 */
void sendMQTT()
{
    // HeapSelectIram ephemeral;
    // Serial.printf("IRamheap %d\n", ESP.getFreeHeap());
    String json;
    json.reserve(320);

    // send states
    bwc->getJSONStates(json);
    if (mqttClient->publish((String(mqttBaseTopic) + "/message").c_str(), String(json).c_str(), true))
    {
        // Serial.println(F("MQTT > message published"));
    }
    else
    {
        // Serial.println(F("MQTT > message not published"));
    }
    // delay(2);

    // send times
    json.clear();
    bwc->getJSONTimes(json);
    if (mqttClient->publish((String(mqttBaseTopic) + "/times").c_str(), String(json).c_str(), true))
    {
        // Serial.println(F("MQTT > times published"));
    }
    else
    {
        // Serial.println(F("MQTT > times not published"));
    }
    // delay(2);

    // send other info
    json.clear();
    getOtherInfo(json);
    if (mqttClient->publish((String(mqttBaseTopic) + "/other").c_str(), String(json).c_str(), true))
    {
        // Serial.println(F("MQTT > other published"));
    }
    else
    {
        // Serial.println(F("MQTT > other not published"));
    }
}

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
            // Serial.print(".");
            tryCount++;

            if (tryCount >= maxTries)
            {
                // Serial.println("");
                // Serial.println(F("WiFi > NOT connected!"));
                if (wifi_info.enableWmApFallback)
                {
                    // disable specific WiFi config
                    wifi_info.enableAp = false;
                    wifi_info.enableStaticIp4 = false;
                    // fallback to WiFi config portal
                    startWiFiConfigPortal();
                    // Safe wifi settings after initial config and restart module
                    wifi_info.apSsid = WiFi.SSID();
                    wifi_info.apPwd = WiFi.psk();
                    saveWifi(wifi_info);
                    ESP.restart();
                }
                break;
            }
            // Serial.println("");
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
void startWiFiConfigPortal()
{
    Serial.println(F("WiFi > Using WiFiManager Config Portal"));
    ESP_WiFiManager wm;

    WiFiManagerParameter custom_text("<p><strong>Willkommen zur Einrichtung deines WifiWhirl WLAN-Moduls!</strong></p><p>Verbinde dich hier mit deinem WLAN, um mit der Einrichtung zu beginnen.</p>");

    wm.setClass("invert");                  // WM Dark Mode
    wm.setShowInfoErase(false);             // WM Disable Erase Button
    wm.addParameter(&custom_text);          // WM Show WifiWhirl Text
    wm.setConfigPortalBlocking(false);      // WM non-blocking mode so we can update display
    
    // Display "net" on pump while in AP mode
    bwc->printStatic("net");
    
    // Start AP - non-blocking
    wm.autoConnect(wmApName, wmApPassword);
    
    // Keep looping until WiFi connects
    while (WiFi.status() != WL_CONNECTED)
    {
        wm.process(); // Process WiFiManager
        bwc->loop();  // Update pump display
        delay(100);
    }
    
    // Reset display when WiFi connects
    bwc->clearStatic();    
    // Serial.println("");
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
        configTime(0, 0, wifi_info.ip4NTP_str.c_str());
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
    Serial.println("detaching");
    updateMqttTimer.detach();
    periodicTimer.detach();
    updateWSTimer.detach();
    // bwc->saveSettings();
    Serial.println("stopping FS");
    LittleFS.end();
    Serial.println("stopping server");
    server->stop();
    Serial.println("stopping ws");
    webSocket->close();
    Serial.println("stopping mqtt");
    if (enableMqtt)
        mqttClient->disconnect();
    Serial.println("end stopall");
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

    webSocket = new WebSocketsServer(81);
    // In case we are already running
    webSocket->close();
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
        // DynamicJsonDocument doc(256);
        StaticJsonDocument<256> doc;
        DeserializationError error = deserializeJson(doc, payload);
        if (error)
        {
            Serial.println(F("WebSocket > JSON command failed"));
            return;
        }

        // Copy values from the JsonDocument to the Config
        Commands command = doc[F("CMD")];
        int64_t value = doc[F("VALUE")];
        int64_t xtime = doc[F("XTIME")];
        int64_t interval = doc[F("INTERVAL")];
        String txt = doc[F("TXT")] | "";
        command_que_item item;
        item.cmd = command;
        item.val = value;
        item.xtime = xtime;
        item.interval = interval;
        item.text = txt;
        bwc->add_command(item);
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
    server = new ESP8266WebServer(80);
    // In case we are already running
    server->stop();
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
    server->on(F("/resetwifi/"), handleResetWifi);
    server->on(F("/getmqtt/"), handleGetMqtt);
    server->on(F("/setmqtt/"), handleSetMqtt);
    server->on(F("/getweather/"), handleGetWeather);
    server->on(F("/getstates/"), handleGetStates);
    server->on(F("/gettemps/"), handleGetTemps);
    server->on(F("/restart/"), handleRestart);
    server->on(F("/metrics"), handlePrometheusMetrics); // prometheus metrics
    server->on(F("/info/"), handleESPInfo);
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
    server->on(F("/cancelsmartschedule/"), handleCancelSmartSchedule);
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

String queryAmbientTemperature()
{
    // Track heap usage for debugging
    Serial.print(F("Weather: Starting query, free heap: "));
    Serial.println(ESP.getFreeHeap());
    
    // Use standard WiFiClient for HTTP connection
    WiFiClient client;
    HTTPClient http;
    http.setUserAgent(DEVICE_NAME);
    
    // Extract PLZ from settings
    String _plz;
    {
        // Scope-limited to ensure immediate cleanup
        DynamicJsonDocument doc(1024);
        String json;
        json.reserve(320);
        bwc->getJSONSettings(json);

        // Deserialize the JSON document
        DeserializationError error = deserializeJson(doc, json);
        json.clear();
        json = String();
        
        if (error)
        {
            Serial.println(F("Weather: Failed to read config"));
            return "Error reading config";
        }

        _plz = doc[F("PLZ")].as<String>();
        // doc goes out of scope here and is destroyed
    }

    String const weatherURL = String(cloudApi) + "/v1/weather/plz/" + _plz + "/";
    Serial.print(F("Weather: Connecting to API: "));
    Serial.println(weatherURL);
    
    if (http.begin(client, weatherURL))
    {
        http.addHeader("X-WW-Firmware", FW_VERSION);
        http.addHeader("X-WW-Apikey", String(cloudApiKey));
        http.addHeader("Accept", "application/json");
        
        int httpResponseCode = http.GET();
        
        Serial.print(F("Weather: Response code: "));
        Serial.println(httpResponseCode);
        
        if (httpResponseCode == 200)
        {
            String payload = http.getString();
            http.end();
            client.stop();
            
            Serial.print(F("Weather: Parsing response, free heap: "));
            Serial.println(ESP.getFreeHeap());
            
            StaticJsonDocument<512> resp;
            DeserializationError error = deserializeJson(resp, payload);
            payload.clear();
            payload = String();
            
            if (error)
            {
                Serial.println(F("Weather: Parse error"));
                return "Error while getting weather data";
            }

            ambExpires = resp[F("expires")];
            int64_t _temperature = resp[F("temperature")];
            const char *_name = resp[F("name")];
            
            // Copy name before clearing document
            String result = String(_name);
            
            bwc->setAmbientTemperature(_temperature, true);
            
            Serial.print(F("Weather: Success, free heap: "));
            Serial.println(ESP.getFreeHeap());
            
            return result;
        }
        else if (httpResponseCode == 404)
        {
            Serial.print(F("Weather: Error code: "));
            Serial.println(httpResponseCode);
            http.end();
            client.stop();
            
            return "Error: ZIP code not found";
        }
        else
        {
            Serial.print(F("Weather: Error code: "));
            Serial.println(httpResponseCode);
            http.end();
            client.stop();
            
            return "Error while getting weather data";
        }
    }
    else
    {
        Serial.println(F("Weather: Could not connect to server"));
        http.end();
        client.stop();
        
        return "Error while getting weather data";
    }
}


void handleGetWeather()
{
    String ambient = queryAmbientTemperature();
    if (ambient.indexOf("Error") >= 0)
    {
        server->send(500, F("text/plain"), "Es ist ein Fehler aufgetreten. Bitte prüfe die PLZ.");
    }
    else
    {
        server->send(200, F("text/plain"), ambient);
    }
}

void handleGetHardware()
{
    // if (!checkHttpPost(server->method()))
    //     return;
    File file = LittleFS.open("hwcfg.json", "r");
    if (!file)
    {
        // Serial.println(F("Failed to open hwcfg.json"));
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
    
    // Parse new config to get new model
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, message);
    String newModel = "";
    if (!error && doc.containsKey(F("MODEL"))) {
        newModel = String(doc[F("MODEL")].as<int>());
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
    
    if (modelChanged) {
        Serial.println("========================================");
        Serial.print("HW: Model changed: ");
        Serial.print(oldModel);
        Serial.print(" -> ");
        Serial.println(newModel);
        Serial.println("HW: WifiWhirl restarting for hardware initialization...");
        Serial.println("========================================");
        
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
        if (mqttClient) {
            mqttClient->disconnect();
        }
        
        delay(1000);
        
        // Restart ESP - after restart, new model will be loaded
        ESP.restart();
    } else {
        // No model change, just normal save
        Serial.println("HW: Hardware config saved (no restart needed)");
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
bool serveEmbeddedFile(const EmbeddedFile* file)
{
    // Read file metadata from PROGMEM
    char contentTypeBuf[48];
    strncpy_P(contentTypeBuf, (PGM_P)pgm_read_ptr(&file->contentType), sizeof(contentTypeBuf) - 1);
    contentTypeBuf[sizeof(contentTypeBuf) - 1] = '\0';
    
    size_t fileSize = pgm_read_dword(&file->size);
    bool isGzipped = pgm_read_byte(&file->isGzipped);
    const uint8_t* data = (const uint8_t*)pgm_read_ptr(&file->data);
    
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
    server->send(200, (const char*)contentTypeBuf, (const char*)"");
    
    size_t bytesSent = 0;
    while (bytesSent < fileSize)
    {
        size_t chunkLen = min(CHUNK_SIZE, fileSize - bytesSent);
        
        // Copy chunk from PROGMEM to RAM buffer
        uint8_t buffer[CHUNK_SIZE];
        memcpy_P(buffer, data + bytesSent, chunkLen);
        
        server->sendContent_P((const char*)buffer, chunkLen);
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
    const EmbeddedFile* embeddedFile = findEmbeddedFile(path);
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
        server->send(405, "text/plain", "Method not allowed.");
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
        server->send(405, "text/plain", "Method not allowed.");
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

    String json = bwc->getJSONCommandQueue();
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

    // DynamicJsonDocument doc(256);
    StaticJsonDocument<256> doc;
    String message = server->arg(0);
    DeserializationError error = deserializeJson(doc, message);
    if (error)
    {
        server->send(400, F("text/plain"), F("Error deserializing message"));
        return;
    }

    Commands command = doc[F("CMD")];
    int64_t value = doc[F("VALUE")];
    int64_t xtime = doc[F("XTIME")];
    int64_t interval = doc[F("INTERVAL")];
    String txt = doc[F("TXT")] | "";
    command_que_item item;
    item.cmd = command;
    item.val = value;
    item.xtime = xtime;
    item.interval = interval;
    item.text = txt;
    bwc->add_command(item);

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

    // DynamicJsonDocument doc(256);
    StaticJsonDocument<256> doc;
    String message = server->arg(0);
    DeserializationError error = deserializeJson(doc, message);
    if (error)
    {
        server->send(400, F("text/plain"), F("Error deserializing message"));
        return;
    }

    Commands command = doc[F("CMD")];
    int64_t value = doc[F("VALUE")];
    int64_t xtime = doc[F("XTIME")];
    int64_t interval = doc[F("INTERVAL")];
    String txt = doc[F("TXT")] | "";
    uint8_t index = doc[F("IDX")];
    command_que_item item;
    item.cmd = command;
    item.val = value;
    item.xtime = xtime;
    item.interval = interval;
    item.text = txt;
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

    // DynamicJsonDocument doc(256);
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

    // DynamicJsonDocument doc(256);
    StaticJsonDocument<256> doc;
    String message = server->arg(0);
    DeserializationError error = deserializeJson(doc, message);
    if (error)
    {
        server->send(400, F("text/plain"), F("Error deserializing message"));
        return;
    }

    String action = doc[F("ACT")].as<String>();
    String filename = "/";
    filename += doc[F("NAME")].as<String>();

    if (action.equals("load"))
    {
        copyFile("/cmdq.json", "/cmdq.backup");
        copyFile(filename, "/cmdq.json");
        bwc->reloadCommandQueue();
    }
    if (action.equals("save"))
    {
        copyFile("/cmdq.json", filename);
    }

    server->send(200, F("text/plain"), "");
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

void copyFile(String source, String dest)
{
    char ibuffer[128]; // declare a buffer

    File f_source = LittleFS.open(source, "r"); // open source file to read
    if (!f_source)
    {
        return;
    }

    File f_dest = LittleFS.open(dest, "w"); // open destination file to write
    if (!f_dest)
    {
        return;
    }

    while (f_source.available() > 0)
    {
        byte i = f_source.readBytes(ibuffer, 128); // i = number of bytes placed in buffer from file f_source
        f_dest.write(ibuffer, i);                  // write i bytes from buffer to file f_dest
    }

    f_dest.close();   // done, close the destination file
    f_source.close(); // done, close the source file
}

/**
 * load "Web Config" json configuration from "webconfig.json"
 */
void loadWebConfig()
{
    // DynamicJsonDocument doc(1024);
    StaticJsonDocument<256> doc;

    File file = LittleFS.open("/webconfig.json", "r");
    if (file)
    {
        DeserializationError error = deserializeJson(doc, file);
        if (error)
        {
            // Serial.println(F("Failed to deserialize webconfig.json"));
            file.close();
            return;
        }
    }
    else
    {
        // Serial.println(F("Failed to read webconfig.json. Using defaults."));
    }

    showSectionTemperature = (doc.containsKey("SST") ? doc[F("SST")] : false);
    showSectionDisplay = (doc.containsKey("SSD") ? doc[F("SSD")] : true);
    showSectionControl = (doc.containsKey("SSC") ? doc[F("SSC")] : true);
    showSectionButtons = (doc.containsKey("SSB") ? doc[F("SSB")] : true);
    showSectionTimer = (doc.containsKey("SSTIM") ? doc[F("SSTIM")] : true);
    showSectionTotals = (doc.containsKey("SSTOT") ? doc[F("SSTOT")] : true);
    showSectionEnergy = (doc.containsKey("SSEN") ? doc[F("SSEN")] : true);
    showSectionWaterQuality = (doc.containsKey("SSWQ") ? doc[F("SSWQ")] : true);
    showWQCyanuric = (doc.containsKey("SWQCYA") ? doc[F("SWQCYA")] : false);
    showWQAlkalinity = (doc.containsKey("SWQALK") ? doc[F("SWQALK")] : false);
    useControlSelector = (doc.containsKey("UCS") ? doc[F("UCS")] : false);
}

/**
 * save "Web Config" json configuration to "webconfig.json"
 */
void saveWebConfig()
{
    File file = LittleFS.open("/webconfig.json", "w");
    if (!file)
    {
        // Serial.println(F("Failed to save webconfig.json"));
        return;
    }

    // DynamicJsonDocument doc(256);
    StaticJsonDocument<256> doc;

    doc[F("SST")] = showSectionTemperature;
    doc[F("SSD")] = showSectionDisplay;
    doc[F("SSC")] = showSectionControl;
    doc[F("SSB")] = showSectionButtons;
    doc[F("SSTIM")] = showSectionTimer;
    doc[F("SSTOT")] = showSectionTotals;
    doc[F("SSEN")] = showSectionEnergy;
    doc[F("SSWQ")] = showSectionWaterQuality;
    doc[F("SWQCYA")] = showWQCyanuric;
    doc[F("SWQALK")] = showWQAlkalinity;
    doc[F("UCS")] = useControlSelector;

    if (serializeJson(doc, file) == 0)
    {
        // Serial.println(F("{\"error\": \"Failed to serialize file\"}"));
    }
    file.close();
}

/**
 * response for /getwebconfig/
 * web server prints a json document
 */
void handleGetWebConfig()
{
    if (!checkHttpPost(server->method()))
        return;

    // DynamicJsonDocument doc(256);
    StaticJsonDocument<256> doc;

    doc[F("SST")] = showSectionTemperature;
    doc[F("SSD")] = showSectionDisplay;
    doc[F("SSC")] = showSectionControl;
    doc[F("SSB")] = showSectionButtons;
    doc[F("SSTIM")] = showSectionTimer;
    doc[F("SSTOT")] = showSectionTotals;
    doc[F("SSEN")] = showSectionEnergy;
    doc[F("SSWQ")] = showSectionWaterQuality;
    doc[F("SWQCYA")] = showWQCyanuric;
    doc[F("SWQALK")] = showWQAlkalinity;
    doc[F("UCS")] = useControlSelector;

    String json;
    if (serializeJson(doc, json) == 0)
    {
        json = F("{\"error\": \"Failed to serialize webcfg\"}");
    }
    server->send(200, "application/json", json);
}

/**
 * response for /setwebconfig/
 * web server writes a json document
 */
void handleSetWebConfig()
{
    if (!checkHttpPost(server->method()))
        return;

    // DynamicJsonDocument doc(256);
    StaticJsonDocument<256> doc;
    String message = server->arg(0);
    DeserializationError error = deserializeJson(doc, message);
    if (error)
    {
        // Serial.println(F("Failed to read config file"));
        server->send(400, F("text/plain"), F("Error deserializing message"));
        return;
    }

    showSectionTemperature = doc[F("SST")];
    showSectionDisplay = doc[F("SSD")];
    showSectionControl = doc[F("SSC")];
    showSectionButtons = doc[F("SSB")];
    showSectionTimer = doc[F("SSTIM")];
    showSectionTotals = doc[F("SSTOT")];
    showSectionEnergy = doc[F("SSEN")];
    showSectionWaterQuality = doc.containsKey("SSWQ") ? doc[F("SSWQ")] : true;
    showWQCyanuric = doc.containsKey("SWQCYA") ? doc[F("SWQCYA")] : false;
    showWQAlkalinity = doc.containsKey("SWQALK") ? doc[F("SWQALK")] : false;
    useControlSelector = doc[F("UCS")];

    saveWebConfig();

    server->send(200, F("text/plain"), "");
}


/**
 * load WiFi json configuration from "wifi.json"
 */
sWifi_info loadWifi()
{
    sWifi_info wifi_info;
    File file = LittleFS.open("/wifi.json", "r");
    if (!file)
    {
        // Serial.println(F("Failed to read wifi.json. Using defaults."));
        return wifi_info;
    }

    DynamicJsonDocument doc(1024);

    DeserializationError error = deserializeJson(doc, file);
    if (error)
    {
        // Serial.println(F("Failed to deserialize wifi.json"));
        file.close();
        return wifi_info;
    }

    wifi_info.enableAp = doc[F("enableAp")];
    if (doc.containsKey("enableWM"))
        wifi_info.enableWmApFallback = doc[F("enableWM")];
    wifi_info.apSsid = doc[F("apSsid")].as<String>();
    wifi_info.apPwd = doc[F("apPwd")].as<String>();

    wifi_info.enableStaticIp4 = doc[F("enableStaticIp4")];
    String s(30);
    wifi_info.ip4Address_str = doc[F("ip4Address")].as<String>();
    wifi_info.ip4Gateway_str = doc[F("ip4Gateway")].as<String>();
    wifi_info.ip4Subnet_str = doc[F("ip4Subnet")].as<String>();
    wifi_info.ip4DnsPrimary_str = doc[F("ip4DnsPrimary")].as<String>();
    wifi_info.ip4DnsSecondary_str = doc[F("ip4DnsSecondary")].as<String>();
    wifi_info.ip4NTP_str = doc[F("ip4NTP")].as<String>();

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
        // Serial.println(F("Failed to save wifi.json"));
        return;
    }

    DynamicJsonDocument doc(1024);

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
        // Serial.println(F("{\"error\": \"Failed to serialize file\"}"));
    }
    file.close();
}

/**
 * response for /getwifi/
 * web server prints a json document
 */
void handleGetWifi()
{
    if (!checkHttpPost(server->method()))
        return;

    DynamicJsonDocument doc(1024);

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

    DynamicJsonDocument doc(1024);
    String message = server->arg(0);
    DeserializationError error = deserializeJson(doc, message);
    if (error)
    {
        // Serial.println(F("Failed to read config file"));
        server->send(400, F("text/plain"), F("Error deserializing message"));
        return;
    }

    sWifi_info wifi_info;

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

    saveWifi(wifi_info);

    server->send(200, F("text/plain"), "");
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
    wifi_info.enableAp = true;
    wifi_info.enableWmApFallback = true;
    wifi_info.apSsid = F("wifiwhirl");
    wifi_info.apPwd = F("wifiwhirl");
    saveWifi(wifi_info);
    delay(3000);
    periodicTimer.detach();
    updateMqttTimer.detach();
    updateWSTimer.detach();
    bwc->stop();
    bwc->saveSettings();
    delay(1000);
    // #if defined(ESP8266)
    //     ESP.eraseConfig();
    // #endif
    //     delay(1000);
    //     wm.resetSettings();
    //     startWiFiConfigPortal();
    //     // WiFi.disconnect();
    //     delay(1000);
}

/**
 * load MQTT json configuration from "mqtt.json"
 */
void loadMqtt()
{
    File file = LittleFS.open("mqtt.json", "r");
    if (!file)
    {
        return;
    }

    DynamicJsonDocument doc(1024);

    DeserializationError error = deserializeJson(doc, file);
    file.close(); // Close file once read
    if (error)
    {
        return;
    }

    bool needsMigration = false;
    useMqtt = doc[F("enableMqtt")];
    
    if (doc.containsKey(F("mqttServer")))
    {
        mqttServer = doc[F("mqttServer")].as<String>();
    }
    // Backwards compatibility for old IPAddress format
    else if (doc.containsKey(F("mqttIpAddress"))) 
    {
        IPAddress ip;
        ip[0] = doc[F("mqttIpAddress")][0];
        ip[1] = doc[F("mqttIpAddress")][1];
        ip[2] = doc[F("mqttIpAddress")][2];
        ip[3] = doc[F("mqttIpAddress")][3];
        mqttServer = ip.toString();
        needsMigration = true; // Mark for migration
    }
    
    mqttPort = doc[F("mqttPort")];
    mqttUsername = doc[F("mqttUsername")].as<String>();
    mqttPassword = doc[F("mqttPassword")].as<String>();
    mqttClientId = doc[F("mqttClientId")].as<String>();
    mqttBaseTopic = doc[F("mqttBaseTopic")].as<String>();
    mqttTelemetryInterval = doc[F("mqttTelemetryInterval")];

    // If an old config was found, migrate it to the new format now.
    if (needsMigration)
    {
        saveMqtt();
    }
}

/**
 * save MQTT json configuration to "mqtt.json"
 */
void saveMqtt()
{
    File file = LittleFS.open("mqtt.json", "w");
    if (!file)
    {
        return;
    }

    DynamicJsonDocument doc(1024);

    doc[F("enableMqtt")] = useMqtt;
    doc[F("mqttServer")] = mqttServer;
    doc[F("mqttPort")] = mqttPort;
    doc[F("mqttUsername")] = mqttUsername;
    doc[F("mqttPassword")] = mqttPassword;
    doc[F("mqttClientId")] = mqttClientId;
    doc[F("mqttBaseTopic")] = mqttBaseTopic;
    doc[F("mqttTelemetryInterval")] = mqttTelemetryInterval;

    if (serializeJson(doc, file) == 0)
    {
    }
    file.close();
}

/**
 * response for /getmqtt/
 * web server prints a json document
 */
void handleGetMqtt()
{
    if (!checkHttpPost(server->method()))
        return;

    DynamicJsonDocument doc(1024);

    doc[F("enableMqtt")] = useMqtt;
    doc[F("mqttServer")] = mqttServer;
    doc[F("mqttPort")] = mqttPort;
    doc[F("mqttUsername")] = mqttUsername;
    doc[F("mqttPassword")] = "<Passwort eingeben>";
    if (!hidePasswords)
    {
        doc[F("mqttPassword")] = mqttPassword;
    }
    doc[F("mqttClientId")] = mqttClientId;
    doc[F("mqttBaseTopic")] = mqttBaseTopic;
    doc[F("mqttTelemetryInterval")] = mqttTelemetryInterval;

    String json;
    if (serializeJson(doc, json) == 0)
    {
        json = F("{\"error\": \"Failed to serialize message\"}");
    }
    server->send(200, F("text/plain"), json);
}

/**
 * response for /setmqtt/
 * web server writes a json document
 */
void handleSetMqtt()
{
    if (!checkHttpPost(server->method()))
        return;

    DynamicJsonDocument doc(1024);
    String message = server->arg(0);
    DeserializationError error = deserializeJson(doc, message);
    if (error)
    {
        server->send(400, F("text/plain"), F("Error deserializing message"));
        return;
    }

    // Store old values to detect changes that require HA discovery re-run
    bool oldUseMqtt = useMqtt;  // Store old MQTT enabled state
    String oldMqttServer = mqttServer;
    int oldMqttPort = mqttPort;
    String oldMqttUsername = mqttUsername;
    String oldMqttPassword = mqttPassword;
    String oldMqttClientId = mqttClientId;
    String oldMqttBaseTopic = mqttBaseTopic;

    useMqtt = doc[F("enableMqtt")];
    enableMqtt = useMqtt;
    
    if (doc.containsKey(F("mqttServer"))) {
        mqttServer = doc[F("mqttServer")].as<String>();
    }

    mqttPort = doc[F("mqttPort")];
    mqttUsername = doc[F("mqttUsername")].as<String>();
    
    // Only update password if a new one is provided (not empty or placeholder)
    String newPassword = doc[F("mqttPassword")].as<String>();
    if (newPassword.length() > 0 && newPassword != "<Passwort eingeben>") {
        mqttPassword = newPassword;
    } else {
        // Load existing password from file to ensure we don't save empty password
        File file = LittleFS.open("mqtt.json", "r");
        if (file) {
            DynamicJsonDocument existingDoc(1024);
            DeserializationError error = deserializeJson(existingDoc, file);
            file.close();
            if (!error && existingDoc.containsKey(F("mqttPassword"))) {
                mqttPassword = existingDoc[F("mqttPassword")].as<String>();
            }
        }
    }
    
    mqttClientId = doc[F("mqttClientId")].as<String>();
    mqttBaseTopic = doc[F("mqttBaseTopic")].as<String>();
    mqttTelemetryInterval = doc[F("mqttTelemetryInterval")];

    // These settings affect how entities are registered in Home Assistant
    bool haRelevantChanged = false;
    String changeReason = "";
    
    if (oldMqttServer != mqttServer) {
        haRelevantChanged = true;
        changeReason = "MQTT Server geändert";
        Serial.print("MQTT: Server changed: ");
        Serial.print(oldMqttServer);
        Serial.print(" -> ");
        Serial.println(mqttServer);
    }
    
    if (oldMqttPort != mqttPort) {
        haRelevantChanged = true;
        changeReason = "MQTT Port geändert";
        Serial.print("MQTT: Port changed: ");
        Serial.print(oldMqttPort);
        Serial.print(" -> ");
        Serial.println(mqttPort);
    }
    
    if (oldMqttUsername != mqttUsername) {
        haRelevantChanged = true;
        changeReason = "MQTT Benutzername geändert";
        Serial.println("MQTT: Username changed");
    }
    
    if (oldMqttPassword != mqttPassword) {
        haRelevantChanged = true;
        changeReason = "MQTT Passwort geändert";
        Serial.println("MQTT: Password changed");
    }
    
    if (oldMqttClientId != mqttClientId) {
        haRelevantChanged = true;
        changeReason = "MQTT Client ID geändert";
        Serial.print("MQTT: Client ID changed: ");
        Serial.print(oldMqttClientId);
        Serial.print(" -> ");
        Serial.println(mqttClientId);
    }
    
    if (oldMqttBaseTopic != mqttBaseTopic) {
        haRelevantChanged = true;
        changeReason = "MQTT Base Topic geändert";
        Serial.print("MQTT: Base topic changed: ");
        Serial.print(oldMqttBaseTopic);
        Serial.print(" -> ");
        Serial.println(mqttBaseTopic);
    }

    // Save settings before responding
    saveMqtt();
    
    // Restart ESP if:
    // 1. MQTT is being enabled (disabled -> enabled): Clean boot ensures proper discovery
    // 2. MQTT was enabled, still enabled, and HA-relevant settings changed: Re-run discovery
    //
    // DO NOT restart if:
    // - MQTT is being disabled (enabled -> disabled): Stop MQTT
    // - MQTT stays disabled and settings changed: Save for future use
    // - Only non-HA-relevant settings changed (telemetry interval): Reconnect
    
    bool mqttBeingEnabled = (!oldUseMqtt && useMqtt);
    bool mqttBeingDisabled = (oldUseMqtt && !useMqtt);
    
    if (mqttBeingEnabled) {
        // MQTT disabled -> enabled: Restart for clean discovery
        Serial.println("========================================");
        Serial.println("MQTT: Enabling MQTT");
        Serial.println("MQTT: WifiWhirl restarting for clean initialization...");
        Serial.println("========================================");
        
        // Send response with restart notification
        String response = "{\"restart\": true, \"reason\": \"MQTT wird aktiviert. WifiWhirl startet neu um Home Assistant Discovery durchzuführen.\"}";
        server->send(200, F("application/json"), response);
        
        // Give server time to send response
        delay(500);
        
        // Stop all services gracefully
        periodicTimer.detach();
        updateMqttTimer.detach();
        updateWSTimer.detach();
        
        // Restart ESP - after restart, discovery will run once with new settings
        ESP.restart();
    } else if (haRelevantChanged && oldUseMqtt && useMqtt) {
        // MQTT was enabled and still is, settings changed -> restart for re-discovery
        Serial.println("========================================");
        Serial.println("MQTT: HA-relevant settings changed!");
        Serial.print("MQTT: Reason: ");
        Serial.println(changeReason);
        Serial.println("MQTT: Restarting to re-run discovery...");
        Serial.println("========================================");
        
        // Send response with restart notification
        String response = "{\"restart\": true, \"reason\": \"" + changeReason + ". WifiWhirl startet neu um MQTT Konfiguration zu aktualisieren.\"}";
        server->send(200, F("application/json"), response);
        
        // Give server time to send response
        delay(500);
        
        // Stop all services gracefully
        periodicTimer.detach();
        updateMqttTimer.detach();
        updateWSTimer.detach();
        if (mqttClient) {
            mqttClient->disconnect();
        }
        
        // Restart ESP - after restart, discovery will run once with new settings
        ESP.restart();
    } else if (mqttBeingDisabled) {
        // MQTT enabled -> disabled: No restart needed, stop MQTT
        Serial.println("MQTT: Disabling MQTT");
        Serial.println("MQTT: Settings saved");
        server->send(200, F("text/plain"), "");
        if (mqttClient) {
            mqttClient->disconnect();
        }
    } else if (haRelevantChanged && !useMqtt) {
        // MQTT disabled, settings changed: Save for future use
        Serial.println("MQTT: HA-relevant settings changed but MQTT is disabled");
        Serial.println("MQTT: Settings saved for future use");
        server->send(200, F("text/plain"), "");
    } else {
        // No HA-relevant changes, restart MQTT connection if enabled
        Serial.println("MQTT: Settings updated (no discovery required)");
        server->send(200, F("text/plain"), "");
        if (useMqtt) {
            startMqtt();
        }
    }
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

/**
 * MQTT setup and connect
 * @author 877dev
 */
void startMqtt()
{
    {
        HeapSelectIram ephemeral;
        Serial.printf("IRamheap %d\n", ESP.getFreeHeap());
        Serial.println(F("startmqtt"));
        if (!aWifiClient)
            aWifiClient = new WiFiClient;
        if (!mqttClient)
            mqttClient = new PubSubClient(*aWifiClient);
    }

    // load mqtt credential file if it exists, and update default strings
    loadMqtt();

    // disconnect in case we are already connected
    mqttClient->disconnect();

    // setup MQTT broker information as defined earlier
    mqttClient->setServer(mqttServer.c_str(), mqttPort);
    // MEMORY OPTIMIZATION: Reduced buffer from 1536 to 768 bytes to save heap
    // Home Assistant discovery still works with smaller buffers due to streaming publish
    if (mqttClient->setBufferSize(768))
    {
        Serial.println(F("MQTT > Buffer size set to 768 bytes"));
    }
    mqttClient->setKeepAlive(60);
    mqttClient->setSocketTimeout(30);
    // set callback details
    // this function is called automatically whenever a message arrives on a subscribed topic.
    mqttClient->setCallback(mqttCallback);
    // Connect to MQTT broker, publish Status/MAC/count, and subscribe to keypad topic.
    mqttConnect();
}

/**
 * MQTT callback function
 * @author 877dev
 */
void mqttCallback(char *topic, byte *payload, unsigned int length)
{
    // Serial.print(F("MQTT > Message arrived ["));
    // Serial.print(topic);
    // Serial.print(")] ");
    for (unsigned int i = 0; i < length; i++)
    {
        // Serial.print((char)payload[i]);
    }
    // Serial.println();
    if (String(topic).equals(String(mqttBaseTopic) + "/command"))
    {
        // DynamicJsonDocument doc(256);
        StaticJsonDocument<256> doc;
        String message = (const char *)&payload[0];
        DeserializationError error = deserializeJson(doc, message);
        if (error)
        {
            return;
        }

        Commands command = doc[F("CMD")];
        int64_t value = doc[F("VALUE")];
        int64_t xtime = doc[F("XTIME")] | 0;
        int64_t interval = doc[F("INTERVAL")] | 0;
        String txt = doc[F("TXT")] | "";
        command_que_item item;
        item.cmd = command;
        item.val = value;
        item.xtime = xtime;
        item.interval = interval;
        item.text = txt;
        bwc->add_command(item);
    }

    /* author @malfurion, edited by @visualapproach for v4 */
    if (String(topic).equals(String(mqttBaseTopic) + "/command_batch"))
    {
        DynamicJsonDocument doc(1024);
        String message = (const char *)&payload[0];
        DeserializationError error = deserializeJson(doc, message);
        if (error)
        {
            return;
        }

        JsonArray commandArray = doc.as<JsonArray>();

        for (JsonVariant commandItem : commandArray)
        {
            Commands command = commandItem[F("CMD")];
            int64_t value = commandItem[F("VALUE")];
            int64_t xtime = commandItem[F("XTIME")] | 0;
            int64_t interval = commandItem[F("INTERVAL")] | 0;
            String txt = commandItem[F("TXT")] | "";
            command_que_item item;
            item.cmd = command;
            item.val = value;
            item.xtime = xtime;
            item.interval = interval;
            item.text = txt;
            bwc->add_command(item);
        }
    }
}

/**
 * Connect to MQTT broker, publish Status/MAC/count, and subscribe to keypad topic.
 */
void mqttConnect()
{
    // do not connect if MQTT is not enabled
    if (!enableMqtt)
    {
        return;
    }
    Serial.println("mqttconn");

    // Serial.print(F("MQTT > Connecting ... "));
    // We'll connect with a Retained Last Will that updates the 'Status' topic with "Dead" when the device goes offline...
    if (mqttClient->connect(
            mqttClientId.c_str(),                        // client_id : the client ID to use when connecting to the server->
            mqttUsername.c_str(),                        // username : the username to use. If NULL, no username or password is used (const char[])
            mqttPassword.c_str(),                        // password : the password to use. If NULL, no password is used (const char[])setupHA
            (String(mqttBaseTopic) + "/Status").c_str(), // willTopic : the topic to be used by the will message (const char[])
            0,                                           // willQoS : the quality of service to be used by the will message (int : 0,1 or 2)
            1,                                           // willRetain : whether the will should be published with the retain flag (int : 0 or 1)
            "Dead"))                                     // willMessage : the payload of the will message (const char[])
    {
        // Serial.println(F("success!"));
        mqtt_connect_count++;

        // update MQTT every X seconds. (will also be updated on state changes)
        updateMqttTimer.attach(mqttTelemetryInterval, []
                               { sendMQTTFlag = true; });

        // These all have the Retained flag set to true, so that the value is stored on the server and can be retrieved at any point
        // Check the 'Status' topic to see that the device is still online before relying on the data from these retained topics
        mqttClient->publish((String(mqttBaseTopic) + "/Status").c_str(), "Alive", true);
        mqttClient->publish((String(mqttBaseTopic) + "/MAC_Address").c_str(), WiFi.macAddress().c_str(), true);                 // Device MAC Address
        mqttClient->publish((String(mqttBaseTopic) + "/MQTT_Connect_Count").c_str(), String(mqtt_connect_count).c_str(), true); // MQTT Connect Count
        mqttClient->loop();

        // Watch the 'command' topic for incoming MQTT messages
        mqttClient->subscribe((String(mqttBaseTopic) + "/command").c_str());
        mqttClient->subscribe((String(mqttBaseTopic) + "/command_batch").c_str());
        mqttClient->loop();

#ifdef ESP8266
        // mqttClient->publish((String(mqttBaseTopic) + "/reboot_time").c_str(), DateTime.format(DateFormatter::SIMPLE).c_str(), true);
        mqttClient->publish((String(mqttBaseTopic) + "/reboot_time").c_str(), (bwc->reboot_time_str + 'Z').c_str(), true);
        mqttClient->publish((String(mqttBaseTopic) + "/reboot_reason").c_str(), ESP.getResetReason().c_str(), true);
        String buttonname;
        buttonname.reserve(32);
        bwc->getButtonName(buttonname);
        mqttClient->publish((String(mqttBaseTopic) + "/button").c_str(), buttonname.c_str(), true);
        mqttClient->loop();
        sendMQTT();
        
        // Only run HA discovery on FIRST connection after boot
        // NOT on every reconnect (would waste memory and cause instability)
        extern bool haDiscoveryHasRunOnce;
        if (!haDiscoveryHasRunOnce)
        {
            Serial.println("HA");
            setupHA();
            Serial.println("done");
            haDiscoveryHasRunOnce = true;
        }
        else
        {
            Serial.println("HA: Skipping discovery (already ran after boot)");
        }
#endif
    }
    else
    {
        // Serial.print(F("failed, Return Code = "));
        // Serial.println(mqttClient->state()); // states explained in webSocket->js
    }
    Serial.println("end mqttcon");
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

// Keeping for later integrations
// void setTemperatureFromSensor()
// {
//     if(bwc->hasTempSensor)
//     {
//             tempSensors.requestTemperatures();
//             float temperatureC = tempSensors.getTempCByIndex(0);
//             //float temperatureF = tempSensors.getTempFByIndex(0);
//             //Serial.print(temperatureC);
//             //Serial.println("ºC");
//             //Serial.print(temperatureF);
//             //Serial.println("ºF");

//             // Ignore bad reads
//             if(temperatureC >= -20.0)
//             {
//                 bwc->setAmbientTemperature(temperatureC, true);
//             }
//     }
// }

#include "webhooks.inc"
#include "homeassistant.inc"
#include "prometheus.inc"
