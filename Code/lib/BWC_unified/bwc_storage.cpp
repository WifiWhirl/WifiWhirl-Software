#include "bwc_internal.h"

namespace
{
bool json01(JsonVariantConst value, bool fallback)
{
    if (value.isNull())
        return fallback;
    if (value.is<bool>())
        return fallback;
    if (value.is<int>())
    {
        int numeric = value.as<int>();
        if (numeric == 0 || numeric == 1)
            return numeric == 1;
    }
    return fallback;
}
}

static bool readHardwareIntegerValue(const JsonVariantConst &value, int &out)
{
    if (value.is<int>())
    {
        out = value.as<int>();
        return true;
    }
    if (!isJsonString(value))
        return false;
    String text = value.as<String>();
    text.trim();
    if (text.length() == 0)
        return false;
    for (size_t i = 0; i < text.length(); i++)
    {
        if (!isDigit(text[i]))
            return false;
    }
    out = text.toInt();
    return true;
}

static bool validModelValue(int model)
{
    return model >= PRE2021 && model <= MSPA;
}

/*          */
/* LOADERS  */
/*          */

bool BWC::_loadHardware(Models &cioNo, Models &dspNo)
{
    File file = LittleFS.open("/hwcfg.json", "r");
    if (!file)
    {
        Serial.println(F("FS: Failed to open /hwcfg.json for read"));
        return false;
    }
    // DynamicJsonDocument doc(256);
    StaticJsonDocument<272> doc;
    DeserializationError error = deserializeJson(doc, file);
    if (error)
    {
        // Serial.println(F("Failed to read settings.txt"));
        file.close();
        return false;
    }
    file.close();
    if (!doc.is<JsonObject>())
        return false;

    int cioModel;
    if (!readHardwareIntegerValue(doc[F("cio")], cioModel) || !validModelValue(cioModel))
    {
        return false;
    }
    int dspModel = cioModel;
    if (doc.containsKey(F("dsp")) &&
        (!readHardwareIntegerValue(doc[F("dsp")], dspModel) || !validModelValue(dspModel)))
    {
        return false;
    }
    cioNo = (Models)cioModel;
    dspNo = (Models)dspModel;

    return true;
}

void BWC::reloadSettings()
{
    _loadSettings();
    return;
}

