#include "DSP_MSPA.h"

void DSP_MSPA::handleStates()
{
    // Build the three display digits from the pump's current temperature, e.g.
    // "2","8","c". dsp_states is populated from the CIO by BWC::loop().
    // TODO: push these out to the physical panel once its driver exists.
    uint8_t shown = dsp_states.temperature;
    dsp_states.char1 = '0' + (shown / 10) % 10;
    dsp_states.char2 = '0' + shown % 10;
    dsp_states.char3 = dsp_states.unit ? 'c' : 'f';
}

void DSP_MSPA::updateToggles()
{
    // No physical buttons wired yet - control comes via web/MQTT/REST.
    // Emit no toggles; EnabledButtons[] is preserved for settings round-trip.
    dsp_toggles = sToggles();
}
