#ifndef GARMIN_SHA256_H
#define GARMIN_SHA256_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "GarminProtocol.h"

/**
 * Portable SHA-256 / HMAC-SHA256 (FIPS 180-4, RFC 2104) so the host suites test the code the
 * board runs. It runs in the NimBLE host task (6144 B stack): an HMAC peaks well under 1 KB.
 */

class Sha256 {
public:
    enum : uint8_t { BLOCK_SIZE = 64, DIGEST_SIZE = 32 };

    Sha256() {
        reset();
    }

    void reset() {
        state[0] = 0x6a09e667u;
        state[1] = 0xbb67ae85u;
        state[2] = 0x3c6ef372u;
        state[3] = 0xa54ff53au;
        state[4] = 0x510e527fu;
        state[5] = 0x9b05688cu;
        state[6] = 0x1f83d9abu;
        state[7] = 0x5be0cd19u;
        bitCount = 0;
        bufferLen = 0;
    }

    void update(const uint8_t *data, size_t len) {
        while (len > 0) {
            size_t n = BLOCK_SIZE - bufferLen;
            if (n > len) n = len;
            memcpy(buffer + bufferLen, data, n);
            bufferLen = static_cast<uint8_t>(bufferLen + n);
            bitCount += static_cast<uint64_t>(n) * 8;
            data += n;
            len -= n;
            if (bufferLen == BLOCK_SIZE) {
                transform(buffer);
                bufferLen = 0;
            }
        }
    }

    void finish(uint8_t out[DIGEST_SIZE]) {
        const uint64_t bits = bitCount;
        const uint8_t pad = 0x80;
        update(&pad, 1);
        const uint8_t zero = 0;
        while (bufferLen != BLOCK_SIZE - 8) update(&zero, 1);
        uint8_t length[8];
        for (uint8_t i = 0; i < 8; i++) length[i] = static_cast<uint8_t>(bits >> (56 - 8 * i));
        update(length, 8);
        for (uint8_t i = 0; i < 8; i++) {
            out[4 * i] = static_cast<uint8_t>(state[i] >> 24);
            out[4 * i + 1] = static_cast<uint8_t>(state[i] >> 16);
            out[4 * i + 2] = static_cast<uint8_t>(state[i] >> 8);
            out[4 * i + 3] = static_cast<uint8_t>(state[i]);
        }
    }

    static void hash(const uint8_t *data, const size_t len, uint8_t out[DIGEST_SIZE]) {
        Sha256 sha;
        sha.update(data, len);
        sha.finish(out);
    }

private:
    uint32_t state[8];
    uint64_t bitCount;
    uint8_t buffer[BLOCK_SIZE];
    uint8_t bufferLen;

    static uint32_t rotr(const uint32_t x, const uint8_t n) {
        return (x >> n) | (x << (32 - n));
    }

    void transform(const uint8_t *block) {
        // Function-local static: one copy in flash, not 256 B on the host task's stack.
        static const uint32_t K[64] = {
            0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
            0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
            0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
            0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
            0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
            0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
            0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
            0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
        };

        // 16-word rolling schedule instead of 64 words: 192 B less stack.
        uint32_t w[16];
        for (uint8_t i = 0; i < 16; i++) {
            w[i] = (static_cast<uint32_t>(block[4 * i]) << 24) | (static_cast<uint32_t>(block[4 * i + 1]) << 16)
                   | (static_cast<uint32_t>(block[4 * i + 2]) << 8) | block[4 * i + 3];
        }

        uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
        uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
        for (uint8_t i = 0; i < 64; i++) {
            if (i >= 16) {
                const uint32_t w15 = w[(i - 15) & 15];
                const uint32_t w2 = w[(i - 2) & 15];
                const uint32_t s0 = rotr(w15, 7) ^ rotr(w15, 18) ^ (w15 >> 3);
                const uint32_t s1 = rotr(w2, 17) ^ rotr(w2, 19) ^ (w2 >> 10);
                w[i & 15] += s0 + w[(i - 7) & 15] + s1;
            }
            const uint32_t t1 = h + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i & 15];
            const uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            h = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }
        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
        state[5] += f;
        state[6] += g;
        state[7] += h;
    }
};

