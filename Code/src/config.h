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

/* The immutable factory hostname from the provisioned /device.json base. This is
 * the identity the device presents to the WifiWhirl cloud so it always matches
 * the preseeded PSK - never the user-renamable deviceName. Returns "" on an
 * unprovisioned (open-source) unit, which keeps the cloud link inactive. */
String cloudHostname();

/* Network Settings */
extern String wmApName;
extern String wmApPassword;
extern String netHostname;
extern const char *defaultTimezone;
extern const char *defaultTimezoneName;

/* OTA Service Credentials */
extern String OTAName;
extern String OTAPassword;
extern const char *update_path;

/* PoolLink cloud PSK seed (64 hex chars, seeded in config.cpp per device). */
extern String cloudPskHex;

/* Web firmware self-update (device pulls a signed image; opt-in, off by default) */
extern bool webUpdateEnabled; // persisted in /devuser.json
extern String webUpdateUrl;   // manifest base; fetches <url>/<env>.json

/* First-run Setup Assistant: false until the wizard finishes (persisted in
 * /devuser.json). The SPA shows the onboarding wizard while this is false. */
extern bool setupComplete;

/* Expert mode: unlocks user-configurable power (wattage) values (persisted in /devuser.json) */
extern bool expertMode;

/* Global UI Authentication */
extern bool globalAuthEnabled;
extern String globalAuthUser;
extern String globalAuthSalt; // hex
extern String globalAuthHash; // hex PBKDF2-HMAC-SHA256(salt, password)

/* Webhook Basic Authentication */
extern bool webhookEnabled;
extern bool webhookAuthEnabled;
extern String webhookAuthUser;
extern String webhookAuthPassword;

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
