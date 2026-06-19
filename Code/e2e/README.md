# WifiWhirl real-module E2E tests

These Playwright tests run against a real WifiWhirl module on your network.
They are intentionally read-only or UI-only and do not click pump, heater, or
Airjet controls. This is important when the pump has no pool connected.

## Requirements

- Docker
- A reachable WifiWhirl module IP address
- The computer running Docker must be on the same network as the module

## Run with Docker

Build the test image from this folder:

```sh
cd e2e
docker build -t wifiwhirl-e2e .
```

Rebuild this image after changing the tests. Docker runs the files copied into
the image at build time, so old failure line numbers usually mean an old image
is still being used.

Run the tests by passing the module address before execution:

```sh
docker run --rm -e MODULE_URL=http://192.168.178.42 wifiwhirl-e2e
```

`MODULE_URL` may include or omit `http://`.

```sh
docker run --rm -e MODULE_URL=192.168.178.42 wifiwhirl-e2e
```

The suite is configured for the ESP8266's limited single-core CPU:

- one Playwright worker only
- high navigation/action/assertion timeouts
- a pause after every mutating request
- retries for transient read requests such as short `socket hang up` resets

The default post-write pause is 1500 ms. Increase it if your module feels busy:

```sh
docker run --rm \
  -e MODULE_URL=http://192.168.178.42 \
  -e ESP_SETTLE_MS=3000 \
  wifiwhirl-e2e
```

Target temperature, display brightness, and lock changes need more time because
they go through the pump/display command path. The tests wait 10000 ms after
each of these commands before checking the result. Increase this separately if
your module or pump reacts more slowly:

```sh
docker run --rm \
  -e MODULE_URL=http://192.168.178.42 \
  -e ESP_HARDWARE_SETTLE_MS=15000 \
  wifiwhirl-e2e
```

Those hardware-facing checks are skipped if the module accepts the command but
the attached pump/display does not report the changed state after the extended
wait. That keeps the suite useful on real modules where a specific pump/display
command is unavailable, delayed, or refused, while still restoring values that
were actually changed.

Read requests are retried 3 times by default. Increase this if the module drops
connections while busy:

```sh
docker run --rm \
  -e MODULE_URL=http://192.168.178.42 \
  -e ESP_REQUEST_RETRIES=5 \
  wifiwhirl-e2e
```

## Run locally without Docker

```sh
cd e2e
npm install
MODULE_URL=http://192.168.178.42 npm test
```

## Reports and artifacts

On failure, Playwright writes traces, screenshots, videos, and HTML reports.
These files are ignored by `e2e/.gitignore`.

To keep artifacts from a Docker run, mount a local directory:

```sh
docker run --rm \
  -e MODULE_URL=http://192.168.178.42 \
  -v "$PWD/test-results:/tests/test-results" \
  -v "$PWD/playwright-report:/tests/playwright-report" \
  wifiwhirl-e2e
```

## Test coverage

The suite currently lives in `tests/read-only.spec.js`.

