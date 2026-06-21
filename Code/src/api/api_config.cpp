#include "api/api.h"
#include "web/web.h"
#include "net/net.h"

/**
 * response for /getconfig/
 * web server prints a json document
 */
void handleGetConfig()
{
    if (!checkHttpPost(server->method()))
        return;

    String json;
    json.reserve(320);
    bwc->getJSONSettings(json);
    server->send(200, F("text/plain"), json);
}

/**
 * response for /setconfig/
 * web server writes a json document
 */
void handleSetConfig()
{
    if (!checkHttpPost(server->method()))
        return;

    String message = server->arg(0);
    bwc->setJSONSettings(message);

    if (bwc->enableWeather)
    {
        queryAmbientTemperature();
    }

    server->send(200, F("text/plain"), "");
}
