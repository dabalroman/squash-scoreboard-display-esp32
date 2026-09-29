// Host shim for IDF 4.4's esp_app_format.h + the sdkconfig chip id: only what
// FirmwareImageCheck.h reads. Layouts copied from the pinned framework's header.
#ifndef LED_DUMP_SHIM_ESP_APP_FORMAT_H
#define LED_DUMP_SHIM_ESP_APP_FORMAT_H

#include <cstdint>

#ifndef CONFIG_IDF_FIRMWARE_CHIP_ID
#define CONFIG_IDF_FIRMWARE_CHIP_ID 0x0009
#endif

typedef enum {
    ESP_CHIP_ID_ESP32 = 0x0000,
    ESP_CHIP_ID_ESP32S2 = 0x0002,
    ESP_CHIP_ID_ESP32C3 = 0x0005,
    ESP_CHIP_ID_ESP32S3 = 0x0009,
    ESP_CHIP_ID_INVALID = 0xFFFF
} __attribute__((packed)) esp_chip_id_t;

#define ESP_IMAGE_HEADER_MAGIC 0xE9
#define ESP_APP_DESC_MAGIC_WORD 0xABCD5432

typedef struct {
    uint8_t magic;
    uint8_t segment_count;
    uint8_t spi_mode;
    uint8_t spi_speed: 4;
    uint8_t spi_size: 4;
    uint32_t entry_addr;
    uint8_t wp_pin;
    uint8_t spi_pin_drv[3];
    esp_chip_id_t chip_id;
    uint8_t min_chip_rev;
    uint16_t min_chip_rev_full;
    uint16_t max_chip_rev_full;
    uint8_t reserved[4];
    uint8_t hash_appended;
} __attribute__((packed)) esp_image_header_t;

typedef struct {
    uint32_t load_addr;
    uint32_t data_len;
} esp_image_segment_header_t;

typedef struct {
    uint32_t magic_word;
    uint32_t secure_version;
    uint32_t reserv1[2];
    char version[32];
    char project_name[32];
    char time[16];
    char date[16];
    char idf_ver[32];
    uint8_t app_elf_sha256[32];
    uint32_t reserv2[20];
} esp_app_desc_t;

#endif
