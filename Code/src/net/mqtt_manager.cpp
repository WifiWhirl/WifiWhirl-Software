#include "net/net.h"
#include "api/api.h"
#include "web/web.h"

static String mqttTopicMessage;
static String mqttTopicTimes;
static String mqttTopicOther;
static String mqttTopicStatus;
static String mqttTopicMac;
static String mqttTopicConnCount;
static String mqttTopicCommand;
static String mqttTopicCommandBatch;
static String mqttTopicRebootTime;
static String mqttTopicRebootReason;
static String mqttTopicButton;

static bool isPrintableNoControl(const String &value, size_t maxLen, bool allowEmpty)
{
    if (value.length() == 0)
        return allowEmpty;
    if (value.length() > maxLen)
        return false;
    for (size_t i = 0; i < value.length(); i++)
    {
        if ((uint8_t)value[i] < 0x20 || value[i] == 0x7f)
            return false;
    }
    return true;
}

static bool isValidMqttBaseTopic(const String &topic)
{
    if (topic.length() == 0 || topic.length() > 128 ||
        topic[0] == '/' || topic[topic.length() - 1] == '/')
        return false;

    for (size_t i = 0; i < topic.length(); i++)
    {
        char c = topic[i];
        bool ok = (c >= 'A' && c <= 'Z') ||
                  (c >= 'a' && c <= 'z') ||
                  (c >= '0' && c <= '9') ||
                  c == '-' || c == '_' || c == '/' || c == '.';
        if (!ok || c == '+' || c == '#')
            return false;
    }
    return true;
}

static bool validateMqttSettings(bool wantMqtt, const String &serverName, int port,
                                 const String &username, const String &password,
                                 const String &clientId, const String &baseTopic,
                                 int telemetryInterval, String &error)
{
    if (port < 1 || port > 65535)
    {
        error = F("Invalid MQTT port");
        return false;
    }
    if (telemetryInterval < 10 || telemetryInterval > 86400)
    {
        error = F("Invalid MQTT telemetry interval");
        return false;
    }
    if (!isPrintableNoControl(username, 128, true) ||
        !isPrintableNoControl(password, 128, true))
    {
        error = F("Invalid MQTT credentials");
        return false;
    }
    if (!isPrintableNoControl(clientId, 64, !wantMqtt))
    {
        error = F("Invalid MQTT client ID");
        return false;
    }
    if (!isValidMqttBaseTopic(baseTopic))
    {
        error = F("Invalid MQTT base topic");
        return false;
    }
    if (wantMqtt && !isValidHostnameOrIp(serverName, false))
    {
        error = F("Invalid MQTT server");
        return false;
    }
    if (!wantMqtt && serverName.length() > 0 && !isValidHostnameOrIp(serverName, false))
    {
        error = F("Invalid MQTT server");
        return false;
    }
    return true;
}

/**
 * Accessor for the MQTT button topic (used by the main loop)
 * @return reference to the cached button topic string
 */
const String& getMqttTopicButton() { return mqttTopicButton; }

/**
 * Build all MQTT topic strings from the configured base topic
 * Must be called after the base topic is loaded/changed
 */
void initMqttTopics()
{
    mqttTopicMessage      = mqttBaseTopic + "/message";
    mqttTopicTimes        = mqttBaseTopic + "/times";
    mqttTopicOther        = mqttBaseTopic + "/other";
    mqttTopicStatus       = mqttBaseTopic + "/Status";
    mqttTopicMac          = mqttBaseTopic + "/MAC_Address";
    mqttTopicConnCount    = mqttBaseTopic + "/MQTT_Connect_Count";
    mqttTopicCommand      = mqttBaseTopic + "/command";
    mqttTopicCommandBatch = mqttBaseTopic + "/command_batch";
    mqttTopicRebootTime   = mqttBaseTopic + "/reboot_time";
    mqttTopicRebootReason = mqttBaseTopic + "/reboot_reason";
    mqttTopicButton       = mqttBaseTopic + "/button";
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
    json.reserve(320);

    bwc->getJSONStates(json);
    mqttClient->publish(mqttTopicMessage.c_str(), json.c_str(), true);

    json.clear();
    bwc->getJSONTimes(json);
    mqttClient->publish(mqttTopicTimes.c_str(), json.c_str(), true);

    json.clear();
    getOtherInfo(json);
    mqttClient->publish(mqttTopicOther.c_str(), json.c_str(), true);
}

/**
 * load MQTT json configuration from "mqtt.json"
 */
