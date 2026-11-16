#include "bwc_internal.h"


static bool readBoundedNumber(const JsonObjectConst &obj, const __FlashStringHelper *key,
                              double minValue, double maxValue, double &out, String &error)
{
    if (!obj.containsKey(key))
        return true;
    JsonVariantConst value = obj[key];
    if (value.isNull() || isJsonString(value) || value.is<bool>())
    {
        error = String(F("Missing or invalid ")) + String(key);
        return false;
    }
    double parsed = value.as<double>();
    if (parsed != parsed || parsed < minValue || parsed > maxValue)
    {
        error = String(key) + F(" out of range");
        return false;
    }
    out = parsed;
    return true;
}

static bool readBoundedInteger(const JsonObjectConst &obj, const __FlashStringHelper *key,
                               int64_t minValue, int64_t maxValue, int64_t &out, String &error)
{
    if (!obj.containsKey(key))
        return true;
    JsonVariantConst value = obj[key];
    if (value.isNull() || isJsonString(value) || value.is<bool>() || !value.is<int64_t>())
    {
        error = String(F("Missing or invalid ")) + String(key);
        return false;
    }
    int64_t parsed = value.as<int64_t>();
    if (parsed < minValue || parsed > maxValue)
    {
        error = String(key) + F(" out of range");
        return false;
    }
    out = parsed;
    return true;
}

static bool readBooleanSetting(const JsonObjectConst &obj, const __FlashStringHelper *key,
                               bool &out, String &error)
{
    if (!obj.containsKey(key))
        return true;
    JsonVariantConst value = obj[key];
    if (value.is<bool>())
    {
        out = value.as<bool>();
        return true;
    }
    if (value.is<int64_t>())
    {
        int64_t parsed = value.as<int64_t>();
        if (parsed == 0 || parsed == 1)
        {
            out = parsed == 1;
            return true;
        }
    }
    error = String(F("Missing or invalid ")) + String(key);
    return false;
}

static bool readBoundedString(const JsonObjectConst &obj, const __FlashStringHelper *key,
                              size_t maxLen, String &out, String &error)
{
    if (!obj.containsKey(key))
        return true;
    JsonVariantConst value = obj[key];
    if (!isJsonString(value))
    {
        error = String(F("Missing or invalid ")) + String(key);
        return false;
    }
    String parsed = value.as<String>();
    if (parsed.length() > maxLen)
    {
        error = String(key) + F(" too long");
        return false;
    }
    for (size_t i = 0; i < parsed.length(); i++)
    {
        if ((uint8_t)parsed[i] < 32)
        {
            error = String(key) + F(" contains control characters");
            return false;
        }
    }
    out = parsed;
    return true;
}

void BWC::_buildJSONStates(JsonObject doc)
{
    doc[F("CONTENT")] = F("STATES");
    doc[F("TIME")] = _timestamp_secs;
    doc[F("LCK")] = cio->cio_states.locked;
    doc[F("PWR")] = cio->cio_states.power;
    doc[F("UNT")] = cio->cio_states.unit;
    doc[F("AIR")] = cio->cio_states.bubbles;
    doc[F("GRN")] = cio->cio_states.heatgrn;
    doc[F("RED")] = cio->cio_states.heatred;
    doc[F("FLT")] = cio->cio_states.pump;
    doc[F("CH1")] = cio->cio_states.char1;
    doc[F("CH2")] = cio->cio_states.char2;
    doc[F("CH3")] = cio->cio_states.char3;
    doc[F("HJT")] = cio->cio_states.jets;
    doc[F("BRT")] = dsp->dsp_states.brightness;
    doc[F("BRTBASE")] = _dsp_brightness;
    doc[F("BRTOVR")] = (_override_dsp_brt_timer > 0);
    doc[F("ERR")] = cio->cio_states.error;
    doc[F("HLCAL")] = _hlcal.active;
    doc[F("SSA")] = _smart_schedule.active; // the dashboard shows a Smart Schedule note
    doc[F("GOD")] = (uint8_t)cio->cio_states.godmode;
    // A pool in standby never shows its target: report the last known one
    // instead of 0, the web UI greys it out while PWR is off.
    uint8_t tgt = cio->cio_states.target ? cio->cio_states.target : _last_target;
    doc[F("TGT")] = tgt;
    doc[F("TMP")] = cio->cio_states.temperature;
    doc[F("AMBC")] = _ambient_temp;
    doc[F("AMBF")] = round(C2F(_ambient_temp));
    if (cio->cio_states.unit)
    {
        // celsius
        doc[F("AMB")] = _ambient_temp;
        doc[F("TGTC")] = tgt;
        doc[F("TMPC")] = cio->cio_states.temperature;
        doc[F("TGTF")] = round(C2F((float)tgt));
        doc[F("TMPF")] = round(C2F((float)cio->cio_states.temperature));
    }
    else
    {
        // farenheit
        doc[F("AMB")] = round(C2F(_ambient_temp));
        doc[F("TGTF")] = tgt;
        doc[F("TMPF")] = cio->cio_states.temperature;
        doc[F("TGTC")] = round(F2C((float)tgt));
        doc[F("TMPC")] = round(F2C((float)cio->cio_states.temperature));
    }
}

