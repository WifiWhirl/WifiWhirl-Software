#pragma once

#if defined(ESP8266)
#elif defined(ESP32)
#else
#error "This library supports 8266/32 only"
#endif

#include "Arduino.h"
// long long needed in arduino core v3+
#define ARDUINOJSON_USE_LONG_LONG 1
#include <ArduinoJson.h>
// #include "ESPDateTime.h"
#include <LittleFS.h>
#include <Ticker.h>
#include <vector>
#include "enums.h"
#include "util.h"

// CIO Includes
#include "CIO_2021.h"
#include "CIO_2021HJT.h"
#include "CIO_PRE2021.h"
#include "CIO_MSPA.h"

// DSP Includes
#include "DSP_2021.h"
#include "DSP_2021HJT.h"
#include "DSP_PRE2021.h"
#include "DSP_MSPA.h"

constexpr int MAXCOMMANDS = 20;

struct command_que_item
{
    Commands cmd;
    int64_t val;
    uint64_t xtime;
    uint32_t interval;
    String text = "";
    bool force = false;  // When true, bypass safety checks (e.g., allow pump off while heater runs)
    bool enabled = true;
    // True only for the user's planned automations (/addcommand/, restored cmdq.json).
    // Turning the queue off parks these; live control (button presses, MQTT) and
    // internal actions (airjet/hydrojet/host timeouts, smart schedule) keep running.
    bool scheduled = false;
};

// Smart Schedule structure for predictive heating
struct smart_schedule_t
{
    bool active = false;                // Is schedule currently active
    uint64_t target_time = 0;           // When pool should be ready (Unix timestamp)
    uint8_t target_temp = 37;           // Desired temperature in Celsius
    bool keep_heater_on = false;        // Keep heating after target reached
    uint8_t repeat_days = 0;            // 0 = one-shot, else re-arm every N calendar days (1=daily, 7=weekly)
    uint64_t calculated_start_time = 0; // Calculated heating start time
    uint64_t next_check_time = 0;       // Next check timestamp
    float last_heating_estimate = 0.0f; // Last calculated heating time in hours
    uint8_t temp_reading_state = 0;     // 0=idle, 1=pump_on, 2=reading
    uint64_t temp_reading_timer = 0;    // Timer for temperature reading sequence
    bool temp_reading_started_pump = false; // True only when Smart Schedule turned pump on for reading
    uint8_t accurate_temperature = 0;   // Temperature read after pump circulation
    bool check_completed = false;       // True when all periodic checks are done (heating will start soon)
    bool target_temp_reached = false;   // True once heater was stopped after reaching target temp
};

// Heat-loss calibration: measure W/K from how fast the water cools while idle.
// On start it turns the heater/AirJet/HydroJet off and the filter pump on
// (phase 0, prep+settle), then times the cool-down (phase 1, measure).
struct heatloss_cal_t
{
    bool active = false;        // calibration in progress
    bool cancel_requested = false; // set by the HTTP handler, acted on in loop()
    uint8_t phase = 0;          // 0 = prep/settle (controlling hardware), 1 = measuring
    time_t req_ts = 0;          // when start was requested (prep timeout)
    time_t prep_until = 0;      // settle countdown end (0 = not circulating yet)
    bool did_heat = false;      // off-toggles issued once each (avoid double-press)
    bool did_air = false;
    bool did_jet = false;
    bool did_pump = false;
    bool did_unit = false;      // F-switch toggle issued once (finer tick resolution)
    bool prev_heat = false;     // state before calibration, restored on finish/cancel
    bool prev_air = false;
    bool prev_jet = false;
    bool prev_pump = false;
    bool prev_unit = false;
    time_t start_ts = 0;        // measuring baseline timestamp
    float start_water_c = 0;    // water temp at measuring start (Celsius)
    int start_amb_c = 0;        // ambient temp at measuring start (Celsius)
    float amb_sum = 0;          // ambient mean over the measured window, so a
    uint32_t amb_n = 0;         // day/night swing doesn't bias dTavg
    time_t amb_last_ts = 0;     // last ambient sample timestamp
    float target_drop = 2.0f;   // finish once water fell this many Kelvin (one tick)
    int last_disp = -1;         // last raw display reading, -1 = uninitialized
    bool anchored = false;      // first downward tick flip seen
    time_t anchor_ts = 0;       // when the anchor flip happened
    float anchor_water_c = 0;   // reading at anchor (Celsius)
    time_t disturb_ts = 0;      // 0 = desired state held; else when it was lost
    time_t reassert_ts = 0;     // last corrective toggle press
    bool heat_ran = false;      // heatred seen during disturbance -> interval invalid
    uint16_t discarded = 0;     // measurement intervals thrown away (noise/disturbance)
    float result = 0;           // last computed coefficient (W/K), 0 = none
    uint8_t error = 0;          // last abort reason, 0 = none
};

