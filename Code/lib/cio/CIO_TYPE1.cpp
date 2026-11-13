#include "CIO_TYPE1.h"
#include "ports.h"
#include "util.h"
#ifndef ESP8266
#include "esp_cpu.h"
#endif

CIO_6_TYPE1 *pointerToClassCIO61;

#ifdef CIO_DIAG
// Bus timing seen by the CLK ISR, in CPU cycles: [0] min CLK edge distance,
// [1] edges where CLK had the same level as the previous ISR (missed edge),
// [2] CLK ISRs total, [3] button read requests (DSP_CMD2_DATAREAD),
// [4] button answers with all 16 bits clocked, [5] longest answer.
volatile uint32_t g_cio_diag[6] = {0xFFFFFFFF, 0, 0, 0, 0, 0};
static uint32_t s_diag_last_ccount;
static bool s_diag_last_clk;
#endif

static void IRAM_ATTR isr_cs_type1(void)
{
    pointerToClassCIO61->isr_packetHandler();
}
static void IRAM_ATTR isr_clk_type1(void)
{
    pointerToClassCIO61->isr_clkHandler();
}

CIO_6_TYPE1::CIO_6_TYPE1()
{
    _byte_count = 0;
    _bit_count = 0;
    _data_is_output = false;
    _received_byte = 0;
    _CIO_cmd_matches = 0;
    _new_packet_available = false;
    _send_bit = 8;
    _brightness = 7;
    _packet_error = 0;
}

void CIO_6_TYPE1::setup(int cio_data_pin, int cio_clk_pin, int cio_cs_pin)
{
    pointerToClassCIO61 = this;
    _DATA_PIN = cio_data_pin;
    _CLK_PIN = cio_clk_pin;
    _CS_PIN = cio_cs_pin;
    _button_code = getButtonCode(NOBTN);
    pinMode(_CS_PIN, INPUT);
    pinMode(_DATA_PIN, INPUT);
    pinMode(_CLK_PIN, INPUT);
    attachInterrupt(digitalPinToInterrupt(_CS_PIN), isr_cs_type1, CHANGE);
    // Write on falling edge and read on rising edge
    attachInterrupt(digitalPinToInterrupt(_CLK_PIN), isr_clk_type1, CHANGE);
}

void CIO_6_TYPE1::stop()
{
    detachInterrupt(digitalPinToInterrupt(_CS_PIN));
    detachInterrupt(digitalPinToInterrupt(_CLK_PIN));
}

void CIO_6_TYPE1::pause_all(bool action)
{
    if (action)
    {
        /*pause*/
        detachInterrupt(digitalPinToInterrupt(_CS_PIN));
        detachInterrupt(digitalPinToInterrupt(_CLK_PIN));
    }
    else
    {
        /*resume*/
        attachInterrupt(digitalPinToInterrupt(_CS_PIN), isr_cs_type1, CHANGE);
        attachInterrupt(digitalPinToInterrupt(_CLK_PIN), isr_clk_type1, CHANGE);
    }
}

