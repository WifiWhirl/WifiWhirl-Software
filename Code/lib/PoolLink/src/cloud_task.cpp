#include "cloud_task.h"
#include "cloud_crypto.h"
#include "cloud_protocol.h"
#include <ArduinoJson.h>
#include "bwc.h"
#include <time.h>
#include <math.h>
#include <cstring>

// Access the global BWC instance defined in main.cpp
extern BWC* bwc;

// ─── Reconnect backoff table ──────────────────────────────────────────────────
static const uint32_t BACKOFF_MS[] = {1000, 2000, 5000, 10000, 30000};

// ─── Static member definitions ───────────────────────────────────────────────
CloudState CloudTask::_state            = CLOUD_DISCONNECTED;
WiFiClient CloudTask::_client;
uint8_t    CloudTask::_session_key[POOLLINK_SESSION_KEY_SIZE] = {};
uint8_t    CloudTask::_psk[32]          = {};
uint8_t    CloudTask::_client_nonce[POOLLINK_NONCE_SIZE]      = {};
char       CloudTask::_hostname[POOLLINK_MAX_HOSTNAME + 1]    = {};
char       CloudTask::_host[64]         = {};
uint16_t   CloudTask::_port             = 8080;
uint64_t   CloudTask::_tx_seq           = 0;
uint64_t   CloudTask::_rx_seq_last      = 0;
uint32_t   CloudTask::_last_ping_ms          = 0;
uint32_t   CloudTask::_last_connect_attempt_ms = 0;
uint8_t    CloudTask::_backoff_index    = 0;
uint32_t   CloudTask::_connect_start_ms = 0;
uint32_t   CloudTask::_handshake_start_ms = 0;
bool       CloudTask::_hello_sent       = false;
bool       CloudTask::_notify_pending   = false;
bool       CloudTask::_force_resync     = false;
bool       CloudTask::_queue_dirty      = false;
bool       CloudTask::_settings_dirty   = false;
uint8_t    CloudTask::_rx_buf[POOLLINK_MAX_FRAME] = {};
size_t     CloudTask::_rx_buf_len       = 0;
char       CloudTask::_location[48]     = {0};
char       CloudTask::_firmware[16]     = {0};
char       CloudTask::_model[24]        = {0};

// Dedup cache: hash of the last state section sent, to suppress identical
// pushes. Only the state section. The runtime counters and the clock in the
// payload's tail change every second and would defeat the comparison.
static uint32_t _last_sensor_hash = 0;
static size_t   _last_sensor_len  = 0;   // length of the hashed section
static uint32_t _last_sensor_push_ms = 0;
// When a frame last actually went out. Separate from _last_sensor_push_ms,
// which also advances on a suppressed push: the keyframe has to be driven by
// real sends, or a spa whose state never changes would go silent forever.
static uint32_t _last_sensor_sent_ms = 0;

// ─── Public API ──────────────────────────────────────────────────────────────

void CloudTask::begin(const char* hostname, const uint8_t psk[32],
                      const char* host, uint16_t port)
{
    strncpy(_hostname, hostname, POOLLINK_MAX_HOSTNAME);
    _hostname[POOLLINK_MAX_HOSTNAME] = '\0';
    strncpy(_host, host, sizeof(_host) - 1);
    _host[sizeof(_host) - 1] = '\0';
    memcpy(_psk, psk, 32);
    _port = port;
    _state = CLOUD_DISCONNECTED;
    _backoff_index = 0;
    _last_connect_attempt_ms = 0;
    Serial.println(F("[Cloud] CloudTask initialized"));
}

void CloudTask::tick()
{
    switch (_state) {
    case CLOUD_DISCONNECTED:  _doConnect();    break;
    case CLOUD_CONNECTING:    _doConnecting(); break;
    case CLOUD_HANDSHAKING:   _doHandshake();  break;
    case CLOUD_CONNECTED:     _doConnected();  break;
    }
}