void BWC::_loadSettings()
{
    File file = LittleFS.open("/settings.json", "r");
    if (!file)
    {
        Serial.println(F("Failed to open settings.json"));
        applyTimezone();
        return;
    }
    Serial.printf("settings.json size: %d bytes\n", file.size());
    DynamicJsonDocument doc(2048);

    // Deserialize the JSON document
    DeserializationError error = deserializeJson(doc, file);
    if (error)
    {
        Serial.printf("Failed to deserialize settings.json: %s\n", error.c_str());
        file.close();
        applyTimezone();
        return;
    }
    Serial.println(F("settings.json loaded successfully"));

    // Copy values from the JsonDocument to the variables
    _cl_timestamp_s = doc[F("CLTIME")];
    _filter_timestamp_s = doc[F("FTIME")];
    _fc_timestamp_s = doc[F("FCTIME")];
    _wc_timestamp_s = doc[F("WCTIME")];
    _ph_timestamp_s = doc[F("PHTIME")] | _ph_timestamp_s;
    _clv_timestamp_s = doc[F("CLVTIME")] | _clv_timestamp_s;
    _uptime = doc[F("UPTIME")];
    _pumptime = doc[F("PUMPTIME")];
    _heatingtime = doc[F("HEATINGTIME")];
    _airtime = doc[F("AIRTIME")];
    _jettime = doc[F("JETTIME")];
    // Quantize to cents. Older firmware kept the price in a float, so widening
    // it into the JSON double wrote 0.349999994 instead of 0.35 into
    // settings.json - without this those files stay ugly forever.
    _price = roundCents(doc[F("PRICE")] | 0.35);
    _price_night = roundCents(doc[F("PRICE_NIGHT")] | _price);
    _night_enabled = doc[F("NIGHT_ENABLED")] | false;
    _night_start_min = doc[F("NIGHT_START_MIN")] | 1320;
    _night_end_min = doc[F("NIGHT_END_MIN")] | 360;
    _night_weekend = doc[F("NIGHT_WEEKEND")] | false;
    _cl_interval = doc[F("CLINT")] | 7;
    _filter_interval = doc[F("FINT")] | 30;
    _fc_interval = doc[F("FCINT")] | 60;
    _wc_interval = doc[F("WCINT")] | 90;
    _ph_interval = doc[F("PHINT")] | 3;
    _last_ph_value = doc[F("PHVAL")] | 72;
    _last_cl_value = doc[F("CLVAL")] | 10;
    _last_cya_value = doc[F("CYAVAL")] | 300;
    _last_alk_value = doc[F("ALKVAL")] | 100;
    _cya_timestamp_s = doc[F("CYATIME")] | 0;
    _alk_timestamp_s = doc[F("ALKTIME")] | 0;
    _dose_ph = doc[F("DPH")] | 0;
    _dose_ph_ts = doc[F("DPHTIME")] | 0;
    _dose_cl = doc[F("DCL")] | 0;
    _dose_cl_tab = doc[F("DCLTAB")] | false;
    _dose_cl_ts = doc[F("DCLTIME")] | 0;
    _dose_ta = doc[F("DTA")] | 0;
    _dose_ta_ts = doc[F("DTATIME")] | 0;
    _audio_enabled = doc[F("AUDIO")] | true;
    _sound_mask = (doc[F("SNDMASK")] | (int)SND_ALL) & SND_ALL;
    _jet_countdown = doc[F("JETCD")] | false;
    _standby_original = doc[F("STBYORIG")] | true;
    _energy_total_kWh = doc[F("KWH")] | 0.0;
    _energy_daily_Ws = doc[F("KWHD")] | 0.0;
    _energy_pending_daily_Ws = doc[F("KWHD_PENDING")] | 0.0;
    _energy_daily_yday = doc[F("KWHD_DAY")] | -1;
    // Upgrade migration: seed the cost accumulators from the old derived values
    if (doc.containsKey(F("COST")))
        _energy_cost_total = doc[F("COST")];
    else
        _energy_cost_total = _energy_total_kWh * _price;
    if (doc.containsKey(F("COSTD")))
    {
        _energy_daily_cost = doc[F("COSTD")];
        _energy_pending_daily_cost = doc[F("COSTD_PENDING")] | 0.0;
    }
    else
    {
        _energy_daily_cost = (_energy_daily_Ws / 3600000.0) * _price;
        _energy_pending_daily_cost = (_energy_pending_daily_Ws / 3600000.0) * _price;
    }
    _timezone = doc[F("TIMEZONE")] | defaultTimezone;
    if (_timezone.length() == 0)
        _timezone = defaultTimezone;
    _timezone_name = doc[F("TIMEZONE_NAME")] | defaultTimezoneName;
    if (_timezone_name.length() == 0)
        _timezone_name = defaultTimezoneName;
    applyTimezone();
    _ambient_temp = doc[F("AMB")] | 20;
    _dsp_brightness = doc[F("BRT")] | 7;
    _weather = doc[F("WEATHER")] | false;
    _weather_auto_off = doc[F("WEATHERAUTO")] | false;
    _pool_capacity = doc[F("POOLCAP")] | 700;
    for (uint8_t i = 0; i < DOSE_FACTORS; i++)
        _dose_factor[i] = doc[DF_KEY(i)] | DOSE_FACTOR_DEFAULTS[i];
    _heat_loss = clampHeatLoss(doc[F("HLOSS")] | HEAT_LOSS_DEFAULT);
    _hlcal_planned_at = doc[F("HLCALAT")] | (uint64_t)0;
    _airjet_timeout_minutes = doc[F("AIRTO")] | 30;
    _hydrojet_timeout_minutes = doc[F("HJTO")] | 60;
    _host_mode = doc[F("GSTMODE")] | false;
    _host_max_target_c = doc[F("GSTMAXTGT")] | 40;
    _host_heater_timeout_min = doc[F("GSTHTRTO")] | 0;
    _brightness_boost_steps = doc[F("BRTSTEP")] | 1;
    _brightness_boost_duration_s = doc[F("BRTDUR")] | 5;
    // Saved as true/false: "| 1" (an int default) ignored a stored false, so a
    // disabled button was enabled again on the very next load.
    auto enabled = [&](const __FlashStringHelper *key)
    { return doc[key].isNull() || doc[key].as<bool>(); };
    dsp->EnabledButtons[LOCK] = enabled(F("LCK"));
    dsp->EnabledButtons[TIMER] = enabled(F("TMR"));
    dsp->EnabledButtons[BUBBLES] = enabled(F("AIR"));
    dsp->EnabledButtons[UNIT] = enabled(F("UNT"));
    dsp->EnabledButtons[HEAT] = enabled(F("HTR"));
    dsp->EnabledButtons[PUMP] = enabled(F("FLT"));
    dsp->EnabledButtons[DOWN] = enabled(F("DN"));
    dsp->EnabledButtons[UP] = enabled(F("UP"));
    dsp->EnabledButtons[POWER] = enabled(F("PWR"));
    dsp->EnabledButtons[HYDROJETS] = enabled(F("HJT"));

    Serial.printf("Loaded: PRICE=%.2f, WEATHER=%d\n", _price, _weather);
    file.close();
}
void BWC::_restoreStates()
{
    File file = LittleFS.open("/states.txt", "r");
    if (!file)
    {
        Serial.println(F("FS: Failed to open /states.txt for read"));
        return;
    }
    // DynamicJsonDocument doc(512);
    StaticJsonDocument<512> doc;
    // Deserialize the JSON document
    DeserializationError error = deserializeJson(doc, file);
    if (error)
    {
        // Serial.println(F("Failed to deserialize states.txt"));
        file.close();
        return;
    }

    uint8_t unt = doc[F("UNT")];
    uint8_t flt = doc[F("FLT")];
    uint8_t htr = doc[F("HTR")];
    uint8_t tgt = doc[F("TGT")] | 20;
    uint8_t god = doc[F("GOD")];
    command_que_item item;
    item.cmd = SETGODMODE;
    item.val = god;
    item.xtime = 0;
    item.interval = 0;
    item.text = "";
    add_command(item);
    // On before the rest: after a power cut the pool comes up in standby. Only
    // if it was on; switched off on the panel it stays off. Files
    // from older firmware have no PWR: nothing to switch.
    if ((doc[F("PWR")] | 0) == 1)
    {
        item.cmd = TOGGLEPWR;
        item.val = 1; // wanted state
        add_command(item);
    }
    // SETTARGET before SETUNIT: SETUNIT converts the pending web target into
    // the new unit; the old order re-converted it into the previous unit.
    item.cmd = SETTARGET;
    item.val = tgt;
    item.xtime = 0;
    item.interval = 0;
    item.text = "";
    if (add_command(item))
        _restore_target_pending = true; // see SETTARGET
    item.cmd = SETUNIT;
    item.val = unt;
    item.xtime = 0;
    item.interval = 0;
    item.text = "";
    add_command(item);
    item.cmd = SETPUMP;
    item.val = flt;
    item.xtime = 0;
    item.interval = 0;
    item.text = "";
    add_command(item);
    item.cmd = SETHEATER;
    item.val = htr;
    item.xtime = 0;
    item.interval = 0;
    item.text = "";
    add_command(item);
    // Serial.println(F("Restoring states"));
    file.close();
}

