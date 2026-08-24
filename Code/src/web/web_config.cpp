#include "web/web.h"

static bool readOptionalWebConfigBool(const JsonObjectConst &obj, const __FlashStringHelper *key, bool &target, String &error)
{
    if (!obj.containsKey(key))
        return true;
    JsonVariantConst value = obj[key];
    if (!value.is<bool>())
    {
        error = String(F("Missing or invalid ")) + String(key);
        return false;
    }
    target = value.as<bool>();
    return true;
}

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
        return;
    }

    if (!doc.is<JsonObject>())
    {
        Serial.println(F("FS: Invalid /webconfig.json root, using defaults"));
        return;
    }

    bool nextShowSectionTemperature = showSectionTemperature;
    bool nextShowSectionDisplay = showSectionDisplay;
    bool nextShowSectionControl = showSectionControl;
    bool nextShowSectionButtons = showSectionButtons;
    bool nextShowSectionTimer = showSectionTimer;
    bool nextShowSectionTotals = showSectionTotals;
    bool nextShowSectionEnergy = showSectionEnergy;
    bool nextShowSectionWaterQuality = showSectionWaterQuality;
    bool nextShowWQCyanuric = showWQCyanuric;
    bool nextShowWQAlkalinity = showWQAlkalinity;

    JsonObjectConst obj = doc.as<JsonObjectConst>();
    String validationError;
    if (!readOptionalWebConfigBool(obj, F("SST"), nextShowSectionTemperature, validationError) ||
        !readOptionalWebConfigBool(obj, F("SSD"), nextShowSectionDisplay, validationError) ||
        !readOptionalWebConfigBool(obj, F("SSC"), nextShowSectionControl, validationError) ||
        !readOptionalWebConfigBool(obj, F("SSB"), nextShowSectionButtons, validationError) ||
        !readOptionalWebConfigBool(obj, F("SSTIM"), nextShowSectionTimer, validationError) ||
        !readOptionalWebConfigBool(obj, F("SSTOT"), nextShowSectionTotals, validationError) ||
        !readOptionalWebConfigBool(obj, F("SSEN"), nextShowSectionEnergy, validationError) ||
        !readOptionalWebConfigBool(obj, F("SSWQ"), nextShowSectionWaterQuality, validationError) ||
        !readOptionalWebConfigBool(obj, F("SWQCYA"), nextShowWQCyanuric, validationError) ||
        !readOptionalWebConfigBool(obj, F("SWQALK"), nextShowWQAlkalinity, validationError))
    {
        Serial.print(F("FS: Invalid /webconfig.json: "));
        Serial.println(validationError);
        return;
    }

    showSectionTemperature = nextShowSectionTemperature;
    showSectionDisplay = nextShowSectionDisplay;
    showSectionControl = nextShowSectionControl;
    showSectionButtons = nextShowSectionButtons;
    showSectionTimer = nextShowSectionTimer;
    showSectionTotals = nextShowSectionTotals;
    showSectionEnergy = nextShowSectionEnergy;
    showSectionWaterQuality = nextShowSectionWaterQuality;
    showWQCyanuric = nextShowWQCyanuric;
    showWQAlkalinity = nextShowWQAlkalinity;
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
    String message = getRequestBody();
    DeserializationError error = deserializeJson(doc, message);
    if (error)
    {
        // Serial.println(F("Failed to read config file"));
        server->send(400, F("text/plain"), F("Error deserializing message"));
        return;
    }
    if (!doc.is<JsonObject>())
    {
        server->send(400, F("text/plain"), F("Web config payload must be an object"));
        return;
    }

    JsonObjectConst obj = doc.as<JsonObjectConst>();
    String validationError;
    if (!readOptionalWebConfigBool(obj, F("SST"), showSectionTemperature, validationError) ||
        !readOptionalWebConfigBool(obj, F("SSD"), showSectionDisplay, validationError) ||
        !readOptionalWebConfigBool(obj, F("SSC"), showSectionControl, validationError) ||
        !readOptionalWebConfigBool(obj, F("SSB"), showSectionButtons, validationError) ||
        !readOptionalWebConfigBool(obj, F("SSTIM"), showSectionTimer, validationError) ||
        !readOptionalWebConfigBool(obj, F("SSTOT"), showSectionTotals, validationError) ||
        !readOptionalWebConfigBool(obj, F("SSEN"), showSectionEnergy, validationError) ||
        !readOptionalWebConfigBool(obj, F("SSWQ"), showSectionWaterQuality, validationError) ||
        !readOptionalWebConfigBool(obj, F("SWQCYA"), showWQCyanuric, validationError) ||
        !readOptionalWebConfigBool(obj, F("SWQALK"), showWQAlkalinity, validationError))
    {
        server->send(400, F("text/plain"), validationError);
        return;
    }

    saveWebConfig();

    server->send(200, F("text/plain"), "");
}