void CloudTask::notifyStateChanged()
{
    _notify_pending = true;
}

void CloudTask::requestResync()
{
    _notify_pending = true;
    _force_resync = true;
}

void CloudTask::notifySettingsChanged()
{
    _settings_dirty = true;
}

void CloudTask::notifyQueueChanged()
{
    _queue_dirty = true;
}

void CloudTask::setDeviceInfo(const char* firmware, const char* model)
{
    strncpy(_firmware, firmware ? firmware : "", sizeof(_firmware) - 1);
    _firmware[sizeof(_firmware) - 1] = '\0';
    strncpy(_model, model ? model : "", sizeof(_model) - 1);
    _model[sizeof(_model) - 1] = '\0';
    _settings_dirty = true;
}

void CloudTask::setLocationName(const char* name)
{
    if (!name) name = "";
    if (strncmp(_location, name, sizeof(_location) - 1) == 0)
        return;  // unchanged, no need to resend the snapshot
    strncpy(_location, name, sizeof(_location) - 1);
    _location[sizeof(_location) - 1] = '\0';
    _settings_dirty = true;
}

const char* CloudTask::getLocationName() { return _location; }

CloudState CloudTask::getState() { return _state; }

const char* CloudTask::getHostname() { return _hostname; }

const char* CloudTask::getStateName()
{
    switch (_state) {
    case CLOUD_DISCONNECTED: return "disconnected";
    case CLOUD_CONNECTING:   return "connecting";
    case CLOUD_HANDSHAKING:  return "handshaking";
    case CLOUD_CONNECTED:    return "connected";
    default:                 return "unknown";
    }
}

// ─── State handlers ───────────────────────────────────────────────────────────

void CloudTask::_doConnect()
{
    uint32_t now = millis();
    uint32_t backoff = BACKOFF_MS[_backoff_index < 5 ? _backoff_index : 4];
    if (now - _last_connect_attempt_ms < backoff) return;

    if (WiFi.status() != WL_CONNECTED) return;

    Serial.printf("[Cloud] Connecting to %s:%u\n", _host, _port);
    _last_connect_attempt_ms = now;
    _connect_start_ms = now;
    _state = CLOUD_CONNECTING;

    if (!_client.connect(_host, _port)) {
        Serial.println(F("[Cloud] TCP connect failed"));
        _disconnect("connect failed");
    }
}

void CloudTask::_doConnecting()
{
    if (_client.connected()) {
        Serial.println(F("[Cloud] TCP connected, starting handshake"));
        _state = CLOUD_HANDSHAKING;
        _hello_sent = false;
        _handshake_start_ms = millis();
        _rx_buf_len = 0;
        return;
    }
    if (millis() - _connect_start_ms > 5000) {
        _disconnect("connect timeout");
    }
}

void CloudTask::_doHandshake()
{
    if (!_hello_sent) {
        if (!_sendHello()) {
            _disconnect("hello send failed");
            return;
        }
        _hello_sent = true;
    }

    if (millis() - _handshake_start_ms > 5000) {
        _disconnect("handshake timeout");
        return;
    }

    if (_receiveHelloAck()) {
        _backoff_index = 0;
        _tx_seq = 1;
        _rx_seq_last = 0;
        _last_ping_ms = millis();
        _state = CLOUD_CONNECTED;
        Serial.println(F("[Cloud] Handshake complete, connected"));
        if (!_sendSensorData())
            _notify_pending = true;
        _last_sensor_push_ms = millis();
        _queue_dirty = true;     // push the command queue snapshot on (re)connect
        _settings_dirty = true;  // …and the settings snapshot
    }
}

