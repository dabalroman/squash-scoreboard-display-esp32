// OTA image check (FirmwareImageCheck::Accumulator) on synthetic images laid out like
// a real build: image header, first segment header, esp_app_desc_t, then the board
// marker or - in a pre-marker build - .flash.rodata's first strings. Both boards.
#include <unity.h>

#include <cstring>
#include <vector>

#include <Arduino.h>

#include "FirmwareImageCheck.h"
#include "../common/check.h"
#include "../common/host_globals.h"

using FirmwareImageCheck::Accumulator;
using FirmwareImageCheck::Verdict;

void setUp() {}
void tearDown() {}

namespace {
    enum class Kind { Marked, Unmarked, Bootloader, NotEsp };

    struct Spec {
        Kind kind;
        uint16_t chipId;
        uint8_t boardRev;
    };

    Spec marked(const uint8_t rev) {
        return Spec{Kind::Marked, ESP_CHIP_ID_ESP32S3, rev};
    }

    Spec ofKind(const Kind kind) {
        return Spec{kind, ESP_CHIP_ID_ESP32S3, BOARD_REV};
    }

    void put32(std::vector<uint8_t> &image, const size_t at, const uint32_t value) {
        memcpy(&image[at], &value, sizeof(value));
    }

    std::vector<uint8_t> makeImage(const Spec &spec) {
        std::vector<uint8_t> image(4096, 0xA5);

        esp_image_header_t header = {};
        header.magic = spec.kind == Kind::NotEsp ? 0x7F : ESP_IMAGE_HEADER_MAGIC;
        header.segment_count = 5;
        header.chip_id = static_cast<esp_chip_id_t>(spec.chipId);
        memcpy(&image[0], &header, sizeof(header));

        const esp_image_segment_header_t segment = {0x3C0A0020, 185616};
        memcpy(&image[sizeof(header)], &segment, sizeof(segment));

        const size_t desc = FirmwareImageCheck::APP_DESC_OFFSET;
        if (spec.kind == Kind::Bootloader) {
            // A bootloader's first segment is code/data, not an app descriptor.
            put32(image, desc, 0x40378000);
            return image;
        }

        esp_app_desc_t app = {};
        app.magic_word = ESP_APP_DESC_MAGIC_WORD;
        strcpy(app.project_name, "arduino-lib-builder");
        strcpy(app.idf_ver, "v4.4.7");
        memcpy(&image[desc], &app, sizeof(app));

        if (spec.kind == Kind::Marked) {
            const BoardMarker::Marker marker = {BoardMarker::MAGIC, spec.boardRev, {0, 0, 0}};
            memcpy(&image[BoardMarker::IMAGE_OFFSET], &marker, sizeof(marker));
        } else {
            // What the pre-marker v1/v2 builds carry at 288: .rodata_wlog_error.
            const char legacy[] = "wifi ipc: failed to post wifi task";
            memcpy(&image[BoardMarker::IMAGE_OFFSET], legacy, sizeof(legacy));
        }
        return image;
    }

    Verdict judgeWhole(const std::vector<uint8_t> &image, const uint8_t receivingRev) {
        Accumulator acc;
        acc.feed(image.data(), image.size());
        return acc.verdict(receivingRev);
    }
}

void test_layout_constants() {
    TEST_ASSERT_EQUAL_UINT32(32, FirmwareImageCheck::APP_DESC_OFFSET);
    TEST_ASSERT_EQUAL_UINT32(288, BoardMarker::IMAGE_OFFSET);
    TEST_ASSERT_EQUAL_UINT32(296, FirmwareImageCheck::HEADER_BYTES);
    TEST_ASSERT_EQUAL_UINT8(BOARD_REV, BoardMarker::RUNNING_REV);
    TEST_ASSERT_EQUAL_UINT8(2, BoardMarker::LEGACY_UNMARKED_REV);
}

// Printable bytes in the magic would let a legacy image's rodata string match it.
void test_magic_has_no_printable_byte() {
    for (int shift = 0; shift < 32; shift += 8) {
        const unsigned byte = (BoardMarker::MAGIC >> shift) & 0xFF;
        CHECK(byte < 0x20 || byte > 0x7E, "magic byte 0x%02X is printable", byte);
    }
}

void test_marked_same_board_ok() {
    for (uint8_t rev = 1; rev <= 2; rev++) {
        CHECK(judgeWhole(makeImage(marked(rev)), rev) == Verdict::Ok, "v%u image on v%u", rev, rev);
    }
}

void test_marked_other_board_rejected() {
    CHECK(judgeWhole(makeImage(marked(2)), 1) == Verdict::WrongBoard, "v2 image on v1");
    CHECK(judgeWhole(makeImage(marked(1)), 2) == Verdict::WrongBoard, "v1 image on v2");
    const uint8_t oddRevs[] = {0, 3, 7, 0xFF};
    for (uint8_t rev = 1; rev <= 2; rev++) {
        for (const uint8_t odd : oddRevs) {
            CHECK(judgeWhole(makeImage(marked(odd)), rev) == Verdict::WrongBoard, "rev %u on v%u", odd, rev);
        }
    }
}

