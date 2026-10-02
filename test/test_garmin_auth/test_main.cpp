// SHA-256 (FIPS 180-4 examples), HMAC-SHA256 (RFC 4231) and the Garmin App Remote MACs
// (docs/garmin-protocol.md 16.2/16.3). Board-agnostic: the same code runs on the device.
#include <unity.h>

#include <cstring>
#include <string>
#include <vector>

#include "Garmin/GarminProtocol.h"
#include "Garmin/Sha256.h"
#include "../common/check.h"
#include "../common/host_globals.h"

void setUp() {}
void tearDown() {}

namespace {
    std::vector<uint8_t> hex(const char *text) {
        std::vector<uint8_t> out;
        int nibble = -1;
        for (const char *p = text; *p; p++) {
            int v;
            if (*p >= '0' && *p <= '9') v = *p - '0';
            else if (*p >= 'a' && *p <= 'f') v = *p - 'a' + 10;
            else continue;
            if (nibble < 0) {
                nibble = v;
            } else {
                out.push_back(static_cast<uint8_t>(nibble << 4 | v));
                nibble = -1;
            }
        }
        return out;
    }

    std::vector<uint8_t> bytes(const char *text) {
        return std::vector<uint8_t>(text, text + strlen(text));
    }

    std::string dump(const uint8_t *data, const size_t len) {
        std::string s;
        for (size_t i = 0; i < len; i++) s += strf("%02x", data[i]);
        return s;
    }

    void expectHex(const char *expectedHex, const uint8_t *data, const size_t len, const char *what) {
        const std::vector<uint8_t> expected = hex(expectedHex);
        CHECK(expected.size() == len && memcmp(expected.data(), data, len) == 0, "%s: got %s, expected %s", what,
              dump(data, len).c_str(), dump(expected.data(), expected.size()).c_str());
    }

    void expectSha(const char *expectedHex, const std::vector<uint8_t> &message, const char *what) {
        uint8_t digest[Sha256::DIGEST_SIZE];
        Sha256::hash(message.data(), message.size(), digest);
        expectHex(expectedHex, digest, sizeof(digest), what);
    }

    void expectHmac(const char *expectedHex, const std::vector<uint8_t> &key, const std::vector<uint8_t> &data,
                    const char *what, const size_t truncate = Sha256::DIGEST_SIZE) {
        uint8_t mac[Sha256::DIGEST_SIZE];
        HmacSha256::mac(key.data(), key.size(), data.data(), data.size(), mac);
        expectHex(expectedHex, mac, truncate, what);
    }

    std::vector<uint8_t> key16() {
        return hex("00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f");
    }
}

void test_sha256_fips() {
    expectSha("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", bytes(""), "empty");
    expectSha("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", bytes("abc"), "abc");
    expectSha("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
              bytes("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"), "448-bit");
    expectSha("cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1",
              bytes("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqr"
                    "lmnopqrsmnopqrstnopqrstu"), "896-bit");
}

void test_sha256_incremental_and_padding_edges() {
    // Lengths around the 55/56/64-byte padding boundaries, hashed whole and byte by byte.
    std::vector<uint8_t> data(130);
    for (size_t i = 0; i < data.size(); i++) data[i] = static_cast<uint8_t>(i * 7 + 1);
    const size_t lengths[] = {55, 56, 57, 63, 64, 65, 119, 120, 128, 130};
    for (size_t k = 0; k < sizeof(lengths) / sizeof(lengths[0]); k++) {
        uint8_t whole[32];
        uint8_t pieces[32];
        Sha256::hash(data.data(), lengths[k], whole);
        Sha256 sha;
        for (size_t i = 0; i < lengths[k]; i++) sha.update(&data[i], 1);
        sha.finish(pieces);
        CHECK(memcmp(whole, pieces, 32) == 0, "length %u incremental", (unsigned) lengths[k]);
    }

    // One million 'a' (FIPS 180-2 C.3), in 1000-byte updates.
    std::vector<uint8_t> a(1000, 'a');
    Sha256 sha;
    for (int i = 0; i < 1000; i++) sha.update(a.data(), a.size());
    uint8_t digest[32];
    sha.finish(digest);
    expectHex("cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0", digest, 32, "million a");
}

void test_hmac_rfc4231() {
    expectHmac("b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7",
               std::vector<uint8_t>(20, 0x0b), bytes("Hi There"), "case 1");
    expectHmac("5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843",
               bytes("Jefe"), bytes("what do ya want for nothing?"), "case 2");
    expectHmac("773ea91e36800e46854db8ebd09181a72959098b3ef8c122d9635514ced565fe",
               std::vector<uint8_t>(20, 0xaa), std::vector<uint8_t>(50, 0xdd), "case 3");
    expectHmac("82558a389a443c0ea4cc819899f2083a85f0faa3e578f8077a2e3ff46729665b",
               hex("0102030405060708090a0b0c0d0e0f10111213141516171819"), std::vector<uint8_t>(50, 0xcd),
               "case 4");
    expectHmac("a3b6167473100ee06e0c796c2955552b", std::vector<uint8_t>(20, 0x0c),
               bytes("Test With Truncation"), "case 5 (128-bit truncation)", 16);
    expectHmac("60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54",
               std::vector<uint8_t>(131, 0xaa), bytes("Test Using Larger Than Block-Size Key - Hash Key First"),
               "case 6");
    expectHmac("9b09ffa71b942fcb27635fbcd5b0e944bfdc63644f0713938a7f51535c3a35e2",
               std::vector<uint8_t>(131, 0xaa),
               bytes("This is a test using a larger than block-size key and a larger than block-size data. "
                     "The key needs to be hashed before being used by the HMAC algorithm."),
               "case 7");
}

