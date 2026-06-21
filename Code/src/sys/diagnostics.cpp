#include "sys/sys.h"
#include "web/web.h"
#include <ArduinoJson.h>
#include <initializer_list>

namespace
{

    /**
     * Verify flash health by writing a known payload to LittleFS and reading it back
     * Removes the temporary test file afterwards
     * @param detail output string set to "ok" on success or the failure reason
     * @return true if the write/read round-trip matched
     */
    bool testFlashWriteRead(String &detail)
    {
        const char *testPath = "/diag_test.tmp";
        const char *testPayload = "wifiwhirl-flash-check";

        File w = LittleFS.open(testPath, "w");
        if (!w)
        {
            detail = F("open-for-write failed");
            return false;
        }
        size_t written = w.print(testPayload);
        w.close();
        if (written != strlen(testPayload))
        {
            detail = F("short write");
            LittleFS.remove(testPath);
            return false;
        }

        File r = LittleFS.open(testPath, "r");
        if (!r)
        {
            detail = F("open-for-read failed");
            return false;
        }
        String readBack = r.readString();
        r.close();
        LittleFS.remove(testPath);

        if (readBack != testPayload)
        {
            detail = F("readback mismatch");
            return false;
        }
        detail = F("ok");
        return true;
    }

    // Anonymizes an SSID for the support package: "My Wifi" -> "My ...".
    String anonymizeSsid(const String &ssid)
    {
        return ssid.substring(0, 3) + F("...");
    }

    // Loads a config file, blanks out any secret key, and re-serializes it.
    // A deserialization error here means the file is corrupt on flash, which is
    // the other common cause of "my settings keep resetting".
    String readConfigFileRedacted(const char *path, size_t capacity, std::initializer_list<const char *> secretKeys)
    {
        File file = LittleFS.open(path, "r");
        if (!file)
        {
            return F("null");
        }

        DynamicJsonDocument doc(capacity);
        DeserializationError err = deserializeJson(doc, file);
        file.close();

        if (err)
        {
            String msg = F("{\"parseError\":\"");
            msg += err.c_str();
            msg += F("\"}");
            return msg;
        }

        for (const char *secretKey : secretKeys)
        {
            if (secretKey && doc.containsKey(secretKey))
            {
                doc[secretKey] = F("***redacted***");
            }
        }
        if (doc.containsKey("apSsid"))
        {
            doc["apSsid"] = anonymizeSsid(doc["apSsid"].as<String>());
        }

        String out;
        serializeJson(doc, out);
        return out;
    }

} // namespace

/**
 * Bundles firmware/network state and a flash healthcheck into a single JSON
 * file the customer can download and send to support. Never includes raw
 * WiFi/MQTT passwords.
 */
void handleSupportPackage()
{
    if (!legacyAuthOk("support"))
        return;

    char stack;
    uint32_t stackSize = stack_start - &stack;

    String flashTestDetail;
    bool flashOk = testFlashWriteRead(flashTestDetail);

    FSInfo fsInfo;
    LittleFS.info(fsInfo);

    uint32_t flashChipSize = ESP.getFlashChipSize();
    uint32_t flashChipRealSize = ESP.getFlashChipRealSize();

    String json;
    json.reserve(2048);
    json += F("{\"firmware\":\"");
    json += FW_VERSION;
    json += F("\",\"model\":\"");
    json += bwc->getModel();
    json += F("\",\"uptimeSeconds\":");
    json += String((uint32_t)(millis() / 1000));
    json += F(",\"resetReason\":\"");
    json += ESP.getResetReason();
    json += F("\",\"freeHeap\":");
    json += String(ESP.getFreeHeap());
    json += F(",\"minFreeHeapSeen\":");
    json += String(heap_water_mark);

    json += F(",\"wifi\":{\"ssid\":\"");
    json += anonymizeSsid(WiFi.SSID());
    json += F("\",\"rssi\":");
    json += String(WiFi.RSSI());
    json += F(",\"ip\":\"");
    json += WiFi.localIP().toString();
    json += F("\",\"gateway\":\"");
    json += WiFi.gatewayIP().toString();
    json += F("\",\"subnet\":\"");
    json += WiFi.subnetMask().toString();
    json += F("\",\"dns1\":\"");
    json += WiFi.dnsIP(0).toString();
    json += F("\",\"dns2\":\"");
    json += WiFi.dnsIP(1).toString();
    json += F("\",\"mac\":\"");
    json += WiFi.macAddress();
    json += F("\"}");

    json += F(",\"mqtt\":{\"enabled\":");
    json += enableMqtt ? F("true") : F("false");
    json += F(",\"connected\":");
    json += (mqttClient && mqttClient->connected()) ? F("true") : F("false");
    json += F("}");

    // ESP runtime diagnostics (folded in from the former /info/ endpoint)
    json += F(",\"esp\":{\"stackSize\":");
    json += String(stackSize);
    json += F(",\"coreVersion\":\"");
    json += ESP.getCoreVersion();
    json += F("\",\"cpuFreqMHz\":");
    json += String(ESP.getCpuFreqMHz());
    json += F(",\"cycleCount\":");
    json += String(ESP.getCycleCount());
    json += F(",\"freeContStack\":");
    json += String(ESP.getFreeContStack());
    json += F(",\"sketchSize\":");
    json += String(ESP.getSketchSize());
    json += F(",\"freeSketchSpace\":");
    json += String(ESP.getFreeSketchSpace());
    json += F(",\"maxFreeBlockSize\":");
    json += String(ESP.getMaxFreeBlockSize());
    json += F("}");

    // Flash healthcheck: a configured/real size mismatch is a classic cause of
    // silent write corruption on ESP8266 clones, surfaced directly here.
    json += F(",\"flashHealth\":{\"chipId\":");
    json += String(ESP.getFlashChipId());
    json += F(",\"configuredSizeBytes\":");
    json += String(flashChipSize);
    json += F(",\"realSizeBytes\":");
    json += String(flashChipRealSize);
    json += F(",\"sizeMismatch\":");
    json += (flashChipSize != flashChipRealSize) ? F("true") : F("false");
    json += F(",\"fsTotalBytes\":");
    json += String(fsInfo.totalBytes);
    json += F(",\"fsUsedBytes\":");
    json += String(fsInfo.usedBytes);
    json += F(",\"writeReadTest\":\"");
    json += flashOk ? F("ok") : flashTestDetail;
    json += F("\"}");

    json += F(",\"config\":{\"wifi\":");
    json += readConfigFileRedacted("/wifi.json", 512, {"apPwd"});
    json += F(",\"mqtt\":");
    json += readConfigFileRedacted("mqtt.json", 512, {"mqttPassword"});
    json += F(",\"webconfig\":");
    json += readConfigFileRedacted("/webconfig.json", 256, {});
    json += F(",\"device\":");
    json += readConfigFileRedacted("/device.json", 512, {"apPwd", "otaPwd", "cloudApiKey"});
    json += F(",\"devuser\":");
    json += readConfigFileRedacted("/devuser.json", 256, {"apPwd", "otaPwd"});
    json += F("}}");

    server->sendHeader(F("Content-Disposition"), F("attachment; filename=\"wifiwhirl-support.json\""));
    server->send(200, F("application/json"), json);
}