class BWC
{

public:
    BWC();
    ~BWC();
    void setup(void);
    void begin();
    void on_save_settings();
    void on_scroll_text();
    void loop();
    void adjust_brightness();
    void play_sound();
    // String get_fromcio();
    // String get_todsp();
    // String get_fromdsp();
    // String get_tocio();
    void stop(void);
    void pause_all(bool action);
    bool add_command(command_que_item command_item);
    bool edit_command(uint8_t index, command_que_item command_item);
    bool del_command(uint8_t index);
    bool set_command_enabled(uint8_t index, bool enabled);
    void set_command_queue_enabled(bool enabled);
    // bool qCommand(int64_t cmd, int64_t val, int64_t xtime, int64_t interval);
    bool newData();
    bool getWeather();
    size_t writeJSONStates(Print &out);
    size_t writeJSONTimes(Print &out);
    void getJSONStates(String &rtn);
    void getJSONTimes(String &rtn);
    void getJSONSettings(String &rtn);
    bool setJSONSettings(const String &message, String &error);
    void getJSONCommandQueue(String &rtn);
    uint8_t getState(int state);
    // Full state snapshot for the cloud link: same compact keys as the local
    // UI JSON (states + times + smart schedule), one flat object.
    /** Cloud sensor payload. state_len_out (optional) receives the serialized
     *  length of the leading state section, which is what CloudTask hashes. */
    void buildCloudSnapshot(JsonObject doc, size_t* state_len_out = nullptr);
    // void saveSettingsFlag();
    void saveSettings();
    void reloadCommandQueue();
    void reloadSettings();
    void getButtonName(String &rtn);
    Buttons getButton();
    void saveDebugInfo(const String &s);
    void saveRebootInfo();
    bool getBtnSeqMatch();
    void setAmbientTemperature(int64_t amb, bool unit);
    int getAmbientTemperature();
    String getModel();
    void print(const String &txt);
    void printStatic(const String &txt);
    void clearStatic();
    void loadCommandQueue();
    
    // Smart Schedule methods
    bool setSmartSchedule(uint64_t target_time, uint8_t target_temp, bool keep_heater_on, uint8_t repeat_days = 0);
    bool updateSmartScheduleKeepHeaterOn(bool keep_heater_on);
    bool updateSmartScheduleRepeat(uint8_t repeat_days);
    void cancelSmartSchedule();
    void getJSONSmartSchedule(String &rtn);

    // Heat-loss calibration / coefficient (feeds the time-to-target estimate)
    bool startHeatLossCalibration(String &err);
    void cancelHeatLossCalibration();
    void getJSONHeatLossCalibration(String &rtn);
    float getHeatLoss() { return _heat_loss; }
    static float getDefaultHeatLoss() { return HEAT_LOSS_DEFAULT; }
    // Single source of truth for the valid W/K range (used by set/calibrate/load).
    static float clampHeatLoss(float v) { return v < 0.5f ? 0.5f : (v > 50.0f ? 50.0f : v); }
    void setHeatLoss(float v);

