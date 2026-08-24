#pragma once
#include <Arduino.h>

// PSK persistence. Stored as a small binary LittleFS file rather than raw EEPROM:
// avoids a fixed-offset collision with the rest of the config and works the same
// on ESP8266 and ESP32 (where EEPROM is NVS-emulated). LittleFS must already be
// mounted (main.cpp does this at boot before CloudTask::begin()).
//
// File /cloudpsk layout (41 bytes):
//   +0  4B  magic   'P','L','P','S'
//   +4  1B  version 0x01
//   +5 32B  PSK
//   +37 4B  CRC32 of the 32 PSK bytes (big-endian)
class CloudPSKStorage {
public:
    // Call once at boot before CloudTask::begin().
    // If no valid PSK is stored (missing file/magic or failed CRC) and
    // hardcoded_hex is a 64-char hex string, that PSK is written and loaded.
    // Loads the PSK into the 32-byte buffer. Returns true on success.
    static bool ensurePSK(uint8_t psk_out[32], const char* hardcoded_hex = "");

    // Explicitly overwrite the stored PSK.
    static bool writePSK(const uint8_t psk[32]);

    // Read PSK from storage into buffer. Returns false if missing or CRC fails.
    static bool readPSK(uint8_t psk_out[32]);

    // Parse a 64-char hex string and persist it. Returns false on bad input.
    static bool writePSKFromHex(const char* hex64);

private:
    static uint32_t computeCRC32(const uint8_t* data, size_t len);
    static bool parseHex(const char* hex, uint8_t* out, size_t out_len);
};
