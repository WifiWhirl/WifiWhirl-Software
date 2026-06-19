#pragma once

#include <Arduino.h>
#ifdef ESP8266
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#endif
#include "WifiWhirl_Version.h"
#define DEVICE_NAME "wifiwhirl-2D2701"

/* Network Settings */
extern bool enableWmApFallback;
extern const char *wmApName;
extern const char *wmApPassword;
extern const char *netHostname;
extern const char *defaultTimezone;
extern const char *defaultTimezoneName;

/* OTA Service Credentials */
extern const char *OTAName;
extern const char *OTAPassword;
extern const char *update_path;

/* WifiWhirl Cloud Settings */
extern const char *cloudApi;
extern const char *cloudApiKey;

/* Web UI Configuration */
extern bool showSectionTemperature;
extern bool showSectionDisplay;
extern bool showSectionControl;
extern bool showSectionButtons;
extern bool showSectionTimer;
extern bool showSectionTotals;
extern bool showSectionEnergy;
extern bool showSectionWaterQuality;
extern bool showWQCyanuric;
extern bool showWQAlkalinity;
extern bool useControlSelector;
extern const bool hidePasswords;

/* Home Assistant Settings */
#define HA_PREFIX "homeassistant"

/* Prometheus Settings */
#define PROM_NAMESPACE "layzspa"

/* MQTT Server */
extern bool useMqtt;
extern String mqttServer;
extern int mqttPort;
extern String mqttUsername;
extern String mqttPassword;
extern String mqttClientId;
extern String mqttBaseTopic;
extern int mqttTelemetryInterval;
