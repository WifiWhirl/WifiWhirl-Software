#include "api/api.h"
#include "web/web.h"
#include "net/net.h"
#include "cloud_task.h"

/*
 * Public, documented API for integrations (ioBroker, Home Assistant, scripts),
 * Unlike the SPA endpoints it never depends on the global login: every
 * request needs an API key ("Authorization: Bearer ww_..."), and it only exists
 * while "API aktivieren" is on. Keys are made on the expert page, shown once and
 * stored as SHA-256(salt + key); a key may be read-only (GET) or full.
 * No CORS headers, so other web pages can't use it.
 *
 * Names are spelled out (pump, not FLT), temperatures are always °C, times are
 * Unix seconds (null = never). Commands take the same path as the SPA buttons
 * (parseCommandValidated + add_live_command), so the same checks apply.
 * Spec: Code/openapi-v1.yaml (also on wifiwhirl.de).
 */
bool apiEnabled = false;
bool apiRequest = false; // lets the reused SPA getters answer GET (checkHttpPost)

// --- helpers ----------------------------------------------------------------

static void apiError(int code, const String &msg)
{
    StaticJsonDocument<256> doc;
    doc[F("error")] = msg;
    String json;
    serializeJson(doc, json);
    server->send(code, F("application/json"), json);
}

static void apiSend(JsonDocument &doc)
{
    String json;
    serializeJson(doc, json);
    server->send(200, F("application/json"), json);
}

static void apiOk()
{
    server->send(200, F("application/json"), F("{\"ok\":true}"));
}

// Parses the request body into doc as a JSON object; answers 400 otherwise.
static bool apiBody(JsonDocument &doc)
{
    if (deserializeJson(doc, getRequestBody()) || !doc.is<JsonObject>())
    {
        apiError(400, F("Body must be a JSON object"));
        return false;
    }
    return true;
}

// Unix seconds, or null when the event never happened.
static void setTime(JsonObject o, const __FlashStringHelper *key, uint64_t ts)
{
    if (ts > 0)
        o[key] = ts;
    else
        o[key] = nullptr;
}

// --- API keys -------------------------------------------------------------------

/*
 * plain salted SHA-256, no PBKDF2. Keys are 32 random [A-Za-z0-9]
 * (~190 bits), so stretching adds nothing, and PBKDF2 would cost ~0.5 s on
 * every request. Upgrade path: key ids if more than 4 keys are ever needed.
 */
static const uint8_t API_MAX_KEYS = 4;
static const char API_KEYS_FILE[] = "/apikeys.json";
struct ApiKey
{
    String name, salt, hash;
    bool control;     // false = read only (GET)
    uint32_t created; // Unix seconds, 0 = clock wasn't set
};
static ApiKey apiKeys[API_MAX_KEYS];
static uint8_t apiKeyCount = 0;

static void saveApiKeys()
{
    DynamicJsonDocument doc(1024);
    JsonArray arr = doc.to<JsonArray>();
    for (uint8_t i = 0; i < apiKeyCount; i++)
    {
        JsonObject o = arr.createNestedObject();
        o[F("n")] = apiKeys[i].name;
        o[F("s")] = apiKeys[i].salt;
        o[F("h")] = apiKeys[i].hash;
        o[F("c")] = apiKeys[i].control;
        o[F("t")] = apiKeys[i].created;
    }
    File f = LittleFS.open(API_KEYS_FILE, "w");
    if (!f)
        return;
    serializeJson(doc, f);
    f.close();
}

static void loadApiKeys()
{
    apiKeyCount = 0;
    File f = LittleFS.open(API_KEYS_FILE, "r");
    if (!f)
        return;
    DynamicJsonDocument doc(1024);
    bool ok = !deserializeJson(doc, f);
    f.close();
    if (!ok)
        return;
    for (JsonObject o : doc.as<JsonArray>())
    {
        if (apiKeyCount >= API_MAX_KEYS)
            break;
        apiKeys[apiKeyCount++] = {o[F("n")].as<String>(), o[F("s")].as<String>(), o[F("h")].as<String>(),
                                  o[F("c")].as<bool>(), o[F("t")].as<uint32_t>()};
    }
}