// After a cmdq.json upload: the queue only. The boot part of loadCommandQueue()
// re-read settings.json, folded the RTC counters in again (counting the unsaved
// milliseconds twice, days per upload) and re-ran the boot restore.
void BWC::reloadCommandQueue()
{
    loadCommandQueue(true);
}

void BWC::loadCommandQueue(bool queue_only)
{
    File file = LittleFS.open("/cmdq.json", "r");
    if (file)
    {
        DynamicJsonDocument doc(COMMAND_QUEUE_JSON_CAPACITY);
        DeserializationError error = deserializeJson(doc, file);
        if (error)
        {
            Serial.printf("Failed to deserialize cmdq.json: %s\n", error.c_str());
        }
        else
        {
            _command_queue_enabled = json01(doc[F("QEN")], true);
            _command_que.clear();
            for (int i = 0; i < doc[F("LEN")]; i++)
            {
                command_que_item item;
                item.cmd = doc[F("CMD")][i];
                item.val = doc[F("VALUE")][i];
                item.xtime = doc[F("XTIME")][i];
                item.interval = doc[F("INTERVAL")][i];
                String s = doc[F("TXT")][i] | "";
                item.text = s;
                item.enabled = json01(doc[F("EN")][i], true);
                item.scheduled = true; // anything that survived a reboot is a planned entry
                // Fast-forward a repeating command to its next future fire time.
                // xtime comes from a file, so a stale value with a small interval
                // could otherwise spin this for billions of iterations after NTP
                // sync -> watchdog reboot loop. Compute the catch-up in one step.
                uint64_t now = (uint64_t)time(nullptr);
                if (item.interval > 0 && item.xtime < now)
                {
                    item.xtime = nextRunAfter(item.xtime, item.interval, now, true);
                }
                _command_que.push_back(item);
            }
            std::stable_sort(_command_que.begin(), _command_que.end(), _compare_command);
        }
        file.close();
    }
    else
    {
        Serial.println(F("FS: /cmdq.json not found - continuing without command queue"));
    }
    if (queue_only)
        return;

    /*
     * Settings, states and smart-schedule must ALWAYS load, regardless of
     * whether cmdq.json was present or parseable.  Previously these calls
     * were gated behind a successful cmdq parse, which meant a missing or
     * corrupt cmdq.json silently left RAM at constructor defaults - and
     * the periodic save ticker then overwrote the good settings.json.
     */
    Serial.println(F("Calling _loadSettings()..."));
    _loadSettings();
    _restoreCountersFromRtc();
    // Restore is deferred to loop(): SETTARGET converts °C/°F against
    // cio_states.unit, which is still the constructor default here. Restoring
    // now turned a saved 38 °C into 100 °F and the module pressed UP/DOWN to
    // 40/20 until someone touched a temperature button.
    _restore_states_pending = true;
    // the target is shown before the restore runs (up to 60 s in standby)
    File states = LittleFS.open("/states.txt", "r");
    if (states)
    {
        StaticJsonDocument<256> sdoc;
        if (!deserializeJson(sdoc, states))
        {
            _last_target = sdoc[F("TGT")] | 0;
            _saved_power = (sdoc[F("PWR")] | 0) == 1;
        }
        states.close();
    }
    Serial.println(F("Calling _loadSmartSchedule()..."));
    _loadSmartSchedule();
}

