#pragma once

#include <Arduino.h>
#include <ArduinoOTA.h>
#include <DNSServer.h>

#ifdef ESP8266

#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266HTTPUpdateServer.h>
#include <time.h>
typedef ESP8266WebServer WebServerT;
typedef ESP8266HTTPUpdateServer HTTPUpdateServerT;

#else

#include <WebServer.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>
#include <esp_random.h>
#include <ctime> // std::time used in api_commands.cpp (ESP8266 core pulls it in implicitly)
typedef WebServer WebServerT;

#endif

#include <LittleFS.h>
#include <PubSubClient.h>
#include <Ticker.h>
#ifdef ESP8266
#include <umm_malloc/umm_heap_select.h>
#endif

#include "bwc.h"
#include "config.h"

// --- Core application state ---
extern BWC *bwc;
extern char *stack_start;
extern uint32_t heap_water_mark;

// --- Timers & periodic tasks ---
extern Ticker bootlogTimer;
extern Ticker periodicTimer;
extern Ticker startComplete;
extern bool periodicTimerFlag;
extern int periodicTimerInterval;

// --- WiFi state ---
extern bool wifiConnected;
// True only while the SoftAP "Setup Assistant" captive portal is running
// (set in startSetupPortal). Read by handleNotFound and handleAuthStatus.
extern bool apSetupMode;

// --- HTTP server ---
extern WebServerT *server;

// --- MQTT runtime state ---
extern WiFiClient *aWifiClient;
extern PubSubClient *mqttClient;
extern int mqtt_connect_count;
extern String prevButtonName;
extern Ticker updateMqttTimer;
extern bool sendMQTTFlag;
extern bool enableMqtt;

// --- Home Assistant discovery state ---
extern bool haDiscoveryInProgress;
extern unsigned long haDiscoveryLastCompleted;
extern bool haDiscoveryHasRunOnce;

// --- OTA / firmware update server ---
// ESP8266 uses the core's HTTPUpdateServer; ESP32's bundled one #includes the
// absent SPIFFS.h, so its web upload is wired through Update in http_routes.cpp.
#ifdef ESP8266
extern HTTPUpdateServerT httpUpdater;
#endif

// --- Core lifecycle (main.cpp) ---
// Per-area function declarations live in net/net.h, web/web.h, sys/sys.h, api/api.h.
void stopall();
void pause_all(bool action);

// Send a {"restart":true,"reason":"<code>"} JSON reply, persist settings, stop
// services, and reboot. Shared by the wifi/mqtt/hardware config handlers; the
// reason is a stable code the SPA localizes (see frontend showRestart()).
void restartWithReason(const char *reasonCode);
