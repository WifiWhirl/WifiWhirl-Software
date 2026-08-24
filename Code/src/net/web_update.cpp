#include "net/net.h"
#include "web/web.h"
#include "config.h"
#include <ArduinoJson.h>

/*
 * Device-initiated firmware self-update.
 *
 * Opt-in (webUpdateEnabled, off by default): a privacy setting, so a device
 * only ever phones home when the user explicitly enables it.
 *
 * Flow: GET a small manifest JSON ({version,url}) from webUpdateUrl, refuse a
 * downgrade, then stream the .bin over plain HTTP straight into Update. Plain
 * HTTP is fine because the image is signed; the signature is the trust anchor,
 * not the transport.
 *
 * Signature verification:
 *   - ESP8266: the core's Update verifier checks the appended RSA trailer at
 *     Update.end() once installSignature() is set up. We just feed it the bytes.
 *   - ESP32:   the Arduino Update lib has no such hook, so we hash the payload
 *     with mbedTLS and RSA-verify the trailer ourselves before Update.end().
 */

#ifdef UPDATE_SIGNING
#include "update_pubkey.h"
#define UPDATE_SIG_LEN 256 // RSA-2048; sign.py / public.key must match
#ifdef ESP8266
#include <BearSSLHelpers.h>
#else
#include <mbedtls/sha256.h>
#include <mbedtls/pk.h>
#include <mbedtls/md.h>
#endif
#endif

// Abandon an in-progress flash. ESP8266's Updater has no abort(); end() resets
// its state (and won't activate a boot slot for an incomplete image).
static void abortUpdate()
{
#ifdef ESP8266
    Update.end();
#else
    Update.abort();
#endif
}

// --- version compare: parse the leading X.Y.Z, ignore any suffix (_beta etc.) ---
static void parseVer(const String &v, int out[3])
{
    out[0] = out[1] = out[2] = 0;
    sscanf(v.c_str(), "%d.%d.%d", &out[0], &out[1], &out[2]);
}

// returns >0 if a newer than b, <0 if older, 0 if equal
static int cmpVer(const String &a, const String &b)
{
    int x[3], y[3];
    parseVer(a, x);
    parseVer(b, y);
    for (int i = 0; i < 3; i++)
        if (x[i] != y[i])
            return x[i] < y[i] ? -1 : 1;
    return 0;
}

// GET <webUpdateUrl>/<env>.json -> {version, url}. Caller owns the phone-home gate.
static bool fetchManifest(String &version, String &binUrl, String &err)
{
    String url = webUpdateUrl + "/" + PIO_ENV_NAME + ".json";
    WiFiClient client;
    HTTPClient http;
    if (!http.begin(client, url))
    {
        err = F("begin");
        return false;
    }
    int code = http.GET();
    if (code != HTTP_CODE_OK)
    {
        err = String(F("http ")) + code;
        http.end();
        return false;
    }
    StaticJsonDocument<384> doc;
    DeserializationError e = deserializeJson(doc, http.getStream());
    http.end();
    if (e)
    {
        err = F("json");
        return false;
    }
    version = doc[F("version")].as<String>();
    binUrl = doc[F("url")].as<String>();
    if (!version.length() || !binUrl.length())
    {
        err = F("incomplete manifest");
        return false;
    }
    return true;
}

#if defined(UPDATE_SIGNING) && defined(ESP8266)
// Install the BearSSL RSA verifier once; the core verifies the appended
// signature trailer inside Update.end() for any transport that feeds it.
static void installUpdateSignature()
{
    static BearSSL::PublicKey *pub = nullptr;
    static BearSSL::HashSHA256 *hash = nullptr;
    static BearSSL::SigningVerifier *verifier = nullptr;
    if (!pub)
    {
        pub = new BearSSL::PublicKey(UPDATE_PUBKEY_PEM);
        hash = new BearSSL::HashSHA256();
        verifier = new BearSSL::SigningVerifier(pub);
    }
    Update.installSignature(hash, verifier);
}
#endif

#if defined(UPDATE_SIGNING) && !defined(ESP8266)
static bool verifyRsa(const uint8_t *digest, const uint8_t *sig, size_t sigLen)
{
    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);
    int r = mbedtls_pk_parse_public_key(
        &pk, (const uint8_t *)UPDATE_PUBKEY_PEM, strlen(UPDATE_PUBKEY_PEM) + 1);
    if (r == 0)
        r = mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, digest, 32, sig, sigLen);
    mbedtls_pk_free(&pk);
    return r == 0;
}
#endif

