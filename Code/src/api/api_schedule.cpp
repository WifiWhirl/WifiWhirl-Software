#include "main.h"

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
