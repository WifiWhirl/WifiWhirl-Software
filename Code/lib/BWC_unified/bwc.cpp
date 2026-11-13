#include "bwc_internal.h"

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

uint64_t nextRunAfter(uint64_t xtime, uint32_t interval, uint64_t now, bool allow_now)
{
    uint64_t k = now >= xtime ? (now - xtime) / interval + (allow_now && (now - xtime) % interval == 0 ? 0 : 1) : 0;
    if (interval % 86400 != 0 || k == 0)
        return xtime + k * interval;
    time_t t = (time_t)xtime;
    struct tm tm;
    localtime_r(&t, &tm);
    int days = interval / 86400;
    tm.tm_mday += (int)(k * days);
    tm.tm_isdst = -1; // let mktime pick DST for the new date
    uint64_t next = (uint64_t)mktime(&tm);
    while (next < now || (next == now && !allow_now)) // the DST hour can leave it behind
    {
        tm.tm_mday += days;
        tm.tm_isdst = -1;
        next = (uint64_t)mktime(&tm);
    }
    return next;
}

namespace
{
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
    // No POWER press here: it was queued before the first frame, with
    // the power state still "off", so every boot switched a running pool off
    // (unlock() switched it on again later) or woke a pool in standby. A pool
    // that ran before a power cut is switched on by the boot restore.
    begin();
}

