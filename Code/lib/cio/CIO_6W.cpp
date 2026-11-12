#include "CIO_6W.h"
#include "util.h"

void CIO_6W::handleToggles()
{
    sButton_queue_item item;
    _handleButtonQ();
    if (_button_que_len > 30)
        return;
    /*Compare with previous requirement. Only add buttons to the queue at first change.*/
    /*If comparing with actual states it will add buttons to the queue every loop.*/ /*will try that anyway, with guards*/
    if (cio_toggles.power_change)
    {
        item.btncode = getButtonCode(POWER);
        item.p_state = &sStates::power;
        item.value = !cio_states.power;
        item.duration_ms = 5000;
        _qButton(item);
        return;
    }

    if (cio_toggles.locked_pressed && (_button_que_len == 0))
    {
        item.btncode = getButtonCode(LOCK);
        item.p_state = &sStates::locked;
        item.value = !cio_states.locked;
        //item.p_state = &sStates::char1; // This field will never meet the value condition.
        //item.value = 0xFF;              // Just a trick to press this button for the duration, no matter what
        item.duration_ms = 5000;
        _qButton(item);
        return;
    }

    // Not in standby: the dark panel shows no unit LED, the unit is only the °F
    // default, so the boot restore's SETUNIT pressed POWER (unlock) and UNIT.
    // The board keeps its unit over standby anyway.
    if (cio_toggles.unit_change && cio_states.power)
    {
        unlock();
        item.btncode = getButtonCode(UNIT);
        item.p_state = &sStates::unit;
        item.value = !cio_states.unit;
        item.duration_ms = 5000;
        _qButton(item);

        // if((requestedStates.target >= 20) && (requestedStates.target <= 104) && (requestedStates.target == _requested_states.target))
        // {
        //     if(requestedStates.unit)
        //     {
        //         cio_states.target = round(F2C(requestedStates.target));
        //     }
        //     else
        //     {
        //         cio_states.target = round(C2F(requestedStates.target));
        //     }
        // }
    }

    // if((requestedStates.bubbles != _requested_states.bubbles) && getHasair())
    if (cio_toggles.bubbles_change && getHasair())
    {
        unlock();
        item.btncode = getButtonCode(BUBBLES);
        item.p_state = &sStates::bubbles;
        item.value = !cio_states.bubbles;
        item.duration_ms = 5000;
        _qButton(item);
    }

    // if(requestedStates.heat != _requested_states.heat)
    if (cio_toggles.heat_change)
    {
        unlock();
        item.btncode = getButtonCode(HEAT);
        item.p_state = &sStates::heat;
        item.value = !cio_states.heat;
        item.duration_ms = 5000;
        _qButton(item);
    }

    // if(requestedStates.pump != _requested_states.pump)
    if (cio_toggles.pump_change)
    {
        unlock();
        item.btncode = getButtonCode(PUMP);
        item.p_state = &sStates::pump;
        item.value = !cio_states.pump;
        item.duration_ms = 5000;
        _qButton(item);
    }

    // Unknown target: press UP once to read it, but not in standby: unlock()
    // presses POWER, which switched a standby pool on after every reboot
    if (cio_states.target == 0 && cio_states.power && _button_que_len == 0)
    {
        unlock();
        // a tap, not a hold: a held key auto-repeats and changed the target
        item.btncode = getButtonCode(UP);
        item.p_state = &sStates::target;
        item.value = 999;
        item.duration_ms = 200;
        _qButton(item);
        item.btncode = getButtonCode(NOBTN);
        item.p_state = &sStates::char1;
        item.value = 0xFF;
        item.duration_ms = 1000;
        _qButton(item);
    }

    // Walk only from a known target (0 = not read yet) and only while the pool is
    // on: unlock() presses POWER, so a walk still running when the user switched
    // the pool off switched it straight back on. It resumes at power on.
    if ((cio_toggles.target != cio_states.target) && cio_states.target != 0 && cio_states.power &&
        (_button_que_len == 0))
    {
        unlock();
        Buttons dir;
        cio_toggles.target > cio_states.target ? dir = UP : dir = DOWN;
        // One tap per step, then wait for the display: holding the key up to
        // 800 ms let the panel's auto-repeat step past the target.
        // 200 ms tap / 600 ms wait guessed, the panel's repeat delay is unmeasured.
        item.btncode = getButtonCode(dir);
        item.p_state = &sStates::target;
        item.value = cio_toggles.target;
        item.duration_ms = 200;
        _qButton(item);
        item.btncode = getButtonCode(NOBTN);
        item.p_state = &sStates::char1;
        item.value = 0xFF;
        item.duration_ms = 600;
        _qButton(item);
    }

    // if((requestedStates.jets != _requested_states.jets) && getHasjets())
    if (cio_toggles.jets_change && getHasjets())
    {
        unlock();
        item.btncode = getButtonCode(HYDROJETS);
        item.p_state = &sStates::jets;
        item.value = !cio_states.jets;
        item.duration_ms = 5000;
        _qButton(item);
    }

    if ((cio_toggles.timer_pressed) && (_button_que_len == 0))
    {
        unlock();
        item.btncode = getButtonCode(TIMER);
        item.p_state = &sStates::char1; // This field will never meet the value condition.
        item.value = 0xFF;              // Just a trick to press this button for the duration, no matter what
        item.duration_ms = 100;
        _qButton(item);
    }

    if ((cio_toggles.up_pressed) && (_button_que_len == 0))
    {
        unlock();
        item.btncode = getButtonCode(UP);
        item.p_state = &sStates::char1;
        item.value = 0xFF;
        item.duration_ms = 100;
        _qButton(item);
    }

    if ((cio_toggles.down_pressed) && (_button_que_len == 0))
    {
        unlock();
        item.btncode = getButtonCode(DOWN);
        item.p_state = &sStates::char1;
        item.value = 0xFF;
        item.duration_ms = 100;
        _qButton(item);
    }

    // _pressed_button = cio_toggles.pressed_button; TODO: remove variable from class
}

