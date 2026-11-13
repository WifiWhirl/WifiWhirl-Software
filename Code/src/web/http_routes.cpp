#include "web/web.h"
#include "net/net.h"
#include "sys/sys.h"
#include "api/api.h"

#ifndef ESP8266
static bool otaUploadAuthorized = false;
static bool otaUploadComplete = false;
static bool otaUploadSucceeded = false;
#endif

/**
 * start a HTTP server with a file read and upload handler
 */
void startHttpServer()
{
    if (server)
    {
        server->stop();
        delete server;
    }
    server = new WebServerT(80);

    // Optional global auth: when enabled, guard() requires a valid session cookie
    // (401 otherwise); when disabled it is a transparent pass-through. The Cookie
    // header must be collected explicitly or server->header("Cookie") is empty.
    // Origin + Host: guard() rejects cross-site browser requests
#ifdef ESP8266
    server->collectHeaders("Cookie", "Origin", "Host");
#else
    const char *collectKeys[] = {"Cookie", "Origin", "Host"}; // ESP32's WebServer only has the array form
    server->collectHeaders(collectKeys, 3);
#endif

    // Login/logout are never guarded. The plain routes: PLAIN_ROUTES further down.
    server->on(F("/auth/status"), HTTP_GET, handleAuthStatus);
    server->on(F("/login"), HTTP_POST, handleLogin);

    server->on(F("/backup/"), HTTP_GET, handleBackup);  // self-guards via legacyAuthOk()
    server->on(F("/restore/"), HTTP_POST, handleRestore, handleRestoreUpload);
    registerApiV1Routes(); // public API, own Basic Auth
    // server->on(F("/getfiles/"), updateFiles);

    // GET /update serves the legacy upload form. With ?basic=1 it does nothing but
    // issue the OTA Basic-Auth challenge on this very path: the SPA calls that
    // before POSTing a .bin, because the updater answers 401 only *after* the whole
    // file arrived - the credential prompt would otherwise appear once the upload
    // had already run, and the browser would then send the file a second time.
    server->on(F("/update"), HTTP_GET, []()
               {
      if (server->hasArg("basic"))
      {
          if (!server->authenticate("update", OTAPassword.c_str()))
          {
              otaAuthFail();
              return;
          }
          server->send(200, F("text/plain"), F("ok"));
          return;
      }
      if(!legacyAuthOk("update")) { return; } handleUpdate(); });

    // handle Update from web. The firmware POST keeps its own OTAPassword Basic
    // Auth prompt even under global auth (the updater library can't read cookies).
#ifdef ESP8266
    httpUpdater.setup(server, update_path, "update", OTAPassword.c_str());
#else
    // ESP32's HTTPUpdateServer is unusable here (it #includes the absent SPIFFS.h),
    // so wire the browser .bin upload straight through the Update library.
    server->on(
        update_path, HTTP_POST,
        []()
        {
            server->sendHeader(F("Connection"), F("close"));
            if (!otaUploadAuthorized)
            {
                // the only response for a failed auth: the upload callback used to
                // send one too, two responses on one socket
                otaAuthFail();
                return;
            }
            if (!otaUploadComplete || !otaUploadSucceeded || Update.hasError())
            {
#ifdef UPDATE_SIGNING
                server->send(500, F("text/plain"), String(F("FAIL: ")) + signedUploadError());
#else
                server->send(500, F("text/plain"), F("FAIL"));
#endif
                otaUploadAuthorized = false;
                return;
            }
            server->send(200, F("text/plain"), F("OK"));
            otaUploadAuthorized = false;
            delay(500);
            ESP.restart();
        },
        []()
        {
            HTTPUpload &upload = server->upload();
            if (upload.status == UPLOAD_FILE_START)
            {
                otaUploadAuthorized = false;
                otaUploadComplete = false;
                otaUploadSucceeded = false;
                if (!server->authenticate("update", OTAPassword.c_str()))
                {
                    Update.abort(); // the final handler answers with the 401
                    return;
                }
                otaUploadAuthorized = true;
#ifdef UPDATE_SIGNING
                otaUploadSucceeded = signedUploadBegin();
#else
                otaUploadSucceeded = Update.begin(UPDATE_SIZE_UNKNOWN);
#endif
            }
            else if (!otaUploadAuthorized)
            {
                Update.abort();
                return;
            }
            else if (upload.status == UPLOAD_FILE_WRITE)
            {
#ifdef UPDATE_SIGNING
                if (!signedUploadWrite(upload.buf, upload.currentSize))
#else
                if (Update.write(upload.buf, upload.currentSize) != upload.currentSize)
#endif
                {
                    otaUploadSucceeded = false;
                }
            }
            else if (upload.status == UPLOAD_FILE_END)
            {
                otaUploadComplete = true;
#ifdef UPDATE_SIGNING
                // unsigned or foreign images end here: nothing gets committed
                otaUploadSucceeded = otaUploadSucceeded && signedUploadEnd();
#else
                otaUploadSucceeded = otaUploadSucceeded && Update.end(true);
#endif
            }
            else if (upload.status == UPLOAD_FILE_ABORTED)
            {
                Update.abort();
                otaUploadAuthorized = false;
                otaUploadComplete = false;
                otaUploadSucceeded = false;
            }
        });
#endif

    // if someone requests any other file or page, go to function 'handleNotFound'
    // and check if the file exists
    server->onNotFound(handleNotFound);
    // start the HTTP server
    server->begin();
    // Serial.println(F("HTTP > server started"));
}

