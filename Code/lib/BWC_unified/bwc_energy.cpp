#include "bwc_internal.h"

namespace
{
// Energy and runtime counters accumulate continuously but only hit flash every
// 600s (and on graceful restart). A soft-WDT/exception reboot loses everything
// since the last save, so the published total/daily jump backwards then climb
// again (HA sawtooth) and the day ends up under-counted; the runtime hour-meters
// lose the same way. RTC memory survives a reset (not a power loss) and costs no
// flash wear, so mirror the live values there every loop and restore on boot.
// crashtrace.cpp owns its own RTC slot; this is separate. NOTE: not power-loss
// persistent by design - a cold boot falls back to flash, which is correct for
// an intentional power-down.
constexpr uint32_t COUNTERS_RTC_MAGIC = 0xE2701EC1;
struct CountersRtc
{
    uint32_t magic;
    double total_kWh;
    double cost_total;
    double daily_Ws;
    double pending_Ws;
    double daily_cost;
    double pending_cost;
    int32_t yday;
    uint32_t uptime_s;
    uint32_t pumptime_s;
    uint32_t heatingtime_s;
    uint32_t airtime_s;
    uint32_t jettime_s;
    uint32_t crc;
};
uint32_t countersRtcCrc(const CountersRtc &e)
{
    const uint32_t *p = reinterpret_cast<const uint32_t *>(&e);
    uint32_t c = 0;
    for (size_t i = 0; i < (sizeof(CountersRtc) - sizeof(uint32_t)) / 4; ++i)
        c = c * 31u + p[i];
    return c;
}

#ifdef ESP8266
constexpr uint32_t COUNTERS_RTC_OFFSET = 96; // 4-byte block offset into RTC user mem (512B total), past crashtrace's 64..95
void countersRtcStore(const CountersRtc &e) { ESP.rtcUserMemoryWrite(COUNTERS_RTC_OFFSET, (uint32_t *)&e, sizeof(e)); }
bool countersRtcFetch(CountersRtc &e) { return ESP.rtcUserMemoryRead(COUNTERS_RTC_OFFSET, (uint32_t *)&e, sizeof(e)); }
#else
RTC_NOINIT_ATTR static CountersRtc s_countersRtc; // survives reset, wiped on power loss
void countersRtcStore(const CountersRtc &e) { s_countersRtc = e; }
bool countersRtcFetch(CountersRtc &e) { e = s_countersRtc; return true; }
#endif
}

bool BWC::_getEnergyDayIndex(int &day_index) const
{
    if (_timestamp_secs <= VALID_TIME_THRESHOLD_S)
        return false;

    time_t ts = (time_t)_timestamp_secs;
    struct tm timeinfo;
    localtime_r(&ts, &timeinfo);
    day_index = timeinfo.tm_yday + timeinfo.tm_year * 366;
    return true;
}

// Tariff-aware price lookup. Falls back to the day price while the clock is
// invalid (short boot transients) so no cost is ever lost.
double BWC::_priceAt(uint64_t ts) const
{
    if (!_night_enabled || ts <= VALID_TIME_THRESHOLD_S)
        return _price;
    time_t t = (time_t)ts;
    struct tm ti;
    localtime_r(&t, &ti);
    if (_night_weekend && (ti.tm_wday == 0 || ti.tm_wday == 6))
        return _price_night;
    int m = ti.tm_hour * 60 + ti.tm_min;
    bool night = (_night_start_min <= _night_end_min)
        ? (m >= _night_start_min && m < _night_end_min)
        : (m >= _night_start_min || m < _night_end_min); // window wraps midnight
    return night ? _price_night : _price;
}

// Average tariff over a heating window, sampled in 15-min steps (capped; this
// is an estimate, not billing).
double BWC::_avgPriceOver(uint64_t start_ts, float hours) const
{
    if (!_night_enabled || start_ts <= VALID_TIME_THRESHOLD_S || hours <= 0)
        return _priceAt(start_ts);
    uint32_t steps = (uint32_t)ceilf(hours * 4);
    if (steps > 960) steps = 960; // >10 days: coarser sampling is fine
    uint64_t step_s = (uint64_t)(hours * 3600.0f / steps);
    double sum = 0;
    for (uint32_t i = 0; i < steps; i++)
        sum += _priceAt(start_ts + i * step_s);
    return sum / steps;
}

void BWC::_addEnergyIncrement(double energy_increment_Ws)
{
    double cost_increment = (energy_increment_Ws / 3600000.0) * _priceAt(_timestamp_secs);
    _energy_total_kWh += energy_increment_Ws / 3600000.0;
    _energy_cost_total += cost_increment;

    int today;
    if (!_getEnergyDayIndex(today))
    {
        _energy_pending_daily_Ws += energy_increment_Ws;
        _energy_pending_daily_cost += cost_increment;
        return;
    }

    if (_energy_daily_yday < 0)
    {
        _energy_daily_yday = today;
    }
    else if (today != _energy_daily_yday)
    {
        _energy_daily_Ws = 0;
        _energy_daily_cost = 0;
        _energy_daily_yday = today;
    }

    if (_energy_pending_daily_Ws > 0)
    {
        _energy_daily_Ws += _energy_pending_daily_Ws;
        _energy_daily_cost += _energy_pending_daily_cost;
        _energy_pending_daily_Ws = 0;
        _energy_pending_daily_cost = 0;
    }

    _energy_daily_Ws += energy_increment_Ws;
    _energy_daily_cost += cost_increment;
}

