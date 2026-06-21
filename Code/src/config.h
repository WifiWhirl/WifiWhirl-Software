#pragma once

#include <Arduino.h>
#ifdef ESP8266
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#endif
#include "WifiWhirl_Version.h"

/* Device identity (loaded from flash at boot, see device_config.cpp) */
extern String deviceName;

/* Network Settings */
extern bool enableWmApFallback;
extern String wmApName;
extern String wmApPassword;
extern String netHostname;
extern const char *defaultTimezone;
extern const char *defaultTimezoneName;

/* OTA Service Credentials */
extern String OTAName;
extern String OTAPassword;
extern const char *update_path;

/* WifiWhirl Cloud Settings */
extern String cloudApi;
extern String cloudApiKey;

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
