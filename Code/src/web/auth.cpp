#include "web/web.h"
#include "net/net.h"
#include "cloud_psk_storage.h"
#include <ArduinoJson.h>
#ifdef ESP8266
#include <bearssl/bearssl.h>
extern "C" unsigned long os_random(void); // ESP8266 hardware RNG (osapi.h)
#define RNG_WORD() ((uint32_t)os_random())
#else
#include <mbedtls/md.h>
#include <mbedtls/pkcs5.h>
#include <mbedtls/sha256.h>
#include <esp_random.h>
#define RNG_WORD() esp_random() // ESP32 hardware RNG
#endif

/*
 * Optional global UI authentication.
 *
 * Password is stored as PBKDF2-HMAC-SHA256(salt, password) (see hashPassword).
 * Hashing runs only on login and on password-save, so an iterated KDF is fine.
 * Sessions are random bearer tokens; the server keeps SHA-256(token), never the
 * cookie value itself. Normal sessions live in RAM only. "Keep me logged in"
 * sessions are persisted to LittleFS with a 30-day expiry.
 */

static const uint32_t PBKDF2_ITERATIONS = 10000;
static const uint32_t VALID_EPOCH_THRESHOLD = 57600;
static const uint32_t PERSISTENT_SESSION_SECONDS = 30UL * 24UL * 60UL * 60UL;
static const uint8_t LOGIN_THROTTLE_AFTER_FAILURES = 5;
static const uint32_t LOGIN_THROTTLE_COOLDOWN_MS = 30UL * 1000UL;
static const char *COOKIE_NAME = "WIFIWHIRL_AUTH";
static const char *SESSION_FILE = "/sessions.json";

#define SESSION_SLOTS 4
struct SessionSlot
{
    String tokenHash;
    uint32_t expiresAt;
    bool persistent;
};

static SessionSlot sessions[SESSION_SLOTS];
static uint8_t nextSlot = 0;
static bool sessionsLoaded = false;
// Login throttle per client: one global counter let any LAN host keep
// the owner locked out with 5 bad logins per cooldown.
// 4 slots, the least recently used one is recycled.
struct LoginThrottle
{
    uint32_t ip;
    uint8_t failedLoginCount;
    uint32_t loginBlockedUntilMs;
    uint32_t lastMs;
};
static LoginThrottle throttles[4];

static LoginThrottle &throttleFor(uint32_t ip)
{
    LoginThrottle *oldest = &throttles[0];
    for (LoginThrottle &t : throttles)
    {
        if (t.ip == ip)
            return t;
        if (t.lastMs < oldest->lastMs)
            oldest = &t;
    }
    *oldest = LoginThrottle{ip, 0, 0, millis()};
    return *oldest;
}

static uint32_t clientIp()
{
    return (uint32_t)server->client().remoteIP();
}

// --- helpers ---------------------------------------------------------------

static String toHex(const uint8_t *buf, size_t len)
{
    static const char *hex = "0123456789abcdef";
    String out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; i++)
    {
        out += hex[buf[i] >> 4];
        out += hex[buf[i] & 0x0f];
    }
    return out;
}

