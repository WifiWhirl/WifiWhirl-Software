#include "net/net.h"

/**
 * Initialize and start the ArduinoOTA service
 * Sets hostname/password and registers OTA lifecycle callbacks
 * (start handler stops all running outputs before flashing)
 */
void startOTA()
{
    ArduinoOTA.setHostname(OTAName.c_str());
    ArduinoOTA.setPassword(OTAPassword.c_str());

    ArduinoOTA.onStart([]()
                       {
        // Serial.println(F("OTA > Start"));
        bwc->saveSettings(); // stopall() unmounts LittleFS: keep counters/energy
        stopall(); });
    ArduinoOTA.onEnd([]()
                     {
                         // Serial.println(F("OTA > End"));
                     });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total)
                          {
                              // Serial.printf("OTA > Progress: %u%%\r\n", (progress / (total / 100)));
                          });
    // ArduinoOTA does not reboot on an error (ESP8266), and stopall() already
    // deleted cio/dsp: back in loop() that was a null deref
    ArduinoOTA.onError([](ota_error_t error)
                       {
                           if (error != OTA_AUTH_ERROR && error != OTA_BEGIN_ERROR) // these fail before onStart
                               ESP.restart();
                           // Serial.printf("OTA > Error[%u]: ", error);
                           // if (error == OTA_AUTH_ERROR) Serial.println(F("Auth Failed"));
                           // else if (error == OTA_BEGIN_ERROR) Serial.println(F("Begin Failed"));
                           // else if (error == OTA_CONNECT_ERROR) Serial.println(F("Connect Failed"));
                           // else if (error == OTA_RECEIVE_ERROR) Serial.println(F("Receive Failed"));
                           // else if (error == OTA_END_ERROR) Serial.println(F("End Failed"));
                       });
    ArduinoOTA.begin();
    // Serial.println(F("OTA > ready"));
}
