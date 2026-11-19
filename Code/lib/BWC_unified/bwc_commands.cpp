#include "bwc_internal.h"

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
