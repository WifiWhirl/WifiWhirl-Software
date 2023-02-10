#include "main.h"
#include "config.h"

BWC bwc;

// initial stack
char *stack_start;
uint32_t heap_water_mark;

void setup()
{
    // init record of stack
    char stack;
    stack_start = &stack;

    Serial.begin(115200);
    Serial.println(F("\nStart"));
    periodicTimer.attach(periodicTimerInterval, []{ periodicTimerFlag = true; });
    // delayed mqtt start
    startComplete.attach(60, []{ if(useMqtt) enableMqtt = true; startComplete.detach(); });

    // update webpage every 2 seconds. (will also be updated on state changes)
    updateWSTimer.attach(2.0, []{ sendWSFlag = true; });

    // when NTP time is valid we save bootlog.txt and this timer stops
    bootlogTimer.attach(5, []{ if(DateTime.isTimeValid()) {bwc.saveRebootInfo(); bootlogTimer.detach();} });

    bwc.setup();
    // needs to be loaded here for reading files
    // LittleFS.begin();
    loadWifi();
    loadWebConfig();
    startWiFi();
    startNTP();
    startOTA();
    startHttpServer();
    startWebSocket();
    startMqtt();
    Serial.println(WiFi.localIP().toString());
    bwc.print("   ");
    bwc.print(WiFi.localIP().toString());
    bwc.print("   ");
    bwc.print(FW_VERSION);
    Serial.println(F("End of setup()"));
    heap_water_mark = ESP.getFreeHeap();
}

void loop()
{
    uint32_t freeheap = ESP.getFreeHeap();
    if(freeheap < heap_water_mark) heap_water_mark = freeheap;
    // We need this self-destructing info several times, so save it locally
    bool newData = bwc.newData();
    // Fiddle with the pump computer
    bwc.loop();

    // run only when a wifi connection is established
    if (WiFi.status() == WL_CONNECTED)
    {
        // listen for websocket events
        // webSocket.loop();
        // listen for webserver events
        server.handleClient();
        // listen for OTA events
        ArduinoOTA.handle();

        // MQTT
        if (enableMqtt && mqttClient.loop())
        {
        String msg = bwc.getButtonName();
        // publish pretty button name if display button is pressed (or NOBTN if released)
        if (!msg.equals(prevButtonName))
        {
            mqttClient.publish((String(mqttBaseTopic) + "/button").c_str(), String(msg).c_str(), true);
            prevButtonName = msg;
        }

        if (newData || sendMQTTFlag)
        {
            sendMQTT();
            sendMQTTFlag = false;
        }
        }

        // web socket
        if (newData)
        {
        sendWS();
        }
        else if (sendWSFlag)
        {
        sendWSFlag = false;
        sendWS();
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
        bwc.print(F("check network"));
        // Serial.println(F("WiFi > Trying to reconnect ..."));
        }
        if (WiFi.status() == WL_CONNECTED)
        {
        // could be interesting to display the IP
        //bwc.print(WiFi.localIP().toString());

        if (!DateTime.isTimeValid())
        {
            // Serial.println(F("NTP > Start synchronisation"));
            DateTime.begin();
        }

        if (enableMqtt && !mqttClient.loop())
        {
            // Serial.println(F("MQTT > Not connected"));
            mqttConnect();
        }
        }
    }

    //Only do this if locked out! (by pressing POWER - LOCK - TIMER - POWER)
    //   if(bwc.getBtnSeqMatch())
    //   {
    //     resetWiFi();
    //     ESP.reset();
    //   }
    //handleAUX();

    // You can add own code here, but don't stall!
    // If CPU is choking you can try to run @ 160 MHz, but that's cheating!
}

/**
 * Send status data to web client in JSON format (because it is easy to decode on the other side)
 */
void sendWS()
{
    String json;

    // send states
    json = bwc.getJSONStates();
    webSocket.broadcastTXT(json);

    // send times
    json = bwc.getJSONTimes();
    webSocket.broadcastTXT(json);

    // send other info
    json = getOtherInfo();
    webSocket.broadcastTXT(json);
}

String getOtherInfo()
{
    DynamicJsonDocument doc(256);
    String json = "";
    // Set the values in the document
    doc["CONTENT"] = "OTHER";
    doc["MQTT"] = mqttClient.state();
    /*TODO: add these:*/
    //   doc["PressedButton"] = bwc.getPressedButton();
    //   doc["HASJETS"] = HASJETS;
    doc["MODEL"] = bwc.getModel();
    doc["RSSI"] = WiFi.RSSI();
    doc["IP"] = WiFi.localIP().toString();
    doc["SSID"] = WiFi.SSID();
    doc["FW"] = FW_VERSION;

    // Serialize JSON to string
    if (serializeJson(doc, json) == 0)
    {
        json = "{\"error\": \"Failed to serialize message\"}";
    }
    return json;
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
    String json;

    // send states
    json = bwc.getJSONStates();
    if (mqttClient.publish((String(mqttBaseTopic) + "/message").c_str(), String(json).c_str(), true))
    {
        //Serial.println(F("MQTT > message published"));
    }
    else
    {
        //Serial.println(F("MQTT > message not published"));
    }

    // send times
    json = bwc.getJSONTimes();
    if (mqttClient.publish((String(mqttBaseTopic) + "/times").c_str(), String(json).c_str(), true))
    {
        //Serial.println(F("MQTT > times published"));
    }
    else
    {
        //Serial.println(F("MQTT > times not published"));
    }

    //send other info
    json = getOtherInfo();
    if (mqttClient.publish((String(mqttBaseTopic) + "/other").c_str(), String(json).c_str(), true))
    {
        //Serial.println(F("MQTT > other published"));
    }
    else
    {
        //Serial.println(F("MQTT > other not published"));
    }
}

