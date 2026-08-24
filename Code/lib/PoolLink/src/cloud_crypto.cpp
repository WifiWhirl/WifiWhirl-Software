#include "cloud_crypto.h"
#include <cstring>

// ─── Backend primitives ──────────────────────────────────────────────────────
// PoolLink needs exactly two primitives: HMAC-SHA256 (for HKDF and the pairing
// proof) and ChaCha20-Poly1305 AEAD (for the framed messages). The wire bytes
// are identical on both targets; only the library underneath differs.
//   ESP8266 → BearSSL (bundled in the core)
//   ESP32   → mbedTLS for HMAC; AEAD via bundled chachapoly_sw.c (the core's
//             precompiled libmbedcrypto.a lacks the chachapoly symbols)
// Neither adds a lib_deps entry or measurable flash.
//
// Two AEAD primitives, both operating in place on `data`:
//   aead_seal - encrypt + produce tag.
//   aead_open - decrypt + verify tag (constant-time), returns true if authentic.

#if defined(ESP8266)

extern "C" {
#include <bearssl/bearssl_hmac.h>
#include <bearssl/bearssl_hash.h>
#include <bearssl/bearssl_aead.h>
}
// os_get_random is declared in osapi.h (pulled in via Arduino.h)
extern "C" int os_get_random(unsigned char *buf, size_t len);

static bool ct_equal(const uint8_t* a, const uint8_t* b, size_t len);

static void hmac_sha256(const uint8_t* key, size_t key_len,
                        const uint8_t* msg, size_t msg_len, uint8_t out[32])
{
    br_hmac_key_context kctx;
    br_hmac_context ctx;
    br_hmac_key_init(&kctx, &br_sha256_vtable, key, key_len);
    br_hmac_init(&ctx, &kctx, 0);
    br_hmac_update(&ctx, msg, msg_len);
    br_hmac_out(&ctx, out);
}

static void aead_seal(const uint8_t key[32], const uint8_t nonce[12],
                      uint8_t* data, size_t len,
                      const uint8_t* aad, size_t aad_len, uint8_t tag[16])
{
    br_poly1305_ctmul_run(key, nonce, data, len, aad, aad_len, tag,
                          br_chacha20_ct_run, 1 /* encrypt */);
}

static bool aead_open(const uint8_t key[32], const uint8_t nonce[12],
                      uint8_t* data, size_t len,
                      const uint8_t* aad, size_t aad_len, const uint8_t tag[16])
{
    // encrypt=0 computes the tag over the ciphertext (in `data`) and decrypts
    // it in place; we compare the tag ourselves.
    uint8_t computed[16];
    br_poly1305_ctmul_run(key, nonce, data, len, aad, aad_len, computed,
                          br_chacha20_ct_run, 0 /* decrypt */);
    return ct_equal(computed, tag, 16);
}

static void rng_fill(uint8_t* buf, size_t len)
{
    os_get_random(reinterpret_cast<unsigned char*>(buf), len);
}

#elif defined(ESP32)

#include <mbedtls/md.h>
#include <esp_random.h>
#include "chachapoly_sw.h"

static bool ct_equal(const uint8_t* a, const uint8_t* b, size_t len);

static void hmac_sha256(const uint8_t* key, size_t key_len,
                        const uint8_t* msg, size_t msg_len, uint8_t out[32])
{
    const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    mbedtls_md_hmac(info, key, key_len, msg, msg_len, out);
}

static void aead_seal(const uint8_t key[32], const uint8_t nonce[12],
                      uint8_t* data, size_t len,
                      const uint8_t* aad, size_t aad_len, uint8_t tag[16])
{
    chachapoly_sw_crypt(key, nonce, data, len);
    // Tag is computed over the produced ciphertext - matches BearSSL/RFC 8439.
    chachapoly_sw_tag(key, nonce, data, len, aad, aad_len, tag);
}

