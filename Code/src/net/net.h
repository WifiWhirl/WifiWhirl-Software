#pragma once

#include "main.h"

// --- WiFi (wifi_manager.cpp) ---
void startWiFi();
void startSetupPortal(const String &storedSsid = "", const String &storedPwd = "");
void startNTP(bool wait = true);
void setBootTimeFromClock();
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
void sendMQTT(bool full);
void loadMqtt();
void saveMqtt();
void handleGetMqtt();
void handleSetMqtt();
void startMqtt();
void mqttCallback(char *topic, byte *payload, unsigned int length);
void mqttConnect();
void publishRebootTime();
const char *getMqttTopicButton();

// --- PoolLink Cloud (cloud.cpp) ---
extern bool cloudEnabled;
// ESP8266: RAM only fits one link, so the cloud link replaces MQTT. ESP32 runs both.
#ifdef ESP8266
constexpr bool linkExclusive = true;
#else
constexpr bool linkExclusive = false;
#endif
inline bool mqttAllowed() { return !(linkExclusive && cloudEnabled); }
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
bool setupHA(); // false: not enough memory, retry on the next connect

// --- OTA (ota.cpp) ---
void startOTA();

// --- Web firmware self-update (web_update.cpp) ---
// Daily update check
void updateCheckTick();
void cloudUpdateTick(); // runs an update the cloud asked for
const String &updateAvailableVersion();
void handleGetUpdate();
void handleDoUpdate();
#if defined(UPDATE_SIGNING) && !defined(ESP8266)
// ESP32 /update upload: feed the uploaded file through these instead of Update.
// The signature trailer is held back, the image is hashed and flashed, and
// signedUploadEnd() only commits it when the RSA signature checks out.
bool signedUploadBegin();
bool signedUploadWrite(const uint8_t *buf, size_t n);
bool signedUploadEnd();
const char *signedUploadError(); // why the last upload was refused, "" if not
#endif

// --- Webhooks (webhooks.cpp) ---
// Enforce the optional webhook basic-auth credentials. Returns true if the
// request may proceed; otherwise it has already sent a 401 challenge.
bool checkWebhookAuth();
void handleWebhook();
void handleGetStates();
void handleGetTemps();
