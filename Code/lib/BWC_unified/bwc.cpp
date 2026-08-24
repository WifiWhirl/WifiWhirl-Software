#include "bwc.h"
#include "util.h"
#include "pitches.h"
#include <algorithm>
#include <cstdlib>
#ifndef ESP8266
#include "esp_attr.h" // RTC_NOINIT_ATTR
#endif

extern const char *defaultTimezone;
extern const char *defaultTimezoneName;

namespace
{
constexpr uint64_t VALID_TIME_THRESHOLD_S = 57600;
constexpr float HEATER_EFFICIENCY = 0.99f;
// Minimum time between two tick flips to count as a real cool-down interval
// (guards against sloshing/noise flipping the reading right after the anchor).
// One tick in 15 min is plausible for a lossy tub in winter.
constexpr uint32_t HLCAL_MIN_ELAPSED_S = 900;
// Must stay consistent with HLCAL_MIN_ELAPSED_S: one F-tick per 15 min on a
// typical 800 l tub is ~2100 W net, so the cap has to sit above that or a
// genuinely lossy tub could never finish (every tick would re-anchor).
constexpr float HLCAL_MAX_NET_LOSS_W = 2500.0f;
constexpr size_t COMMAND_QUEUE_JSON_CAPACITY = 4096;

enum HlCalError : uint8_t
{
    HLCAL_ERR_NONE = 0,
    HLCAL_ERR_PREP = 1,
    HLCAL_ERR_INTERRUPTED = 2,
    HLCAL_ERR_TIMEOUT = 3,
    HLCAL_ERR_UNSTABLE = 4
};

float displayTempC(uint8_t unit, float temp)
{
    return unit == 0 ? F2C(temp) : temp;
}

// Current water temperature in Celsius (cio_states.temperature is in the display
// unit; unit==0 means Fahrenheit). Float math keeps sub-degree resolution.
float waterTempC(CIO *cio)
{
    return displayTempC(cio->cio_states.unit, (float)cio->cio_states.temperature);
}

float heatingHours(float current_temp_c, float target_temp_c, int heaterPwr,
                   float heatLoss, int ambTemp, int poolCapacity)
{
    if (current_temp_c >= target_temp_c)
        return 0.0f;

    float avgTemp = (target_temp_c + current_temp_c) / 2.0f;
    float tempDiff = std::max(avgTemp - (float)ambTemp, 0.0f);
    float netHeatingPower = ((float)heaterPwr * HEATER_EFFICIENCY) - (heatLoss * tempDiff);
    if (netHeatingPower <= 0.0f)
        return -1.0f;

    const float tcpH2O = 1.163f; // Wh/(kg*K)
    float totalEnergy = (float)poolCapacity * tcpH2O * (target_temp_c - current_temp_c);
    return totalEnergy / netHeatingPower;
}

const __FlashStringHelper *hlCalErrorName(uint8_t error)
{
    switch (error)
    {
    case HLCAL_ERR_PREP:
        return F("prep");
    case HLCAL_ERR_INTERRUPTED:
        return F("interrupted");
    case HLCAL_ERR_TIMEOUT:
        return F("timeout");
    case HLCAL_ERR_UNSTABLE:
        return F("unstable");
    default:
        return F("");
    }
}

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

// Energy and runtime counters accumulate continuously but only hit flash every
// 600s (and on graceful restart). A soft-WDT/exception reboot loses everything
// since the last save, so the published total/daily jump backwards then climb
// again (HA sawtooth) and the day ends up under-counted; the runtime hour-meters
// lose the same way. RTC memory survives a reset (not a power loss) and costs no
// flash wear, so mirror the live values there every loop and restore on boot.
// crashtrace.cpp owns its own RTC slot; this is separate. NOTE: not power-loss
// persistent by design - a cold boot falls back to flash, which is correct for
// an intentional power-down.
constexpr uint32_t COUNTERS_RTC_MAGIC = 0xE2701EC1;
struct CountersRtc
{
    uint32_t magic;
    double total_kWh;
    double cost_total;
    double daily_Ws;
    double pending_Ws;
    double daily_cost;
    double pending_cost;
    int32_t yday;
    uint32_t uptime_s;
    uint32_t pumptime_s;
    uint32_t heatingtime_s;
    uint32_t airtime_s;
    uint32_t jettime_s;
    uint32_t crc;
};
uint32_t countersRtcCrc(const CountersRtc &e)
{
    const uint32_t *p = reinterpret_cast<const uint32_t *>(&e);
    uint32_t c = 0;
    for (size_t i = 0; i < (sizeof(CountersRtc) - sizeof(uint32_t)) / 4; ++i)
        c = c * 31u + p[i];
    return c;
}

#ifdef ESP8266
constexpr uint32_t COUNTERS_RTC_OFFSET = 96; // 4-byte block offset into RTC user mem (512B total), past crashtrace's 64..95
void countersRtcStore(const CountersRtc &e) { ESP.rtcUserMemoryWrite(COUNTERS_RTC_OFFSET, (uint32_t *)&e, sizeof(e)); }
bool countersRtcFetch(CountersRtc &e) { return ESP.rtcUserMemoryRead(COUNTERS_RTC_OFFSET, (uint32_t *)&e, sizeof(e)); }
#else
RTC_NOINIT_ATTR static CountersRtc s_countersRtc; // survives reset, wiped on power loss
void countersRtcStore(const CountersRtc &e) { s_countersRtc = e; }
bool countersRtcFetch(CountersRtc &e) { e = s_countersRtc; return true; }
#endif
}

BWC::BWC()
{
    _dsp_brightness = 7;
    _cl_timestamp_s = time(nullptr);
    _filter_timestamp_s = time(nullptr);
    _fc_timestamp_s = time(nullptr);
    _wc_timestamp_s = time(nullptr);
    _ph_timestamp_s = time(nullptr);
    _clv_timestamp_s = time(nullptr);
    _uptime = 0;
    _pumptime = 0;
    _heatingtime = 0;
    _airtime = 0;
    _jettime = 0;
    _uptime_ms = 0;
    _pumptime_ms = 0;
    _heatingtime_ms = 0;
    _airtime_ms = 0;
    _jettime_ms = 0;
    _energy_total_kWh = 0.0;
    _energy_daily_Ws = 0.0;
    _energy_pending_daily_Ws = 0.0;
    _energy_daily_yday = -1;
    _energy_power_W = 0;
    _price = 0.35;
    _cl_interval = 7;
    _filter_interval = 30;
    _fc_interval = 60;
    _wc_interval = 90;
    _ph_interval = 3;              // Check pH every 3 days by default
    _audio_enabled = true;
    _ambient_temp = 20;
    _last_ph_value = 72;           // Default pH 7.2 (stored as 72)
    _last_cl_value = 10;           // Default 1.0 mg/L (stored as 10)
    _last_cya_value = 300;         // Default 30.0 mg/L (stored as 300)
    _last_alk_value = 100;         // Default 100 mg/L
    _cya_timestamp_s = 0;
    _alk_timestamp_s = 0;
    _timezone = defaultTimezone;
    _timezone_name = defaultTimezoneName;
}

BWC::~BWC()
{
    stop();
};

void save_settings_cb(BWC *bwcInstance)
{
    bwcInstance->on_save_settings();
}
void scroll_text_cb(BWC *bwcInstance)
{
    bwcInstance->on_scroll_text();
}

static bool isJsonString(const JsonVariantConst &value)
{
    return value.is<const char *>() || value.is<String>();
}

// Energy prices are edited in cent steps everywhere in the UI, so keep them on
// that grid instead of carrying float noise (0.349999994) around.
static double roundCents(double value)
{
    return round(value * 100.0) / 100.0;
}

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

static void setDefaultHardwarePins(int pins[])
{
#ifdef ESP8266
    pins[0] = D1;
    pins[1] = D2;
    pins[2] = D5;
    pins[3] = D6;
    pins[4] = D4;
    pins[5] = D3;
    pins[6] = D7;
    pins[7] = D0;
#else
    for (int i = 0; i < 8; i++)
        pins[i] = i + 1; // GPIO1..GPIO8
#endif
}

void BWC::on_save_settings()
{
    if (++_ticker_count >= 3)
    {
        _save_settings_needed = true;
        _ticker_count = 0;
    }
}

void BWC::on_scroll_text()
{
    _scroll = true;
}

void BWC::setup(void)
{
    Models ciomodel;
    Models dspmodel;
    setDefaultHardwarePins(pins);

    if (!_loadHardware(ciomodel, dspmodel))
    {
        ciomodel = MIAMI2021;
        dspmodel = MIAMI2021;
    }
    // Serial.printf("Cio loaded: %d, dsp model: %d\n", ciomodel, dspmodel);
    for (int i = 0; i < 8; i++)
    {
        // Serial.printf("pin%d: %d\n", i, pins[i]);
    }
    switch (ciomodel)
    {
    case PRE2021:
        cio = new CIO_PRE2021;
        break;
    case MIAMI2021:
        cio = new CIO_2021;
        break;
    case MALDIVES2021:
        cio = new CIO_2021_HJT;
        break;
    case MSPA:
        cio = new CIO_MSPA;
        break;
    default:
        cio = new CIO_2021;
        break;
    }
    // MSPA has no separate display protocol; always pair it with the MSPA display
    // layer regardless of what the frontend sent for dsp.
    if (ciomodel == MSPA)
        dspmodel = MSPA;
    switch (dspmodel)
    {
    case PRE2021:
        dsp = new DSP_PRE2021;
        break;
    case MIAMI2021:
        dsp = new DSP_2021;
        break;
    case MALDIVES2021:
        dsp = new DSP_2021_HJT;
        break;
    case MSPA:
        dsp = new DSP_MSPA;
        break;
    default:
        dsp = new DSP_2021;
        break;
    }
    cio->setup(pins[0], pins[1], pins[2]);
    dsp->setup(pins[3], pins[4], pins[5], pins[6]);
    hasjets = cio->getHasjets();
    hasgod = cio->getHasgod();
    cio->cio_toggles.power_change = 1;
    begin();
}

void BWC::begin()
{
    // _save_melody("melody.bin");
    // if(_audio_enabled) dsp->playIntro();
    // dsp->LEDshow();
    _save_settings_ticker.attach(600.0f, save_settings_cb, this);
    _scroll_text_ticker.attach(0.25f, scroll_text_cb, this);
    // Seed change timestamps to current millis() so timeouts don't fire at boot
    _pump_change_timestamp_ms = millis();
    _bubbles_change_timestamp_ms = millis();
    _jets_change_timestamp_ms = millis();
}

void BWC::loop()
{
    ++loop_count;
#ifdef ESP8266
    ESP.wdtFeed();
#endif
    _timestamp_secs = time(nullptr);
    _updateTimes();
    if (_scroll && (dsp->text.length() > 0))
    {
        dsp->text.remove(0, 1);
        _scroll = false;
    }
    cio->updateStates();
    dsp->dsp_states = cio->cio_states;

    /*Modify and use dsp->dsp_states here if we want to show text or something*/
    // Apply static text override if active
    if (_static_text_active)
    {
        dsp->dsp_states.char1 = _static_char1;
        dsp->dsp_states.char2 = _static_char2;
        dsp->dsp_states.char3 = _static_char3;
        dsp->dsp_states.power = 1; // Force display on for static text
    }
    
    dsp->setRawPayload(cio->getRawPayload());
    /*Increase screen brightness when pressing buttons*/
    adjust_brightness();
    dsp->handleStates();

    dsp->updateToggles();

    /*Host mode (Gastgebermodus): block TIMER/POWER/UNIT on the physical panel.
      Web/MQTT commands enter via _handleCommandQ() and stay unrestricted.*/
    if (_host_mode)
    {
        dsp->dsp_toggles.timer_pressed = 0;
        dsp->dsp_toggles.power_change = 0;
        dsp->dsp_toggles.unit_change = 0;
    }

    cio->cio_toggles = dsp->dsp_toggles;

    play_sound();

    if (_dsp_tgt_used)
        cio->cio_toggles.target = cio->cio_states.target;
    else
        cio->cio_toggles.target = _web_target;

    if (dsp->dsp_toggles.unit_change)
    {
        cio->cio_states.unit ? cio->cio_toggles.target = C2F(cio->cio_toggles.target) : cio->cio_toggles.target = F2C(cio->cio_toggles.target);
    }

    /*Host mode: cap target set from the physical panel. Downward correction is
      handled by CIO handleToggles pressing DOWN until target matches.*/
    if (_host_mode && _dsp_tgt_used)
    {
        uint8_t max_t = cio->cio_states.unit ? _host_max_target_c : (uint8_t)round(C2F(_host_max_target_c));
        if (cio->cio_toggles.target > max_t)
            cio->cio_toggles.target = max_t;
        if (cio->cio_states.target >= max_t)
            cio->cio_toggles.up_pressed = 0;
    }

    /*following method will change target temp and set _dsp_tgt_used to false if target temp is changed*/
    _handleCommandQ();
    /*Handle smart schedule before processing toggles so pump/heater changes are applied*/
    _handleSmartSchedule();
    /*Sample the water cool-down if a heat-loss calibration is running*/
    _handleHeatLossCalibration();
    /*Check if jets have exceeded their configured timeout*/
    _checkJetTimeouts();
    /*Host mode: turn heater off some time after target temperature was reached*/
    _checkHostHeaterTimeout();
    /*If new target was not set above, use whatever the cio says*/
    cio->setRawPayload(dsp->getRawPayload());
    cio->handleToggles();

    if (_save_settings_needed)
        saveSettings();
    if (_save_cmdq_needed)
        _saveCommandQueue();
    if (_save_states_needed)
        _saveStates();
    if (_save_smartschedule_needed)
        _saveSmartSchedule();
    _handleStateChanges();
    // logstates();
    if (BWC_DEBUG)
        _log();
}