static bool aead_open(const uint8_t key[32], const uint8_t nonce[12],
                      uint8_t* data, size_t len,
                      const uint8_t* aad, size_t aad_len, const uint8_t tag[16])
{
    // Verify the tag (over the ciphertext) first; only decrypt when authentic.
    uint8_t computed[16];
    chachapoly_sw_tag(key, nonce, data, len, aad, aad_len, computed);
    if (!ct_equal(computed, tag, 16))
        return false;
    chachapoly_sw_crypt(key, nonce, data, len);
    return true;
}

static void rng_fill(uint8_t* buf, size_t len)
{
    esp_fill_random(buf, len);
}

#else
#error "PoolLink: unsupported platform (need ESP8266 or ESP32)"
#endif

// ─── Constant-time compare ───────────────────────────────────────────────────
static bool ct_equal(const uint8_t* a, const uint8_t* b, size_t len)
{
    uint8_t diff = 0;
    for (size_t i = 0; i < len; i++) diff |= a[i] ^ b[i];
    return diff == 0;
}

// ─── HKDF-SHA256 ─────────────────────────────────────────────────────────────

void CloudCrypto::deriveSessionKey(
    const uint8_t psk[32],
    const uint8_t client_nonce[POOLLINK_NONCE_SIZE],
    const uint8_t server_nonce[POOLLINK_NONCE_SIZE],
    uint8_t session_key_out[POOLLINK_SESSION_KEY_SIZE])
{
    // HKDF-Extract: salt = client_nonce || server_nonce, ikm = psk
    uint8_t salt[POOLLINK_NONCE_SIZE * 2];
    memcpy(salt,                       client_nonce, POOLLINK_NONCE_SIZE);
    memcpy(salt + POOLLINK_NONCE_SIZE, server_nonce, POOLLINK_NONCE_SIZE);

    uint8_t prk[32];
    hmac_sha256(salt, sizeof(salt), psk, 32, prk);

    // HKDF-Expand: T(1) = HMAC(prk, info || 0x01); session_key = T(1)
    uint8_t msg[sizeof(POOLLINK_HKDF_INFO) /* info chars + 0x01, NUL slot reused */];
    size_t info_len = strlen(POOLLINK_HKDF_INFO);
    memcpy(msg, POOLLINK_HKDF_INFO, info_len);
    msg[info_len] = 0x01;
    hmac_sha256(prk, sizeof(prk), msg, info_len + 1, session_key_out);
}

// ─── HELLO_ACK auth tag ──────────────────────────────────────────────────────

void CloudCrypto::computeHelloAuthTag(
    const uint8_t session_key[POOLLINK_SESSION_KEY_SIZE],
    uint8_t tag_out[POOLLINK_AUTH_TAG_SIZE])
{
    uint8_t nonce[12] = {};  // all-zero 12-byte nonce
    static const char aad[] = "server-hello";
    aead_seal(session_key, nonce, nullptr, 0,
              (const uint8_t*)aad, sizeof(aad) - 1, tag_out);
}

bool CloudCrypto::verifyHelloAuthTag(
    const uint8_t session_key[POOLLINK_SESSION_KEY_SIZE],
    const uint8_t received_tag[POOLLINK_AUTH_TAG_SIZE])
{
    uint8_t expected_tag[POOLLINK_AUTH_TAG_SIZE];
    computeHelloAuthTag(session_key, expected_tag);
    return ct_equal(expected_tag, received_tag, POOLLINK_AUTH_TAG_SIZE);
}

// ─── Nonce / AAD construction (identical bytes on both backends) ──────────────

// nonce = direction_prefix(4B BE) || seq(8B BE)  → 12 bytes
static void buildNonce(uint32_t direction, uint64_t seq, uint8_t nonce[12])
{
    nonce[0] = (direction >> 24) & 0xFF;
    nonce[1] = (direction >> 16) & 0xFF;
    nonce[2] = (direction >>  8) & 0xFF;
    nonce[3] = (direction      ) & 0xFF;
    for (int i = 0; i < 8; i++)
        nonce[4 + i] = (seq >> (56 - 8 * i)) & 0xFF;
}