void CloudTask::_doConnected()
{
    if (!_client.connected()) {
        _disconnect("connection lost");
        return;
    }

    // Drain every complete frame already buffered/available this tick so a burst
    // of server messages isn't spread one-per-loop. _readFrame returns false
    // once no full frame remains (and may disconnect on a protocol error).
    uint8_t type;
    static uint8_t payload[POOLLINK_MAX_PAYLOAD];  // static: too big for the loop stack
    size_t  payload_len = 0;
    while (_state == CLOUD_CONNECTED &&
           _readFrame(&type, payload, &payload_len, sizeof(payload))) {
        switch (type) {
        case POOLLINK_MSG_SENSOR_ACK: _handleSensorAck(payload, payload_len); break;
        case POOLLINK_MSG_COMMAND:    _handleCommand(payload, payload_len);   break;
        case POOLLINK_MSG_PING:       _handlePing();                          break;
        case POOLLINK_MSG_PONG:       break;
        case POOLLINK_MSG_ERROR:      _handleError(payload, payload_len);     break;
        case POOLLINK_MSG_GOODBYE:    _disconnect("server goodbye");          break;
        default:                      break;
        }
    }
    if (_state != CLOUD_CONNECTED) return;  // a handler may have disconnected

    uint32_t now = millis();

    // Push pending state change when payload differs from last successful send
    if (_notify_pending) {
        // force: a resync must put a frame on the wire even when the state is
        // byte-identical, because what we are really after is the reply.
        if (_sendSensorData(_force_resync)) {
            _notify_pending = false;
            _force_resync = false;
        }
    }

    // Push the command queue snapshot after connect / queue mutations
    if (_queue_dirty) {
        if (_sendQueueData())
            _queue_dirty = false;
    }

    // Push the settings snapshot after connect / set_settings
    if (_settings_dirty) {
        if (_sendSettingsData())
            _settings_dirty = false;
    }

    // Periodic ping (must run even when notify_pending is true)
    if (now - _last_ping_ms >= (uint32_t)CLOUD_PING_INTERVAL * 1000UL) {
        if (_sendPing())
            _last_ping_ms = now;
    }

    // Heartbeat push: every 60 s the runtime counters and energy are worth a new
    // history point, but an unchanged state section is suppressed. Force the
    // send once a keyframe is due so an idle spa still reports in. The cloud
    // cannot otherwise tell a quiet pump from an unplugged one.
    if (now - _last_sensor_push_ms >= 60000UL) {
        if (_sendSensorData(now - _last_sensor_sent_ms >= CLOUD_KEYFRAME_MS))
            _last_sensor_push_ms = now;
    }
}

void CloudTask::_disconnect(const char* reason)
{
    _client.stop();
    _rx_buf_len = 0;
    _last_sensor_hash = 0;
    _last_sensor_len  = 0;
    _state = CLOUD_DISCONNECTED;
    _last_connect_attempt_ms = millis();
    if (_backoff_index < 4) _backoff_index++;
    Serial.print(F("[Cloud] Disconnected: "));
    Serial.println(reason);
}

// ─── Handshake ───────────────────────────────────────────────────────────────

bool CloudTask::_sendHello()
{
    size_t hostname_len = strlen(_hostname);
    // [type(1)] [version(1)] [hostname_len(1)] [hostname(N)] [client_nonce(POOLLINK_NONCE_SIZE)]
    uint8_t buf[1 + 1 + 1 + POOLLINK_MAX_HOSTNAME + POOLLINK_NONCE_SIZE];
    buf[0] = POOLLINK_MSG_HELLO;
    buf[1] = POOLLINK_PROTOCOL_VERSION;
    buf[2] = (uint8_t)hostname_len;
    memcpy(buf + 3, _hostname, hostname_len);
    CloudCrypto::randomBytes(_client_nonce, POOLLINK_NONCE_SIZE);
    memcpy(buf + 3 + hostname_len, _client_nonce, POOLLINK_NONCE_SIZE);
    size_t len = 3 + hostname_len + POOLLINK_NONCE_SIZE;
    return _client.write(buf, len) == len;
}