void BWC::_log()
{
    static uint32_t writes = 0;
    static std::vector<uint8_t> prev_fromcio;
    static std::vector<uint8_t> prev_fromdsp;
    std::vector<uint8_t> fromcio = cio->getRawPayload();
    std::vector<uint8_t> fromdsp = dsp->getRawPayload();
    if ((fromcio == prev_fromcio) && (fromdsp == prev_fromdsp))
        return;
    prev_fromcio = fromcio;
    prev_fromdsp = fromdsp;

    File file = LittleFS.open("/log.txt", "a");
    if (!file)
    {
        Serial.println(F("FS: Failed to open /log.txt for append"));
        return;
    }
    if (writes >= 1000)
    {
        file.close();
        return;
    }
    writes++;
    file.print(_timestamp_secs);
    file.print(':');
    for (unsigned int i = 0; i < fromcio.size(); i++)
    {
        if (i > 0)
            file.print(",");
        file.print(fromcio[i], HEX);
    }
    file.print('\t');
    for (unsigned int i = 0; i < fromdsp.size(); i++)
    {
        file.print(",");
        file.print(fromdsp[i], HEX);
    }
    file.print('\n');
    file.close();
}

void BWC::adjust_brightness()
{
    if (dsp->dsp_toggles.pressed_button != NOBTN)
        _override_dsp_brt_timer = (int32_t)_brightness_boost_duration_s * 1000;
    if (_override_dsp_brt_timer > 0)
    {
        dsp->dsp_states.brightness = _dsp_brightness + _brightness_boost_steps;
        if (dsp->dsp_states.brightness > 8)
            dsp->dsp_states.brightness = 8;
    }
    else
    {
        dsp->dsp_states.brightness = _dsp_brightness;
    }
}

void BWC::play_sound()
{
    if (!dsp->dsp_states.locked && dsp->dsp_states.power)
    {
        switch (dsp->dsp_toggles.pressed_button)
        {
        case UP:
            if (dsp->EnabledButtons[UP])
                _sweepup();
            _dsp_tgt_used = true;
            break;
        case DOWN:
            if (dsp->EnabledButtons[DOWN])
                _sweepdown();
            _dsp_tgt_used = true;
            break;
        case TIMER:
            if (dsp->EnabledButtons[TIMER])
                _beep();
            break;
        default:

            break;
        }
    }

    if (
        dsp->dsp_toggles.bubbles_change || dsp->dsp_toggles.heat_change ||
        dsp->dsp_toggles.jets_change || dsp->dsp_toggles.power_change ||
        dsp->dsp_toggles.pump_change || dsp->dsp_toggles.unit_change)
        _accord();
    /* Lock button sound is taken care of in _handleStateChanges() */
}

void BWC::stop()
{
    _save_settings_ticker.detach();
    _scroll_text_ticker.detach();
    if (cio != nullptr)
    {
        Serial.println("stopping cio");
        cio->stop();
        Serial.println("del cio");
        delete cio;
        cio = nullptr;
    }
    if (dsp != nullptr)
    {
        Serial.println("stopping dsp");
        dsp->stop();
        Serial.println("del dsp");
        delete dsp;
        dsp = nullptr;
    }
}

void BWC::pause_all(bool action)
{
    if (action)
    {
        _save_settings_ticker.detach();
        _scroll_text_ticker.detach();
    }
    else
    {
        _save_settings_ticker.attach(3600.0f, save_settings_cb, this);
        _scroll_text_ticker.attach(0.25f, scroll_text_cb, this);
    }
    cio->pause_all(action);
    dsp->pause_all(action);
}

/*Sort by xtime, ascending*/
bool BWC::_compare_command(const command_que_item &i1, const command_que_item &i2)
{
    return i1.xtime < i2.xtime;
}

void BWC::_handleCommandQ()
{
    // Calibration drives pump and heater itself, so it still freezes everything.
    if (_command_queue_paused_for_calibration)
        return;
    if (_command_que.size() < 1)
        return;

    // A disabled queue parks the user's planned automations only. Stopping the
    // whole queue also killed live control (the panel looked locked) and the
    // airjet/hydrojet/host-mode safety timeouts, which run through here too.
    const bool run_scheduled = _command_queue_enabled;

    int commandIndex = -1;
    for (size_t i = 0; i < _command_que.size(); i++)
    {
        if (!_command_que[i].enabled)
            continue;
        if (!run_scheduled && _command_que[i].scheduled)
            continue;
        if (_timestamp_secs < _command_que[i].xtime)
            return;
        commandIndex = (int)i;
        break;
    }
    if (commandIndex < 0)
        return;
    if (commandIndex > 0)
        std::swap(_command_que[0], _command_que[commandIndex]);

    /* time for next command? */
    if (_timestamp_secs < _command_que[0].xtime)
        return;
    // If interval > 0 then append to commandQ with updated xtime.
    if (_command_que[0].interval > 0)
    {
        // Advance to the next future fire time in one step. A stale xtime with a
        // small interval would otherwise spin this loop for billions of iterations
        // after the NTP clock jump -> watchdog reboot. (Same fix as loadCommandQueue.)
        uint64_t now = (uint64_t)time(nullptr);
        if (_command_que[0].xtime < now)
        {
            uint64_t k = (now - _command_que[0].xtime + _command_que[0].interval - 1) / _command_que[0].interval;
            _command_que[0].xtime += k * _command_que[0].interval;
        }
        _command_que.push_back(_command_que[0]);
    }

    // Do not turn off pump if Heater is running (unless forced)
    // Fixes situations when you want to heat your pool but your daily pump cycle is running and turning off your heater
    if (_command_que[0].cmd == SETPUMP && 
        _command_que[0].val == 0 &&           // Only block turn-off commands
        cio->cio_states.heat && 
        !_command_que[0].force)               // Allow if force=true
    {
        _command_que.erase(_command_que.begin());
        _save_cmdq_needed = true;
        return;
    }

    // Force pump off while heater is running: turn off heater first
    // The hot tub hardware prevents pump-off while the heater is active.
    // Insert a heater-off command before the pump-off so it is processed first.
    if (_command_que[0].cmd == SETPUMP &&
        _command_que[0].val == 0 &&
        cio->cio_states.heat &&
        _command_que[0].force)
    {
        command_que_item heater_off;
        heater_off.cmd = SETHEATER;
        heater_off.val = 0;
        heater_off.xtime = 0;
        heater_off.interval = 0;
        heater_off.force = false;
        heater_off.enabled = true;
        _command_que.insert(_command_que.begin(), heater_off);
        // heater-off is now at [0], pump-off shifted to [1]; heater-off is processed this cycle
    }

    _handlecommand(_command_que[0].cmd, _command_que[0].val, _command_que[0].text);
}

bool BWC::_handlecommand(Commands cmd, int64_t val, const String &txt = "")
{
    bool restartESP = false;

    dsp->text += String(" ") + txt;
    switch (cmd)
    {
    case TOGGLEPWR:
    {
        cio->cio_toggles.power_change = 1;
        break;
    }
    case TOGGLELCK:
    {
        cio->cio_toggles.locked_pressed = 1;
        break;
    }
    case SETTARGET:
    {
        if (!((val > 0 && val < 41) || (val > 50 && val < 105)))
            break;
        bool implied_unit_is_celsius = (val < 41);
        bool required_unit = cio->cio_states.unit;
        if (implied_unit_is_celsius && !required_unit)
            cio->cio_toggles.target = round(C2F(val));
        else if (!implied_unit_is_celsius && required_unit)
            cio->cio_toggles.target = round(F2C(val));
        else
            cio->cio_toggles.target = val;
        /*Send this value to cio instead of results from button presses on the display*/
        _dsp_tgt_used = false;
        _web_target = cio->cio_toggles.target;
        
        // Update smart schedule target temp if active (store in Celsius)
        if (_smart_schedule.active)
        {
            uint8_t temp_celsius = implied_unit_is_celsius ? val : round(F2C(val));
            if (temp_celsius != _smart_schedule.target_temp)
            {
                _smart_schedule.target_temp = temp_celsius;
                _save_smartschedule_needed = true;
                _new_data_available = true;
                Serial.print(F("SmartSchedule: Target temp updated to "));
                Serial.println(temp_celsius);
            }
        }
        break;
    }
    case SETUNIT:
        if (hasgod && !cio->cio_toggles.godmode)
            break;
        if (val == 1 && cio->cio_states.unit == 0)
            cio->cio_toggles.target = round(F2C(cio->cio_toggles.target));
        if (val == 0 && cio->cio_states.unit == 1)
            cio->cio_toggles.target = round(C2F(cio->cio_toggles.target));
        if ((uint8_t)val != cio->cio_states.unit)
            cio->cio_toggles.unit_change = 1;
        _dsp_tgt_used = false;
        _web_target = cio->cio_toggles.target;
        break;
    case SETBUBBLES:
        if (val != cio->cio_states.bubbles)
            cio->cio_toggles.bubbles_change = 1;
        break;
    case SETHEATER:
        if (val != cio->cio_states.heat)
        {
            cio->cio_toggles.heat_change = 1;
            // If user manually turns off heater while smart schedule is active and heating,
            // cancel the schedule to prevent it from overriding the user's decision
            if (val == 0 && _smart_schedule.active && _smart_schedule.calculated_start_time > 0 &&
                _timestamp_secs >= _smart_schedule.calculated_start_time)
            {
                Serial.println(F("SmartSchedule: Cancelled due to manual heater off"));
                _smart_schedule.active = false;
                _smart_schedule.temp_reading_state = 0;
                _smart_schedule.check_completed = false;
                _save_smartschedule_needed = true;
                _new_data_available = true;
            }
        }
        break;
    case SETPUMP:
        if (val != cio->cio_states.pump)
            cio->cio_toggles.pump_change = 1;
        break;
    case RESETQ:
        _command_que.clear();
        _save_cmdq_needed = true;
        return false;
        break;
    case REBOOTESP:
        restartESP = true;
        break;
    case GETTARGET:
        /*Not used atm*/
        break;
    case RESETTIMES:
        _uptime = 0;
        _pumptime = 0;
        _jettime = 0;
        _heatingtime = 0;
        _airtime = 0;
        _uptime_ms = 0;
        _pumptime_ms = 0;
        _jettime_ms = 0;
        _heatingtime_ms = 0;
        _airtime_ms = 0;
        _energy_total_kWh = 0;
        _energy_cost_total = 0;
        _energy_daily_Ws = 0;
        _energy_daily_cost = 0;
        _energy_pending_daily_Ws = 0;
        _energy_pending_daily_cost = 0;
        _energy_daily_yday = -1;
        _save_settings_needed = true;
        _new_data_available = true;
        break;
    case RESETCLTIMER:
        _cl_timestamp_s = _timestamp_secs;
        _save_settings_needed = true;
        _new_data_available = true;
        break;
    case RESETFTIMER:
        _filter_timestamp_s = _timestamp_secs;
        _save_settings_needed = true;
        _new_data_available = true;
        break;
    case RESETFCTIMER:
        _fc_timestamp_s = _timestamp_secs;
        _save_settings_needed = true;
        _new_data_available = true;
        break;
    case RESETWCTIMER:
        _wc_timestamp_s = _timestamp_secs;
        _save_settings_needed = true;
        _new_data_available = true;
        break;
    case SETPHVALUE:
        // pH value * 10 (e.g. 72 = 7.2 pH), valid range 0-140 (0.0-14.0)
        if (val >= 0 && val <= 140)
        {
            _last_ph_value = (uint16_t)val;
            _ph_timestamp_s = _timestamp_secs; // Also update timestamp when value is set
            _save_settings_needed = true;
            _new_data_available = true;
        }
        break;
    case SETCLVALUE:
        // Chlorine value * 10 (e.g. 15 = 1.5 mg/L), valid range 0-100 (0.0-10.0 mg/L)
        if (val >= 0 && val <= 100)
        {
            _last_cl_value = (uint16_t)val;
            _clv_timestamp_s = _timestamp_secs; // Update chlorine value check timestamp
            _save_settings_needed = true;
            _new_data_available = true;
        }
        break;
    case SETCYAVALUE:
        // Cyanuric acid value * 10 (e.g. 300 = 30.0 mg/L), valid range 0-1000 (0.0-100.0 mg/L)
        if (val >= 0 && val <= 1000)
        {
            _last_cya_value = (uint16_t)val;
            _cya_timestamp_s = _timestamp_secs;
            _save_settings_needed = true;
            _new_data_available = true;
        }
        break;
    case SETALKVALUE:
        // Alkalinity value in mg/L, valid range 0-300
        if (val >= 0 && val <= 300)
        {
            _last_alk_value = (uint16_t)val;
            _alk_timestamp_s = _timestamp_secs;
            _save_settings_needed = true;
            _new_data_available = true;
        }
        break;
    case SETJETS:
        if (val != cio->cio_states.jets)
            cio->cio_toggles.jets_change = 1;
        break;
    case SETBRIGHTNESS:
        _dsp_brightness = val;
        _save_settings_needed = true;
        _new_data_available = true;
        break;
    case SETBEEP:
        if (val == 0)
            _beep();
        else if (val == 1)
            _accord();
        else
            _load_melody_json(txt);
        break;
    case SETAMBIENTF:
        setAmbientTemperature(val, false);
        _new_data_available = true;
        break;
    case SETAMBIENTC:
        setAmbientTemperature(val, true);
        _new_data_available = true;
        break;
    case RESETDAILY:
    {
        _energy_daily_Ws = 0;
        _energy_daily_cost = 0;
        _energy_pending_daily_Ws = 0;
        _energy_pending_daily_cost = 0;
        int today;
        _energy_daily_yday = _getEnergyDayIndex(today) ? today : -1;
        _save_settings_needed = true;
        _new_data_available = true;
        break;
    }
    case SETGODMODE:
        cio->cio_toggles.godmode = val > 0;
        break;
    case SETFULLPOWER:
        val = std::clamp((int)val, 0, 1);
        cio->cio_toggles.no_of_heater_elements_on = val + 1;
        break;
    /*PRINTTEXT is not a command per se. Every command prints the txt string, and if we ONLY want to print txt we do nothing to the command.*/
    case SETREADY:
    {
        command_que_item item;
        if ((int64_t)_timestamp_secs > (int64_t)(val - _estHeatingTime() * 3600.0f - 7200)) // 2 hours extra margin
        {
            /*time to start heating*/
            item.cmd = SETHEATER;
            item.interval = 0;
            item.text = "";
            item.val = 1;
            item.xtime = _timestamp_secs + 1;
            add_command(item);
        }
        else
        {
            /*Not time yet, so add check in one minute*/
            item.cmd = cmd;
            item.interval = 0;
            item.text = "";
            item.val = val;
            item.xtime = _timestamp_secs + 60;
            /*We can't use addcommand() because it will copy xtime to val again*/
            _command_que.push_back(item);
            std::sort(_command_que.begin(), _command_que.end(), _compare_command);
        }
    }
    break;
    case SETENABLEBUTTONS:
    {
        // Enable or disable all physical buttons at once
        // val > 0 = enable all, val == 0 = disable all
        uint8_t enable_state = (val > 0) ? 1 : 0;
        // Iterate through all button indices from LOCK to HYDROJETS
        for (int btn = LOCK; btn <= HYDROJETS; btn++)
        {
            dsp->EnabledButtons[btn] = enable_state;
        }
        _save_settings_needed = true;
        _new_data_available = true;
        break;
    }
    default:
        break;
    }
    // remove from commandQ
    _command_que.erase(_command_que.begin());
    _save_cmdq_needed = true;
    if (restartESP)
    {
        saveSettings();
        _saveCommandQueue();
        stop();
        delay(3000);
        ESP.restart();
    }
    /*If we pushed back an item, we need to re-sort the que*/
    std::sort(_command_que.begin(), _command_que.end(), _compare_command);
    return false;
}

