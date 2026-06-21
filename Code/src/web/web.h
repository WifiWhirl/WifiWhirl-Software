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

// --- Global authentication (auth.cpp) ---
String makeSalt();
String hashPassword(const String &saltHex, const String &password);
bool isAuthed();
bool legacyAuthOk(const char *basicUser);
ESP8266WebServer::THandlerFunction guard(ESP8266WebServer::THandlerFunction handler);
void handleLogin();
void handleLogout();
bool wsCookieValidator(String headerName, String headerValue);
