# ESPHome port (work in progress)

> **Upstream project:** https://github.com/WifiWhirl/WifiWhirl-Software  
> This repository contains an ESPHome-focused port/adapter layer. The original project is licensed under **GPL-3.0**, and this repo remains **GPL-3.0** as well.

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

## Quickstart: test flash (ESPHome)

This repo contains an **ESPHome external component** under:

- `esphome/components/wifiwhirl/`

### 1) Clone this repo next to your ESPHome YAML

Example layout:

```
/home/user/esphome/
  whirlpool.yaml
  WifiWhirl-Software/
    esphome/components/wifiwhirl/...
```

### 2) Use the example YAML

Start with:

- `esphome/wifiwhirl_s100101_example.yaml`

Key parts:

- `external_components` uses `type: local` (easy for private repos)
- `wifiwhirl:` configures the MITM pins and model
- `climate/switch/number/sensor` platforms expose entities directly to Home Assistant via the ESPHome API

### 3) Compile + flash

From ESPHome, run `install` / `run` for your node.

Important:
- Keep `logger:` at `WARN` (or lower) because the pump/display protocol is timing sensitive.

## What’s implemented (MVP)

- Native ESPHome API integration (no WifiWhirl web UI, no MQTT required)
- MITM mode kept intact → **physical display stays usable**
- Entities:
  - `climate` (off / heat / fan_only)
  - switches: power, lock, pump, heater, bubbles, jets
  - number: target temperature
  - sensors: temperature, target temperature, error code

## Next steps

- Expand entity coverage (energy/uptime/water chemistry/etc.)
- Add better model presets (pin mappings, capability flags)
- Add unit handling (°C/°F) + expose unit as a select
- Add optional safe-guards (e.g. block pump-off unless `force` is set)
