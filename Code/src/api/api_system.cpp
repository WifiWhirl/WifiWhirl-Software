#include "api/api.h"
#include "web/web.h"

/**
 * response for /restart/
 */
void handleRestart()
{
    // Send styled restart page
    String html =
        F("<!DOCTYPE html>"
          "<html>"
          "<head>"
          "<title>WifiWhirl | Neustart</title>"
          "<meta charset='utf-8' />"
          "<meta name='viewport' content='width=device-width, initial-scale=1' />"
          "<style>"
          "body { font-family: sans-serif; text-align: center; padding: 50px; margin: 0; background: #f5f5f5; }"
          "h1 { color: #4051b5; margin-bottom: 20px; }"
          "p { font-size: 18px; margin: 20px; color: #333; }"
          ".info { color: #666; font-size: 16px; }"
          ".btn { display: inline-block; margin-top: 30px; padding: 10px 20px; background: #4051b5; color: white; text-decoration: none; border-radius: 5px; }"
          ".btn:hover { background: #2c3a8f; }"
          "</style>"
          "</head>"
          "<body>"
          "<h1>WifiWhirl startet neu...</h1>"
          "<p>Das Modul wird neu gestartet.</p>"
          "<p class='info'>Bitte warte ca. 30 Sekunden...</p>"
          "<a href='/' class='btn'>Zurück zur Übersicht</a>"
          "<script>"
          "setTimeout(function() { window.location.href = '/'; }, 30000);"
          "</script>"
          "</body>"
          "</html>");

    server->send(200, F("text/html"), html);

    // save all settings
    bwc->saveSettings();

    delay(1000);
    // stop all services
    stopall();
    delay(1000);

    // restart
    Serial.println(F("ESP restart ..."));
    ESP.restart();
    delay(3000);
}

/**
 * response for /update
 */
void handleUpdate()
{
    handleFileRead("/update.html");
}

/**
 * Estimate the wall-clock time the device booted
 * Subtracts uptime (millis) from current time
 * @return boot time as a Unix timestamp
 */
time_t getBootTime()
{
    time_t seconds = millis() / 1000;
    time_t result = time(nullptr) - seconds;
    return result;
}

/**
 * response for /info/
 * Send ESP8266 runtime diagnostics (heap, stack, sketch size, CPU) as plain text
 */
void handleESPInfo()
{
#ifdef ESP8266
    char stack;
    uint32_t stacksize = stack_start - &stack;
    size_t const BUFSIZE = 1024;
    char response[BUFSIZE];
    char const *response_template =
        PSTR("Stack size:          %u \n"
             "Free Heap:           %u \n"
             "Min  Heap:           %u \n"
             "Core version:        %s \n"
             "CPU fq:              %u MHz\n"
             "Cycle count:         %u \n"
             "Free cont stack:     %u \n"
             "Sketch size:         %u \n"
             "Free sketch space:   %u \n"
             "Max free block size: %u \n");

    snprintf_P(response, BUFSIZE, response_template,
               stacksize,
               ESP.getFreeHeap(),
               heap_water_mark,
               ESP.getCoreVersion().c_str(),
               ESP.getCpuFreqMHz(),
               ESP.getCycleCount(),
               ESP.getFreeContStack(),
               ESP.getSketchSize(),
               ESP.getFreeSketchSpace(),
               ESP.getMaxFreeBlockSize());
    server->send(200, F("text/plain; charset=utf-8"), response);
#endif
}
