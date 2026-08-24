#include "net/net.h"
#include "web/web.h"
#include "cloud_psk_storage.h"

/*
 * Per-device identity/secrets, stored in flash so a single generic firmware.
 *
 * Two layers, applied low -> high over the compiled defaults from config.cpp:
 *   /device.json   - provisioned "factory" base (incl. cloud), written once by
 *                    /provision/ or seeded from config.cpp on first boot. GUI
 *                    never touches it; survives factory reset.
 *   /devuser.json  - user overrides (hostname/apPwd/otaPwd, no cloud), written
 *                    by the GUI. Deleted on factory reset.
 */

// The compiled identity from config.cpp, snapshotted by loadDevice() before the
// flash layers overwrite the globals. This - never a MAC-derived name - is the
// fallback identity, so a generic firmware that carries no per-device config
// still finds the unit's name in /device.json.
static String compiledDeviceName, compiledApPwd, compiledOtaPwd;

// The factory hostname, read straight from the provisioned base. deviceName gets
// overwritten by the user layer (/devuser.json), so we can't use it for the cloud
// identity - re-read /device.json here. Empty when unprovisioned (open-source).
String cloudHostname()
{
    File base = LittleFS.open("/device.json", "r");
    if (!base)
        return String();
    StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, base);
    base.close();
    if (err || !doc.containsKey("hostname"))
        return String();
    return doc[F("hostname")].as<String>();
}

static bool isValidDeviceHostname(const String &name)
{
    if (name.length() == 0 || name.length() > 63 ||
        name[0] == '-' || name[name.length() - 1] == '-')
        return false;

    for (size_t i = 0; i < name.length(); i++)
    {
        char c = name[i];
        bool ok = (c >= 'A' && c <= 'Z') ||
                  (c >= 'a' && c <= 'z') ||
                  (c >= '0' && c <= '9') ||
                  c == '-';
        if (!ok)
            return false;
    }
    return true;
}

// Clamp a user-supplied wattage to a sane range (trust boundary).
static int clampWatt(int w, int minW = 0)
{
    if (w < minW)
        return minW;
    if (w > 5000)
        return 5000;
    return w;
}

static void powerToJson(JsonObject pwr, const Power &p)
{
    pwr[F("heater")] = p.HEATERPOWER;
    pwr[F("pump")] = p.PUMPPOWER;
    pwr[F("air")] = p.AIRPOWER;
    pwr[F("idle")] = p.IDLEPOWER;
    pwr[F("jet")] = p.JETPOWER;
}

static Power powerFromJson(JsonObject pwr, const Power &base)
{
    Power p = base;
    p.HEATERPOWER = clampWatt(pwr[F("heater")] | p.HEATERPOWER, 1);
    p.PUMPPOWER = clampWatt(pwr[F("pump")] | p.PUMPPOWER);
    p.AIRPOWER = clampWatt(pwr[F("air")] | p.AIRPOWER);
    p.IDLEPOWER = clampWatt(pwr[F("idle")] | p.IDLEPOWER);
    p.JETPOWER = clampWatt(pwr[F("jet")] | p.JETPOWER);
    return p;
}

static void applyDerived()
{
    wmApName = deviceName;
    netHostname = deviceName;
    OTAName = deviceName;
}

// Drop the GUI user overrides for hostname/apPwd/otaPwd and fall back to the
// provisioned base (/device.json) or, failing that, the compiled defaults.
// Auth/webhook settings are left untouched. Used when global auth is disabled.
static void revertIdentityToBase()
{
    deviceName = compiledDeviceName;
    wmApPassword = compiledApPwd;
    OTAPassword = compiledOtaPwd;

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
        }
        base.close();
    }
    applyDerived();
}

/**
 * Persist the provisioned base (/device.json): the factory identity.
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
    serializeJson(doc, file);
    file.close();
}

/**
 * Persist the user overrides (/devuser.json). No cloud fields.
 */