bool CloudTask::_receiveHelloAck()
{
    // HELLO_ACK: [0x02][server_nonce(POOLLINK_NONCE_SIZE)][auth_tag(POOLLINK_AUTH_TAG_SIZE)]
    const size_t ackSize = 1 + POOLLINK_NONCE_SIZE + POOLLINK_AUTH_TAG_SIZE;
    while (_client.available() && _rx_buf_len < ackSize) {
        _rx_buf[_rx_buf_len++] = (uint8_t)_client.read();
    }
    if (_rx_buf_len < ackSize) return false;

    if (_rx_buf[0] != POOLLINK_MSG_HELLO_ACK) {
        _disconnect("unexpected handshake message");
        return false;
    }

    uint8_t server_nonce[POOLLINK_NONCE_SIZE];
    memcpy(server_nonce, _rx_buf + 1, POOLLINK_NONCE_SIZE);

    uint8_t received_tag[POOLLINK_AUTH_TAG_SIZE];
    memcpy(received_tag, _rx_buf + 1 + POOLLINK_NONCE_SIZE, POOLLINK_AUTH_TAG_SIZE);

    CloudCrypto::deriveSessionKey(_psk, _client_nonce, server_nonce, _session_key);

    if (!CloudCrypto::verifyHelloAuthTag(_session_key, received_tag)) {
        _disconnect("auth tag mismatch");
        return false;
    }

    _rx_buf_len = 0;
    return true;
}

// ─── Frame I/O ────────────────────────────────────────────────────────────────

bool CloudTask::_sendFrame(uint8_t type, const uint8_t* payload, size_t len)
{
    if (len > POOLLINK_MAX_PAYLOAD) return false;

    // Frame: [length(2)][type(1)][seq(8)][ciphertext(len)][tag(16)]
    static uint8_t frame[POOLLINK_MAX_FRAME];  // static: ~1 KB, too big for the loop stack

    uint16_t frame_body_len = (uint16_t)(POOLLINK_FRAME_HEADER_SIZE + len + POOLLINK_FRAME_TAG_SIZE);
    frame[0] = (frame_body_len >> 8) & 0xFF;
    frame[1] = frame_body_len & 0xFF;

    frame[2] = type;

    _tx_seq++;
    uint64_t seq = _tx_seq;
    for (int i = 0; i < 8; i++)
        frame[3 + i] = (seq >> (56 - 8 * i)) & 0xFF;

    // Encrypt payload into frame[11..], tag into frame[11+len..]
    uint8_t tag[POOLLINK_AUTH_TAG_SIZE];
    if (!CloudCrypto::encrypt(_session_key, type, seq,
                              payload, len,
                              frame + 11, tag)) {
        return false;
    }
    memcpy(frame + 11 + len, tag, POOLLINK_AUTH_TAG_SIZE);

    size_t total = 2 + frame_body_len;
    return _client.write(frame, total) == total;
}

