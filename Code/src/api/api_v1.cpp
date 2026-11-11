#include "api/api.h"
#include "web/web.h"
#include "net/net.h"

/*
 * Public, documented API for integrations (ioBroker, Home Assistant, scripts),
 * Unlike the SPA endpoints it never depends on the global login: it
 * always needs HTTP Basic Auth with the webhook credentials, and it only exists
 * while "API aktivieren" is on. No CORS headers, so other web pages can't use it.
 */
bool apiEnabled = false;

static void apiError(int code, const __FlashStringHelper *msg)
{
    server->send(code, F("application/json"), String(F("{\"error\":\"")) + msg + F("\"}"));
}

static WebServerT::THandlerFunction apiGuard(WebServerT::THandlerFunction handler)
{
    return [handler]()
    {
        if (!apiEnabled)
            return apiError(403, F("API is disabled. Turn it on in the expert mode page, card API."));
        if (!webhookAuthEnabled || webhookAuthUser.length() == 0 || webhookAuthPassword.length() == 0)
            return apiError(403, F("API needs the webhook credentials to be enabled."));
        if (crossSiteRequest())
            return apiError(403, F("Cross-site request rejected."));
        if (!basicAuthOk(webhookAuthUser, webhookAuthPassword))
            return;
        handler();
    };
}

// GET /api/v1/info
static void handleApiInfo()
{
    StaticJsonDocument<384> doc;
    bool sta = WiFi.status() == WL_CONNECTED;
    doc[F("apiVersion")] = 1;
    doc[F("firmware")] = FW_VERSION;
    doc[F("model")] = bwc->getModel();
    doc[F("hostname")] = deviceName;
    doc[F("ip")] = sta ? WiFi.localIP().toString() : String();
    doc[F("rssi")] = sta ? WiFi.RSSI() : 0;
    doc[F("uptime")] = millis() / 1000;
    String json;
    serializeJson(doc, json);
    server->send(200, F("application/json"), json);
}

void registerApiV1Routes()
{
    server->on(F("/api/v1/info"), HTTP_GET, apiGuard(handleApiInfo));
}
