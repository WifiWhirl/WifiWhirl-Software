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

    // streamed: the document plus a ~1.1 KB String copy of it before
    DynamicJsonDocument doc(2048);
    if (doc.capacity() == 0)
    {
        server->send(503, F("text/plain"), F("Out of memory"));
        return;
    }
    bwc->buildJSONSettings(doc.to<JsonObject>());
    server->setContentLength(CONTENT_LENGTH_UNKNOWN);
    server->send(200, F("text/plain"), "");
    ChunkedResponsePrint out;
    serializeJson(doc, out);
    out.flush();
    server->sendContent("");
}

/**
 * response for /setconfig/
 * web server writes a json document
 */
void handleSetConfig()
{
    if (!checkHttpPost(server->method()))
        return;

    const String &message = getRequestBody();
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