/**
 * Start a Wi-Fi access point, and try to connect to some given access points.
 * Then wait for either an AP or STA connection
 */
void startWiFi()
{
    //WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.persistent(true);
    WiFi.hostname(netHostname);

    if (enableStaticIp4)
    {
        // Serial.println("WiFi > using static IP \"" + ip4Address.toString() + "\" on gateway \"" + ip4Gateway.toString() + "\"");
        WiFi.config(ip4Address, ip4Gateway, ip4Subnet, ip4DnsPrimary, ip4DnsSecondary);
    }

    if (enableAp)
    {
        // Serial.println("WiFi > using WiFi configuration with SSID \"" + apSsid + "\"");

        WiFi.begin(apSsid.c_str(), apPwd.c_str());

        // Serial.print(F("WiFi > Trying to connect ..."));
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
            if (enableWmApFallback)
            {
            // disable specific WiFi config
            enableAp = false;
            enableStaticIp4 = false;
            // fallback to WiFi config portal
            startWiFiConfigPortal();
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
        enableAp = true;
        apSsid = WiFi.SSID();
        apPwd = WiFi.psk();
        saveWifi();

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
    wm.autoConnect(wmApName, wmApPassword);
    // Serial.print(F("WiFi > Trying to connect ..."));
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        // Serial.print(".");
    }
    // Serial.println("");
}

/**
 * start NTP sync
 */
void startNTP()
{
    DateTime.setServer("pool.ntp.org");
    DateTime.begin(3000);
}

void startOTA()
{
    ArduinoOTA.setHostname(OTAName);
    ArduinoOTA.setPassword(OTAPassword);

    ArduinoOTA.onStart([]() {
        // Serial.println(F("OTA > Start"));
        stopall();
    });
    ArduinoOTA.onEnd([]() {
        // Serial.println(F("OTA > End"));
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        // Serial.printf("OTA > Progress: %u%%\r\n", (progress / (total / 100)));
    });
    ArduinoOTA.onError([](ota_error_t error) {
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
    bwc.stop();
    updateMqttTimer.detach();
    periodicTimer.detach();
    updateWSTimer.detach();
    //bwc.saveSettings();
    LittleFS.end();
    server.stop();
    webSocket.close();
    mqttClient.disconnect();
}

void startWebSocket()
{
    // In case we are already running
    webSocket.close();
    webSocket.begin();
    webSocket.enableHeartbeat(3000, 3000, 1);
    webSocket.onEvent(webSocketEvent);
    // Serial.println(F("WebSocket > server started"));
}

/**
 * handle web socket events
 */
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t len)
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
            // IPAddress ip = webSocket.remoteIP(num);
            // Serial.printf("WebSocket > [%u] Connected from %d.%d.%d.%d url: %s\r\n", num, ip[0], ip[1], ip[2], ip[3], payload);
            sendWS();
        }
        break;

        // if new text data is received
        case WStype_TEXT:
        {
            // Serial.printf("WebSocket > [%u] get Text: %s\r\n", num, payload);
            DynamicJsonDocument doc(256);
            DeserializationError error = deserializeJson(doc, payload);
            if (error)
            {
            Serial.println(F("WebSocket > JSON command failed"));
            return;
            }

            // Copy values from the JsonDocument to the Config
            int64_t command = doc["CMD"];
            int64_t value = doc["VALUE"];
            int64_t xtime = doc["XTIME"];
            int64_t interval = doc["INTERVAL"];
            String txt = doc["TXT"] | "";
            command_que_item item;
            item.cmd = command;
            item.val = value;
            item.xtime = xtime;
            item.interval = interval;
            item.text = txt;
            bwc.add_command(item);
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
    // In case we are already running
    server.stop();
    server.on(F("/getconfig/"), handleGetConfig);
    server.on(F("/setconfig/"), handleSetConfig);
    server.on(F("/getcommands/"), handleGetCommandQueue);
    server.on(F("/addcommand/"), handleAddCommand);
    server.on(F("/getwebconfig/"), handleGetWebConfig);
    server.on(F("/setwebconfig/"), handleSetWebConfig);
    server.on(F("/getwifi/"), handleGetWifi);
    server.on(F("/setwifi/"), handleSetWifi);
    server.on(F("/resetwifi/"), handleResetWifi);
    server.on(F("/getmqtt/"), handleGetMqtt);
    server.on(F("/setmqtt/"), handleSetMqtt);
    server.on(F("/dir/"), handleDir);
    server.on(F("/hwtest/"), handleHWtest);
    server.on(F("/upload.html"), HTTP_POST, [](){
        server.send(200, "text/plain", "");
    }, handleFileUpload);
    server.on(F("/remove.html"), HTTP_POST, handleFileRemove);
    server.on(F("/restart/"), handleRestart);
    server.on(F("/metrics"), handlePrometheusMetrics);  //prometheus metrics
    server.on(F("/info/"), handleESPInfo);
    server.on(F("/sethardware/"), handleSetHardware);
    server.on(F("/gethardware/"), handleGetHardware);
    

    // if someone requests any other file or page, go to function 'handleNotFound'
    // and check if the file exists
    server.onNotFound(handleNotFound);
    // start the HTTP server
    server.begin();
    // Serial.println(F("HTTP > server started"));
}

void handleGetHardware()
{
    if (!checkHttpPost(server.method())) return;
    File file = LittleFS.open("hwcfg.json", "r");
    if (!file)
    {
        // Serial.println(F("Failed to open hwcfg.json"));
        server.send(404, "text/plain", "not found");
        return;
    }
    server.send(200, "text/plain", file.readString());
    file.close();
}

void handleSetHardware()
{
    if (!checkHttpPost(server.method())) return;
    String message = server.arg(0);
    // Serial.printf("Set hw message; %s\n", message.c_str());
    File file = LittleFS.open("hwcfg.json", "w");
    if (!file)
    {
        // Serial.println(F("Failed to save hwcfg.json"));
        return;
    }
    file.print(message);
    file.close();
    server.send(200, "text/plain", "ok");
    // Serial.println("sethardware done");
}

void handleHWtest()
{
    int errors = 0;
    bool state = false;
    String result = "";

    bwc.stop();
    delay(1000);

    for(int pin = 0; pin < 3; pin++)
    {
        pinMode(bwc.pins[pin], OUTPUT);
        pinMode(bwc.pins[pin+3], INPUT);
        for(int t = 0; t < 1000; t++)
        {
        state = !state;
        digitalWrite(bwc.pins[pin], state);
        delayMicroseconds(10);
        errors += digitalRead(bwc.pins[pin+3]) != state;
        }
        if(errors > 499)
        result += "CIO to DSP pin " + String(pin+3) + " fail!";
        else if(errors == 0)
        result += "CIO to DSP pin " + String(pin+3) + " success!";
        else
        result += "CIO to DSP pin " + String(pin+3) + " " + String(errors/500) + "\% bad";
        errors = 0;
        delay(0);
    }
    for(int pin = 0; pin < 3; pin++)
    {
        pinMode(bwc.pins[pin+3], OUTPUT);
        pinMode(bwc.pins[pin], INPUT);
        for(int t = 0; t < 1000; t++)
        {
        state = !state;
        digitalWrite(bwc.pins[pin+3], state);
        delayMicroseconds(10);
        errors += digitalRead(bwc.pins[pin]) != state;
        }
        if(errors > 499)
        result += "DSP to CIO pin " + String(pin+3) + " fail!";
        else if(errors == 0)
        result += "DSP to CIO pin " + String(pin+3) + " success!";
        else
        result += "DSP to CIO pin " + String(pin+3) + " " + String(errors/500) + "\% bad";
        errors = 0;
        delay(0);
    }

    server.send(200, "text/plain", result);
    delay(10000);
    bwc.setup();
}

void handleNotFound()
{
    // check if the file exists in the flash memory (LittleFS), if so, send it
    if (!handleFileRead(server.uri()))
    {
        server.send(404, "text/plain", "404: File Not Found");
    }
}

String getContentType(const String& filename)
{
    if (filename.endsWith(".html")) return "text/html";
    else if (filename.endsWith(".css")) return "text/css";
    else if (filename.endsWith(".js")) return "application/javascript";
    else if (filename.endsWith(".ico")) return "image/x-icon";
    else if (filename.endsWith(".gz")) return "application/x-gzip";
    else if (filename.endsWith(".json")) return "application/json";
    return "text/plain";
}

/**
 * send the right file to the client (if it exists)
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
        server.send(403, "text/plain", "Permission denied.");
        // Serial.println(F("HTTP > file reading denied (credentials)."));
        return false;
    }
    String contentType = getContentType(path);             // Get the MIME type
    String pathWithGz = path + ".gz";
    if (LittleFS.exists(pathWithGz) || LittleFS.exists(path)) { // If the file exists, either as a compressed archive, or normal
        if (LittleFS.exists(pathWithGz))                         // If there's a compressed version available
        path += ".gz";                                         // Use the compressed version
        File file = LittleFS.open(path, "r");                    // Open the file
        // size_t sent = server.streamFile(file, contentType);    // Send it to the client
        server.streamFile(file, contentType);    // Send it to the client
        file.close();                                          // Close the file again
        // Serial.println("HTTP > file sent: " + path + " (" + sent + " bytes)");
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
        server.send(405, "text/plain", "Method not allowed.");
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
    if (!checkHttpPost(server.method())) return;

    String json = bwc.getJSONSettings();
    server.send(200, "text/plain", json);
}

/**
 * response for /setconfig/
 * web server writes a json document
 */
void handleSetConfig()
{
    if (!checkHttpPost(server.method())) return;

    String message = server.arg(0);
    bwc.setJSONSettings(message);

    server.send(200, "text/plain", "");
}

/**
 * response for /getcommands/
 * web server prints a json document
 */
void handleGetCommandQueue()
{
    if (!checkHttpPost(server.method())) return;

    String json = bwc.getJSONCommandQueue();
    server.send(200, "application/json", json);
}

/**
 * response for /addcommand/
 * add a command to the queue
 */
void handleAddCommand()
{
    if (!checkHttpPost(server.method())) return;

    DynamicJsonDocument doc(256);
    String message = server.arg(0);
    DeserializationError error = deserializeJson(doc, message);
    if (error)
    {
        server.send(400, "text/plain", "Error deserializing message");
        return;
    }

    int64_t command = doc["CMD"];
    int64_t value = doc["VALUE"];
    int64_t xtime = doc["XTIME"];
    int64_t interval = doc["INTERVAL"];
    String txt = doc["TXT"] | "";
    command_que_item item;
    item.cmd = command;
    item.val = value;
    item.xtime = xtime;
    item.interval = interval;
    item.text = txt;
    bwc.add_command(item);

    server.send(200, "text/plain", "");
}

/**
 * load "Web Config" json configuration from "webconfig.json"
 */
void loadWebConfig()
{
    DynamicJsonDocument doc(1024);

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

    showSectionTemperature = (doc.containsKey("SST") ? doc["SST"] : true);
    showSectionDisplay = (doc.containsKey("SSD") ? doc["SSD"] : true);
    showSectionControl = (doc.containsKey("SSC") ? doc["SSC"] : true);
    showSectionButtons = (doc.containsKey("SSB") ? doc["SSB"] : true);
    showSectionTimer = (doc.containsKey("SSTIM") ? doc["SSTIM"] : true);
    showSectionTotals = (doc.containsKey("SSTOT") ? doc["SSTOT"] : true);
    useControlSelector = (doc.containsKey("UCS") ? doc["UCS"] : false);
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

    DynamicJsonDocument doc(256);

    doc["SST"] = showSectionTemperature;
    doc["SSD"] = showSectionDisplay;
    doc["SSC"] = showSectionControl;
    doc["SSB"] = showSectionButtons;
    doc["SSTIM"] = showSectionTimer;
    doc["SSTOT"] = showSectionTotals;
    doc["UCS"] = useControlSelector;

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
    if (!checkHttpPost(server.method())) return;

    DynamicJsonDocument doc(256);

    doc["SST"] = showSectionTemperature;
    doc["SSD"] = showSectionDisplay;
    doc["SSC"] = showSectionControl;
    doc["SSB"] = showSectionButtons;
    doc["SSTIM"] = showSectionTimer;
    doc["SSTOT"] = showSectionTotals;
    doc["UCS"] = useControlSelector;

    String json;
    if (serializeJson(doc, json) == 0)
    {
        json = F("{\"error\": \"Failed to serialize message\"}");
    }
    server.send(200, "application/json", json);
}

/**
 * response for /setwebconfig/
 * web server writes a json document
 */
void handleSetWebConfig()
{
    if (!checkHttpPost(server.method())) return;

    DynamicJsonDocument doc(256);
    String message = server.arg(0);
    DeserializationError error = deserializeJson(doc, message);
    if (error)
    {
        // Serial.println(F("Failed to read config file"));
        server.send(400, "text/plain", "Error deserializing message");
        return;
    }

    showSectionTemperature = doc["SST"];
    showSectionDisplay = doc["SSD"];
    showSectionControl = doc["SSC"];
    showSectionButtons = doc["SSB"];
    showSectionTimer = doc["SSTIM"];
    showSectionTotals = doc["SSTOT"];
    useControlSelector = doc["UCS"];

    saveWebConfig();

    server.send(200, "text/plain", "");
}

/**
 * load WiFi json configuration from "wifi.json"
 */
void loadWifi()
{
    File file = LittleFS.open("/wifi.json", "r");
    if (!file)
    {
        // Serial.println(F("Failed to read wifi.json. Using defaults."));
        return;
    }

    DynamicJsonDocument doc(1024);

    DeserializationError error = deserializeJson(doc, file);
    if (error)
    {
        // Serial.println(F("Failed to deserialize wifi.json"));
        file.close();
        return;
    }

    enableAp = doc["enableAp"];
    if(doc.containsKey("enableWM")) enableWmApFallback = doc["enableWM"];
    apSsid = doc["apSsid"].as<String>();
    apPwd = doc["apPwd"].as<String>();

    enableStaticIp4 = doc["enableStaticIp4"];
    ip4Address[0] = doc["ip4Address"][0];
    ip4Address[1] = doc["ip4Address"][1];
    ip4Address[2] = doc["ip4Address"][2];
    ip4Address[3] = doc["ip4Address"][3];
    ip4Gateway[0] = doc["ip4Gateway"][0];
    ip4Gateway[1] = doc["ip4Gateway"][1];
    ip4Gateway[2] = doc["ip4Gateway"][2];
    ip4Gateway[3] = doc["ip4Gateway"][3];
    ip4Subnet[0] = doc["ip4Subnet"][0];
    ip4Subnet[1] = doc["ip4Subnet"][1];
    ip4Subnet[2] = doc["ip4Subnet"][2];
    ip4Subnet[3] = doc["ip4Subnet"][3];
    ip4DnsPrimary[0] = doc["ip4DnsPrimary"][0];
    ip4DnsPrimary[1] = doc["ip4DnsPrimary"][1];
    ip4DnsPrimary[2] = doc["ip4DnsPrimary"][2];
    ip4DnsPrimary[3] = doc["ip4DnsPrimary"][3];
    ip4DnsSecondary[0] = doc["ip4DnsSecondary"][0];
    ip4DnsSecondary[1] = doc["ip4DnsSecondary"][1];
    ip4DnsSecondary[2] = doc["ip4DnsSecondary"][2];
    ip4DnsSecondary[3] = doc["ip4DnsSecondary"][3];
}

/**
 * save WiFi json configuration to "wifi.json"
 */
void saveWifi()
{
    File file = LittleFS.open("/wifi.json", "w");
    if (!file)
    {
        // Serial.println(F("Failed to save wifi.json"));
        return;
    }

    DynamicJsonDocument doc(1024);

    doc["enableAp"] = enableAp;
    doc["enableWM"] = enableWmApFallback;
    doc["apSsid"] = apSsid;
    doc["apPwd"] = apPwd;

    doc["enableStaticIp4"] = enableStaticIp4;
    doc["ip4Address"][0] = ip4Address[0];
    doc["ip4Address"][1] = ip4Address[1];
    doc["ip4Address"][2] = ip4Address[2];
    doc["ip4Address"][3] = ip4Address[3];
    doc["ip4Gateway"][0] = ip4Gateway[0];
    doc["ip4Gateway"][1] = ip4Gateway[1];
    doc["ip4Gateway"][2] = ip4Gateway[2];
    doc["ip4Gateway"][3] = ip4Gateway[3];
    doc["ip4Subnet"][0] = ip4Subnet[0];
    doc["ip4Subnet"][1] = ip4Subnet[1];
    doc["ip4Subnet"][2] = ip4Subnet[2];
    doc["ip4Subnet"][3] = ip4Subnet[3];
    doc["ip4DnsPrimary"][0] = ip4DnsPrimary[0];
    doc["ip4DnsPrimary"][1] = ip4DnsPrimary[1];
    doc["ip4DnsPrimary"][2] = ip4DnsPrimary[2];
    doc["ip4DnsPrimary"][3] = ip4DnsPrimary[3];
    doc["ip4DnsSecondary"][0] = ip4DnsSecondary[0];
    doc["ip4DnsSecondary"][1] = ip4DnsSecondary[1];
    doc["ip4DnsSecondary"][2] = ip4DnsSecondary[2];
    doc["ip4DnsSecondary"][3] = ip4DnsSecondary[3];

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
    if (!checkHttpPost(server.method())) return;

    DynamicJsonDocument doc(1024);

    doc["enableAp"] = enableAp;
    doc["enableWM"] = enableWmApFallback;
    doc["apSsid"] = apSsid;
    doc["apPwd"] = "<enter password>";
    if (!hidePasswords)
    {
        doc["apPwd"] = apPwd;
    }

    doc["enableStaticIp4"] = enableStaticIp4;
    doc["ip4Address"][0] = ip4Address[0];
    doc["ip4Address"][1] = ip4Address[1];
    doc["ip4Address"][2] = ip4Address[2];
    doc["ip4Address"][3] = ip4Address[3];
    doc["ip4Gateway"][0] = ip4Gateway[0];
    doc["ip4Gateway"][1] = ip4Gateway[1];
    doc["ip4Gateway"][2] = ip4Gateway[2];
    doc["ip4Gateway"][3] = ip4Gateway[3];
    doc["ip4Subnet"][0] = ip4Subnet[0];
    doc["ip4Subnet"][1] = ip4Subnet[1];
    doc["ip4Subnet"][2] = ip4Subnet[2];
    doc["ip4Subnet"][3] = ip4Subnet[3];
    doc["ip4DnsPrimary"][0] = ip4DnsPrimary[0];
    doc["ip4DnsPrimary"][1] = ip4DnsPrimary[1];
    doc["ip4DnsPrimary"][2] = ip4DnsPrimary[2];
    doc["ip4DnsPrimary"][3] = ip4DnsPrimary[3];
    doc["ip4DnsSecondary"][0] = ip4DnsSecondary[0];
    doc["ip4DnsSecondary"][1] = ip4DnsSecondary[1];
    doc["ip4DnsSecondary"][2] = ip4DnsSecondary[2];
    doc["ip4DnsSecondary"][3] = ip4DnsSecondary[3];

    String json;
    if (serializeJson(doc, json) == 0)
    {
        json = F("{\"error\": \"Failed to serialize message\"}");
    }
    server.send(200, "application/json", json);
}

/**
 * response for /setwifi/
 * web server writes a json document
 */
void handleSetWifi()
{
    if (!checkHttpPost(server.method())) return;

    DynamicJsonDocument doc(1024);
    String message = server.arg(0);
    DeserializationError error = deserializeJson(doc, message);
    if (error)
    {
        // Serial.println(F("Failed to read config file"));
        server.send(400, "text/plain", "Error deserializing message");
        return;
    }

    enableAp = doc["enableAp"];
    if(doc.containsKey("enableWM")) enableWmApFallback = doc["enableWM"];
    apSsid = doc["apSsid"].as<String>();
    apPwd = doc["apPwd"].as<String>();

    enableStaticIp4 = doc["enableStaticIp4"];
    ip4Address[0] = doc["ip4Address"][0];
    ip4Address[1] = doc["ip4Address"][1];
    ip4Address[2] = doc["ip4Address"][2];
    ip4Address[3] = doc["ip4Address"][3];
    ip4Gateway[0] = doc["ip4Gateway"][0];
    ip4Gateway[1] = doc["ip4Gateway"][1];
    ip4Gateway[2] = doc["ip4Gateway"][2];
    ip4Gateway[3] = doc["ip4Gateway"][3];
    ip4Subnet[0] = doc["ip4Subnet"][0];
    ip4Subnet[1] = doc["ip4Subnet"][1];
    ip4Subnet[2] = doc["ip4Subnet"][2];
    ip4Subnet[3] = doc["ip4Subnet"][3];
    ip4DnsPrimary[0] = doc["ip4DnsPrimary"][0];
    ip4DnsPrimary[1] = doc["ip4DnsPrimary"][1];
    ip4DnsPrimary[2] = doc["ip4DnsPrimary"][2];
    ip4DnsPrimary[3] = doc["ip4DnsPrimary"][3];
    ip4DnsSecondary[0] = doc["ip4DnsSecondary"][0];
    ip4DnsSecondary[1] = doc["ip4DnsSecondary"][1];
    ip4DnsSecondary[2] = doc["ip4DnsSecondary"][2];
    ip4DnsSecondary[3] = doc["ip4DnsSecondary"][3];

    saveWifi();

    server.send(200, "text/plain", "");
}

/**
 * response for /resetwifi/
 * do this before giving away the device (be aware of other credentials e.g. MQTT)
 * a complete flash erase should do the job but remember to upload the filesystem as well.
 */
void handleResetWifi()
{
    server.send(200, F("text/html"), F("WiFi connection reset (erase) ..."));
    // Serial.println(F("WiFi connection reset (erase) ..."));
    resetWiFi();

    server.send(200, F("text/html"), F("WiFi connection reset (erase) ... done."));
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
    periodicTimer.detach();
    updateMqttTimer.detach();
    updateWSTimer.detach();
    bwc.stop();
    bwc.saveSettings();
    delay(1000);
#if defined(ESP8266)
    ESP.eraseConfig();
#endif
    delay(1000);

    enableAp = false;
    enableWmApFallback = true;
    apSsid = F("empty");
    apPwd = F("empty");
    saveWifi();
    delay(1000);

    wm.resetSettings();
    //WiFi.disconnect();
    delay(1000);
}

/**
 * load MQTT json configuration from "mqtt.json"
 */
void loadMqtt()
{
    File file = LittleFS.open("mqtt.json", "r");
    if (!file)
    {
        Serial.println(F("Failed to read mqtt.json. Using defaults."));
        return;
    }

    DynamicJsonDocument doc(1024);

    DeserializationError error = deserializeJson(doc, file);
    if (error)
    {
        // Serial.println(F("Failed to deserialize mqtt.json."));
        file.close();
        return;
    }

    useMqtt = doc["enableMqtt"];
    mqttIpAddress[0] = doc["mqttIpAddress"][0];
    mqttIpAddress[1] = doc["mqttIpAddress"][1];
    mqttIpAddress[2] = doc["mqttIpAddress"][2];
    mqttIpAddress[3] = doc["mqttIpAddress"][3];
    mqttPort = doc["mqttPort"];
    mqttUsername = doc["mqttUsername"].as<String>();
    mqttPassword = doc["mqttPassword"].as<String>();
    mqttClientId = doc["mqttClientId"].as<String>();
    mqttBaseTopic = doc["mqttBaseTopic"].as<String>();
    mqttTelemetryInterval = doc["mqttTelemetryInterval"];
}

/**
 * save MQTT json configuration to "mqtt.json"
 */
void saveMqtt()
{
    File file = LittleFS.open("mqtt.json", "w");
    if (!file)
    {
        // Serial.println(F("Failed to save mqtt.json"));
        return;
    }

    DynamicJsonDocument doc(1024);

    doc["enableMqtt"] = useMqtt;
    doc["mqttIpAddress"][0] = mqttIpAddress[0];
    doc["mqttIpAddress"][1] = mqttIpAddress[1];
    doc["mqttIpAddress"][2] = mqttIpAddress[2];
    doc["mqttIpAddress"][3] = mqttIpAddress[3];
    doc["mqttPort"] = mqttPort;
    doc["mqttUsername"] = mqttUsername;
    doc["mqttPassword"] = mqttPassword;
    doc["mqttClientId"] = mqttClientId;
    doc["mqttBaseTopic"] = mqttBaseTopic;
    doc["mqttTelemetryInterval"] = mqttTelemetryInterval;

    if (serializeJson(doc, file) == 0)
    {
        // Serial.println(F("{\"error\": \"Failed to serialize file\"}"));
    }
    file.close();
}

/**
 * response for /getmqtt/
 * web server prints a json document
 */
void handleGetMqtt()
{
    if (!checkHttpPost(server.method())) return;

    DynamicJsonDocument doc(1024);

    doc["enableMqtt"] = useMqtt;
    doc["mqttIpAddress"][0] = mqttIpAddress[0];
    doc["mqttIpAddress"][1] = mqttIpAddress[1];
    doc["mqttIpAddress"][2] = mqttIpAddress[2];
    doc["mqttIpAddress"][3] = mqttIpAddress[3];
    doc["mqttPort"] = mqttPort;
    doc["mqttUsername"] = mqttUsername;
    doc["mqttPassword"] = "<enter password>";
    if (!hidePasswords)
    {
        doc["mqttPassword"] = mqttPassword;
    }
    doc["mqttClientId"] = mqttClientId;
    doc["mqttBaseTopic"] = mqttBaseTopic;
    doc["mqttTelemetryInterval"] = mqttTelemetryInterval;

    String json;
    if (serializeJson(doc, json) == 0)
    {
        json = F("{\"error\": \"Failed to serialize message\"}");
    }
    server.send(200, "text/plain", json);
}

/**
 * response for /setmqtt/
 * web server writes a json document
 */
void handleSetMqtt()
{
    if (!checkHttpPost(server.method())) return;

    DynamicJsonDocument doc(1024);
    String message = server.arg(0);
    DeserializationError error = deserializeJson(doc, message);
    if (error)
    {
        // Serial.println(F("Failed to read config file"));
        server.send(400, "text/plain", "Error deserializing message");
        return;
    }

    useMqtt = doc["enableMqtt"];
    mqttIpAddress[0] = doc["mqttIpAddress"][0];
    mqttIpAddress[1] = doc["mqttIpAddress"][1];
    mqttIpAddress[2] = doc["mqttIpAddress"][2];
    mqttIpAddress[3] = doc["mqttIpAddress"][3];
    mqttPort = doc["mqttPort"];
    mqttUsername = doc["mqttUsername"].as<String>();
    mqttPassword = doc["mqttPassword"].as<String>();
    mqttClientId = doc["mqttClientId"].as<String>();
    mqttBaseTopic = doc["mqttBaseTopic"].as<String>();
    mqttTelemetryInterval = doc["mqttTelemetryInterval"];

    server.send(200, "text/plain", "");

    saveMqtt();
    startMqtt();
}

/**
 * response for /dir/
 * web server prints a list of files
 */
void handleDir()
{
    #ifdef ESP8266
    String mydir;
    Dir root = LittleFS.openDir("/");
    while (root.next())
    {
        // Serial.println(root.fileName());
        mydir += "<a href=\"/" + root.fileName() + "\">" + root.fileName() + "</a>" + F(" \t Size: ");
        mydir += String(root.fileSize()) + F(" Bytes<br>");
    }
    server.send(200, "text/html", mydir);
    #endif
}

/**
 * response for /upload.html
 * upload a new file to the LittleFS
 */
void handleFileUpload()
{
    HTTPUpload& upload = server.upload();
    String path;
    if (upload.status == UPLOAD_FILE_START)
    {
        path = upload.filename;
        if (!path.startsWith("/"))
        {
        path = "/" + path;
        }

        // The file server always prefers a compressed version of a file
        if (!path.endsWith(".gz"))
        {
        // So if an uploaded file is not compressed, the existing compressed
        String pathWithGz = path + ".gz";
        // version of that file must be deleted (if it exists)
        if (LittleFS.exists(pathWithGz))
        {
            LittleFS.remove(pathWithGz);
        }
        }

        // Serial.print(F("handleFileUpload Name: "));
        // Serial.println(path);

        // Open the file for writing in LittleFS (create if it doesn't exist)
        fsUploadFile = LittleFS.open(path, "w");
        path = String();
    }
    else if (upload.status == UPLOAD_FILE_WRITE)
    {
        if (fsUploadFile)
        {
        // Write the received bytes to the file
        fsUploadFile.write(upload.buf, upload.currentSize);
        }
    }
    else if (upload.status == UPLOAD_FILE_END)
    {
        if (fsUploadFile)
        {
            fsUploadFile.close();
            // Serial.print(F("handleFileUpload Size: "));
            // Serial.println(upload.totalSize);

            server.sendHeader("Location", "/success.html");
            server.send(303);

            if (upload.filename == "cmdq.json")
            {
                bwc.reloadCommandQueue();
            }
            if (upload.filename == "settings.json")
            {
                bwc.reloadSettings();
            }
        }
        else
        {
            server.send(500, "text/plain", "500: couldn't create file");
        }
    }
}

/**
 * response for /remove.html
 * delete a file from the LittleFS
 */
void handleFileRemove()
{
    String path;
    path = server.arg(F("FileToRemove"));
    if (!path.startsWith("/"))
    {
        path = "/" + path;
    }

    // Serial.print(F("handleFileRemove Name: "));
    // Serial.println(path);

    if (LittleFS.exists(path) && LittleFS.remove(path))
    {
        // Serial.print(F("handleFileRemove success: "));
        // Serial.println(path);
        server.sendHeader("Location", "/success.html");
        server.send(303);
    }
    else
    {
        // Serial.print(F("handleFileRemove error: "));
        // Serial.println(path);
        server.send(500, "text/plain", "500: couldn't delete file");
    }
}

/**
 * response for /restart/
 */
void handleRestart()
{
    server.send(200, F("text/html"), F("ESP restart ..."));

    server.sendHeader(F("Location"), "/");
    server.send(303);

    delay(1000);
    stopall();
    delay(1000);
    Serial.println(F("ESP restart ..."));
    ESP.restart();
    delay(3000);
}


/**
 * MQTT setup and connect
 * @author 877dev
 */
void startMqtt()
{
    // load mqtt credential file if it exists, and update default strings
    loadMqtt();

    // disconnect in case we are already connected
    mqttClient.disconnect();

    // setup MQTT broker information as defined earlier
    mqttClient.setServer(mqttIpAddress, mqttPort);
    // set buffer for larger messages, new to library 2.8.0
    if (mqttClient.setBufferSize(1536))
    {
        // Serial.println(F("MQTT > Buffer size successfully increased"));
    }
    mqttClient.setKeepAlive(60);
    mqttClient.setSocketTimeout(30);
    // set callback details
    // this function is called automatically whenever a message arrives on a subscribed topic.
    mqttClient.setCallback(mqttCallback);
    // Connect to MQTT broker, publish Status/MAC/count, and subscribe to keypad topic.
    mqttConnect();
}

/**
 * MQTT callback function
 * @author 877dev
 */
void mqttCallback(char* topic, byte* payload, unsigned int length)
{
    // Serial.print(F("MQTT > Message arrived ["));
    // Serial.print(topic);
    // Serial.print("] ");
    for (unsigned int i = 0; i < length; i++)
    {
        // Serial.print((char)payload[i]);
    }
    // Serial.println();
    if (String(topic).equals(String(mqttBaseTopic) + "/command"))
    {
        DynamicJsonDocument doc(256);
        String message = (const char *) &payload[0];
        DeserializationError error = deserializeJson(doc, message);
        if (error)
        {
        return;
        }

        int64_t command = doc["CMD"];
        int64_t value = doc["VALUE"];
        int64_t xtime = doc["XTIME"];
        int64_t interval = doc["INTERVAL"];
        String txt = doc["TXT"] | "";
        command_que_item item;
        item.cmd = command;
        item.val = value;
        item.xtime = xtime;
        item.interval = interval;
        item.text = txt;
        bwc.add_command(item);
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

    // Serial.print(F("MQTT > Connecting ... "));
    // We'll connect with a Retained Last Will that updates the 'Status' topic with "Dead" when the device goes offline...
    if (mqttClient.connect(
        mqttClientId.c_str(), // client_id : the client ID to use when connecting to the server.
        mqttUsername.c_str(), // username : the username to use. If NULL, no username or password is used (const char[])
        mqttPassword.c_str(), // password : the password to use. If NULL, no password is used (const char[])
        (String(mqttBaseTopic) + "/Status").c_str(), // willTopic : the topic to be used by the will message (const char[])
        0, // willQoS : the quality of service to be used by the will message (int : 0,1 or 2)
        1, // willRetain : whether the will should be published with the retain flag (int : 0 or 1)
        "Dead")) // willMessage : the payload of the will message (const char[])
    {
        // Serial.println(F("success!"));
        mqtt_connect_count++;

        // update MQTT every X seconds. (will also be updated on state changes)
        updateMqttTimer.attach(mqttTelemetryInterval, []{ sendMQTTFlag = true; });

        // These all have the Retained flag set to true, so that the value is stored on the server and can be retrieved at any point
        // Check the 'Status' topic to see that the device is still online before relying on the data from these retained topics
        mqttClient.publish((String(mqttBaseTopic) + "/Status").c_str(), "Alive", true);
        mqttClient.publish((String(mqttBaseTopic) + "/MAC_Address").c_str(), WiFi.macAddress().c_str(), true);                 // Device MAC Address
        mqttClient.publish((String(mqttBaseTopic) + "/MQTT_Connect_Count").c_str(), String(mqtt_connect_count).c_str(), true); // MQTT Connect Count
        mqttClient.loop();

        // Watch the 'command' topic for incoming MQTT messages
        mqttClient.subscribe((String(mqttBaseTopic) + "/command").c_str());
        mqttClient.loop();

        /*TODO:get reboot time properly*/
        #ifdef ESP8266
        mqttClient.publish((String(mqttBaseTopic) + "/reboot_time").c_str(), ESP.getResetInfo().c_str(), true);
        mqttClient.publish((String(mqttBaseTopic) + "/reboot_reason").c_str(), ESP.getResetReason().c_str(), true);
        mqttClient.publish((String(mqttBaseTopic) + "/button").c_str(), bwc.getButtonName().c_str(), true);
        mqttClient.loop();
        sendMQTT();
        setupHA();
        #endif
    }
    else
    {
        // Serial.print(F("failed, Return Code = "));
        // Serial.println(mqttClient.state()); // states explained in WebSocket.js
    }
}

void handleESPInfo()
{
    #ifdef ESP8266
    char stack;
    uint32_t stacksize = stack_start - &stack;
    size_t const BUFSIZE = 1024;
    char response[BUFSIZE];
    char const *response_template =
    "Stack size:          %u \n"
    "Free Heap:           %u \n"
    "Min  Heap:           %u \n"
    "Core version:        %s \n"
    "CPU fq:              %u MHz\n"
    "Cycle count:         %u \n"
    "Free cont stack:     %u \n"
    "Sketch size:         %u \n"
    "Free sketch space:   %u \n"
    "Max free block size: %u \n";

    snprintf(response, BUFSIZE, response_template,
        stacksize,
        ESP.getFreeHeap(),
        heap_water_mark,
        ESP.getCoreVersion(),
        ESP.getCpuFreqMHz(),
        ESP.getCycleCount(),
        ESP.getFreeContStack(),
        ESP.getSketchSize(),
        ESP.getFreeSketchSpace(),
        ESP.getMaxFreeBlockSize() );
        server.send(200, "text/plain; charset=utf-8", response);
    #endif
}

    // void setupHA(){}
    // void handlePrometheusMetrics(){}
    #include "ha.txt"
    #include "prometheus.txt"