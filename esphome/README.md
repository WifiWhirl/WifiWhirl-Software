# ESPHome port (work in progress)

Goal: make WifiWhirl usable as a *native* ESPHome device (no separate WifiWhirl web UI / MQTT discovery required), so Home Assistant can talk to it via the ESPHome API.

## Hardware (tested baseline)

This matches the official WifiWhirl BOM and wiring concept (man-in-the-middle between pump control unit and display panel):

- Pump model: **S100101 / P05326 (6‑pin)**
- MCU: **NodeMCU v3 (ESP8266)**
- Level shifter: **TXS0108E** (bidirectional 3.3V ↔ 5V)
- JST XH 6‑pin cable set (plug + socket)

## Architecture approach

WifiWhirl contains a proven low-level protocol implementation (`Code/lib/…`), built around:

- `BWC` state machine (`Code/lib/BWC_unified`)
- `CIO_*` (pump/control-unit side) + `DSP_*` (display-panel side)
- tight GPIO interrupt handling (`attachInterrupt` on CS/CLK)

For an ESPHome port we will:

1) **Reuse the low-level CIO/DSP/BWC logic** as a library inside an ESPHome external component.
2) Expose the state as ESPHome entities:
   - `climate` (current temp + target temp + heat/off)
   - `switch` (pump/filter, heater, air bubbles, hydrojets, power, lock)
   - `number` (target temp, brightness, ambient temp, water chemistry values)
   - `sensor` (temp/target/ambient, power/energy, uptime timers, error code, etc.)
3) Drop/ignore WifiWhirl-specific features that ESPHome already provides:
   - WiFiManager captive portal
   - MQTT discovery + custom topics
   - internal web UI / webhooks

## Notes / caveats

- This is timing-sensitive logic. ESPHome logging must be kept low to avoid jitter.
- ESP8266 resources are tight; an ESP32 variant is possible but we’ll start with ESP8266 for compatibility with the BOM.

## Next steps

- Create `external_components/wifiwhirl` component (C++ + Python codegen)
- Map commands (SETTARGET, SETHEATER, SETPUMP, SETBUBBLES, SETJETS, …) to entity callbacks
- Provide a reference YAML for **S100101/P05326 6‑pin**