| Test name | What it checks | Writes to module |
| --- | --- | --- |
| `read-only backend endpoints respond with expected shapes` | Calls `/gettemps/`, `/getconfig/`, `/getsmartschedule/`, and `/getcommands/`; verifies expected JSON fields and basic types. | No |
| `read-only configuration endpoints expose valid JSON` | Calls `/getwebconfig/`, `/getmqtt/`, `/getwifi/`, and `/gethardware/`; verifies representative config keys exist. | No |
| `temperature endpoint supports field filtering` | Calls `/gettemps/?currentC&targetC`; verifies only requested temperature fields are returned. | No |
| `polling fallback returns states, times, and other info` | Calls `/getpolldata/`; verifies it returns `[STATES, TIMES, OTHER]` with representative fields such as target temperature, brightness, uptime, firmware, and IP. | No |
| `webhook status endpoints expose safe read-only state` | Calls webhook read endpoints `/getstates/` and `/gettemps/`; verifies state and temperature JSON fields and types. | No |
| `webhook endpoints enforce GET and reject invalid hook payloads` | Verifies `/hook/`, `/getstates/`, and `/gettemps/` reject POST with `405`; verifies `/hook/` rejects missing or invalid `send` JSON with `400`. | No saved state should change |
| `webhook can schedule and clean up a safe future text command` | Calls `/hook/?send=...` with a future `PRINTTEXT` command, verifies it appears in the queue, then deletes it. Skips if the queue is full. | Yes, one temporary future text command |
| `sendcommand fallback can schedule and clean up a safe future text command` | Calls `/sendcommand/` with a future `PRINTTEXT` command, verifies it appears in the queue, then deletes it. Skips if the queue is full. | Yes, one temporary future text command |
| `command queue can download and rejects invalid restore payload` | Downloads queue JSON through `/cmdq_file/`; posts invalid JSON to upload and expects a clean `400` error. | Only sends invalid upload; no saved state should change |
| `command queue restore rejects malformed structures` | Uploads invalid queue JSON variants: missing `LEN`, array length mismatch, more than 20 commands, and invalid `TXT` shape. Verifies each is rejected. | No saved state should change |
| `smart schedule API rejects invalid updates without creating schedules` | Posts invalid Smart Schedule payloads: past target time, invalid target temperature, and missing `KEEPON` update. Verifies clean rejection. | No saved state should change |
| `target temperature and brightness can be changed and restored` | Uses `/sendcommand/` to set a temporary target temperature and display brightness, waits for the real pump/display path, verifies via `/getpolldata/`, then restores values that changed. Skips if the queue is full or the module accepts the command but does not report the changed state. | Yes, temporary target/brightness changes |
| `lock can be toggled and restored` | Uses `/sendcommand/` to toggle lock, waits for the real pump/display path, verifies via `/getstates/`, then toggles back to the original state. Skips if the queue is full or the module accepts the command but does not report the changed state. | Yes, temporary lock toggle |
| `safe future text command can be added, edited, and deleted` | Adds a future `PRINTTEXT` command, verifies it appears, edits it, verifies the edit, deletes it, and verifies cleanup. Skips if the queue is full. | Yes, one temporary future text command |
| `dashboard and navigation pages load without unsafe control actions` | Opens dashboard, config, automation, Smart Schedule, web, WiFi, hardware, and info pages; verifies page shell/header. | No |
| `smart schedule exposes cost estimate and editable heater target behavior` | Checks Smart Schedule cost UI and heater-behavior select/options exist. | No |
| `smart schedule client-side validation blocks unsafe submissions` | Calls `setSchedule()` with missing date, invalid temp, and past date; verifies browser validation alerts prevent submission. | No |
| `automation UI uses the safe Bedientasten sperren wording` | Verifies command option `26` label and form labels read `Bedientasten sperren`, `sperren`, and `entsperren`. | No |
| `automation form serializes safe commands without hardware command IDs` | Checks client-side JSON for command `26` and `PRINTTEXT`; does not send it to the module. | No |
| `dark mode toggle persists locally without module writes` | Toggles dark mode in browser localStorage, reloads, verifies persistence, then turns it off. | No |
| `web config localStorage controls dashboard section visibility without module writes` | Intercepts web-config API calls in the browser, saves UI section visibility to localStorage, and verifies dashboard section hiding without writing real module config. | No |
| `mobile navigation menu opens and closes` | Uses a mobile viewport and verifies the hamburger nav toggles responsive state. | No |
| `core pages do not overflow on desktop or mobile viewports` | Opens core pages on mobile and desktop viewport sizes; verifies no horizontal document overflow. | No |
| `static assets and manifest are served` | Fetches `/manifest.json`, `/main.css`, `/function.js`, and `/logo.png`; verifies they are served. | No |
| `system info assets and prometheus metrics are served` | Fetches `/info.html`, favicon, fonts, melodies, and `/metrics`; verifies successful responses and Prometheus metric text. | No |
| `configuration pages use alternating section styling` | Opens config-related pages and verifies at least one `section.odd-section` exists. | No |

## Safety scope

The tests may:

- read current module state and configuration
- navigate UI pages
- change browser-local state such as dark mode
- create, edit, and delete one temporary future `PRINTTEXT` queue entry
- temporarily change and restore target temperature, display brightness, and lock state

The tests do not:

- turn on/off the pump
- turn on/off the heater
- turn on/off Airjet or Hydrojet
- restore or reset the automation queue
- save configuration changes