void BWC::_handleStateChanges()
{
    if (_prev_cio_states != cio->cio_states || _prev_dsp_states.brightness != dsp->dsp_states.brightness)
        _new_data_available = true;
    if (cio->cio_states.temperature != _prev_cio_states.temperature)
    {
        _deltatemp = cio->cio_states.temperature - _prev_cio_states.temperature;
        _temp_change_timestamp_ms = millis();
    }

    // Store virtual temp data point
    if (cio->cio_states.heatred != _prev_cio_states.heatred)
    {
        _heatred_change_timestamp_ms = millis();
    }

    if (cio->cio_states.pump != _prev_cio_states.pump)
    {
        _pump_change_timestamp_ms = millis();
    }

    if (cio->cio_states.bubbles != _prev_cio_states.bubbles)
    {
        _bubbles_change_timestamp_ms = millis();
    }

    if (cio->cio_states.jets != _prev_cio_states.jets)
    {
        _jets_change_timestamp_ms = millis();
    }

    if ((cio->cio_states.locked != _prev_cio_states.locked) && dsp->EnabledButtons[LOCK] && _audio_enabled && (dsp->dsp_toggles.pressed_button == LOCK))
    {
        _beep();
    }

    if (
        cio->cio_states.unit != _prev_cio_states.unit ||
        cio->cio_states.pump != _prev_cio_states.pump ||
        cio->cio_states.heat != _prev_cio_states.heat ||
        cio->cio_states.target != _prev_cio_states.target)
        _save_states_needed = true;

    Buttons _currbutton = dsp->dsp_toggles.pressed_button;
    if (_currbutton != _prevbutton && _currbutton != NOBTN)
    {
        _btn_sequence[0] = _btn_sequence[1];
        _btn_sequence[1] = _btn_sequence[2];
        _btn_sequence[2] = _btn_sequence[3];
        _btn_sequence[3] = _currbutton;
    }

    _prev_cio_states = cio->cio_states;
    _prev_dsp_states = dsp->dsp_states;
    _prevbutton = _currbutton;
    /* check changes from DSP 4W - go to antigodmode if someone presses a button*/
}

// return how many hours until pool is ready. (provided the heater is on)
float BWC::_estHeatingTime()
{
    float tgtTemp = cio->cio_states.target;      // Target temperature (set by user)
    if (!cio->cio_states.unit)
        tgtTemp = F2C(tgtTemp);                  // Check if unit is set to fahrenheit and convert to C for calculation
    float curTemp = waterTempC(cio);
    if (curTemp >= tgtTemp)
        return -2;                               // Pool is ready, target is reached

    float hoursRemaining = heatingHours(curTemp, tgtTemp, cio->getPower().HEATERPOWER,
                                        _heat_loss, _ambient_temp, _pool_capacity);
    return hoursRemaining >= 0.0f ? hoursRemaining : -1.0f;
}

void BWC::setHeatLoss(float v)
{
    _heat_loss = clampHeatLoss(v);
    _save_settings_needed = true;
}

// Begin a heat-loss measurement. The actual hardware control (heater/AirJet/
// HydroJet off, pump on) happens in _handleHeatLossCalibration so the toggles
// are set at the right point in the loop. The only refusal here is too small a
// water/ambient difference (err carries a short code the UI localizes).
bool BWC::startHeatLossCalibration(String &err)
{
    float waterC = waterTempC(cio);
    if (waterC - _ambient_temp < 8.0f)
    {
        err = F("tempdiff");
        return false;
    }

    _hlcal = heatloss_cal_t{}; // reset, keep no stale result mid-run
    _hlcal.active = true;
    _hlcal.phase = 0;
    _hlcal.req_ts = _timestamp_secs;
    _hlcal.target_drop = 2.0f;
    // Remember what we turned off so we can put it back afterwards, otherwise a
    // tub that was heating to target silently stops heating once we finish.
    _hlcal.prev_heat = cio->cio_states.heat;
    _hlcal.prev_air = cio->cio_states.bubbles;
    _hlcal.prev_jet = cio->cio_states.jets;
    _hlcal.prev_pump = cio->cio_states.pump;
    _hlcal.prev_unit = cio->cio_states.unit;
    _command_queue_paused_for_calibration = true;
    return true;
}

// Called from the HTTP handler. Only flag the cancel: loop() overwrites
// cio_toggles from the display every iteration (before handleToggles runs),
// so restore presses issued here would be wiped before ever being consumed.
// _handleHeatLossCalibration acts on the flag from inside the loop window.
void BWC::cancelHeatLossCalibration()
{
    if (_hlcal.active)
        _hlcal.cancel_requested = true;
}

void BWC::_finishHeatLossCalibration(uint8_t error)
{
    _restoreCalibrationState();
    _hlcal.error = error;
    _hlcal.active = false;
    _command_queue_paused_for_calibration = false;
}

// Re-issue toggles to undo whatever phase 0 changed (heater/air/jets off, pump
// on), bringing each back to its pre-calibration state. A toggle is a press, so
// only issue it when the current state still differs from the remembered one.
void BWC::_restoreCalibrationState()
{
    if (_hlcal.did_heat && cio->cio_states.heat != _hlcal.prev_heat)
        cio->cio_toggles.heat_change = 1;
    if (_hlcal.did_air && cio->cio_states.bubbles != _hlcal.prev_air)
        cio->cio_toggles.bubbles_change = 1;
    if (_hlcal.did_jet && cio->getHasjets() && cio->cio_states.jets != _hlcal.prev_jet)
        cio->cio_toggles.jets_change = 1;
    if (_hlcal.did_pump && cio->cio_states.pump != _hlcal.prev_pump)
        cio->cio_toggles.pump_change = 1;
    if (_hlcal.did_unit && cio->cio_states.unit != _hlcal.prev_unit)
        cio->cio_toggles.unit_change = 1;
}

