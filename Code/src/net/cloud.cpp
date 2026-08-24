#include "main.h"
#include "net/net.h"
#include "web/web.h"
#include "cloud_task.h"
#include "cloud_crypto.h"
#include "cloud_psk_storage.h"
#include "cloud_protocol.h"

// Runtime cloud settings are SEEDED in config.cpp (per device) and defined
// there. /cloud.json only persists the end-user-toggleable `enabled` flag; the
// connection details (host/port/webUrl) always come from the seed. The PSK
// lives separately in /cloudpsk (cloud_psk_storage), seeded from cloudPskHex.

void loadCloudConfig()
{
    File file = LittleFS.open("/cloud.json", "r");
    if (!file)
    {
        Serial.println(F("FS: /cloud.json not found, using seeded defaults"));
        return;
    }
    StaticJsonDocument<384> doc;
    DeserializationError err = deserializeJson(doc, file);
    file.close();
    if (err)
        return;

    // Only `enabled` is user-mutable and persisted; fall back to the seed. The
    // rest stay on their config.cpp seed values (host/port/webUrl are read
    // opportunistically for units provisioned via the older /cloud.json path).
    //
    // A persisted value only wins when it is actually usable. A unit first
    // booted on a build with blank cloud seeds (config.cpp.dist) wrote
    // host/webUrl as "" here, and a firmware flash does not erase LittleFS - so
    // without these length checks that empty string outlived the reflash and
    // clobbered a good compiled seed, leaving the Cloud page's Server field on
    // "-" and CloudTask connecting to nowhere. Port already had this guard.
    cloudEnabled = doc[F("enabled")] | cloudEnabled;
    if (doc.containsKey(F("host")) && doc[F("host")].as<String>().length() > 0)
        cloudHost = doc[F("host")].as<String>();
    if (doc.containsKey(F("port")))
    {
        int p = doc[F("port")] | cloudPort;
        if (p >= 1 && p <= 65535)
            cloudPort = p;
    }
    if (doc.containsKey(F("webUrl")) && doc[F("webUrl")].as<String>().length() > 0)
        cloudWebUrl = doc[F("webUrl")].as<String>();
}

void saveCloudConfig()
{
    File file = LittleFS.open("/cloud.json", "w");
    if (!file)
    {
        Serial.println(F("FS: Failed to open /cloud.json for write"));
        return;
    }
    StaticJsonDocument<384> doc;
    doc[F("enabled")] = cloudEnabled;
    doc[F("host")] = cloudHost;
    doc[F("port")] = cloudPort;
    doc[F("webUrl")] = cloudWebUrl;
    serializeJson(doc, file);
    file.close();
}

/** response for /getcloud/ - current cloud settings + link state */
void handleGetCloud()
{
    if (!checkHttpPost(server->method()))
        return;

    StaticJsonDocument<384> doc;
    doc[F("enabled")] = cloudEnabled;
    doc[F("host")] = cloudHost;
    doc[F("port")] = cloudPort;
    doc[F("webUrl")] = cloudWebUrl;
    doc[F("state")] = cloudEnabled ? CloudTask::getStateName() : "disabled";
    // Tell the UI whether a PSK is provisioned without ever exposing it.
    uint8_t psk[32];
    doc[F("hasPsk")] = CloudPSKStorage::readPSK(psk);

    String json;
    if (serializeJson(doc, json) == 0)
        json = F("{\"error\": \"Failed to serialize message\"}");
    server->send(200, F("application/json"), json);
}

/** response for /setcloud/ - end user may ONLY toggle `enabled`. All connection
 * details (PSK, host, port, webUrl) are provisioned via /provision/ or preseeded
 * firmware and are never accepted here. */
void handleSetCloud()
{
    if (!checkHttpPost(server->method()))
        return;

    StaticJsonDocument<128> doc;
    DeserializationError err = deserializeJson(doc, getRequestBody());
    if (err)
    {
        server->send(400, F("text/plain"), F("Error deserializing message"));
        return;
    }

    if (!doc.containsKey(F("enabled")) || !doc[F("enabled")].is<bool>())
    {
        server->send(400, F("text/plain"), F("Invalid cloud enable flag"));
        return;
    }

    bool wantEnabled = doc[F("enabled")];
    if (wantEnabled == cloudEnabled)
    {
        server->send(200, F("application/json"), F("{\"restart\":false,\"saved\":true}"));
        return;
    }

    cloudEnabled = wantEnabled;
    saveCloudConfig();
    // Enabling/disabling flips MQTT⇄cloud and (dis)connects the link, both of
    // which happen in setup()/CloudTask::begin - so a reboot is required.
    restartWithReason("cloud");
}

/** response for /getpairing/ - proof the SPA hands to the cloud to bind a device
 * to an account. Never exposes the PSK; emits an HMAC proof over a fresh nonce. */
void handleGetPairing()
{
    if (!checkHttpGet(server->method()))
        return;

    uint8_t psk[32];
    if (!CloudPSKStorage::readPSK(psk))
    {
        server->send(503, F("text/plain"), F("PSK not provisioned"));
        return;
    }

    // Always bind the proof to the factory hostname (matches the preseeded PSK),
    // never the user-renamable deviceName - otherwise a rename would break pairing
    // or let a device claim another's identity.
    String hn = cloudHostname();
    if (hn.length() == 0)
    {
        server->send(503, F("text/plain"), F("Device not provisioned for cloud"));
        return;
    }

    uint8_t nonce[POOLLINK_NONCE_SIZE];
    CloudCrypto::randomBytes(nonce, sizeof(nonce));

    uint8_t proof[32];
    CloudCrypto::accountPairingHmac(psk, hn.c_str(), nonce, proof);

    char nonceB64[48];
    char proofB64[48];
    CloudCrypto::base64UrlEncodeNoPad(nonce, sizeof(nonce), nonceB64);
    CloudCrypto::base64UrlEncodeNoPad(proof, sizeof(proof), proofB64);

    StaticJsonDocument<384> doc;
    doc[F("hostname")] = hn;
    doc[F("nonce_b64")] = nonceB64;
    doc[F("proof_b64")] = proofB64;

    String json;
    if (serializeJson(doc, json) == 0)
        json = F("{\"error\":\"serialize\"}");
    server->send(200, F("application/json"), json);
}

/**
 * response for /getweather/: the town the cloud resolved for this unit.
 *
 * The module no longer looks the weather up itself: it used to poll a separate
 * API over HTTP, which cost 10-18 KB of heap per call against ~26 KB free and
 * blocked loop() for up to 25 s per failed attempt. The cloud now sends the
 * outdoor temperature and the town in the PoolLink sensor ack
 * (CloudTask::_handleSensorAck), so all that is left here is handing the town
 * to the local Config page. Empty until the first ack that carries one.
 *
 * The route keeps its old name: the local web UI and any third-party polling
 * of the device's HTTP API already know it.
 */
void handleGetWeather()
{
    server->send(200, F("text/plain"), CloudTask::getLocationName());
}
