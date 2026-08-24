// RFC 8439 ChaCha20-Poly1305, portable C99. See chachapoly_sw.h for why.
// ChaCha20 is the reference construction; Poly1305 follows the widely used
// 26-bit-limb layout (poly1305-donna-32, public domain, Andrew Moon).
// Verified against the RFC 8439 §2.8.2 vector (test/test_chachapoly_sw.c)
// and the PoolLink known-answer selfTest() on target.

#include "chachapoly_sw.h"
#include <string.h>

static uint32_t load32_le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void store32_le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

// ─── ChaCha20 ────────────────────────────────────────────────────────────────

static uint32_t rotl32(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

#define QR(a, b, c, d)                          \
    a += b; d ^= a; d = rotl32(d, 16);          \
    c += d; b ^= c; b = rotl32(b, 12);          \
    a += b; d ^= a; d = rotl32(d, 8);           \
    c += d; b ^= c; b = rotl32(b, 7);

static void chacha20_block(const uint8_t key[32], uint32_t counter,
                           const uint8_t nonce[12], uint8_t out[64])
{
    uint32_t s[16], x[16];
    int i;
    s[0] = 0x61707865; s[1] = 0x3320646e; s[2] = 0x79622d32; s[3] = 0x6b206574;
    for (i = 0; i < 8; i++) s[4 + i] = load32_le(key + 4 * i);
    s[12] = counter;
    for (i = 0; i < 3; i++) s[13 + i] = load32_le(nonce + 4 * i);
    memcpy(x, s, sizeof(x));
    for (i = 0; i < 10; i++) {
        QR(x[0], x[4], x[8],  x[12]) QR(x[1], x[5], x[9],  x[13])
        QR(x[2], x[6], x[10], x[14]) QR(x[3], x[7], x[11], x[15])
        QR(x[0], x[5], x[10], x[15]) QR(x[1], x[6], x[11], x[12])
        QR(x[2], x[7], x[8],  x[13]) QR(x[3], x[4], x[9],  x[14])
    }
    for (i = 0; i < 16; i++) store32_le(out + 4 * i, x[i] + s[i]);
}

void chachapoly_sw_crypt(const uint8_t key[32], const uint8_t nonce[12],
                         uint8_t *data, size_t len)
{
    uint8_t ks[64];
    uint32_t counter = 1;
    size_t off = 0;
    while (off < len) {
        size_t n = len - off < 64 ? len - off : 64;
        chacha20_block(key, counter++, nonce, ks);
        for (size_t i = 0; i < n; i++) data[off + i] ^= ks[i];
        off += n;
    }
}

// ─── Poly1305 ────────────────────────────────────────────────────────────────

typedef struct {
    uint32_t r[5];   // clamped key, 26-bit limbs
    uint32_t h[5];   // accumulator, 26-bit limbs
    uint32_t pad[4]; // s (added mod 2^128 at the end)
} poly1305_state;

static void poly1305_init(poly1305_state *st, const uint8_t key[32])
{
    uint32_t t0 = load32_le(key + 0), t1 = load32_le(key + 4);
    uint32_t t2 = load32_le(key + 8), t3 = load32_le(key + 12);
    // r &= 0x0ffffffc0ffffffc0ffffffc0fffffff
    st->r[0] = t0 & 0x3ffffff;
    st->r[1] = ((t0 >> 26) | (t1 << 6)) & 0x3ffff03;
    st->r[2] = ((t1 >> 20) | (t2 << 12)) & 0x3ffc0ff;
    st->r[3] = ((t2 >> 14) | (t3 << 18)) & 0x3f03fff;
    st->r[4] = (t3 >> 8) & 0x00fffff;
    memset(st->h, 0, sizeof(st->h));
    for (int i = 0; i < 4; i++) st->pad[i] = load32_le(key + 16 + 4 * i);
}

// One full 16-byte block. The AEAD MAC input is always 16-aligned (aad and
// ciphertext are zero-padded, the length block is exactly 16 bytes), so the
// 0x01-append partial-block path of general Poly1305 is never needed here.
static void poly1305_block16(poly1305_state *st, const uint8_t m[16])
{
    const uint32_t r0 = st->r[0], r1 = st->r[1], r2 = st->r[2],
                   r3 = st->r[3], r4 = st->r[4];
    const uint32_t s1 = r1 * 5, s2 = r2 * 5, s3 = r3 * 5, s4 = r4 * 5;
    uint32_t h0 = st->h[0], h1 = st->h[1], h2 = st->h[2],
             h3 = st->h[3], h4 = st->h[4];

    uint32_t t0 = load32_le(m + 0), t1 = load32_le(m + 4);
    uint32_t t2 = load32_le(m + 8), t3 = load32_le(m + 12);
    h0 += t0 & 0x3ffffff;
    h1 += ((t0 >> 26) | (t1 << 6)) & 0x3ffffff;
    h2 += ((t1 >> 20) | (t2 << 12)) & 0x3ffffff;
    h3 += ((t2 >> 14) | (t3 << 18)) & 0x3ffffff;
    h4 += (t3 >> 8) | (1u << 24);

    uint64_t d0 = (uint64_t)h0 * r0 + (uint64_t)h1 * s4 + (uint64_t)h2 * s3 + (uint64_t)h3 * s2 + (uint64_t)h4 * s1;
    uint64_t d1 = (uint64_t)h0 * r1 + (uint64_t)h1 * r0 + (uint64_t)h2 * s4 + (uint64_t)h3 * s3 + (uint64_t)h4 * s2;
    uint64_t d2 = (uint64_t)h0 * r2 + (uint64_t)h1 * r1 + (uint64_t)h2 * r0 + (uint64_t)h3 * s4 + (uint64_t)h4 * s3;
    uint64_t d3 = (uint64_t)h0 * r3 + (uint64_t)h1 * r2 + (uint64_t)h2 * r1 + (uint64_t)h3 * r0 + (uint64_t)h4 * s4;
    uint64_t d4 = (uint64_t)h0 * r4 + (uint64_t)h1 * r3 + (uint64_t)h2 * r2 + (uint64_t)h3 * r1 + (uint64_t)h4 * r0;

    uint32_t c;
    c = (uint32_t)(d0 >> 26); h0 = (uint32_t)d0 & 0x3ffffff; d1 += c;
    c = (uint32_t)(d1 >> 26); h1 = (uint32_t)d1 & 0x3ffffff; d2 += c;
    c = (uint32_t)(d2 >> 26); h2 = (uint32_t)d2 & 0x3ffffff; d3 += c;
    c = (uint32_t)(d3 >> 26); h3 = (uint32_t)d3 & 0x3ffffff; d4 += c;
    c = (uint32_t)(d4 >> 26); h4 = (uint32_t)d4 & 0x3ffffff; h0 += c * 5;
    c = h0 >> 26; h0 &= 0x3ffffff; h1 += c;

    st->h[0] = h0; st->h[1] = h1; st->h[2] = h2; st->h[3] = h3; st->h[4] = h4;
}

static void poly1305_finish(poly1305_state *st, uint8_t mac[16])
{
    uint32_t h0 = st->h[0], h1 = st->h[1], h2 = st->h[2],
             h3 = st->h[3], h4 = st->h[4];
    uint32_t c;
    c = h1 >> 26; h1 &= 0x3ffffff; h2 += c;
    c = h2 >> 26; h2 &= 0x3ffffff; h3 += c;
    c = h3 >> 26; h3 &= 0x3ffffff; h4 += c;
    c = h4 >> 26; h4 &= 0x3ffffff; h0 += c * 5;
    c = h0 >> 26; h0 &= 0x3ffffff; h1 += c;

    // g = h - p; keep g when it didn't borrow (h >= p), else keep h
    uint32_t g0 = h0 + 5; c = g0 >> 26; g0 &= 0x3ffffff;
    uint32_t g1 = h1 + c; c = g1 >> 26; g1 &= 0x3ffffff;
    uint32_t g2 = h2 + c; c = g2 >> 26; g2 &= 0x3ffffff;
    uint32_t g3 = h3 + c; c = g3 >> 26; g3 &= 0x3ffffff;
    uint32_t g4 = h4 + c - (1u << 26);

    uint32_t mask = (g4 >> 31) - 1; // all-ones when h >= p
    g0 &= mask; g1 &= mask; g2 &= mask; g3 &= mask; g4 &= mask;
    mask = ~mask;
    h0 = (h0 & mask) | g0; h1 = (h1 & mask) | g1; h2 = (h2 & mask) | g2;
    h3 = (h3 & mask) | g3; h4 = (h4 & mask) | g4;

    // pack to 2^32 limbs
    h0 = h0 | (h1 << 26);
    h1 = (h1 >> 6) | (h2 << 20);
    h2 = (h2 >> 12) | (h3 << 14);
    h3 = (h3 >> 18) | (h4 << 8);

    // mac = (h + pad) mod 2^128
    uint64_t f;
    f = (uint64_t)h0 + st->pad[0];             store32_le(mac + 0,  (uint32_t)f);
    f = (uint64_t)h1 + st->pad[1] + (f >> 32); store32_le(mac + 4,  (uint32_t)f);
    f = (uint64_t)h2 + st->pad[2] + (f >> 32); store32_le(mac + 8,  (uint32_t)f);
    f = (uint64_t)h3 + st->pad[3] + (f >> 32); store32_le(mac + 12, (uint32_t)f);
}

// full blocks, then a zero-padded tail block (RFC 8439 pad16)
static void poly1305_feed_padded(poly1305_state *st, const uint8_t *data, size_t len)
{
    while (len >= 16) { poly1305_block16(st, data); data += 16; len -= 16; }
    if (len) {
        uint8_t b[16] = {0};
        memcpy(b, data, len);
        poly1305_block16(st, b);
    }
}

void chachapoly_sw_tag(const uint8_t key[32], const uint8_t nonce[12],
                       const uint8_t *ct, size_t ct_len,
                       const uint8_t *aad, size_t aad_len, uint8_t tag[16])
{
    uint8_t polykey[64];
    chacha20_block(key, 0, nonce, polykey); // block 0 keys the MAC

    poly1305_state st;
    poly1305_init(&st, polykey);
    poly1305_feed_padded(&st, aad, aad_len);
    poly1305_feed_padded(&st, ct, ct_len);

    uint8_t lens[16] = {0};
    store32_le(lens + 0, (uint32_t)aad_len); // LE64; sizes never exceed 32 bits here
    store32_le(lens + 8, (uint32_t)ct_len);
    poly1305_block16(&st, lens);
    poly1305_finish(&st, tag);
}
