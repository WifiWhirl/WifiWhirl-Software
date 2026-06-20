#include "api/api.h"
#include "web/web.h"

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