void saveDeviceUser()
{
    // Heap-allocated (not StaticJsonDocument on the stack): this is called from
    // the web handler which already holds a JSON doc on the ~4KB ESP8266 stack;
    // nesting two stack docs overflowed it and rebooted. See random-crash notes.
    // Sized for worst-case fields (63-char hostname/passwords, hex salt/hash,
    // webhook creds, power object); the overflowed() guard below is the backstop.
    DynamicJsonDocument doc(1536);
    doc[F("hostname")] = deviceName;
    doc[F("apPwd")] = wmApPassword;
    doc[F("otaPwd")] = OTAPassword;
    doc[F("authEnabled")] = globalAuthEnabled;
    doc[F("authUser")] = globalAuthUser;
    doc[F("authSalt")] = globalAuthSalt;
    doc[F("authHash")] = globalAuthHash;
    doc[F("webhookEnabled")] = webhookEnabled;
    doc[F("webhookAuthEnabled")] = webhookAuthEnabled;
    doc[F("webhookAuthUser")] = webhookAuthUser;
    doc[F("webhookAuthPassword")] = webhookAuthPassword;
    doc[F("webUpdateEnabled")] = webUpdateEnabled;
    doc[F("setupComplete")] = setupComplete;
    doc[F("expertMode")] = expertMode;
    if (bwc && bwc->cio)
    {
        Power p = bwc->cio->getPower();
        JsonObject pwr = doc.createNestedObject(F("power"));
        powerToJson(pwr, p);
    }

    // Build the doc BEFORE opening the file: open("w") truncates immediately, so
    // bailing after a successful open would leave a 0-byte/partial devuser.json
    // that fails to parse on the next boot and silently reverts ALL settings.
    if (doc.overflowed())
    {
        Serial.println(F("FS: devuser.json exceeds buffer, not saving (keeping previous)"));
        return;
    }

    File file = LittleFS.open("/devuser.json", "w");
    if (!file)
    {
        Serial.println(F("FS: Failed to open /devuser.json for write"));
        return;
    }
    serializeJson(doc, file);
    file.close();
}

/**
 * Factory/migration build (-DSEED_DEVICE_CONFIG): force this unit's compiled
 * config.cpp values over whatever is already in flash, then reflash the generic
 * binary. A no-op in every other build.
 *
 * Exists because a firmware flash does not erase LittleFS, so stale files from
 * an earlier provisioning outlive it and silently win: /cloud.json's host over
 * the good seed, and - the one that has no other cure - a CRC-valid but wrong
 * /cloudpsk, which ensurePSK() never replaces (it only fills a *missing* key),
 * so the unit fails the PoolLink handshake no matter how often it is reflashed.
 *
 * MUST be called before loadDevice()/loadCloudConfig(): every global still holds
 * its compiled config.cpp value at that point, so this just writes them out and
 * lets the normal load path read them back. Blank compiled values are skipped
 * rather than written, so an open-source build cannot wipe a provisioned unit.
 */
void seedDeviceConfig()
{
#ifdef SEED_DEVICE_CONFIG
    Serial.println(F("[Seed] SEED_DEVICE_CONFIG build: forcing compiled config.cpp into flash"));

    // `enabled` is the one cloud field the end user owns. Carry the persisted
    // value over, or a seed build would revert their toggle on every reboot and
    // the link could never be switched on.
    File existing = LittleFS.open("/cloud.json", "r");
    if (existing)
    {
        StaticJsonDocument<384> doc;
        if (!deserializeJson(doc, existing))
            cloudEnabled = doc[F("enabled")] | cloudEnabled;
        existing.close();
    }

    saveDevice();       // /device.json: hostname, AP/OTA pwd, weather API creds
    saveCloudConfig();  // /cloud.json:  enabled (carried over), host, port, webUrl

    if (cloudPskHex.length() == 0)
        Serial.println(F("[Seed] no compiled cloudPskHex - kept the stored PSK"));
    else if (CloudPSKStorage::writePSKFromHex(cloudPskHex.c_str()))
        Serial.println(F("[Seed] /cloudpsk rewritten from cloudPskHex"));
    else
        Serial.println(F("[Seed] cloudPskHex is not 64 valid hex chars - kept the stored PSK"));
#endif
}

/**
 * Load device identity/secrets at boot: compiled defaults, then the provisioned
 * base, then the user overrides.
 */