// aad = msg_type(1B) || seq(8B BE)  → 9 bytes
static void buildAAD(uint8_t msg_type, uint64_t seq, uint8_t aad[9])
{
    aad[0] = msg_type;
    for (int i = 0; i < 8; i++)
        aad[1 + i] = (seq >> (56 - 8 * i)) & 0xFF;
}

// ─── Encrypt / Decrypt ───────────────────────────────────────────────────────

bool CloudCrypto::encrypt(
    const uint8_t session_key[POOLLINK_SESSION_KEY_SIZE],
    uint8_t msg_type, uint64_t seq,
    const uint8_t* plaintext, size_t plaintext_len,
    uint8_t* ciphertext_out, uint8_t tag_out[POOLLINK_AUTH_TAG_SIZE])
{
    if (plaintext_len > POOLLINK_MAX_PAYLOAD) return false;

    uint8_t nonce[12]; buildNonce(0x00000001, seq, nonce);  // Device → Server
    uint8_t aad[9];    buildAAD(msg_type, seq, aad);

    if (plaintext && plaintext_len > 0)
        memcpy(ciphertext_out, plaintext, plaintext_len);  // in-place AEAD
    aead_seal(session_key, nonce, ciphertext_out, plaintext_len,
              aad, sizeof(aad), tag_out);
    return true;
}

bool CloudCrypto::decrypt(
    const uint8_t session_key[POOLLINK_SESSION_KEY_SIZE],
    uint8_t msg_type, uint64_t seq,
    const uint8_t* ciphertext, size_t ciphertext_len,
    uint8_t* plaintext_out, const uint8_t tag[POOLLINK_AUTH_TAG_SIZE])
{
    if (ciphertext_len > POOLLINK_MAX_PAYLOAD) return false;

    uint8_t nonce[12]; buildNonce(0x00000002, seq, nonce);  // Server → Device
    uint8_t aad[9];    buildAAD(msg_type, seq, aad);

    if (ciphertext && ciphertext_len > 0)
        memcpy(plaintext_out, ciphertext, ciphertext_len);  // in-place AEAD
    return aead_open(session_key, nonce, plaintext_out, ciphertext_len,
                     aad, sizeof(aad), tag);
}

// ─── Random bytes ────────────────────────────────────────────────────────────

void CloudCrypto::randomBytes(uint8_t* buf, size_t len)
{
    rng_fill(buf, len);
}

// ─── Account pairing (PoolLink POP) ──────────────────────────────────────────

void CloudCrypto::accountPairingHmac(
    const uint8_t psk[32],
    const char* hostname_ascii,
    const uint8_t nonce[POOLLINK_NONCE_SIZE],
    uint8_t proof_out[32])
{
    static const char PREFIX[] = "poollink-account-bind-v1";  // + implicit '\0' → DOMAIN_PREFIX
    size_t hl = hostname_ascii ? strlen(hostname_ascii) : 0;
    if (hl > POOLLINK_MAX_HOSTNAME)   // never overflow msg[]; matches _sendHello's cap
        hl = POOLLINK_MAX_HOSTNAME;
    uint8_t msg[sizeof(PREFIX) + POOLLINK_MAX_HOSTNAME + 1 + POOLLINK_NONCE_SIZE];
    size_t pos = 0;
    memcpy(msg + pos, PREFIX, sizeof(PREFIX));  // includes terminating NUL (matches Python DOMAIN_PREFIX)
    pos += sizeof(PREFIX);
    memcpy(msg + pos, hostname_ascii, hl);
    pos += hl;
    msg[pos++] = 0;
    memcpy(msg + pos, nonce, POOLLINK_NONCE_SIZE);
    pos += POOLLINK_NONCE_SIZE;

    hmac_sha256(psk, 32, msg, pos, proof_out);
}

