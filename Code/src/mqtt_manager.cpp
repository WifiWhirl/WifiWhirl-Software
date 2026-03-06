#include "main.h"

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
 * load MQTT json configuration from "mqtt.json"
 */
void loadMqtt()
{
    File file = LittleFS.open("mqtt.json", "r");
    if (!file)
    {
        return;
    }

    StaticJsonDocument<512> doc;

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

    StaticJsonDocument<512> doc;
    String message = server->arg(0);
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

    useMqtt = doc[F("enableMqtt")];
    enableMqtt = useMqtt;

    if (doc.containsKey(F("mqttServer")))
    {
        mqttServer = doc[F("mqttServer")].as<String>();
    }

    mqttPort = doc[F("mqttPort")];
    mqttUsername = doc[F("mqttUsername")].as<String>();

    // Only update password if a new one is provided (not empty or placeholder)
    String newPassword = doc[F("mqttPassword")].as<String>();
    if (newPassword.length() > 0 && newPassword != "<Passwort eingeben>")
    {
        mqttPassword = newPassword;
    }
    else
    {
        // Load existing password from file to ensure we don't save empty password
        File file = LittleFS.open("mqtt.json", "r");
        if (file)
        {
            StaticJsonDocument<512> existingDoc;
            DeserializationError error = deserializeJson(existingDoc, file);
            file.close();
            if (!error && existingDoc.containsKey(F("mqttPassword")))
            {
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

    if (oldMqttServer != mqttServer)
    {
        haRelevantChanged = true;
        changeReason = "MQTT Server geändert";
        Serial.print("MQTT: Server changed: ");
        Serial.print(oldMqttServer);
        Serial.print(" -> ");
        Serial.println(mqttServer);
    }

    if (oldMqttPort != mqttPort)
    {
        haRelevantChanged = true;
        changeReason = "MQTT Port geändert";
        Serial.print("MQTT: Port changed: ");
        Serial.print(oldMqttPort);
        Serial.print(" -> ");
        Serial.println(mqttPort);
    }

    if (oldMqttUsername != mqttUsername)
    {
        haRelevantChanged = true;
        changeReason = "MQTT Benutzername geändert";
        Serial.println("MQTT: Username changed");
    }

    if (oldMqttPassword != mqttPassword)
    {
        haRelevantChanged = true;
        changeReason = "MQTT Passwort geändert";
        Serial.println("MQTT: Password changed");
    }

    if (oldMqttClientId != mqttClientId)
    {
        haRelevantChanged = true;
        changeReason = "MQTT Client ID geändert";
        Serial.print("MQTT: Client ID changed: ");
        Serial.print(oldMqttClientId);
        Serial.print(" -> ");
        Serial.println(mqttClientId);
    }

    if (oldMqttBaseTopic != mqttBaseTopic)
    {
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

    if (mqttBeingEnabled)
    {
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
    }
    else if (haRelevantChanged && oldUseMqtt && useMqtt)
    {
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
        if (mqttClient)
        {
            mqttClient->disconnect();
        }

        // Restart ESP - after restart, discovery will run once with new settings
        ESP.restart();
    }
    else if (mqttBeingDisabled)
    {
        // MQTT enabled -> disabled: No restart needed, stop MQTT
        Serial.println("MQTT: Disabling MQTT");
        Serial.println("MQTT: Settings saved");
        server->send(200, F("text/plain"), "");
        if (mqttClient)
        {
            mqttClient->disconnect();
        }
    }
    else if (haRelevantChanged && !useMqtt)
    {
        // MQTT disabled, settings changed: Save for future use
        Serial.println("MQTT: HA-relevant settings changed but MQTT is disabled");
        Serial.println("MQTT: Settings saved for future use");
        server->send(200, F("text/plain"), "");
    }
    else
    {
        // No HA-relevant changes, restart MQTT connection if enabled
        Serial.println("MQTT: Settings updated (no discovery required)");
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
    if (length == 0 || length > 1024)
        return;

    // Null-terminate payload safely: MQTT payloads are NOT null-terminated
    payload[length] = '\0';

    String topicStr(topic);

    if (topicStr.equals(String(mqttBaseTopic) + "/command"))
    {
        StaticJsonDocument<256> doc;
        DeserializationError error = deserializeJson(doc, (const char *)payload);
        if (error)
        {
            return;
        }

        bwc->add_command(parseCommandFromJson(doc));
    }

    if (topicStr.equals(String(mqttBaseTopic) + "/command_batch"))
    {
        DynamicJsonDocument doc(1024);
        DeserializationError error = deserializeJson(doc, (const char *)payload);
        if (error)
        {
            return;
        }

        JsonArray commandArray = doc.as<JsonArray>();
        for (JsonVariant commandItem : commandArray)
        {
            bwc->add_command(parseCommandFromJson(commandItem));
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
