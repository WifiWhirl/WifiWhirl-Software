#include "web/web.h"
#include "net/net.h"
#include <bearssl/bearssl.h>

extern "C" unsigned long os_random(void); // ESP8266 hardware RNG (osapi.h)

/*
 * Optional global UI authentication.
 *
 * Password is stored as PBKDF2-HMAC-SHA256(salt, password) (see hashPassword).
 * Hashing runs only on login and on password-save, so an iterated KDF is fine.
 * Sessions are random tokens kept in RAM only (a reboot logs everyone out).
 *
 * ponytail: 4-slot RAM token ring + single shared credential per device; persist
 * tokens or add per-user accounts only if that ever becomes a real need.
 */

static const uint32_t PBKDF2_ITERATIONS = 10000;
static const char *COOKIE_NAME = "WHIRL_SESSION";

#define SESSION_SLOTS 4
static String sessions[SESSION_SLOTS];
static uint8_t nextSlot = 0;

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

static size_t hexToBytes(const String &hex, uint8_t *out, size_t maxLen)
{
    size_t n = hex.length() / 2;
    if (n > maxLen)
        n = maxLen;
    for (size_t i = 0; i < n; i++)
    {
        char hi = hex[i * 2];
        char lo = hex[i * 2 + 1];
        auto nib = [](char c) -> uint8_t
        {
            if (c >= '0' && c <= '9')
                return c - '0';
            if (c >= 'a' && c <= 'f')
                return c - 'a' + 10;
            if (c >= 'A' && c <= 'F')
                return c - 'A' + 10;
            return 0;
        };
        out[i] = (nib(hi) << 4) | nib(lo);
    }
    return n;
}

// 16 random bytes from the hardware RNG, hex-encoded.
String makeSalt()
{
    uint8_t buf[16];
    for (size_t i = 0; i < sizeof(buf); i += 4)
    {
        uint32_t r = os_random();
        memcpy(buf + i, &r, 4);
    }
    return toHex(buf, sizeof(buf));
}

// PBKDF2-HMAC-SHA256, single output block (dkLen == hLen == 32). Returns hex.
String hashPassword(const String &saltHex, const String &password)
{
    uint8_t salt[16];
    size_t saltLen = hexToBytes(saltHex, salt, sizeof(salt));

    br_hmac_key_context kc;
    br_hmac_key_init(&kc, &br_sha256_vtable,
                     (const uint8_t *)password.c_str(), password.length());

    // U1 = HMAC(pwd, salt || INT(1))
    uint8_t u[32];
    uint8_t t[32];
    const uint8_t blockIndex[4] = {0, 0, 0, 1};
    {
        br_hmac_context hc;
        br_hmac_init(&hc, &kc, 0);
        br_hmac_update(&hc, salt, saltLen);
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

    return toHex(t, sizeof(t));
}

// constant-time-ish string compare (lengths already known/short)
static bool secureEquals(const String &a, const String &b)
{
    if (a.length() != b.length())
        return false;
    uint8_t diff = 0;
    for (size_t i = 0; i < a.length(); i++)
        diff |= (uint8_t)a[i] ^ (uint8_t)b[i];
    return diff == 0;
}

// --- sessions --------------------------------------------------------------

static String addSession()
{
    uint8_t buf[16];
    for (size_t i = 0; i < sizeof(buf); i += 4)
    {
        uint32_t r = os_random();
        memcpy(buf + i, &r, 4);
    }
    String tok = toHex(buf, sizeof(buf));
    sessions[nextSlot] = tok;
    nextSlot = (nextSlot + 1) % SESSION_SLOTS;
    return tok;
}

static void removeSession(const String &tok)
{
    if (tok.length() == 0)
        return;
    for (uint8_t i = 0; i < SESSION_SLOTS; i++)
        if (sessions[i] == tok)
            sessions[i] = "";
}

static bool validSession(const String &tok)
{
    if (tok.length() == 0)
        return false;
    for (uint8_t i = 0; i < SESSION_SLOTS; i++)
        if (sessions[i].length() > 0 && sessions[i] == tok)
            return true;
    return false;
}

// extract the WHIRL_SESSION value out of a raw Cookie header string
static String tokenFromCookieHeader(const String &cookie)
{
    String key = String(COOKIE_NAME) + "=";
    int start = cookie.indexOf(key);
    if (start < 0)
        return "";
    start += key.length();
    int end = cookie.indexOf(';', start);
    if (end < 0)
        end = cookie.length();
    String tok = cookie.substring(start, end);
    tok.trim();
    return tok;
}

static String cookieToken()
{
    return tokenFromCookieHeader(server->header("Cookie"));
}

// --- public guard / endpoints ---------------------------------------------

bool isAuthed()
{
    return !globalAuthEnabled || validSession(cookieToken());
}

// Wrap a handler so it 401s when global auth is on and the caller has no session.
// When auth is off this is a transparent pass-through (no behavior change).
ESP8266WebServer::THandlerFunction guard(ESP8266WebServer::THandlerFunction handler)
{
    return [handler]()
    {
        if (!isAuthed())
        {
            server->send(401, F("text/plain"), F("Authentication required."));
            return;
        }
        handler();
    };
}

// Auth for the OTA-password endpoints (/update GET, /support/, /debug-*).
// With global auth on, use the session (no extra Basic-Auth prompt); otherwise
// keep the legacy HTTP Basic Auth against OTAPassword. Returns false and has
// already answered the client when not authorized.
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
        server->requestAuthentication();
        return false;
    }
    return true;
}

void handleLogin()
{
    // If auth isn't enabled there is nothing to log into.
    if (!globalAuthEnabled)
    {
        server->sendHeader(F("Location"), F("/"));
        server->send(302, F("text/plain"), F(""));
        return;
    }

    String user = server->arg("user");
    String pwd = server->arg("pwd");

    bool ok = secureEquals(user, globalAuthUser) &&
              globalAuthHash.length() > 0 &&
              secureEquals(hashPassword(globalAuthSalt, pwd), globalAuthHash);

    if (!ok)
    {
        server->sendHeader(F("Location"), F("/login.html?e=1"));
        server->send(302, F("text/plain"), F(""));
        return;
    }

    String tok = addSession();
    // Not HttpOnly on purpose: function.js shows the Logout link when the cookie
    // is present. ponytail: fine on an LAN/no-TLS device; revisit if XSS matters.
    server->sendHeader(F("Set-Cookie"),
                       String(COOKIE_NAME) + "=" + tok + "; Path=/; SameSite=Strict");
    server->sendHeader(F("Location"), F("/"));
    server->send(302, F("text/plain"), F(""));
}

void handleLogout()
{
    removeSession(cookieToken());
    server->sendHeader(F("Set-Cookie"),
                       String(COOKIE_NAME) + "=; Path=/; Max-Age=0");
    server->sendHeader(F("Location"), F("/login.html"));
    server->send(302, F("text/plain"), F(""));
}

// WebSocket handshake validator. The library calls this for every non-standard
// header (Host, Origin, User-Agent, Cookie, ...) and rejects the handshake if
// any call returns false, so only the Cookie header is actually validated here.
// The mandatory-header list guarantees Cookie is present.
bool wsCookieValidator(String headerName, String headerValue)
{
    if (!headerName.equalsIgnoreCase("Cookie"))
        return true;
    return validSession(tokenFromCookieHeader(headerValue));
}