static bool isPublicFallbackAsset(const String &path)
{
    if (path == "/" || path == "/index.html")
        return true;

    if (path.indexOf("..") >= 0 || path.indexOf('\\') >= 0)
        return false;

    if ((path.startsWith("/app-") && (path.endsWith(".js") || path.endsWith(".css"))) ||
        path == "/manifest.json" ||
        path == "/favicon.ico" ||
        path == "/favicon-dark.png" ||
        path == "/favicon-light.png" ||
        path == "/apple-touch-icon.png" ||
        path == "/icon-192.png" ||
        path == "/icon-512.png" ||
        path == "/maskable-icon-192.png" ||
        path == "/maskable-icon-512.png" ||
        path == "/logo.png" ||
        path == "/menu.png" ||
        path == "/license.txt" ||
        path == "/furelise.mel" ||
        path == "/popcorn.mel" ||
        path == "/weekender")
    {
        return true;
    }

    return false;
}

/**
 * Fallback handler for unmatched routes.
 * Only public SPA/static assets are served here. Internal LittleFS files must be
 * exposed through explicit guarded routes, never through the global not-found
 * file fallback.
 */
// The plain routes (any method, no upload) are not registered one by one: each
// server->on() kept a handler object, a String copy of the path and, through
// guard(), a heap-allocated std::function on the ESP8266 heap for good. They sit
// in a flash table and handleNotFound() dispatches them before anything else.
struct PlainRoute
{
    const char *path; // PROGMEM
    void (*fn)();
    bool guarded; // session/cross-site check like guard()
};

