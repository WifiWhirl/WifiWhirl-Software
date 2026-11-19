// Shared internals of the BWC class, which is split over several bwc_*.cpp
// files by topic. Only for those files, not a public header.
#pragma once

#include <StreamString.h>
#include "bwc.h"
#include "util.h"
#include "pitches.h"
#include <algorithm>
#include <cstdlib>
#ifndef ESP8266
#include "esp_attr.h" // RTC_NOINIT_ATTR
#endif

extern const char *defaultTimezone;
extern const char *defaultTimezoneName;

namespace
{
constexpr uint64_t VALID_TIME_THRESHOLD_S = 57600;
constexpr float HEATER_EFFICIENCY = 0.99f;
constexpr size_t COMMAND_QUEUE_JSON_CAPACITY = 4096;
// Dosing factors for the care-product suggestion, grams per m3 of water:
// pH-/pH+ per 0.1 pH, chlorine granules per 1 mg/l, TA+/TA- per 10 mg/l.
// Defaults: Bayrol SpaTime granules (pH 10 g, Alca-Plus 20 g; chlorine from its
// active ingredient dichloroisocyanurate ~56 % -> ~2 g; TA- = pH-Minus
// (bisulfate) ~24 g). Users adapt them to their own product's label.
static const char DF0[] PROGMEM = "DFPHM";
static const char DF1[] PROGMEM = "DFPHP";
static const char DF2[] PROGMEM = "DFCL";
static const char DF3[] PROGMEM = "DFTAP";
static const char DF4[] PROGMEM = "DFTAM";
static const char *const DOSE_FACTOR_KEYS[BWC::DOSE_FACTORS] PROGMEM = {DF0, DF1, DF2, DF3, DF4};
static const uint8_t DOSE_FACTOR_DEFAULTS[BWC::DOSE_FACTORS] = {10, 10, 2, 20, 24};
#define DF_KEY(i) FPSTR((PGM_P)pgm_read_ptr(&DOSE_FACTOR_KEYS[i]))

inline float displayTempC(uint8_t unit, float temp)
{
    return unit == 0 ? F2C(temp) : temp;
}

// Current water temperature in Celsius (cio_states.temperature is in the display
// unit; unit==0 means Fahrenheit). Float math keeps sub-degree resolution.
inline float waterTempC(CIO *cio)
{
    return displayTempC(cio->cio_states.unit, (float)cio->cio_states.temperature);
}

inline bool isJsonString(const JsonVariantConst &value)
{
    return value.is<const char *>() || value.is<String>();
}

// Energy prices are edited in cent steps everywhere in the UI, so keep them on
// that grid instead of carrying float noise (0.349999994) around.
inline double roundCents(double value)
{
    return round(value * 100.0) / 100.0;
}

}

// defined once in bwc.cpp
float heatingHours(float current_temp_c, float target_temp_c, int heaterPwr,
                   float heatLoss, int ambTemp, int poolCapacity);

// Next run of a repeating command strictly after `now` (or at `now` if
// allow_now). Whole-day intervals step in local time, so "daily 08:00" stays
// 08:00 across DST changes instead of drifting by an hour.
uint64_t nextRunAfter(uint64_t xtime, uint32_t interval, uint64_t now, bool allow_now);
