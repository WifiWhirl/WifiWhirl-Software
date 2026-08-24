#pragma once
#include <Arduino.h>
#include "enums.h"
#include "CIO_BASE.h"

/*
 * MSPA control I/O.
 *
 * Unlike Bestway CIOs (6-wire bit-banged SPI, man-in-the-middle), the
 * WifiWhirl PCB fully *replaces* the MSPA cable remote and talks to the control
 * box directly over a 4-wire link: GND, +5V, and two UART lines at 9600 8N1.
 * Frame format: [0xA5][cmd][value][checksum], checksum = (0xA5+cmd+value)&0xFF.
 *
 * Pin reuse (no hwcfg schema change): data_pin = RX (box->us),
 * clk_pin = TX (us->box), cs_pin unused.
 *
 * Inherits CIO directly (the SPI button-queue layers CIO_6W/CIO_6_TYPE1 don't
 * apply): commands are sent as single frames, state is read from the box.
 * Serial I/O is ESP32-only; the class still compiles (no-ops the UART) on
 * ESP8266 so that build keeps linking.
 */
class CIO_MSPA : public CIO
{
public:
    CIO_MSPA(){};
    virtual ~CIO_MSPA(){};
    void setup(int cio_data_pin, int cio_clk_pin, int cio_cs_pin) override;
    void stop() override;
    void pause_all(bool action) override;
    void updateStates() override;
    void handleToggles() override;
    String getModel() override { return _MYMODEL; }
    Power getPower() override { return power; }
    Power getDefaultPower() override { return _powerDefault; }
    void setPower(const Power &p) override { power = p; }
    bool getHasgod() override { return false; }
    bool getHasjets() override { return _HASJETS; }
    bool getHasair() override { return _HASAIR; }

public:
    // placeholder wattages - tune to the real MSPA draw once measured (calibration knob)
    const Power _powerDefault = {1500, 40, 800, 2, 0};
    Power power = _powerDefault;

private:
    const String _MYMODEL = "MSPA";
    // MSPA bubbles/jets are a single function -> exposed as "air", no separate hydrojets.
    const bool _HASJETS = false;
    const bool _HASAIR = true;

    static constexpr uint8_t MSPA_START = 0xA5;
    enum : uint8_t
    {
        MSPA_HEATER = 0x01,
        MSPA_PUMP = 0x02,
        MSPA_BUBBLE = 0x03,
        MSPA_TARGET = 0x04,
        MSPA_TEMP = 0x06,
        MSPA_FLOW = 0x08,
    };

    void _sendFrame(uint8_t cmd, uint8_t value);
    void _parseFrame(const uint8_t *f);

    bool _paused = false;
    uint8_t _rx_buf[4] = {0};
    uint8_t _rx_len = 0;
};
