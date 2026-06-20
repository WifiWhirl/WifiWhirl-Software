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
    server->on(F("/getconfig/"), handleGetConfig);
    server->on(F("/setconfig/"), handleSetConfig);
    server->on(F("/getcommands/"), handleGetCommandQueue);
    server->on(F("/addcommand/"), handleAddCommand);
    server->on(F("/editcommand/"), handleEditCommand);
    server->on(F("/delcommand/"), handleDelCommand);
    server->on(F("/getwebconfig/"), handleGetWebConfig);
    server->on(F("/setwebconfig/"), handleSetWebConfig);
    server->on(F("/getwifi/"), handleGetWifi);
    server->on(F("/setwifi/"), handleSetWifi);
    server->on(F("/scanwifi/"), handleScanWifi);
    server->on(F("/resetwifi/"), handleResetWifi);
    server->on(F("/getmqtt/"), handleGetMqtt);
    server->on(F("/setmqtt/"), handleSetMqtt);
    server->on(F("/getweather/"), handleGetWeather);
    server->on(F("/getstates/"), handleGetStates);
    server->on(F("/gettemps/"), handleGetTemps);
    server->on(F("/restart/"), handleRestart);
    server->on(F("/metrics"), handlePrometheusMetrics); // prometheus metrics
    server->on(F("/info/"), handleESPInfo);
    server->on(F("/support/"), handleSupportPackage);
    server->on(F("/sethardware/"), handleSetHardware);
    server->on(F("/gethardware/"), handleGetHardware);
    server->on(F("/debug-on/"), []()
               {if(!server->authenticate("debug", OTAPassword)) { return server->requestAuthentication(); } bwc->BWC_DEBUG = true; server->send(200, F("text/plain"), "ok"); });
    server->on(F("/debug-off/"), []()
               {if(!server->authenticate("debug", OTAPassword)) { return server->requestAuthentication(); } bwc->BWC_DEBUG = false; server->send(200, F("text/plain"), "ok"); });
    server->on(F("/cmdq_file/"), handle_cmdq_file);
    server->on(F("/hook/"), handleWebhook);
    server->on(F("/getsmartschedule/"), handleGetSmartSchedule);
    server->on(F("/setsmartschedule/"), handleSetSmartSchedule);
    server->on(F("/updatesmartschedule/"), handleUpdateSmartSchedule);
    server->on(F("/cancelsmartschedule/"), handleCancelSmartSchedule);
    server->on(F("/getpolldata/"), handleGetPollData); // Polling fallback for WebSocket data
    server->on(F("/sendcommand/"), handleSendCommand); // Polling fallback for WebSocket commands
    // server->on(F("/getfiles/"), updateFiles);

    server->on(F("/update"), HTTP_GET, []()
               {
      if(!server->authenticate("update", OTAPassword)) { return server->requestAuthentication(); } handleUpdate(); });

    // handle Update from web
    httpUpdater.setup(server, update_path, "update", OTAPassword);

    // if someone requests any other file or page, go to function 'handleNotFound'
    // and check if the file exists
    server->onNotFound(handleNotFound);
    // start the HTTP server
    server->begin();
    // Serial.println(F("HTTP > server started"));
}

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