// Index of the key in "Authorization: Bearer <key>", -1 if none matches.
static int matchApiKey(const String &header)
{
    if (!header.startsWith(F("Bearer ")))
        return -1;
    String key = header.substring(7);
    key.trim();
    int found = -1;
    for (uint8_t i = 0; i < apiKeyCount; i++) // no early exit: same time for every key
        if (secureEquals(sha256Hex(apiKeys[i].salt + key), apiKeys[i].hash))
            found = i;
    return found;
}

/*
 * Checks the API key of this request (throttled like /login). Answers 401/429
 * itself and returns -1, else the key index.
 */
static int apiKeyFromRequest()
{
    if (!server->hasHeader(F("Authorization")))
    {
        server->sendHeader(F("WWW-Authenticate"), F("Bearer"));
        apiError(401, F("API key missing: send \"Authorization: Bearer <key>\""));
        return -1;
    }
    if (!authAttemptAllowed())
        return -1;
    int k = matchApiKey(server->header(F("Authorization")));
    authAttemptResult(k >= 0);
    if (k < 0)
    {
        server->sendHeader(F("WWW-Authenticate"), F("Bearer"));
        apiError(401, F("Invalid API key"));
    }
    return k;
}

/*
 * One path, several methods: answers 405 for the others instead of falling
 * through to the SPA's 404. Every route runs behind the API guard.
 */
static void route(const __FlashStringHelper *path, WebServerT::THandlerFunction get,
                  WebServerT::THandlerFunction post = nullptr, WebServerT::THandlerFunction del = nullptr)
{
    server->on(path, HTTP_ANY, [get, post, del]()
               {
        if (!apiEnabled)
            return apiError(403, F("API is disabled. Turn it on in the expert mode page, card API."));
        if (crossSiteRequest())
            return apiError(403, F("Cross-site request rejected."));
        int k = apiKeyFromRequest();
        if (k < 0)
            return;
        HTTPMethod m = server->method();
        WebServerT::THandlerFunction h = m == HTTP_GET ? get : m == HTTP_POST ? post : m == HTTP_DELETE ? del : nullptr;
        if (!h)
            return apiError(405, F("Method not allowed"));
        if (m != HTTP_GET && !apiKeys[k].control)
            return apiError(403, F("This API key may only read"));
        apiRequest = true;
        h();
        apiRequest = false; });
}

struct ApiCmd
{
    const __FlashStringHelper *field; // API field name for error messages
    Commands cmd;
    int64_t value;
};

/*
 * Checks every command with the SPA's validation first, then queues them all,
 * so a bad field changes nothing. Answers the request itself.
 */
static void queueAll(const ApiCmd *cmds, uint8_t n)
{
    if (n == 0)
        return apiError(400, F("Nothing to do"));
    if (bwc->calibrating())
        return apiError(423, F("Calibration running"));
    command_que_item items[12];
    for (uint8_t i = 0; i < n && i < 12; i++)
    {
        StaticJsonDocument<96> doc;
        doc[F("CMD")] = (int)cmds[i].cmd;
        doc[F("VALUE")] = cmds[i].value;
        String error;
        if (!parseCommandValidated(doc.as<JsonVariantConst>(), items[i], error))
            return apiError(400, String(cmds[i].field) + F(": value out of range"));
    }
    for (uint8_t i = 0; i < n && i < 12; i++)
        if (!bwc->add_live_command(items[i]))
            return apiError(409, F("Queue full (max. 20 commands)"));
    apiOk();
}

// --- GET /api/v1/info ---------------------------------------------------------

/*
 * Public on purpose (even with the API off): lets an integration tell "too old
 * firmware" from "API off". The rest only with a valid key.
 */
