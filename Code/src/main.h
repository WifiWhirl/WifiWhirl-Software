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

command_que_item parseCommandFromJson(const JsonVariantConst &src);
void sendWS();
void getOtherInfo(String &rtn);
void sendMQTT();
void startWiFi();
void startWiFiConfigPortal(const String &storedSsid = "", const String &storedPwd = "");
void startNTP();
void startOTA();
void stopall();
void pause_all(bool action);
void startWebSocket();
void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t len);
void startHttpServer();
void handleGetHardware();
void handleSetHardware();
void handleNotFound();
String getContentType(const String &filename);
bool handleFileRead(String path);
bool checkHttpPost(HTTPMethod method);
bool checkHttpGet(HTTPMethod method);
String queryAmbientTemperature();
void handleGetWeather();
void handleGetConfig();
void handleSetConfig();
void handleGetCommandQueue();
void handleAddCommand();
void handleEditCommand();
void handleDelCommand();
void handle_cmdq_file();
void loadWebConfig();
void saveWebConfig();
void handleGetWebConfig();
void handleSetWebConfig();
sWifi_info loadWifi();
void saveWifi(const sWifi_info &wifi_info);
void handleGetWifi();
void handleSetWifi();
void handleScanWifi();
void handleResetWifi();
void resetWiFi();
void loadMqtt();
void saveMqtt();
void handleGetMqtt();
void handleSetMqtt();
void handleRestart();
void handleWebhook();
void handleGetStates();
void handleGetTemps();
void handleUpdate();
void handleGetSmartSchedule();
void handleSetSmartSchedule();
void handleUpdateSmartSchedule();
void handleCancelSmartSchedule();
void startMqtt();
void mqttCallback(char *topic, byte *payload, unsigned int length);
void mqttConnect();
const String& getMqttTopicButton();
time_t getBootTime();
void handleESPInfo();

void setupHA();
void handlePrometheusMetrics();