void CIO_6_TYPE1::updateStates()
{
    /*update all states*/

    //_new_packet_available is true when a data packet has arrived from cio
    if (!_new_packet_available)
        return;

    // Critical section: copy volatile ISR data to local scope atomically
    noInterrupts();
    _new_packet_available = false;
    uint8_t local_packet_error = _ready_packet_error;
    uint8_t local_payload[11];
    memcpy(local_payload, (const void *)_ready_payload, sizeof(local_payload));
    uint8_t local_brightness = _brightness;
    interrupts();

    if (local_packet_error)
    {
        bad_packets_count++;
        packet_error = local_packet_error;
        return;
    }
    static uint32_t buttonReleaseTime;
    enum Readmode : int
    {
        readtemperature,
        uncertain,
        readtarget
    };
    static Readmode capturePhase = readtemperature;
    // Store last valid temperature to enable rate-of-change validation
    static uint8_t last_valid_temperature = 25;
    static bool first_temperature_reading = true; // Bypass validation for first reading
    static uint32_t last_valid_temperature_ms = 0;
    // A °C<->°F switch makes the reading legitimately jump ~30-50°, which the
    // rate-of-change check below would reject forever (locking TMP at the stale
    // value). Treat a unit change like a fresh start so the next reading is taken.
    static uint8_t last_unit = cio_states.unit;
    static uint8_t temperature_unit = 255; // unit of the last accepted reading, 255 = none yet
    if (cio_states.unit != last_unit)
    {
        last_unit = cio_states.unit;
        first_temperature_reading = true;
    }

// require two consecutive messages to be equal before registering
#if FILTER_6W_SPIKES == 1
    static uint8_t prev_checksum = 0;
    uint8_t checksum = 0;
    for (int i = 0; i < 11; i++)
    {
        checksum += local_payload[i];
    }
    if (checksum != prev_checksum)
    {
        prev_checksum = checksum;
        return;
    }
#endif
    // copy local snapshot to public array
    for (unsigned int i = 0; i < sizeof(local_payload); i++)
    {
        _raw_payload_from_cio[i] = local_payload[i];
    }
    good_packets_count++;
    brightness = local_brightness & 7;
    cio_states.locked = (_raw_payload_from_cio[LCK_IDX] & (1 << LCK_BIT)) > 0;
    cio_states.power = (_raw_payload_from_cio[PWR_IDX] & (1 << PWR_BIT)) > 0;
    /*If both leds are out, don't change (When TIMER is pressed)*/
    if (_raw_payload_from_cio[C_IDX] & (1 << C_BIT) || _raw_payload_from_cio[F_IDX] & (1 << F_BIT))
        cio_states.unit = (_raw_payload_from_cio[C_IDX] & (1 << C_BIT)) > 0;
    // After a unit switch the display shows the target for a while, so the next
    // reading comes seconds later. Until then TMP kept the old unit's number:
    // 73 "°C" in the web UI and on MQTT (seen on a real unit). Convert it in the
    // same pass that learns the new unit.
    if (temperature_unit <= 1 && temperature_unit != cio_states.unit)
    {
        cio_states.temperature = round(cio_states.unit ? F2C(cio_states.temperature) : C2F(cio_states.temperature));
        last_valid_temperature = cio_states.temperature;
        temperature_unit = cio_states.unit;
    }
    cio_states.bubbles = (_raw_payload_from_cio[AIR_IDX] & (1 << AIR_BIT)) > 0;
    cio_states.heatgrn = (_raw_payload_from_cio[GRNHTR_IDX] & (1 << GRNHTR_BIT)) > 0;
    cio_states.heatred = (_raw_payload_from_cio[REDHTR_IDX] & (1 << REDHTR_BIT)) > 0;
    cio_states.timerled1 = (_raw_payload_from_cio[TMR1_IDX] & (1 << TMR1_BIT)) > 0;
    cio_states.timerled2 = (_raw_payload_from_cio[TMR2_IDX] & (1 << TMR2_BIT)) > 0;
    cio_states.timerbuttonled = (_raw_payload_from_cio[TMRBTNLED_IDX] & (1 << TMRBTNLED_BIT)) > 0;
    cio_states.heat = cio_states.heatgrn || cio_states.heatred;
    cio_states.pump = (_raw_payload_from_cio[FLT_IDX] & (1 << FLT_BIT)) > 0;
    cio_states.char1 = (uint8_t)_getChar(_raw_payload_from_cio[DGT1_IDX]);
    cio_states.char2 = (uint8_t)_getChar(_raw_payload_from_cio[DGT2_IDX]);
    cio_states.char3 = (uint8_t)_getChar(_raw_payload_from_cio[DGT3_IDX]);
    cio_states.dsp_ok = true;
    if (getHasjets())
        cio_states.jets = (_raw_payload_from_cio[HJT_IDX] & (1 << HJT_BIT)) > 0;
    else
        cio_states.jets = 0;

    // Determine if display is showing target temp or actual temp or anything else.
    // Unreadable characters - exit
    if (cio_states.char1 == '*' || cio_states.char2 == '*' || cio_states.char3 == '*')
        return;
    // Error or user plays with timer button - exit (error notification can be dealt with in main.cpp or elsewhere)
    static uint32_t error_seen_ms;
    if (cio_states.char1 == 'e')
    {
        // Only "E" plus two digits is an error code. Without digits (a blinking
        // or half-drawn display) toInt() gave 0 and the error flickered between
        // 0 and its number (upstream 1d93c76).
        if (!isdigit(cio_states.char2) || !isdigit(cio_states.char3))
            return;
        cio_states.error = (uint8_t)((cio_states.char2 - '0') * 10 + (cio_states.char3 - '0'));
        error_seen_ms = millis();
        return;
    }
    if (cio_states.char3 == 'H' || cio_states.char3 == ' ')
        return;

    /* Reset error state, but only 5 s after the last error frame: a single
       normal frame in between must not clear it (upstream 8b8fa95) */
    if (cio_states.error && millis() - error_seen_ms > 5000)
        cio_states.error = 0;

    // capture TARGET after UP/DOWN has been pressed...
    // the pump board switches the display to the target some frames
    // after the press. Reading at once took the water temperature as the
    // target, and the boot restore then walked UP/DOWN from that wrong value.
    // fixed 250 ms settle, the real board's reaction time is unmeasured.
    static uint32_t buttonPressTime;
    static bool upDownHeld = false;
    if ((_button_code == getButtonCode(UP)) || (_button_code == getButtonCode(DOWN)))
    {
        if (!upDownHeld)
        {
            upDownHeld = true;
            buttonPressTime = millis();
        }
        buttonReleaseTime = millis(); // updated as long as buttons are pressed
        if (cio_states.power && !cio_states.locked)
            capturePhase = (millis() - buttonPressTime) < 250 ? uncertain : readtarget;
    }
    else
    {
        upDownHeld = false;
        // the module taps UP/DOWN shorter than 250 ms (a held key
        // auto-repeats on the panel), so the target is read after the release
        // too: from 250 ms after the press until the 2 s timeout below.
        if (capturePhase == uncertain && cio_states.power && !cio_states.locked &&
            buttonReleaseTime != 0 && (millis() - buttonPressTime) >= 250 && (millis() - buttonReleaseTime) <= 2000)
            capturePhase = readtarget;
    }

    // Stop expecting target temp after timeout
    if ((millis() - buttonReleaseTime) > 2000)
        capturePhase = uncertain;
    if ((millis() - buttonReleaseTime) > 6000)
        capturePhase = readtemperature;
    
    // convert text on display to a value if the chars are recognized
    String tempstring = String((char)cio_states.char1) + String((char)cio_states.char2) + String((char)cio_states.char3);
    uint8_t parsedValue = tempstring.toInt();
    
    // Handle TARGET TEMPERATURE setting (when user presses UP/DOWN buttons)
    // capture target temperature only if showing plausible values (not blank screen while blinking)
    if ((capturePhase == readtarget) && (parsedValue > 19))
    {
        cio_states.target = parsedValue;
        cio_states.tgt_ok = true;
    }
    
    // Handle ACTUAL TEMPERATURE reading with full validation
    // wait 6 seconds after UP/DOWN is released to be sure that actual temp is shown
    if (capturePhase == readtemperature)
    {
        // VALIDATION LAYER 1: Ensure all characters are numeric digits or spaces (leading zeros)
        if ((cio_states.char1 != ' ' && (cio_states.char1 < '0' || cio_states.char1 > '9')) ||
            (cio_states.char2 != ' ' && (cio_states.char2 < '0' || cio_states.char2 > '9')) ||
            (cio_states.char3 < '0' || cio_states.char3 > '9'))
        {
            return; // Exit early if invalid characters detected
        }
        
        // VALIDATION LAYER 2: Range validation (Celsius: 0-50°C, Fahrenheit: 32-120°F)
        if ((cio_states.unit && parsedValue > 50) || (!cio_states.unit && (parsedValue < 32 || parsedValue > 120)))
        {
            return; // Exit if temperature is outside reasonable operating range
        }
        
        // Standby shows a blank display, so no reading passes for hours while the
        // water cools; the jump check below then rejected the real value forever.
        // After 30 s without a valid reading, start over.
        if (millis() - last_valid_temperature_ms > 30000)
            first_temperature_reading = true;

        // VALIDATION LAYER 3: 
        if (parsedValue == cio_states.target && !first_temperature_reading)
        {
            uint8_t target_delta = (parsedValue > last_valid_temperature)
                ? (parsedValue - last_valid_temperature)
                : (last_valid_temperature - parsedValue);
            if (target_delta > 2)
                return;
        }

        // VALIDATION LAYER 4: Rate-of-change validation (max 10° change per cycle)
        // Skip on first reading to allow any valid starting temperature
        if (!first_temperature_reading)
        {
            uint8_t temp_delta = (parsedValue > last_valid_temperature) 
                ? (parsedValue - last_valid_temperature) 
                : (last_valid_temperature - parsedValue);
            if (temp_delta > 10)
            {
                return; // Reject extreme temperature jumps indicating data corruption
            }
        }
        
        // ALL VALIDATIONS PASSED - Safe to update actual temperature
        last_valid_temperature = parsedValue;
        last_valid_temperature_ms = millis();
        first_temperature_reading = false;
        cio_states.temperature = parsedValue;
        temperature_unit = cio_states.unit;
        cio_states.tmp_ok = true;
    }

    return;
}