static int8_t hexNibble(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

static bool hexToBytesExact(const String &hex, uint8_t *out, size_t outLen)
{
    if (hex.length() != outLen * 2)
        return false;
    memset(out, 0, outLen);
    size_t n = outLen;
    for (size_t i = 0; i < n; i++)
    {
        int8_t hi = hexNibble(hex[i * 2]);
        int8_t lo = hexNibble(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0)
            return false;
        out[i] = ((uint8_t)hi << 4) | (uint8_t)lo;
    }
    return true;
}

// 16 random bytes from the hardware RNG, hex-encoded.
String makeSalt()
{
    uint8_t buf[16];
    for (size_t i = 0; i < sizeof(buf); i += 4)
    {
        uint32_t r = RNG_WORD();
        memcpy(buf + i, &r, 4);
    }
    return toHex(buf, sizeof(buf));
}

// PBKDF2-HMAC-SHA256, single output block (dkLen == hLen == 32). Returns hex.
String hashPassword(const String &saltHex, const String &password)
{
    uint8_t salt[16];
    if (!hexToBytesExact(saltHex, salt, sizeof(salt)))
    {
        Serial.println(F("AUTH: Invalid password salt"));
        return "";
    }
    uint8_t t[32];

#ifdef ESP8266
    br_hmac_key_context kc;
    br_hmac_key_init(&kc, &br_sha256_vtable,
                     (const uint8_t *)password.c_str(), password.length());

    // U1 = HMAC(pwd, salt || INT(1))
    uint8_t u[32];
    const uint8_t blockIndex[4] = {0, 0, 0, 1};
    {
        br_hmac_context hc;
        br_hmac_init(&hc, &kc, 0);
        br_hmac_update(&hc, salt, sizeof(salt));
        br_hmac_update(&hc, blockIndex, sizeof(blockIndex));
        br_hmac_out(&hc, u);
    }
    memcpy(t, u, sizeof(t));

    for (uint32_t i = 1; i < PBKDF2_ITERATIONS; i++)
    {
        br_hmac_context hc;
        br_hmac_init(&hc, &kc, 0);
        br_hmac_update(&hc, u, sizeof(u));
        br_hmac_out(&hc, u);
        for (size_t j = 0; j < sizeof(t); j++)
            t[j] ^= u[j];
        if ((i & 0xff) == 0)
            yield(); // feed the watchdog during the stretch
    }
#else
    // ESP32: mbedTLS provides PBKDF2-HMAC-SHA256 directly (same KDF/params).
    int rc = mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA256,
                                           (const uint8_t *)password.c_str(), password.length(),
                                           salt, sizeof(salt), PBKDF2_ITERATIONS, sizeof(t), t);
    if (rc != 0)
    {
        memset(t, 0, sizeof(t));
        Serial.println(F("AUTH: PBKDF2 failed"));
        return "";
    }
#endif

    return toHex(t, sizeof(t));
}

// constant-time-ish string compare (lengths already known/short)
bool secureEquals(const String &a, const String &b)
{
    if (a.length() != b.length())
        return false;
    uint8_t diff = 0;
    for (size_t i = 0; i < a.length(); i++)
        diff |= (uint8_t)a[i] ^ (uint8_t)b[i];
    return diff == 0;
}

// --- sessions --------------------------------------------------------------

static bool hasValidClock()
{
    return time(nullptr) > VALID_EPOCH_THRESHOLD;
}

static uint32_t nowEpoch()
{
    return (uint32_t)time(nullptr);
}

static void clearSlot(uint8_t i)
{
    sessions[i].tokenHash = "";
    sessions[i].expiresAt = 0;
    sessions[i].persistent = false;
}

// SHA-256 as hex; also hashes API keys (salt + key) in api_v1.cpp
String sha256Hex(const String &tok)
{
    uint8_t digest[32];
#ifdef ESP8266
    br_sha256_context sc;
    br_sha256_init(&sc);
    br_sha256_update(&sc, tok.c_str(), tok.length());
    br_sha256_out(&sc, digest);
#else
    mbedtls_sha256((const uint8_t *)tok.c_str(), tok.length(), digest, 0);
#endif
    return toHex(digest, sizeof(digest));
}