bool CloudTask::_readFrame(uint8_t* type_out, uint8_t* payload_out,
                           size_t* payload_len_out, size_t max_payload)
{
    // Accumulate available bytes into rx buffer
    while (_client.available() && _rx_buf_len < POOLLINK_MAX_FRAME) {
        _rx_buf[_rx_buf_len++] = (uint8_t)_client.read();
    }

    // Need at least the 2-byte length field
    if (_rx_buf_len < 2) return false;

    uint16_t frame_body_len = ((uint16_t)_rx_buf[0] << 8) | _rx_buf[1];

    // Sanity check: minimum body = type(1) + seq(8) + tag(16) = 25
    if (frame_body_len < POOLLINK_FRAME_HEADER_SIZE + POOLLINK_FRAME_TAG_SIZE) return false;

    size_t ciphertext_len = frame_body_len - POOLLINK_FRAME_HEADER_SIZE - POOLLINK_FRAME_TAG_SIZE;
    if (ciphertext_len > POOLLINK_MAX_PAYLOAD) {
        _disconnect("frame too large");
        return false;
    }
    if (ciphertext_len > max_payload) {
        _disconnect("payload overflow");
        return false;
    }

    size_t total_frame = 2 + frame_body_len;
    if (_rx_buf_len < total_frame) return false;  // incomplete frame

    uint8_t type = _rx_buf[2];

    // Decode sequence number (big-endian)
    uint64_t seq = 0;
    for (int i = 0; i < 8; i++)
        seq = (seq << 8) | _rx_buf[3 + i];

    const uint8_t* ciphertext = _rx_buf + 11;
    const uint8_t* tag        = ciphertext + ciphertext_len;

    // Replay protection
    if (seq <= _rx_seq_last) {
        // Silently discard replay
        memmove(_rx_buf, _rx_buf + total_frame, _rx_buf_len - total_frame);
        _rx_buf_len -= total_frame;
        return false;
    }

    // Decrypt and verify
    if (!CloudCrypto::decrypt(_session_key, type, seq,
                              ciphertext, ciphertext_len,
                              payload_out, tag)) {
        _disconnect("auth tag mismatch");
        return false;
    }

    _rx_seq_last = seq;
    *type_out = type;
    *payload_len_out = ciphertext_len;

    // Consume frame from buffer
    memmove(_rx_buf, _rx_buf + total_frame, _rx_buf_len - total_frame);
    _rx_buf_len -= total_frame;

    return true;
}

// ─── Outbound messages ────────────────────────────────────────────────────────

// FNV-1a - duplicate-push suppression by hash instead of keeping a full copy
// of the last payload (saves POOLLINK_MAX_PAYLOAD bytes of RAM).
static uint32_t _fnv1a(const uint8_t* data, size_t len)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; i++) { h ^= data[i]; h *= 16777619u; }
    return h;
}

bool CloudTask::_sendSensorData(bool force)
{
    // Full compact-key snapshot (states + times + smart schedule) built by BWC.
    // Transient heap allocation, same pattern/size as the local UI JSON path.
    DynamicJsonDocument payload(2048);
    size_t state_len = 0;
    bwc->buildCloudSnapshot(payload.to<JsonObject>(), &state_len);

    static char buf[POOLLINK_MAX_PAYLOAD];  // static: too big for the loop stack
    size_t len = serializeJson(payload, buf, sizeof(buf));
    if (len == 0 || len >= sizeof(buf))
        return false;

    // Hash the state section only. ArduinoJson serializes in insertion order, so
    // it is the first state_len-1 bytes of the payload (the state document minus
    // its closing brace). No second buffer needed.
    size_t hlen = (state_len > 0 && state_len <= len) ? state_len - 1 : len;
    uint32_t hash = _fnv1a((const uint8_t*)buf, hlen);
    if (!force && _last_sensor_len == hlen && _last_sensor_hash == hash)
        return true;  // nothing changed - suppress

    if (!_sendFrame(POOLLINK_MSG_SENSOR_DATA, (const uint8_t*)buf, len))
        return false;

    _last_sensor_hash = hash;
    _last_sensor_len  = hlen;
    _last_sensor_sent_ms = millis();
    return true;
}

bool CloudTask::_sendQueueData()
{
    // Same columnar JSON the local UI gets (LEN/QEN/CMD[]/VALUE[]/XTIME[]/…).
    String json;
    bwc->getJSONCommandQueue(json);
    if (json.length() == 0)
        return true;  // nothing to send
    if (json.length() > POOLLINK_MAX_PAYLOAD) {
        // ponytail: oversized queues (>~15 entries) are dropped, not chunked;
        // add a paged message type if real queues ever grow that large.
        Serial.println(F("[Cloud] queue snapshot too large, skipped"));
        return true;
    }
    return _sendFrame(POOLLINK_MSG_QUEUE_DATA, (const uint8_t*)json.c_str(), json.length());
}