static const char R_LOGOUT[] PROGMEM = "/logout";
static const char R_GETCONFIG[] PROGMEM = "/getconfig/";
static const char R_SETCONFIG[] PROGMEM = "/setconfig/";
static const char R_GETCOMMANDS[] PROGMEM = "/getcommands/";
static const char R_ADDCOMMAND[] PROGMEM = "/addcommand/";
static const char R_EDITCOMMAND[] PROGMEM = "/editcommand/";
static const char R_DELCOMMAND[] PROGMEM = "/delcommand/";
static const char R_SETCOMMANDS[] PROGMEM = "/setcommands/";
static const char R_GETWEBCONFIG[] PROGMEM = "/getwebconfig/";
static const char R_SETWEBCONFIG[] PROGMEM = "/setwebconfig/";
static const char R_GETDEVICE[] PROGMEM = "/getdevice/";
static const char R_SETDEVICE[] PROGMEM = "/setdevice/";
static const char R_PROVISION[] PROGMEM = "/provision/";
static const char R_GETWIFI[] PROGMEM = "/getwifi/";
static const char R_SETWIFI[] PROGMEM = "/setwifi/";
static const char R_SCANWIFI[] PROGMEM = "/scanwifi/";
static const char R_RESETWIFI[] PROGMEM = "/resetwifi/";
static const char R_GETMQTT[] PROGMEM = "/getmqtt/";
static const char R_SETMQTT[] PROGMEM = "/setmqtt/";
static const char R_GETCLOUD[] PROGMEM = "/getcloud/";
static const char R_SETCLOUD[] PROGMEM = "/setcloud/";
static const char R_GETPAIRING[] PROGMEM = "/getpairing/";
static const char R_GETWEATHER[] PROGMEM = "/getweather/";
static const char R_GETSTATES[] PROGMEM = "/getstates/";
static const char R_GETTEMPS[] PROGMEM = "/gettemps/";
static const char R_RESTART[] PROGMEM = "/restart/";
static const char R_METRICS[] PROGMEM = "/metrics";
static const char R_SUPPORT[] PROGMEM = "/support/";
static const char R_SETHARDWARE[] PROGMEM = "/sethardware/";
static const char R_GETHARDWARE[] PROGMEM = "/gethardware/";
static const char R_CMDQ_FILE[] PROGMEM = "/cmdq_file/";
static const char R_HOOK[] PROGMEM = "/hook/";
static const char R_GETSMARTSCHEDULE[] PROGMEM = "/getsmartschedule/";
static const char R_SETSMARTSCHEDULE[] PROGMEM = "/setsmartschedule/";
static const char R_UPDATESMARTSCHEDULE[] PROGMEM = "/updatesmartschedule/";
static const char R_CANCELSMARTSCHEDULE[] PROGMEM = "/cancelsmartschedule/";
static const char R_GETHLCAL[] PROGMEM = "/gethlcal/";
static const char R_STARTHLCAL[] PROGMEM = "/starthlcal/";
static const char R_CANCELHLCAL[] PROGMEM = "/cancelhlcal/";
static const char R_SETHEATLOSS[] PROGMEM = "/setheatloss/";
static const char R_GETPOLLDATA[] PROGMEM = "/getpolldata/";
static const char R_SENDCOMMAND[] PROGMEM = "/sendcommand/";
static const char R_GETUPDATE[] PROGMEM = "/getupdate/";
static const char R_DOUPDATE[] PROGMEM = "/doupdate/";

static const char R_GETAPIKEYS[] PROGMEM = "/getapikeys/";
static const char R_ADDAPIKEY[] PROGMEM = "/addapikey/";
static const char R_DELAPIKEY[] PROGMEM = "/delapikey/";

