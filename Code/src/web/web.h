#pragma once

#include "main.h"

// --- HTTP server / routes (http_routes.cpp) ---
void startHttpServer();
void handleNotFound();
bool checkHttpPost(HTTPMethod method);
bool checkHttpGet(HTTPMethod method);
// ESP8266: a reference to the server's own String, no copy of the body.
// ESP32's WebServer returns args by value, so there it stays a copy.
// Bind it as `const String &body = getRequestBody();` on both.
#ifdef ESP8266
typedef const String &RequestBody;
#else
typedef String RequestBody;
#endif
RequestBody getRequestBody();

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
bool crossSiteRequest();
bool basicAuthOk(const String &user, const String &pwd);
bool authAttemptAllowed();
void authAttemptResult(bool ok);
bool secureEquals(const String &a, const String &b);
String sha256Hex(const String &data);
String randomToken(size_t len);
bool legacyAuthOk(const char *basicUser);
void otaAuthFail();
bool guardOk();
WebServerT::THandlerFunction guard(WebServerT::THandlerFunction handler);
void clearAuthSessions();
void clearAuthCookie();
void handleAuthStatus();
void handleLogin();
void handleLogout();