    // String getDebugData();

public:
    String reboot_time_str;
    time_t reboot_time_t;
    int pins[8];
    unsigned int loop_count = 0;
    bool hasjets, hasgod;
    CIO *cio;
    DSP *dsp;
    bool BWC_DEBUG = false;
    bool weatherEnabled() const { return _weather; }

private:
    bool _loadHardware(Models &cioNo, Models &dspNo);
    void _buildJSONStates(JsonObject doc);
    void _buildJSONTimes(JsonObject doc);
    bool _handlecommand(Commands cmd, int64_t val, const String &txt);
    void _handleCommandQ();
    void _loadSettings();
    void applyTimezone() const;
    bool _getEnergyDayIndex(int &day_index) const;
    void _addEnergyIncrement(double energy_increment_Ws);
    double _priceAt(uint64_t ts) const;
    double _avgPriceOver(uint64_t start_ts, float hours) const;
    void _restoreCountersFromRtc();
    void _saveCommandQueue();
    void _updateTimes();
    void _restoreStates();
    void _saveStates();
    float _estHeatingTime();
    void _handleStateChanges();
    static bool _compare_command(const command_que_item &i1, const command_que_item &i2);
    bool _load_melody_json(const String &filename);
    void _add_melody(const String &filename);
    void _save_melody(const String &filename);
    void _sweepdown();
    void _sweepup();
    void _beep();
    void _accord();
    void _log();
    
    // Smart Schedule private methods
    void _handleSmartSchedule();
    void _startAccurateTempReading();
    void _processAccurateTempReading();
    void _resetSmartScheduleState();
    void _rearmSmartSchedule(uint64_t next_target_time);
    float _calculateHeatingTime(float current_temp_c, float target_temp_c);
    void _loadSmartSchedule();
    void _saveSmartSchedule();
    bool _save_smartschedule_needed = false;

    // Heat-loss calibration
    void _handleHeatLossCalibration();
    void _anchorHeatLossCalibration(time_t now, float waterC);
    void _finishHeatLossCalibration(uint8_t error);
    void _restoreCalibrationState();
    static constexpr float HEAT_LOSS_DEFAULT = 7.5f;  // W/K, compiled fallback
    float _heat_loss = HEAT_LOSS_DEFAULT;             // live coefficient (persisted)
    heatloss_cal_t _hlcal;
    