/*          */
/* SAVERS   */
/*          */

void BWC::saveRebootInfo()
{
    File file = LittleFS.open("/bootlog.txt", "a");
    if (!file)
    {
        Serial.println(F("FS: Failed to open /bootlog.txt for append"));
        return;
    }

    // DynamicJsonDocument doc(1024);
    StaticJsonDocument<256> doc;

// Set the values in the document
#ifdef ESP8266
    doc[F("BOOTINFO")] = ESP.getResetReason() + " " + reboot_time_str;
#endif

    // Serialize JSON to file
    if (serializeJson(doc, file) == 0)
    {
        // Serial.println(F("Failed to write bootlog.txt"));
    }
    file.println();
    file.close();
}

void BWC::_saveStates()
{
// //kill the dog
// // ESP.wdtDisable();
#ifdef ESP8266
    ESP.wdtFeed();
#endif
    _save_states_needed = false;
    File file = LittleFS.open("/states.txt", "w");
    if (!file)
    {
        Serial.println(F("FS: Failed to open /states.txt for write"));
        return;
    }

    // DynamicJsonDocument doc(1024);
    StaticJsonDocument<256> doc;

    // Set the values in the document
    doc[F("PWR")] = cio->cio_states.power;
    doc[F("UNT")] = cio->cio_states.unit;
    doc[F("HTR")] = cio->cio_states.heat;
    doc[F("FLT")] = cio->cio_states.pump;
    doc[F("TGT")] = cio->cio_states.target;
    doc[F("GOD")] = (uint8_t)cio->cio_states.godmode; // makes the file look better

    // Serialize JSON to file
    if (serializeJson(doc, file) == 0)
    {
        // Serial.println(F("Failed to write states.txt"));
    }
    file.close();
    // //revive the dog
    // // ESP.wdtEnable(0);
}