// Called every loop. Phase 0 drives the tub into the measuring state (pump on,
// heater/AirJet/HydroJet off, display in F for finer ticks) and lets the water
// circulate; phase 1 times the cool-down between two sensor tick edges and
// derives the W/K from that interval.
void BWC::_handleHeatLossCalibration()
{
    if (!_hlcal.active)
        return;

    if (_hlcal.cancel_requested)
    {
        _hlcal.cancel_requested = false;
        _restoreCalibrationState();
        _hlcal.active = false;
        _hlcal.phase = 0;
        _command_queue_paused_for_calibration = false;
        return;
    }

    if (_hlcal.phase == 0)
    {
        // Issue each off/on toggle once (a *_change flag is a toggle press, so
        // re-issuing would flip it back). The conditional guards the desired state.
        if (!_hlcal.did_heat)
        {
            if (cio->cio_states.heat)
                cio->cio_toggles.heat_change = 1;
            _hlcal.did_heat = true;
        }
        if (!_hlcal.did_air)
        {
            if (cio->cio_states.bubbles)
                cio->cio_toggles.bubbles_change = 1;
            _hlcal.did_air = true;
        }
        if (!_hlcal.did_jet)
        {
            if (cio->getHasjets() && cio->cio_states.jets)
                cio->cio_toggles.jets_change = 1;
            _hlcal.did_jet = true;
        }
        if (!_hlcal.did_pump)
        {
            if (!cio->cio_states.pump)
                cio->cio_toggles.pump_change = 1;
            _hlcal.did_pump = true;
        }
        if (!_hlcal.did_unit)
        {
            // Measure in Fahrenheit: an F tick is 5/9 C, nearly doubling the
            // sensor resolution and halving the tick interval. MSPA is
            // Celsius-native and ignores unit_change -> runs with 1 C ticks.
            if (cio->cio_states.unit)
                cio->cio_toggles.unit_change = 1;
            _hlcal.did_unit = true;
        }

        // Mirror the phase-1 abort guard exactly, or a state it aborts on
        // (e.g. a lingering heatred) could survive the settle and trip an
        // immediate INTERRUPTED right after entering phase 1.
        bool ready = cio->cio_states.pump && !cio->cio_states.heat &&
                     !cio->cio_states.heatred && !cio->cio_states.bubbles &&
                     (!cio->getHasjets() || !cio->cio_states.jets);
        if (ready)
        {
            // Start a settle countdown once the pump is circulating, so the
            // baseline reading is accurate. A heated idle tub is stratified;
            // the first pump minutes mix the layers and drop the reading -
            // that mixing must not be booked as cooling.
            if (_hlcal.prep_until == 0)
                _hlcal.prep_until = _timestamp_secs + 60;
            if ((int64_t)_timestamp_secs >= (int64_t)_hlcal.prep_until)
            {
                _hlcal.start_water_c = waterTempC(cio);
                _hlcal.start_amb_c = _ambient_temp;
                _hlcal.amb_sum = (float)_ambient_temp;
                _hlcal.amb_n = 1;
                _hlcal.amb_last_ts = _timestamp_secs;
                _hlcal.start_ts = _timestamp_secs;
                _hlcal.last_disp = cio->cio_states.temperature;
                // one display tick in the unit we measure in (F preferred above)
                _hlcal.target_drop = cio->cio_states.unit ? 1.0f : (5.0f / 9.0f);
                _hlcal.phase = 1;
            }
        }
        else
        {
            _hlcal.prep_until = 0; // state regressed, restart the countdown
        }

        // Give up if we never reach the measuring state (e.g. pump won't run).
        // Only while still trying to reach `ready` - once the settle has
        // started (prep_until != 0) it has its own deadline, so a slow path to
        // idle must not trip this PREP timeout mid-settle.
        if (_hlcal.prep_until == 0 &&
            (int64_t)_timestamp_secs - (int64_t)_hlcal.req_ts > 120)
        {
            _finishHeatLossCalibration(HLCAL_ERR_PREP);
        }
        return;
    }

    // phase 1: measuring via tick edges. The sensor only reports whole display
    // degrees, but the *moment* the reading flips down one tick the true
    // temperature crosses a known boundary. Anchor on the first downward flip,
    // finish on the next one: the interval between two exact crossings measures
    // exactly one tick of cooling, independent of where in a tick we started.
    time_t now = _timestamp_secs;

    // Hold the measuring state (pump on, heater/AirJet/HydroJet off) instead of
    // aborting: many tub boxes auto-cycle the pump or re-heat on their own, and
    // only our command queue is paused. Re-press the offending button and only
    // give up if the state can't be restored within 5 minutes.
    if (cio->cio_states.heat || cio->cio_states.heatred ||
        cio->cio_states.bubbles || !cio->cio_states.pump ||
        (cio->getHasjets() && cio->cio_states.jets))
    {
        if (_hlcal.disturb_ts == 0)
        {
            _hlcal.disturb_ts = now;
            _hlcal.reassert_ts = 0;
        }
        if (cio->cio_states.heatred)
            _hlcal.heat_ran = true; // energy actually entered the water
        if ((int64_t)now - (int64_t)_hlcal.disturb_ts > 300)
        {
            _finishHeatLossCalibration(HLCAL_ERR_INTERRUPTED);
            return;
        }
        if ((int64_t)now - (int64_t)_hlcal.reassert_ts >= 5)
        {
            _hlcal.reassert_ts = now;
            // heatred alone has no button to press; wait for it to clear.
            if (cio->cio_states.heat)
                cio->cio_toggles.heat_change = 1;
            if (cio->cio_states.bubbles)
                cio->cio_toggles.bubbles_change = 1;
            if (cio->getHasjets() && cio->cio_states.jets)
                cio->cio_toggles.jets_change = 1;
            if (!cio->cio_states.pump)
                cio->cio_toggles.pump_change = 1;
        }
        return; // never sample while disturbed
    }
    if (_hlcal.disturb_ts != 0)
    {
        _hlcal.disturb_ts = 0;
        if (_hlcal.heat_ran)
        {
            if (_hlcal.anchored)
                _hlcal.discarded++;
            _hlcal.anchored = false; // heater added energy -> interval invalid
            _hlcal.heat_ran = false;
        }
        _hlcal.last_disp = cio->cio_states.temperature; // swallow reading jumps
    }

    // Sample ambient once a minute over the measured window only, so a
    // day/night swing during a multi-hour interval doesn't bias dTavg.
    if (_hlcal.anchored && (int64_t)now - (int64_t)_hlcal.amb_last_ts >= 60)
    {
        _hlcal.amb_sum += (float)_ambient_temp;
        _hlcal.amb_n++;
        _hlcal.amb_last_ts = now;
    }

    float step = cio->cio_states.unit ? 1.0f : (5.0f / 9.0f); // C per display tick
    int disp = cio->cio_states.temperature;
    if (disp > _hlcal.last_disp)
    {
        // upward flip (sloshing/solar gain): current interval is unusable
        if (_hlcal.anchored)
            _hlcal.discarded++;
        _hlcal.anchored = false;
        _hlcal.last_disp = disp;
    }
    else if (disp < _hlcal.last_disp)
    {
        _hlcal.last_disp = disp;
        float waterC = waterTempC(cio);
        int64_t interval_s = (int64_t)now - (int64_t)_hlcal.anchor_ts;
        if (!_hlcal.anchored || interval_s < (int64_t)HLCAL_MIN_ELAPSED_S)
        {
            // first flip, or implausibly fast (noise) -> (re-)anchor here
            if (_hlcal.anchored)
                _hlcal.discarded++;
            _anchorHeatLossCalibration(now, waterC);
        }
        else
        {
            // Finish: exactly one tick fell between two known boundary
            // crossings. Energy balance while idle: C*drop over the interval
            // is the NET power leaving; the running pump feeds PUMPPOWER of
            // heat back in, so true loss = netLoss + pump. k = loss / dTavg.
            float drop_c = _hlcal.anchor_water_c - waterC;
            float elapsed_h = (float)interval_s / 3600.0f;
            float amb_avg = _hlcal.amb_sum / (float)_hlcal.amb_n;
            // Midpoint of the two crossing boundaries. The +0.5*step offset is
            // exact for a rounding display, off by at most half a tick for a
            // truncating one (<=6% on the required >=8 K difference).
            float dTavg = (_hlcal.anchor_water_c + waterC) / 2.0f + 0.5f * step - amb_avg;
            if (dTavg < 1.0f) // water/ambient difference collapsed -> unusable
            {
                _finishHeatLossCalibration(HLCAL_ERR_UNSTABLE);
                return;
            }
            float netLossW = (_pool_capacity * 1.163f * drop_c) / elapsed_h;
            if (netLossW > HLCAL_MAX_NET_LOSS_W)
            {
                // implausibly fast = noise; re-anchor instead of aborting,
                // the 36h deadline below is the backstop
                _hlcal.discarded++;
                _anchorHeatLossCalibration(now, waterC);
                return;
            }
            float k = clampHeatLoss((netLossW + cio->getPower().PUMPPOWER) / dTavg);
            _heat_loss = k;
            _hlcal.result = k;
            _finishHeatLossCalibration(HLCAL_ERR_NONE);
            _save_settings_needed = true;
            return;
        }
    }

    // genuinely too stable/unresponsive to measure
    if ((int64_t)now - (int64_t)_hlcal.start_ts > 36 * 3600)
        _finishHeatLossCalibration(HLCAL_ERR_TIMEOUT);
}

// (Re-)start a measurement interval at a tick boundary: remember the crossing
// time/temperature and reset the ambient window to cover only this interval.
void BWC::_anchorHeatLossCalibration(time_t now, float waterC)
{
    _hlcal.anchored = true;
    _hlcal.anchor_ts = now;
    _hlcal.anchor_water_c = waterC;
    _hlcal.amb_sum = (float)_ambient_temp;
    _hlcal.amb_n = 1;
    _hlcal.amb_last_ts = now;
}

void BWC::getJSONHeatLossCalibration(String &rtn)
{
    DynamicJsonDocument doc(512);
    doc[F("ACTIVE")] = _hlcal.active;
    doc[F("CURRENT")] = _heat_loss;
    doc[F("DEFAULT")] = HEAT_LOSS_DEFAULT;
    doc[F("TARGETDROP")] = _hlcal.target_drop;
    doc[F("RESULT")] = _hlcal.result;
    if (_hlcal.error != HLCAL_ERR_NONE)
        doc[F("ERROR")] = hlCalErrorName(_hlcal.error);
    doc[F("PHASE")] = _hlcal.phase; // 0 = preparing (pump/heater), 1 = measuring
    if (_hlcal.active && _hlcal.phase == 1)
    {
        float waterC = waterTempC(cio);
        doc[F("STARTWATER")] = _hlcal.start_water_c;
        // running ambient mean so far (what the result will actually use)
        doc[F("STARTAMB")] = _hlcal.amb_n ? _hlcal.amb_sum / (float)_hlcal.amb_n
                                          : (float)_hlcal.start_amb_c;
        doc[F("DROP")] = _hlcal.start_water_c - waterC;
        doc[F("ELAPSED")] = (uint32_t)((int64_t)_timestamp_secs - (int64_t)_hlcal.start_ts);
        doc[F("ANCHORED")] = _hlcal.anchored;
        doc[F("RESTORING")] = _hlcal.disturb_ts != 0;
        doc[F("DISCARDED")] = _hlcal.discarded;
        if (_hlcal.anchored)
            doc[F("ANCHORELAPSED")] = (uint32_t)((int64_t)_timestamp_secs - (int64_t)_hlcal.anchor_ts);
    }
    serializeJson(doc, rtn);
}

void BWC::print(const String &txt)
{
    dsp->text += txt;
}

void BWC::printStatic(const String &txt)
{
    // Clear any scrolling text to show static characters
    dsp->text = "";
    
    // Store static text characters - these will be applied in loop()
    _static_char1 = txt.length() > 0 ? txt[0] : ' ';
    _static_char2 = txt.length() > 1 ? txt[1] : ' ';
    _static_char3 = txt.length() > 2 ? txt[2] : ' ';
    _static_text_active = true;
}

void BWC::clearStatic()
{
    _static_text_active = false;
}

// String BWC::getDebugData()
// {
//     String res = "from cio ";
//     res += cio->cio_states.toString();
//     res += "to dsp ";
//     res += dsp->dsp_states.toString();
//     res += "from dsp ";
//     res += dsp->dsp_toggles.toString();
//     res += "to cio ";
//     res += cio->cio_toggles.toString();
//     res += "BtnQLen: ";
//     res += cio->_button_que_len;
//     return res;
// }

void BWC::setAmbientTemperature(int64_t amb, bool unit)
{
    _ambient_temp = (int)amb;
    if (!unit)
        _ambient_temp = F2C(_ambient_temp);
    _ambient_set = true;
}

int BWC::getAmbientTemperature()
{
    return _ambient_temp;
}

String BWC::getModel()
{
    return cio->getModel();
}

bool BWC::getWeather()
{
    return _weather;
}

bool BWC::add_command(command_que_item command_item)
{
    if ((int)_command_que.size() >= MAXCOMMANDS)
    {
        Serial.printf("Command queue full (%d/%d) - rejected CMD %d\n",
                       _command_que.size(), MAXCOMMANDS, command_item.cmd);
        return false;
    }
    _save_cmdq_needed = true;
    if (command_item.cmd == SETREADY)
    {
        command_item.val = (int64_t)command_item.xtime;
        command_item.xtime = 0;
        command_item.interval = 0;
    }
    _command_que.push_back(command_item);
    std::sort(_command_que.begin(), _command_que.end(), _compare_command);
    return true;
}

bool BWC::edit_command(uint8_t index, command_que_item command_item)
{
    if (index >= _command_que.size())
        return false;
    _save_cmdq_needed = true;
    if (command_item.cmd == SETREADY)
    {
        command_item.val = (int64_t)command_item.xtime; // Use val field to store the time to be ready
        command_item.xtime = 0;                         // And start checking now
        command_item.interval = 0;
    }
    // add parameters to _command_que[index] and sort the array on xtime.
    _command_que.at(index) = command_item;
    std::sort(_command_que.begin(), _command_que.end(), _compare_command);
    return true;
}

bool BWC::set_command_enabled(uint8_t index, bool enabled)
{
    if (index >= _command_que.size())
        return false;
    _command_que[index].enabled = enabled;
    _save_cmdq_needed = true;
    return true;
}

void BWC::set_command_queue_enabled(bool enabled)
{
    _command_queue_enabled = enabled;
    _save_cmdq_needed = true;
}

bool BWC::del_command(uint8_t index)
{
    if (index >= _command_que.size())
        return false;
    _save_cmdq_needed = true;
    _command_que.erase(_command_que.begin() + index);
    return true;
}

