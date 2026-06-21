#include "web/web.h"
#include "net/net.h"
#include "sys/sys.h"
#include "api/api.h"

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
    server = new ESP8266WebServer(80);

    // Optional global auth: when enabled, guard() requires a valid session cookie
    // (401 otherwise); when disabled it is a transparent pass-through. The Cookie
    // header must be collected explicitly or server->header("Cookie") is empty.
    server->collectHeaders("Cookie");

    // Login/logout are never guarded.
    server->on(F("/login"), HTTP_POST, handleLogin);
    server->on(F("/logout"), handleLogout);

    server->on(F("/getconfig/"), guard(handleGetConfig));
    server->on(F("/setconfig/"), guard(handleSetConfig));
    server->on(F("/getcommands/"), guard(handleGetCommandQueue));
    server->on(F("/addcommand/"), guard(handleAddCommand));
    server->on(F("/editcommand/"), guard(handleEditCommand));
    server->on(F("/delcommand/"), guard(handleDelCommand));
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
    server->on(F("/getpolldata/"), guard(handleGetPollData)); // Polling fallback for WebSocket data
    server->on(F("/sendcommand/"), guard(handleSendCommand)); // Polling fallback for WebSocket commands
    // server->on(F("/getfiles/"), updateFiles);

    server->on(F("/update"), HTTP_GET, []()
               {
      if(!legacyAuthOk("update")) { return; } handleUpdate(); });

    // handle Update from web. The firmware POST keeps its own OTAPassword Basic
    // Auth prompt even under global auth (the updater library can't read cookies).
    httpUpdater.setup(server, update_path, "update", OTAPassword.c_str());

    // if someone requests any other file or page, go to function 'handleNotFound'
    // and check if the file exists
    server->onNotFound(handleNotFound);
    // start the HTTP server
    server->begin();
    // Serial.println(F("HTTP > server started"));
}

/**
 * Fallback handler for unmatched routes
 * Attempts to serve the requested URI from LittleFS, else returns 404
 */
void handleNotFound()
{
    // check if the file exists in the flash memory (LittleFS), if so, send it
    if (!handleFileRead(server->uri()))
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