void loadMqtt()
{
    File file = LittleFS.open("/mqtt.json", "r");
    if (!file)
    {
        Serial.println(F("FS: /mqtt.json not found, using defaults"));
        return;
    }

    StaticJsonDocument<512> doc;

    DeserializationError error = deserializeJson(doc, file);
    file.close();
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

    // Validation only runs on the HTTP save path, so a corrupt/old/hand-edited
    // file could carry a port of 0 (broken connection) or interval of 0 (publish
    // storm). Clamp the numeric fields to safe defaults on load too.
    mqttPort = doc[F("mqttPort")] | 1883;
    if (mqttPort < 1 || mqttPort > 65535)
        mqttPort = 1883;
    mqttUsername = doc[F("mqttUsername")].as<String>();
    mqttPassword = doc[F("mqttPassword")].as<String>();
    mqttClientId = doc[F("mqttClientId")].as<String>();
    mqttBaseTopic = doc[F("mqttBaseTopic")].as<String>();
    mqttTelemetryInterval = doc[F("mqttTelemetryInterval")] | 600;
    if (mqttTelemetryInterval < 10 || mqttTelemetryInterval > 86400)
        mqttTelemetryInterval = 600;

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
    File file = LittleFS.open("/mqtt.json", "w");
    if (!file)
    {
        Serial.println(F("FS: Failed to open /mqtt.json for write"));
        return;
    }

    StaticJsonDocument<512> doc;

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

    StaticJsonDocument<512> doc;

    doc[F("enableMqtt")] = useMqtt;
    doc[F("mqttServer")] = mqttServer;
    doc[F("mqttPort")] = mqttPort;
    doc[F("mqttUsername")] = mqttUsername;
    // Never sent to the client: omitted rather than masked with a sentinel,
    // so there is no magic string the UI has to recognise (and no UI text in
    // the firmware, which could only ever be in one language).
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
    server->send(200, F("application/json"), json);
}

/**
 * response for /setmqtt/
 * web server writes a json document
 */