// Only V2 ever ran an unmarked S3 image over OTA; V1's first S3 image went in over USB.
void test_unmarked_accepted_on_v2_only() {
    const std::vector<uint8_t> image = makeImage(ofKind(Kind::Unmarked));
    CHECK(judgeWhole(image, 2) == Verdict::Ok, "unmarked on v2");
    CHECK(judgeWhole(image, 1) == Verdict::MissingMarker, "unmarked on v1");
}

// A marker with one bit of the magic off is no marker.
void test_damaged_magic_is_unmarked() {
    for (size_t i = 0; i < 4; i++) {
        std::vector<uint8_t> image = makeImage(marked(1));
        image[BoardMarker::IMAGE_OFFSET + i] ^= 0x01;
        CHECK(judgeWhole(image, 1) == Verdict::MissingMarker, "byte %u flipped, v1", static_cast<unsigned>(i));
        CHECK(judgeWhole(image, 2) == Verdict::Ok, "byte %u flipped, v2", static_cast<unsigned>(i));
    }
}

void test_default_judges_as_running_board() {
    Accumulator acc;
    std::vector<uint8_t> image = makeImage(marked(BOARD_REV));
    acc.feed(image.data(), image.size());
    TEST_ASSERT_TRUE(acc.verdict() == Verdict::Ok);

    acc.reset();
    image = makeImage(marked(BOARD_REV == 1 ? 2 : 1));
    acc.feed(image.data(), image.size());
    TEST_ASSERT_TRUE(acc.verdict() == Verdict::WrongBoard);

    acc.reset();
    image = makeImage(ofKind(Kind::Unmarked));
    acc.feed(image.data(), image.size());
    const Verdict expected = BOARD_REV == 2 ? Verdict::Ok : Verdict::MissingMarker;
    TEST_ASSERT_TRUE(acc.verdict() == expected);
}

void test_not_firmware() {
    for (uint8_t rev = 1; rev <= 2; rev++) {
        CHECK(judgeWhole(makeImage(ofKind(Kind::NotEsp)), rev) == Verdict::NotFirmware, "no 0xE9 on v%u", rev);
        CHECK(judgeWhole(makeImage(ofKind(Kind::Bootloader)), rev) == Verdict::NotFirmware, "bootloader on v%u", rev);

        // Marker bytes where an app's would be do not make a bootloader an app.
        std::vector<uint8_t> image = makeImage(ofKind(Kind::Bootloader));
        const BoardMarker::Marker marker = {BoardMarker::MAGIC, rev, {0, 0, 0}};
        memcpy(&image[BoardMarker::IMAGE_OFFSET], &marker, sizeof(marker));
        CHECK(judgeWhole(image, rev) == Verdict::NotFirmware, "marked bootloader on v%u", rev);

        const std::vector<uint8_t> zeros(4096, 0);
        CHECK(judgeWhole(zeros, rev) == Verdict::NotFirmware, "zeros on v%u", rev);
    }
}

void test_wrong_chip() {
    const uint16_t chips[] = {ESP_CHIP_ID_ESP32, ESP_CHIP_ID_ESP32C3, 0xFFFF};
    const Kind kinds[] = {Kind::Marked, Kind::Unmarked, Kind::Bootloader};
    for (uint8_t rev = 1; rev <= 2; rev++) {
        for (const uint16_t chip : chips) {
            // Wrong chip outranks a missing descriptor and any marker verdict.
            for (const Kind kind : kinds) {
                const Spec spec{kind, chip, rev};
                CHECK(judgeWhole(makeImage(spec), rev) == Verdict::WrongChip, "chip 0x%04X kind %d on v%u",
                      chip, static_cast<int>(kind), rev);
            }
        }
    }
}

// A short upload never reaches a verdict; the upload handler turns that into NotFirmware.
void test_short_upload_pending() {
    const std::vector<uint8_t> image = makeImage(marked(BOARD_REV));
    for (size_t len = 0; len < FirmwareImageCheck::HEADER_BYTES; len++) {
        Accumulator acc;
        acc.feed(image.data(), len);
        CHECK(!acc.ready(), "ready after %u bytes", static_cast<unsigned>(len));
        CHECK(acc.verdict(1) == Verdict::Pending && acc.verdict(2) == Verdict::Pending,
              "not pending after %u bytes", static_cast<unsigned>(len));
        CHECK(acc.seenChipId() == 0xFFFF, "chip id seen after %u bytes", static_cast<unsigned>(len));
        CHECK(acc.seenBoardRev() == 0, "board seen after %u bytes", static_cast<unsigned>(len));
    }

    Accumulator acc;
    acc.feed(image.data(), FirmwareImageCheck::HEADER_BYTES);
    TEST_ASSERT_TRUE(acc.ready());
    TEST_ASSERT_TRUE(acc.verdict() == Verdict::Ok);
}