// ─── Known-answer self-test ──────────────────────────────────────────────────
// Vectors from lib/PoolLink/test/gen_kat.py (reference: python `cryptography`).
// psk=0x00..0x1f, client_nonce=0xA0.., server_nonce=0x50..
bool CloudCrypto::selfTest()
{
    uint8_t psk[32];          for (int i = 0; i < 32; i++) psk[i] = (uint8_t)i;
    uint8_t cn[POOLLINK_NONCE_SIZE]; for (int i = 0; i < POOLLINK_NONCE_SIZE; i++) cn[i] = 0xA0 + i;
    uint8_t sn[POOLLINK_NONCE_SIZE]; for (int i = 0; i < POOLLINK_NONCE_SIZE; i++) sn[i] = 0x50 + i;

    static const uint8_t SK[32]  = {0xe0,0x54,0xae,0x5d,0x8a,0x88,0x28,0x57,0xd1,0x39,0xcb,0xc5,0x7c,0xf0,0x9f,0x04,0x45,0xb2,0xa7,0xa1,0xc7,0x0d,0x09,0xba,0x6e,0x18,0xc4,0x5a,0xde,0x97,0xb9,0xd6};
    static const uint8_t ECT[7]  = {0xe6,0xd6,0xe9,0x41,0x12,0x7a,0x92};
    static const uint8_t ETAG[16]= {0xee,0x9b,0x77,0x47,0x1f,0x64,0xd1,0x0b,0x8a,0x94,0xff,0x36,0x3f,0xf7,0x91,0xb8};
    static const uint8_t DCT[7]  = {0x3a,0x9d,0xef,0x5c,0x65,0x53,0xdd};
    static const uint8_t DTAG[16]= {0x36,0xaa,0xa4,0xf6,0xdb,0x76,0x38,0x79,0x8c,0xfe,0x95,0x20,0xd9,0x06,0x17,0x84};
    static const uint8_t DPT[7]  = {0x7b,0x22,0x61,0x22,0x3a,0x32,0x7d};
    static const uint8_t HTAG[16]= {0x3f,0xbd,0x14,0x27,0x85,0x06,0xc2,0x3d,0x5a,0xf9,0xcd,0x6d,0xad,0x70,0xda,0xab};

    // 1) HKDF session key
    uint8_t sk[32];
    deriveSessionKey(psk, cn, sn, sk);
    if (memcmp(sk, SK, 32) != 0) return false;

    // 2) encrypt (device→server): msg_type 0x03, seq 1, pt {"x":1}
    const uint8_t pt[7] = {'{','"','x','"',':','1','}'};
    uint8_t ct[7], tag[16];
    if (!encrypt(sk, 0x03, 1, pt, sizeof(pt), ct, tag)) return false;
    if (memcmp(ct, ECT, 7) != 0 || memcmp(tag, ETAG, 16) != 0) return false;

    // 3) HELLO auth tag
    if (!verifyHelloAuthTag(sk, HTAG)) return false;

    // 4) decrypt (server→device): msg_type 0x04, seq 1 → recovers {"a":2}
    uint8_t out[7];
    if (!decrypt(sk, 0x04, 1, DCT, 7, out, DTAG)) return false;
    if (memcmp(out, DPT, 7) != 0) return false;

    // 5) tamper rejection: flip one ciphertext byte → must fail auth
    uint8_t bad[7]; memcpy(bad, DCT, 7); bad[0] ^= 0x01;
    if (decrypt(sk, 0x04, 1, bad, 7, out, DTAG)) return false;

    return true;
}

size_t CloudCrypto::base64UrlEncodeNoPad(const uint8_t* data, size_t len, char* out)
{
    static const char tbl[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    size_t out_len = 0;
    for (size_t i = 0; i < len; i += 3) {
        uint32_t v = (uint32_t)data[i] << 16;
        if (i + 1 < len) v |= (uint32_t)data[i + 1] << 8;
        if (i + 2 < len) v |= data[i + 2];
        out[out_len++] = tbl[(v >> 18) & 63];
        out[out_len++] = tbl[(v >> 12) & 63];
        if (i + 1 < len) out[out_len++] = tbl[(v >> 6) & 63];
        if (i + 2 < len) out[out_len++] = tbl[v & 63];
    }
    out[out_len] = '\0';
    return out_len;
}
