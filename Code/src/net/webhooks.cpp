#include "net/net.h"
#include "api/api.h"
#include "web/web.h"
#include "util.h"

/**
 * response for /hook/
 * web server adds command to cmdq "Webhook"
 * Examples:
 * Pump on: http://[ipaddress]/hook/?send={"CMD":4,"VALUE":true}
 * Pump off: http://[ipaddress]/hook/?send={"CMD":4,"VALUE":false}
 * Heating on: http://[ipaddress]/hook/?send={"CMD":3,"VALUE":true}
 * Heating off: http://[ipaddress]/hook/?send={"CMD":3,"VALUE":false}
 * Smart Schedule: http://[ipaddress]/hook/?schedule={"TARGETTIME":1767283200,"TARGETTEMP":38,"KEEPPUMP":true}
 * Cancel it:      http://[ipaddress]/hook/?schedule=cancel
 * 
 * See https://wifiwhirl.de/Integrationen/Webhooks/ for details
 */

bool checkWebhookAuth()
{
    if (!webhookAuthEnabled)
        return true;
    if (webhookAuthUser.length() == 0 || webhookAuthPassword.length() == 0 ||
        !server->authenticate(webhookAuthUser.c_str(), webhookAuthPassword.c_str()))
    {
        server->requestAuthentication();
        return false;
    }
    return true;
}

void handleWebhook()
{
    if (!checkHttpGet(server->method()))
        return;

    if (!webhookEnabled)
    {
        server->send(404, F("text/plain"), F("Webhooks disabled."));
        return;
    }

    if (!checkWebhookAuth())
        return;

    // Smart Schedule: same fields as /setsmartschedule/, KEEPON optional here
    if (server->hasArg(F("schedule")))
    {
        String arg = server->arg(F("schedule"));
        if (arg == F("cancel"))
        {
            bwc->cancelSmartSchedule();
            server->send(200, F("text/plain"), F("Schedule cancelled"));
            return;
        }
        StaticJsonDocument<256> sdoc;
        if (deserializeJson(sdoc, arg) || !sdoc.is<JsonObject>())
        {
            server->send(400, F("text/plain"), F("Schedule must be a JSON object or \"cancel\""));
            return;
        }
        if (!sdoc.containsKey(F("KEEPON")))
            sdoc[F("KEEPON")] = false;
        String err = setSmartScheduleFromJson(sdoc.as<JsonObjectConst>());
        if (err.length())
            server->send(400, F("text/plain"), err);
        else
            server->send(200, F("text/plain"), F("Schedule set"));
        return;
    }

    StaticJsonDocument<256> doc;
    String message = server->arg("send");
    DeserializationError error = deserializeJson(doc, message);
    if (error)
    {
        server->send(400, F("text/plain"), F("Error deserializing message"));
        return;
    }

    command_que_item item;
    String parseError;
    if (!parseCommandValidated(doc.as<JsonVariantConst>(), item, parseError))
    {
        server->send(400, F("text/plain"), parseError);
        return;
    }
    if (!bwc->add_live_command(item))
    {
        // same answer as /sendcommand/: a running heat-loss calibration drops
        // live commands, the queue isn't actually full
        if (bwc->calibrating())
            server->send(423, F("text/plain"), F("Calibration running"));
        else
            server->send(409, F("text/plain"), F("Queue full (max. 20 commands)"));
        return;
    }

    server->send(200, F("text/plain"), String(item.cmd) + " " + String(item.val) + " " + String(item.xtime));
}

/**
 * response for /getstates/
 * Returns a JSON document with the current ON/OFF states of main components
 * Example response:
 * {"pump":true,"heater":false,"bubbles":false,"jets":true,"power":true,"lock":false}
 */

void handleGetStates()
{
    if (!checkHttpGet(server->method()))
        return;

    StaticJsonDocument<256> doc;

    doc[F("pump")] = static_cast<bool>(bwc->cio->cio_states.pump);
    doc[F("heater")] = static_cast<bool>(bwc->cio->cio_states.heatred || bwc->cio->cio_states.heatgrn);
    doc[F("bubbles")] = static_cast<bool>(bwc->cio->cio_states.bubbles);
    doc[F("jets")] = static_cast<bool>(bwc->cio->cio_states.jets);
    doc[F("power")] = static_cast<bool>(bwc->cio->cio_states.power);
    doc[F("lock")] = static_cast<bool>(bwc->cio->cio_states.locked);

    String json;
    serializeJson(doc, json);

    server->send(200, F("application/json"), json);
}

/**
 * response for /gettemps/
 * Returns a JSON document with temperature values in both Celsius and Fahrenheit
 * 
 * Query Parameters (optional field filtering):
 *   No params: Returns all fields
 *   With params: Returns only requested fields
 * 
 * Examples:
 *   /gettemps/                     -> Full response with all fields
 *   /gettemps/?currentC            -> {"currentC":38}
 *   /gettemps/?currentC&targetC    -> {"currentC":38,"targetC":40}
 *   /gettemps/?currentC&currentF   -> {"currentC":38,"currentF":100}
 * 
 * Available fields: currentC, currentF, targetC, targetF, ambientC, ambientF, unit
 */

void handleGetTemps()
{
    if (!checkHttpGet(server->method()))
        return;

    StaticJsonDocument<384> doc;

    bool filteringActive = server->args() > 0;

    auto isFieldRequested = [filteringActive](const String &fieldName) -> bool {
        if (!filteringActive)
            return true;
        return server->hasArg(fieldName);
    };

    if (isFieldRequested(F("currentC")))
    {
        doc[F("currentC")] = bwc->cio->cio_states.unit ? bwc->cio->cio_states.temperature : round(F2C((float)bwc->cio->cio_states.temperature));
    }

    if (isFieldRequested(F("currentF")))
    {
        doc[F("currentF")] = bwc->cio->cio_states.unit ? round(C2F((float)bwc->cio->cio_states.temperature)) : bwc->cio->cio_states.temperature;
    }

    if (isFieldRequested(F("targetC")))
    {
        doc[F("targetC")] = bwc->cio->cio_states.unit ? bwc->cio->cio_states.target : round(F2C((float)bwc->cio->cio_states.target));
    }

    if (isFieldRequested(F("targetF")))
    {
        doc[F("targetF")] = bwc->cio->cio_states.unit ? round(C2F((float)bwc->cio->cio_states.target)) : bwc->cio->cio_states.target;
    }

    if (isFieldRequested(F("ambientC")))
    {
        doc[F("ambientC")] = bwc->getAmbientTemperature();
    }

    if (isFieldRequested(F("ambientF")))
    {
        doc[F("ambientF")] = round(C2F(bwc->getAmbientTemperature()));
    }

    if (isFieldRequested(F("unit")))
    {
        doc[F("unit")] = bwc->cio->cio_states.unit ? F("C") : F("F");
    }

    String json;
    serializeJson(doc, json);

    server->send(200, F("application/json"), json);
}
