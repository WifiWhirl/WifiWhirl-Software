#include "bwc_internal.h"

namespace
{
// Minimum time between two tick flips to count as a real cool-down interval
// (guards against sloshing/noise flipping the reading right after the anchor).
// One tick in 15 min is plausible for a lossy tub in winter.
constexpr uint32_t HLCAL_MIN_ELAPSED_S = 900;
// Must stay consistent with HLCAL_MIN_ELAPSED_S: one C-tick per 15 min on a
// typical 800 l tub is ~3700 W net, so the cap has to sit above that or a
// genuinely lossy tub could never finish (every tick would re-anchor).
constexpr float HLCAL_MAX_NET_LOSS_W = 4500.0f;
// share of the filter pump heat counted in the calibration: k then describes
// the loss mostly without the pump running (tester decision)
constexpr float HLCAL_PUMP_SHARE = 0.75f;

enum HlCalError : uint8_t
{
    HLCAL_ERR_NONE = 0,
    HLCAL_ERR_PREP = 1,
    HLCAL_ERR_INTERRUPTED = 2,
    HLCAL_ERR_TIMEOUT = 3,
    HLCAL_ERR_UNSTABLE = 4,
    HLCAL_ERR_TEMPDIFF = 5 // planned start skipped: water not warm enough above the air
};
// Minimum water/ambient difference to start. 8 K let a whole night
// pass without one sensor tick at 12 K (tester); value still being decided.
constexpr float HLCAL_MIN_DIFF_K = 15.0f; // tester: 12 K gave no sensor step in 23 h

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
    case HLCAL_ERR_TEMPDIFF:
        return F("tempdiff");
    default:
        return F("");
    }
}
}


void BWC::setHeatLoss(float v)
{
    _heat_loss = clampHeatLoss(v);
    _save_settings_needed = true;
    // A value set by hand replaces the last calibration: its timeline and
    // result go. A running calibration carries on.
    if (!_hlcal.active && !_hlcal.restore_pending)
        _hlcal = heatloss_cal_t{};
}

