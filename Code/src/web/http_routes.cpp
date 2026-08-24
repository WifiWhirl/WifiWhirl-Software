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
#ifdef ESP8266
    server->collectHeaders("Cookie");
#else
    const char *collectKeys[] = {"Cookie"}; // ESP32's WebServer only has the array form
    server->collectHeaders(collectKeys, 1);
#endif

    // Login/logout are never guarded.
    server->on(F("/auth/status"), HTTP_GET, handleAuthStatus);
    server->on(F("/login"), HTTP_POST, handleLogin);
    server->on(F("/logout"), handleLogout);

    server->on(F("/getconfig/"), guard(handleGetConfig));
    server->on(F("/setconfig/"), guard(handleSetConfig));
    server->on(F("/getcommands/"), guard(handleGetCommandQueue));
    server->on(F("/addcommand/"), guard(handleAddCommand));
    server->on(F("/editcommand/"), guard(handleEditCommand));
    server->on(F("/delcommand/"), guard(handleDelCommand));
    server->on(F("/setcommands/"), guard(handleSetCommandQueue));
    server->on(F("/getwebconfig/"), guard(handleGetWebConfig));
    server->on(F("/setwebconfig/"), guard(handleSetWebConfig));
    server->on(F("/getdevice/"), guard(handleGetDevice));
    server->on(F("/setdevice/"), guard(handleSetDevice));
    server->on(F("/provision/"), guard(handleProvisionDevice)); // backend-only factory provisioning
    server->on(F("/getwifi/"), guard(handleGetWifi));
    server->on(F("/setwifi/"), guard(handleSetWifi));
    server->on(F("/scanwifi/"), guard(handleScanWifi));
    server->on(F("/resetwifi/"), guard(handleResetWifi));
    server->on(F("/getmqtt/"), guard(handleGetMqtt));
    server->on(F("/setmqtt/"), guard(handleSetMqtt));
    server->on(F("/getcloud/"), guard(handleGetCloud));
    server->on(F("/setcloud/"), guard(handleSetCloud));
    server->on(F("/getpairing/"), guard(handleGetPairing));
    server->on(F("/getweather/"), guard(handleGetWeather));
    server->on(F("/getstates/"), guard(handleGetStates));
    server->on(F("/gettemps/"), guard(handleGetTemps));
    server->on(F("/restart/"), guard(handleRestart));
    server->on(F("/metrics"), handlePrometheusMetrics); // machine endpoint: left open (no cookie possible)
    server->on(F("/support/"), handleSupportPackage);   // self-guards via legacyAuthOk()
    server->on(F("/sethardware/"), guard(handleSetHardware));
    server->on(F("/gethardware/"), guard(handleGetHardware));
    server->on(F("/debug-on/"), []()
               {if(!legacyAuthOk("debug")) { return; } bwc->BWC_DEBUG = true; server->send(200, F("text/plain"), "ok"); });
    server->on(F("/debug-off/"), []()
               {if(!legacyAuthOk("debug")) { return; } bwc->BWC_DEBUG = false; server->send(200, F("text/plain"), "ok"); });
    server->on(F("/cmdq_file/"), guard(handle_cmdq_file));
    server->on(F("/hook/"), handleWebhook); // machine endpoint: left open (no cookie possible)
    server->on(F("/getsmartschedule/"), guard(handleGetSmartSchedule));
    server->on(F("/setsmartschedule/"), guard(handleSetSmartSchedule));
    server->on(F("/updatesmartschedule/"), guard(handleUpdateSmartSchedule));
    server->on(F("/cancelsmartschedule/"), guard(handleCancelSmartSchedule));
    server->on(F("/gethlcal/"), guard(handleGetHlCal));
    server->on(F("/starthlcal/"), guard(handleStartHlCal));
    server->on(F("/cancelhlcal/"), guard(handleCancelHlCal));
    server->on(F("/setheatloss/"), guard(handleSetHeatLoss));
    server->on(F("/getpolldata/"), guard(handleGetPollData)); // Polling fallback for WebSocket data
    server->on(F("/sendcommand/"), guard(handleSendCommand)); // Polling fallback for WebSocket commands
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
              server->requestAuthentication();
              return;
          }
          server->send(200, F("text/plain"), F("ok"));
          return;
      }
      if(!legacyAuthOk("update")) { return; } handleUpdate(); });

    // Device-initiated web firmware self-update (opt-in; verifies signature).
    server->on(F("/getupdate/"), guard(handleGetUpdate));
    server->on(F("/doupdate/"), guard(handleDoUpdate));

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
                server->send(401, F("text/plain"), F("Authentication required."));
                return;
            }
            if (!otaUploadComplete || !otaUploadSucceeded || Update.hasError())
            {
                server->send(500, F("text/plain"), F("FAIL"));
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
                    Update.abort();
                    server->requestAuthentication();
                    return;
                }
                otaUploadAuthorized = true;
                if (!Update.begin(UPDATE_SIZE_UNKNOWN))
                {
                    otaUploadSucceeded = false;
                }
            }
            else if (!otaUploadAuthorized)
            {
                Update.abort();
                return;
            }
            else if (upload.status == UPLOAD_FILE_WRITE)
            {
                if (Update.write(upload.buf, upload.currentSize) != upload.currentSize)
                {
                    otaUploadSucceeded = false;
                }
            }
            else if (upload.status == UPLOAD_FILE_END)
            {
                otaUploadComplete = true;
                otaUploadSucceeded = Update.end(true);
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
void handleNotFound()
{
    String path = server->uri();

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
    if (method != HTTP_POST)
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
