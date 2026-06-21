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

#else

#include <WebServer.h>
#include <WiFi.h>

#endif

#include <LittleFS.h>
#include <PubSubClient.h>
#include <Ticker.h>
#include <WebSocketsServer.h>
#include <WiFiManager.h>
#define ESP_WiFiManager WiFiManager
#include <umm_malloc/umm_heap_select.h>

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

// --- HTTP server ---
#if defined(ESP8266)
extern ESP8266WebServer *server;
#elif defined(ESP32)
extern WebServer server;
#endif

// --- WebSocket state ---
extern WebSocketsServer *webSocket;
extern Ticker updateWSTimer;
extern bool sendWSFlag;

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

// --- Weather state ---
extern uint64_t ambExpires;

// --- OTA / firmware update server ---
extern ESP8266HTTPUpdateServer httpUpdater;

// --- Core lifecycle (main.cpp) ---
// Per-area function declarations live in net/net.h, web/web.h, sys/sys.h, api/api.h.
void stopall();
void pause_all(bool action);