size_t BWC::writeJSONStates(Print &out)
{
// feed the dog
#ifdef ESP8266
    ESP.wdtFeed();
#endif
    DynamicJsonDocument doc(1536);
    _buildJSONStates(doc.to<JsonObject>());
    return serializeJson(doc, out);
}

void BWC::getJSONStates(String &rtn)
{
    // Allocate a temporary JsonDocument
    // Don't forget to change the capacity to match your requirements.
    // Use arduinojson.org/assistant to compute the capacity.
// feed the dog
#ifdef ESP8266
    ESP.wdtFeed();
#endif
    DynamicJsonDocument doc(1536);
    _buildJSONStates(doc.to<JsonObject>());

    // Serialize JSON to string
    if (serializeJson(doc, rtn) == 0)
    {
        rtn = F("{\"error\": \"Failed to serialize states\"}");
    }
}


void BWC::_buildJSONTimes(JsonObject doc)
{
    doc[F("CONTENT")] = F("TIMES");
    doc[F("TIME")] = _timestamp_secs;
    doc[F("CLTIME")] = _cl_timestamp_s;
    doc[F("FTIME")] = _filter_timestamp_s;
    doc[F("FCTIME")] = _fc_timestamp_s;
    doc[F("WCTIME")] = _wc_timestamp_s;
    doc[F("UPTIME")] = _uptime + _uptime_ms / 1000;
    doc[F("PUMPTIME")] = _pumptime + _pumptime_ms / 1000;
    doc[F("HEATINGTIME")] = _heatingtime + _heatingtime_ms / 1000;
    doc[F("AIRTIME")] = _airtime + _airtime_ms / 1000;
    doc[F("JETTIME")] = _jettime + _jettime_ms / 1000;
    doc[F("COST")] = _energy_cost_total;
    doc[F("CLINT")] = _cl_interval;
    doc[F("FINT")] = _filter_interval;
    doc[F("FCINT")] = _fc_interval;
    doc[F("WCINT")] = _wc_interval;
    doc[F("PHTIME")] = _ph_timestamp_s;
    doc[F("PHINT")] = _ph_interval;
    doc[F("PHVAL")] = _last_ph_value;
    doc[F("CLVTIME")] = _clv_timestamp_s;
    doc[F("CLVAL")] = _last_cl_value;
    doc[F("CYATIME")] = _cya_timestamp_s;
    doc[F("CYAVAL")] = _last_cya_value;
    doc[F("ALKTIME")] = _alk_timestamp_s;
    doc[F("ALKVAL")] = _last_alk_value;
    doc[F("DPH")] = _dose_ph;
    doc[F("DPHTIME")] = _dose_ph_ts;
    doc[F("DCL")] = _dose_cl;
    doc[F("DCLTAB")] = _dose_cl_tab;
    doc[F("DCLTIME")] = _dose_cl_ts;
    doc[F("DTA")] = _dose_ta;
    doc[F("DTATIME")] = _dose_ta_ts;
    doc[F("KWH")] = _energy_total_kWh;
    doc[F("KWHD")] = (_energy_daily_Ws + _energy_pending_daily_Ws) / 3600000.0; // Ws -> kWh
    doc[F("COSTD")] = _energy_daily_cost + _energy_pending_daily_cost;
    doc[F("WATT")] = _energy_power_W;
    float t2r = _estHeatingTime();
    String t2r_string = F("Not ready");
    if (t2r == -2.0f)
        t2r_string = F("Ready");
    if (t2r == -1.0f)
        t2r_string = F("Never");
    doc[F("T2R")] = t2r;
    doc[F("RS")] = t2r_string;
    String s = cio->debug();
    doc[F("DBG")] = s;
    // cio->clk_per = 1000;  //reset minimum clock period
}

size_t BWC::writeJSONTimes(Print &out)
{
// feed the dog
#ifdef ESP8266
    ESP.wdtFeed();
#endif
    DynamicJsonDocument doc(1280); // 1024 + the dose keys
    _buildJSONTimes(doc.to<JsonObject>());
    return serializeJson(doc, out);
}

void BWC::getJSONTimes(String &rtn)
{
// Allocate a temporary JsonDocument
// Don't forget to change the capacity to match your requirements.
// Use arduinojson.org/assistant to compute the capacity.
// feed the dog
#ifdef ESP8266
    ESP.wdtFeed();
#endif
    DynamicJsonDocument doc(1280); // 1024 + the dose keys
    _buildJSONTimes(doc.to<JsonObject>());

    // Serialize JSON to string
    if (serializeJson(doc, rtn) == 0)
    {
        rtn = F("{\"error\": \"Failed to serialize times\"}");
    }
}

