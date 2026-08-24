#include "api/api.h"
#include "web/web.h"

static bool readHardwareInt(const JsonVariantConst &value, int &out)
{
    if (value.is<int>())
    {
        out = value.as<int>();
        return true;
    }
    // _loadHardware (bwc.cpp) also accepts numeric-string values like "1", and
    // /gethardware/ round-trips the raw hwcfg.json. The setup wizard re-sends that
    // payload with dsp untouched, so a string-typed dsp must be accepted here too
    // or the setter rejects a config its own loader reads -> "Missing or invalid dsp".
    if (!value.is<const char *>() && !value.is<String>())
        return false;
    String text = value.as<String>();
    text.trim();
    if (text.length() == 0)
        return false;
    for (size_t i = 0; i < text.length(); i++)
        if (!isDigit(text[i]))
            return false;
    out = text.toInt();
    return true;
}

static bool validHardwareModel(int model)
{
    return model >= PRE2021 && model <= MSPA;
}

static const __FlashStringHelper *modelName(int model)
{
    switch (model)
    {
    case PRE2021:
        return F("PRE2021");
    case MIAMI2021:
        return F("MIAMI2021");
    case MALDIVES2021:
        return F("MALDIVES2021");
    case MSPA:
        return F("MSPA");
    default:
        return F("");
    }
}

static bool buildCanonicalHardwareConfig(const String &message, StaticJsonDocument<384> &canonical, String &error)
{
    StaticJsonDocument<384> doc;
    DeserializationError parseError = deserializeJson(doc, message);
    if (parseError || !doc.is<JsonObject>())
    {
        error = F("Error deserializing message");
        return false;
    }

    int cioModel;
    if (!readHardwareInt(doc[F("cio")], cioModel) || !validHardwareModel(cioModel))
    {
        error = F("Missing or invalid cio");
        return false;
    }

    int dspModel = cioModel;
    if (doc.containsKey(F("dsp")) &&
        (!readHardwareInt(doc[F("dsp")], dspModel) || !validHardwareModel(dspModel)))
    {
        error = F("Missing or invalid dsp");
        return false;
    }

    canonical.clear();
    canonical[F("cio")] = cioModel;
    canonical[F("dsp")] = (cioModel == MSPA) ? MSPA : dspModel;
    return true;
}

/**
 * response for /gethardware/
 * Serve the stored hardware configuration (hwcfg.json) from LittleFS
 */
void handleGetHardware()
{
    // if (!checkHttpPost(server->method()))
    //     return;
    File file = LittleFS.open("/hwcfg.json", "r");
    if (!file)
    {
        Serial.println(F("FS: Failed to open /hwcfg.json for read"));
        server->send(404, F("text/plain"), F("not found"));
        return;
    }
    server->send(200, F("text/plain"), file.readString());
    file.close();
}

/**
 * Read the currently stored cio/dsp models from hwcfg.json.
 * @return false when the file is missing or unreadable; the outputs keep their
 *         previous values so the caller can supply the firmware defaults.
 */
static bool storedHardwareConfig(int &cioModel, int &dspModel)
{
    File file = LittleFS.open("/hwcfg.json", "r");
    if (!file)
        return false;
    StaticJsonDocument<384> doc;
    DeserializationError parseError = deserializeJson(doc, file);
    file.close();
    if (parseError || !doc.is<JsonObject>())
        return false;
    if (!readHardwareInt(doc[F("cio")], cioModel))
        return false;
    if (!doc.containsKey(F("dsp")) || !readHardwareInt(doc[F("dsp")], dspModel))
        dspModel = cioModel;
    return true;
}

/**
 * response for /sethardware/
 * Persist hardware configuration to hwcfg.json; if the CIO or DSP model changed,
 * save settings and restart the ESP to reinitialize the new hardware
 */
void handleSetHardware()
{
    if (!checkHttpPost(server->method()))
        return;

    String message = getRequestBody();

    // The cio/dsp objects are created once in BWC::setup(), so any model change
    // needs a restart. Compare against the stored file, not just bwc->getModel():
    // a stale dsp keeps the old button decoder alive and the physical panel keys
    // end up mapped to the wrong functions (e.g. temp-down toggles °C/°F).
    int oldCio = MIAMI2021; // BWC::setup() falls back to this when hwcfg.json is unreadable
    int oldDsp = MIAMI2021;
    storedHardwareConfig(oldCio, oldDsp);

    StaticJsonDocument<384> canonical;
    String error;
    if (!buildCanonicalHardwareConfig(message, canonical, error))
    {
        server->send(400, F("text/plain"), error);
        return;
    }
    String newModel = modelName(canonical[F("cio")].as<int>());

    // Save hardware config
    File file = LittleFS.open("/hwcfg.json", "w");
    if (!file)
    {
        Serial.println(F("HW: Failed to save /hwcfg.json"));
        server->send(500, F("text/plain"), F("Error saving"));
        return;
    }
    if (serializeJson(canonical, file) == 0)
    {
        file.close();
        server->send(500, F("text/plain"), F("Error saving"));
        return;
    }
    file.close();

    // Check if model changed
    bool modelChanged = oldCio != canonical[F("cio")].as<int>() ||
                        oldDsp != canonical[F("dsp")].as<int>();

    if (modelChanged)
    {
        Serial.println(F("========================================"));
        Serial.printf("HW: Model changed: cio %d/dsp %d -> cio %d/dsp %d (%s)\n",
                      oldCio, oldDsp, canonical[F("cio")].as<int>(), canonical[F("dsp")].as<int>(),
                      newModel.c_str());
        Serial.println(F("HW: WifiWhirl restarting for hardware initialization..."));
        Serial.println(F("========================================"));
        restartWithReason("hw");
    }
    else
    {
        // No model change, just normal save
        Serial.println(F("HW: Hardware config saved (no restart needed)"));
        server->send(200, F("text/plain"), "ok");
    }
}
