#include "bwc_internal.h"

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

bool BWC::setSmartSchedule(uint64_t target_time, uint8_t target_temp, bool keep_heater_on, uint8_t repeat_days, bool keep_pump_on)
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
    _smart_schedule.keep_pump_on = keep_pump_on;
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
 * Filter pump keeps running after the target time (until switched off by hand)
 * @return true if an active schedule was updated
 */
bool BWC::updateSmartScheduleKeepPumpOn(bool keep_pump_on)
{
    if (!_smart_schedule.active)
        return false;
    if (_smart_schedule.keep_pump_on != keep_pump_on)
    {
        _smart_schedule.keep_pump_on = keep_pump_on;
        _save_smartschedule_needed = true;
        _new_data_available = true;
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
    
    // The schedule switched the heater on in the last seconds but the pump does
    // not report it yet: a heater-off now would see "off" and do nothing, and the
    // heater then runs on with no schedule left. Switch off once it shows (the
    // same settle window the pump-off below covers via temp_reading_state).
    bool heat_pending = !cio->cio_states.heat && (int32_t)(millis() - _schedule_heat_queued_until) < 0;
    _schedule_heat_queued_until = millis();
    if (heat_pending)
    {
        item.xtime = _timestamp_secs + 10;
        item.cmd = SETHEATER;
        item.val = 0;
        item.force = false;
        add_command(item);
        item.cmd = SETPUMP; // the heater pulled the pump on
        item.force = true;
        add_command(item);
        item.xtime = 0;
        Serial.println(F("SmartSchedule: Cancel - heater just started, off in 10 s"));
    }

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
    _smart_schedule.keep_pump_on = false;
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
    doc[F("KEEPPUMP")] = _smart_schedule.keep_pump_on;
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
            
            // "only the filter pump" leaves it running until switched off by hand
            if (cio->cio_states.pump && !_smart_schedule.keep_pump_on)
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
                _schedule_heater_off_pending = true; // not a manual cancel
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
        // once per start, not on every loop() until the pump reports the heater on
        if (!cio->cio_states.heat && (int32_t)(millis() - _schedule_heat_queued_until) >= 0)
        {
            _schedule_heat_queued_until = millis() + 10000;
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
                
                if (!cio->cio_states.heat && (int32_t)(millis() - _schedule_heat_queued_until) >= 0)
                {
                    _schedule_heat_queued_until = millis() + 10000;
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
    _smart_schedule.keep_pump_on = doc[F("KEEPPUMP")] | false;
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
    // time(nullptr), not _timestamp_secs: loop() has not set that yet.
    // Before NTP the clock is ~0, so nothing expires here; loop() completes
    // an overdue schedule once the time is valid.
    if (_smart_schedule.active && _smart_schedule.target_time <= (uint64_t)time(nullptr) &&
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
    doc[F("KEEPPUMP")] = _smart_schedule.keep_pump_on;
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