void BWC::buildJSONSettings(JsonObject doc)
{
// feed the dog
#ifdef ESP8266
    ESP.wdtFeed();
#endif
    // Serialize from live RAM. Do NOT _loadSettings() here: this is a getter,
    // and reloading stale flash (saved only every 600s) clobbered the live
    // energy/runtime counters, making total/daily jump backwards on every
    // settings poll.


    // Set the values in the document
    doc[F("CONTENT")] = F("SETTINGS");
    doc[F("PRICE")] = _price;
    doc[F("PRICE_NIGHT")] = _price_night;
    doc[F("NIGHT_ENABLED")] = _night_enabled;
    doc[F("NIGHT_START_MIN")] = _night_start_min;
    doc[F("NIGHT_END_MIN")] = _night_end_min;
    doc[F("NIGHT_WEEKEND")] = _night_weekend;
    doc[F("CLINT")] = _cl_interval;
    doc[F("FINT")] = _filter_interval;
    doc[F("FCINT")] = _fc_interval;
    doc[F("WCINT")] = _wc_interval;
    doc[F("PHINT")] = _ph_interval;
    doc[F("AUDIO")] = _audio_enabled;
    doc[F("SNDMASK")] = _sound_mask;
    doc[F("JETCD")] = _jet_countdown;
    doc[F("STBYORIG")] = _standby_original;
    doc[F("TIMEZONE")] = _timezone;
    doc[F("TIMEZONE_NAME")] = _timezone_name;
#ifdef ESP8266
    doc[F("REBOOTINFO")] = ESP.getResetReason();
#endif
    doc[F("REBOOTTIME")] = reboot_time_t;
    doc[F("MODEL")] = cio->getModel();
    doc[F("WEATHER")] = _weather;
    doc[F("CLOUD")] = _cloud_on; // weather needs it
    doc[F("HASJETS")] = cio->getHasjets();
    doc[F("POOLCAP")] = _pool_capacity;
    for (uint8_t i = 0; i < DOSE_FACTORS; i++)
        doc[DF_KEY(i)] = _dose_factor[i];
    doc[F("HLOSS")] = _heat_loss;
    doc[F("AIRTO")] = _airjet_timeout_minutes;
    doc[F("HJTO")] = _hydrojet_timeout_minutes;
    doc[F("GSTMODE")] = _host_mode;
    doc[F("GSTMAXTGT")] = _host_max_target_c;
    doc[F("GSTHTRTO")] = _host_heater_timeout_min;
    doc[F("BRTSTEP")] = _brightness_boost_steps;
    doc[F("BRTDUR")] = _brightness_boost_duration_s;
    doc[F("LCK")] = dsp->EnabledButtons[LOCK];
    doc[F("TMR")] = dsp->EnabledButtons[TIMER];
    doc[F("AIR")] = dsp->EnabledButtons[BUBBLES];
    doc[F("UNT")] = dsp->EnabledButtons[UNIT];
    doc[F("HTR")] = dsp->EnabledButtons[HEAT];
    doc[F("FLT")] = dsp->EnabledButtons[PUMP];
    doc[F("DN")] = dsp->EnabledButtons[DOWN];
    doc[F("UP")] = dsp->EnabledButtons[UP];
    doc[F("PWR")] = dsp->EnabledButtons[POWER];
    doc[F("HJT")] = dsp->EnabledButtons[HYDROJETS];

    // Serialize JSON to string
}

void BWC::getJSONSettings(String &rtn)
{
    DynamicJsonDocument doc(2048);
    buildJSONSettings(doc.to<JsonObject>());
    if (serializeJson(doc, rtn) == 0)
    {
        rtn = F("{\"error\": \"Failed to serialize settings\"}");
    }
}

// The command queue as columnar JSON, written straight to `out`. Same bytes as
// the ArduinoJson document it replaces (key order, escaping), without its 4 KB
// pool: {"LEN":n,"QEN":0|1,"CMD":[..],"VALUE":[..],"XTIME":[..],"INTERVAL":[..],
// "TXT":[..],"EN":[..]} - the arrays only when the queue is not empty.
static void printJsonString(Print &out, const String &s)
{
    out.write('"');
    for (size_t i = 0; i < s.length(); i++)
    {
        char c = s[i];
        switch (c) // the characters ArduinoJson 6 escapes
        {
        case '"': out.print(F("\\\"")); break;
        case '\\': out.print(F("\\\\")); break;
        case '\b': out.print(F("\\b")); break;
        case '\f': out.print(F("\\f")); break;
        case '\n': out.print(F("\\n")); break;
        case '\r': out.print(F("\\r")); break;
        case '\t': out.print(F("\\t")); break;
        case 0: out.print(F("\\u0000")); break;
        default: out.write(c);
        }
    }
    out.write('"');
}

