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

    void appendJsonString(String &json, const String &value)
    {
        static const char *hex = "0123456789abcdef";
        json += '"';
        for (size_t i = 0; i < value.length(); i++)
        {
            uint8_t c = (uint8_t)value[i];
            switch (c)
            {
            case '"': json += F("\\\""); break;
            case '\\': json += F("\\\\"); break;
            case '\b': json += F("\\b"); break;
            case '\f': json += F("\\f"); break;
            case '\n': json += F("\\n"); break;
            case '\r': json += F("\\r"); break;
            case '\t': json += F("\\t"); break;
            default:
                if (c < 0x20)
                {
                    json += F("\\u00");
                    json += hex[c >> 4];
                    json += hex[c & 0x0f];
                }
                else
                {
                    json += (char)c;
                }
                break;
            }
        }
        json += '"';
    }

    void appendJsonString(String &json, const char *value)
    {
        appendJsonString(json, String(value ? value : ""));
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
            String msg = F("{\"parseError\":");
            appendJsonString(msg, err.c_str());
            msg += F("}");
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

#ifndef ESP8266
    // ESP32 has no ESP.getResetReason() String; map the enum to a short label.
    String esp32ResetReasonStr()
    {
        switch (esp_reset_reason())
        {
        case ESP_RST_POWERON: return F("Power on");
        case ESP_RST_EXT: return F("External reset");
        case ESP_RST_SW: return F("Software reset");
        case ESP_RST_PANIC: return F("Exception/panic");
        case ESP_RST_INT_WDT: return F("Interrupt watchdog");
        case ESP_RST_TASK_WDT: return F("Task watchdog");
        case ESP_RST_WDT: return F("Other watchdog");
        case ESP_RST_DEEPSLEEP: return F("Deep sleep wake");
        case ESP_RST_BROWNOUT: return F("Brownout");
        case ESP_RST_SDIO: return F("SDIO reset");
        default: return F("Unknown");
        }
    }
#endif

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

    uint32_t flashChipSize = ESP.getFlashChipSize();
#ifdef ESP8266
    FSInfo fsInfo;
    LittleFS.info(fsInfo);
    uint32_t fsTotalBytes = fsInfo.totalBytes;
    uint32_t fsUsedBytes = fsInfo.usedBytes;
    uint32_t flashChipRealSize = ESP.getFlashChipRealSize();
#else
    uint32_t fsTotalBytes = LittleFS.totalBytes();
    uint32_t fsUsedBytes = LittleFS.usedBytes();
    uint32_t flashChipRealSize = flashChipSize; // ESP32 has no separate "real" size probe
#endif

    // Stream the document in small chunks rather than building one ~2KB String:
    // this bounds peak heap to a single section and avoids the realloc churn and
    // fragmentation that a large growing String causes on ESP8266.
    server->setContentLength(CONTENT_LENGTH_UNKNOWN);
    server->sendHeader(F("Content-Disposition"), F("attachment; filename=\"wifiwhirl-support.json\""));
    server->send(200, F("application/json"), "");

    String json;
    json.reserve(384);
    // Flush the current section. Guarded against an empty buffer, which would
    // otherwise send the terminating 0-length chunk early.
    auto flush = [&]()
    {
        if (json.length())
        {
            server->sendContent(json);
            json = "";
        }
    };

    json += F("{\"firmware\":");
    appendJsonString(json, FW_VERSION);
    json += F(",\"WifiWhirl Model\":"); // full build env, incl. any "_seed" suffix
    appendJsonString(json, PIO_ENV_NAME);
    json += F(",\"model\":");
    appendJsonString(json, bwc->getModel());
    json += F(",\"uptimeSeconds\":");
    json += String((uint32_t)(millis() / 1000));
    json += F(",\"resetReason\":");
#ifdef ESP8266
    appendJsonString(json, ESP.getResetReason());
#else
    appendJsonString(json, esp32ResetReasonStr());
#endif
#ifdef ESP8266
    // Detailed crash classification from the last reset (survives the reboot).
    // reason: 0=power-on 1=hw-wdt 2=exception 3=soft-wdt 4=soft-restart 6=ext.
    // For exception/wdt, exccause+epc1+excvaddr pinpoint it: exccause 28/29 =
    // null/bad-pointer deref (excvaddr is the address), 9 = unaligned/OOB,
    // 0 = illegal instruction. epc1 decodes to a line via the firmware .elf.
    struct rst_info *ri = ESP.getResetInfoPtr();
    json += F(",\"resetCode\":");
    json += String(ri->reason);
    json += F(",\"exccause\":");
    json += String(ri->exccause);
    json += F(",\"epc1\":\"0x");
    json += String(ri->epc1, HEX);
    json += F("\",\"excvaddr\":\"0x");
    json += String(ri->excvaddr, HEX);
    json += F("\",");
#else
    // ESP32: detailed crash classification (PC, backtrace) comes from the core
    // dump via crashtrace_appendJson() below.
    json += F(",");
#endif
    crashtrace_appendJson(json); // full backtrace from the last crash
    json += F(",\"freeHeap\":");
    json += String(ESP.getFreeHeap());
    json += F(",\"minFreeHeapSeen\":");
    json += String(heap_water_mark);
    flush();

    json += F(",\"wifi\":{\"ssid\":");
    appendJsonString(json, anonymizeSsid(WiFi.SSID()));
    json += F(",\"rssi\":");
    json += String(WiFi.RSSI());
    json += F(",\"ip\":");
    appendJsonString(json, WiFi.localIP().toString());
    json += F(",\"gateway\":");
    appendJsonString(json, WiFi.gatewayIP().toString());
    json += F(",\"subnet\":");
    appendJsonString(json, WiFi.subnetMask().toString());
    json += F(",\"dns1\":");
    appendJsonString(json, WiFi.dnsIP(0).toString());
    json += F(",\"dns2\":");
    appendJsonString(json, WiFi.dnsIP(1).toString());
    json += F(",\"mac\":");
    appendJsonString(json, WiFi.macAddress());
    json += F("}");
    flush();

    json += F(",\"mqtt\":{\"enabled\":");
    json += enableMqtt ? F("true") : F("false");
    json += F(",\"connected\":");
    json += (mqttClient && mqttClient->connected()) ? F("true") : F("false");
    json += F("}");
    flush();

    // ESP runtime diagnostics (folded in from the former /info/ endpoint)
    json += F(",\"esp\":{\"stackSize\":");
    json += String(stackSize);
    json += F(",\"coreVersion\":");
#ifdef ESP8266
    appendJsonString(json, ESP.getCoreVersion());
#else
    appendJsonString(json, ESP.getSdkVersion());
#endif
    json += F(",\"cpuFreqMHz\":");
    json += String(ESP.getCpuFreqMHz());
    json += F(",\"cycleCount\":");
    json += String(ESP.getCycleCount());
    json += F(",\"freeContStack\":");
#ifdef ESP8266
    json += String(ESP.getFreeContStack());
#else
    json += String(uxTaskGetStackHighWaterMark(NULL)); // loop() task stack headroom (words)
#endif
    json += F(",\"sketchSize\":");
    json += String(ESP.getSketchSize());
    json += F(",\"freeSketchSpace\":");
    json += String(ESP.getFreeSketchSpace());
    json += F(",\"maxFreeBlockSize\":");
#ifdef ESP8266
    json += String(ESP.getMaxFreeBlockSize());
#else
    json += String(ESP.getMaxAllocHeap());
#endif
    json += F("}");
    flush();

    // Flash healthcheck: a configured/real size mismatch is a classic cause of
    // silent write corruption on ESP8266 clones, surfaced directly here.
    json += F(",\"flashHealth\":{\"chipId\":");
#ifdef ESP8266
    json += String(ESP.getFlashChipId());
#else
    json += String(0); // ESP32 has no getFlashChipId(); size mismatch check below still applies
#endif
    json += F(",\"configuredSizeBytes\":");
    json += String(flashChipSize);
    json += F(",\"realSizeBytes\":");
    json += String(flashChipRealSize);
    json += F(",\"sizeMismatch\":");
    json += (flashChipSize != flashChipRealSize) ? F("true") : F("false");
    json += F(",\"fsTotalBytes\":");
    json += String(fsTotalBytes);
    json += F(",\"fsUsedBytes\":");
    json += String(fsUsedBytes);
    json += F(",\"writeReadTest\":");
    appendJsonString(json, flashOk ? String(F("ok")) : flashTestDetail);
    json += F("}");
    flush();

    // Each redacted file is appended then flushed so only one file's worth of
    // JSON is resident at a time.
    json += F(",\"config\":{\"wifi\":");
    json += readConfigFileRedacted("/wifi.json", 512, {"apPwd"});
    flush();
    json += F(",\"mqtt\":");
    json += readConfigFileRedacted("/mqtt.json", 512, {"mqttPassword"});
    flush();
    json += F(",\"webconfig\":");
    json += readConfigFileRedacted("/webconfig.json", 256, {});
    flush();
    json += F(",\"device\":");
    json += readConfigFileRedacted("/device.json", 512, {"apPwd", "otaPwd"});
    flush();
    json += F(",\"devuser\":");
    json += readConfigFileRedacted("/devuser.json", 768, {"apPwd", "otaPwd", "authHash", "webhookAuthPassword"});
    json += F("}}");
    flush();

    server->sendContent(""); // terminate the chunked response
}
