#pragma once
#include <Arduino.h>
#include "enums.h"
#include "DSP_BASE.h"

/*
 * Virtual display layer for the MSPA WifiWhirl PCB.
 *
 * The board will carry its own display + buttons, but driving that hardware is
 * deferred. This layer already computes what the panel should show from the
 * pump's reported state (BWC::loop() feeds dsp_states from the CIO each loop),
 * so wiring a real panel later is a drop-in. Inherits DSP directly - the SPI
 * layer DSP_6W does not apply.
 */
class DSP_MSPA : public DSP
{
public:
    DSP_MSPA(){};
    virtual ~DSP_MSPA(){};
    void setup(int, int, int, int) override {} // no panel hardware yet
    void stop() override {}
    void pause_all(bool) override {}
    void updateToggles() override;
    void handleStates() override;
};
