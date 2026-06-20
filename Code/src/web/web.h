#pragma once

#include "main.h"

// --- HTTP server / routes (http_routes.cpp) ---
void startHttpServer();
void handleNotFound();
bool checkHttpPost(HTTPMethod method);
bool checkHttpGet(HTTPMethod method);

// --- File server (file_server.cpp) ---
String getContentType(const String &filename);
bool handleFileRead(String path);

// --- WebSocket (websocket.cpp) ---
void sendWS();
void getOtherInfo(String &rtn);
void startWebSocket();
void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t len);

// --- Web config (web_config.cpp) ---
void loadWebConfig();
void saveWebConfig();
void handleGetWebConfig();
void handleSetWebConfig();
