#include "bwc_internal.h"

/* SOUND */

/*temporary function to render some soundfiles*/
// void BWC::_save_melody(const String& filename)
// {
//     File file = LittleFS.open(filename, "w");
//     if (!file) return;
//     sNote n = {1000, 500};
//     file.write((byte*)&n, sizeof(n));
//     file.close();
// }

#ifndef SND_PITCH_HZ
#define SND_PITCH_HZ 1500
#endif
bool BWC::_load_melody_json(const String &filename)
{
    if (_notes.size() || !_audio_enabled)
    {
        // Serial.println("Q busy");
        return false;
    }
    // experiment: "mel:<beat> <duty> <freq> <dur> ..." plays an inline melody, no file needed
    StreamString inl;
    File file;
    if (filename.startsWith(F("mel:")))
    {
        String m = filename.substring(4);
        m.replace(' ', '\n');
        inl.print(m + '\n'); // trailing newline: readStringUntil would wait out its 1 s timeout on the last value
        inl.setTimeout(0);
    }
    else
    {
        file = LittleFS.open(filename, "r");
        if (!file)
        {
            Serial.println(F("FS: Failed to open melody file for read"));
            return false;
        }
    }
    Stream &in = file ? (Stream &)file : (Stream &)inl;
    int beat_period;
    float note_duty_cycle;
    sNote n;

    /*new file format:
    beat period
    note duty cycle
    frequency
    duration (fraction of beat_period)
    frequency
    duration
    ...eof
    */
    String s = in.readStringUntil('\n');
    beat_period = s.toInt();
    s = in.readStringUntil('\n');
    note_duty_cycle = s.toFloat();
    while (in.available())
    {
        s = in.readStringUntil('\n');
        // build-time pitch test, every tone is written around 4 kHz and
        // shifted by SND_PITCH_HZ / 4000. Drop the define once the pitch is chosen.
        n.frequency_hz = (uint32_t)s.toInt() * SND_PITCH_HZ / 4000;
        s = in.readStringUntil('\n');
        n.duration_ms = beat_period * s.toFloat();
        n.duration_ms *= note_duty_cycle;
        _notes.push_back(n);
        /*add a little break between the notes (will be placed before each note due to reversing)*/
        n.frequency_hz = 0;
        n.duration_ms = beat_period * s.toFloat();
        n.duration_ms *= (1 - note_duty_cycle);
        _notes.push_back(n);
    }

    std::reverse(_notes.begin(), _notes.end());
    file.close();

    return true;
}

// void BWC::_add_melody(const String &filename)
// {
//     if(_notes.size() || !_audio_enabled) return;
//     File file = LittleFS.open(filename, "r");
//     if (!file) return;
//     while(file.available())
//     {
//         sNote n;
//         file.readBytes((char*)&n, sizeof(n));
//         _notes.push_back(n);
//     }
//     file.close();
//     /* We read and erase from the back of the vector (faster) so if notes are stored in the natural order we need to reverse*/
//     std::reverse(_notes.begin(), _notes.end());
// }

// Sound identity: "beat_ms duty freq beats freq beats ...", same format as the .mel
// files in Sound/identity/ (render.py turns them into WAVs to listen to).
// Everything sits around 4 kHz like the original pump beep; BOOT is
// jingle 4 "entspannt".
static const char MEL_BOOT[] PROGMEM = "150 0.95 5274 2 4186 2 3136 2 3520 2 0 1 3136 1 4186 6";
static const char MEL_CONNECTED[] PROGMEM = "110 0.85 3520 1 4000 1 4186 3";
static const char MEL_CLICK[] PROGMEM = "60 1 4000 1";
static const char MEL_CONFIRM[] PROGMEM = "70 0.85 3951 1 4186 1";
static const char MEL_ERROR[] PROGMEM = "160 0.8 4000 1 3520 1 0 0.5 3520 2";
static const char MEL_READY[] PROGMEM = "140 0.85 3520 1 3951 1 4186 1 3951 1 4186 1 4000 3";
static const char MEL_REMINDER[] PROGMEM = "120 0.6 4000 1 0 1 4000 1";
static const char MEL_UP[] PROGMEM = "45 0.9 3520 1 3951 1 4186 1";
static const char MEL_DOWN[] PROGMEM = "45 0.9 4186 1 3951 1 3520 1";
static const char MEL_UNLOCK[] PROGMEM = "70 0.85 4186 1 3951 1"; // MEL_CONFIRM falling
static const char *const MELODIES[] PROGMEM = {MEL_BOOT, MEL_CONNECTED, MEL_CLICK, MEL_CONFIRM, MEL_ERROR,
                                               MEL_READY, MEL_REMINDER, MEL_UP, MEL_DOWN,
                                               MEL_UNLOCK};