static void handleApiInfo()
{
    StaticJsonDocument<384> doc;
    doc[F("apiVersion")] = 1;
    doc[F("firmware")] = FW_VERSION;
    doc[F("apiEnabled")] = apiEnabled;
    if (!apiEnabled || !server->hasHeader(F("Authorization")))
        return apiSend(doc);
    int k = apiKeyFromRequest();
    if (k < 0)
        return;
    bool sta = WiFi.status() == WL_CONNECTED;
    doc[F("keyName")] = apiKeys[k].name;
    doc[F("keyControl")] = apiKeys[k].control;
    doc[F("model")] = bwc->getModel();
    doc[F("hasJets")] = bwc->hasjets;
    doc[F("hostname")] = deviceName;
    doc[F("ip")] = sta ? WiFi.localIP().toString() : String();
    doc[F("rssi")] = sta ? WiFi.RSSI() : 0;
    doc[F("uptime")] = millis() / 1000;
    apiSend(doc);
}

// --- GET /api/v1/status ---------------------------------------------

static void handleApiStatus()
{
    DynamicJsonDocument out(2560);
    {
        // re-parses the SPA's state JSON instead of reading BWC
        // internals twice; one source of truth, ~1.5 KB extra heap per call.
        String raw;
        bwc->getJSONStates(raw);
        DynamicJsonDocument s(1536);
        deserializeJson(s, raw);
        out[F("time")] = s[F("TIME")];
        out[F("power")] = s[F("PWR")].as<bool>();
        out[F("lock")] = s[F("LCK")].as<bool>();
        out[F("pump")] = s[F("FLT")].as<bool>();
        out[F("heater")] = s[F("RED")].as<bool>() || s[F("GRN")].as<bool>();
        out[F("heating")] = s[F("RED")].as<bool>();
        out[F("targetReached")] = s[F("GRN")].as<bool>();
        out[F("bubbles")] = s[F("AIR")].as<bool>();
        out[F("jets")] = s[F("HJT")].as<bool>();
        out[F("unit")] = s[F("UNT")].as<bool>() ? F("C") : F("F");
        out[F("brightness")] = s[F("BRTBASE")]; // the set level, not a temporary boost
        out[F("error")] = s[F("ERR")];
        out[F("calibrating")] = s[F("HLCAL")].as<bool>();
        char disp[4] = {(char)(s[F("CH1")] | 32), (char)(s[F("CH2")] | 32), (char)(s[F("CH3")] | 32), 0};
        out[F("display")] = disp;
        JsonObject t = out.createNestedObject(F("temperature"));
        t[F("water")] = s[F("TMPC")];
        t[F("target")] = s[F("TGTC")];
        t[F("ambient")] = s[F("AMBC")];
    }
    {
        String raw;
        bwc->getJSONTimes(raw);
        DynamicJsonDocument s(1536);
        deserializeJson(s, raw);
        float t2r = s[F("T2R")] | -1.0f; // -2 ready, -1 never
        if (t2r == -2.0f)
            out[F("readyInHours")] = 0;
        else if (t2r < 0)
            out[F("readyInHours")] = nullptr;
        else
            out[F("readyInHours")] = round(t2r * 10) / 10.0;

        JsonObject e = out.createNestedObject(F("energy"));
        e[F("powerW")] = s[F("WATT")];
        e[F("todayKwh")] = s[F("KWHD")];
        e[F("totalKwh")] = s[F("KWH")];
        e[F("todayCost")] = s[F("COSTD")];
        e[F("totalCost")] = s[F("COST")];

        JsonObject r = out.createNestedObject(F("runtimeSeconds"));
        r[F("uptime")] = s[F("UPTIME")];
        r[F("pump")] = s[F("PUMPTIME")];
        r[F("heater")] = s[F("HEATINGTIME")];
        r[F("bubbles")] = s[F("AIRTIME")];
        r[F("jets")] = s[F("JETTIME")];

        JsonObject m = out.createNestedObject(F("maintenance"));
        const struct { const __FlashStringHelper *name, *ts, *interval; } maint[] = {
            {F("chlorine"), F("CLTIME"), F("CLINT")},
            {F("filterCleaned"), F("FCTIME"), F("FCINT")},
            {F("filterChanged"), F("FTIME"), F("FINT")},
            {F("waterChanged"), F("WCTIME"), F("WCINT")},
            {F("phMeasured"), F("PHTIME"), F("PHINT")},
        };
        for (auto &x : maint)
        {
            JsonObject o = m.createNestedObject(x.name);
            setTime(o, F("last"), s[x.ts].as<uint64_t>());
            o[F("intervalDays")] = s[x.interval];
        }

        // water values are stored x10 (pH 72 = 7.2), alkalinity as is
        JsonObject w = out.createNestedObject(F("waterQuality"));
        const struct { const __FlashStringHelper *name, *val, *ts; float div; } wq[] = {
            {F("ph"), F("PHVAL"), F("PHTIME"), 10},
            {F("chlorine"), F("CLVAL"), F("CLVTIME"), 10},
            {F("cyanuricAcid"), F("CYAVAL"), F("CYATIME"), 10},
            {F("alkalinity"), F("ALKVAL"), F("ALKTIME"), 1},
        };
        for (auto &x : wq)
        {
            JsonObject o = w.createNestedObject(x.name);
            uint32_t v = s[x.val] | 0;
            if (v)
                o[F("value")] = v / x.div;
            else
                o[F("value")] = nullptr;
            setTime(o, F("measured"), s[x.ts].as<uint64_t>());
        }

        // last care product per kind; grams < 0 = pH-/TA-
        JsonObject d = out.createNestedObject(F("lastDose"));
        JsonObject ph = d.createNestedObject(F("ph"));
        ph[F("grams")] = s[F("DPH")];
        setTime(ph, F("time"), s[F("DPHTIME")].as<uint64_t>());
        JsonObject cl = d.createNestedObject(F("chlorine"));
        // DCLTAB marks the amount as tablets instead of grams
        bool tabs = s[F("DCLTAB")];
        cl[F("grams")] = tabs ? 0 : s[F("DCL")].as<int>();
        cl[F("tablets")] = tabs ? s[F("DCL")].as<int>() : 0;
        setTime(cl, F("time"), s[F("DCLTIME")].as<uint64_t>());
        JsonObject ta = d.createNestedObject(F("alkalinity"));
        ta[F("grams")] = s[F("DTA")];
        setTime(ta, F("time"), s[F("DTATIME")].as<uint64_t>());
    }
    apiSend(out);
}

