#include "cloud_psk_storage.h"
#include <LittleFS.h>

static const char     PSK_PATH[]   = "/cloudpsk";
static const uint8_t  MAGIC[4]      = {'P', 'L', 'P', 'S'};
static const uint8_t  PSK_VERSION   = 0x01;
static const size_t   PSK_FILE_SIZE = 4 + 1 + 32 + 4;  // 41

uint32_t CloudPSKStorage::computeCRC32(const uint8_t* data, size_t len)
{
    uint32_t crc = 0xFFFFFFFFUL;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++)
            crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320UL : (crc >> 1);
    }
    return ~crc;
}

bool CloudPSKStorage::parseHex(const char* hex, uint8_t* out, size_t out_len)
{
    if (!hex || strlen(hex) < out_len * 2) return false;
    auto hexVal = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i < out_len; i++) {
        int h = hexVal(hex[i * 2]);
        int l = hexVal(hex[i * 2 + 1]);
        if (h < 0 || l < 0) return false;
        out[i] = (uint8_t)((h << 4) | l);
    }
    return true;
}

bool CloudPSKStorage::writePSK(const uint8_t psk[32])
{
    uint8_t blob[PSK_FILE_SIZE];
    memcpy(blob, MAGIC, 4);
    blob[4] = PSK_VERSION;
    memcpy(blob + 5, psk, 32);
    uint32_t crc = computeCRC32(psk, 32);
    blob[37] = (crc >> 24) & 0xFF;
    blob[38] = (crc >> 16) & 0xFF;
    blob[39] = (crc >>  8) & 0xFF;
    blob[40] = (crc      ) & 0xFF;

    File f = LittleFS.open(PSK_PATH, "w");
    if (!f) return false;
    size_t n = f.write(blob, sizeof(blob));
    f.close();
    return n == sizeof(blob);
}

bool CloudPSKStorage::readPSK(uint8_t psk_out[32])
{
    File f = LittleFS.open(PSK_PATH, "r");
    if (!f) return false;
    uint8_t blob[PSK_FILE_SIZE];
    size_t n = f.read(blob, sizeof(blob));
    f.close();
    if (n != sizeof(blob)) return false;
    if (memcmp(blob, MAGIC, 4) != 0) return false;

    memcpy(psk_out, blob + 5, 32);
    uint32_t stored_crc = ((uint32_t)blob[37] << 24) | ((uint32_t)blob[38] << 16) |
                          ((uint32_t)blob[39] <<  8) |  (uint32_t)blob[40];
    return computeCRC32(psk_out, 32) == stored_crc;
}

bool CloudPSKStorage::writePSKFromHex(const char* hex64)
{
    uint8_t psk[32];
    if (!parseHex(hex64, psk, 32)) return false;
    return writePSK(psk);
}

bool CloudPSKStorage::ensurePSK(uint8_t psk_out[32], const char* hardcoded_hex)
{
    if (readPSK(psk_out)) return true;

    // No valid stored PSK - fall back to the compiled-in one if present.
    uint8_t psk[32];
    if (!parseHex(hardcoded_hex, psk, 32)) {
        Serial.println(F("[PSK] No valid PSK available"));
        return false;
    }
    if (!writePSK(psk)) {
        Serial.println(F("[PSK] Failed to persist PSK"));
        return false;
    }
    memcpy(psk_out, psk, 32);
    Serial.println(F("[PSK] Migrated hardcoded PSK to storage"));
    return true;
}
