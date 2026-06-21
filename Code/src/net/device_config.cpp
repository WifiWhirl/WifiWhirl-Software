#include "net/net.h"
#include "web/web.h"

/*
 * Per-device identity/secrets, stored in flash so a single generic firmware.
 *
 * Two layers, applied low -> high over the compiled defaults from config.cpp:
 *   /device.json   - provisioned "factory" base (incl. cloud), written once by
 *                    /provision/ or the SEED_DEVICE_CONFIG build. GUI never
 *                    touches it; survives factory reset.
 *   /devuser.json  - user overrides (hostname/apPwd/otaPwd, no cloud), written
 *                    by the GUI. Deleted on factory reset.
 */

// wifiwhirl-<last 3 MAC octets>, e.g. "wifiwhirl-2D2701". The unique default
// name for an unprovisioned generic unit.
static String defaultDeviceName()
{
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char buf[7];
    snprintf(buf, sizeof(buf), "%02X%02X%02X", mac[3], mac[4], mac[5]);
    return String(F("wifiwhirl-")) + buf;
}

static void applyDerived()
{
    wmApName = deviceName;
    netHostname = deviceName;
    OTAName = deviceName;
}

/**
 * Persist the provisioned base (/device.json). Includes cloud credentials.
 */
void saveDevice()
{
    File file = LittleFS.open("/device.json", "w");
    if (!file)
    {
        Serial.println(F("FS: Failed to open /device.json for write"));
        return;
    }

    StaticJsonDocument<512> doc;
    doc[F("hostname")] = deviceName;
    doc[F("apPwd")] = wmApPassword;
    doc[F("otaPwd")] = OTAPassword;
    doc[F("cloudApi")] = cloudApi;
    doc[F("cloudApiKey")] = cloudApiKey;
    serializeJson(doc, file);
    file.close();
}

/**
 * Persist the user overrides (/devuser.json). No cloud fields.
 */
void saveDeviceUser()
{
    File file = LittleFS.open("/devuser.json", "w");
    if (!file)
    {
        Serial.println(F("FS: Failed to open /devuser.json for write"));
        return;
    }

    StaticJsonDocument<384> doc;
    doc[F("hostname")] = deviceName;
    doc[F("apPwd")] = wmApPassword;
    doc[F("otaPwd")] = OTAPassword;
    doc[F("authEnabled")] = globalAuthEnabled;
    doc[F("authUser")] = globalAuthUser;
    doc[F("authSalt")] = globalAuthSalt;
    doc[F("authHash")] = globalAuthHash;
    serializeJson(doc, file);
    file.close();
}

/**
 * Load device identity/secrets at boot: compiled defaults, then the provisioned
 * base, then the user overrides.
 */
void loadDevice()
{
    // Compiled defaults: wmApPassword/OTAPassword/cloud* are already set from
    // config.cpp. Derive a unique default name from the chip id.
    deviceName = defaultDeviceName();

    // Base layer
    File base = LittleFS.open("/device.json", "r");
    if (base)
    {
        StaticJsonDocument<512> doc;
        if (!deserializeJson(doc, base))
        {
            if (doc.containsKey("hostname"))
                deviceName = doc[F("hostname")].as<String>();
            if (doc.containsKey("apPwd"))
                wmApPassword = doc[F("apPwd")].as<String>();
            if (doc.containsKey("otaPwd"))
                OTAPassword = doc[F("otaPwd")].as<String>();
            if (doc.containsKey("cloudApi"))
                cloudApi = doc[F("cloudApi")].as<String>();
            if (doc.containsKey("cloudApiKey"))
                cloudApiKey = doc[F("cloudApiKey")].as<String>();
        }
        base.close();
    }
    else
    {
#ifdef SEED_DEVICE_CONFIG
        // One-time migration build: persist this customer's compiled defaults
        // as the provisioned base, then OTA the generic binary forever after.
        Serial.println(F("FS: seeding /device.json from compiled defaults"));
        saveDevice();
#else
        Serial.println(F("FS: /device.json not found, using defaults"));
#endif
    }

    // User layer (overrides hostname/apPwd/otaPwd only)
    File user = LittleFS.open("/devuser.json", "r");
    if (user)
    {
        StaticJsonDocument<384> doc;
        if (!deserializeJson(doc, user))
        {
            if (doc.containsKey("hostname"))
                deviceName = doc[F("hostname")].as<String>();
            if (doc.containsKey("apPwd"))
                wmApPassword = doc[F("apPwd")].as<String>();
            if (doc.containsKey("otaPwd"))
                OTAPassword = doc[F("otaPwd")].as<String>();
            if (doc.containsKey("authEnabled"))
                globalAuthEnabled = doc[F("authEnabled")].as<bool>();
            if (doc.containsKey("authUser"))
                globalAuthUser = doc[F("authUser")].as<String>();
            if (doc.containsKey("authSalt"))
                globalAuthSalt = doc[F("authSalt")].as<String>();
            if (doc.containsKey("authHash"))
                globalAuthHash = doc[F("authHash")].as<String>();
        }
        user.close();
    }

    applyDerived();
}

/**
 * Factory reset: drop the user overrides, keeping the provisioned base
 * (incl. cloud and provisioned hostname/psk) intact.
 */