void CIO_6W::unlock()
{
    sButton_queue_item item;
    item.btncode = getButtonCode(POWER);
    item.p_state = &sStates::power;
    item.value = 1;
    item.duration_ms = 5000;
    _qButton(item);
    item.btncode = getButtonCode(LOCK);
    item.p_state = &sStates::locked;
    item.value = 0;
    item.duration_ms = 5000;
    _qButton(item);
}

void CIO_6W::_qButton(sButton_queue_item item)
{
    if (_button_que_len >= MAXBUTTONS)
        return; // maybe textout an error message if queue is full?
    _button_que[_button_que_len].btncode = item.btncode;
    _button_que[_button_que_len].p_state = item.p_state;
    _button_que[_button_que_len].value = item.value;
    _button_que[_button_que_len].duration_ms = item.duration_ms;
    _button_que_len++;
    /*"_button_que[0] = item" may not copy values but pointer to item which goes out of scope. Not sure.*/
}

void CIO_6W::_handleButtonQ(void)
{
    static uint32_t prevMillis = millis();
    static uint32_t elapsedTime = 0;

    elapsedTime = millis() - prevMillis;
    prevMillis = millis();
    uint8_t waitlimit = 0;
    // after a press, keep the button released long enough for the pump
    // to poll NOBTN (~20 ms cycle). Otherwise the next press of the same button
    // starts on the next loop() pass, the pump sees one long press, and the
    // second toggle is lost.
    if ((int32_t)(_release_until - millis()) > 0)
        return;
    // No presses before the first decoded frame: the states are still the
    // defaults (power 0), so unlock() pressed POWER on a running pool
    if (!cio_states.dsp_ok)
        return;
    if (_button_que_len == 0)
    {
        // Held UP/DOWN go to the pump as they are, for as long as they are
        // held, so the pump repeats them itself (queued taps stepped once).
        uint16_t code = getButtonCode(cio_toggles.held_button);
        if (_button_code == code)
            return;
        waitlimit = 0;
        while (_packet_transm_active && ++waitlimit < 10)
            delay(1);
        _button_code = code;
        return;
    }
    // First subtract elapsed time from maxduration
    _button_que[0].duration_ms -= elapsedTime;
    // check if state is as desired, or duration is up. If so - remove row. Else set BTNCODE
    if ((cio_states.*_button_que[0].p_state == _button_que[0].value) || (_button_que[0].duration_ms <= 0))
    {
        // remove row
        for (int i = 0; i < _button_que_len - 1; i++)
        {
            _button_que[i].btncode = _button_que[i + 1].btncode;
            _button_que[i].p_state = _button_que[i + 1].p_state;
            _button_que[i].value = _button_que[i + 1].value;
            _button_que[i].duration_ms = _button_que[i + 1].duration_ms;
        }
        _button_que_len--;
        /*Avoiding write to variable when it's beeing sent to the cio*/
        waitlimit = 0;
        while (_packet_transm_active && ++waitlimit < 10)
            delay(1);
        if (_button_code != getButtonCode(NOBTN))
            _release_until = millis() + 100; // a press ended (not an already-met item)
        _button_code = getButtonCode(NOBTN);
    }
    else
    {
        // keep button "pressed"
        /*Avoiding write to variable when it's beeing sent to the cio*/
        waitlimit = 0;
        while (_packet_transm_active && ++waitlimit < 10)
            delay(1);
        _button_code = _button_que[0].btncode;
    }
}

CIO_6W::CIO_6W()
{
    cio_states.target = 0;
}