// check for special button sequence
bool BWC::getBtnSeqMatch()
{
    if (_btn_sequence[0] == POWER &&
        _btn_sequence[1] == LOCK &&
        _btn_sequence[2] == TIMER &&
        _btn_sequence[3] == POWER)
    {
        return true;
    }
    return false;
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
    doc[F("GOD")] = (uint8_t)cio->cio_states.godmode;
    doc[F("TGT")] = cio->cio_states.target;
    doc[F("TMP")] = cio->cio_states.temperature;
    doc[F("AMBC")] = _ambient_temp;
    doc[F("AMBF")] = round(C2F(_ambient_temp));
    if (cio->cio_states.unit)
    {
        // celsius
        doc[F("AMB")] = _ambient_temp;
        doc[F("TGTC")] = cio->cio_states.target;
        doc[F("TMPC")] = cio->cio_states.temperature;
        doc[F("TGTF")] = round(C2F((float)cio->cio_states.target));
        doc[F("TMPF")] = round(C2F((float)cio->cio_states.temperature));
    }
    else
    {
        // farenheit
        doc[F("AMB")] = round(C2F(_ambient_temp));
        doc[F("TGTF")] = cio->cio_states.target;
        doc[F("TMPF")] = cio->cio_states.temperature;
        doc[F("TGTC")] = round(F2C((float)cio->cio_states.target));
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
    DynamicJsonDocument doc(1024);
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
    DynamicJsonDocument doc(1024);
    _buildJSONTimes(doc.to<JsonObject>());

    // Serialize JSON to string
    if (serializeJson(doc, rtn) == 0)
    {
        rtn = F("{\"error\": \"Failed to serialize times\"}");
    }
}

void BWC::getJSONSettings(String &rtn)
{
// feed the dog
#ifdef ESP8266
    ESP.wdtFeed();
#endif
    // Serialize from live RAM. Do NOT _loadSettings() here: this is a getter,
    // and reloading stale flash (saved only every 600s) clobbered the live
    // energy/runtime counters, making total/daily jump backwards on every
    // settings poll.

    DynamicJsonDocument doc(2048);

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
    doc[F("TIMEZONE")] = _timezone;
    doc[F("TIMEZONE_NAME")] = _timezone_name;
#ifdef ESP8266
    doc[F("REBOOTINFO")] = ESP.getResetReason();
#endif
    doc[F("REBOOTTIME")] = reboot_time_t;
    doc[F("MODEL")] = cio->getModel();
    doc[F("WEATHER")] = _weather;
    doc[F("HASJETS")] = cio->getHasjets();
    doc[F("POOLCAP")] = _pool_capacity;
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
    if (serializeJson(doc, rtn) == 0)
    {
        rtn = F("{\"error\": \"Failed to serialize settings\"}");
    }
}

void BWC::getJSONCommandQueue(String &rtn)
{
#ifdef ESP8266
    ESP.wdtFeed();
#endif
    DynamicJsonDocument doc(COMMAND_QUEUE_JSON_CAPACITY);
    // A failed pool allocation leaves capacity 0, every insert below is silently
    // dropped and the result serializes as "{}" - which looks like an empty queue
    // instead of an error. Say so instead.
    if (doc.capacity() == 0)
    {
        rtn = F("{\"error\": \"Out of memory building cmdq\"}");
        return;
    }
    doc[F("LEN")] = _command_que.size();
    doc[F("QEN")] = _command_queue_enabled ? 1 : 0;
    for (unsigned int i = 0; i < _command_que.size(); i++)
    {
        doc[F("CMD")][i] = _command_que[i].cmd;
        doc[F("VALUE")][i] = _command_que[i].val;
        doc[F("XTIME")][i] = _command_que[i].xtime;
        doc[F("INTERVAL")][i] = _command_que[i].interval;
        doc[F("TXT")][i] = _command_que[i].text;
        doc[F("EN")][i] = _command_que[i].enabled ? 1 : 0;
    }

    rtn.clear();
    if (serializeJson(doc, rtn) == 0)
    {
        rtn = F("{\"error\": \"Failed to serialize cmdq\"}");
    }
}

/*TODO:*/
uint8_t BWC::getState(int state)
{
    // return cio->getState(state);
    return 0;
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
                            const __FlashStringHelper* tkey, uint16_t value, uint32_t stamp)
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

    // Smart schedule status (device-side fields of getJSONSmartSchedule; the
    // backend derives buffer/cost/countdowns from these + settings PRICE)
    JsonObject ss = doc.createNestedObject(F("SS"));
    ss[F("ACTIVE")]     = _smart_schedule.active;
    ss[F("TARGETTIME")] = _smart_schedule.target_time;
    ss[F("TARGETTEMP")] = _smart_schedule.target_temp;
    ss[F("KEEPON")]     = _smart_schedule.keep_heater_on;
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

void BWC::getButtonName(String &rtn)
{
    rtn = ButtonNames[dsp->dsp_toggles.pressed_button];
}

Buttons BWC::getButton()
{
    return dsp->dsp_toggles.pressed_button;
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
        !readBoundedString(obj, F("TIMEZONE"), 96, timezone, errorMessage) ||
        !readBoundedString(obj, F("TIMEZONE_NAME"), 64, timezoneName, errorMessage) ||
        !readBooleanSetting(obj, F("WEATHER"), weather, errorMessage) ||
        !readBoundedInteger(obj, F("POOLCAP"), 1, 100000, poolCapacity, errorMessage) ||
        !readBoundedInteger(obj, F("AIRTO"), 5, 30, airjetTimeout, errorMessage) ||
        !readBoundedInteger(obj, F("HJTO"), 5, 60, hydrojetTimeout, errorMessage) ||
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
    if (doc.containsKey(F("TIMEZONE")))
        _timezone = timezone;
    if (_timezone.length() == 0)
        _timezone = defaultTimezone;
    if (doc.containsKey(F("TIMEZONE_NAME")))
        _timezone_name = timezoneName;
    if (_timezone_name.length() == 0)
        _timezone_name = defaultTimezoneName;
    applyTimezone();
    if (doc.containsKey(F("WEATHER")))
        _weather = weather;
    if (doc.containsKey(F("POOLCAP")))
        _pool_capacity = (int)poolCapacity;
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
    saveSettings();
    _loadSettings();
    return true;
}

bool BWC::newData()
{
    bool result = _new_data_available;
    _new_data_available = false;
    return result;
}

void BWC::applyTimezone() const
{
    const char *timezone = _timezone.length() ? _timezone.c_str() : defaultTimezone;
    setenv("TZ", timezone, 1);
    tzset();
}

bool BWC::_getEnergyDayIndex(int &day_index) const
{
    if (_timestamp_secs <= VALID_TIME_THRESHOLD_S)
        return false;

    time_t ts = (time_t)_timestamp_secs;
    struct tm timeinfo;
    localtime_r(&ts, &timeinfo);
    day_index = timeinfo.tm_yday + timeinfo.tm_year * 366;
    return true;
}

// Tariff-aware price lookup. Falls back to the day price while the clock is
// invalid (short boot transients) so no cost is ever lost.
double BWC::_priceAt(uint64_t ts) const
{
    if (!_night_enabled || ts <= VALID_TIME_THRESHOLD_S)
        return _price;
    time_t t = (time_t)ts;
    struct tm ti;
    localtime_r(&t, &ti);
    if (_night_weekend && (ti.tm_wday == 0 || ti.tm_wday == 6))
        return _price_night;
    int m = ti.tm_hour * 60 + ti.tm_min;
    bool night = (_night_start_min <= _night_end_min)
        ? (m >= _night_start_min && m < _night_end_min)
        : (m >= _night_start_min || m < _night_end_min); // window wraps midnight
    return night ? _price_night : _price;
}

// Average tariff over a heating window, sampled in 15-min steps (capped; this
// is an estimate, not billing).
double BWC::_avgPriceOver(uint64_t start_ts, float hours) const
{
    if (!_night_enabled || start_ts <= VALID_TIME_THRESHOLD_S || hours <= 0)
        return _priceAt(start_ts);
    uint32_t steps = (uint32_t)ceilf(hours * 4);
    if (steps > 960) steps = 960; // >10 days: coarser sampling is fine
    uint64_t step_s = (uint64_t)(hours * 3600.0f / steps);
    double sum = 0;
    for (uint32_t i = 0; i < steps; i++)
        sum += _priceAt(start_ts + i * step_s);
    return sum / steps;
}

void BWC::_addEnergyIncrement(double energy_increment_Ws)
{
    double cost_increment = (energy_increment_Ws / 3600000.0) * _priceAt(_timestamp_secs);
    _energy_total_kWh += energy_increment_Ws / 3600000.0;
    _energy_cost_total += cost_increment;

    int today;
    if (!_getEnergyDayIndex(today))
    {
        _energy_pending_daily_Ws += energy_increment_Ws;
        _energy_pending_daily_cost += cost_increment;
        return;
    }

    if (_energy_daily_yday < 0)
    {
        _energy_daily_yday = today;
    }
    else if (today != _energy_daily_yday)
    {
        _energy_daily_Ws = 0;
        _energy_daily_cost = 0;
        _energy_daily_yday = today;
    }

    if (_energy_pending_daily_Ws > 0)
    {
        _energy_daily_Ws += _energy_pending_daily_Ws;
        _energy_daily_cost += _energy_pending_daily_cost;
        _energy_pending_daily_Ws = 0;
        _energy_pending_daily_cost = 0;
    }

    _energy_daily_Ws += energy_increment_Ws;
    _energy_daily_cost += cost_increment;
}

/**
 * Check if airjets or hydrojets have exceeded their configured timeout
 * and turn them off if necessary
 */
void BWC::_checkJetTimeouts()
{
    uint32_t now = millis();
    
    // Check airjet timeout (only if configured below hardware default of 30 min)
    if (cio->cio_states.bubbles && _airjet_timeout_minutes < 30)
    {
        uint32_t elapsed_ms = now - _bubbles_change_timestamp_ms;
        uint32_t timeout_ms = (uint32_t)_airjet_timeout_minutes * 60UL * 1000UL;
        if (elapsed_ms >= timeout_ms)
        {
            // Queue command to turn off airjets
            command_que_item item;
            item.cmd = SETBUBBLES;
            item.val = 0;
            item.xtime = 0;
            item.interval = 0;
            item.text = "";
            add_command(item);
            Serial.println(F("Airjet timeout - turning off"));
        }
    }
    
    // Check hydrojet timeout (only if configured below hardware default of 60 min)
    if (cio->cio_states.jets && _hydrojet_timeout_minutes < 60)
    {
        uint32_t elapsed_ms = now - _jets_change_timestamp_ms;
        uint32_t timeout_ms = (uint32_t)_hydrojet_timeout_minutes * 60UL * 1000UL;
        if (elapsed_ms >= timeout_ms)
        {
            // Queue command to turn off hydrojets
            command_que_item item;
            item.cmd = SETJETS;
            item.val = 0;
            item.xtime = 0;
            item.interval = 0;
            item.text = "";
            add_command(item);
            Serial.println(F("Hydrojet timeout - turning off"));
        }
    }
}

/**
 * Host mode (Gastgebermodus): once the water has reached the target temperature
 * while the heater is enabled, allow the heater to keep maintaining the
 * temperature for a limited time, then turn it off. Pressing HEAT again starts
 * a new cycle. Temperature dips below target do NOT reset the countdown,
 * otherwise thermostat oscillation would keep the heater on forever.
 */
void BWC::_checkHostHeaterTimeout()
{
    if (!_host_mode || _host_heater_timeout_min == 0 || !cio->cio_states.heat)
    {
        _host_target_reached_ms = 0;
        return;
    }

    float targetC = cio->cio_states.unit ? (float)cio->cio_states.target : F2C((float)cio->cio_states.target);
    if (_host_target_reached_ms == 0)
    {
        if (waterTempC(cio) >= targetC)
            _host_target_reached_ms = millis();
        return;
    }

    if (millis() - _host_target_reached_ms >= (unsigned long)_host_heater_timeout_min * 60UL * 1000UL)
    {
        command_que_item item;
        item.cmd = SETHEATER;
        item.val = 0;
        item.xtime = 0;
        item.interval = 0;
        item.text = "";
        add_command(item);
        _host_target_reached_ms = 0;
        Serial.println(F("Host mode heater timeout - turning off"));
    }
}

void BWC::_updateTimes()
{
    uint32_t now = millis();
    static uint32_t prevtime = now;
    uint32_t elapsedtime_ms = now - prevtime;
    prevtime = now;
    if (cio->cio_states.heatred)
    {
        _heatingtime_ms += elapsedtime_ms;
    }
    if (cio->cio_states.pump)
    {
        _pumptime_ms += elapsedtime_ms;
    }
    if (cio->cio_states.bubbles)
    {
        _airtime_ms += elapsedtime_ms;
    }
    if (cio->cio_states.jets)
    {
        _jettime_ms += elapsedtime_ms;
    }
    _uptime_ms += elapsedtime_ms;

    if (_uptime_ms > 1000000000)
    {
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
    }

    if (_override_dsp_brt_timer > 0)
        _override_dsp_brt_timer -= (int32_t)elapsedtime_ms;

    _energy_power_W = cio->cio_states.heatred * cio->getPower().HEATERPOWER;
    _energy_power_W += cio->cio_states.pump * cio->getPower().PUMPPOWER;
    _energy_power_W += cio->cio_states.bubbles * cio->getPower().AIRPOWER;
    _energy_power_W += cio->getPower().IDLEPOWER;
    _energy_power_W += cio->cio_states.jets * cio->getPower().JETPOWER;

    double energy_increment_Ws = (static_cast<double>(elapsedtime_ms) * _energy_power_W) / 1000.0;
    _addEnergyIncrement(energy_increment_Ws);

    // Mirror the live counters to RTC so a crash-reboot doesn't lose them.
    // written every loop - RTC RAM, no flash wear; fold ms into
    // seconds so the snapshot is current (the second-counters only roll over
    // every ~11 days otherwise).
    CountersRtc snap{};
    snap.magic = COUNTERS_RTC_MAGIC;
    snap.total_kWh = _energy_total_kWh;
    snap.cost_total = _energy_cost_total;
    snap.daily_Ws = _energy_daily_Ws;
    snap.pending_Ws = _energy_pending_daily_Ws;
    snap.daily_cost = _energy_daily_cost;
    snap.pending_cost = _energy_pending_daily_cost;
    snap.yday = _energy_daily_yday;
    snap.uptime_s = _uptime + _uptime_ms / 1000;
    snap.pumptime_s = _pumptime + _pumptime_ms / 1000;
    snap.heatingtime_s = _heatingtime + _heatingtime_ms / 1000;
    snap.airtime_s = _airtime + _airtime_ms / 1000;
    snap.jettime_s = _jettime + _jettime_ms / 1000;
    snap.crc = countersRtcCrc(snap);
    countersRtcStore(snap);

    if (_notes.size())
    {
        _note_duration += (int)elapsedtime_ms;
        while (_notes.size() && _note_duration >= _notes.back().duration_ms)
        {
            _note_duration -= _notes.back().duration_ms;
            _notes.pop_back();
        }
        dsp->audiofrequency = _notes.size() ? _notes.back().frequency_hz : 0;
    }
    else
    {
        dsp->audiofrequency = 0;
    }
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
    _audio_enabled = doc[F("AUDIO")] | true;
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
    _pool_capacity = doc[F("POOLCAP")] | 700;
    _heat_loss = clampHeatLoss(doc[F("HLOSS")] | HEAT_LOSS_DEFAULT);
    _airjet_timeout_minutes = doc[F("AIRTO")] | 30;
    _hydrojet_timeout_minutes = doc[F("HJTO")] | 60;
    _host_mode = doc[F("GSTMODE")] | false;
    _host_max_target_c = doc[F("GSTMAXTGT")] | 40;
    _host_heater_timeout_min = doc[F("GSTHTRTO")] | 0;
    _brightness_boost_steps = doc[F("BRTSTEP")] | 1;
    _brightness_boost_duration_s = doc[F("BRTDUR")] | 5;
    dsp->EnabledButtons[LOCK] = doc[F("LCK")] | 1;
    dsp->EnabledButtons[TIMER] = doc[F("TMR")] | 1;
    dsp->EnabledButtons[BUBBLES] = doc[F("AIR")] | 1;
    dsp->EnabledButtons[UNIT] = doc[F("UNT")] | 1;
    dsp->EnabledButtons[HEAT] = doc[F("HTR")] | 1;
    dsp->EnabledButtons[PUMP] = doc[F("FLT")] | 1;
    dsp->EnabledButtons[DOWN] = doc[F("DN")] | 1;
    dsp->EnabledButtons[UP] = doc[F("UP")] | 1;
    dsp->EnabledButtons[POWER] = doc[F("PWR")] | 1;
    dsp->EnabledButtons[HYDROJETS] = doc[F("HJT")] | 1;

    Serial.printf("Loaded: PRICE=%.2f, WEATHER=%d\n", _price, _weather);
    file.close();
}

// Recover counters lost between the last flash save and a crash-reboot. RTC is
// strictly newer than flash (written every loop vs every 600s), so prefer it
// when valid; max() guards the monotonic counters against any stale RTC. A
// power-loss cold boot has no valid RTC and falls through to the flash values.
void BWC::_restoreCountersFromRtc()
{
    CountersRtc e{};
    if (!countersRtcFetch(e) || e.magic != COUNTERS_RTC_MAGIC || e.crc != countersRtcCrc(e))
        return;

    _energy_total_kWh = std::max(_energy_total_kWh, e.total_kWh);
    _energy_cost_total = std::max(_energy_cost_total, e.cost_total);
    _energy_daily_Ws = e.daily_Ws;
    _energy_pending_daily_Ws = e.pending_Ws;
    _energy_daily_cost = e.daily_cost;
    _energy_pending_daily_cost = e.pending_cost;
    _energy_daily_yday = e.yday;
    _uptime = std::max(_uptime, e.uptime_s);
    _pumptime = std::max(_pumptime, e.pumptime_s);
    _heatingtime = std::max(_heatingtime, e.heatingtime_s);
    _airtime = std::max(_airtime, e.airtime_s);
    _jettime = std::max(_jettime, e.jettime_s);
    Serial.println(F("Restored counters from RTC"));
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
    item.cmd = SETTARGET;
    item.val = tgt;
    item.xtime = 0;
    item.interval = 0;
    item.text = "";
    add_command(item);
    // Serial.println(F("Restoring states"));
    file.close();
}

void BWC::reloadCommandQueue()
{
    loadCommandQueue();
    return;
}

void BWC::loadCommandQueue()
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
                    uint64_t k = (now - item.xtime + item.interval - 1) / item.interval;
                    item.xtime += k * item.interval;
                }
                _command_que.push_back(item);
            }
            std::sort(_command_que.begin(), _command_que.end(), _compare_command);
        }
        file.close();
    }
    else
    {
        Serial.println(F("FS: /cmdq.json not found - continuing without command queue"));
    }

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
    Serial.println(F("Calling _restoreStates()..."));
    _restoreStates();
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

    // Build before truncating the file: without the pool the document stays empty
    // and would overwrite a good cmdq.json with "{}", losing the whole queue.
    DynamicJsonDocument doc(COMMAND_QUEUE_JSON_CAPACITY);
    if (doc.capacity() == 0)
    {
        Serial.printf("CMDQ: out of memory, keeping the stored queue (heap=%u)\n",
                      (unsigned)ESP.getFreeHeap());
        _save_cmdq_needed = true; // retry on the next save tick
        return;
    }

    File file = LittleFS.open("/cmdq.json", "w");
    if (!file)
    {
        Serial.println(F("FS: Failed to open /cmdq.json for write"));
        return;
    }

    // Set the values in the document
    doc[F("LEN")] = _command_que.size();
    doc[F("QEN")] = _command_queue_enabled ? 1 : 0;
    for (unsigned int i = 0; i < _command_que.size(); i++)
    {
        doc[F("CMD")][i] = _command_que[i].cmd;
        doc[F("VALUE")][i] = _command_que[i].val;
        doc[F("XTIME")][i] = _command_que[i].xtime;
        doc[F("INTERVAL")][i] = _command_que[i].interval;
        doc[F("TXT")][i] = _command_que[i].text;
        doc[F("EN")][i] = _command_que[i].enabled ? 1 : 0;
    }

    // Serialize JSON to file
    if (serializeJson(doc, file) == 0)
    {
        // Serial.println(F("Failed to write cmdq.json"));
    }
    else
    {
        String s;
        serializeJson(doc, s);
        // Serial.println(s);
    }
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
    File file = LittleFS.open("/settings.json", "w");
    if (!file)
    {
        Serial.println(F("FS: Failed to open /settings.json for write"));
        return;
    }

    DynamicJsonDocument doc(2048);
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
    doc[F("AUDIO")] = _audio_enabled;
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
    doc[F("POOLCAP")] = _pool_capacity;
    doc[F("HLOSS")] = _heat_loss;
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