void handleSetMqtt()
{
    if (!checkHttpPost(server->method()))
        return;

    StaticJsonDocument<512> doc;
    String message = getRequestBody();
    DeserializationError error = deserializeJson(doc, message);
    if (error)
    {
        server->send(400, F("text/plain"), F("Error deserializing message"));
        return;
    }

    // Store old values to detect changes that require HA discovery re-run
    bool oldUseMqtt = useMqtt; // Store old MQTT enabled state
    String oldMqttServer = mqttServer;
    int oldMqttPort = mqttPort;
    String oldMqttUsername = mqttUsername;
    String oldMqttPassword = mqttPassword;
    String oldMqttClientId = mqttClientId;
    String oldMqttBaseTopic = mqttBaseTopic;

    // Each field is updated only when present, so partial saves keep the rest.
    bool nextUseMqtt = useMqtt;
    String nextMqttServer = mqttServer;
    int nextMqttPort = mqttPort;
    String nextMqttUsername = mqttUsername;
    String nextMqttPassword = mqttPassword;
    String nextMqttClientId = mqttClientId;
    String nextMqttBaseTopic = mqttBaseTopic;
    int nextMqttTelemetryInterval = mqttTelemetryInterval;

    if (doc.containsKey(F("enableMqtt")))
    {
        if (!doc[F("enableMqtt")].is<bool>())
        {
            server->send(400, F("text/plain"), F("Invalid MQTT enable flag"));
            return;
        }
        nextUseMqtt = doc[F("enableMqtt")];
    }

    if (doc.containsKey(F("mqttServer")))
        nextMqttServer = doc[F("mqttServer")].as<String>();

    if (doc.containsKey(F("mqttPort")))
    {
        if (!doc[F("mqttPort")].is<int>())
        {
            server->send(400, F("text/plain"), F("Invalid MQTT port"));
            return;
        }
        nextMqttPort = doc[F("mqttPort")].as<int>();
    }

    if (doc.containsKey(F("mqttUsername")))
        nextMqttUsername = doc[F("mqttUsername")].as<String>();

    // Only update when a real one is sent: the UI omits the key when the field
    // is left blank, so an untouched form keeps the stored secret.
    if (doc.containsKey(F("mqttPassword")) &&
        doc[F("mqttPassword")].as<String>().length() > 0)
    {
        nextMqttPassword = doc[F("mqttPassword")].as<String>();
    }

    if (doc.containsKey(F("mqttClientId")))
        nextMqttClientId = doc[F("mqttClientId")].as<String>();

    if (doc.containsKey(F("mqttBaseTopic")))
        nextMqttBaseTopic = doc[F("mqttBaseTopic")].as<String>();

    if (doc.containsKey(F("mqttTelemetryInterval")))
    {
        if (!doc[F("mqttTelemetryInterval")].is<int>())
        {
            server->send(400, F("text/plain"), F("Invalid MQTT telemetry interval"));
            return;
        }
        nextMqttTelemetryInterval = doc[F("mqttTelemetryInterval")].as<int>();
    }

    String validationError;
    if (!validateMqttSettings(nextUseMqtt, nextMqttServer, nextMqttPort,
                              nextMqttUsername, nextMqttPassword, nextMqttClientId,
                              nextMqttBaseTopic, nextMqttTelemetryInterval, validationError))
    {
        server->send(400, F("text/plain"), validationError);
        return;
    }

    useMqtt = nextUseMqtt;
    enableMqtt = useMqtt;
    mqttServer = nextMqttServer;
    mqttPort = nextMqttPort;
    mqttUsername = nextMqttUsername;
    mqttPassword = nextMqttPassword;
    mqttClientId = nextMqttClientId;
    mqttBaseTopic = nextMqttBaseTopic;
    mqttTelemetryInterval = nextMqttTelemetryInterval;

    // These settings affect how entities are registered in Home Assistant
    bool haRelevantChanged = false;
    String changeReason = "";

    if (oldMqttServer != mqttServer)
    {
        haRelevantChanged = true;
        changeReason = "MQTT Server geändert";
        Serial.print(F("MQTT: Server changed: "));
        Serial.print(oldMqttServer);
        Serial.print(F(" -> "));
        Serial.println(mqttServer);
    }

    if (oldMqttPort != mqttPort)
    {
        haRelevantChanged = true;
        changeReason = "MQTT Port geändert";
        Serial.print(F("MQTT: Port changed: "));
        Serial.print(oldMqttPort);
        Serial.print(F(" -> "));
        Serial.println(mqttPort);
    }

    if (oldMqttUsername != mqttUsername)
    {
        haRelevantChanged = true;
        changeReason = "MQTT Benutzername geändert";
        Serial.println(F("MQTT: Username changed"));
    }

    if (oldMqttPassword != mqttPassword)
    {
        haRelevantChanged = true;
        changeReason = "MQTT Passwort geändert";
        Serial.println(F("MQTT: Password changed"));
    }

    if (oldMqttClientId != mqttClientId)
    {
        haRelevantChanged = true;
        changeReason = "MQTT Client ID geändert";
        Serial.print(F("MQTT: Client ID changed: "));
        Serial.print(oldMqttClientId);
        Serial.print(F(" -> "));
        Serial.println(mqttClientId);
    }

    if (oldMqttBaseTopic != mqttBaseTopic)
    {
        haRelevantChanged = true;
        changeReason = "MQTT Base Topic geändert";
        Serial.print(F("MQTT: Base topic changed: "));
        Serial.print(oldMqttBaseTopic);
        Serial.print(F(" -> "));
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

    if (mqttBeingEnabled)
    {
        // MQTT disabled -> enabled: Restart for clean discovery
        Serial.println(F("========================================"));
        Serial.println(F("MQTT: Enabling MQTT"));
        Serial.println(F("MQTT: WifiWhirl restarting for clean initialization..."));
        Serial.println(F("========================================"));
        restartWithReason("mqtt");
    }
    else if (haRelevantChanged && oldUseMqtt && useMqtt)
    {
        // MQTT was enabled and still is, settings changed -> restart for re-discovery
        Serial.println(F("========================================"));
        Serial.println(F("MQTT: HA-relevant settings changed!"));
        Serial.print(F("MQTT: Reason: "));
        Serial.println(changeReason);
        Serial.println(F("MQTT: Restarting to re-run discovery..."));
        Serial.println(F("========================================"));
        restartWithReason("mqtt"); // changeReason is logged above for diagnostics
    }
    else if (mqttBeingDisabled)
    {
        // MQTT enabled -> disabled: No restart needed, stop MQTT
        Serial.println(F("MQTT: Disabling MQTT"));
        Serial.println(F("MQTT: Settings saved"));
        server->send(200, F("text/plain"), "");
        if (mqttClient)
        {
            mqttClient->disconnect();
        }
    }
    else if (haRelevantChanged && !useMqtt)
    {
        // MQTT disabled, settings changed: Save for future use
        Serial.println(F("MQTT: HA-relevant settings changed but MQTT is disabled"));
        Serial.println(F("MQTT: Settings saved for future use"));
        server->send(200, F("text/plain"), "");
    }
    else
    {
        // No HA-relevant changes, restart MQTT connection if enabled
        Serial.println(F("MQTT: Settings updated (no discovery required)"));
        server->send(200, F("text/plain"), "");
        if (useMqtt)
        {
            startMqtt();
        }
    }
}

/**
 * MQTT setup and connect
 * @author 877dev
 */
void startMqtt()
{
    {
#ifdef ESP8266
        HeapSelectIram ephemeral; // park the next allocations in IRAM heap (ESP8266 only)
#endif
        Serial.printf("IRamheap %d\n", ESP.getFreeHeap());
        Serial.println(F("startmqtt"));
        if (!aWifiClient)
            aWifiClient = new WiFiClient;
        if (!mqttClient)
            mqttClient = new PubSubClient(*aWifiClient);
    }

    // load mqtt credential file if it exists, and update default strings
    loadMqtt();
    initMqttTopics();

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
    if (length == 0 || length > 1024)
        return;

    // Do NOT null-terminate in place: `payload` points into PubSubClient's heap
    // buffer (setBufferSize(768)), so payload[length] can write one byte past the
    // allocation on a buffer-filling message (e.g. a large batch command) and
    // corrupt the heap. ArduinoJson parses from a pointer+length, so no
    // terminator is needed.
    String topicStr(topic);

    if (topicStr.equals(mqttTopicCommand))
    {
        StaticJsonDocument<256> doc;
        DeserializationError error = deserializeJson(doc, payload, length);
        if (error)
        {
            return;
        }

        command_que_item item;
        String parseError;
        if (parseCommandValidated(doc.as<JsonVariantConst>(), item, parseError))
            bwc->add_command(item);
    }

    if (topicStr.equals(mqttTopicCommandBatch))
    {
        DynamicJsonDocument doc(1024);
        DeserializationError error = deserializeJson(doc, payload, length);
        if (error)
        {
            return;
        }

        JsonArray commandArray = doc.as<JsonArray>();
        for (JsonVariant commandItem : commandArray)
        {
            command_que_item item;
            String parseError;
            if (parseCommandValidated(commandItem, item, parseError))
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
    Serial.println(F("mqttconn"));

    // Serial.print(F("MQTT > Connecting ... "));
    // We'll connect with a Retained Last Will that updates the 'Status' topic with "Dead" when the device goes offline...
    if (mqttClient->connect(
            mqttClientId.c_str(),
            mqttUsername.c_str(),
            mqttPassword.c_str(),
            mqttTopicStatus.c_str(),
            0,
            1,
            "Dead"))
    {
        mqtt_connect_count++;

        updateMqttTimer.attach(mqttTelemetryInterval, []
                               { sendMQTTFlag = true; });

        mqttClient->publish(mqttTopicStatus.c_str(), "Alive", true);
        mqttClient->publish(mqttTopicMac.c_str(), WiFi.macAddress().c_str(), true);
        mqttClient->publish(mqttTopicConnCount.c_str(), String(mqtt_connect_count).c_str(), true);
        mqttClient->loop();

        mqttClient->subscribe(mqttTopicCommand.c_str());
        mqttClient->subscribe(mqttTopicCommandBatch.c_str());
        mqttClient->loop();

        mqttClient->publish(mqttTopicRebootTime.c_str(), (bwc->reboot_time_str + 'Z').c_str(), true);
#ifdef ESP8266
        mqttClient->publish(mqttTopicRebootReason.c_str(), ESP.getResetReason().c_str(), true);
#else
        mqttClient->publish(mqttTopicRebootReason.c_str(), String((int)esp_reset_reason()).c_str(), true);
#endif
        String buttonname;
        buttonname.reserve(32);
        bwc->getButtonName(buttonname);
        mqttClient->publish(mqttTopicButton.c_str(), buttonname.c_str(), true);
        mqttClient->loop();
        sendMQTT();

        // Only run HA discovery on FIRST connection after boot
        // NOT on every reconnect (would waste memory and cause instability)
        if (!haDiscoveryHasRunOnce)
        {
            Serial.println(F("HA"));
            setupHA();
            Serial.println(F("done"));
            haDiscoveryHasRunOnce = true;
        }
        else
        {
            Serial.println(F("HA: Skipping discovery (already ran after boot)"));
        }
    }
    else
    {
        // Serial.print(F("failed, Return Code = "));
        // Serial.println(mqttClient->state()); // states explained in webSocket->js
    }
    Serial.println(F("end mqttcon"));
}