// --- POST /api/v1/controls ------------------------------------------

/*
 * Body: any of power, lock, pump, heater, bubbles, jets (true/false),
 * targetTemperature (°C), unit ("C"/"F"), brightness (0-8). Everything is
 * validated first, then queued, so a bad field changes nothing.
 */
static void handleApiControls()
{
    StaticJsonDocument<384> in;
    if (!apiBody(in))
        return;
    ApiCmd cmds[12];
    uint8_t n = 0;
    const struct { const __FlashStringHelper *key; Commands cmd; } switches[] = {
        {F("power"), TOGGLEPWR}, {F("lock"), TOGGLELCK}, {F("pump"), SETPUMP},
        {F("heater"), SETHEATER}, {F("bubbles"), SETBUBBLES}, {F("jets"), SETJETS},
    };
    for (JsonPair kv : in.as<JsonObject>())
    {
        String key = kv.key().c_str();
        JsonVariant v = kv.value();
        bool known = false;
        for (auto &s : switches)
        {
            if (key != String(s.key))
                continue;
            if (!v.is<bool>())
                return apiError(400, key + F(" must be true or false"));
            cmds[n++] = {s.key, s.cmd, v.as<bool>() ? 1 : 0};
            known = true;
        }
        if (known)
            continue;
        if (key == F("targetTemperature"))
        {
            if (!v.is<int>() || v.as<int>() < 20 || v.as<int>() > 40)
                return apiError(400, F("targetTemperature must be a whole number from 20 to 40 (°C)"));
            int c = v.as<int>();
            // the pump takes the target in the unit it shows
            cmds[n++] = {F("targetTemperature"), SETTARGET, bwc->cio->cio_states.unit ? c : (int)lround(c * 9.0 / 5.0 + 32)};
        }
        else if (key == F("unit"))
        {
            String u = v.as<String>();
            if (u != F("C") && u != F("F"))
                return apiError(400, F("unit must be \"C\" or \"F\""));
            cmds[n++] = {F("unit"), SETUNIT, u == F("C") ? 1 : 0};
        }
        else if (key == F("brightness"))
        {
            if (!v.is<int>() || v.as<int>() < 0 || v.as<int>() > 8)
                return apiError(400, F("brightness must be a whole number from 0 to 8"));
            cmds[n++] = {F("brightness"), SETBRIGHTNESS, v.as<int>()};
        }
        else
            return apiError(400, String(F("Unknown control: ")) + key);
    }
    queueAll(cmds, n);
}