void resetDeviceConfig()
{
    LittleFS.remove("/devuser.json");
}

/**
 * Backend-only factory provisioning: POST raw JSON
 * {hostname, apPwd, otaPwd, cloudApi, cloudApiKey}. Writes the base once; a
 * device that already has /device.json answers 403. Not linked in the frontend.
 */
void handleProvisionDevice()
{
    if (!checkHttpPost(server->method()))
        return;

    if (LittleFS.exists("/device.json"))
    {
        server->send(403, F("text/plain"), F("Already provisioned."));
        return;
    }

    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, server->arg(0));
    if (error)
    {
        server->send(400, F("text/plain"), F("Error deserializing message"));
        return;
    }

    if (doc.containsKey("hostname"))
        deviceName = doc[F("hostname")].as<String>();
    if (doc.containsKey("apPwd"))
        wmApPassword = doc[F("apPwd")].as<String>();
    if (doc.containsKey("otaPwd"))
        OTAPassword = doc[F("otaPwd")].as<String>();
    if (doc.containsKey("cloudApi"))
        cloudApi = doc[F("cloudApi")].as<String>();
    if (doc.containsKey("cloudApiKey"))
        cloudApiKey = doc[F("cloudApiKey")].as<String>();

    saveDevice();

    server->send(200, F("application/json"), F("{\"provisioned\":true}"));
    delay(500);
    ESP.restart();
}

/**
 * response for /getdevice/
 * Returns the effective (post-layering) hostname and masked credentials.
 */
void handleGetDevice()
{
    if (!checkHttpPost(server->method()))
        return;

    StaticJsonDocument<256> doc;
    doc[F("hostname")] = deviceName;
    if (hidePasswords)
    {
        doc[F("apPwd")] = F("<Passwort eingeben>");
        doc[F("otaPwd")] = F("<Passwort eingeben>");
    }
    else
    {
        doc[F("apPwd")] = wmApPassword;
        doc[F("otaPwd")] = OTAPassword;
    }
    // expose auth state to the UI (never the salt/hash)
    doc[F("authEnabled")] = globalAuthEnabled;
    doc[F("authUser")] = globalAuthUser;

    String json;
    if (serializeJson(doc, json) == 0)
    {
        json = F("{\"error\": \"Failed to serialize message\"}");
    }
    server->send(200, F("application/json"), json);
}

/**
 * response for /setdevice/
 * Partial-update the user overrides (hostname/apPwd/otaPwd). Allowed anytime;
 * never touches cloud credentials. Network identity change needs a reboot.
 */
void handleSetDevice()
{
    if (!checkHttpPost(server->method()))
        return;

    StaticJsonDocument<384> doc;
    DeserializationError error = deserializeJson(doc, server->arg(0));
    if (error)
    {
        server->send(400, F("text/plain"), F("Error deserializing message"));
        return;
    }

    String placeholder = F("<Passwort eingeben>");

    // --- Global auth changes (processed first; gates the device secrets below) ---
    if (doc.containsKey("authEnabled"))
    {
        bool wantEnabled = doc[F("authEnabled")].as<bool>();
        String newUser = doc.containsKey("authUser") ? doc[F("authUser")].as<String>() : globalAuthUser;
        bool havePwd = doc.containsKey("authPwd") &&
                       doc[F("authPwd")].as<String>().length() > 0 &&
                       doc[F("authPwd")].as<String>() != placeholder;

        if (!wantEnabled)
        {
            // disable + wipe stored credential
            globalAuthEnabled = false;
            globalAuthSalt = "";
            globalAuthHash = "";
        }
        else if (havePwd)
        {
            // enable (or rotate password): derive a fresh salt + hash
            globalAuthUser = newUser;
            globalAuthSalt = makeSalt();
            globalAuthHash = hashPassword(globalAuthSalt, doc[F("authPwd")].as<String>());
            globalAuthEnabled = true;
        }
        else if (globalAuthHash.length() > 0)
        {
            // already configured: just keep enabled / update username
            globalAuthUser = newUser;
            globalAuthEnabled = true;
        }
        else
        {
            server->send(400, F("text/plain"), F("Passwort erforderlich zum Aktivieren der Anmeldung."));
            return;
        }
    }

    // Device identity/secrets are only configurable when global auth is enabled.
    if (globalAuthEnabled)
    {
        if (doc.containsKey("hostname"))
            deviceName = doc[F("hostname")].as<String>();
        // skip placeholder-valued password fields so an unchanged masked field
        // doesn't overwrite the real password (mirrors handleSetWifi)
        if (doc.containsKey("apPwd") && doc[F("apPwd")].as<String>() != placeholder)
            wmApPassword = doc[F("apPwd")].as<String>();
        if (doc.containsKey("otaPwd") && doc[F("otaPwd")].as<String>() != placeholder)
            OTAPassword = doc[F("otaPwd")].as<String>();
    }

    saveDeviceUser();

    server->send(200, F("application/json"),
                 F("{\"restart\":true,\"reason\":\"Geräteeinstellungen geändert. WifiWhirl startet neu.\"}"));
    delay(500);
    ESP.restart();
}
