#include "sys/sys.h"
#include "web/web.h"
#include <ArduinoJson.h>

// the whole configuration as one JSON file, to restore it on this or a
// replacement module. Left out, as agreed with the tester: WiFi, device name,
// cloud access, and every password except MQTT and webhook. No password on the
// file itself. Without global login, both directions ask for user "backup" and
// the OTA password (like the support package).

// Lists in flash, '|'-separated: as RAM string tables they cost ~170 B of static RAM.
static const char BACKUP_FILES[] PROGMEM =
    "/settings.json|/hwcfg.json|/webconfig.json|/cmdq.json|/smartschedule.json|/mqtt.json|/devuser.json";
// devuser.json also holds the device name and the login/OTA/AP secrets
static const char DEVUSER_KEEP_LOCAL[] PROGMEM = "|hostname|apPwd|otaPwd|authEnabled|authUser|authSalt|authHash|";
static const char RESTORE_TMP[] PROGMEM = "/restore.tmp";
static const size_t RESTORE_MAX = 16384;

static bool keptLocal(const char *key)
{
    // Copy the list to RAM first: reading flash byte by byte (strstr_P had the
    // flash string as haystack) is an exception on the ESP8266, which reset the
    // module half way through the download.
    char list[sizeof(DEVUSER_KEEP_LOCAL)];
    strcpy_P(list, DEVUSER_KEEP_LOCAL);
    char needle[24];
    snprintf(needle, sizeof(needle), "|%s|", key);
    return strstr(list, needle) != nullptr;
}

// for (path : BACKUP_FILES) over a RAM copy of the flash list
#define FOR_BACKUP_FILE(path)                        \
    char list_[sizeof(BACKUP_FILES)];                \
    strcpy_P(list_, BACKUP_FILES);                   \
    char *save_;                                     \
    for (char *path = strtok_r(list_, "|", &save_); path; path = strtok_r(nullptr, "|", &save_))

void handleBackup()
{
    if (!legacyAuthOk("backup"))
        return;
    server->sendHeader(F("Content-Disposition"), F("attachment; filename=\"wifiwhirl-backup.json\""));
    server->setContentLength(CONTENT_LENGTH_UNKNOWN);
    server->send(200, F("application/json"), F(""));
    server->sendContent(F("{\"wifiwhirlBackup\":1,\"files\":{"));
    bool first = true;
    FOR_BACKUP_FILE(path)
    {
        File f = LittleFS.open(path, "r");
        if (!f)
            continue;
        String body;
        if (!strcmp(path, "/devuser.json"))
        {
            DynamicJsonDocument doc(1536);
            if (deserializeJson(doc, f) == DeserializationError::Ok)
            {
                JsonObject o = doc.as<JsonObject>();
                for (JsonObject::iterator it = o.begin(); it != o.end();)
                {
                    if (keptLocal(it->key().c_str()))
                    {
                        o.remove(it);
                        it = o.begin(); // restart: remove() invalidates the iterator
                    }
                    else
                        ++it;
                }
                serializeJson(doc, body);
            }
        }
        else
            body = f.readString();
        f.close();
        body.trim();
        if (!body.length())
            continue;
        String chunk = first ? String() : String(',');
        first = false;
        chunk += '"';
        chunk += path + 1; // name without the leading slash
        chunk += F("\":");
        server->sendContent(chunk);
        server->sendContent(body);
    }
    server->sendContent(F("}}"));
    server->sendContent(""); // end of the chunked response
}

static bool restoreAuthed;
static bool restoreTooBig;
static File restoreFile;

// Streams the uploaded file to flash, so a large command queue never has to fit
// in RAM as one request body.
void handleRestoreUpload()
{
    HTTPUpload &up = server->upload();
    if (up.status == UPLOAD_FILE_START)
    {
        restoreAuthed = !crossSiteRequest() &&
                        (globalAuthEnabled ? isAuthed() : server->authenticate("backup", OTAPassword.c_str()));
        restoreTooBig = false;
        if (restoreAuthed)
            restoreFile = LittleFS.open(FPSTR(RESTORE_TMP), "w");
    }
    else if (up.status == UPLOAD_FILE_WRITE && restoreFile)
    {
        if (restoreFile.size() + up.currentSize > RESTORE_MAX)
        {
            restoreTooBig = true;
            restoreFile.close();
        }
        else
            restoreFile.write(up.buf, up.currentSize);
    }
    else if (up.status == UPLOAD_FILE_END || up.status == UPLOAD_FILE_ABORTED)
    {
        if (restoreFile)
            restoreFile.close();
    }
}

void handleRestore()
{
    if (crossSiteRequest())
    {
        LittleFS.remove(FPSTR(RESTORE_TMP));
        server->send(403, F("text/plain"), F("Cross-site request rejected."));
        return;
    }
    if (!legacyAuthOk("backup") || !restoreAuthed)
    {
        LittleFS.remove(FPSTR(RESTORE_TMP));
        return;
    }
    File in = LittleFS.open(FPSTR(RESTORE_TMP), "r");
    if (restoreTooBig || !in)
    {
        LittleFS.remove(FPSTR(RESTORE_TMP));
        server->send(400, F("text/plain"), F("invalid"));
        return;
    }
    {
        StaticJsonDocument<32> filter;
        filter[F("wifiwhirlBackup")] = true;
        StaticJsonDocument<64> head;
        deserializeJson(head, in, DeserializationOption::Filter(filter));
        if ((head[F("wifiwhirlBackup")] | 0) != 1)
        {
            in.close();
            LittleFS.remove(FPSTR(RESTORE_TMP));
            server->send(400, F("text/plain"), F("invalid"));
            return;
        }
    }
    // one file at a time, so only one of them is in RAM
    FOR_BACKUP_FILE(path)
    {
        in.seek(0);
        StaticJsonDocument<96> filter;
        filter[F("files")][path + 1] = true;
        DynamicJsonDocument doc(4096);
        if (deserializeJson(doc, in, DeserializationOption::Filter(filter)) != DeserializationError::Ok)
            continue;
        JsonVariant v = doc[F("files")][path + 1];
        if (v.isNull())
            continue;
        if (!strcmp(path, "/devuser.json"))
        {
            // merge: this module keeps its name and its own secrets
            DynamicJsonDocument cur(1536);
            File f = LittleFS.open(path, "r");
            if (f)
            {
                deserializeJson(cur, f);
                f.close();
            }
            for (JsonPair kv : v.as<JsonObject>())
                if (!keptLocal(kv.key().c_str()))
                    cur[kv.key()] = kv.value();
            File out = LittleFS.open(path, "w");
            serializeJson(cur, out);
            out.close();
        }
        else
        {
            File out = LittleFS.open(path, "w");
            serializeJson(v, out);
            out.close();
        }
    }
    in.close();
    LittleFS.remove(FPSTR(RESTORE_TMP));
    // Reboot without restartWithReason(): its saveSettings() would write this
    // module's old settings over the restored ones.
    server->send(200, F("application/json"), F("{\"restart\":true,\"reason\":\"restore\"}"));
    delay(500);
    stopall();
    delay(1000);
    ESP.restart();
}