class HmacSha256 {
public:
    HmacSha256(const uint8_t *key, size_t keyLen) {
        uint8_t hashedKey[Sha256::DIGEST_SIZE];
        if (keyLen > Sha256::BLOCK_SIZE) {
            Sha256::hash(key, keyLen, hashedKey);
            key = hashedKey;
            keyLen = Sha256::DIGEST_SIZE;
        }
        memset(outerPad, 0x5c, sizeof(outerPad));
        uint8_t innerPad[Sha256::BLOCK_SIZE];
        memset(innerPad, 0x36, sizeof(innerPad));
        for (size_t i = 0; i < keyLen; i++) {
            innerPad[i] ^= key[i];
            outerPad[i] ^= key[i];
        }
        inner.update(innerPad, sizeof(innerPad));
    }

    void update(const uint8_t *data, const size_t len) {
        inner.update(data, len);
    }

    void finish(uint8_t out[Sha256::DIGEST_SIZE]) {
        uint8_t innerDigest[Sha256::DIGEST_SIZE];
        inner.finish(innerDigest);
        Sha256 outer;
        outer.update(outerPad, sizeof(outerPad));
        outer.update(innerDigest, sizeof(innerDigest));
        outer.finish(out);
    }

    static void mac(const uint8_t *key, const size_t keyLen, const uint8_t *data, const size_t len,
                    uint8_t out[Sha256::DIGEST_SIZE]) {
        HmacSha256 hmac(key, keyLen);
        hmac.update(data, len);
        hmac.finish(out);
    }

private:
    Sha256 inner;
    uint8_t outerPad[Sha256::BLOCK_SIZE];
};

namespace GarminAuth {
    using Garmin::KEY_SIZE;
    using Garmin::NONCE_SIZE;
    using Garmin::MAC_SIZE;

    // Time depends only on `len`, never on where the first difference is.
    inline bool constantTimeEquals(const uint8_t *a, const uint8_t *b, const size_t len) {
        volatile uint8_t diff = 0;
        for (size_t i = 0; i < len; i++) diff = static_cast<uint8_t>(diff | (a[i] ^ b[i]));
        return diff == 0;
    }

    // Spec 8: MAC(x) = first 8 bytes of HMAC-SHA256(K, x), x = tag || nonce.
    inline void mac(const uint8_t key[KEY_SIZE], const uint8_t tag, const uint8_t nonce[NONCE_SIZE],
                    uint8_t out[MAC_SIZE]) {
        HmacSha256 hmac(key, KEY_SIZE);
        hmac.update(&tag, 1);
        hmac.update(nonce, NONCE_SIZE);
        uint8_t full[Sha256::DIGEST_SIZE];
        hmac.finish(full);
        memcpy(out, full, MAC_SIZE);
    }

    // The watch's proof: MAC("W" || Nb).
    inline void watchMac(const uint8_t key[KEY_SIZE], const uint8_t nb[NONCE_SIZE], uint8_t out[MAC_SIZE]) {
        mac(key, 0x57, nb, out);
    }

    // The board's answer: MAC("B" || Nw).
    inline void boardMac(const uint8_t key[KEY_SIZE], const uint8_t nw[NONCE_SIZE], uint8_t out[MAC_SIZE]) {
        mac(key, 0x42, nw, out);
    }

    inline bool verifyWatchMac(const uint8_t key[KEY_SIZE], const uint8_t nb[NONCE_SIZE],
                               const uint8_t received[MAC_SIZE]) {
        uint8_t expected[MAC_SIZE];
        watchMac(key, nb, expected);
        return constantTimeEquals(expected, received, MAC_SIZE);
    }
}

#endif //GARMIN_SHA256_H