/* SOUND */

/*temporary function to render some soundfiles*/
// void BWC::_save_melody(const String& filename)
// {
//     File file = LittleFS.open(filename, "w");
//     if (!file) return;
//     sNote n = {1000, 500};
//     file.write((byte*)&n, sizeof(n));
//     file.close();
// }

bool BWC::_load_melody_json(const String &filename)
{
    if (_notes.size() || !_audio_enabled)
    {
        // Serial.println("Q busy");
        return false;
    }
    File file = LittleFS.open(filename, "r");
    if (!file)
    {
        Serial.println(F("FS: Failed to open melody file for read"));
        return false;
    }
    int beat_period;
    float note_duty_cycle;
    sNote n;

    /*new file format:
    beat period
    note duty cycle
    frequency
    duration (fraction of beat_period)
    frequency
    duration
    ...eof
    */
    String s = file.readStringUntil('\n');
    beat_period = s.toInt();
    s = file.readStringUntil('\n');
    note_duty_cycle = s.toFloat();
    while (file.available())
    {
        s = file.readStringUntil('\n');
        n.frequency_hz = s.toInt();
        s = file.readStringUntil('\n');
        n.duration_ms = beat_period * s.toFloat();
        n.duration_ms *= note_duty_cycle;
        _notes.push_back(n);
        /*add a little break between the notes (will be placed before each note due to reversing)*/
        n.frequency_hz = 0;
        n.duration_ms = beat_period * s.toFloat();
        n.duration_ms *= (1 - note_duty_cycle);
        _notes.push_back(n);
    }

    std::reverse(_notes.begin(), _notes.end());
    file.close();

    return true;
}

// void BWC::_add_melody(const String &filename)
// {
//     if(_notes.size() || !_audio_enabled) return;
//     File file = LittleFS.open(filename, "r");
//     if (!file) return;
//     while(file.available())
//     {
//         sNote n;
//         file.readBytes((char*)&n, sizeof(n));
//         _notes.push_back(n);
//     }
//     file.close();
//     /* We read and erase from the back of the vector (faster) so if notes are stored in the natural order we need to reverse*/
//     std::reverse(_notes.begin(), _notes.end());
// }

void BWC::_sweepdown()
{
    if (_notes.size() || !_audio_enabled)
        return;
    for (int i = 0; i < 128; i++)
    {
        sNote n;
        n.duration_ms = 2;
        n.frequency_hz = 1000 + 8 * i;
        _notes.push_back(n);
    }
}

void BWC::_sweepup()
{
    if (_notes.size() || !_audio_enabled)
        return;
    for (int i = 0; i < 128; i++)
    {
        sNote n;
        n.duration_ms = 2;
        n.frequency_hz = 2000 - 8 * i;
        _notes.push_back(n);
    }
}

void BWC::_beep()
{
    if (_notes.size() || !_audio_enabled)
        return;
    sNote n;
    n.duration_ms = 50;
    n.frequency_hz = 2400;
    _notes.push_back(n);
    n.duration_ms = 50;
    n.frequency_hz = 800;
    _notes.push_back(n);
}

void BWC::_accord()
{
    if (_notes.size() || !_audio_enabled)
        return;
    sNote n;
    for (int i = 0; i < 5; i++)
    {
        n.duration_ms = 10;
        n.frequency_hz = NOTE_C6;
        _notes.push_back(n);
        n.duration_ms = 10;
        n.frequency_hz = NOTE_E6;
        _notes.push_back(n);
    }
}

/*                  */
/* SMART SCHEDULE   */
/*                  */

/**
 * Set a smart schedule for pool heating
 * @param target_time Unix timestamp when pool should be ready
 * @param target_temp Desired temperature in Celsius
 * @param keep_heater_on Keep heater on after target is reached
 * @return true if schedule was set successfully
 */
/*Advance a schedule time by whole calendar days until it lies in the future.
  Calendar math via localtime/mktime so "Sunday 18:00" stays 18:00 local time
  across DST changes (a fixed +604800s would drift by an hour twice a year).
  Returns 0 if no valid future time could be computed.*/
static uint64_t advanceScheduleDays(uint64_t t, uint8_t days, uint64_t now)
{
    if (days == 0)
        return 0;
    time_t tt = (time_t)t;
    struct tm tmv;
    localtime_r(&tt, &tmv);
    for (int guard = 0; (uint64_t)tt <= now && guard < 1000; guard++)
    {
        tmv.tm_mday += days;
        tmv.tm_isdst = -1; // let mktime resolve DST for the new date
        tt = mktime(&tmv);
        if (tt < 0)
            return 0;
    }
    return ((uint64_t)tt > now) ? (uint64_t)tt : 0;
}