void test_one_byte_chunks() {
    const Spec specs[] = {marked(1), marked(2), ofKind(Kind::Unmarked), ofKind(Kind::Bootloader)};
    for (const Spec &spec : specs) {
        const std::vector<uint8_t> image = makeImage(spec);
        for (uint8_t rev = 1; rev <= 2; rev++) {
            Accumulator acc;
            for (size_t i = 0; i < image.size(); i++) {
                acc.feed(&image[i], 1);
            }
            CHECK(acc.verdict(rev) == judgeWhole(image, rev), "1-byte chunks differ, v%u", rev);
        }
    }
}

// Every split point up to past the marker, so each one inside it too.
void test_two_chunks_every_split() {
    const Spec specs[] = {marked(1), marked(2), ofKind(Kind::Unmarked)};
    for (const Spec &spec : specs) {
        const std::vector<uint8_t> image = makeImage(spec);
        for (size_t split = 0; split <= FirmwareImageCheck::HEADER_BYTES + 4; split++) {
            for (uint8_t rev = 1; rev <= 2; rev++) {
                Accumulator acc;
                acc.feed(image.data(), split);
                acc.feed(image.data() + split, image.size() - split);
                CHECK(acc.verdict(rev) == judgeWhole(image, rev), "split %u differs, v%u",
                      static_cast<unsigned>(split), rev);
            }
        }
    }
}

void test_later_chunks_and_null_ignored() {
    const std::vector<uint8_t> image = makeImage(marked(1));
    Accumulator acc;
    acc.feed(nullptr, 100);
    acc.feed(image.data(), image.size());
    const std::vector<uint8_t> other = makeImage(ofKind(Kind::NotEsp));
    acc.feed(other.data(), other.size());
    TEST_ASSERT_TRUE(acc.verdict(1) == Verdict::Ok);
    TEST_ASSERT_EQUAL_UINT8(1, acc.seenBoardRev());
    TEST_ASSERT_EQUAL_UINT16(ESP_CHIP_ID_ESP32S3, acc.seenChipId());
}

void test_reset_starts_over() {
    Accumulator acc;
    const std::vector<uint8_t> bad = makeImage(ofKind(Kind::NotEsp));
    acc.feed(bad.data(), bad.size());
    TEST_ASSERT_TRUE(acc.verdict(1) == Verdict::NotFirmware);

    acc.reset();
    TEST_ASSERT_TRUE(acc.verdict(1) == Verdict::Pending);
    const std::vector<uint8_t> good = makeImage(marked(1));
    acc.feed(good.data(), good.size());
    TEST_ASSERT_TRUE(acc.verdict(1) == Verdict::Ok);
}

void test_seen_board_rev() {
    Accumulator acc;
    std::vector<uint8_t> image = makeImage(marked(2));
    acc.feed(image.data(), image.size());
    TEST_ASSERT_EQUAL_UINT8(2, acc.seenBoardRev());

    acc.reset();
    image = makeImage(ofKind(Kind::Unmarked));
    acc.feed(image.data(), image.size());
    TEST_ASSERT_EQUAL_UINT8(0, acc.seenBoardRev());
}

void test_reasons_and_labels() {
    TEST_ASSERT_TRUE(FirmwareImageCheck::isWrongBoard(Verdict::WrongChip));
    TEST_ASSERT_TRUE(FirmwareImageCheck::isWrongBoard(Verdict::WrongBoard));
    TEST_ASSERT_TRUE(FirmwareImageCheck::isWrongBoard(Verdict::MissingMarker));
    TEST_ASSERT_FALSE(FirmwareImageCheck::isWrongBoard(Verdict::NotFirmware));
    TEST_ASSERT_FALSE(FirmwareImageCheck::isWrongBoard(Verdict::Ok));
    TEST_ASSERT_FALSE(FirmwareImageCheck::isWrongBoard(Verdict::Pending));

    const char *fallback = FirmwareImageCheck::reasonFor(Verdict::Pending);
    const Verdict rejects[] = {Verdict::NotFirmware, Verdict::WrongChip, Verdict::WrongBoard, Verdict::MissingMarker};
    for (const Verdict verdict : rejects) {
        const char *reason = FirmwareImageCheck::reasonFor(verdict);
        CHECK(reason != nullptr && strcmp(reason, fallback) != 0, "verdict %u has no own reason",
              static_cast<unsigned>(verdict));
    }
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_layout_constants);
    RUN_TEST(test_magic_has_no_printable_byte);
    RUN_TEST(test_marked_same_board_ok);
    RUN_TEST(test_marked_other_board_rejected);
    RUN_TEST(test_unmarked_accepted_on_v2_only);
    RUN_TEST(test_damaged_magic_is_unmarked);
    RUN_TEST(test_default_judges_as_running_board);
    RUN_TEST(test_not_firmware);
    RUN_TEST(test_wrong_chip);
    RUN_TEST(test_short_upload_pending);
    RUN_TEST(test_one_byte_chunks);
    RUN_TEST(test_two_chunks_every_split);
    RUN_TEST(test_later_chunks_and_null_ignored);
    RUN_TEST(test_reset_starts_over);
    RUN_TEST(test_seen_board_rev);
    RUN_TEST(test_reasons_and_labels);
    return UNITY_END();
}
