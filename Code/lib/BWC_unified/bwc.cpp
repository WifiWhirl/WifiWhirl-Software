#include "bwc.h"
#include "util.h"
#include "pitches.h"
#include <algorithm>

BWC::BWC()
{
    // Initialize variables

    _dsp_brightness = 7;
    _cl_timestamp_s = time(nullptr);
    _filter_timestamp_s = time(nullptr);
    _fc_timestamp_s = time(nullptr);
    _wc_timestamp_s = time(nullptr);
    _uptime = 0;
    _pumptime = 0;
    _heatingtime = 0;
    _airtime = 0;
    _jettime = 0;
    _price = 0.35;
    _cl_interval = 14;
    _filter_interval = 30;
    _fc_interval = 60;
    _wc_interval = 90;
    _audio_enabled = true;
    _ambient_temp = 20;
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

    if (!_loadHardware(ciomodel, dspmodel, pins))
    {
        pins[0] = D1;
        pins[1] = D2;
        pins[2] = D3;
        pins[3] = D4;
        pins[4] = D5;
        pins[5] = D6;
        pins[6] = D7;
        pins[7] = D8;
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
    default:
        cio = new CIO_2021;
        break;
    }
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
    default:
        dsp = new DSP_2021;
        break;
    }
    cio->setup(pins[0], pins[1], pins[2]);
    dsp->setup(pins[3], pins[4], pins[5], pins[6]);
    tempSensorPin = pins[7];
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

    _next_notification_time = _notification_time;
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

    /*following method will change target temp and set _dsp_tgt_used to false if target temp is changed*/
    _handleCommandQ();
    /*If new target was not set above, use whatever the cio says*/
    cio->setRawPayload(dsp->getRawPayload());
    cio->handleToggles();

    if (_save_settings_needed)
        saveSettings();
    if (_save_cmdq_needed)
        _saveCommandQueue();
    if (_save_states_needed)
        _saveStates();
    _handleNotification();
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

    File file = LittleFS.open("log.txt", "a");
    if (!file)
    {
        // Serial.println(F("Failed to save states.txt"));
        return;
    }
    if (++writes > 1000)
        return;
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
        _override_dsp_brt_timer = 5000;
    if (_override_dsp_brt_timer > 0)
    {
        dsp->dsp_states.brightness = _dsp_brightness + 1;
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

void BWC::_handleNotification()
{
    /* user don't want a notification*/
    if (!_notify)
        return;
    /* there is no upcoming command*/
    if (_command_que.size() == 0)
    {
        _next_notification_time = _notification_time;
        return;
    }
    /* not the time yet*/
    if ((int64_t)_command_que[0].xtime - (int64_t)_timestamp_secs > (int64_t)_next_notification_time)
        return;
    /* only _notify for these commands*/
    if (!(_command_que[0].cmd == SETBUBBLES || _command_que[0].cmd == SETHEATER || _command_que[0].cmd == SETJETS || _command_que[0].cmd == SETPUMP))
        return;

    if (_audio_enabled)
        _sweepup();
    dsp->text += "  --" + String(_next_notification_time) + "--";
    // dsp->dsp_states.text = "i-i-";
    if (_next_notification_time <= 2)
        _next_notification_time = -10; // postpone "alarm" until after the command xtime (will be reset on command execution)
    else
        _next_notification_time /= 2;
}

void BWC::_handleCommandQ()
{
    if (_command_que.size() < 1)
        return;
    /* time for next command? */
    if (_timestamp_secs < _command_que[0].xtime)
        return;
    // If interval > 0 then append to commandQ with updated xtime.
    if (_command_que[0].interval > 0)
    {
        while (_command_que[0].xtime < (uint64_t)time(nullptr))
            _command_que[0].xtime += _command_que[0].interval;
        _command_que.push_back(_command_que[0]);
    }

    // Do not turn off pump if Heater is running
    // Fixes situations when you want to heat your pool but your daily pump cycle is running and turning off your heater
    if (_command_que[0].cmd == SETPUMP && cio->cio_states.heat)
    {
        _command_que.erase(_command_que.begin());
        _next_notification_time = _notification_time; // reset alarm time
        _save_cmdq_needed = true;
        return;
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
            cio->cio_toggles.heat_change = 1;
        break;
    case SETPUMP:
        if (val != cio->cio_states.pump)
            cio->cio_toggles.pump_change = 1;
        break;
    case RESETQ:
        _command_que.clear();
        _save_cmdq_needed = true;
        _next_notification_time = _notification_time; // reset alarm time
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
    case SETJETS:
        if (val != cio->cio_states.jets)
            cio->cio_toggles.jets_change = 1;
        break;
    case SETBRIGHTNESS:
        _dsp_brightness = val;
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
        _energy_daily_Ws = 0;
        _new_data_available = true;
        break;
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
    _next_notification_time = _notification_time; // reset alarm time
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
    int tgtTemp = cio->cio_states.target; // Target temperature (set by user)
    if (!cio->cio_states.unit)
        tgtTemp = F2C(tgtTemp); // Check if unit is set to fahrenheit and convert to c for calculation
    if (cio->cio_states.temperature > tgtTemp)
        return -2; // Pool is ready, target is reached

    const int heaterPwr = 2000;                  // Heater power in watts
    const float heatLoss = 4.85f;                // Heat loss per Kelvin per hour
    const float tcpH2O = 1.163f;                 // Specific heat capacity of water in Wh/(kg*K)
    int ambTemp = _ambient_temp;                 // Ambient temperature (set by user or automation)
    float curTemp = cio->cio_states.temperature; // Current temperature
    int poolCapacity = _pool_capacity;           // Water capacity in liters (set by user)

    // calculate efficiency
    float avgTemp = (tgtTemp + curTemp) / 2;
    float heaterEfficiency = 0.99f - ((avgTemp - ambTemp) * 0.005f);
    heaterEfficiency = std::max(heaterEfficiency, 0.1f);

    // net heating power calculation
    float netHeatingPower = (heaterPwr * heaterEfficiency) - (heatLoss * (avgTemp - ambTemp));
    if (netHeatingPower <= 0)
    {
        return -1;
    }

    // Calculate remaining hours
    float totalEnergy = poolCapacity * tcpH2O * (tgtTemp - curTemp);
    float hoursRemaining = totalEnergy / netHeatingPower;

    if (hoursRemaining >= 0)
        return hoursRemaining;
    else
        return -1; // Never
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
    _save_cmdq_needed = true;
    if (command_item.cmd == SETREADY)
    {
        command_item.val = (int64_t)command_item.xtime; // Use val field to store the time to be ready
        command_item.xtime = 0;                         // And start checking now
        command_item.interval = 0;
    }
    // add parameters to _command_que[rows][parameter columns] and sort the array on xtime.
    _command_que.push_back(command_item);
    std::sort(_command_que.begin(), _command_que.end(), _compare_command);
    return true;
}

bool BWC::edit_command(uint8_t index, command_que_item command_item)
{
    if (index > _command_que.size())
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

    // Set the values in the document
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
    doc[F("ERR")] = cio->cio_states.error;
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

    // Serialize JSON to string
    if (serializeJson(doc, rtn) == 0)
    {
        rtn = F("{\"error\": \"Failed to serialize states\"}");
    }
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

    // Set the values in the document
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
    doc[F("COST")] = _energy_total_kWh * _price;
    doc[F("CLINT")] = _cl_interval;
    doc[F("FINT")] = _filter_interval;
    doc[F("FCINT")] = _fc_interval;
    doc[F("WCINT")] = _wc_interval;
    doc[F("KWH")] = _energy_total_kWh;
    doc[F("KWHD")] = _energy_daily_Ws / 3600000.0; // Ws -> kWh
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

    // Serialize JSON to string
    if (serializeJson(doc, rtn) == 0)
    {
        rtn = F("{\"error\": \"Failed to serialize times\"}");
    }
}

void BWC::getJSONSettings(String &rtn)
{
// Allocate a temporary JsonDocument
// Don't forget to change the capacity to match your requirements.
// Use arduinojson.org/assistant to compute the capacity.
// feed the dog
#ifdef ESP8266
    ESP.wdtFeed();
#endif
    DynamicJsonDocument doc(1024);

    // Set the values in the document
    doc[F("CONTENT")] = F("SETTINGS");
    doc[F("PRICE")] = _price;
    doc[F("CLINT")] = _cl_interval;
    doc[F("FINT")] = _filter_interval;
    doc[F("FCINT")] = _fc_interval;
    doc[F("WCINT")] = _wc_interval;
    doc[F("AUDIO")] = _audio_enabled;
#ifdef ESP8266
    doc[F("REBOOTINFO")] = ESP.getResetReason();
#endif
    doc[F("REBOOTTIME")] = reboot_time_t;
    doc[F("MODEL")] = cio->getModel();
    doc[F("NOTIFY")] = _notify;
    doc[F("NOTIFTIME")] = _notification_time;
    doc[F("PLZ")] = _plz;
    doc[F("WEATHER")] = _weather;
    doc[F("HASJETS")] = cio->getHasjets();
    doc[F("POOLCAP")] = _pool_capacity;
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

String BWC::getJSONCommandQueue()
{
// feed the dog
#ifdef ESP8266
    ESP.wdtFeed();
#endif
    DynamicJsonDocument doc(1024);
    // Set the values in the document
    doc[F("LEN")] = _command_que.size();
    for (unsigned int i = 0; i < _command_que.size(); i++)
    {
        doc[F("CMD")][i] = _command_que[i].cmd;
        doc[F("VALUE")][i] = _command_que[i].val;
        doc[F("XTIME")][i] = _command_que[i].xtime;
        doc[F("INTERVAL")][i] = _command_que[i].interval;
        doc[F("TXT")][i] = _command_que[i].text;
    }

    // Serialize JSON to file
    String jsonmsg;
    if (serializeJson(doc, jsonmsg) == 0)
    {
        jsonmsg = F("{\"error\": \"Failed to serialize cmdq\"}");
    }
    return jsonmsg;
}

/*TODO:*/
uint8_t BWC::getState(int state)
{
    // return cio->getState(state);
    return 0;
}

void BWC::getButtonName(String &rtn)
{
    rtn = ButtonNames[dsp->dsp_toggles.pressed_button];
}

Buttons BWC::getButton()
{
    return dsp->dsp_toggles.pressed_button;
}

void BWC::setJSONSettings(const String &message)
{
    // feed the dog
    //  ESP.wdtFeed();
    DynamicJsonDocument doc(1024);

    // Deserialize the JSON document
    DeserializationError error = deserializeJson(doc, message);
    if (error)
    {
        // Serial.println(F("Failed to read config file"));
        return;
    }

    // Copy values from the JsonDocument to the variables
    _price = doc[F("PRICE")];
    _cl_interval = doc[F("CLINT")];
    _filter_interval = doc[F("FINT")];
    _fc_interval = doc[F("FCINT")];
    _wc_interval = doc[F("WCINT")];
    _audio_enabled = doc[F("AUDIO")];
    _notify = doc[F("NOTIFY")];
    _notification_time = doc[F("NOTIFTIME")];
    _plz = doc[F("PLZ")].as<String>();
    _weather = doc[F("WEATHER")];
    _pool_capacity = doc[F("POOLCAP")];
    dsp->EnabledButtons[LOCK] = doc[F("LCK")];
    dsp->EnabledButtons[TIMER] = doc[F("TMR")];
    dsp->EnabledButtons[BUBBLES] = doc[F("AIR")];
    dsp->EnabledButtons[UNIT] = doc[F("UNT")];
    dsp->EnabledButtons[HEAT] = doc[F("HTR")];
    dsp->EnabledButtons[PUMP] = doc[F("FLT")];
    dsp->EnabledButtons[DOWN] = doc[F("DN")];
    dsp->EnabledButtons[UP] = doc[F("UP")];
    dsp->EnabledButtons[POWER] = doc[F("PWR")];
    dsp->EnabledButtons[HYDROJETS] = doc[F("HJT")];
    saveSettings();
}

bool BWC::newData()
{
    bool result = _new_data_available;
    _new_data_available = false;
    return result;
}

void BWC::_updateTimes()
{
    uint32_t now = millis();
    static uint32_t prevtime = now;
    int elapsedtime_ms = now - prevtime;
    prevtime = now;
    // //(some of) these age-counters resets when the state changes
    // for(unsigned int i = 0; i < cio->getSizeofStates(); i++)
    // {
    //     cio->setStateAge(i, cio->getStateAge(i) + elapsedtime_ms);
    // }

    if (elapsedtime_ms < 0)
        return; // millis() rollover every 24,8 days
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
        _heatingtime_ms = 0;
        _pumptime_ms = 0;
        _airtime_ms = 0;
        _jettime_ms = 0;
        _uptime_ms = 0;
    }

    if (_override_dsp_brt_timer > 0)
        _override_dsp_brt_timer -= elapsedtime_ms; // counts down to or below zero

    // watts, kWh today, total kWh
    float heatingEnergy = (_heatingtime + _heatingtime_ms / 1000) / 3600.0 * cio->getPower().HEATERPOWER;
    float pumpEnergy = (_pumptime + _pumptime_ms / 1000) / 3600.0 * cio->getPower().PUMPPOWER;
    float airEnergy = (_airtime + _airtime_ms / 1000) / 3600.0 * cio->getPower().AIRPOWER;
    float idleEnergy = (_uptime + _uptime_ms / 1000) / 3600.0 * cio->getPower().IDLEPOWER;
    float jetEnergy = (_jettime + _jettime_ms / 1000) / 3600.0 * cio->getPower().JETPOWER;
    _energy_total_kWh = (heatingEnergy + pumpEnergy + airEnergy + idleEnergy + jetEnergy) / 1000; // Wh -> kWh
    _energy_power_W = cio->cio_states.heatred * cio->getPower().HEATERPOWER;
    _energy_power_W += cio->cio_states.pump * cio->getPower().PUMPPOWER;
    _energy_power_W += cio->cio_states.bubbles * cio->getPower().AIRPOWER;
    _energy_power_W += cio->getPower().IDLEPOWER;
    _energy_power_W += cio->cio_states.jets * cio->getPower().JETPOWER;

    _energy_daily_Ws += elapsedtime_ms * _energy_power_W / 1000.0;

    if (_notes.size())
    {
        dsp->audiofrequency = _notes.back().frequency_hz;
        _note_duration += elapsedtime_ms;
        if (_note_duration >= _notes.back().duration_ms)
        {
            _note_duration -= _notes.back().duration_ms;
            _notes.pop_back();
            _note_duration = 0;
        }
    }
    else
    {
        dsp->audiofrequency = 0;
    }
}

/*          */
/* LOADERS  */
/*          */

bool BWC::_loadHardware(Models &cioNo, Models &dspNo, int pins[])
{
    File file = LittleFS.open("/hwcfg.json", "r");
    if (!file)
    {
        // Serial.println(F("Failed to open hwcfg.json"));
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
    cioNo = doc[F("cio")];
    dspNo = doc[F("dsp")];

    if (doc[F("hasTempSensor")].as<int>() == 1)
    {
        hasTempSensor = true;
    }

    String pcbname = doc[F("pcb")].as<String>();
// int pins[7];
#ifdef ESP8266
    int DtoGPIO[] = {D0, D1, D2, D3, D4, D5, D6, D7, D8};
#endif
    for (int i = 0; i < 8; i++)
    {
        pins[i] = doc[F("pins")][i];
#ifdef ESP8266
        pins[i] = DtoGPIO[pins[i]];
#endif
    }
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
        // Serial.println(F("Failed to load settings.json"));
        return;
    }
    DynamicJsonDocument doc(1024);

    // Deserialize the JSON document
    DeserializationError error = deserializeJson(doc, file);
    if (error)
    {
        // Serial.println(F("Failed to deser. settings.json"));
        file.close();
        return;
    }

    // Copy values from the JsonDocument to the variables
    _cl_timestamp_s = doc[F("CLTIME")];
    _filter_timestamp_s = doc[F("FTIME")];
    _fc_timestamp_s = doc[F("FCTIME")];
    _wc_timestamp_s = doc[F("WCIME")];
    _uptime = doc[F("UPTIME")];
    _pumptime = doc[F("PUMPTIME")];
    _heatingtime = doc[F("HEATINGTIME")];
    _airtime = doc[F("AIRTIME")];
    _jettime = doc[F("JETTIME")];
    _price = doc[F("PRICE")];
    _cl_interval = doc[F("CLINT")];
    _filter_interval = doc[F("FINT")];
    _fc_interval = doc[F("FCINT")];
    _wc_interval = doc[F("WCINT")];
    _audio_enabled = doc[F("AUDIO")];
    _notify = doc[F("NOTIFY")];
    _notification_time = doc[F("NOTIFTIME")];
    _energy_total_kWh = doc[F("KWH")];
    _energy_daily_Ws = doc[F("KWHD")];
    _ambient_temp = doc[F("AMB")] | 20;
    _dsp_brightness = doc[F("BRT")] | 7;
    _plz = doc[F("PLZ")].as<String>();
    _weather = doc[F("WEATHER")];
    _pool_capacity = doc[F("POOLCAP")];
    enableWeather = _weather;
    dsp->EnabledButtons[LOCK] = doc[F("LCK")];
    dsp->EnabledButtons[TIMER] = doc[F("TMR")];
    dsp->EnabledButtons[BUBBLES] = doc[F("AIR")];
    dsp->EnabledButtons[UNIT] = doc[F("UNT")];
    dsp->EnabledButtons[HEAT] = doc[F("HTR")];
    dsp->EnabledButtons[PUMP] = doc[F("FLT")];
    dsp->EnabledButtons[DOWN] = doc[F("DN")];
    dsp->EnabledButtons[UP] = doc[F("UP")];
    dsp->EnabledButtons[POWER] = doc[F("PWR")];
    dsp->EnabledButtons[HYDROJETS] = doc[F("HJT")];

    file.close();
}

void BWC::_restoreStates()
{
    File file = LittleFS.open("states.txt", "r");
    if (!file)
    {
        // Serial.println(F("Failed to read states.txt"));
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
    if (!file)
    {
        // Serial.println(F("Failed to read cmdq.json"));
        return;
    }

    DynamicJsonDocument doc(1024);
    // Deserialize the JSON document
    DeserializationError error = deserializeJson(doc, file);
    if (error)
    {
        // Serial.println(F("Failed to deserialize cmdq.json"));
        file.close();
        return;
    }
    _command_que.clear();
    // Set the values in the variables
    for (int i = 0; i < doc[F("LEN")]; i++)
    {
        command_que_item item;
        item.cmd = doc[F("CMD")][i];
        item.val = doc[F("VALUE")][i];
        item.xtime = doc[F("XTIME")][i];
        item.interval = doc[F("INTERVAL")][i];
        String s = doc[F("TXT")][i] | "";
        item.text = s;
        while ((item.interval > 0) && (item.xtime < (uint64_t)time(nullptr)))
            item.xtime += item.interval;
        _command_que.push_back(item);
    }
    file.close();
    std::sort(_command_que.begin(), _command_que.end(), _compare_command);
    _loadSettings();
    _restoreStates();
}

/*          */
/* SAVERS   */
/*          */

void BWC::saveRebootInfo()
{
    File file = LittleFS.open("bootlog.txt", "a");
    if (!file)
    {
        // Serial.println(F("Failed to save bootlog.txt"));
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
    File file = LittleFS.open("states.txt", "w");
    if (!file)
    {
        // Serial.println(F("Failed to save states.txt"));
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
// kill the dog
//  ESP.wdtDisable();
#ifdef ESP8266
    ESP.wdtFeed();
#endif
    File file = LittleFS.open("cmdq.json", "w");
    if (!file)
    {
        // Serial.println(F("Failed to save cmdq.json"));
        return;
    }
    else
    {
        // Serial.println(F("Wrote cmdq.json"));
    }
    /*Do not save instant reboot command. Don't ask me how I know.*/
    if (_command_que.size())
        if (_command_que[0].cmd == REBOOTESP && _command_que[0].interval == 0)
            return;
    DynamicJsonDocument doc(1024);

    // Set the values in the document
    doc[F("LEN")] = _command_que.size();
    for (unsigned int i = 0; i < _command_que.size(); i++)
    {
        doc[F("CMD")][i] = _command_que[i].cmd;
        doc[F("VALUE")][i] = _command_que[i].val;
        doc[F("XTIME")][i] = _command_que[i].xtime;
        doc[F("INTERVAL")][i] = _command_que[i].interval;
        doc[F("TXT")][i] = _command_que[i].text;
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
    File file = LittleFS.open("settings.json", "w");
    if (!file)
    {
        // Serial.println(F("Failed to save settings.json"));
        return;
    }

    DynamicJsonDocument doc(1024);
    _heatingtime += _heatingtime_ms / 1000;
    _pumptime += _pumptime_ms / 1000;
    _airtime += _airtime_ms / 1000;
    _jettime += _jettime_ms / 1000;
    _uptime += _uptime_ms / 1000;
    _heatingtime_ms = 0;
    _pumptime_ms = 0;
    _airtime_ms = 0;
    _jettime_ms = 0;
    _uptime_ms = 0;
    // Set the values in the document
    doc[F("CLTIME")] = _cl_timestamp_s;
    doc[F("FTIME")] = _filter_timestamp_s;
    doc[F("FCTIME")] = _fc_timestamp_s;
    doc[F("WCIME")] = _wc_timestamp_s;
    doc[F("FCTIME")] = _fc_timestamp_s;
    doc[F("WCTIME")] = _wc_timestamp_s;
    doc[F("UPTIME")] = _uptime;
    doc[F("PUMPTIME")] = _pumptime;
    doc[F("HEATINGTIME")] = _heatingtime;
    doc[F("AIRTIME")] = _airtime;
    doc[F("JETTIME")] = _jettime;
    doc[F("PRICE")] = _price;
    doc[F("CLINT")] = _cl_interval;
    doc[F("FINT")] = _filter_interval;
    doc[F("FCINT")] = _fc_interval;
    doc[F("WCINT")] = _wc_interval;
    doc[F("AUDIO")] = _audio_enabled;
    doc[F("KWH")] = _energy_total_kWh;
    doc[F("KWHD")] = _energy_daily_Ws;
    // doc[F("SAVETIME")] = DateTime.format(DateFormatter::SIMPLE);
    doc[F("AMB")] = _ambient_temp;
    doc[F("BRT")] = _dsp_brightness;
    doc[F("NOTIFY")] = _notify;
    doc[F("NOTIFTIME")] = _notification_time;
    doc[F("PLZ")] = _plz;
    doc[F("WEATHER")] = _weather;
    enableWeather = _weather;
    doc[F("POOLCAP")] = _pool_capacity;
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

    // Serialize JSON to file
    if (serializeJson(doc, file) == 0)
    {
        // Serial.println(F("Failed to write json to settings.json"));
    }
    file.close();
    // revive the dog
    //  ESP.wdtEnable(0);
}

// save out debug text to file "debug.txt" on littleFS
void BWC::saveDebugInfo(const String &s)
{
    File file = LittleFS.open("debug.txt", "a");
    if (!file)
    {
        // Serial.println(F("Failed to save debug.txt"));
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
        // Serial.println("file error");
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