void loadDevice()
{
    // Compiled defaults: wmApPassword/OTAPassword/cloud* are already set from
    // config.cpp. loadMqtt() runs later, so the MQTT identity globals still hold
    // their compiled default here - snapshot it to detect "never customized".
    String mqttIdentityDefault = mqttClientId; // == mqttBaseTopic == DEVICE_NAME at boot

    // config.cpp is the identity of record. Snapshot it before the flash layers
    // overwrite the globals, so factory reset and the seeding below can fall back
    // to it instead of inventing a name.
    compiledDeviceName = deviceName;
    compiledApPwd = wmApPassword;
    compiledOtaPwd = OTAPassword;

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
        }
        base.close();
    }
    else
    {
#ifdef FACTORY_UNPROVISIONED
        // unseeded factory image - deliberately leave /device.json
        // absent. loadDevice() runs before startHttpServer(), so auto-seeding
        // here would make handleProvisionDevice() answer "Already provisioned."
        // on every reachable unit and POST /provision/ could never be used.
        // The globals keep their generic compiled config.cpp values until the
        // flashing tool provisions the unit, which then reboots into the branch
        // above. Seeded and field builds are unaffected and keep the 403 guard.
        Serial.println(F("FS: unprovisioned image, awaiting POST /provision/"));
#else
        // First boot: persist this unit's compiled config.cpp identity (name, AP
        // and OTA passwords, weather creds) to flash. From here on the identity
        // lives in the filesystem, so the unit can be updated forever with a
        // generic binary that carries no per-device config without losing its
        // name. /cloudpsk is seeded separately by ensurePSK() in setup().
        Serial.println(F("FS: /device.json not found, seeding from compiled defaults"));
        saveDevice();
        saveCloudConfig();
#endif
    }

    // User layer (overrides hostname/apPwd/otaPwd only)
    File user = LittleFS.open("/devuser.json", "r");
    if (user)
    {
        DynamicJsonDocument doc(1536); // must match saveDeviceUser's buffer
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
            if (doc.containsKey("webhookEnabled"))
                webhookEnabled = doc[F("webhookEnabled")].as<bool>();
            if (doc.containsKey("webhookAuthEnabled"))
                webhookAuthEnabled = doc[F("webhookAuthEnabled")].as<bool>();
            if (doc.containsKey("webhookAuthUser"))
                webhookAuthUser = doc[F("webhookAuthUser")].as<String>();
            if (doc.containsKey("webhookAuthPassword"))
                webhookAuthPassword = doc[F("webhookAuthPassword")].as<String>();
            if (doc.containsKey("webUpdateEnabled"))
                webUpdateEnabled = doc[F("webUpdateEnabled")].as<bool>();
            if (doc.containsKey("setupComplete"))
                setupComplete = doc[F("setupComplete")].as<bool>();
            if (doc.containsKey("expertMode"))
                expertMode = doc[F("expertMode")].as<bool>();
            // Apply stored wattage overrides only in expert mode; otherwise the
            // CIO keeps its compiled per-model defaults.
            if (expertMode && doc.containsKey("power") && bwc && bwc->cio)
            {
                JsonObject pwr = doc[F("power")];
                bwc->cio->setPower(powerFromJson(pwr, bwc->cio->getPower()));
            }
        }
        user.close();
    }

    // MQTT identity tracks the (unique) device name until the user customizes it,
    // so two devices on one broker don't collide on client id / base topic. A
    // persisted mqtt.json overrides these later in loadMqtt().
    if (mqttClientId == mqttIdentityDefault)
        mqttClientId = deviceName;
    if (mqttBaseTopic == mqttIdentityDefault)
        mqttBaseTopic = deviceName;

    applyDerived();
}

/**
 * Factory reset: drop ONLY the user overrides (/devuser.json).
 *
 * The provisioned identity must survive a factory reset, so this must never
 * touch /device.json (factory base: hostname + cloud API creds) or /cloudpsk
 * (the PoolLink cloud PSK). Because /device.json survives, /provision/ also
 * stays closed (it 403s once that file exists) - a reset never reopens it.
 */
void resetDeviceConfig()
{
    LittleFS.remove("/devuser.json");
}