void BWC::_saveCommandQueue()
{
    _save_cmdq_needed = false;
#ifdef ESP8266
    ESP.wdtFeed();
#endif
    /*
     * Check BEFORE opening the file with "w" (which truncates).
     * Previously this check was after open(), leaving an empty cmdq.json
     * on disk - which prevented _loadSettings() from running on next boot,
     * causing a full settings reset to defaults.
     */
    if (_command_que.size())
        if (_command_que[0].cmd == REBOOTESP && _command_que[0].interval == 0)
            return;

    File file = LittleFS.open("/cmdq.json", "w");
    if (!file)
    {
        Serial.println(F("FS: Failed to open /cmdq.json for write"));
        return;
    }
    // written straight to the file, no 4 KB document (same JSON as /getcommands/)
    writeJSONCommandQueue(file);
    file.close();
    // revive the dog
    //  ESP.wdtEnable(0);
}

void BWC::saveSettings()
{
// kill the dog
//  ESP.wdtDisable();
#ifdef ESP8266
    ESP.wdtFeed();
#endif
    _save_settings_needed = false;
    // Allocate before truncating the file: out of memory, "w" + an empty
    // document wrote "null" and the next boot reset all settings.
    // The periodic save ticker tries again.
    DynamicJsonDocument doc(2048);
    if (doc.capacity() == 0)
    {
        Serial.printf_P(PSTR("FS: out of memory, keeping /settings.json (heap=%u)\n"), (unsigned)ESP.getFreeHeap());
        return;
    }
    File file = LittleFS.open("/settings.json", "w");
    if (!file)
    {
        Serial.println(F("FS: Failed to open /settings.json for write"));
        return;
    }

    _heatingtime += _heatingtime_ms / 1000;
    _pumptime += _pumptime_ms / 1000;
    _airtime += _airtime_ms / 1000;
    _jettime += _jettime_ms / 1000;
    _uptime += _uptime_ms / 1000;
    _heatingtime_ms %= 1000;
    _pumptime_ms %= 1000;
    _airtime_ms %= 1000;
    _jettime_ms %= 1000;
    _uptime_ms %= 1000;
    doc[F("CLTIME")] = _cl_timestamp_s;
    doc[F("FTIME")] = _filter_timestamp_s;
    doc[F("FCTIME")] = _fc_timestamp_s;
    doc[F("WCTIME")] = _wc_timestamp_s;
    doc[F("PHTIME")] = _ph_timestamp_s;
    doc[F("CLVTIME")] = _clv_timestamp_s;
    doc[F("UPTIME")] = _uptime;
    doc[F("PUMPTIME")] = _pumptime;
    doc[F("HEATINGTIME")] = _heatingtime;
    doc[F("AIRTIME")] = _airtime;
    doc[F("JETTIME")] = _jettime;
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
    doc[F("PHVAL")] = _last_ph_value;
    doc[F("CLVAL")] = _last_cl_value;
    doc[F("CYAVAL")] = _last_cya_value;
    doc[F("ALKVAL")] = _last_alk_value;
    doc[F("CYATIME")] = _cya_timestamp_s;
    doc[F("ALKTIME")] = _alk_timestamp_s;
    doc[F("DPH")] = _dose_ph;
    doc[F("DPHTIME")] = _dose_ph_ts;
    doc[F("DCL")] = _dose_cl;
    doc[F("DCLTAB")] = _dose_cl_tab;
    doc[F("DCLTIME")] = _dose_cl_ts;
    doc[F("DTA")] = _dose_ta;
    doc[F("DTATIME")] = _dose_ta_ts;
    doc[F("AUDIO")] = _audio_enabled;
    doc[F("SNDMASK")] = _sound_mask;
    doc[F("JETCD")] = _jet_countdown;
    doc[F("STBYORIG")] = _standby_original;
    doc[F("KWH")] = _energy_total_kWh;
    doc[F("KWHD")] = _energy_daily_Ws;
    doc[F("KWHD_PENDING")] = _energy_pending_daily_Ws;
    doc[F("KWHD_DAY")] = _energy_daily_yday;
    doc[F("COST")] = _energy_cost_total;
    doc[F("COSTD")] = _energy_daily_cost;
    doc[F("COSTD_PENDING")] = _energy_pending_daily_cost;
    doc[F("TIMEZONE")] = _timezone;
    doc[F("TIMEZONE_NAME")] = _timezone_name;
    doc[F("AMB")] = _ambient_temp;
    doc[F("BRT")] = _dsp_brightness;
    doc[F("WEATHER")] = _weather;
    doc[F("WEATHERAUTO")] = _weather_auto_off;
    doc[F("POOLCAP")] = _pool_capacity;
    for (uint8_t i = 0; i < DOSE_FACTORS; i++)
        doc[DF_KEY(i)] = _dose_factor[i];
    doc[F("HLOSS")] = _heat_loss;
    doc[F("HLCALAT")] = _hlcal_planned_at;
    doc[F("AIRTO")] = _airjet_timeout_minutes;
    doc[F("HJTO")] = _hydrojet_timeout_minutes;
    doc[F("GSTMODE")] = _host_mode;
    doc[F("GSTMAXTGT")] = _host_max_target_c;
    doc[F("GSTHTRTO")] = _host_heater_timeout_min;
    doc[F("BRTSTEP")] = _brightness_boost_steps;
    doc[F("BRTDUR")] = _brightness_boost_duration_s;
    if (dsp != nullptr)
    {
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
    }
    else
    {
        Serial.println(F("WARNING: saveSettings() - dsp deleted, button states not saved"));
    }

    if (serializeJson(doc, file) == 0)
    {
        Serial.println(F("ERROR: Failed to serialize settings.json"));
    }
    file.close();
    // revive the dog
    //  ESP.wdtEnable(0);
}

// save out debug text to file "debug.txt" on littleFS
void BWC::saveDebugInfo(const String &s)
{
    File file = LittleFS.open("/debug.txt", "a");
    if (!file)
    {
        Serial.println(F("FS: Failed to open /debug.txt for append"));
        return;
    }

    DynamicJsonDocument doc(1024);

    // Set the values in the document
    doc[F("timestamp")] = time(nullptr);
    doc[F("message")] = s;
    // Serialize JSON to file
    if (serializeJson(doc, file) == 0)
    {
        // Serial.println(F("Failed to write debug.txt"));
    }
    file.close();
}
