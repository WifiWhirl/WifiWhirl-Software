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

// DSP Includes
#include "DSP_2021.h"
#include "DSP_2021HJT.h"
#include "DSP_PRE2021.h"

constexpr int MAXCOMMANDS = 20;

struct command_que_item
{
    Commands cmd;
    int64_t val;
    uint64_t xtime;
    uint32_t interval;
    String text = "";
    bool force = false;  // When true, bypass safety checks (e.g., allow pump off while heater runs)
};

// Smart Schedule structure for predictive heating
struct smart_schedule_t
{
    bool active = false;                // Is schedule currently active
    uint64_t target_time = 0;           // When pool should be ready (Unix timestamp)
    uint8_t target_temp = 37;           // Desired temperature in Celsius
    bool keep_heater_on = false;        // Keep heating after target reached
    uint64_t calculated_start_time = 0; // Calculated heating start time
    uint64_t next_check_time = 0;       // Next check timestamp
    float last_heating_estimate = 0.0f; // Last calculated heating time in hours
    uint8_t temp_reading_state = 0;     // 0=idle, 1=pump_on, 2=waiting, 3=reading
    uint64_t temp_reading_timer = 0;    // Timer for temperature reading sequence
    bool original_pump_state = false;   // Store original pump state before reading
    uint8_t accurate_temperature = 0;   // Temperature read after pump circulation
    bool check_completed = false;       // True when all periodic checks are done (heating will start soon)
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
    // bool qCommand(int64_t cmd, int64_t val, int64_t xtime, int64_t interval);
    bool newData();
    bool getWeather();
    void getJSONStates(String &rtn);
    void getJSONTimes(String &rtn);
    void getJSONSettings(String &rtn);
    void setJSONSettings(const String &message);
    String getJSONCommandQueue();
    uint8_t getState(int state);
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
    bool setSmartSchedule(uint64_t target_time, uint8_t target_temp, bool keep_heater_on);
    void cancelSmartSchedule();
    void getJSONSmartSchedule(String &rtn);

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
    bool hasTempSensor = false;
    int tempSensorPin;
    bool enableWeather = false;

private:
    bool _loadHardware(Models &cioNo, Models &dspNo, int pins[]);
    bool _handlecommand(Commands cmd, int64_t val, const String &txt);
    void _handleCommandQ();
    void _loadSettings();
    void _saveCommandQueue();
    void _updateTimes();
    void _restoreStates();
    void _saveStates();
    float _estHeatingTime();
    void _handleStateChanges();
    void _handleNotification();
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
    float _calculateHeatingTime(uint8_t current_temp, uint8_t target_temp);
    void _loadSmartSchedule();
    void _saveSmartSchedule();
    bool _save_smartschedule_needed = false;

private:
    bool _notify;
    int _notification_time, _next_notification_time;
    Ticker _save_settings_ticker;
    Ticker _scroll_text_ticker;
    bool _scroll = false;
    uint64_t _timestamp_secs; // seconds
    uint8_t _dsp_brightness;
    int16_t _override_dsp_brt_timer;
    std::vector<command_que_item> _command_que;
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
    uint16_t _last_alk_value;      // Last alkalinity value in ppm (e.g. 100 = 100 ppm)
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
    float _price;
    uint32_t _cl_interval;
    uint32_t _filter_interval;
    uint32_t _fc_interval;
    uint32_t _wc_interval;
    uint32_t _ph_interval;         // pH check interval in days
    bool _audio_enabled;
    float _energy_total_kWh;
    double _energy_daily_Ws; // Wattseconds internally
    int _energy_power_W;
    String _plz;
    bool _weather = 0;
    int _pool_capacity = 700;
    bool _save_settings_needed = false;
    bool _save_cmdq_needed = false;
    bool _save_states_needed = false;
    int _ticker_count;
    int _btn_sequence[4] = {NOBTN, NOBTN, NOBTN, NOBTN}; // keep track of the four latest button presses
    int _ambient_temp;               // always in C internally
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
    unsigned long _pump_change_timestamp_ms, _bubbles_change_timestamp_ms;
    int _deltatemp;
    
    // Smart Schedule state
    smart_schedule_t _smart_schedule;
};

void save_settings_cb(BWC *bwcInstance);

void scroll_text_cb(BWC *bwcInstance);