bool CloudTask::_sendSettingsData()
{
    // Same JSON the local /getconfig/ endpoint returns (PRICE, intervals, …),
    // plus the resolved town, which is not a setting. The cloud sent it to us
    // in a sensor ack, and this echoes it back as "CITY".
    String json;
    bwc->getJSONSettings(json);
    if (json.length() == 0)
        return true;
    // Device facts the settings JSON doesn't carry: the resolved town, the
    // firmware version, the WifiWhirl model and the current IP. One prefix, so
    // the snapshot is rebuilt once rather than once per field.
    String prefix("{");
    if (_location[0]) prefix += String(F("\"CITY\":\"")) + _location + "\",";
    if (_firmware[0]) prefix += String(F("\"FW\":\"")) + _firmware + "\",";
    if (_model[0])    prefix += String(F("\"WWMODEL\":\"")) + _model + "\",";
    if (WiFi.status() == WL_CONNECTED)
        prefix += String(F("\"IP\":\"")) + WiFi.localIP().toString() + "\",";
    if (prefix.length() > 1 && json.startsWith("{"))
        json = prefix + json.substring(1);
    if (json.length() > POOLLINK_MAX_PAYLOAD) {
        Serial.println(F("[Cloud] settings snapshot too large, skipped"));
        return true;
    }
    return _sendFrame(POOLLINK_MSG_SETTINGS_DATA, (const uint8_t*)json.c_str(), json.length());
}

bool CloudTask::_sendPing()
{
    return _sendFrame(POOLLINK_MSG_PING, nullptr, 0);
}

bool CloudTask::_sendGoodbye()
{
    return _sendFrame(POOLLINK_MSG_GOODBYE, nullptr, 0);
}

bool CloudTask::_sendCommandAck(uint32_t cmd_id, bool success)
{
    StaticJsonDocument<64> doc;
    doc[F("cmd_id")] = cmd_id;
    doc[F("ok")]     = success;
    char buf[64];
    size_t len = serializeJson(doc, buf, sizeof(buf));
    return _sendFrame(POOLLINK_MSG_COMMAND_ACK, (const uint8_t*)buf, len);
}

// ─── Inbound message handlers ─────────────────────────────────────────────────

void CloudTask::_handleSensorAck(const uint8_t* payload, size_t len)
{
    // Payload: {"outdoor_temp": 18.5, "location": "Münster"}. The server holds
    // the location and resolves it, so this unit makes no weather request of
    // its own. Empty when nothing is configured, which is not an error.
    //
    // 256, not 128: ArduinoJson copies strings out of a const buffer, so the
    // town needs pool space of its own. Stack-local, so no permanent cost.
    StaticJsonDocument<256> doc;
    DeserializationError err = deserializeJson(doc, payload, len);
    if (err) return;

    // Only when the user wants the outdoor temperature from outside; with
    // WEATHER off they set it by hand (HA number, MQTT CMD 15, local UI) and a
    // push from the cloud would silently overwrite it.
    if (bwc->weatherEnabled() && doc.containsKey(F("outdoor_temp"))) {
        float raw = doc[F("outdoor_temp")].as<float>();
        // Reject NaN/inf and anything off the scale before it reaches the
        // heating model. Same range as the Home Assistant manual slider.
        if (raw == raw && raw >= -20.0f && raw <= 50.0f) {
            // BWC stores ambient as whole degrees; round (don't truncate) to
            // keep the nearest degree, e.g. 18.5 → 19.
            int64_t temp = (int64_t)lroundf(raw);
            bwc->setAmbientTemperature(temp, true);  // true = Celsius
        }
    }

    // The town the coordinates resolved to, for the local UI. setLocationName
    // ignores a repeat, so this does not re-send the settings snapshot every
    // 60 s.
    if (doc.containsKey(F("location")))
        setLocationName(doc[F("location")].as<const char*>());
}

