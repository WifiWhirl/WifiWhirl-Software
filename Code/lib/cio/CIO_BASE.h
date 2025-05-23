#pragma once
#include <Arduino.h>
#include "enums.h"

constexpr int MAXBUTTONS = 50;

class CIO
{
public:
    CIO(){};
    virtual ~CIO(){};
    /*init cio*/
    virtual void setup(int cio_data_pin, int cio_clk_pin, int cio_cs_pin) = 0;
    /*Stop the interrupts etc*/
    virtual void stop() = 0;
    /*action true = stop, false = resume*/
    virtual void pause_all(bool action) = 0;
    virtual void updateStates() = 0;
    virtual void handleToggles() = 0;
    void setRawPayload(const std::vector<uint8_t> &pl);
    std::vector<uint8_t> getRawPayload();
    virtual String getModel() = 0;
    virtual Power getPower() = 0;
    virtual bool getHasgod() = 0;
    virtual bool getHasjets() = 0;
    virtual bool getHasair() = 0;
    // virtual bool getSerialReceived() { return false; } //"overridden" in CIO 4W
    // virtual void setSerialReceived(bool txok) {}       //"overridden" in CIO 4W
    String debug();

public:
    sStates cio_states;
    sToggles cio_toggles;
    int _button_que_len = 0; // length of buttonQ
    int write_msg_count = 0;
    uint32_t good_packets_count = 0;
    uint32_t bad_packets_count = 0;
    // debugging comm problems. Bit 0: bit error, Bit 1: too many bytes error, Bit 2: too few bytes error
    uint8_t packet_error = 0;
    // debug - stores cio special bytes
    uint8_t CIO_CMD_LOG[64] = {0};
    uint8_t CIO_CMD_LOG_index = 0;
    std::vector<uint8_t> _raw_payload_to_cio = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

protected:
    std::vector<uint8_t> _raw_payload_from_cio = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
};
