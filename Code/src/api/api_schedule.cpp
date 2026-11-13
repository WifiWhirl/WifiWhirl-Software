#include "api/api.h"
#include "web/web.h"

static const uint64_t MIN_VALID_TIME_SECONDS = 57600ULL;
static const int64_t MAX_SCHEDULE_EPOCH_SECONDS = 4102444800LL; // 2100-01-01

static bool readRequiredScheduleInteger(const JsonObjectConst &obj, const __FlashStringHelper *key,
                                        int64_t minValue, int64_t maxValue, int64_t &out, String &error)
{
    if (!obj.containsKey(key) || obj[key].is<const char *>() || obj[key].is<String>() ||
        obj[key].is<bool>() || !obj[key].is<int64_t>())
    {
        error = String(F("Missing or invalid ")) + String(key);
        return false;
    }
    int64_t parsed = obj[key].as<int64_t>();
    if (parsed < minValue || parsed > maxValue)
    {
        error = String(key) + F(" out of range");
        return false;
    }
    out = parsed;
    return true;
}

static bool readRequiredScheduleBool(const JsonObjectConst &obj, const __FlashStringHelper *key,
                                     bool &out, String &error)
{
    if (!obj.containsKey(key) || !obj[key].is<bool>())
    {
        error = String(F("Missing or invalid ")) + String(key);
        return false;
    }
    out = obj[key].as<bool>();
    return true;
}

static bool validateScheduleTargetTime(int64_t targetTimeValue, String &error)
{
    time_t now = time(nullptr);
    if (now < (time_t)MIN_VALID_TIME_SECONDS)
    {
        error = F("NTP not synced");
        return false;
    }
    if (targetTimeValue <= (int64_t)now)
    {
        error = F("TARGETTIME must be in the future");
        return false;
    }
    return true;
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
 * Validate a smart schedule object and set it. Shared by /setsmartschedule/ and
 * the webhook (/hook/?schedule=...), so both accept the same fields:
 * TARGETTIME (epoch), TARGETTEMP (20-40), KEEPON, optional KEEPPUMP and REPEAT.
 * Returns an empty string on success, else the reason.
 */
String setSmartScheduleFromJson(JsonObjectConst obj)
{
    String validationError;
    int64_t targetTimeValue;
    int64_t targetTempValue;
    bool keep_heater_on;
    bool keep_pump_on = false; // optional
    int64_t repeatValue = 0; // optional: 0 = one-shot, else repeat every N days
    if (!readRequiredScheduleInteger(obj, F("TARGETTIME"), 1, MAX_SCHEDULE_EPOCH_SECONDS, targetTimeValue, validationError) ||
        !readRequiredScheduleInteger(obj, F("TARGETTEMP"), 20, 40, targetTempValue, validationError) ||
        !readRequiredScheduleBool(obj, F("KEEPON"), keep_heater_on, validationError) ||
        (obj.containsKey(F("KEEPPUMP")) && !readRequiredScheduleBool(obj, F("KEEPPUMP"), keep_pump_on, validationError)) ||
        (obj.containsKey(F("REPEAT")) &&
         !readRequiredScheduleInteger(obj, F("REPEAT"), 0, 30, repeatValue, validationError)) ||
        !validateScheduleTargetTime(targetTimeValue, validationError))
        return validationError;
    if (!bwc->setSmartSchedule((uint64_t)targetTimeValue, (uint8_t)targetTempValue, keep_heater_on, (uint8_t)repeatValue, keep_pump_on))
        return F("Invalid schedule parameters or NTP not synced");
    return String();
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
    const String &message = getRequestBody();
    DeserializationError error = deserializeJson(doc, message);
    if (error)
    {
        server->send(400, F("text/plain"), F("Error deserializing message"));
        return;
    }

    if (!doc.is<JsonObject>())
    {
        server->send(400, F("text/plain"), F("Schedule payload must be an object"));
        return;
    }

    String err = setSmartScheduleFromJson(doc.as<JsonObjectConst>());
    if (err.length())
        server->send(400, F("text/plain"), err);
    else
        server->send(200, F("text/plain"), F("Schedule set successfully"));
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
    const String &message = getRequestBody();
    DeserializationError error = deserializeJson(doc, message);
    if (error)
    {
        server->send(400, F("text/plain"), F("Error deserializing message"));
        return;
    }

    if (!doc.is<JsonObject>())
    {
        server->send(400, F("text/plain"), F("Schedule payload must be an object"));
        return;
    }

    JsonObjectConst obj = doc.as<JsonObjectConst>();
    String validationError;
    bool keep_heater_on;
    bool keep_pump_on = false;
    int64_t repeatValue = -1; // optional: only applied when present
    if (!readRequiredScheduleBool(obj, F("KEEPON"), keep_heater_on, validationError) ||
        (obj.containsKey(F("KEEPPUMP")) && !readRequiredScheduleBool(obj, F("KEEPPUMP"), keep_pump_on, validationError)) ||
        (obj.containsKey(F("REPEAT")) &&
         !readRequiredScheduleInteger(obj, F("REPEAT"), 0, 30, repeatValue, validationError)))
    {
        server->send(400, F("text/plain"), validationError);
        return;
    }
    if (repeatValue >= 0)
        bwc->updateSmartScheduleRepeat((uint8_t)repeatValue);
    if (obj.containsKey(F("KEEPPUMP")))
        bwc->updateSmartScheduleKeepPumpOn(keep_pump_on);
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