static const PlainRoute PLAIN_ROUTES[] PROGMEM = {
    {R_GETAPIKEYS, handleGetApiKeys, true},
    {R_ADDAPIKEY, handleAddApiKey, true},
    {R_DELAPIKEY, handleDelApiKey, true},
    {R_LOGOUT, handleLogout, false},
    {R_GETCONFIG, handleGetConfig, true},
    {R_SETCONFIG, handleSetConfig, true},
    {R_GETCOMMANDS, handleGetCommandQueue, true},
    {R_ADDCOMMAND, handleAddCommand, true},
    {R_EDITCOMMAND, handleEditCommand, true},
    {R_DELCOMMAND, handleDelCommand, true},
    {R_SETCOMMANDS, handleSetCommandQueue, true},
    {R_GETWEBCONFIG, handleGetWebConfig, true},
    {R_SETWEBCONFIG, handleSetWebConfig, true},
    {R_GETDEVICE, handleGetDevice, true},
    {R_SETDEVICE, handleSetDevice, true},
    {R_PROVISION, handleProvisionDevice, true}, // backend-only factory provisioning
    {R_GETWIFI, handleGetWifi, true},
    {R_SETWIFI, handleSetWifi, true},
    {R_SCANWIFI, handleScanWifi, true},
    {R_RESETWIFI, handleResetWifi, true},
    {R_GETMQTT, handleGetMqtt, true},
    {R_SETMQTT, handleSetMqtt, true},
    {R_GETCLOUD, handleGetCloud, true},
    {R_SETCLOUD, handleSetCloud, true},
    {R_GETPAIRING, handleGetPairing, true},
    {R_GETWEATHER, handleGetWeather, true},
    {R_GETSTATES, handleGetStates, true},
    {R_GETTEMPS, handleGetTemps, true},
    {R_RESTART, handleRestart, true},
    {R_METRICS, handlePrometheusMetrics, false}, // machine endpoint: left open (no cookie possible)
    {R_SUPPORT, handleSupportPackage, false}, // self-guards via legacyAuthOk()
    {R_SETHARDWARE, handleSetHardware, true},
    {R_GETHARDWARE, handleGetHardware, true},
    {R_CMDQ_FILE, handle_cmdq_file, true},
    {R_HOOK, handleWebhook, false}, // machine endpoint: left open (no cookie possible)
    {R_GETSMARTSCHEDULE, handleGetSmartSchedule, true},
    {R_SETSMARTSCHEDULE, handleSetSmartSchedule, true},
    {R_UPDATESMARTSCHEDULE, handleUpdateSmartSchedule, true},
    {R_CANCELSMARTSCHEDULE, handleCancelSmartSchedule, true},
    {R_GETHLCAL, handleGetHlCal, true},
    {R_STARTHLCAL, handleStartHlCal, true},
    {R_CANCELHLCAL, handleCancelHlCal, true},
    {R_SETHEATLOSS, handleSetHeatLoss, true},
    {R_GETPOLLDATA, handleGetPollData, true}, // Polling fallback for WebSocket data
    {R_SENDCOMMAND, handleSendCommand, true}, // Polling fallback for WebSocket commands
    {R_GETUPDATE, handleGetUpdate, true}, // device-initiated web firmware self-update (opt-in; verifies signature)
    {R_DOUPDATE, handleDoUpdate, true},
};

static bool dispatchPlainRoute(const String &path)
{
    PlainRoute r;
    for (const PlainRoute &e : PLAIN_ROUTES)
    {
        memcpy_P(&r, &e, sizeof(r));
        if (strcmp_P(path.c_str(), r.path) != 0)
            continue;
        if (!r.guarded || guardOk())
            r.fn();
        return true;
    }
    return false;
}

void handleNotFound()
{
    String path = server->uri();
    if (dispatchPlainRoute(path))
        return;

    // Captive portal: while the SoftAP Setup Assistant is up, steer every
    // unknown request (incl. the OS connectivity-check probes like
    // /generate_204, /hotspot-detect.html, /ncsi.txt) to the SPA root so the
    // "sign in to network" sheet opens the onboarding wizard.
    if (apSetupMode && !isPublicFallbackAsset(path))
    {
        server->sendHeader(F("Location"),
                           String(F("http://")) + WiFi.softAPIP().toString() + F("/"));
        server->send(302, F("text/plain"), F(""));
        return;
    }

    if (!isPublicFallbackAsset(path))
    {
        server->send(404, F("text/plain"), F("404: File Not Found"));
        return;
    }

    if (!handleFileRead(path))
    {
        server->send(404, F("text/plain"), F("404: File Not Found"));
    }
}

/**
 * checks the method to be a POST
 */
bool checkHttpPost(HTTPMethod method)
{
    // /api/v1 reuses some SPA getters for GET; the API guard already ran
    if (method != HTTP_POST && !apiRequest)
    {
        server->send(405, F("text/plain"), F("Method not allowed."));
        return false;
    }
    return true;
}

/**
 * checks the method to be a GET
 */
bool checkHttpGet(HTTPMethod method)
{
    if (method != HTTP_GET)
    {
        server->send(405, F("text/plain"), F("Method not allowed."));
        return false;
    }
    return true;
}

String getRequestBody()
{
    if (!server)
        return String();

    if (server->hasArg(F("plain")))
        return server->arg(F("plain"));

    for (int i = 0; i < server->args(); i++)
    {
        if (server->argName(i).length() == 0)
            return server->arg(i);
    }

    if (server->args() == 1)
        return server->arg(0);

    return String();
}
