#pragma once

#include "main.h"

// --- WiFi (wifi_manager.cpp) ---
void startWiFi();
void startSetupPortal(const String &storedSsid = "", const String &storedPwd = "");
void startNTP();
sWifi_info loadWifi();
void saveWifi(const sWifi_info &wifi_info);
void handleGetWifi();
void handleSetWifi();
void handleScanWifi();
void handleResetWifi();
void resetWiFi();
// Shared validator: accepts a dotted IPv4 or an RFC-1123 hostname. When
// allowEmpty is true an empty string passes (e.g. optional NTP override).
bool isValidHostnameOrIp(const String &host, bool allowEmpty);

// --- Device identity/secrets (device_config.cpp) ---
void seedDeviceConfig();
void loadDevice();
void saveDevice();
void saveDeviceUser();
void resetDeviceConfig();
void handleProvisionDevice();
void handleGetDevice();
void handleSetDevice();

// --- MQTT (mqtt_manager.cpp) ---
void sendMQTT();
void loadMqtt();
void saveMqtt();
void handleGetMqtt();
void handleSetMqtt();
void startMqtt();
void mqttCallback(char *topic, byte *payload, unsigned int length);
void mqttConnect();
const String &getMqttTopicButton();

// --- PoolLink Cloud (cloud.cpp) ---
extern bool cloudEnabled;
extern String cloudHost;
extern int cloudPort;
extern String cloudWebUrl;
void loadCloudConfig();
void saveCloudConfig();
void handleGetCloud();
void handleSetCloud();
void handleGetPairing();
// The outdoor temperature arrives in the PoolLink sensor ack; this only hands
// the town that came with it to the local UI.
void handleGetWeather();

// --- Home Assistant (homeassistant.cpp) ---
void setupHA();

// --- OTA (ota.cpp) ---
void startOTA();

// --- Web firmware self-update (web_update.cpp) ---
// Daily update check
void updateCheckTick();
const String &updateAvailableVersion();
void handleGetUpdate();
void handleDoUpdate();

// --- Webhooks (webhooks.cpp) ---
// Enforce the optional webhook basic-auth credentials. Returns true if the
// request may proceed; otherwise it has already sent a 401 challenge.
bool checkWebhookAuth();
void handleWebhook();
void handleGetStates();
void handleGetTemps();