void BWC::_updateTimes()
{
    uint32_t now = millis();
    static uint32_t prevtime = now;
    uint32_t elapsedtime_ms = now - prevtime;
    prevtime = now;
    if (cio->cio_states.heatred)
    {
        _heatingtime_ms += elapsedtime_ms;
    }
    if (cio->cio_states.pump)
    {
        _pumptime_ms += elapsedtime_ms;
    }
    if (cio->cio_states.bubbles)
    {
        _airtime_ms += elapsedtime_ms;
    }
    if (cio->cio_states.jets)
    {
        _jettime_ms += elapsedtime_ms;
    }
    _uptime_ms += elapsedtime_ms;

    if (_uptime_ms > 1000000000)
    {
        _heatingtime += _heatingtime_ms / 1000;
        _pumptime += _pumptime_ms / 1000;
        _airtime += _airtime_ms / 1000;
        _jettime += _jettime_ms / 1000;
        _uptime += _uptime_ms / 1000;
        _heatingtime_ms %= 1000;
        _pumptime_ms %= 1000;
        _airtime_ms %= 1000;
        _jettime_ms %= 1000;
        _uptime_ms %= 1000;
    }

    if (_override_dsp_brt_timer > 0)
        _override_dsp_brt_timer -= (int32_t)elapsedtime_ms;

    int power_W = cio->cio_states.heatred * cio->getPower().HEATERPOWER;
    power_W += cio->cio_states.pump * cio->getPower().PUMPPOWER;
    power_W += cio->cio_states.bubbles * cio->getPower().AIRPOWER;
    power_W += cio->getPower().IDLEPOWER;
    power_W += cio->cio_states.jets * cio->getPower().JETPOWER;

    // The energy/tariff math (localtime, doubles) and the RTC mirror ran on every
    // loop() pass. Collect the time and book it every 250 ms, or right away when
    // the power changes, so each millisecond is still booked at the power that
    // was on during it.
    static uint32_t energy_acc_ms = 0;
    static int energy_acc_W = -1;
    if (energy_acc_W < 0)
        energy_acc_W = power_W;
    energy_acc_ms += elapsedtime_ms;
    _energy_power_W = power_W;
    if (power_W == energy_acc_W && energy_acc_ms < 250)
        return _playNotes(elapsedtime_ms);
    _addEnergyIncrement((static_cast<double>(energy_acc_ms) * energy_acc_W) / 1000.0);
    energy_acc_ms = 0;
    energy_acc_W = power_W;

    // Mirror the live counters to RTC so a crash-reboot doesn't lose them.
    // written with the energy booking above - RTC RAM, no flash wear; fold ms into
    // seconds so the snapshot is current (the second-counters only roll over
    // every ~11 days otherwise).
    CountersRtc snap{};
    snap.magic = COUNTERS_RTC_MAGIC;
    snap.total_kWh = _energy_total_kWh;
    snap.cost_total = _energy_cost_total;
    snap.daily_Ws = _energy_daily_Ws;
    snap.pending_Ws = _energy_pending_daily_Ws;
    snap.daily_cost = _energy_daily_cost;
    snap.pending_cost = _energy_pending_daily_cost;
    snap.yday = _energy_daily_yday;
    snap.uptime_s = _uptime + _uptime_ms / 1000;
    snap.pumptime_s = _pumptime + _pumptime_ms / 1000;
    snap.heatingtime_s = _heatingtime + _heatingtime_ms / 1000;
    snap.airtime_s = _airtime + _airtime_ms / 1000;
    snap.jettime_s = _jettime + _jettime_ms / 1000;
    snap.crc = countersRtcCrc(snap);
    countersRtcStore(snap);

    _playNotes(elapsedtime_ms);
}


// Recover counters lost between the last flash save and a crash-reboot. RTC is
// strictly newer than flash (written every loop vs every 600s), so prefer it
// when valid; max() guards the monotonic counters against any stale RTC. A
// power-loss cold boot has no valid RTC and falls through to the flash values.
void BWC::_restoreCountersFromRtc()
{
    CountersRtc e{};
    if (!countersRtcFetch(e) || e.magic != COUNTERS_RTC_MAGIC || e.crc != countersRtcCrc(e))
        return;

    _energy_total_kWh = std::max(_energy_total_kWh, e.total_kWh);
    _energy_cost_total = std::max(_energy_cost_total, e.cost_total);
    _energy_daily_Ws = e.daily_Ws;
    _energy_pending_daily_Ws = e.pending_Ws;
    _energy_daily_cost = e.daily_cost;
    _energy_pending_daily_cost = e.pending_cost;
    _energy_daily_yday = e.yday;
    _uptime = std::max(_uptime, e.uptime_s);
    _pumptime = std::max(_pumptime, e.pumptime_s);
    _heatingtime = std::max(_heatingtime, e.heatingtime_s);
    _airtime = std::max(_airtime, e.airtime_s);
    _jettime = std::max(_jettime, e.jettime_s);
    Serial.println(F("Restored counters from RTC"));
}

void BWC::_playNotes(uint32_t elapsedtime_ms)
{
    if (_notes.size())
    {
        // capped: a stalled loop() (end of setup, HA discovery) holds the note a bit
        // longer instead of skipping the whole melody in one step
        _note_duration += (int)(elapsedtime_ms < 50 ? elapsedtime_ms : 50);
        while (_notes.size() && _note_duration >= _notes.back().duration_ms)
        {
            _note_duration -= _notes.back().duration_ms;
            _notes.pop_back();
        }
        dsp->audiofrequency = _notes.size() ? _notes.back().frequency_hz : 0;
    }
    else
    {
        dsp->audiofrequency = 0;
    }
}