// Begin a heat-loss measurement. The actual hardware control (heater/AirJet/
// HydroJet off, pump on) happens in _handleHeatLossCalibration so the toggles
// are set at the right point in the loop. The only refusal here is too small a
// water/ambient difference (err carries a short code the UI localizes).
bool BWC::startHeatLossCalibration(String &err)
{
    float waterC = waterTempC(cio);
    if (waterC - _ambient_temp < HLCAL_MIN_DIFF_K)
    {
        err = F("tempdiff");
        return false;
    }

    _hlcal = heatloss_cal_t{}; // reset, keep no stale result mid-run
    _hlcal.active = true;
    _hlcal.phase = 0;
    _hlcal.req_ts = _timestamp_secs;
    _hlcal.req_water_c = waterC;
    _hlcal.start_amb_c = _ambient_temp;
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
bool BWC::planHeatLossCalibration(uint64_t at)
{
    if (at != 0 && (_hlcal.active || at <= _timestamp_secs))
        return false;
    _hlcal_planned_at = at;
    _save_settings_needed = true;
    return true;
}

void BWC::cancelHeatLossCalibration()
{
    if (_hlcal_planned_at)
        planHeatLossCalibration(0);
    if (_hlcal.active)
        _hlcal.cancel_requested = true;
}

void BWC::_finishHeatLossCalibration(uint8_t error)
{
    _restoreCalibrationState();
    _hlcal.error = error;
    _hlcal.end_ts = _timestamp_secs;
    _hlcal.end_water_c = waterTempC(cio);
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
    // planned start, only with a valid clock
    if (_hlcal_planned_at && !_hlcal.active && _timestamp_secs > 57600 && _timestamp_secs >= _hlcal_planned_at)
    {
        planHeatLossCalibration(0);
        String err;
        // Too little difference: skip it, and leave the reason for whoever opens
        // the web UI next (tester changed his mind: no forced start).
        if (!startHeatLossCalibration(err))
        {
            _hlcal = heatloss_cal_t{};
            _hlcal.error = HLCAL_ERR_TEMPDIFF;
            _hlcal.req_ts = _hlcal.end_ts = _timestamp_secs;
            _hlcal.req_water_c = _hlcal.end_water_c = waterTempC(cio);
            _hlcal.start_amb_c = _ambient_temp;
        }
    }
    if (_hlcal.restore_pending && (cio->_button_que_len == 0 || (int32_t)(millis() - _hlcal.restore_deadline) >= 0))
    {
        _hlcal.restore_pending = false;
        _restoreCalibrationState();
    }
    if (!_hlcal.active)
        return;

    if (_hlcal.cancel_requested)
    {
        _hlcal.cancel_requested = false;
        // Not right away: phase 0's presses (heater off, pump on, °C) may still
        // be queued, and the restore would see the old states, press nothing,
        // and leave the tub in the wrong unit once the queue ran.
        _hlcal.restore_pending = true;
        _hlcal.restore_deadline = millis() + 30000;
        _hlcal.active = false;
        _hlcal.end_ts = _timestamp_secs;
        _hlcal.end_water_c = waterTempC(cio);
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
            // Measure in Celsius. The pump measures in C and converts, so in
            // F the reading regularly jumps two ticks at once:
            // the finer F steps were never real resolution.
            if (!cio->cio_states.unit)
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
                // one display tick in the unit we measure in (C, see above)
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
            // is the NET power leaving; the running pump feeds heat back in.
            // Only HLCAL_PUMP_SHARE of it is counted, so k describes the loss
            // with the pump mostly at rest (tester decision).
            // k = (netLoss + share*pump) / dTavg.
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
            float k = clampHeatLoss((netLossW + HLCAL_PUMP_SHARE * cio->getPower().PUMPPOWER) / dTavg);
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
    DynamicJsonDocument doc(768);
    doc[F("ACTIVE")] = _hlcal.active;
    doc[F("CURRENT")] = _heat_loss;
    doc[F("DEFAULT")] = HEAT_LOSS_DEFAULT;
    doc[F("MINDIFF")] = HLCAL_MIN_DIFF_K;
    doc[F("TARGETDROP")] = _hlcal.target_drop;
    doc[F("RESULT")] = _hlcal.result;
    if (_hlcal.error != HLCAL_ERR_NONE)
        doc[F("ERROR")] = hlCalErrorName(_hlcal.error);
    doc[F("PHASE")] = _hlcal.phase; // 0 = preparing (pump/heater), 1 = measuring
    doc[F("NOW")] = _timestamp_secs;
    if (_hlcal_planned_at)
        doc[F("PLANNED")] = _hlcal_planned_at;
    // The last run, active or not, until the next start: when each step was
    // reached and the water temperature then.
    if (_hlcal.req_ts)
    {
        doc[F("REQTS")] = _hlcal.req_ts;
        doc[F("REQWATER")] = _hlcal.req_water_c;
        doc[F("STARTAMB")] = _hlcal.amb_n ? _hlcal.amb_sum / (float)_hlcal.amb_n
                                          : (float)_hlcal.start_amb_c;
        doc[F("DISCARDED")] = _hlcal.discarded;
        doc[F("RESTORING")] = _hlcal.active && _hlcal.disturb_ts != 0;
        doc[F("ANCHORED")] = _hlcal.anchored;
    }
    if (_hlcal.start_ts)
    {
        doc[F("STARTTS")] = _hlcal.start_ts;
        doc[F("STARTWATER")] = _hlcal.start_water_c;
    }
    if (_hlcal.anchored)
    {
        doc[F("ANCHORTS")] = _hlcal.anchor_ts;
        doc[F("ANCHORWATER")] = _hlcal.anchor_water_c;
    }
    if (_hlcal.end_ts)
    {
        doc[F("ENDTS")] = _hlcal.end_ts;
        doc[F("ENDWATER")] = _hlcal.end_water_c;
    }
    serializeJson(doc, rtn);
}