// "on"/true/1 → 1, everything else → 0
static int64_t _onOff(JsonVariantConst v)
{
    if (v.is<const char*>()) return strcmp(v.as<const char*>(), "on") == 0 ? 1 : 0;
    if (v.is<bool>())        return v.as<bool>() ? 1 : 0;
    return v.as<int64_t>() != 0 ? 1 : 0;
}

void CloudTask::_handleCommand(const uint8_t* payload, size_t len)
{
    // Payload: {"cmd_id":42,"action":"heater","value":"on"}
    // value may be a string, a number, or an object (smart schedule).
    DynamicJsonDocument doc(512);
    DeserializationError err = deserializeJson(doc, payload, len);
    if (err) {
        _sendCommandAck(0, false);
        return;
    }

    uint32_t cmd_id      = doc[F("cmd_id")] | 0;
    const char* action   = doc[F("action")] | "";
    JsonVariantConst val = doc[F("value")];

    command_que_item item;
    item.xtime    = (uint64_t)time(nullptr);
    item.interval = 0;
    item.force    = false;

    // action → (bwc command, value); mirrors the local web API's mapping.
    struct ActionMap { const char* name; Commands cmd; enum { ONOFF, NUM, TENTHS, NONE } vk; };
    static const ActionMap MAP[] = {
        { "heater",                  SETHEATER,     ActionMap::ONOFF },
        { "filter",                  SETPUMP,       ActionMap::ONOFF },
        { "bubbles",                 SETBUBBLES,    ActionMap::ONOFF },
        { "jets",                    SETJETS,       ActionMap::ONOFF },
        { "set_target_temp",         SETTARGET,     ActionMap::NUM },
        { "toggle_power",            TOGGLEPWR,     ActionMap::NONE },
        { "toggle_lock",             TOGGLELCK,     ActionMap::NONE },
        { "set_brightness",          SETBRIGHTNESS, ActionMap::NUM },
        { "set_ambient_c",           SETAMBIENTC,   ActionMap::NUM },
        { "set_ambient_f",           SETAMBIENTF,   ActionMap::NUM },
        { "set_ph",                  SETPHVALUE,    ActionMap::TENTHS },
        { "set_chlorine",            SETCLVALUE,    ActionMap::TENTHS },
        { "set_cyanuric",            SETCYAVALUE,   ActionMap::TENTHS },
        { "set_alkalinity",          SETALKVALUE,   ActionMap::NUM },
        { "reset_timer_chlorine",    RESETCLTIMER,  ActionMap::NONE },
        { "reset_timer_filter",      RESETFTIMER,   ActionMap::NONE },
        { "reset_timer_filter_clean", RESETFCTIMER, ActionMap::NONE },
        { "reset_timer_water_change", RESETWCTIMER, ActionMap::NONE },
        { "reset_totals",            RESETTIMES,    ActionMap::NONE },
    };

    bool success = false;

    if (strcmp(action, "set_unit") == 0) {
        item.cmd = SETUNIT;
        const char* u = val | "";
        item.val = (strcmp(u, "c") == 0 || strcmp(u, "C") == 0) ? 1 : 0;
        success = bwc->add_command(item);

    } else if (strcmp(action, "set_smart_schedule") == 0) {
        // value: {"target_time":…,"target_temp":…,"keep_heater_on":…}
        // (compact TARGETTIME/TARGETTEMP/KEEPON accepted too)
        uint64_t tt  = val[F("target_time")].as<uint64_t>();
        if (!tt) tt  = val[F("TARGETTIME")].as<uint64_t>();
        uint8_t  tmp = val[F("target_temp")].as<uint8_t>();
        if (!tmp) tmp = val[F("TARGETTEMP")].as<uint8_t>();
        bool kp = val[F("keep_heater_on")].as<bool>() || val[F("KEEPON")].as<bool>();
        // Days between repeats: 0 never, 1 daily, 7 weekly (same as /setsmartschedule/).
        uint8_t rp = val[F("REPEAT")].as<uint8_t>();
        if (!rp) rp = val[F("repeat")].as<uint8_t>();
        success = (tt > 0 && tmp > 0) && bwc->setSmartSchedule(tt, tmp, kp, rp);

    } else if (strcmp(action, "cancel_smart_schedule") == 0) {
        bwc->cancelSmartSchedule();
        success = true;

    } else if (strcmp(action, "set_smart_schedule_keepon") == 0) {
        success = bwc->updateSmartScheduleKeepHeaterOn(_onOff(val) != 0);

    } else if (strcmp(action, "set_smart_schedule_repeat") == 0) {
        success = bwc->updateSmartScheduleRepeat(val.as<uint8_t>());

    } else if (strcmp(action, "set_settings") == 0) {
        // value: partial settings object; bwc merges by key (same as /setconfig/).
        String msg, errStr;
        serializeJson(val, msg);
        success = bwc->setJSONSettings(msg, errStr);
        _settings_dirty = true;
        // Weather may have just been switched on; ask for a reading rather
        // than leaving the user staring at a stale ambient until the keyframe.
        requestResync();

    // ── Automation: command queue management ──
    } else if (strcmp(action, "queue_enable") == 0) {
        bwc->set_command_queue_enabled(_onOff(val) != 0);
        success = true;
        _queue_dirty = true;

    } else if (strcmp(action, "queue_item_enable") == 0) {
        success = bwc->set_command_enabled(val[F("IDX")].as<uint8_t>(), _onOff(val[F("EN")]) != 0);
        _queue_dirty = true;

    } else if (strcmp(action, "queue_del") == 0) {
        success = bwc->del_command(val[F("IDX")].as<uint8_t>());
        _queue_dirty = true;

    } else if (strcmp(action, "queue_add") == 0 || strcmp(action, "queue_edit") == 0) {
        // value: {CMD, VALUE, XTIME, INTERVAL, TXT?, IDX (edit only)}
        item.cmd      = (Commands)val[F("CMD")].as<int>();
        item.val      = val[F("VALUE")].as<int64_t>();
        item.xtime    = val[F("XTIME")].as<uint64_t>();
        item.interval = val[F("INTERVAL")].as<uint32_t>();
        const char* txt = val[F("TXT")] | "";
        item.text = txt;
        // Same as /addcommand/: a planned automation, so turning the queue off
        // parks it instead of letting it fire like live control.
        item.scheduled = true;
        success = (action[6] == 'a')  // queue_add vs queue_edit
            ? bwc->add_command(item)
            : bwc->edit_command(val[F("IDX")].as<uint8_t>(), item);
        _queue_dirty = true;

    } else {
        for (const auto& m : MAP) {
            if (strcmp(action, m.name) != 0) continue;
            item.cmd = m.cmd;
            switch (m.vk) {
                case ActionMap::ONOFF:  item.val = _onOff(val); break;
                case ActionMap::NUM:    item.val = (int64_t)lroundf(val.as<float>()); break;
                case ActionMap::TENTHS: item.val = (int64_t)lroundf(val.as<float>() * 10.0f); break;
                case ActionMap::NONE:   item.val = 1; break;
            }
            success = bwc->add_command(item);
            break;
        }
    }

    _sendCommandAck(cmd_id, success);

    if (success && !_sendSensorData())
        _notify_pending = true;
}

void CloudTask::_handlePing()
{
    _sendFrame(POOLLINK_MSG_PONG, nullptr, 0);
}

void CloudTask::_handleError(const uint8_t* payload, size_t len)
{
    Serial.print(F("[Cloud] Server error: "));
    if (payload && len > 0) Serial.write(payload, len);
    Serial.println();
    _disconnect("server error");
}