/**
 * Backend-only factory provisioning: POST raw JSON with every per-device seed:
 *   {hostname, apPwd, otaPwd,                                 -> /device.json
 *    psk,                                                     -> /cloudpsk (64 hex)
 *    cloudEnabled, cloudHost, cloudPort, cloudWebUrl}         -> /cloud.json
 * Writes the base once; a device that already has /device.json answers 403.
 * All fields optional. Not linked in the frontend.
 *
 * `psk` is the per-device PoolLink cloud key; without it the cloud UI/link stay
 * hidden/inactive. The cloud* fields mirror config.cpp's seed for units flashed
 * with a generic build and provisioned over HTTP instead of a per-device config.
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

    StaticJsonDocument<768> doc;
    String message = getRequestBody();
    DeserializationError error = deserializeJson(doc, message);
    if (error)
    {
        server->send(400, F("text/plain"), F("Error deserializing message"));
        return;
    }

    if (doc.containsKey("hostname"))
    {
        String newHostname = doc[F("hostname")].as<String>();
        if (!isValidDeviceHostname(newHostname))
        {
            server->send(400, F("text/plain"), F("Invalid hostname"));
            return;
        }
        deviceName = newHostname;
    }
    if (doc.containsKey("apPwd"))
        wmApPassword = doc[F("apPwd")].as<String>();
    if (doc.containsKey("otaPwd"))
        OTAPassword = doc[F("otaPwd")].as<String>();
    // PoolLink cloud PSK → /cloudpsk (separate binary store). Validate and write
    // before saveDevice() so a bad key leaves the unit unprovisioned (retryable),
    // never with a /device.json but no matching PSK.
    if (doc.containsKey("psk"))
    {
        String pskHex = doc[F("psk")].as<String>();
        if (pskHex.length() != 64 || !CloudPSKStorage::writePSKFromHex(pskHex.c_str()))
        {
            server->send(400, F("text/plain"), F("PSK must be 64 hex chars"));
            return;
        }
    }

    // PoolLink cloud connection → /cloud.json. Validate up front so a bad value
    // aborts before any file is written.
    bool haveCloud = false;
    if (doc.containsKey("cloudPort"))
    {
        int p = doc[F("cloudPort")] | 0;
        if (p < 1 || p > 65535)
        {
            server->send(400, F("text/plain"), F("Invalid cloudPort"));
            return;
        }
        cloudPort = p;
        haveCloud = true;
    }
    if (doc.containsKey("cloudEnabled"))
    {
        cloudEnabled = doc[F("cloudEnabled")].as<bool>();
        haveCloud = true;
    }
    if (doc.containsKey("cloudHost"))
    {
        cloudHost = doc[F("cloudHost")].as<String>();
        haveCloud = true;
    }
    if (doc.containsKey("cloudWebUrl"))
    {
        cloudWebUrl = doc[F("cloudWebUrl")].as<String>();
        haveCloud = true;
    }

    saveDevice();
    if (haveCloud)
        saveCloudConfig();

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

    DynamicJsonDocument doc(1280);
    doc[F("hostname")] = deviceName;
    // Passwords are never sent to the client. The fields are omitted rather than
    // masked with a sentinel: the UI starts them blank and only submits one when
    // the user types a new value, so there is no magic string for the two sides
    // to agree on - and no UI text in the firmware, which could only ever be in
    // one language.
    if (!hidePasswords)
    {
        doc[F("apPwd")] = wmApPassword;
        doc[F("otaPwd")] = OTAPassword;
    }
    // expose auth state to the UI (never the salt/hash)
    doc[F("authEnabled")] = globalAuthEnabled;
    doc[F("authUser")] = globalAuthUser;
    doc[F("webhookEnabled")] = webhookEnabled;
    doc[F("webhookAuthEnabled")] = webhookAuthEnabled;
    doc[F("webhookAuthUser")] = webhookAuthUser;
    doc[F("webhookAuthConfigured")] = webhookAuthPassword.length() > 0;
    doc[F("webUpdateEnabled")] = webUpdateEnabled;
    doc[F("expertMode")] = expertMode;
    if (bwc && bwc->cio)
    {
        doc[F("hasjets")] = bwc->cio->getHasjets();
        Power p = bwc->cio->getPower();
        JsonObject pwr = doc.createNestedObject(F("power"));
        powerToJson(pwr, p);
        Power d = bwc->cio->getDefaultPower();
        JsonObject pwd = doc.createNestedObject(F("powerDefaults"));
        powerToJson(pwd, d);
    }

    // overflowed() catches a too-small pool: ArduinoJson silently DROPS members
    // on overflow (it does not fail serialization), so without this guard the
    // trailing power objects would vanish and the Expert page would see undefined
    // power instead of a clear error.
    if (doc.overflowed())
    {
        server->send(500, F("application/json"), F("{\"error\": \"device doc overflow\"}"));
        return;
    }

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

    DynamicJsonDocument doc(1024);
    String message = getRequestBody();
    DeserializationError error = deserializeJson(doc, message);
    if (error)
    {
        server->send(400, F("text/plain"), F("Error deserializing message"));
        return;
    }

    bool restartRequired = false;

    // Validate every fail-able input BEFORE the auth block runs: that block
    // rewrites credentials in RAM and clears sessions/cookie, so a 400 returned
    // from a later section (webhook/hostname) would skip saveDeviceUser() and
    // leave the admin locked out - RAM auth enabled, flash auth disabled. Reject
    // bad input up front so no mutation happens unless the whole request is valid.
    {
        bool wantWebhookEnabled = doc.containsKey("webhookEnabled")
                                      ? doc[F("webhookEnabled")].as<bool>()
                                      : webhookEnabled;
        bool wantWebhookAuth = doc.containsKey("webhookAuthEnabled")
                                   ? doc[F("webhookAuthEnabled")].as<bool>()
                                   : webhookAuthEnabled;
        if (wantWebhookEnabled && wantWebhookAuth)
        {
            String u = doc.containsKey("webhookAuthUser")
                           ? doc[F("webhookAuthUser")].as<String>()
                           : webhookAuthUser;
            bool havePwd = (doc.containsKey("webhookAuthPwd") &&
                            doc[F("webhookAuthPwd")].as<String>().length() > 0) ||
                           webhookAuthPassword.length() > 0;
            if (u.length() == 0)
            {
                server->send(400, F("text/plain"), F("Benutzername erforderlich zum Aktivieren der Webhook-Anmeldung."));
                return;
            }
            if (!havePwd)
            {
                server->send(400, F("text/plain"), F("Passwort erforderlich zum Aktivieren der Webhook-Anmeldung."));
                return;
            }
        }
        if (doc.containsKey("hostname") && !isValidDeviceHostname(doc[F("hostname")].as<String>()))
        {
            server->send(400, F("text/plain"), F("Invalid hostname"));
            return;
        }
        // Wattage overrides only apply in expert mode (effective state = this
        // request's value if it toggles it, else the current one). Reject up front
        // when power is sent but expert mode is off, so the client gets an error
        // instead of a "saved" for wattages the handler silently discards.
        bool effectiveExpert = doc.containsKey("expertMode")
                                   ? doc[F("expertMode")].as<bool>()
                                   : expertMode;
        if (doc.containsKey("power") && !effectiveExpert)
        {
            server->send(403, F("text/plain"), F("Expert mode disabled"));
            return;
        }
    }

    // --- Global auth changes (processed first; gates the device secrets below) ---
    if (doc.containsKey("authEnabled"))
    {
        bool wantEnabled = doc[F("authEnabled")].as<bool>();
        String newUser = doc.containsKey("authUser") ? doc[F("authUser")].as<String>() : globalAuthUser;
        bool havePwd = doc.containsKey("authPwd") &&
                       doc[F("authPwd")].as<String>().length() > 0;
        bool invalidatedSessions = false;

        // Turning auth ON from a disabled state is the one privileged transition
        // that isn't already session-authenticated (guard() is a pass-through
        // while auth is off, so any LAN client could otherwise seize the device).
        // Require the OTA password. Disabling or changing user/password while
        // already enabled is session-authenticated and needs nothing extra.
        // First-run setup (setupComplete still false, fresh from the SoftAP
        // portal) is the legitimate owner onboarding, who has no factory OTA
        // password yet - skip the gate then.
        if (wantEnabled && !globalAuthEnabled && setupComplete)
        {
            if (OTAPassword.length() == 0 ||
                doc[F("otaPwdConfirm")].as<String>() != OTAPassword)
            {
                server->send(403, F("text/plain"),
                             F("OTA-Passwort erforderlich zum Aktivieren der Anmeldung."));
                return;
            }
        }

        if (!wantEnabled)
        {
            // disable + wipe stored credential, and drop the user-defined
            // hostname/apPwd/otaPwd overrides (revert to provisioned base).
            globalAuthEnabled = false;
            globalAuthSalt = "";
            globalAuthHash = "";
            clearAuthSessions();
            invalidatedSessions = true;
            revertIdentityToBase();
        }
        else if (havePwd)
        {
            // enable (or rotate password): derive a fresh salt + hash
            globalAuthUser = newUser;
            globalAuthSalt = makeSalt();
            globalAuthHash = hashPassword(globalAuthSalt, doc[F("authPwd")].as<String>());
            if (globalAuthHash.length() == 0)
            {
                server->send(500, F("text/plain"), F("Failed to hash password"));
                return;
            }
            globalAuthEnabled = true;
            clearAuthSessions();
            invalidatedSessions = true;
        }
        else if (globalAuthHash.length() > 0)
        {
            // already configured: just keep enabled / update username
            if (newUser != globalAuthUser)
            {
                clearAuthSessions();
                invalidatedSessions = true;
            }
            globalAuthUser = newUser;
            globalAuthEnabled = true;
        }
        else
        {
            server->send(400, F("text/plain"), F("Passwort erforderlich zum Aktivieren der Anmeldung."));
            return;
        }

        if (invalidatedSessions)
            clearAuthCookie();
        restartRequired = true;
    }

    // --- Webhook Basic Auth (live setting; no reboot required) ---
    if (doc.containsKey("webhookEnabled") ||
        doc.containsKey("webhookAuthEnabled") ||
        doc.containsKey("webhookAuthUser") ||
        doc.containsKey("webhookAuthPwd"))
    {
        bool wantWebhookEnabled = doc.containsKey("webhookEnabled")
                                      ? doc[F("webhookEnabled")].as<bool>()
                                      : webhookEnabled;
        bool wantWebhookAuth = doc.containsKey("webhookAuthEnabled")
                                   ? doc[F("webhookAuthEnabled")].as<bool>()
                                   : webhookAuthEnabled;
        String newWebhookUser = doc.containsKey("webhookAuthUser")
                                    ? doc[F("webhookAuthUser")].as<String>()
                                    : webhookAuthUser;
        bool haveWebhookPwd = doc.containsKey("webhookAuthPwd") &&
                              doc[F("webhookAuthPwd")].as<String>().length() > 0;

        // Validity (user + password present when auth is on) was already checked
        // in the up-front pre-pass, so only the mutation runs here.
        webhookEnabled = wantWebhookEnabled;
        webhookAuthEnabled = wantWebhookAuth;
        webhookAuthUser = newWebhookUser;
        if (haveWebhookPwd)
            webhookAuthPassword = doc[F("webhookAuthPwd")].as<String>();
    }

    // --- Web self-update opt-in (live privacy setting; no reboot) ---
    if (doc.containsKey("webUpdateEnabled"))
        webUpdateEnabled = doc[F("webUpdateEnabled")].as<bool>();

    // --- First-run wizard completion (live; persisted below) ---
    if (doc.containsKey("setupComplete"))
        setupComplete = doc[F("setupComplete")].as<bool>();

    // --- Expert mode + wattage overrides (live; no reboot) ---
    if (doc.containsKey("expertMode"))
    {
        expertMode = doc[F("expertMode")].as<bool>();
        if (!expertMode && bwc && bwc->cio)
            bwc->cio->setPower(bwc->cio->getDefaultPower());
    }
    if (expertMode && doc.containsKey("power") && bwc && bwc->cio)
    {
        JsonObject pwr = doc[F("power")];
        bwc->cio->setPower(powerFromJson(pwr, bwc->cio->getPower()));
    }

    // Device identity/secrets are only configurable when global auth is enabled.
    if (globalAuthEnabled)
    {
        if (doc.containsKey("hostname"))
        {
            // hostname format was validated in the up-front pre-pass
            deviceName = doc[F("hostname")].as<String>();
            restartRequired = true;
        }
        // Present means "change it". The UI omits these unless the user typed a
        // new value, so an untouched form cannot overwrite the stored secret
        // (mirrors handleSetWifi).
        if (doc.containsKey("apPwd"))
        {
            wmApPassword = doc[F("apPwd")].as<String>();
            restartRequired = true;
        }
        if (doc.containsKey("otaPwd"))
        {
            OTAPassword = doc[F("otaPwd")].as<String>();
            restartRequired = true;
        }
    }

    saveDeviceUser();

    if (restartRequired)
    {
        server->send(200, F("application/json"),
                     F("{\"restart\":true,\"reason\":\"Geräteeinstellungen geändert. WifiWhirl startet neu.\"}"));
        delay(500);
        ESP.restart();
        return;
    }

    server->send(200, F("application/json"), F("{\"saved\":true}"));
}