void BWC::playSound(Sound s, bool interrupt)
{
    const uint8_t bit = s == SND_UNLOCK ? SND_CONFIRM : s; // one switch for lock and unlock
    if (!_audio_enabled || s >= sizeof(MELODIES) / sizeof(MELODIES[0]) || !(_sound_mask & (1 << bit)))
        return;
    if (_notes.size())
    {
        if (!interrupt)
            return;
        _notes.clear(); // events win over whatever is playing
    }
    _note_duration = 0;
    _load_melody_json(String(F("mel:")) + FPSTR((const char *)pgm_read_ptr(&MELODIES[s])));
}

/** Event sounds: error (every 10 s), target reached, maintenance timer due. */
void BWC::_soundEvents()
{
    uint32_t now = millis();
    if (_queued_sound >= 0 && !_notes.size())
    {
        playSound((Sound)_queued_sound);
        _queued_sound = -1;
    }

    if (cio->cio_states.error)
    {
        // waits until nothing else plays or waits (boot, connected), then every 10 s
        if ((!_error_sound_ms || now - _error_sound_ms >= 10000) && !_notes.size() && _queued_sound < 0)
        {
            _error_sound_ms = now | 1;
            playSound(SND_ERROR);
        }
    }
    else
        _error_sound_ms = 0;

    // Ready once per heating session: armed when the heater goes on or the target
    // changes, so the thermostat switching red/green while holding stays quiet.
    // Not armed in the first 30 s, the boot restore switches a running heater on again.
    if (now > 30000 && cio->cio_states.heat &&
        (!_prev_cio_states.heat || cio->cio_states.target != _prev_cio_states.target))
        _ready_armed = true;
    if (!cio->cio_states.heat)
        _ready_armed = false;
    if (_ready_armed && cio->cio_states.heatgrn && !_prev_cio_states.heatgrn)
    {
        _ready_armed = false;
        playSound(SND_READY);
    }

    // Maintenance timers, once when one becomes due (checked every minute, needs NTP)
    if (now - _reminder_check_ms >= 60000 || _due_mask < 0)
    {
        _reminder_check_ms = now;
        if (_timestamp_secs < 1600000000)
            return;
        const uint32_t ts[] = {_cl_timestamp_s, _filter_timestamp_s, _fc_timestamp_s, _wc_timestamp_s, _ph_timestamp_s};
        const uint32_t days[] = {_cl_interval, _filter_interval, _fc_interval, _wc_interval, _ph_interval};
        int8_t mask = 0;
        for (int i = 0; i < 5; i++)
            if (ts[i] && days[i] && (uint64_t)_timestamp_secs >= (uint64_t)ts[i] + (uint64_t)days[i] * 86400ULL)
                mask |= 1 << i;
        if (_due_mask >= 0 && (mask & ~_due_mask))
            playSound(SND_REMINDER);
        _due_mask = mask;
    }
}

void BWC::_beep()
{
    if (_notes.size() || !_audio_enabled)
        return;
    sNote n;
    n.duration_ms = 50;
    n.frequency_hz = 2400;
    _notes.push_back(n);
    n.duration_ms = 50;
    n.frequency_hz = 800;
    _notes.push_back(n);
}

void BWC::_accord()
{
    if (_notes.size() || !_audio_enabled)
        return;
    sNote n;
    for (int i = 0; i < 5; i++)
    {
        n.duration_ms = 10;
        n.frequency_hz = NOTE_C6;
        _notes.push_back(n);
        n.duration_ms = 10;
        n.frequency_hz = NOTE_E6;
        _notes.push_back(n);
    }
}
