#ifndef POOLLINK_CHACHAPOLY_SW_H
#define POOLLINK_CHACHAPOLY_SW_H

#include <stddef.h>
#include <stdint.h>

// Portable RFC 8439 ChaCha20-Poly1305 primitives.
//
// Why this exists: the precompiled Arduino-ESP32 core ships the
// mbedtls/chachapoly.h header, but its libmbedcrypto.a is built with
// MBEDTLS_CHACHA20_C/POLY1305_C/CHACHAPOLY_C disabled (IDF defaults), so the
// symbols don't link. This is a self-contained software fallback used by the
// ESP32 branch of cloud_crypto.cpp; ESP8266 keeps BearSSL.
//
// Split into crypt + tag so the caller controls encrypt-then-MAC vs
// verify-then-decrypt with its own constant-time compare.

#ifdef __cplusplus
extern "C" {
#endif

// XOR `data` in place with the ChaCha20 keystream (block counter starts at 1,
// as RFC 8439 AEAD reserves block 0 for the Poly1305 key). Encrypt == decrypt.
void chachapoly_sw_crypt(const uint8_t key[32], const uint8_t nonce[12],
                         uint8_t *data, size_t len);

// Poly1305 tag over aad||pad16 || ct||pad16 || len(aad)LE64 || len(ct)LE64,
// keyed from ChaCha20 block 0. `ct` is the ciphertext.
void chachapoly_sw_tag(const uint8_t key[32], const uint8_t nonce[12],
                       const uint8_t *ct, size_t ct_len,
                       const uint8_t *aad, size_t aad_len, uint8_t tag[16]);

#ifdef __cplusplus
}
#endif

#endif
