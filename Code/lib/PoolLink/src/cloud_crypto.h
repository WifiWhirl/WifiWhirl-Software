#pragma once
#include <Arduino.h>
#include "cloud_protocol.h"

// Crypto primitives for the PoolLink protocol: HKDF-SHA256 key derivation and
// ChaCha20-Poly1305 AEAD framing. The wire format is identical on both targets;
// only the backend differs - BearSSL on ESP8266, mbedTLS on ESP32 (see .cpp).
class CloudCrypto {
public:
    // HKDF-SHA256 key derivation.
    // Derives a 32-byte session key from PSK and the two nonces.
    static void deriveSessionKey(
        const uint8_t psk[32],
        const uint8_t client_nonce[POOLLINK_NONCE_SIZE],
        const uint8_t server_nonce[POOLLINK_NONCE_SIZE],
        uint8_t session_key_out[POOLLINK_SESSION_KEY_SIZE]
    );

    // Compute the server's auth_tag for HELLO_ACK verification.
    static void computeHelloAuthTag(
        const uint8_t session_key[POOLLINK_SESSION_KEY_SIZE],
        uint8_t tag_out[POOLLINK_AUTH_TAG_SIZE]
    );

    // Verify a received auth_tag. Constant-time comparison.
    static bool verifyHelloAuthTag(
        const uint8_t session_key[POOLLINK_SESSION_KEY_SIZE],
        const uint8_t received_tag[POOLLINK_AUTH_TAG_SIZE]
    );

    // Encrypt a plaintext payload. Returns false on error.
    // direction_prefix 0x00000001 = Device→Server
    static bool encrypt(
        const uint8_t session_key[POOLLINK_SESSION_KEY_SIZE],
        uint8_t msg_type,
        uint64_t seq,
        const uint8_t* plaintext,
        size_t plaintext_len,
        uint8_t* ciphertext_out,
        uint8_t tag_out[POOLLINK_AUTH_TAG_SIZE]
    );

    // Decrypt and verify a received frame.
    // Returns false if auth tag verification fails.
    // direction_prefix 0x00000002 = Server→Device
    static bool decrypt(
        const uint8_t session_key[POOLLINK_SESSION_KEY_SIZE],
        uint8_t msg_type,
        uint64_t seq,
        const uint8_t* ciphertext,
        size_t ciphertext_len,
        uint8_t* plaintext_out,
        const uint8_t tag[POOLLINK_AUTH_TAG_SIZE]
    );

    // Fill buffer with hardware random bytes.
    static void randomBytes(uint8_t* buf, size_t len);

    // Account pairing proof: HMAC-SHA256(PSK, canonical_message) - same bytes as cloud `pairing_crypto.py`.
    static void accountPairingHmac(
        const uint8_t psk[32],
        const char* hostname_ascii,
        const uint8_t nonce[POOLLINK_NONCE_SIZE],
        uint8_t proof_out[32]);

    /** Base64url without padding (RFC 4648 alphabet `-`/`_`). `out` must hold ≥ 4*((len+2)/3)+1 bytes. */
    static size_t base64UrlEncodeNoPad(const uint8_t* data, size_t len, char* out);

    /** Known-answer self-test: verifies HKDF, encrypt, decrypt, the HELLO tag,
     *  and tamper rejection against fixed vectors (lib/PoolLink/test/gen_kat.py).
     *  Returns true if this build's backend matches the reference byte-for-byte.
     *  Call once at boot under -D CLOUD_SELFTEST to catch a broken ESP32/ESP8266
     *  crypto backend before it silently fails to talk to the server. */
    static bool selfTest();
};