/*End Of Packet.*/
void IRAM_ATTR CIO_6_TYPE1::eopHandler(void)
{
    // process latest data and enter corresponding mode (like listen for DSP_STS or send BTN_OUT)
    // pinMode(_DATA_PIN, INPUT);
    WRITE_PERI_REG(PIN_DIR_INPUT, 1 << _DATA_PIN);
    if (_byte_count != 11 && _byte_count != 0)
        _packet_error |= 2;
    if (_bit_count != 0)
        _packet_error |= 1;
    _byte_count = 0;
    _bit_count = 0;
    uint8_t msg = _received_byte;

    switch (msg)
    {
    case DSP_CMD1_MODE6_11_7:
        _CIO_cmd_matches = 1;
        break;
    case DSP_CMD1_MODE6_11_7_P05504:
        _CIO_cmd_matches = 1;
        break;
    case DSP_CMD2_DATAWRITE:
        if (_CIO_cmd_matches == 1)
        {
            _CIO_cmd_matches = 2;
        }
        else
        {
            // reset - DSP_CMD1_MODE6_11_7 must be followed by DSP_CMD2_DATAWRITE to activate command
            _CIO_cmd_matches = 0;
        }
        break;
    default:
        if (_CIO_cmd_matches == 3)
        {
            _brightness = msg;
            _CIO_cmd_matches = 0;
            // Hand over a complete copy: the next frame writes _payload byte by
            // byte, and a slow loop() could copy a mix of two frames.
            for (int i = 0; i < 11; i++)
                _ready_payload[i] = _payload[i];
            _ready_packet_error = _packet_error;
            _packet_error = 0;
            _new_packet_available = true;
        }
        if (_CIO_cmd_matches == 2)
        {
            _CIO_cmd_matches = 3;
        }
        break;
    }
}

