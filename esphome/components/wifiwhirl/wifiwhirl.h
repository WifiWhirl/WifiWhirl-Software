#pragma once

// Umbrella header for the ESPHome WifiWhirl external component.
//
// ESPHome codegen will include this file so that all entities are visible.

#include "wifiwhirl_component.h"

// Only include platform headers when the corresponding ESPHome component is enabled.
// This prevents build errors when a user config doesn't include e.g. switch/number/etc.
#ifdef USE_SENSOR
#include "wifiwhirl_sensors.h"
#endif

#ifdef USE_TEXT_SENSOR
#include "wifiwhirl_text_sensors.h"
#endif

#ifdef USE_SWITCH
#include "wifiwhirl_switches.h"
#endif

#ifdef USE_NUMBER
#include "wifiwhirl_numbers.h"
#endif

#ifdef USE_SELECT
#include "wifiwhirl_selects.h"
#endif

#ifdef USE_BUTTON
#include "wifiwhirl_buttons.h"
#endif

#ifdef USE_CLIMATE
#include "wifiwhirl_climate.h"
#endif