bool BWC::setSmartSchedule(uint64_t target_time, uint8_t target_temp, bool keep_heater_on, uint8_t repeat_days)
{
    // Validate time is in future and NTP is synced
    if (time(nullptr) < 57600)
    {
        // NTP not synced yet
        return false;
    }
    
    if (target_time <= _timestamp_secs)
    {
        // Target time is in the past
        return false;
    }
    
    // Validate temperature range (20-40°C)
    if (target_temp < 20 || target_temp > 40)
    {
        return false;
    }
    
    // Initialize smart schedule
    _smart_schedule.active = true;
    _smart_schedule.target_time = target_time;
    _smart_schedule.target_temp = target_temp;
    _smart_schedule.keep_heater_on = keep_heater_on;
    _smart_schedule.repeat_days = repeat_days;
    _smart_schedule.next_check_time = 0; // Check immediately on next loop
    _smart_schedule.calculated_start_time = 0;
    _smart_schedule.temp_reading_state = 0;
    _smart_schedule.temp_reading_timer = 0;
    _smart_schedule.temp_reading_started_pump = false;
    _smart_schedule.accurate_temperature = 0; // Will be read during first temp check cycle
    _smart_schedule.check_completed = false;  // Reset completion status
    _smart_schedule.target_temp_reached = false;
    
    // Calculate initial heating estimate immediately using current sensor temperature.
    // This provides instant UI feedback; the pump measurement cycle will refine it.
    _smart_schedule.last_heating_estimate = _calculateHeatingTime(
        waterTempC(cio), target_temp);
    
    _save_smartschedule_needed = true;
    _new_data_available = true;
    
    Serial.print(F("SmartSchedule: Schedule activated, initial estimate: "));
    Serial.print(_smart_schedule.last_heating_estimate);
    Serial.println(F(" hours"));
    
    return true;
}

/**
 * Update smart schedule heater behavior without resetting timing/calculation state
 * @param keep_heater_on Keep heater on after target temperature is reached
 * @return true if an active schedule was updated
 */
bool BWC::updateSmartScheduleKeepHeaterOn(bool keep_heater_on)
{
    if (!_smart_schedule.active)
        return false;

    if (_smart_schedule.keep_heater_on != keep_heater_on)
    {
        _smart_schedule.keep_heater_on = keep_heater_on;
        _save_smartschedule_needed = true;
        _new_data_available = true;
        Serial.print(F("SmartSchedule: keep heater on updated to "));
        Serial.println(keep_heater_on ? F("true") : F("false"));
    }

    return true;
}

/**
 * Update the repeat interval of an active schedule (0 = one-shot)
 * @return true if an active schedule was updated
 */
bool BWC::updateSmartScheduleRepeat(uint8_t repeat_days)
{
    if (!_smart_schedule.active)
        return false;

    if (_smart_schedule.repeat_days != repeat_days)
    {
        _smart_schedule.repeat_days = repeat_days;
        _save_smartschedule_needed = true;
        _new_data_available = true;
        Serial.print(F("SmartSchedule: repeat days updated to "));
        Serial.println(repeat_days);
    }

    return true;
}

/**
 * Cancel active smart schedule
 * SAFETY: If heater is currently on due to this schedule, turn it off immediately
 * SAFETY: If pump was turned on for temp reading, restore original state
 */
void BWC::cancelSmartSchedule()
{
    command_que_item item;
    item.xtime = 0;
    item.interval = 0;
    item.text = "";
    
    // Turn off heater if it was started by this schedule
    // Check if heater is on AND we had a calculated start time (meaning schedule started it)
    if (cio->cio_states.heat && _smart_schedule.calculated_start_time > 0 && 
        _timestamp_secs >= _smart_schedule.calculated_start_time)
    {
        item.cmd = SETHEATER;
        item.val = 0;
        item.force = false;
        add_command(item);
        Serial.println(F("SmartSchedule: Cancel - Queued heater off"));
    }
    
    // Turn off pump if it's running (either from temp reading or heating phase).
    // temp_reading_state == 1 means the reading cycle just fired pump_change but the
    // hardware hasn't settled yet, so cio_states.pump still reads false -> force it off
    // anyway, otherwise the pump lands on after the schedule is already gone.
    if (cio->cio_states.pump || _smart_schedule.temp_reading_state == 1)
    {
        item.cmd = SETPUMP;
        item.val = 0;
        item.force = true;  // Force pump off even if heater command is pending
        add_command(item);
        Serial.println(F("SmartSchedule: Cancel - Queued pump off (forced)"));
    }
    
    _resetSmartScheduleState();
    Serial.println(F("SmartSchedule: Cancelled and deleted"));
}

/**
 * Reset all smart schedule state to defaults
 * Used by cancelSmartSchedule() and when schedule completes at target_time
 */
void BWC::_resetSmartScheduleState()
{
    _smart_schedule.active = false;
    _smart_schedule.target_time = 0;
    _smart_schedule.target_temp = 0;
    _smart_schedule.keep_heater_on = false;
    _smart_schedule.repeat_days = 0;
    _smart_schedule.calculated_start_time = 0;
    _smart_schedule.next_check_time = 0;
    _smart_schedule.last_heating_estimate = 0;
    _smart_schedule.accurate_temperature = 0;
    _smart_schedule.temp_reading_state = 0;
    _smart_schedule.temp_reading_timer = 0;
    _smart_schedule.temp_reading_started_pump = false;
    _smart_schedule.check_completed = false;
    _smart_schedule.target_temp_reached = false;

    _save_smartschedule_needed = true;
    _new_data_available = true;
}

/**
 * Re-arm a recurring schedule for its next occurrence: new target time,
 * timing/measurement state cleared, temp/keep-on/repeat settings kept.
 */
void BWC::_rearmSmartSchedule(uint64_t next_target_time)
{
    _smart_schedule.target_time = next_target_time;
    _smart_schedule.calculated_start_time = 0;
    _smart_schedule.next_check_time = 0; // Check immediately on next loop
    _smart_schedule.last_heating_estimate = _calculateHeatingTime(
        waterTempC(cio), _smart_schedule.target_temp);
    _smart_schedule.temp_reading_state = 0;
    _smart_schedule.temp_reading_timer = 0;
    _smart_schedule.temp_reading_started_pump = false;
    _smart_schedule.accurate_temperature = 0;
    _smart_schedule.check_completed = false;
    _smart_schedule.target_temp_reached = false;

    _save_smartschedule_needed = true;
    _new_data_available = true;
}

/**
 * Get smart schedule status as JSON
 */
void BWC::getJSONSmartSchedule(String &rtn)
{
#ifdef ESP8266
    ESP.wdtFeed();
#endif
    StaticJsonDocument<1024> doc;
    
    doc[F("CONTENT")] = F("SMARTSCHEDULE");
    doc[F("ACTIVE")] = _smart_schedule.active;
    doc[F("TARGETTIME")] = _smart_schedule.target_time;
    doc[F("TARGETTEMP")] = _smart_schedule.target_temp;
    doc[F("KEEPON")] = _smart_schedule.keep_heater_on;
    doc[F("REPEAT")] = _smart_schedule.repeat_days;
    doc[F("STARTTIME")] = _smart_schedule.calculated_start_time;
    doc[F("NEXTCHECK")] = _smart_schedule.next_check_time;
    doc[F("ESTIMATE")] = _smart_schedule.last_heating_estimate;
    // Calculate safety buffer (10% of estimate, minimum 1 hour)
    float buffer_hours = 0;
    if (_smart_schedule.last_heating_estimate > 0 && _smart_schedule.last_heating_estimate < 999)
    {
        buffer_hours = _smart_schedule.last_heating_estimate * 0.10f;
        if (buffer_hours < 1.0f) buffer_hours = 1.0f;
    }
    doc[F("BUFFER")] = buffer_hours;
    float estimated_kWh = 0.0f;
    float estimated_cost = 0.0f;
    if (_smart_schedule.last_heating_estimate > 0 && _smart_schedule.last_heating_estimate < 999)
    {
        estimated_kWh = _smart_schedule.last_heating_estimate * 2.0f;
        estimated_cost = estimated_kWh * (float)_avgPriceOver(_smart_schedule.calculated_start_time, _smart_schedule.last_heating_estimate);
    }
    doc[F("ESTIMATED_KWH")] = estimated_kWh;
    doc[F("ESTIMATED_COST")] = estimated_cost;
    doc[F("CHECKCOMPLETED")] = _smart_schedule.check_completed;
    doc[F("CURRENTTIME")] = _timestamp_secs;
    doc[F("CURRENTTEMP")] = cio->cio_states.temperature;
    doc[F("GLOBALTARGET")] = cio->cio_states.target; // Current global target temperature
    // If pump is running, the live sensor reading is accurate (water is circulating).
    // Otherwise, use the last stored reading from when the pump was last running.
    // Fallback to current sensor reading if no measurement has been taken yet.
    uint8_t display_temp;
    if (cio->cio_states.pump)
    {
        display_temp = cio->cio_states.temperature;
    }
    else if (_smart_schedule.accurate_temperature > 0)
    {
        display_temp = _smart_schedule.accurate_temperature;
    }
    else
    {
        display_temp = cio->cio_states.temperature;
    }
    doc[F("ACCURATETEMP")] = display_temp;
    doc[F("HEATER")] = cio->cio_states.heat;
    // Split heater state so the UI can tell "heating up" (red) from "target
    // reached, holding" (green); HEATER stays for compatibility.
    doc[F("HEATERRED")] = cio->cio_states.heatred;
    doc[F("HEATERGRN")] = cio->cio_states.heatgrn;
    doc[F("PUMP")] = cio->cio_states.pump;
    doc[F("READING_STATE")] = _smart_schedule.temp_reading_state; // 0=idle, 1=pump_on, 2=reading
    
    // Calculate dynamic remaining heating time (same calculation as dashboard T2R)
    // Only calculate if heater is running and we have valid temperatures
    float remaining_heating_hours = -1.0f; // -1 = not applicable/not heating
    float display_temp_c = displayTempC(cio->cio_states.unit, display_temp);
    if (cio->cio_states.heat && display_temp > 0 && _smart_schedule.target_temp > display_temp_c)
    {
        remaining_heating_hours = _calculateHeatingTime(display_temp_c, _smart_schedule.target_temp);
    }
    else if (cio->cio_states.heat && display_temp_c >= _smart_schedule.target_temp)
    {
        remaining_heating_hours = 0.0f; // Already at or above target
    }
    doc[F("REMAINING_HEATING_TIME")] = remaining_heating_hours;
    
    // Calculate time remaining
    int64_t time_remaining = (int64_t)_smart_schedule.target_time - (int64_t)_timestamp_secs;
    doc[F("TIMEREMAINING")] = time_remaining;
    
    // Calculate time until start
    int64_t time_until_start = (int64_t)_smart_schedule.calculated_start_time - (int64_t)_timestamp_secs;
    doc[F("TIMEUNTILSTART")] = time_until_start;
    
    // Format estimate as HH:mm string for display
    if (_smart_schedule.last_heating_estimate > 0 && _smart_schedule.last_heating_estimate < 999)
    {
        int total_minutes = (int)(_smart_schedule.last_heating_estimate * 60.0f);
        int hours = total_minutes / 60;
        int minutes = total_minutes % 60;
        // Bounds check to prevent buffer overflow (max 999:59)
        if (hours > 999) hours = 999;
        if (minutes < 0) minutes = 0;
        if (minutes > 59) minutes = 59;
        char estimate_str[16];
        snprintf(estimate_str, sizeof(estimate_str), "%02d:%02d", hours, minutes);
        doc[F("ESTIMATE_FMT")] = estimate_str;
    }
    else
    {
        doc[F("ESTIMATE_FMT")] = "";
    }
    
    if (serializeJson(doc, rtn) == 0)
    {
        rtn = F("{\"error\": \"Failed to serialize smartschedule\"}");
    }
}

/**
 * Handle smart schedule logic - called from main loop
 */