// CIO comm
// packet start
// CS line toggles
void IRAM_ATTR CIO_6_TYPE1::isr_packetHandler()
{
    if ((READ_PERI_REG(PIN_IN) & (1 << _CS_PIN)))
    {
        // end of packet (CS is idle at high)
        _packet_transm_active = false;
        _data_is_output = false;
        eopHandler();
    }
    else
    {
        // packet start (CS active low)
        _packet_transm_active = true;
    }
}

// CIO comm
// Read incoming bits, and take action after a complete byte
// CLK line toggles
void IRAM_ATTR CIO_6_TYPE1::isr_clkHandler(void)
{
    // sanity check on clock signal
#ifdef CIO_DIAG
    {
        uint32_t now = esp_cpu_get_cycle_count();
        bool clk = READ_PERI_REG(PIN_IN) & (1 << _CLK_PIN);
        uint32_t dt = now - s_diag_last_ccount;
        s_diag_last_ccount = now;
        if (dt < g_cio_diag[0])
            g_cio_diag[0] = dt;
        if (clk == s_diag_last_clk)
            g_cio_diag[1]++;
        s_diag_last_clk = clk;
        g_cio_diag[2]++;
    }
#endif

    if (!_packet_transm_active)
        return;
        // CS line is active, so send/receive bits on DATA line

    bool clockstate = READ_PERI_REG(PIN_IN) & (1 << _CLK_PIN);
#ifdef ESP8266
    // shift out bits on low clock (falling edge). ESP32: _sendButtonPolled()
    if (!clockstate & _data_is_output)
    {
        // send BTN_OUT
        if (_button_code & (1 << _send_bit))
        {
// digitalWrite(_DATA_PIN, HIGH);
            WRITE_PERI_REG(PIN_OUT_SET, 1 << _DATA_PIN);
        }
        else
        {
// digitalWrite(_DATA_PIN, LOW);
            WRITE_PERI_REG(PIN_OUT_CLEAR, 1 << _DATA_PIN);
        }
        _send_bit++;
        if (_send_bit > 15)
            _send_bit = 0;
    }
#endif

    // read bits on high clock (rising edge)
    if (clockstate & !_data_is_output)
    {
        // read data pin to a byte
        //_received_byte = (_received_byte << 1) | digitalRead(_DATA_PIN);
        //_received_byte = (_received_byte << 1) | ( ( (READ_PERI_REG(PIN_IN) & (1 << _DATA_PIN)) ) > 0);
        //_received_byte = (_received_byte >> 1) | digitalRead(_DATA_PIN) << 7;
        _received_byte = (_received_byte >> 1) | (((READ_PERI_REG(PIN_IN) & (1 << _DATA_PIN))) > 0) << 7;
        _bit_count++;
        if (_bit_count == 8)
        {
            _bit_count = 0;
            // We have received the header for 11 data bytes to come
            if (_CIO_cmd_matches == 2)
            {
                if (_byte_count < 11)
                {
                    _payload[_byte_count] = _received_byte;
                    _byte_count++;
                }
                else
                {
                    _packet_error |= 4; // too many bytes received
                }
            }
            // We have received request for button pressed
            else if (_received_byte == DSP_CMD2_DATAREAD)
            {
                _send_bit = 8;
                _data_is_output = true;
                // pinMode(_DATA_PIN, OUTPUT);
#ifdef CIO_DIAG
                g_cio_diag[3]++;
#endif
#ifndef CIO_READ_ONLY // listen only: never drive DATA, so no button reaches the pump
                WRITE_PERI_REG(PIN_DIR_OUTPUT, 1 << _DATA_PIN);
#endif
#ifndef ESP8266
                _sendButtonPolled();
#endif
            }
            CIO_CMD_LOG[CIO_CMD_LOG_index++] = (uint8_t)_received_byte;
            CIO_CMD_LOG_index %= 64;
        }
    }
}

