#include "api/api.h"
#include "web/web.h"
#include "config.h"

// These endpoints physically toggle the heater/pump or change the heat-loss
// coefficient, so they must be gated on expert mode just like the wattage path
// in handleSetDevice - auth alone isn't enough (the UI only hides the controls).
static bool requireExpertMode()
{
    if (expertMode)
        return true;
    // body is a code the UI localizes as expert.calErr.<body>
    server->send(403, F("text/plain"), F("expert"));
    return false;
}

/**
 * response for /gethlcal/
 * Heat-loss calibration status + current/default coefficient as JSON.
 */
void handleGetHlCal()
{
    if (!checkHttpPost(server->method()))
        return;

    String json;
    json.reserve(512);
    bwc->getJSONHeatLossCalibration(json);
    server->send(200, F("application/json"), json);
}

/**
 * response for /starthlcal/
 * Begin a heat-loss measurement. 400 + message if a precondition isn't met
 * (heater/AirJet on, pump off, or too small a water/ambient difference).
 */
void handleStartHlCal()
{
    if (!checkHttpPost(server->method()))
        return;
    if (!requireExpertMode())
        return;

    // {"AT": epoch} plans a single start instead
    StaticJsonDocument<64> doc;
    if (!deserializeJson(doc, getRequestBody()) && doc.containsKey(F("AT")))
    {
        if (bwc->planHeatLossCalibration(doc[F("AT")].as<uint64_t>()))
            server->send(200, F("application/json"), F("{\"planned\":true}"));
        else
            server->send(400, F("text/plain"), F("plan"));
        return;
    }
    String err;
    if (bwc->startHeatLossCalibration(err))
        server->send(200, F("application/json"), F("{\"started\":true}"));
    else
        server->send(400, F("text/plain"), err);
}

/**
 * response for /cancelhlcal/
 */
void handleCancelHlCal()
{
    if (!checkHttpPost(server->method()))
        return;
    // No expert-mode check: the dashboard offers "cancel" to anyone who sees
    // the tub locked by a running calibration. Starting stays expert.

    bwc->cancelHeatLossCalibration();
    server->send(200, F("application/json"), F("{\"cancelled\":true}"));
}

/**
 * response for /setheatloss/
 * Manually set the heat-loss coefficient (W/K). Body: {"HLOSS": <number>}.
 * Reset to default = send the default value.
 */
void handleSetHeatLoss()
{
    if (!checkHttpPost(server->method()))
        return;
    if (!requireExpertMode())
        return;

    StaticJsonDocument<64> doc;
    const String &message = getRequestBody();
    DeserializationError error = deserializeJson(doc, message);
    if (error || !doc.containsKey(F("HLOSS")))
    {
        server->send(400, F("text/plain"), F("Missing HLOSS"));
        return;
    }

    bwc->setHeatLoss(doc[F("HLOSS")].as<float>());
    server->send(200, F("application/json"), F("{\"saved\":true}"));
}
