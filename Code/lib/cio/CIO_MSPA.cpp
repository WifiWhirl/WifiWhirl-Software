#include "CIO_MSPA.h"

void CIO_MSPA::setup(int cio_data_pin, int cio_clk_pin, int cio_cs_pin)
{
    (void)cio_cs_pin; // unused for UART
#if defined(ESP32)
    // data_pin = RX, clk_pin = TX
    Serial1.begin(9600, SERIAL_8N1, cio_data_pin, cio_clk_pin);
#else
    (void)cio_data_pin;
    (void)cio_clk_pin;
#endif
    // The spa is powered whenever the control box is connected; MSPA is Celsius-native.
    cio_states.power = 1;
    cio_states.unit = 1;
}

void CIO_MSPA::stop()
{
#if defined(ESP32)
    Serial1.end();
#endif
}

void CIO_MSPA::pause_all(bool action)
{
    _paused = action;
}

void CIO_MSPA::updateStates()
{
    if (_paused)
        return;
#if defined(ESP32)
    while (Serial1.available())
    {
        uint8_t b = (uint8_t)Serial1.read();
        // Resync: a frame must start with MSPA_START.
        if (_rx_len == 0 && b != MSPA_START)
            continue;
        _rx_buf[_rx_len++] = b;
        if (_rx_len < 4)
            continue;
        _rx_len = 0;
        uint8_t cs = (uint8_t)(_rx_buf[0] + _rx_buf[1] + _rx_buf[2]);
        if (cs == _rx_buf[3])
        {
            good_packets_count++;
            _parseFrame(_rx_buf);
        }
        else
        {
            bad_packets_count++;
            packet_error |= 0x01;
        }
    }
#endif
}

void CIO_MSPA::_parseFrame(const uint8_t *f)
{
    uint8_t cmd = f[1];
    uint8_t value = f[2];
    switch (cmd)
    {
    case MSPA_TEMP:
        // value is in half-degrees Celsius (e.g. 0x1C = 28 -> 14.0 C); round to whole.
        cio_states.temperature = (uint8_t)((value + 1) / 2);
        cio_states.tmp_ok = true;
        // Keep the heating element indicator in sync for energy estimation.
        if (cio_states.heat)
        {
            cio_states.heatred = (cio_states.temperature < cio_states.target) ? 1 : 0;
            cio_states.heatgrn = cio_states.heatred ? 0 : 1;
        }
        break;
    case MSPA_FLOW:
        // flow-status code meaning unconfirmed without a capture; left unmapped.
        break;
    default:
        break;
    }
    _raw_payload_from_cio = {f[0], f[1], f[2], f[3]};
}

void CIO_MSPA::handleToggles()
{
    // Power and child-lock have no MSPA wire equivalent (we are the remote);
    // track them locally so the UI buttons still behave.
    if (cio_toggles.power_change)
        cio_states.power = !cio_states.power;

    if (cio_toggles.locked_pressed)
        cio_states.locked = !cio_states.locked;

    if (cio_toggles.heat_change)
    {
        uint8_t v = !cio_states.heat;
        _sendFrame(MSPA_HEATER, v);
        cio_states.heat = v;
        cio_states.heatred = (v && cio_states.temperature < cio_states.target) ? 1 : 0;
        cio_states.heatgrn = (v && !cio_states.heatred) ? 1 : 0;
    }

    if (cio_toggles.pump_change)
    {
        uint8_t v = !cio_states.pump;
        _sendFrame(MSPA_PUMP, v);
        cio_states.pump = v;
    }

    if (cio_toggles.bubbles_change && getHasair())
    {
        uint8_t v = !cio_states.bubbles;
        _sendFrame(MSPA_BUBBLE, v);
        cio_states.bubbles = v;
    }

    if (cio_toggles.target != cio_states.target)
    {
        uint8_t tgt = cio_toggles.target;
        _sendFrame(MSPA_TARGET, (uint8_t)(tgt * 2)); // half-degree encoding
        cio_states.target = tgt;
        cio_states.tgt_ok = true;
    }
    // unit_change: MSPA is Celsius-native, nothing to send.
}

void CIO_MSPA::_sendFrame(uint8_t cmd, uint8_t value)
{
    uint8_t f[4] = {MSPA_START, cmd, value, (uint8_t)(MSPA_START + cmd + value)};
#if defined(ESP32)
    Serial1.write(f, 4);
#endif
    _raw_payload_to_cio = {f[0], f[1], f[2], f[3]};
    write_msg_count++;
}