#ifndef ESP8266
// ESP32: the GPIO interrupt latency is close to the bus clock half period, so
// an edge-per-interrupt answer lost ~1.6 % of the edges and the pump read
// phantom buttons. Answer the 16 button bits in one go instead, polling CLK
// in the register. Called from the CLK ISR right after DSP_CMD2_DATAREAD;
// stops early on CS high or after ~2 ms (at 240 MHz) if the clock stalls.
void IRAM_ATTR CIO_6_TYPE1::_sendButtonPolled()
{
    const uint32_t clk = 1u << _CLK_PIN, cs = 1u << _CS_PIN, data = 1u << _DATA_PIN;
    const uint32_t start = esp_cpu_get_cycle_count();
    const uint16_t code = _button_code;
    uint8_t bit = 8; // 8..15 then 0..7, same order as the ESP8266 path
    int n = 0;
    for (; n < 16; n++)
    {
        uint32_t in;
        while ((in = READ_PERI_REG(PIN_IN)) & clk) // wait for falling edge
            if ((in & cs) || esp_cpu_get_cycle_count() - start > 480000)
                goto done;
        WRITE_PERI_REG((code & (1u << bit)) ? PIN_OUT_SET : PIN_OUT_CLEAR, data);
        bit = (bit + 1) & 15;
        while (!((in = READ_PERI_REG(PIN_IN)) & clk)) // pump samples on rising edge
            if ((in & cs) || esp_cpu_get_cycle_count() - start > 480000)
                goto done;
    }
done:
#ifdef CIO_DIAG
    uint32_t took = esp_cpu_get_cycle_count() - start;
    if (n == 16)
        g_cio_diag[4]++;
    if (took > g_cio_diag[5])
        g_cio_diag[5] = took;
#endif
    return;
}
#endif

char CIO_6_TYPE1::_getChar(uint8_t value)
{
    for (unsigned int index = 0; index < sizeof(CHARCODES); index++)
    {
        /* Mask out the LSB due to some pumps differ and that bit is not used anyway. */
        if ((value & 0xFE) == (CHARCODES[index] & 0xFE))
        {
            return CHARS[index];
        }
    }
    return '*';
}