// --- POST /api/v1/maintenance ---------------------------------------

// Body: {"done":["chlorine","filterCleaned","filterChanged","waterChanged"]}
static void handleApiMaintenance()
{
    StaticJsonDocument<256> in;
    if (!apiBody(in))
        return;
    JsonArray done = in[F("done")];
    if (done.isNull() || done.size() == 0)
        return apiError(400, F("done must be a list, e.g. [\"chlorine\"]"));
    ApiCmd cmds[4];
    uint8_t n = 0;
    for (JsonVariant v : done)
    {
        String k = v.as<String>();
        if (n >= 4)
            return apiError(400, F("Too many entries"));
        if (k == F("chlorine"))
            cmds[n++] = {F("done"), RESETCLTIMER, 0};
        else if (k == F("filterCleaned"))
            cmds[n++] = {F("done"), RESETFCTIMER, 0};
        else if (k == F("filterChanged"))
            cmds[n++] = {F("done"), RESETFTIMER, 0};
        else if (k == F("waterChanged"))
            cmds[n++] = {F("done"), RESETWCTIMER, 0};
        else
            return apiError(400, String(F("Unknown maintenance task: ")) + k);
    }
    queueAll(cmds, n);
}

// --- POST /api/v1/waterquality --------------------------------------

/*
 * Body: measured values ph, chlorine (mg/l), cyanuricAcid (mg/l), alkalinity
 * (mg/l), and/or "dose": {ph (g, < 0 = pH-), chlorine (g), chlorineTablets,
 * alkalinity (g, < 0 = TA-)}. Ranges are checked by the command layer.
 */
static void handleApiWaterQuality()
{
    StaticJsonDocument<384> in;
    if (!apiBody(in))
        return;
    ApiCmd cmds[8];
    uint8_t n = 0;
    const struct { const __FlashStringHelper *key; Commands cmd; float mul; } values[] = {
        {F("ph"), SETPHVALUE, 10}, {F("chlorine"), SETCLVALUE, 10},
        {F("cyanuricAcid"), SETCYAVALUE, 10}, {F("alkalinity"), SETALKVALUE, 1},
    };
    const struct { const __FlashStringHelper *key; Commands cmd; } doses[] = {
        {F("ph"), ADDPH}, {F("chlorine"), ADDCL}, {F("chlorineTablets"), ADDCLTAB}, {F("alkalinity"), ADDTA},
    };
    for (auto &x : values)
    {
        JsonVariant v = in[x.key];
        if (v.isNull())
            continue;
        if (!v.is<float>())
            return apiError(400, String(x.key) + F(" must be a number"));
        cmds[n++] = {x.key, x.cmd, (int64_t)lround(v.as<float>() * x.mul)};
    }
    JsonObject dose = in[F("dose")];
    for (auto &x : doses)
    {
        if (dose.isNull() || dose[x.key].isNull())
            continue;
        if (!dose[x.key].is<int>())
            return apiError(400, String(F("dose.")) + x.key + F(" must be a whole number"));
        cmds[n++] = {x.key, x.cmd, dose[x.key].as<int>()};
    }
    queueAll(cmds, n);
}

// --- /api/v1/schedule ------------------------------------------------