// Download + verify + flash. Returns true on success (caller then reboots).
static bool performWebUpdate(const String &url, String &err)
{
    WiFiClient client;
    HTTPClient http;
    if (!http.begin(client, url))
    {
        err = F("begin");
        return false;
    }
    int code = http.GET();
    if (code != HTTP_CODE_OK)
    {
        err = String(F("http ")) + code;
        http.end();
        return false;
    }
    int total = http.getSize();
    if (total <= 0)
    {
        err = F("no content-length");
        http.end();
        return false;
    }

    int imageLen = total;   // payload bytes (excludes signature trailer)
    size_t beginSize = total;
#ifdef UPDATE_SIGNING
    const int kTrailer = UPDATE_SIG_LEN + 4;
    if (total <= kTrailer)
    {
        err = F("too small");
        http.end();
        return false;
    }
    imageLen = total - kTrailer;
#ifdef ESP8266
    installUpdateSignature(); // core strips + verifies the trailer at end()
    beginSize = total;
#else
    beginSize = imageLen;     // ESP32: we strip the trailer ourselves
#endif
#endif

    stopall();
    if (!Update.begin(beginSize))
    {
        err = String(F("begin ")) + Update.getError();
        http.end();
        return false;
    }

#if defined(UPDATE_SIGNING) && !defined(ESP8266)
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    if (mbedtls_sha256_starts(&sha, 0) != 0)
    {
        abortUpdate();
        http.end();
        err = F("sha init");
        return false;
    }
    uint8_t sig[UPDATE_SIG_LEN];
    uint8_t lenbuf[4] = {0};
#endif

    WiFiClient *stream = http.getStreamPtr();
    uint8_t buf[512];
    int pos = 0;
    unsigned long lastData = millis();
    bool failed = false;

    while (pos < total)
    {
        int avail = stream->available();
        if (avail <= 0)
        {
            if (!http.connected())
            {
                err = F("disconnected");
                failed = true;
                break;
            }
            if (millis() - lastData > 15000)
            {
                err = F("timeout");
                failed = true;
                break;
            }
            delay(1);
            continue;
        }
        lastData = millis();
        int want = avail;
        if (want > (int)sizeof(buf))
            want = sizeof(buf);
        if (want > total - pos)
            want = total - pos;
        int n = stream->readBytes(buf, want);
        if (n <= 0)
            continue;

        // Split this chunk at the image/trailer boundary.
        int imgBytes = 0;
        if (pos < imageLen)
        {
            imgBytes = n;
            if (imgBytes > imageLen - pos)
                imgBytes = imageLen - pos;
            if ((int)Update.write(buf, imgBytes) != imgBytes)
            {
                err = F("flash write");
                failed = true;
                break;
            }
#if defined(UPDATE_SIGNING) && !defined(ESP8266)
            mbedtls_sha256_update(&sha, buf, imgBytes);
#endif
        }
#ifdef UPDATE_SIGNING
#ifdef ESP8266
        // 8266: the trailer is part of what the core hashes+verifies; flash it too.
        if (n > imgBytes)
        {
            if ((int)Update.write(buf + imgBytes, n - imgBytes) != n - imgBytes)
            {
                err = F("flash write");
                failed = true;
                break;
            }
        }
#else
        // ESP32: collect the trailer (signature || u32 len); never write it to flash.
        for (int i = imgBytes; i < n; i++)
        {
            int t = (pos + i) - imageLen; // 0 .. kTrailer-1
            if (t < UPDATE_SIG_LEN)
                sig[t] = buf[i];
            else
                lenbuf[t - UPDATE_SIG_LEN] = buf[i];
        }
#endif
#endif
        pos += n;
        yield();
    }
    http.end();

    if (!failed && pos != total)
    {
        err = F("short read");
        failed = true;
    }
    if (failed)
    {
        abortUpdate();
#if defined(UPDATE_SIGNING) && !defined(ESP8266)
        mbedtls_sha256_free(&sha);
#endif
        return false;
    }

#if defined(UPDATE_SIGNING) && !defined(ESP8266)
    uint8_t digest[32];
    mbedtls_sha256_finish(&sha, digest);
    mbedtls_sha256_free(&sha);
    uint32_t sigLen = (uint32_t)lenbuf[0] | ((uint32_t)lenbuf[1] << 8) |
                      ((uint32_t)lenbuf[2] << 16) | ((uint32_t)lenbuf[3] << 24);
    if (sigLen != UPDATE_SIG_LEN)
    {
        abortUpdate();
        err = F("bad sig length");
        return false;
    }
    if (!verifyRsa(digest, sig, sigLen))
    {
        abortUpdate();
        err = F("signature");
        return false;
    }
#endif

    if (!Update.end(true)) // ESP8266: this is where the trailer signature is verified
    {
        err = String(F("end ")) + Update.getError();
        return false;
    }
    return true;
}

