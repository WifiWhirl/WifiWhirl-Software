#pragma once
#include <Arduino.h>
#if defined(ESP8266)
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#endif
#include "cloud_protocol.h"

// Seconds between keepalive pings. Override with -D CLOUD_PING_INTERVAL=N.
#ifndef CLOUD_PING_INTERVAL
#define CLOUD_PING_INTERVAL 30
#endif

// Send a snapshot at least this often even when nothing changed, so the cloud
// can tell "idle" from "gone" and every history bucket gets a point. Under the
// backend's 300 s staleness limit with room for network jitter.
#ifndef CLOUD_KEYFRAME_MS
#define CLOUD_KEYFRAME_MS 240000UL
#endif

enum CloudState {
    CLOUD_DISCONNECTED,
    CLOUD_CONNECTING,
    CLOUD_HANDSHAKING,
    CLOUD_CONNECTED,
};

class CloudTask {
public:
    // Call once during setup() if CLOUD_ENABLED.
    static void begin(const char* hostname, const uint8_t psk[32],
                      const char* host, uint16_t port);

    // Call on every loop() iteration. Non-blocking.
    static void tick();

    // Trigger an immediate sensor push on next tick (safe when disconnected).
    static void notifyStateChanged();

    // Ask the cloud for a fresh reply now, bypassing the unchanged-state
    // suppression. The sensor ack is the only thing that carries the outdoor
    // temperature down, and it only comes in answer to a push - so after a
    // settings change that starts caring about it (WEATHER off -> on) the
    // device has to solicit one, or it waits out the keyframe.
    static void requestResync();

    // Push a fresh settings/queue snapshot on the next tick. Call these after
    // anything changes them locally (web UI, setup assistant) - without it the
    // cloud keeps the snapshot from the last connect and drifts out of sync.
    // Safe when disconnected: the flag is consumed once connected.
    static void notifySettingsChanged();
    static void notifyQueueChanged();

    // Town the cloud resolved this unit's location to, pushed down in the
    // sensor ack. Purely for display: the local UI shows it, and it is echoed
    // back in the settings snapshot as "CITY". Empty until the first ack that
    // carries one.
    static void setLocationName(const char* name);
    static const char* getLocationName();

    // Firmware version and WifiWhirl model, for the cloud's device info page.
    // They live in the app (WifiWhirl_Version.h / PIO_ENV_NAME), so main.cpp
    // hands them down instead of this library reaching up for them.
    static void setDeviceInfo(const char* firmware, const char* model);

    static CloudState  getState();
    static const char* getStateName();
    /** PoolLink hostname string passed to begin() (empty if begin not called). */
    static const char* getHostname();

private:
    static CloudState _state;
    static WiFiClient _client;
    static uint8_t    _session_key[POOLLINK_SESSION_KEY_SIZE];
    static uint8_t    _psk[32];
    static uint8_t    _client_nonce[POOLLINK_NONCE_SIZE];
    static char       _hostname[POOLLINK_MAX_HOSTNAME + 1];
    static char       _host[64];
    static uint16_t   _port;

    static uint64_t   _tx_seq;
    static uint64_t   _rx_seq_last;

    static uint32_t   _last_ping_ms;
    static uint32_t   _last_connect_attempt_ms;
    static uint8_t    _backoff_index;

    static uint32_t   _connect_start_ms;
    static uint32_t   _handshake_start_ms;
    static bool       _hello_sent;
    static bool       _notify_pending;
    static bool       _force_resync;

    static uint8_t    _rx_buf[POOLLINK_MAX_FRAME];
    static size_t     _rx_buf_len;

    static char       _location[48];
    static char       _firmware[16];
    static char       _model[24];

    static void     _doConnect();
    static void     _doConnecting();
    static void     _doHandshake();
    static void     _doConnected();
    static void     _disconnect(const char* reason);

    static bool     _sendHello();
    static bool     _receiveHelloAck();
    static bool     _sendSensorData(bool force = false);
    static bool     _sendQueueData();
    static bool     _queue_dirty;
    static bool     _sendSettingsData();
    static bool     _settings_dirty;
    static bool     _sendPing();
    static bool     _sendGoodbye();
    static bool     _sendCommandAck(uint32_t cmd_id, bool success);

    static bool     _sendFrame(uint8_t type, const uint8_t* payload, size_t len);
    static bool     _readFrame(uint8_t* type_out, uint8_t* payload_out,
                               size_t* payload_len_out, size_t max_payload);

    static void     _handleCommand(const uint8_t* payload, size_t len);
    static void     _handleSensorAck(const uint8_t* payload, size_t len);
    static void     _handlePing();
    static void     _handleError(const uint8_t* payload, size_t len);
};