static void handleApiScheduleGet()
{
    String raw;
    bwc->getJSONSmartSchedule(raw);
    DynamicJsonDocument s(1024);
    deserializeJson(s, raw);
    StaticJsonDocument<512> out;
    bool active = s[F("ACTIVE")];
    out[F("active")] = active;
    setTime(out.as<JsonObject>(), F("targetTime"), s[F("TARGETTIME")].as<uint64_t>());
    out[F("targetTemperature")] = s[F("TARGETTEMP")];
    out[F("afterTarget")] = s[F("KEEPON")].as<bool>() ? F("heater") : s[F("KEEPPUMP")].as<bool>() ? F("pump") : F("off");
    out[F("repeatDays")] = s[F("REPEAT")];
    setTime(out.as<JsonObject>(), F("heatingStart"), active ? s[F("STARTTIME")].as<uint64_t>() : 0);
    float est = s[F("ESTIMATE")] | 0.0f;
    if (est > 0 && est < 999)
        out[F("estimatedHours")] = round(est * 10) / 10.0;
    else
        out[F("estimatedHours")] = nullptr;
    out[F("estimatedKwh")] = s[F("ESTIMATED_KWH")];
    out[F("estimatedCost")] = s[F("ESTIMATED_COST")];
    apiSend(out);
}

/*
 * Body: targetTime (Unix seconds), targetTemperature (20-40 °C), optional
 * afterTarget ("off" default, "pump", "heater") and repeatDays (0-30).
 */
static void handleApiSchedulePost()
{
    StaticJsonDocument<256> in;
    if (!apiBody(in))
        return;
    if (!in[F("targetTime")].is<uint64_t>() || !in[F("targetTemperature")].is<int>())
        return apiError(400, F("targetTime (Unix seconds) and targetTemperature (20-40) are required"));
    String after = in[F("afterTarget")] | "off";
    if (after != F("off") && after != F("pump") && after != F("heater"))
        return apiError(400, F("afterTarget must be \"off\", \"pump\" or \"heater\""));
    StaticJsonDocument<256> fw;
    fw[F("TARGETTIME")] = in[F("targetTime")];
    fw[F("TARGETTEMP")] = in[F("targetTemperature")];
    fw[F("KEEPON")] = after == F("heater");
    fw[F("KEEPPUMP")] = after == F("pump");
    if (in.containsKey(F("repeatDays")))
        fw[F("REPEAT")] = in[F("repeatDays")];
    String err = setSmartScheduleFromJson(fw.as<JsonObjectConst>());
    if (err.length())
        return apiError(400, err);
    apiOk();
}

static void handleApiScheduleDelete()
{
    bwc->cancelSmartSchedule();
    apiOk();
}

// --- /api/v1/config/spa ----------------------------------------------

/*
 * API name -> settings key of /getconfig/ and /setconfig/. "buttons.x" lives in
 * a nested object. Read-only keys (model ...) are only in the GET answer.
 * one flash string scanned per call instead of a table in RAM.
 */
static const char SPA_KEYS[] PROGMEM =
    "electricityPrice=PRICE,nightPrice=PRICE_NIGHT,nightTariff=NIGHT_ENABLED,"
    "nightStartMinute=NIGHT_START_MIN,nightEndMinute=NIGHT_END_MIN,nightTariffWeekend=NIGHT_WEEKEND,"
    "chlorineIntervalDays=CLINT,filterChangeIntervalDays=FINT,filterCleanIntervalDays=FCINT,"
    "waterChangeIntervalDays=WCINT,phIntervalDays=PHINT,sound=AUDIO,jetCountdown=JETCD,"
    "standbyOriginal=STBYORIG,timezone=TIMEZONE,timezoneName=TIMEZONE_NAME,weather=WEATHER,"
    "poolCapacityLiters=POOLCAP,airjetTimeoutMinutes=AIRTO,hydrojetTimeoutMinutes=HJTO,"
    "guestMode=GSTMODE,guestMaxTarget=GSTMAXTGT,guestHeaterTimeoutMinutes=GSTHTRTO,"
    "brightnessSteps=BRTSTEP,brightnessDuration=BRTDUR,"
    "buttons.lock=LCK,buttons.timer=TMR,buttons.bubbles=AIR,buttons.unit=UNT,buttons.heater=HTR,"
    "buttons.pump=FLT,buttons.down=DN,buttons.up=UP,buttons.power=PWR,buttons.jets=HJT";