// --- daily check cache -----------------------------------------------------
// Result of the last manifest check. The nav badge and the Info page read this
// instead of blocking on the network, so a page load costs no HTTP round-trip.
static String cachedLatest;
static bool cachedAvailable = false;
static uint32_t nextCheckMs = 0;
static bool checkDue = true; // force a check on the first tick after enabling

// fixed interval, no config knob. A spa firmware release is not an
// hourly event
static const uint32_t kCheckOkMs = 24UL * 60 * 60 * 1000;
static const uint32_t kCheckErrMs = 60UL * 60 * 1000; // retry sooner after a failure

// Refresh the cache from the manifest server and arm the next deadline.
// A failed check keeps the previous cache and retries in kCheckErrMs.
static bool runCheck(String &err)
{
    String latest, binUrl;
    bool ok = fetchManifest(latest, binUrl, err);
    if (ok)
    {
        cachedLatest = latest;
        cachedAvailable = cmpVer(latest, FW_VERSION) > 0;
    }
    checkDue = false;
    nextCheckMs = millis() + (ok ? kCheckOkMs : kCheckErrMs);
    return ok;
}

/**
 * Called from the 60 s periodic tick in loop() while WiFi is up. Does at most
 * one HTTP GET per day.
 */
void updateCheckTick()
{
    if (!webUpdateEnabled)
    {
        // Disabled again: forget what we knew so the badge clears.
        cachedLatest = "";
        cachedAvailable = false;
        checkDue = true;
        return;
    }
    if (!checkDue && (int32_t)(millis() - nextCheckMs) < 0)
        return;
    String err;
    runCheck(err);
}

/**
 * Latest known *newer* version, or "" when we are up to date or have not
 * checked yet. Feeds the NEWFW field of the OTHER poll payload.
 *
 * Also gated on webUpdateEnabled so switching the check off clears the badge at
 * once, rather than at the next tick up to a minute later.
 */
const String &updateAvailableVersion()
{
    static const String kNone;
    return (webUpdateEnabled && cachedAvailable) ? cachedLatest : kNone;
}

/**
 * POST /getupdate/ -> {enabled, current[, latest, available][, error]}.
 *
 * Answers from the daily-check cache by default, so opening the Info page costs
 * nothing. Body {"force":true} (the "Check now" button) checks right away.
 * Only contacts the manifest server when web update is enabled (privacy).
 */
void handleGetUpdate()
{
    if (!checkHttpPost(server->method()))
        return;

    bool force = false;
    String body = getRequestBody();
    if (body.length())
    {
        StaticJsonDocument<64> in;
        if (!deserializeJson(in, body))
            force = in[F("force")].as<bool>();
    }

    StaticJsonDocument<256> out;
    out[F("enabled")] = webUpdateEnabled;
    out[F("current")] = FW_VERSION;
    if (webUpdateEnabled)
    {
        if (force)
        {
            String err;
            if (!runCheck(err))
                out[F("error")] = err;
        }
        if (cachedLatest.length())
        {
            out[F("latest")] = cachedLatest;
            out[F("available")] = cachedAvailable;
        }
    }
    String json;
    serializeJson(out, json);
    server->send(200, F("application/json"), json);
}

/**
 * POST /doupdate/ : check manifest, refuse downgrade, download+verify+flash,
 * then reboot. Requires web update to be enabled.
 */
void handleDoUpdate()
{
    if (!checkHttpPost(server->method()))
        return;
    if (!webUpdateEnabled)
    {
        server->send(403, F("text/plain"), F("Web update disabled"));
        return;
    }

    String latest, binUrl, err;
    if (!fetchManifest(latest, binUrl, err))
    {
        server->send(502, F("text/plain"), String(F("manifest: ")) + err);
        return;
    }
    // Refuse downgrade/equal
    if (cmpVer(latest, FW_VERSION) <= 0)
    {
        server->send(409, F("text/plain"), F("No newer signed firmware available"));
        return;
    }

    if (!performWebUpdate(binUrl, err))
    {
        server->send(500, F("text/plain"), String(F("update failed: ")) + err);
        return;
    }

    server->send(200, F("application/json"), F("{\"updated\":true}"));
    delay(500);
    ESP.restart();
}