void BWC::writeJSONCommandQueue(Print &out)
{
#ifdef ESP8266
    ESP.wdtFeed();
#endif
    const size_t n = _command_que.size();
    out.print(F("{\"LEN\":"));
    out.print((unsigned)n);
    out.print(F(",\"QEN\":"));
    out.print(_command_queue_enabled ? 1 : 0);
    if (n)
    {
        auto column = [&](const __FlashStringHelper *key, auto value)
        {
            out.print(F(",\""));
            out.print(key);
            out.print(F("\":["));
            for (size_t i = 0; i < n; i++)
            {
                if (i)
                    out.write(',');
                value(_command_que[i]);
            }
            out.write(']');
        };
        column(F("CMD"), [&](const command_que_item &c) { out.print((int)c.cmd); });
        column(F("VALUE"), [&](const command_que_item &c) { out.print((long long)c.val); });
        column(F("XTIME"), [&](const command_que_item &c) { out.print((unsigned long long)c.xtime); });
        column(F("INTERVAL"), [&](const command_que_item &c) { out.print((unsigned long)c.interval); });
        column(F("TXT"), [&](const command_que_item &c) { printJsonString(out, c.text); });
        column(F("EN"), [&](const command_que_item &c) { out.print(c.enabled ? 1 : 0); });
    }
    out.write('}');
}

namespace
{
struct CountPrint : public Print
{
    size_t n = 0;
    size_t write(uint8_t) override { return ++n, 1; }
    size_t write(const uint8_t *, size_t len) override { return n += len, len; }
};
struct StringPrint : public Print
{
    String &s;
    explicit StringPrint(String &str) : s(str) {}
    size_t write(uint8_t c) override { return s.concat((char)c) ? 1 : 0; }
};
struct HashPrint : public Print
{
    uint32_t h = 2166136261u; // FNV-1a
    size_t write(uint8_t c) override { h = (h ^ c) * 16777619u; return 1; }
};
}

uint32_t BWC::commandQueueHash()
{
    HashPrint hp;
    writeJSONCommandQueue(hp);
    return hp.h;
}

void BWC::getJSONCommandQueue(String &rtn)
{
    CountPrint count;
    writeJSONCommandQueue(count);
    rtn.clear();
    rtn.reserve(count.n); // one exact block instead of a growing String
    StringPrint out(rtn);
    writeJSONCommandQueue(out);
}

// Epoch below which the clock has clearly not reached NTP yet. Same threshold
// the web auth and boot log use (src/web/auth.cpp VALID_EPOCH_THRESHOLD).
static constexpr uint32_t CLOUD_VALID_EPOCH = 57600;

// One unix stamp, or null when it predates the clock being set.
static void _cloudStamp(JsonObject doc, const __FlashStringHelper* key, uint32_t stamp)
{
    if (stamp > CLOUD_VALID_EPOCH)
        doc[key] = stamp;
    else
        doc[key] = nullptr;
}

// A water care reading and its stamp. The stamp is only written when a value is
// actually entered, so it is the validity flag for the value as well. Either
// both go out or both are null.
static void _cloudWaterCare(JsonObject doc, const __FlashStringHelper* vkey,
                            const __FlashStringHelper* tkey, int32_t value, uint32_t stamp)
{
    if (stamp > CLOUD_VALID_EPOCH)
    {
        doc[vkey] = value;
        doc[tkey] = stamp;
    }
    else
    {
        doc[vkey] = nullptr;
        doc[tkey] = nullptr;
    }
}