    // Jet timeout handling
    void _checkJetTimeouts();
    void _checkHostHeaterTimeout();

private:
    Ticker _save_settings_ticker;
    Ticker _scroll_text_ticker;
    bool _scroll = false;
    uint64_t _timestamp_secs; // seconds
    uint8_t _dsp_brightness;
    int32_t _override_dsp_brt_timer;
    uint8_t _brightness_boost_steps = 1;        // 1-8, steps to add on key press
    uint16_t _brightness_boost_duration_s = 5;  // 1-60s, hold time after last key press
    std::vector<command_que_item> _command_que;
    bool _command_queue_enabled = true;
    bool _command_queue_paused_for_calibration = false;
    std::vector<sNote> _notes;
    int _note_duration;
    uint32_t _cl_timestamp_s;
    uint32_t _filter_timestamp_s;
    uint32_t _fc_timestamp_s;
    uint32_t _wc_timestamp_s;
    uint32_t _ph_timestamp_s;      // pH check timestamp
    uint32_t _clv_timestamp_s;     // Chlorine value check timestamp (separate from chlorine addition)
    uint32_t _cya_timestamp_s;     // Cyanuric acid check timestamp
    uint32_t _alk_timestamp_s;     // Alkalinity check timestamp
    uint16_t _last_ph_value;       // Last pH value * 10 (e.g. 72 = 7.2 pH)
    uint16_t _last_cl_value;       // Last chlorine value * 10 (e.g. 15 = 1.5 mg/L)
    uint16_t _last_cya_value;      // Last cyanuric acid value * 10 (e.g. 300 = 30.0 mg/L)
    uint16_t _last_alk_value;      // Last alkalinity value in mg/L (e.g. 100 = 100 mg/L)
    uint32_t _uptime;
    uint32_t _pumptime;
    uint32_t _heatingtime;
    uint32_t _airtime;
    uint32_t _jettime;
    uint32_t _uptime_ms;
    uint32_t _pumptime_ms;
    uint32_t _heatingtime_ms;
    uint32_t _airtime_ms;
    uint32_t _jettime_ms;
    double _price;
    bool _night_enabled = false;               // NIGHT_ENABLED: optional night tariff
    double _price_night = 0.35;                // PRICE_NIGHT
    uint16_t _night_start_min = 1320;          // NIGHT_START_MIN: minutes since midnight (22:00)
    uint16_t _night_end_min = 360;             // NIGHT_END_MIN: minutes since midnight (06:00)
    bool _night_weekend = false;               // NIGHT_WEEKEND: Sat/Sun entirely at night tariff
    double _energy_cost_total = 0.0;           // COST: accumulated cost, tariff-aware
    double _energy_daily_cost = 0.0;           // COSTD: today's cost, tariff-aware
    double _energy_pending_daily_cost = 0.0;   // cost accrued while clock invalid
    uint32_t _cl_interval;
    uint32_t _filter_interval;
    uint32_t _fc_interval;
    uint32_t _wc_interval;
    uint32_t _ph_interval;         // pH check interval in days
    bool _audio_enabled;
    double _energy_total_kWh;
    double _energy_daily_Ws;
    double _energy_pending_daily_Ws;
    int _energy_daily_yday = -1;
    int _energy_power_W;
    String _timezone;
    String _timezone_name;
    bool _weather = 0;
    int _pool_capacity = 700;
    uint8_t _airjet_timeout_minutes = 30;   // 5-30 min, default 30 (no software timeout)
    uint8_t _hydrojet_timeout_minutes = 60; // 5-60 min, default 60 (no software timeout)
    bool _host_mode = false;                    // GSTMODE: guest/host mode for rentals
    uint8_t _host_max_target_c = 40;            // GSTMAXTGT: max target temp, stored in Celsius
    uint16_t _host_heater_timeout_min = 0;      // GSTHTRTO: heater-off delay after target reached, 0 = disabled
    unsigned long _host_target_reached_ms = 0;  // 0 = countdown not armed
    bool _save_settings_needed = false;
    bool _save_cmdq_needed = false;
    bool _save_states_needed = false;
    int _ticker_count;
    int _btn_sequence[4] = {NOBTN, NOBTN, NOBTN, NOBTN}; // keep track of the four latest button presses
    int _ambient_temp;               // always in C internally
    // Whether anything ever set it (weather, cloud ack, command). Not persisted:
    // after a reboot the cloud gets null until the next weather poll or ack,
    // which is a minute at most and beats reporting a default as a measurement.
    bool _ambient_set = false;
    bool _new_data_available = false;
    bool _dsp_tgt_used = true;
    uint8_t _web_target = 20;
    bool _static_text_active = false;
    char _static_char1 = ' ';
    char _static_char2 = ' ';
    char _static_char3 = ' ';
    sStates _prev_cio_states, _prev_dsp_states;
    Buttons _prevbutton = NOBTN;
    unsigned long _temp_change_timestamp_ms, _heatred_change_timestamp_ms;
    unsigned long _pump_change_timestamp_ms, _bubbles_change_timestamp_ms, _jets_change_timestamp_ms;
    int _deltatemp;
    
    // Smart Schedule state
    smart_schedule_t _smart_schedule;
};

void save_settings_cb(BWC *bwcInstance);

void scroll_text_cb(BWC *bwcInstance);
