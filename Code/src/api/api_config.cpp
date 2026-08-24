#include "api/api.h"
#include "web/web.h"
#include "net/net.h"
#include "cloud_task.h"

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

    String message = getRequestBody();
    String error;
    if (!bwc->setJSONSettings(message, error))
    {
        server->send(400, F("text/plain"), error.length() ? error : F("Invalid settings"));
        return;
    }

    // Settings changed locally - push a fresh snapshot so the cloud UI agrees,
    // and solicit a reply in case this turned the cloud weather on.
    CloudTask::notifySettingsChanged();
    CloudTask::requestResync();

    server->send(200, F("text/plain"), "");
}