void BWC::buildCloudSnapshot(JsonObject doc, size_t* state_len_out)
{
    // Two sections, and the order matters. Everything the cloud treats as state
    // comes first, then the values that move on their own (runtimes, energy,
    // the clock). CloudTask hashes only the first section to decide whether
    // anything actually changed. See _sendSensorData.
    //
    // A value we have never read is sent as null, not as the struct default: a
    // fabricated 25 °C or pH 7.2 is indistinguishable from a measurement once
    // it is in the cloud. Under the wire contract null means "don't know" and
    // an absent key means "unchanged", so a full snapshot must spell out null.

    // States: compact keys identical to _buildJSONStates so the cloud UI can
    // mirror the local dashboard 1:1. Temperatures in the display unit + UNT.
    doc[F("FULL")] = 1;  // complete snapshot, not a delta
    doc[F("UNT")] = cio->cio_states.unit;
    if (cio->cio_states.tmp_ok)
        doc[F("TMP")] = cio->cio_states.temperature;
    else
        doc[F("TMP")] = nullptr;
    if (cio->cio_states.tgt_ok)
        doc[F("TGT")] = cio->cio_states.target;
    else
        doc[F("TGT")] = nullptr;
    if (_ambient_set)
        doc[F("AMB")] = cio->cio_states.unit ? _ambient_temp : round(C2F(_ambient_temp));
    else
        doc[F("AMB")] = nullptr;
    doc[F("BRTBASE")] = _dsp_brightness;  // base level; BRT may be momentarily boosted
    doc[F("LCK")] = cio->cio_states.locked;
    doc[F("PWR")] = cio->cio_states.power;
    doc[F("AIR")] = cio->cio_states.bubbles;
    doc[F("FLT")] = cio->cio_states.pump;
    doc[F("HJT")] = cio->cio_states.jets;
    doc[F("RED")] = cio->cio_states.heatred;
    doc[F("GRN")] = cio->cio_states.heatgrn;
    doc[F("BRT")] = _dsp_brightness;  // base level, not a momentary boost
    doc[F("HASJETS")] = hasjets;
    doc[F("HLCAL")] = _hlcal.active; // calibration running: cloud UI locks
    doc[F("ERR")] = cio->cio_states.error;
    if (cio->cio_states.dsp_ok)
    {
        char dsp[4] = {(char)cio->cio_states.char1, (char)cio->cio_states.char2,
                       (char)cio->cio_states.char3, 0};
        doc[F("DSP")] = String(dsp);  // String forces a copy into the doc
    }
    else
        doc[F("DSP")] = nullptr;

    // Water care values (tenths, like the local UI) + last-measured stamps. The
    // stamp doubles as the validity flag: it is only written when a value is
    // actually entered, so a pre-NTP or zero stamp means "never measured".
    _cloudWaterCare(doc, F("PHVAL"),  F("PHTIME"),  _last_ph_value,  _ph_timestamp_s);
    _cloudWaterCare(doc, F("CLVAL"),  F("CLVTIME"), _last_cl_value,  _clv_timestamp_s);
    _cloudWaterCare(doc, F("CYAVAL"), F("CYATIME"), _last_cya_value, _cya_timestamp_s);
    _cloudWaterCare(doc, F("ALKVAL"), F("ALKTIME"), _last_alk_value, _alk_timestamp_s);

    // Maintenance timers (unix ts) + their intervals (days). Same rule: a stamp
    // from before the clock was set would claim "last changed in 1970".
    _cloudStamp(doc, F("CLTIME"), _cl_timestamp_s);
    doc[F("CLINT")]  = _cl_interval;
    _cloudStamp(doc, F("FTIME"), _filter_timestamp_s);
    doc[F("FINT")]   = _filter_interval;
    _cloudStamp(doc, F("FCTIME"), _fc_timestamp_s);
    doc[F("FCINT")]  = _fc_interval;
    _cloudStamp(doc, F("WCTIME"), _wc_timestamp_s);
    doc[F("WCINT")]  = _wc_interval;

    // Last care product added: grams, sign = minus/plus product;
    // chlorine in g or tablets (DCLTAB). Same null rule as the readings.
    _cloudWaterCare(doc, F("DPH"), F("DPHTIME"), _dose_ph, _dose_ph_ts);
    _cloudWaterCare(doc, F("DCL"), F("DCLTIME"), _dose_cl, _dose_cl_ts);
    doc[F("DCLTAB")] = _dose_cl_tab;
    _cloudWaterCare(doc, F("DTA"), F("DTATIME"), _dose_ta, _dose_ta_ts);

    // Smart schedule status (device-side fields of getJSONSmartSchedule; the
    // backend derives buffer/cost/countdowns from these + settings PRICE)
    JsonObject ss = doc.createNestedObject(F("SS"));
    ss[F("ACTIVE")]     = _smart_schedule.active;
    ss[F("TARGETTIME")] = _smart_schedule.target_time;
    ss[F("TARGETTEMP")] = _smart_schedule.target_temp;
    ss[F("KEEPON")]     = _smart_schedule.keep_heater_on;
    ss[F("KEEPPUMP")]   = _smart_schedule.keep_pump_on;
    ss[F("REPEAT")]     = _smart_schedule.repeat_days;
    ss[F("STARTTIME")]  = _smart_schedule.calculated_start_time;
    ss[F("NEXTCHECK")]  = _smart_schedule.next_check_time;
    ss[F("ESTIMATE")]   = _smart_schedule.last_heating_estimate;
    ss[F("CHECKCOMPLETED")] = _smart_schedule.check_completed;
    ss[F("READING_STATE")]  = _smart_schedule.temp_reading_state;
    // Accurate temp: live reading while the pump circulates, else last stored.
    // Null rather than the fabricated default when no temperature was ever read.
    if (_smart_schedule.accurate_temperature > 0 && !cio->cio_states.pump)
        ss[F("ACCURATETEMP")] = _smart_schedule.accurate_temperature;
    else if (cio->cio_states.tmp_ok)
        ss[F("ACCURATETEMP")] = cio->cio_states.temperature;
    else
        ss[F("ACCURATETEMP")] = nullptr;

    // End of the state section: everything below moves on its own, so CloudTask
    // hashes only up to here when deciding whether anything really changed.
    if (state_len_out)
        *state_len_out = measureJson(doc);

    // Runtime totals
    doc[F("UPTIME")]      = _uptime + _uptime_ms / 1000;
    doc[F("PUMPTIME")]    = _pumptime + _pumptime_ms / 1000;
    doc[F("HEATINGTIME")] = _heatingtime + _heatingtime_ms / 1000;
    doc[F("AIRTIME")]     = _airtime + _airtime_ms / 1000;
    doc[F("JETTIME")]     = _jettime + _jettime_ms / 1000;

    // Energy
    doc[F("WATT")] = _energy_power_W;
    doc[F("KWH")]  = _energy_total_kWh;
    doc[F("KWHD")] = (_energy_daily_Ws + _energy_pending_daily_Ws) / 3600000.0;
    doc[F("COSTD")] = _energy_daily_cost + _energy_pending_daily_cost;
    doc[F("COST")] = _energy_cost_total;
    doc[F("T2R")]  = _estHeatingTime();  // hours; -2 = ready, -1 = never

    // The reading's own timestamp. Omitted (not null) before NTP: the cloud
    // stamps arrival time itself, and a wrong clock is worse than no clock.
    time_t now = time(nullptr);
    if (now > (time_t)CLOUD_VALID_EPOCH)
        doc[F("TS")] = (uint32_t)now;
}