void BWC::begin()
{
    // _save_melody("melody.bin");
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
    if (cio->cio_states.target)
        _last_target = cio->cio_states.target;
    // Boot restore once the CIO has parsed a frame and learned the target
    // (6-wire boards read it only after their first UP self-press). Fall back
    // after 60 s so pump/heater still get restored if the panel stays dark.
    // After a power cut (was on, now in standby) the pump never shows a target:
    // restore at the first frame instead of after 60 s, like the old boot-time
    // POWER press.
    if (_restore_states_pending &&
        ((cio->good_packets_count > 0 && cio->cio_states.target != 0) ||
         (_saved_power && cio->good_packets_count > 0 && !cio->cio_states.power) ||
         millis() > 60000))
    {
        _restore_states_pending = false;
        _restoreStates();
    }
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
    else if (_jet_countdown)
    {
        char cd[4];
        if (_jetCountdownText(cd))
        {
            dsp->dsp_states.char1 = cd[0];
            dsp->dsp_states.char2 = cd[1];
            dsp->dsp_states.char3 = cd[2];
        }
    }
    
    dsp->setRawPayload(cio->getRawPayload());
    dsp->standby_original = _standby_original;
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
    _soundEvents();

    if (_dsp_tgt_used)
        cio->cio_toggles.target = cio->cio_states.target;
    else
        cio->cio_toggles.target = _web_target;

    if (dsp->dsp_toggles.unit_change)
    {
        cio->cio_toggles.target = cio->cio_states.unit ? round(C2F(cio->cio_toggles.target)) : round(F2C(cio->cio_toggles.target));
        // keep the web target in the new unit too: converting it for this one
        // pass only made the next pass walk the target down to the minimum
        if (!_dsp_tgt_used)
            _web_target = cio->cio_toggles.target;
    }

    /*Host mode: cap target set from the physical panel. Downward correction is
      handled by CIO handleToggles pressing DOWN until target matches.*/
    if (_host_mode && _dsp_tgt_used)
    {
        uint8_t max_t = cio->cio_states.unit ? _host_max_target_c : (uint8_t)round(C2F(_host_max_target_c));
        if (cio->cio_toggles.target > max_t)
            cio->cio_toggles.target = max_t;
        if (cio->cio_states.target >= max_t)
        {
            cio->cio_toggles.up_pressed = 0;
            if (cio->cio_toggles.held_button == UP)
                cio->cio_toggles.held_button = NOBTN;
        }
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
    if (_save_cmdq_needed && (int32_t)(millis() - _save_cmdq_retry_at) >= 0)
        _saveCommandQueue();
    // Not before the boot restore has read states.txt: the first frames change
    // every state from the constructor defaults, and saving then replaced the
    // file with "target 0, pump/heater as the board is now", so the restore
    // never restored anything.
    // Not while the target is unknown (0) either: a pool booted in standby is
    // never asked for it, and the save replaced the real one with 0.
    if (_save_states_needed && !_restore_states_pending && cio->cio_states.target != 0)
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
    // Click only on the press itself; UP/DOWN once per press and once per tap
    // the module sends while the key is held, so it beeps evenly even when the
    // pump skips a step.
    Buttons btn = dsp->dsp_toggles.pressed_button;
    bool pressed = btn != _prev_sound_button;
    _prev_sound_button = btn;
    static uint32_t prev_taps = 0;
    bool step = pressed || cio->held_taps != prev_taps;
    prev_taps = cio->held_taps;
    if (!dsp->dsp_states.locked && dsp->dsp_states.power)
    {
        switch (btn)
        {
        case UP:
            if (step && dsp->EnabledButtons[UP])
                playSound(SND_UP, false);
            _dsp_tgt_used = true;
            break;
        case DOWN:
            if (step && dsp->EnabledButtons[DOWN])
                playSound(SND_DOWN, false);
            _dsp_tgt_used = true;
            break;
        case TIMER:
            if (pressed && dsp->EnabledButtons[TIMER])
                playSound(SND_CLICK, false);
            break;
        default:

            break;
        }
    }

    if (
        dsp->dsp_toggles.bubbles_change || dsp->dsp_toggles.heat_change ||
        dsp->dsp_toggles.jets_change || dsp->dsp_toggles.power_change ||
        dsp->dsp_toggles.pump_change || dsp->dsp_toggles.unit_change)
        playSound(SND_CLICK, false);
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
// Used with std::stable_sort: commands due at the same time (a command_batch,
// the boot restore) must keep their order, e.g. heater off before pump off.
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
        // RESETQ is not an automation: "clear queue" must work while the queue is
        // off, instead of waiting and wiping newer entries later
        if (!run_scheduled && _command_que[i].scheduled && _command_que[i].cmd != RESETQ)
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
    // a heater-off sent just before (same batch / automation) is still
    // on its way to the pump, and the guard below would drop this pump-off.
    // Wait for it (before the interval re-queue, which must run only once).
    if (_command_que[0].cmd == SETPUMP && _command_que[0].val == 0 && cio->cio_states.heat &&
        (int32_t)(_heater_off_pending_until - millis()) > 0)
        return;
    // If interval > 0 then append to commandQ with updated xtime.
    if (_command_que[0].interval > 0)
    {
        // Advance to the next future fire time in one step. A stale xtime with a
        // small interval would otherwise spin this loop for billions of iterations
        // after the NTP clock jump -> watchdog reboot. (Same fix as loadCommandQueue.)
        // The copy must lie strictly in the future: with "xtime < now" a
        // command due this very second was re-queued unchanged and fired again on
        // every loop() pass until the second ticked over.
        uint64_t now = (uint64_t)time(nullptr);
        _command_que[0].xtime = nextRunAfter(_command_que[0].xtime, _command_que[0].interval,
                                             now >= _command_que[0].xtime ? now : _command_que[0].xtime, false);
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

    if (!(cmd == SETBEEP && txt.startsWith(F("mel:"))))
        dsp->text += String(" ") + txt;
    switch (cmd)
    {
    // VALUE 0/1 is the wanted state (schedule "off at 23:00", HA switch),
    // so an already-off unit is not switched on. Any other VALUE toggles.
    case TOGGLEPWR:
    {
        if (val < 0 || val > 1 || val != cio->cio_states.power)
            cio->cio_toggles.power_change = 1;
        break;
    }
    case TOGGLELCK:
    {
        if (val < 0 || val > 1 || val != cio->cio_states.locked)
            cio->cio_toggles.locked_pressed = 1;
        break;
    }
    case SETTARGET:
    {
        bool restored = _restore_target_pending; // see below
        _restore_target_pending = false;
        // the pump's range: anything else made the CIO press DOWN/UP forever
        if (!((val >= 20 && val <= 40) || (val >= 68 && val <= 104)))
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
        
        // Update smart schedule target temp if active (store in Celsius).
        // Not for the boot restore's SETTARGET: that is the tub's old
        // target from states.txt, not a user decision, and would replace the
        // schedule's target after every reboot. Only plausible targets (20-40 C).
        uint8_t new_temp_c = implied_unit_is_celsius ? val : round(F2C(val));
        if (_smart_schedule.active && !restored && new_temp_c >= 20 && new_temp_c <= 40)
        {
            uint8_t temp_celsius = new_temp_c;
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
        // an unknown target (0) is no web target: the walk then pressed DOWN
        // forever, down to the pump's 20 °C minimum
        if (cio->cio_toggles.target == 0)
            break;
        _dsp_tgt_used = false;
        _web_target = cio->cio_toggles.target;
        break;
    case SETBUBBLES:
        if (val != cio->cio_states.bubbles)
            cio->cio_toggles.bubbles_change = 1;
        break;
    case SETHEATER:
    {
        bool own_off = _schedule_heater_off_pending; // see below
        _schedule_heater_off_pending = false;
        if (val != cio->cio_states.heat)
        {
            cio->cio_toggles.heat_change = 1;
            if (val == 0)
                _heater_off_pending_until = millis() + 10000; // see the SETPUMP guard
            else
                _heater_off_pending_until = millis();
            // If user manually turns off heater while smart schedule is active and heating,
            // cancel the schedule to prevent it from overriding the user's decision
            // (not when the schedule itself switched it off at the target)
            if (val == 0 && !own_off && _smart_schedule.active && _smart_schedule.calculated_start_time > 0 &&
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
    }
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
        // Cyanuric acid value * 10 (e.g. 300 = 30.0 mg/L), valid range 0-2000 (0.0-200.0 mg/L,
        // what the dashboard offers: higher readings are real)
        if (val >= 0 && val <= 2000)
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
    case ADDPH:
    case ADDTA:
        if (val != 0 && val >= -2000 && val <= 2000)
        {
            (cmd == ADDPH ? _dose_ph : _dose_ta) = (int16_t)val;
            (cmd == ADDPH ? _dose_ph_ts : _dose_ta_ts) = _timestamp_secs;
            _save_settings_needed = true;
            _new_data_available = true;
        }
        break;
    case ADDCL:
    case ADDCLTAB:
        if (val > 0 && val <= (cmd == ADDCL ? 2000 : 50))
        {
            _dose_cl = (uint16_t)val;
            _dose_cl_tab = cmd == ADDCLTAB;
            _dose_cl_ts = _timestamp_secs;
            // chlorine added = the "add chlorine" timer starts over (tester);
            // the web UI shows it in the water card now
            _cl_timestamp_s = _timestamp_secs;
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
            std::stable_sort(_command_que.begin(), _command_que.end(), _compare_command);
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
    std::stable_sort(_command_que.begin(), _command_que.end(), _compare_command);
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

    // Sound when the lock flips after a press on the panel. A short tap is already
    // released by then (the module holds LOCK until the lock flips), so
    // remember the press for a few seconds instead of asking for it to be held.
    if (dsp->dsp_toggles.pressed_button == LOCK)
        _lock_press_ms = millis() | 1;
    if (cio->cio_states.locked != _prev_cio_states.locked)
    {
        if (_lock_press_ms && millis() - _lock_press_ms < 8000 && dsp->EnabledButtons[LOCK])
            playSound(cio->cio_states.locked ? SND_CONFIRM : SND_CLICK);
        _lock_press_ms = 0;
    }

    if (
        cio->cio_states.power != _prev_cio_states.power ||
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

void BWC::setCloudOn(bool on)
{
    _cloud_on = on;
    if (!on && _weather)
    {
        _weather = false;
        _weather_auto_off = true;
        _save_settings_needed = true;
        _new_data_available = true;
    }
}

void BWC::cloudWeatherArrived()
{
    if (!_weather_auto_off)
        return;
    _weather = true;
    _weather_auto_off = false;
    _save_settings_needed = true;
    _new_data_available = true;
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
    std::stable_sort(_command_que.begin(), _command_que.end(), _compare_command);
    return true;
}

bool BWC::add_live_command(command_que_item item)
{
    if (_hlcal.active && !item.scheduled && item.interval == 0 &&
        item.xtime <= (uint64_t)_timestamp_secs + 60)
        return false;
    return add_command(item);
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
    std::stable_sort(_command_que.begin(), _command_que.end(), _compare_command);
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

// Does the entry at index still hold this command? The UI addresses entries by
// position, which shifts when one runs, repeats or is re-sorted.
bool BWC::command_at_matches(uint8_t index, uint8_t cmd, uint64_t xtime)
{
    return index < _command_que.size() && _command_que[index].cmd == cmd && _command_que[index].xtime == xtime;
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

/*TODO:*/
uint8_t BWC::getState(int state)
{
    // return cio->getState(state);
    return 0;
}

void BWC::getButtonName(String &rtn)
{
    // Pretty names, same order as enum Buttons. Here, not in enums.h: a
    // "const String" array in a header became one 132-byte copy per .cpp,
    // 38 copies = 5 KB of RAM the MQTT socket was short of.
    static const char *const ButtonNames[] = {"NOBTN", "LOCK", "TIMER", "BUBBLES", "UNIT", "HEAT",
                                              "PUMP", "DOWN", "UP", "POWER", "HYDROJETS"};
    rtn = ButtonNames[dsp->dsp_toggles.pressed_button];
}

Buttons BWC::getButton()
{
    return dsp->dsp_toggles.pressed_button;
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
    // Clear first: tzset() skips parsing when TZ did not change, and
    // configTime() rewrites the rules behind TZ's back (module ran in UTC).
    unsetenv("TZ");
    tzset();
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
// "A12" / "H12" = 12 minutes of AirJet / HydroJet left (the panel's 'a'
// glyph is a capital A). Shown for 8 s right at the start and then every
// minute after it (counted from the switch-on, not the clock: the tester saw
// up to 50 s of delay); in the last minute the seconds count down without a
// break. Both running and both due: one per minute, alternating.
bool BWC::_jetCountdownText(char *out)
{
    uint32_t now = millis();
    uint32_t on_ms[2] = {now - _bubbles_change_timestamp_ms, now - _jets_change_timestamp_ms};
    int32_t rem[2] = {-1, -1};
    if (cio->cio_states.bubbles)
        rem[0] = (int32_t)(_airjet_timeout_minutes * 60UL) - (int32_t)(on_ms[0] / 1000);
    if (cio->getHasjets() && cio->cio_states.jets)
        rem[1] = (int32_t)(_hydrojet_timeout_minutes * 60UL) - (int32_t)(on_ms[1] / 1000);
    static const char tag[2] = {'a', 'H'};
    for (int i = 0; i < 2; i++)
        // the last minute counts down from 59 right away, no "A 1" first
        if (rem[i] >= 0 && rem[i] <= 60)
        {
            snprintf(out, 4, "%c%02d", tag[i], (int)(rem[i] > 59 ? 59 : rem[i]));
            return true;
        }
    bool due[2];
    for (int i = 0; i < 2; i++)
        due[i] = rem[i] >= 0 && (on_ms[i] / 1000) % 60 < 8;
    int i = due[0] && due[1] ? (int)((now / 60000) % 2) : due[0] ? 0 : due[1] ? 1 : -1;
    if (i < 0)
        return false;
    snprintf(out, 4, "%c%2d", tag[i], (int)((rem[i] + 59) / 60));
    return true;
}

void BWC::_checkJetTimeouts()
{
    uint32_t now = millis();
    
    // Only once it was on in the previous pass too: this runs before
    // _handleStateChanges() stamps the switch-on, so on the first pass the
    // stamp is still the last switch-off and a jet switched on later than the
    // timeout after it went off was switched off again at once.
    // Check airjet timeout (only if configured below hardware default of 30 min)
    if (cio->cio_states.bubbles && _prev_cio_states.bubbles && _airjet_timeout_minutes < 30)
    {
        uint32_t elapsed_ms = now - _bubbles_change_timestamp_ms;
        uint32_t timeout_ms = (uint32_t)_airjet_timeout_minutes * 60UL * 1000UL;
        // once per timeout, not on every loop() until the pump reports it
        if (elapsed_ms >= timeout_ms && (int32_t)(now - _airjet_off_queued_until) >= 0)
        {
            _airjet_off_queued_until = now + 10000;
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
    
    // Reminder sound one minute before a jet goes off, by WifiWhirl or by the pump itself
    if (cio->cio_states.bubbles && _prev_cio_states.bubbles)
    {
        uint32_t limit_ms = (uint32_t)(_airjet_timeout_minutes < 30 ? _airjet_timeout_minutes : 30) * 60000UL;
        if (!_airjet_warned && limit_ms > 60000UL && now - _bubbles_change_timestamp_ms >= limit_ms - 60000UL)
        {
            _airjet_warned = true;
            playSound(SND_REMINDER);
        }
    }
    else
        _airjet_warned = false;
    if (cio->cio_states.jets && _prev_cio_states.jets)
    {
        uint32_t limit_ms = (uint32_t)(_hydrojet_timeout_minutes < 60 ? _hydrojet_timeout_minutes : 60) * 60000UL;
        if (!_hydrojet_warned && limit_ms > 60000UL && now - _jets_change_timestamp_ms >= limit_ms - 60000UL)
        {
            _hydrojet_warned = true;
            playSound(SND_REMINDER);
        }
    }
    else
        _hydrojet_warned = false;

    // Check hydrojet timeout (only if configured below hardware default of 60 min)
    if (cio->cio_states.jets && _prev_cio_states.jets && _hydrojet_timeout_minutes < 60)
    {
        uint32_t elapsed_ms = now - _jets_change_timestamp_ms;
        uint32_t timeout_ms = (uint32_t)_hydrojet_timeout_minutes * 60UL * 1000UL;
        if (elapsed_ms >= timeout_ms && (int32_t)(now - _hydrojet_off_queued_until) >= 0)
        {
            _hydrojet_off_queued_until = now + 10000;
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
        // capped: a stalled loop() (end of setup, HA discovery) holds the note a bit
        // longer instead of skipping the whole melody in one step
        _note_duration += (int)(elapsedtime_ms < 50 ? elapsedtime_ms : 50);
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