void test_spec_full_hmacs() {
    expectHmac("4e470a94b924acae1ef4234f341a5c760ddebc1858661c39580b7201b2a95047", key16(),
               hex("57 a0 a1 a2 a3 a4 a5 a6 a7"), "16.2 HMAC(K, W||Nb)");
    expectHmac("1857e1b4a96ebf5a8236582beab5ab29d65cef89dc6c9aab8e492a0a97ed76ce", key16(),
               hex("42 b0 b1 b2 b3 b4 b5 b6 b7"), "16.2 HMAC(K, B||Nw)");
}

void test_spec_auth_exchange() {
    const std::vector<uint8_t> key = key16();
    const std::vector<uint8_t> nb = hex("a0 a1 a2 a3 a4 a5 a6 a7");
    const std::vector<uint8_t> nw = hex("b0 b1 b2 b3 b4 b5 b6 b7");

    uint8_t mac[Garmin::MAC_SIZE];
    GarminAuth::watchMac(key.data(), nb.data(), mac);
    expectHex("4e470a94b924acae", mac, sizeof(mac), "16.2 watch MAC");
    GarminAuth::boardMac(key.data(), nw.data(), mac);
    expectHex("1857e1b4a96ebf5a", mac, sizeof(mac), "16.2 board MAC");

    // The board's side of 16.2: parse the proof write, verify it, answer with its MAC.
    const std::vector<uint8_t> write = hex("00 01 02 b0 b1 b2 b3 b4 b5 b6 b7 4e 47 0a 94 b9 24 ac ae");
    Garmin::AuthProof proof{};
    TEST_ASSERT_TRUE(Garmin::parseAuthProof(write.data(), write.size(), proof));
    TEST_ASSERT_TRUE(GarminAuth::verifyWatchMac(key.data(), nb.data(), proof.mac));
    GarminAuth::boardMac(key.data(), proof.nw, mac);
    const Garmin::Frame result = Garmin::buildAuthResultOk(mac);
    expectHex("00 02 00 18 57 e1 b4 a9 6e bf 5a", result.data, result.len, "16.2 result read");

    // Any flipped bit in the proof, the nonce or the key fails.
    for (uint8_t bit = 0; bit < 64; bit++) {
        uint8_t bad[Garmin::MAC_SIZE];
        memcpy(bad, proof.mac, sizeof(bad));
        bad[bit / 8] ^= static_cast<uint8_t>(1 << (bit % 8));
        CHECK(!GarminAuth::verifyWatchMac(key.data(), nb.data(), bad), "flipped MAC bit %u accepted", bit);
    }
    std::vector<uint8_t> otherNb = nb;
    otherNb[7] ^= 1;
    TEST_ASSERT_FALSE_MESSAGE(GarminAuth::verifyWatchMac(key.data(), otherNb.data(), proof.mac), "replayed proof");
    std::vector<uint8_t> otherKey = key;
    otherKey[0] ^= 1;
    TEST_ASSERT_FALSE_MESSAGE(GarminAuth::verifyWatchMac(otherKey.data(), nb.data(), proof.mac), "wrong key");
}

void test_spec_pairing_key_authenticates() {
    // 16.3: the key returned by pairing is the one 16.2 authenticates with.
    const std::vector<uint8_t> read = hex("00 02 00 02 00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f");
    const std::vector<uint8_t> nb = hex("a0 a1 a2 a3 a4 a5 a6 a7");
    uint8_t mac[Garmin::MAC_SIZE];
    GarminAuth::watchMac(&read[4], nb.data(), mac);
    expectHex("4e470a94b924acae", mac, sizeof(mac), "16.3 key -> 16.2 proof");
    TEST_ASSERT_EQUAL_UINT8(2, read[3]);
}

void test_constant_time_equals() {
    const uint8_t a[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint8_t b[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    TEST_ASSERT_TRUE(GarminAuth::constantTimeEquals(a, b, 8));
    TEST_ASSERT_TRUE_MESSAGE(GarminAuth::constantTimeEquals(a, b, 0), "zero length");
    for (uint8_t i = 0; i < 8; i++) {
        b[i] ^= 0x80;
        CHECK(!GarminAuth::constantTimeEquals(a, b, 8), "difference at %u", i);
        b[i] ^= 0x80;
    }
    b[7] = 0;
    TEST_ASSERT_TRUE_MESSAGE(GarminAuth::constantTimeEquals(a, b, 7), "only the compared prefix counts");
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_sha256_fips);
    RUN_TEST(test_sha256_incremental_and_padding_edges);
    RUN_TEST(test_hmac_rfc4231);
    RUN_TEST(test_spec_full_hmacs);
    RUN_TEST(test_spec_auth_exchange);
    RUN_TEST(test_spec_pairing_key_authenticates);
    RUN_TEST(test_constant_time_equals);
    return UNITY_END();
}
