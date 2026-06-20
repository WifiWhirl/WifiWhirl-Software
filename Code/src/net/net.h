#pragma once

#include "main.h"

// --- WiFi (wifi_manager.cpp) ---
void startWiFi();
void startWiFiConfigPortal(const String &storedSsid = "", const String &storedPwd = "");
void startNTP();
sWifi_info loadWifi();
void saveWifi(const sWifi_info &wifi_info);
void handleGetWifi();
void handleSetWifi();
void handleScanWifi();
void handleResetWifi();
void resetWiFi();

// --- MQTT (mqtt_manager.cpp) ---
void sendMQTT();
void loadMqtt();
void saveMqtt();
void handleGetMqtt();
void handleSetMqtt();
void startMqtt();
void mqttCallback(char *topic, byte *payload, unsigned int length);
void mqttConnect();
const String& getMqttTopicButton();

// --- Home Assistant (homeassistant.cpp) ---
void setupHA();

// --- OTA (ota.cpp) ---
void startOTA();

// --- Weather (weather.cpp) ---
String queryAmbientTemperature();
void handleGetWeather();

// --- Webhooks (webhooks.cpp) ---
void handleWebhook();
void handleGetStates();
void handleGetTemps();
