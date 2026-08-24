#pragma once

#include "main.h"

// --- HTTP server / routes (http_routes.cpp) ---
void startHttpServer();
void handleNotFound();
bool checkHttpPost(HTTPMethod method);
bool checkHttpGet(HTTPMethod method);
String getRequestBody();

// --- File server (file_server.cpp) ---
String getContentType(const String &filename);
bool handleFileRead(String path);

// --- Live status JSON (api_commands.cpp) ---
void getOtherInfo(String &rtn);

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
WebServerT::THandlerFunction guard(WebServerT::THandlerFunction handler);
void clearAuthSessions();
void clearAuthCookie();
void handleAuthStatus();
void handleLogin();
void handleLogout();