static const char SPA_READONLY[] PROGMEM = "model=MODEL,hasJets=HASJETS,cloud=CLOUD,heatLoss=HLOSS";

// Calls fn(apiName, fwKey) for every pair in a "a=B,c=D" flash list.
template <typename Fn>
static void eachKey(PGM_P list, Fn fn)
{
    String all = FPSTR(list);
    int start = 0;
    while (start < (int)all.length())
    {
        int comma = all.indexOf(',', start);
        if (comma < 0)
            comma = all.length();
        int eq = all.indexOf('=', start);
        fn(all.substring(start, eq), all.substring(eq + 1, comma));
        start = comma + 1;
    }
}

static bool knownSpaKey(const String &k)
{
    bool found = false;
    eachKey(SPA_KEYS, [&](const String &api, const String &)
            { if (api == k) found = true; });
    return found;
}

static void handleApiSpaGet()
{
    String raw;
    bwc->getJSONSettings(raw);
    DynamicJsonDocument s(1536);
    deserializeJson(s, raw);
    DynamicJsonDocument out(2048);
    JsonObject buttons = out.createNestedObject(F("buttons"));
    auto copy = [&](const String &api, const String &fw)
    {
        if (api.startsWith(F("buttons.")))
            buttons[api.substring(8)] = s[fw];
        else
            out[api] = s[fw];
    };
    eachKey(SPA_KEYS, copy);
    eachKey(SPA_READONLY, copy);
    apiSend(out);
}

static void handleApiSpaPost()
{
    DynamicJsonDocument in(1024);
    if (!apiBody(in))
        return;
    DynamicJsonDocument fw(1024);
    eachKey(SPA_KEYS, [&](const String &api, const String &key)
            {
        JsonVariant v = api.startsWith(F("buttons.")) ? in[F("buttons")][api.substring(8)].as<JsonVariant>() : in[api].as<JsonVariant>();
        if (!v.isNull())
            fw[key] = v; });
    // anything not in the table is a typo or read-only: say so instead of ignoring it
    for (JsonPair kv : in.as<JsonObject>())
    {
        String k = kv.key().c_str();
        if (k == F("buttons") && kv.value().is<JsonObject>())
        {
            for (JsonPair b : kv.value().as<JsonObject>())
                if (!knownSpaKey(String(F("buttons.")) + b.key().c_str()))
                    return apiError(400, String(F("Unknown button: ")) + b.key().c_str());
        }
        else if (!knownSpaKey(k))
            return apiError(400, String(F("Unknown or read-only setting: ")) + k);
    }
    if (fw.size() == 0)
        return apiError(400, F("Nothing to do"));
    String body, error;
    serializeJson(fw, body);
    if (!bwc->setJSONSettings(body, error))
    {
        // "CLINT out of range" -> "chlorineIntervalDays out of range"
        eachKey(SPA_KEYS, [&](const String &api, const String &key)
                { if (error.startsWith(key + ' ')) error = api + error.substring(key.length()); });
        return apiError(400, error.length() ? error : String(F("Invalid settings")));
    }
    CloudTask::notifySettingsChanged(); // same as /setconfig/
    CloudTask::requestResync();
    apiOk();
}

// --- system -----------------------------------------------------------

static void handleApiRestart()
{
    restartWithReason("api"); // answers {"restart":true,"reason":"api"} itself
}

// --- key management for the web interface (behind guard()) -------------------

// POST /getapikeys/ -> {"max":4,"keys":[{"id","name","control","created"}]}
static void handleGetApiKeys()
{
    if (!checkHttpPost(server->method()))
        return;
    StaticJsonDocument<768> doc;
    doc[F("max")] = API_MAX_KEYS;
    JsonArray arr = doc.createNestedArray(F("keys"));
    for (uint8_t i = 0; i < apiKeyCount; i++)
    {
        JsonObject o = arr.createNestedObject();
        o[F("id")] = i;
        o[F("name")] = apiKeys[i].name;
        o[F("control")] = apiKeys[i].control;
        o[F("created")] = apiKeys[i].created;
    }
    apiSend(doc);
}

