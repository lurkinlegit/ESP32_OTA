#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Manifest definition
typedef struct {
    char version[16];
    uint32_t build;
    char url[256];
    uint16_t port;
    uint32_t size;
    char sha256_hex[65];
} fota_manifest_t;

/**
 * @brief Current running firmware build number
 */
#define CURRENT_FIRMWARE_VERSION  "1.0.0"
#define CURRENT_FIRMWARE_BUILD    1

/**
 * @brief Parse manifest JSON string into fota_manifest_t
 */
esp_err_t fota_parse_manifest(const char *json_str, fota_manifest_t *out_manifest);

/**
 * @brief Execute the full FOTA download, write, integrity check, and slot activation
 * @param manifest The parsed manifest to download and verify
 * @param current_build The current device build number (for anti-downgrade check)
 */
esp_err_t fota_start_update(const fota_manifest_t *manifest, uint32_t current_build);

/**
 * @brief Maximum consecutive OTA failures before aborting to protect cellular data
 */
#define FOTA_MAX_RETRY_COUNT      3

/**
 * @brief Get consecutive OTA failure count from NVS
 */
uint32_t fota_get_retry_count(void);

/**
 * @brief Increment consecutive OTA failure count in NVS
 */
esp_err_t fota_increment_retry_count(void);

/**
 * @brief Reset OTA retry count to 0 in NVS
 */
esp_err_t fota_reset_retry_count(void);

/**
 * @brief Start background soak test task to monitor stability during probation window
 *        Only marks firmware permanent (esp_ota_mark_app_valid_cancel_rollback)
 *        after running continuously for FOTA_SOAK_TEST_TIME_SEC without reset.
 */
esp_err_t fota_start_soak_test_monitor(void);

#ifdef __cplusplus
}
#endif
