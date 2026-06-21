#include "web/web.h"

/**
 * load "Web Config" json configuration from "webconfig.json"
 */
void loadWebConfig()
{
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
        Serial.println(F("FS: /webconfig.json not found, using defaults"));
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
        Serial.println(F("FS: Failed to open /webconfig.json for write"));
        return;
    }

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
    server->send(200, F("application/json"), json);
}

/**
 * response for /setwebconfig/
 * web server writes a json document
 */
void handleSetWebConfig()
{
    if (!checkHttpPost(server->method()))
        return;

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