// POST /addapikey/ {"name","control"} -> {"id","key"}: the only time the key is shown
static void handleAddApiKey()
{
    if (!checkHttpPost(server->method()))
        return;
    StaticJsonDocument<192> in;
    if (!apiBody(in))
        return;
    String name = in[F("name")] | "";
    name.trim();
    if (name.length() == 0 || name.length() > 32)
        return apiError(400, F("Name: 1-32 characters"));
    for (char c : name)
        if ((uint8_t)c < 32 || c == '"' || c == '\\')
            return apiError(400, F("Name contains invalid characters"));
    if (apiKeyCount >= API_MAX_KEYS)
        return apiError(409, F("Maximum number of API keys reached"));
    String key = String(F("ww_")) + randomToken(32);
    String salt = makeSalt();
    uint32_t now = time(nullptr) > 1600000000 ? (uint32_t)time(nullptr) : 0;
    apiKeys[apiKeyCount] = {name, salt, sha256Hex(salt + key), in[F("control")].as<bool>(), now};
    StaticJsonDocument<128> out;
    out[F("id")] = apiKeyCount++;
    out[F("key")] = key;
    saveApiKeys();
    apiSend(out);
}

// POST /delapikey/ {"id"}
static void handleDelApiKey()
{
    if (!checkHttpPost(server->method()))
        return;
    StaticJsonDocument<64> in;
    if (!apiBody(in))
        return;
    int id = in[F("id")] | -1;
    if (id < 0 || id >= apiKeyCount)
        return apiError(400, F("Unknown key"));
    for (uint8_t i = id; i + 1 < apiKeyCount; i++)
        apiKeys[i] = apiKeys[i + 1];
    apiKeys[--apiKeyCount] = ApiKey();
    saveApiKeys();
    apiOk();
}

// Support package: {"enabled","count","list":[{"name","rights","created"}]}.
// Field names avoid "key": the CI leak check flags those (ci/net/test_net.py).
void apiSupportInfo(String &json)
{
    StaticJsonDocument<512> doc;
    doc[F("enabled")] = apiEnabled;
    doc[F("count")] = apiKeyCount;
    JsonArray arr = doc.createNestedArray(F("list"));
    for (uint8_t i = 0; i < apiKeyCount; i++)
    {
        JsonObject o = arr.createNestedObject();
        o[F("name")] = apiKeys[i].name;
        o[F("rights")] = apiKeys[i].control ? F("control") : F("read");
        o[F("created")] = apiKeys[i].created;
    }
    serializeJson(doc, json);
}

void registerApiV1Routes()
{
    loadApiKeys();
    server->on(F("/api/v1/info"), HTTP_GET, handleApiInfo);
    server->on(F("/getapikeys/"), guard(handleGetApiKeys));
    server->on(F("/addapikey/"), guard(handleAddApiKey));
    server->on(F("/delapikey/"), guard(handleDelApiKey));
    route(F("/api/v1/status"), handleApiStatus);
    route(F("/api/v1/controls"), nullptr, handleApiControls);
    route(F("/api/v1/maintenance"), nullptr, handleApiMaintenance);
    route(F("/api/v1/waterquality"), nullptr, handleApiWaterQuality);
    route(F("/api/v1/schedule"), handleApiScheduleGet, handleApiSchedulePost, handleApiScheduleDelete);
    route(F("/api/v1/config/spa"), handleApiSpaGet, handleApiSpaPost);
    // these keep the field names of the web interface, documented in the spec
    route(F("/api/v1/config/mqtt"), handleGetMqtt, handleSetMqtt);
    route(F("/api/v1/config/wifi"), handleGetWifi, handleSetWifi);
    route(F("/api/v1/config/hardware"), handleGetHardware, handleSetHardware);
    route(F("/api/v1/system/wifiscan"), handleScanWifi);
    route(F("/api/v1/system/restart"), nullptr, handleApiRestart);
}