bool BWC::setJSONSettings(const String &message, String &errorMessage)
{
    // feed the dog
    //  ESP.wdtFeed();
    DynamicJsonDocument doc(2048);

    // Deserialize the JSON document
    DeserializationError error = deserializeJson(doc, message);
    if (error)
    {
        errorMessage = F("Error deserializing message");
        return false;
    }
    if (!doc.is<JsonObject>())
    {
        errorMessage = F("Settings payload must be an object");
        return false;
    }

    JsonObjectConst obj = doc.as<JsonObjectConst>();
    double price = _price;
    double priceNight = _price_night;
    bool nightEnabled = _night_enabled;
    int64_t nightStartMin = _night_start_min;
    int64_t nightEndMin = _night_end_min;
    bool nightWeekend = _night_weekend;
    int64_t clInterval = _cl_interval;
    int64_t filterInterval = _filter_interval;
    int64_t fcInterval = _fc_interval;
    int64_t wcInterval = _wc_interval;
    int64_t phInterval = _ph_interval;
    bool audioEnabled = _audio_enabled;
    int64_t soundMask = _sound_mask;
    bool jetCountdown = _jet_countdown;
    bool standbyOriginal = _standby_original;
    String timezone = _timezone;
    String timezoneName = _timezone_name;
    bool weather = _weather;
    int64_t poolCapacity = _pool_capacity;
    int64_t airjetTimeout = _airjet_timeout_minutes;
    int64_t hydrojetTimeout = _hydrojet_timeout_minutes;
    bool hostMode = _host_mode;
    int64_t hostMaxTarget = _host_max_target_c;
    int64_t hostHeaterTimeout = _host_heater_timeout_min;
    int64_t brightnessSteps = _brightness_boost_steps;
    int64_t brightnessDuration = _brightness_boost_duration_s;
    bool buttonEnabled[HYDROJETS + 1];
    for (uint8_t i = 0; i <= HYDROJETS; i++)
        buttonEnabled[i] = dsp->EnabledButtons[i];

    if (!readBoundedNumber(obj, F("PRICE"), 0.0, 100.0, price, errorMessage) ||
        !readBoundedNumber(obj, F("PRICE_NIGHT"), 0.0, 100.0, priceNight, errorMessage) ||
        !readBooleanSetting(obj, F("NIGHT_ENABLED"), nightEnabled, errorMessage) ||
        !readBoundedInteger(obj, F("NIGHT_START_MIN"), 0, 1439, nightStartMin, errorMessage) ||
        !readBoundedInteger(obj, F("NIGHT_END_MIN"), 0, 1439, nightEndMin, errorMessage) ||
        !readBooleanSetting(obj, F("NIGHT_WEEKEND"), nightWeekend, errorMessage) ||
        !readBoundedInteger(obj, F("CLINT"), 0, 3650, clInterval, errorMessage) ||
        !readBoundedInteger(obj, F("FINT"), 0, 3650, filterInterval, errorMessage) ||
        !readBoundedInteger(obj, F("FCINT"), 0, 3650, fcInterval, errorMessage) ||
        !readBoundedInteger(obj, F("WCINT"), 0, 3650, wcInterval, errorMessage) ||
        !readBoundedInteger(obj, F("PHINT"), 0, 3650, phInterval, errorMessage) ||
        !readBooleanSetting(obj, F("AUDIO"), audioEnabled, errorMessage) ||
        !readBoundedInteger(obj, F("SNDMASK"), 0, SND_ALL, soundMask, errorMessage) ||
        !readBooleanSetting(obj, F("JETCD"), jetCountdown, errorMessage) ||
        !readBooleanSetting(obj, F("STBYORIG"), standbyOriginal, errorMessage) ||
        !readBoundedString(obj, F("TIMEZONE"), 96, timezone, errorMessage) ||
        !readBoundedString(obj, F("TIMEZONE_NAME"), 64, timezoneName, errorMessage) ||
        !readBooleanSetting(obj, F("WEATHER"), weather, errorMessage) ||
        !readBoundedInteger(obj, F("POOLCAP"), 1, 100000, poolCapacity, errorMessage) ||
        !readBoundedInteger(obj, F("AIRTO"), 1, 30, airjetTimeout, errorMessage) ||
        !readBoundedInteger(obj, F("HJTO"), 1, 60, hydrojetTimeout, errorMessage) ||
        !readBooleanSetting(obj, F("GSTMODE"), hostMode, errorMessage) ||
        !readBoundedInteger(obj, F("GSTMAXTGT"), 20, 40, hostMaxTarget, errorMessage) ||
        !readBoundedInteger(obj, F("GSTHTRTO"), 0, 1440, hostHeaterTimeout, errorMessage) ||
        !readBoundedInteger(obj, F("BRTSTEP"), 1, 8, brightnessSteps, errorMessage) ||
        !readBoundedInteger(obj, F("BRTDUR"), 1, 60, brightnessDuration, errorMessage) ||
        !readBooleanSetting(obj, F("LCK"), buttonEnabled[LOCK], errorMessage) ||
        !readBooleanSetting(obj, F("TMR"), buttonEnabled[TIMER], errorMessage) ||
        !readBooleanSetting(obj, F("AIR"), buttonEnabled[BUBBLES], errorMessage) ||
        !readBooleanSetting(obj, F("UNT"), buttonEnabled[UNIT], errorMessage) ||
        !readBooleanSetting(obj, F("HTR"), buttonEnabled[HEAT], errorMessage) ||
        !readBooleanSetting(obj, F("FLT"), buttonEnabled[PUMP], errorMessage) ||
        !readBooleanSetting(obj, F("DN"), buttonEnabled[DOWN], errorMessage) ||
        !readBooleanSetting(obj, F("UP"), buttonEnabled[UP], errorMessage) ||
        !readBooleanSetting(obj, F("PWR"), buttonEnabled[POWER], errorMessage) ||
        !readBooleanSetting(obj, F("HJT"), buttonEnabled[HYDROJETS], errorMessage))
    {
        return false;
    }
    int64_t doseFactor[DOSE_FACTORS];
    for (uint8_t i = 0; i < DOSE_FACTORS; i++)
    {
        doseFactor[i] = _dose_factor[i];
        if (!readBoundedInteger(obj, DF_KEY(i), 1, 250, doseFactor[i], errorMessage))
            return false;
    }

    // Copy values from the JsonDocument to the variables.
    // Only assign keys actually present in the POST body, otherwise a partial
    // update clobbers untouched settings to 0/false.
    if (doc.containsKey(F("PRICE")))
        _price = roundCents(price);
    if (doc.containsKey(F("PRICE_NIGHT")))
        _price_night = roundCents(priceNight);
    if (doc.containsKey(F("NIGHT_ENABLED")))
        _night_enabled = nightEnabled;
    if (doc.containsKey(F("NIGHT_START_MIN")))
        _night_start_min = (uint16_t)nightStartMin;
    if (doc.containsKey(F("NIGHT_END_MIN")))
        _night_end_min = (uint16_t)nightEndMin;
    if (doc.containsKey(F("NIGHT_WEEKEND")))
        _night_weekend = nightWeekend;
    if (doc.containsKey(F("CLINT")))
        _cl_interval = (uint32_t)clInterval;
    if (doc.containsKey(F("FINT")))
        _filter_interval = (uint32_t)filterInterval;
    if (doc.containsKey(F("FCINT")))
        _fc_interval = (uint32_t)fcInterval;
    if (doc.containsKey(F("WCINT")))
        _wc_interval = (uint32_t)wcInterval;
    if (doc.containsKey(F("PHINT")))
        _ph_interval = (uint32_t)phInterval;
    if (doc.containsKey(F("AUDIO")))
        _audio_enabled = audioEnabled;
    if (doc.containsKey(F("SNDMASK")))
        _sound_mask = (uint16_t)soundMask;
    if (doc.containsKey(F("JETCD")))
        _jet_countdown = jetCountdown;
    if (doc.containsKey(F("STBYORIG")))
        _standby_original = standbyOriginal;
    if (doc.containsKey(F("TIMEZONE")))
        _timezone = timezone;
    if (_timezone.length() == 0)
        _timezone = defaultTimezone;
    if (doc.containsKey(F("TIMEZONE_NAME")))
        _timezone_name = timezoneName;
    if (_timezone_name.length() == 0)
        _timezone_name = defaultTimezoneName;
    applyTimezone();
    // Without the cloud there is no weather source: WEATHER stays off and the
    // "switch back on with the cloud" mark stays as it is.
    if (doc.containsKey(F("WEATHER")) && _cloud_on)
    {
        _weather = weather;
        _weather_auto_off = false; // the user decided
    }
    if (doc.containsKey(F("POOLCAP")))
        _pool_capacity = (int)poolCapacity;
    for (uint8_t i = 0; i < DOSE_FACTORS; i++)
        _dose_factor[i] = (uint8_t)doseFactor[i]; // unchanged unless sent
    // Airjet timeout: 5-30 minutes, default 30
    if (doc.containsKey(F("AIRTO")))
    {
        _airjet_timeout_minutes = (uint8_t)airjetTimeout;
    }
    // Hydrojet timeout: 5-60 minutes, default 60
    if (doc.containsKey(F("HJTO")))
    {
        _hydrojet_timeout_minutes = (uint8_t)hydrojetTimeout;
    }
    const bool entering_host_mode = doc.containsKey(F("GSTMODE")) && hostMode && !_host_mode;
    if (doc.containsKey(F("GSTMODE")))
        _host_mode = hostMode;
    // Host mode max target: 20-40 C, default 40
    if (doc.containsKey(F("GSTMAXTGT")))
        _host_max_target_c = (uint8_t)hostMaxTarget;
    // Web/MQTT targets are intentionally unrestricted, so a target set above the
    // guest limit before host mode was switched on would simply stay active for
    // the guests. Bring it down to the limit; a lower target is left alone.
    // Queued (not written to cio_toggles) because this runs in an HTTP handler.
    if (entering_host_mode &&
        displayTempC(cio->cio_states.unit, (float)cio->cio_states.target) > (float)_host_max_target_c)
    {
        command_que_item item;
        item.cmd = SETTARGET;
        item.val = _host_max_target_c; // 20-40 -> read as Celsius by SETTARGET
        item.xtime = 0;
        item.interval = 0;
        item.text = "";
        add_command(item);
        Serial.printf("Host mode: target above guest limit - queued %d C\n", _host_max_target_c);
    }
    // Host mode heater timeout: 0-1440 minutes, 0 = disabled
    if (doc.containsKey(F("GSTHTRTO")))
        _host_heater_timeout_min = (uint16_t)hostHeaterTimeout;
    // Brightness boost steps: 1-8, default 1
    if (doc.containsKey(F("BRTSTEP")))
    {
        _brightness_boost_steps = (uint8_t)brightnessSteps;
    }
    // Brightness boost duration: 1-60s, default 5
    if (doc.containsKey(F("BRTDUR")))
    {
        _brightness_boost_duration_s = (uint16_t)brightnessDuration;
    }
    if (doc.containsKey(F("LCK")))
        dsp->EnabledButtons[LOCK] = buttonEnabled[LOCK];
    if (doc.containsKey(F("TMR")))
        dsp->EnabledButtons[TIMER] = buttonEnabled[TIMER];
    if (doc.containsKey(F("AIR")))
        dsp->EnabledButtons[BUBBLES] = buttonEnabled[BUBBLES];
    if (doc.containsKey(F("UNT")))
        dsp->EnabledButtons[UNIT] = buttonEnabled[UNIT];
    if (doc.containsKey(F("HTR")))
        dsp->EnabledButtons[HEAT] = buttonEnabled[HEAT];
    if (doc.containsKey(F("FLT")))
        dsp->EnabledButtons[PUMP] = buttonEnabled[PUMP];
    if (doc.containsKey(F("DN")))
        dsp->EnabledButtons[DOWN] = buttonEnabled[DOWN];
    if (doc.containsKey(F("UP")))
        dsp->EnabledButtons[UP] = buttonEnabled[UP];
    if (doc.containsKey(F("PWR")))
        dsp->EnabledButtons[POWER] = buttonEnabled[POWER];
    if (doc.containsKey(F("HJT")))
        dsp->EnabledButtons[HYDROJETS] = buttonEnabled[HYDROJETS];
    // Saved by loop(), not here: from the cloud this runs at the bottom of the
    // PoolLink command handler, and LittleFS on top of that (a directory
    // compaction now and then) ran out of the 4 KB stack - a hardware watchdog
    // reset on "save" in the cloud. The members are set, so no reload either.
    _save_settings_needed = true;
    return true;
}
