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
    pins[2] = 8;         // CIO CS: not GPIO3, a strapping pin on the ESP32-S3
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
    if (_save_states_needed && !_restore_states_pending && cio->cio_states.target != 0 &&
        (int32_t)(millis() - _save_states_at) >= 0)
        _saveStates();
    if (_save_smartschedule_needed)
        _saveSmartSchedule();
    _handleStateChanges();
    // logstates();
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
        cio->cio_states.heat != _prev_cio_states.heat)
    {
        _save_states_needed = true;
        _save_states_at = millis(); // the power-cut restore needs these right away
    }
    else if (cio->cio_states.target != _prev_cio_states.target && !_save_states_needed)
    {
        // a held UP/DOWN steps every ~0.5 s and each step was a flash write
        // (10-40 ms blocking): save the target at most every 2 s
        _save_states_needed = true;
        _save_states_at = millis() + 2000;
    }

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