void BWC::_handleSmartSchedule()
{
    if (!_smart_schedule.active)
        return;
    // Don't let a scheduled heat-on fight the calibration (it would re-toggle the
    // heater and abort the measurement). Resumed when calibration ends.
    if (_command_queue_paused_for_calibration)
        return;
    
    // Check if target time has passed FIRST (before temp reading)
    // This ensures schedule is completed even if temp reading is in progress
    if (_timestamp_secs >= _smart_schedule.target_time)
    {
        Serial.println(F("SmartSchedule: Target time reached"));
        
        // If keep_heater_on is false, turn off heater and pump
        if (!_smart_schedule.keep_heater_on)
        {
            command_que_item item;
            item.xtime = 0;
            item.interval = 0;
            item.text = "";
            
            if (cio->cio_states.heat)
            {
                item.cmd = SETHEATER;
                item.val = 0;
                item.force = false;
                add_command(item);
                Serial.println(F("SmartSchedule: Queued heater off"));
            }
            
            if (cio->cio_states.pump)
            {
                item.cmd = SETPUMP;
                item.val = 0;
                item.force = true;  // Force pump off even if heater command is pending
                add_command(item);
                Serial.println(F("SmartSchedule: Queued pump off (forced)"));
            }
        }
        
        // Recurring schedule: re-arm for the next occurrence instead of deleting.
        // Also covers boot after a power outage: missed occurrences are skipped.
        if (_smart_schedule.repeat_days > 0)
        {
            uint64_t next = advanceScheduleDays(_smart_schedule.target_time,
                                                _smart_schedule.repeat_days, _timestamp_secs);
            if (next > _timestamp_secs)
            {
                _rearmSmartSchedule(next);
                Serial.println(F("SmartSchedule: Re-armed for next occurrence"));
                return;
            }
        }

        // Delete/reset the schedule completely
        _resetSmartScheduleState();
        Serial.println(F("SmartSchedule: Schedule completed and deleted"));
        return;
    }

    if (!_smart_schedule.keep_heater_on && _smart_schedule.target_temp_reached)
    {
        return;
    }

    if (!_smart_schedule.keep_heater_on &&
        _smart_schedule.calculated_start_time > 0 &&
        _timestamp_secs >= _smart_schedule.calculated_start_time &&
        waterTempC(cio) >= _smart_schedule.target_temp)
    {
        bool heater_off_queued = true;
        if (cio->cio_states.heat)
        {
            command_que_item item;
            item.xtime = 0;
            item.interval = 0;
            item.text = "";
            item.force = false;
            item.cmd = SETHEATER;
            item.val = 0;
            heater_off_queued = add_command(item);
            if (heater_off_queued)
            {
                Serial.println(F("SmartSchedule: Target temperature reached - heater off"));
            }
        }
        if (heater_off_queued)
        {
            _smart_schedule.target_temp_reached = true;
            _save_smartschedule_needed = true;
            _new_data_available = true;
        }
        return;
    }
    
    // Process temperature reading sequence if active
    if (_smart_schedule.temp_reading_state > 0)
    {
        _processAccurateTempReading();
        return;
    }
    
    // If we have a calculated start time and it has arrived, start heating
    if (_smart_schedule.calculated_start_time > 0 && 
        _timestamp_secs >= _smart_schedule.calculated_start_time)
    {
        if (!cio->cio_states.heat)
        {
            command_que_item item;
            item.xtime = 0;
            item.interval = 0;
            item.text = "";
            item.force = false;
            
            // Set target temperature from schedule
            item.cmd = SETTARGET;
            item.val = _smart_schedule.target_temp;
            add_command(item);
            
            // Turn on heater
            item.cmd = SETHEATER;
            item.val = 1;
            add_command(item);
            
            Serial.print(F("SmartSchedule: Start time reached - target: "));
            Serial.print(_smart_schedule.target_temp);
            Serial.println(F("C, heater on"));
        }
    }
    
    // Check if it's time for next temperature check and recalculation
    // IMPORTANT: Once check_completed is true (heating has started), don't do more recalculations
    // The schedule should maintain heating until target_time, not recalculate start time
    if (_smart_schedule.check_completed)
        return;
    
    if (_timestamp_secs < _smart_schedule.next_check_time)
        return;
    
    // Start accurate temperature reading sequence for recalculation
    _startAccurateTempReading();
}

/**
 * Start accurate temperature reading by turning on pump
 */
void BWC::_startAccurateTempReading()
{
    _smart_schedule.temp_reading_started_pump = false;
    
    // Turn on pump if not already on
    if (!cio->cio_states.pump)
    {
        cio->cio_toggles.pump_change = 1;
        _smart_schedule.temp_reading_started_pump = true;
    }
    
    // Set state to pump_on and start timer
    _smart_schedule.temp_reading_state = 1;
    _smart_schedule.temp_reading_timer = _timestamp_secs + 20; // Run pump for 20 seconds
    
    Serial.println(F("SmartSchedule: Starting temp reading - pump on"));
}

/**
 * Process temperature reading state machine
 */
void BWC::_processAccurateTempReading()
{
    switch (_smart_schedule.temp_reading_state)
    {
    case 1: // Pump running
        if (_timestamp_secs >= _smart_schedule.temp_reading_timer)
        {
            // 20 seconds passed, read temperature immediately
            _smart_schedule.accurate_temperature = cio->cio_states.temperature;
            _smart_schedule.temp_reading_state = 2; // Move to reading complete
            Serial.print(F("SmartSchedule: Pump run complete - temp reading: "));
            Serial.println(_smart_schedule.accurate_temperature);
        }
        break;
        
    case 2: // Reading complete
        {
            // Restore pump state only if this temp reading turned it on.
            if (_smart_schedule.temp_reading_started_pump && cio->cio_states.pump)
            {
                cio->cio_toggles.pump_change = 1; // Turn pump back off
                Serial.println(F("SmartSchedule: Restoring pump state"));
            }
            
            // Calculate heating time with accurate temperature
            float heating_time_hours = _calculateHeatingTime(
                displayTempC(cio->cio_states.unit, _smart_schedule.accurate_temperature),
                _smart_schedule.target_temp
            );
            
            _smart_schedule.last_heating_estimate = heating_time_hours;
            
            Serial.print(F("SmartSchedule: Heating estimate: "));
            Serial.print(heating_time_hours);
            Serial.println(F(" hours"));
            
            // Calculate when to start heating (with 10% buffer, minimum 1 hour)
            float buffer_hours = heating_time_hours * 0.10f; // 10% buffer
            if (buffer_hours < 1.0f) buffer_hours = 1.0f;    // Minimum 1 hour buffer
            
            uint64_t total_time_needed = (uint64_t)((heating_time_hours + buffer_hours) * 3600.0f);
            
            if (total_time_needed >= (_smart_schedule.target_time - _timestamp_secs))
            {
                // Time to start heating NOW!
                _smart_schedule.calculated_start_time = _timestamp_secs;
                _smart_schedule.check_completed = true; // No more checks needed
                
                if (!cio->cio_states.heat)
                {
                    command_que_item item;
                    item.xtime = 0;
                    item.interval = 0;
                    item.text = "";
                    item.force = false;
                    
                    // Set target temperature from schedule
                    item.cmd = SETTARGET;
                    item.val = _smart_schedule.target_temp;
                    add_command(item);
                    
                    // Turn on heater
                    item.cmd = SETHEATER;
                    item.val = 1;
                    add_command(item);
                    
                    Serial.print(F("SmartSchedule: Starting heater NOW - target: "));
                    Serial.print(_smart_schedule.target_temp);
                    Serial.println(F("C"));
                }
            }
            else
            {
                // Calculate exact start time
                _smart_schedule.calculated_start_time = _smart_schedule.target_time - total_time_needed;
                Serial.print(F("SmartSchedule: Heater starts at: "));
                Serial.println(_smart_schedule.calculated_start_time);
                
                // Calculate time until heating should start (in seconds)
                int64_t time_until_start = (int64_t)_smart_schedule.calculated_start_time - (int64_t)_timestamp_secs;
                
                // Adaptive check interval logic:
                // - If more than 24 hours until start: check again in 12 hours
                // - Otherwise: check again in half the remaining time
                uint64_t next_check_interval;
                const uint64_t SECS_24H = 24 * 60 * 60;
                const uint64_t SECS_12H = 12 * 60 * 60;
                const uint64_t MIN_CHECK_INTERVAL = 5 * 60; // Minimum 5 minutes between checks
                
                if (time_until_start > (int64_t)SECS_24H)
                {
                    // More than 24 hours away: next check in 12 hours
                    next_check_interval = SECS_12H;
                    Serial.println(F("SmartSchedule: >24h until start, next check in 12h"));
                }
                else
                {
                    // Less than 24 hours: check in half the remaining time
                    next_check_interval = (uint64_t)(time_until_start / 2);
                    if (next_check_interval < MIN_CHECK_INTERVAL)
                    {
                        next_check_interval = MIN_CHECK_INTERVAL;
                    }
                    Serial.print(F("SmartSchedule: next check in "));
                    Serial.print(next_check_interval / 60);
                    Serial.println(F(" minutes"));
                }
                
                uint64_t proposed_next_check = _timestamp_secs + next_check_interval;
                
                // Completion logic: If next check would be after heating start time,
                // mark as completed and don't schedule further checks
                if (proposed_next_check >= _smart_schedule.calculated_start_time)
                {
                    _smart_schedule.check_completed = true;
                    _smart_schedule.next_check_time = _smart_schedule.calculated_start_time;
                    Serial.println(F("SmartSchedule: Check completed - waiting"));
                }
                else
                {
                    _smart_schedule.check_completed = false;
                    _smart_schedule.next_check_time = proposed_next_check;
                }
            }
            
            // Reset temp reading state
            _smart_schedule.temp_reading_state = 0;
            _smart_schedule.temp_reading_started_pump = false;
            
            _save_smartschedule_needed = true;
            _new_data_available = true;
        }
        break;
    }
}

/**
 * Calculate heating time with current and target temperatures
 * @param current_temp Current temperature in Celsius
 * @param target_temp Target temperature in Celsius
 * @return Estimated heating time in hours
 */
float BWC::_calculateHeatingTime(float current_temp_c, float target_temp_c)
{
    if (current_temp_c >= target_temp_c)
        return 0.0f; // Already at or above target

    float hoursRemaining = heatingHours(current_temp_c, target_temp_c,
                                        cio->getPower().HEATERPOWER, _heat_loss,
                                        _ambient_temp, _pool_capacity);
    return hoursRemaining >= 0.0f ? hoursRemaining : 999.0f;
}

/**
 * Load smart schedule from persistent storage
 */
void BWC::_loadSmartSchedule()
{
    File file = LittleFS.open("/smartschedule.json", "r");
    if (!file)
    {
        Serial.println(F("FS: /smartschedule.json not found, using defaults"));
        return;
    }
    
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    
    if (error)
    {
        Serial.println(F("SmartSchedule: Failed to deserialize"));
        return;
    }
    
    _smart_schedule.active = doc[F("ACTIVE")] | false;
    _smart_schedule.target_time = doc[F("TARGETTIME")] | 0;
    _smart_schedule.target_temp = doc[F("TARGETTEMP")] | 37;
    _smart_schedule.keep_heater_on = doc[F("KEEPON")] | false;
    _smart_schedule.repeat_days = doc[F("REPEATDAYS")] | 0;
    _smart_schedule.calculated_start_time = doc[F("STARTTIME")] | 0;
    _smart_schedule.next_check_time = doc[F("NEXTCHECK")] | 0;
    _smart_schedule.last_heating_estimate = doc[F("ESTIMATE")] | 0.0f;
    _smart_schedule.accurate_temperature = doc[F("ACCURATETEMP")] | 20;
    _smart_schedule.check_completed = doc[F("CHECKCOMPLETED")] | false;
    
    // Reset temp reading state (don't persist this)
    _smart_schedule.temp_reading_state = 0;
    _smart_schedule.temp_reading_started_pump = false;
    _smart_schedule.target_temp_reached = false;
    
    // If target time has passed, deactivate. A recurring schedule stays active:
    // the completion path in _handleSmartSchedule() re-arms it on the next loop
    // (and handles heater/pump off first), so a power outage over the target
    // time doesn't kill the recurrence.
    if (_smart_schedule.active && _smart_schedule.target_time <= _timestamp_secs &&
        _smart_schedule.repeat_days == 0)
    {
        _smart_schedule.active = false;
    }
    
    Serial.println(F("SmartSchedule: Loaded from file"));
}

/**
 * Save smart schedule to persistent storage
 */
void BWC::_saveSmartSchedule()
{
#ifdef ESP8266
    ESP.wdtFeed();
#endif
    
    _save_smartschedule_needed = false;
    
    File file = LittleFS.open("/smartschedule.json", "w");
    if (!file)
    {
        Serial.println(F("SmartSchedule: Failed to save"));
        return;
    }
    
    StaticJsonDocument<512> doc;
    
    doc[F("ACTIVE")] = _smart_schedule.active;
    doc[F("TARGETTIME")] = _smart_schedule.target_time;
    doc[F("TARGETTEMP")] = _smart_schedule.target_temp;
    doc[F("KEEPON")] = _smart_schedule.keep_heater_on;
    doc[F("REPEATDAYS")] = _smart_schedule.repeat_days;
    doc[F("STARTTIME")] = _smart_schedule.calculated_start_time;
    doc[F("NEXTCHECK")] = _smart_schedule.next_check_time;
    doc[F("ESTIMATE")] = _smart_schedule.last_heating_estimate;
    doc[F("ACCURATETEMP")] = _smart_schedule.accurate_temperature;
    doc[F("CHECKCOMPLETED")] = _smart_schedule.check_completed;
    
    if (serializeJson(doc, file) == 0)
    {
        Serial.println(F("SmartSchedule: Failed to serialize"));
    }
    
    file.close();
    Serial.println(F("SmartSchedule: Saved to file"));
}