// Random token from [A-Za-z0-9] with the hardware RNG (API keys).
String randomToken(size_t len)
{
    static const char ALPHA[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    String out;
    out.reserve(len);
    while (out.length() < len)
    {
        uint32_t r = RNG_WORD();
        for (int i = 0; i < 4 && out.length() < len; i++, r >>= 8)
            if ((r & 0xFF) < 248) // 248 = 4 * 62: no modulo bias
                out += ALPHA[(r & 0xFF) % 62];
    }
    return out;
}

static bool slotExpired(const SessionSlot &slot)
{
    return slot.expiresAt > 0 && hasValidClock() && nowEpoch() >= slot.expiresAt;
}

static void savePersistentSessions()
{
    StaticJsonDocument<768> doc;
    JsonArray arr = doc.createNestedArray(F("sessions"));

    for (uint8_t i = 0; i < SESSION_SLOTS; i++)
    {
        if (sessions[i].tokenHash.length() == 0 || !sessions[i].persistent)
            continue;
        if (slotExpired(sessions[i]))
            continue;

        JsonObject item = arr.createNestedObject();
        item[F("h")] = sessions[i].tokenHash;
        item[F("e")] = sessions[i].expiresAt;
    }

    File file = LittleFS.open(SESSION_FILE, "w");
    if (!file)
    {
        Serial.println(F("FS: Failed to open /sessions.json for write"));
        return;
    }
    serializeJson(doc, file);
    file.close();
}

static void ensureSessionsLoaded()
{
    if (sessionsLoaded)
        return;
    sessionsLoaded = true;

    for (uint8_t i = 0; i < SESSION_SLOTS; i++)
        clearSlot(i);
    nextSlot = 0;

    File file = LittleFS.open(SESSION_FILE, "r");
    if (!file)
        return;

    StaticJsonDocument<768> doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    if (error)
    {
        Serial.println(F("FS: Failed to parse /sessions.json"));
        return;
    }

    JsonArray arr = doc[F("sessions")].as<JsonArray>();
    bool dirty = false;
    for (JsonObject item : arr)
    {
        if (nextSlot >= SESSION_SLOTS)
            break;
        String hash = item[F("h")].as<String>();
        uint32_t expiresAt = item[F("e")] | 0;
        if (hash.length() != 64 || expiresAt == 0)
        {
            dirty = true;
            continue;
        }
        sessions[nextSlot].tokenHash = hash;
        sessions[nextSlot].expiresAt = expiresAt;
        sessions[nextSlot].persistent = true;
        if (slotExpired(sessions[nextSlot]))
        {
            clearSlot(nextSlot);
            dirty = true;
            continue;
        }
        nextSlot++;
    }

    if (nextSlot >= SESSION_SLOTS)
        nextSlot = 0;
    if (dirty)
        savePersistentSessions();
}

static String addSession(bool keepLoggedIn)
{
    ensureSessionsLoaded();

    uint8_t buf[16];
    for (size_t i = 0; i < sizeof(buf); i += 4)
    {
        uint32_t r = RNG_WORD();
        memcpy(buf + i, &r, 4);
    }
    String tok = toHex(buf, sizeof(buf));
    bool persist = keepLoggedIn && hasValidClock(); // shared RNG_WORD() macro picks os_random/esp_random per platform
    bool replacedPersistent = sessions[nextSlot].persistent;
    sessions[nextSlot].tokenHash = sha256Hex(tok);
    sessions[nextSlot].expiresAt = persist ? nowEpoch() + PERSISTENT_SESSION_SECONDS : 0;
    sessions[nextSlot].persistent = persist;
    nextSlot = (nextSlot + 1) % SESSION_SLOTS;
    if (persist || replacedPersistent)
        savePersistentSessions();
    return tok;
}

static void removeSession(const String &tok)
{
    if (tok.length() == 0)
        return;
    ensureSessionsLoaded();
    String hash = sha256Hex(tok);
    bool changedPersistent = false;
    for (uint8_t i = 0; i < SESSION_SLOTS; i++)
    {
        if (sessions[i].tokenHash.length() > 0 && secureEquals(sessions[i].tokenHash, hash))
        {
            changedPersistent = changedPersistent || sessions[i].persistent;
            clearSlot(i);
        }
    }
    if (changedPersistent)
        savePersistentSessions();
}

static bool validSession(const String &tok)
{
    if (tok.length() == 0)
        return false;
    ensureSessionsLoaded();
    String hash = sha256Hex(tok);
    bool changedPersistent = false;
    for (uint8_t i = 0; i < SESSION_SLOTS; i++)
    {
        if (sessions[i].tokenHash.length() == 0)
            continue;
        if (slotExpired(sessions[i]))
        {
            changedPersistent = changedPersistent || sessions[i].persistent;
            clearSlot(i);
            continue;
        }
        if (secureEquals(sessions[i].tokenHash, hash))
        {
            if (sessions[i].persistent && !hasValidClock())
                return false;
            return true;
        }
    }
    if (changedPersistent)
        savePersistentSessions();
    return false;
}

void clearAuthSessions()
{
    sessionsLoaded = true;
    for (uint8_t i = 0; i < SESSION_SLOTS; i++)
        clearSlot(i);
    nextSlot = 0;
    LittleFS.remove(SESSION_FILE);
}

void clearAuthCookie()
{
    server->sendHeader(F("Set-Cookie"),
                       String(COOKIE_NAME) + "=; Path=/; Max-Age=0; HttpOnly; SameSite=Strict");
}

// extract the WIFIWHIRL_AUTH value out of a raw Cookie header string
static String tokenFromCookieHeader(const String &cookie)
{
    String key = String(COOKIE_NAME) + "=";
    int start = 0;
    while (start < (int)cookie.length())
    {
        int end = cookie.indexOf(';', start);
        if (end < 0)
            end = cookie.length();
        String part = cookie.substring(start, end);
        part.trim();
        if (part.startsWith(key))
        {
            String tok = part.substring(key.length());
            tok.trim();
            return tok;
        }
        start = end + 1;
    }
    return "";
}

static String cookieToken()
{
    return tokenFromCookieHeader(server->header("Cookie"));
}

static bool loginThrottleActive(LoginThrottle &t, uint32_t nowMs)
{
    return t.loginBlockedUntilMs != 0 && (int32_t)(nowMs - t.loginBlockedUntilMs) < 0;
}

static uint32_t loginRetryAfterSeconds(LoginThrottle &t, uint32_t nowMs)
{
    if (!loginThrottleActive(t, nowMs))
        return 0;
    return (t.loginBlockedUntilMs - nowMs + 999) / 1000;
}

static void recordFailedLogin(LoginThrottle &t)
{
    uint32_t nowMs = millis();
    // If a previous block has elapsed, start a fresh failure window. Otherwise the
    // count stays pinned at the threshold and every later single failure re-arms a
    // full cooldown -> one bad login per cooldown throttles logins forever.
    if (t.loginBlockedUntilMs != 0 && !loginThrottleActive(t, nowMs))
    {
        t.failedLoginCount = 0;
        t.loginBlockedUntilMs = 0;
    }
    if (t.failedLoginCount < 255)
        t.failedLoginCount++;
    if (t.failedLoginCount >= LOGIN_THROTTLE_AFTER_FAILURES)
        t.loginBlockedUntilMs = nowMs + LOGIN_THROTTLE_COOLDOWN_MS;
}

static void resetLoginThrottle(LoginThrottle &t)
{
    t.failedLoginCount = 0;
    t.loginBlockedUntilMs = 0;
}

// --- public guard / endpoints ---------------------------------------------

// HTTP Basic Auth for machine endpoints (/hook/, /api/v1/), throttled per
// client like /login so the password can't be guessed at full speed.
// Answers 401/429 itself and returns false when the caller may not continue.
// Same per-client throttle as /login for other credentials (Basic Auth, API
// keys). authAttemptAllowed() answers 429 itself while the client is blocked.
bool authAttemptAllowed()
{
    uint32_t nowMs = millis();
    LoginThrottle &t = throttleFor(clientIp());
    t.lastMs = nowMs;
    if (!loginThrottleActive(t, nowMs))
        return true;
    server->sendHeader(F("Retry-After"), String(loginRetryAfterSeconds(t, nowMs)));
    server->send(429, F("text/plain"), F("Too many failed attempts, try again later."));
    return false;
}

void authAttemptResult(bool ok)
{
    LoginThrottle &t = throttleFor(clientIp());
    if (ok)
        resetLoginThrottle(t);
    else
        recordFailedLogin(t);
}

bool basicAuthOk(const String &user, const String &pwd)
{
    if (!authAttemptAllowed())
        return false;
    if (!server->hasHeader(F("Authorization")))
    {
        server->requestAuthentication(); // first request without credentials: not a failure
        return false;
    }
    bool ok = server->authenticate(user.c_str(), pwd.c_str());
    authAttemptResult(ok);
    if (!ok)
        server->requestAuthentication();
    return ok;
}

bool isAuthed()
{
    return !globalAuthEnabled || validSession(cookieToken());
}

// A browser on another site (any page opened in the LAN) may send requests to
// the module; with auth off nothing stopped them. Browsers always send
// Origin on cross-site POSTs; same-origin requests carry our own host, and
// non-browser clients (curl, Home Assistant, scripts) send no Origin at all.
bool crossSiteRequest()
{
    if (server->method() == HTTP_GET || !server->hasHeader(F("Origin")))
        return false;
    String origin = server->header(F("Origin")); // "http://host[:port]" or "null"
    int scheme = origin.indexOf(F("://"));
    if (scheme < 0)
        return true;
    return !origin.substring(scheme + 3).equalsIgnoreCase(server->header(F("Host")));
}

// 401 when global auth is on and the caller has no session, 403 for cross-site
// browser requests. false = already answered.
bool guardOk()
{
    if (crossSiteRequest())
    {
        server->send(403, F("text/plain"), F("Cross-site request rejected."));
        return false;
    }
    if (!isAuthed())
    {
        server->send(401, F("text/plain"), F("Authentication required."));
        return false;
    }
    return true;
}

// Wrap a handler in guardOk(). Each use costs a heap std::function for good:
// plain routes go into PLAIN_ROUTES (http_routes.cpp) instead.
WebServerT::THandlerFunction guard(WebServerT::THandlerFunction handler)
{
    return [handler]()
    {
        if (guardOk())
            handler();
    };
}

// Auth for the OTA-password endpoints (/update GET, /support/, /debug-*).
// With global auth on, use the session (no extra Basic-Auth prompt); otherwise
// keep the legacy HTTP Basic Auth against OTAPassword. Returns false and has
// already answered the client when not authorized.
// 401 for wrong OTA credentials. The SPA always sends an Authorization header
// and asks for the password in its own dialog; a WWW-Authenticate challenge would
// pop up the browser's login box on top of it. A plain browser visit without
// credentials still gets the challenge.
void otaAuthFail()
{
    if (server->hasHeader(F("Authorization")))
        server->send(401, F("text/plain"), F("Wrong password."));
    else
        server->requestAuthentication();
}

bool legacyAuthOk(const char *basicUser)
{
    if (globalAuthEnabled)
    {
        if (!isAuthed())
        {
            server->send(401, F("text/plain"), F("Authentication required."));
            return false;
        }
        return true;
    }
    if (!server->authenticate(basicUser, OTAPassword.c_str()))
    {
        otaAuthFail();
        return false;
    }
    return true;
}

// The login UI lives in the SPA, so these endpoints answer with JSON for fetch()
// instead of redirecting.
void handleLogin()
{
    // If auth isn't enabled there is nothing to log into.
    if (!globalAuthEnabled)
    {
        server->send(200, F("application/json"), F("{\"ok\":true}"));
        return;
    }

    uint32_t nowMs = millis();
    LoginThrottle &throttle = throttleFor(clientIp());
    throttle.lastMs = nowMs;
    if (loginThrottleActive(throttle, nowMs))
    {
        uint32_t retryAfter = loginRetryAfterSeconds(throttle, nowMs);
        server->sendHeader(F("Retry-After"), String(retryAfter));
        server->send(429, F("application/json"), String(F("{\"ok\":false,\"retryAfter\":")) + retryAfter + "}");
        return;
    }

    String user = server->arg("user");
    String pwd = server->arg("pwd");
    String keep = server->arg("keep");

    // Username is case-insensitive; password stays exact.
    String userLc = user;
    userLc.toLowerCase();
    String authUserLc = globalAuthUser;
    authUserLc.toLowerCase();

    // Hash first, always: with "user && hash" PBKDF2 (hundreds of ms) only ran
    // for the right user name, so the response time told which name is valid.
    String pwdHash = hashPassword(globalAuthSalt, pwd);
    bool userOk = secureEquals(userLc, authUserLc);
    bool ok = userOk && globalAuthHash.length() > 0 && secureEquals(pwdHash, globalAuthHash);

    if (!ok)
    {
        recordFailedLogin(throttle);
        server->send(401, F("application/json"), F("{\"ok\":false}"));
        return;
    }

    resetLoginThrottle(throttle);
    bool keepLoggedIn = keep == F("1") || keep == F("true") || keep == F("on");
    String tok = addSession(keepLoggedIn);
    String cookie = String(COOKIE_NAME) + "=" + tok + "; Path=/; HttpOnly; SameSite=Strict";
    if (keepLoggedIn && hasValidClock())
        cookie += String(F("; Max-Age=")) + PERSISTENT_SESSION_SECONDS;
    server->sendHeader(F("Set-Cookie"),
                       cookie);
    server->send(200, F("application/json"), F("{\"ok\":true}"));
}

void handleAuthStatus()
{
    // cloudAvailable gates the whole cloud UI: true only when a PSK is provisioned
    // (preseeded units). Open-source flashes have none, so the SPA hides cloud.
    uint8_t psk[32];
    bool cloudAvailable = CloudPSKStorage::readPSK(psk);
    String response = String(F("{\"enabled\":")) + (globalAuthEnabled ? F("true") : F("false")) +
                      F(",\"authed\":") + (isAuthed() ? F("true") : F("false")) +
                      F(",\"apMode\":") + (apSetupMode ? F("true") : F("false")) +
                      F(",\"setupComplete\":") + (setupComplete ? F("true") : F("false")) +
                      F(",\"expertMode\":") + (expertMode ? F("true") : F("false")) +
                      F(",\"cloudAvailable\":") + (cloudAvailable ? F("true") : F("false")) + "}";
    server->send(200, F("application/json"), response);
}

void handleLogout()
{
    removeSession(cookieToken());
    clearAuthCookie();
    server->send(200, F("application/json"), F("{\"ok\":true}"));
}
