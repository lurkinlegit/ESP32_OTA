#include "fota_engine.h"
#include "modem_n706b.h"
#include "app_config.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "psa/crypto.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "FOTA_ENGINE";
static const char *NVS_NAMESPACE = "fota";
static const char *NVS_KEY_RETRY = "retry_cnt";

#define FOTA_CHUNK_SIZE 2048

uint32_t fota_get_retry_count(void) {
    nvs_handle_t nvs_h;
    uint32_t count = 0;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_h) == ESP_OK) {
        nvs_get_u32(nvs_h, NVS_KEY_RETRY, &count);
        nvs_close(nvs_h);
    }
    return count;
}

esp_err_t fota_increment_retry_count(void) {
    nvs_handle_t nvs_h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_h);
    if (err == ESP_OK) {
        uint32_t count = 0;
        nvs_get_u32(nvs_h, NVS_KEY_RETRY, &count);
        count++;
        nvs_set_u32(nvs_h, NVS_KEY_RETRY, count);
        nvs_commit(nvs_h);
        nvs_close(nvs_h);
        ESP_LOGW(TAG, "OTA failure recorded in NVS. Consecutive failures: %lu", (unsigned long)count);
    }
    return err;
}

esp_err_t fota_reset_retry_count(void) {
    nvs_handle_t nvs_h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_h);
    if (err == ESP_OK) {
        nvs_erase_key(nvs_h, NVS_KEY_RETRY);
        nvs_commit(nvs_h);
        nvs_close(nvs_h);
        ESP_LOGI(TAG, "OTA retry counter reset in NVS.");
    }
    return err;
}

static void fota_soak_test_task(void *pvParameters) {
    uint32_t elapsed_sec = 0;
    ESP_LOGI(TAG, ">>> Soak test monitor started. Target probation window: %d seconds (%d mins)",
             FOTA_SOAK_TEST_TIME_SEC, FOTA_SOAK_TEST_TIME_SEC / 60);

    while (elapsed_sec < FOTA_SOAK_TEST_TIME_SEC) {
        vTaskDelay(pdMS_TO_TICKS(FOTA_SOAK_CHECK_INTERVAL_SEC * 1000));
        elapsed_sec += FOTA_SOAK_CHECK_INTERVAL_SEC;

        int percent = (int)((elapsed_sec * 100ULL) / FOTA_SOAK_TEST_TIME_SEC);
        ESP_LOGI(TAG, ">>> [SOAK TEST] Probation Progress: %d%% (%lu / %d sec) - State: PENDING_VERIFY",
                 percent, (unsigned long)elapsed_sec, FOTA_SOAK_TEST_TIME_SEC);
    }

    ESP_LOGI(TAG, "===============================================================");
    ESP_LOGI(TAG, "  SOAK TEST COMPLETED SUCCESSFULLY (%d seconds)!", FOTA_SOAK_TEST_TIME_SEC);
    ESP_LOGI(TAG, "  Confirming firmware validity and cancelling rollback...");
    ESP_LOGI(TAG, "===============================================================");

    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err == ESP_OK) {
        fota_reset_retry_count();
        ESP_LOGI(TAG, ">>> FIRMWARE MARKED PERMANENT (VALIDATED) IN FLASH OTADATA.");
    } else {
        ESP_LOGE(TAG, "Failed to mark app valid: %s", esp_err_to_name(err));
    }

    vTaskDelete(NULL);
}

esp_err_t fota_start_soak_test_monitor(void) {
    BaseType_t res = xTaskCreate(fota_soak_test_task, "fota_soak_task", 4096, NULL, 5, NULL);
    return (res == pdPASS) ? ESP_OK : ESP_FAIL;
}

static bool parse_json_field_str(const char *json, const char *key, char *out_val, size_t max_len) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *pos = strstr(json, pattern);
    if (!pos) return false;

    pos += strlen(pattern);
    while (*pos == ' ' || *pos == ':' || *pos == '\t') pos++;
    if (*pos != '\"') return false;
    pos++; // Skip opening quote

    size_t i = 0;
    while (*pos && *pos != '\"' && i < (max_len - 1)) {
        out_val[i++] = *pos++;
    }
    out_val[i] = '\0';
    return (i > 0);
}

static bool parse_json_field_num(const char *json, const char *key, uint32_t *out_num) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *pos = strstr(json, pattern);
    if (!pos) return false;

    pos += strlen(pattern);
    while (*pos == ' ' || *pos == ':' || *pos == '\t') pos++;
    *out_num = (uint32_t)strtoul(pos, NULL, 10);
    return true;
}

esp_err_t fota_parse_manifest(const char *json_str, fota_manifest_t *out_manifest) {
    if (!json_str || !out_manifest) return ESP_ERR_INVALID_ARG;
    memset(out_manifest, 0, sizeof(fota_manifest_t));

    // Parse mandatory fields: build, url, size, sha256
    if (!parse_json_field_num(json_str, "build", &out_manifest->build)) {
        ESP_LOGE(TAG, "Manifest missing 'build' field");
        return ESP_FAIL;
    }
    if (!parse_json_field_str(json_str, "url", out_manifest->url, sizeof(out_manifest->url))) {
        ESP_LOGE(TAG, "Manifest missing 'url' field");
        return ESP_FAIL;
    }
    if (!parse_json_field_num(json_str, "size", &out_manifest->size)) {
        ESP_LOGE(TAG, "Manifest missing 'size' field");
        return ESP_FAIL;
    }
    if (!parse_json_field_str(json_str, "sha256", out_manifest->sha256_hex, sizeof(out_manifest->sha256_hex))) {
        ESP_LOGE(TAG, "Manifest missing 'sha256' field");
        return ESP_FAIL;
    }

    // Optional fields
    parse_json_field_str(json_str, "version", out_manifest->version, sizeof(out_manifest->version));
    uint32_t port = 80;
    if (parse_json_field_num(json_str, "port", &port)) {
        out_manifest->port = (uint16_t)port;
    } else {
        out_manifest->port = 80;
    }

    ESP_LOGI(TAG, "Manifest parsed: Version=%s, Build=%lu, Size=%lu bytes, Port=%u",
             out_manifest->version, (unsigned long)out_manifest->build,
             (unsigned long)out_manifest->size, out_manifest->port);
    return ESP_OK;
}

esp_err_t fota_start_update(const fota_manifest_t *manifest, uint32_t current_build) {
    if (!manifest) return ESP_ERR_INVALID_ARG;

    // Check consecutive failure limit to prevent burning cellular data
    uint32_t retries = fota_get_retry_count();
    if (retries >= FOTA_MAX_RETRY_COUNT) {
        ESP_LOGW(TAG, "Maximum consecutive OTA failures (%lu >= %d) reached. Aborting to protect cellular data.",
                 (unsigned long)retries, FOTA_MAX_RETRY_COUNT);
        return ESP_ERR_INVALID_STATE;
    }

    // Gate 1: Check build version (Anti-downgrade & Redundancy prevention)
    if (manifest->build <= current_build) {
        ESP_LOGI(TAG, "Current build (%lu) is >= manifest build (%lu). Firmware is up to date.",
                 (unsigned long)current_build, (unsigned long)manifest->build);
        return ESP_OK;
    }

    // Gate 1b: Check size limit (1920 KB partition max)
    if (manifest->size > MAX_OTA_IMAGE_SIZE) {
        ESP_LOGE(TAG, "Image size (%lu bytes) exceeds maximum partition slot size (%d bytes)!",
                 (unsigned long)manifest->size, MAX_OTA_IMAGE_SIZE);
        return ESP_ERR_INVALID_SIZE;
    }

    // Gate 2: Locate inactive partition
    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    if (!update_partition) {
        ESP_LOGE(TAG, "Passive OTA partition not found!");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Targeting OTA slot: %s (Offset: 0x%08" PRIx32 ", Size: %" PRIu32 " KB)",
             update_partition->label, update_partition->address, update_partition->size / 1024);

    esp_ota_handle_t ota_handle = 0;
    esp_err_t err = esp_ota_begin(update_partition, manifest->size, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        fota_increment_retry_count();
        return err;
    }

    // Initialize PSA Crypto & SHA-256 calculation
    psa_crypto_init();
    psa_hash_operation_t hash_op = psa_hash_operation_init();
    psa_status_t p_status = psa_hash_setup(&hash_op, PSA_ALG_SHA_256);
    if (p_status != PSA_SUCCESS) {
        ESP_LOGE(TAG, "psa_hash_setup failed: %ld", (long)p_status);
        esp_ota_abort(ota_handle);
        fota_increment_retry_count();
        return ESP_FAIL;
    }

    uint8_t *chunk_buffer = (uint8_t *)malloc(FOTA_CHUNK_SIZE);
    if (!chunk_buffer) {
        ESP_LOGE(TAG, "Failed to allocate memory for chunk buffer");
        esp_ota_abort(ota_handle);
        psa_hash_abort(&hash_op);
        fota_increment_retry_count();
        return ESP_ERR_NO_MEM;
    }

    uint32_t offset = 0;
    uint32_t total_size = manifest->size;
    ESP_LOGI(TAG, "Starting chunked download (%lu bytes total, chunk=%d)...",
             (unsigned long)total_size, FOTA_CHUNK_SIZE);

    while (offset < total_size) {
        uint32_t to_fetch = total_size - offset;
        if (to_fetch > FOTA_CHUNK_SIZE) {
            to_fetch = FOTA_CHUNK_SIZE;
        }

        uint32_t received = 0;
        // Retry current chunk up to 3 times on transient mobile network drop
        for (int attempt = 0; attempt < 3; attempt++) {
            err = modem_http_get_chunk(manifest->url, manifest->port, offset, to_fetch, chunk_buffer, &received, 15000);
            if (err == ESP_OK && received == to_fetch) {
                break;
            }
            ESP_LOGW(TAG, "Chunk fetch retry %d at offset %lu...", attempt + 1, (unsigned long)offset);
            vTaskDelay(pdMS_TO_TICKS(500));
        }

        if (err != ESP_OK || received != to_fetch) {
            ESP_LOGE(TAG, "Chunk fetch failed at offset %lu after retries: %s",
                     (unsigned long)offset, esp_err_to_name(err));
            break;
        }

        // Write chunk into inactive flash slot
        err = esp_ota_write(ota_handle, chunk_buffer, received);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed at offset %lu: %s", (unsigned long)offset, esp_err_to_name(err));
            break;
        }

        // Feed chunk into SHA-256
        p_status = psa_hash_update(&hash_op, chunk_buffer, received);
        if (p_status != PSA_SUCCESS) {
            ESP_LOGE(TAG, "psa_hash_update failed: %ld", (long)p_status);
            break;
        }

        offset += received;
        int progress = (int)((offset * 100ULL) / total_size);
        if ((offset % (FOTA_CHUNK_SIZE * 10) == 0) || (offset == total_size)) {
            ESP_LOGI(TAG, "Progress: %d%% (%lu / %lu bytes)", progress, (unsigned long)offset, (unsigned long)total_size);
        }
    }

    free(chunk_buffer);

    if (offset < total_size) {
        ESP_LOGE(TAG, "Download interrupted! Discarding incomplete update.");
        esp_ota_abort(ota_handle);
        psa_hash_abort(&hash_op);
        fota_increment_retry_count();
        return ESP_FAIL;
    }

    // Finalize SHA-256 computation
    uint8_t computed_sha[32];
    size_t hash_len = 0;
    p_status = psa_hash_finish(&hash_op, computed_sha, sizeof(computed_sha), &hash_len);
    if (p_status != PSA_SUCCESS) {
        ESP_LOGE(TAG, "psa_hash_finish failed: %ld", (long)p_status);
        esp_ota_abort(ota_handle);
        fota_increment_retry_count();
        return ESP_FAIL;
    }

    char computed_sha_hex[65];
    for (int i = 0; i < 32; i++) {
        sprintf(&computed_sha_hex[i * 2], "%02x", computed_sha[i]);
    }
    computed_sha_hex[64] = '\0';

    ESP_LOGI(TAG, "Computed SHA-256: %s", computed_sha_hex);
    ESP_LOGI(TAG, "Manifest SHA-256: %s", manifest->sha256_hex);

    // Verify SHA-256 hash
    if (strcasecmp(computed_sha_hex, manifest->sha256_hex) != 0) {
        ESP_LOGE(TAG, "SHA-256 integrity check FAILED! Image corrupted or invalid. Aborting.");
        esp_ota_abort(ota_handle);
        fota_increment_retry_count();
        return ESP_ERR_OTA_VALIDATE_FAILED;
    }

    ESP_LOGI(TAG, "SHA-256 integrity check PASSED!");

    // Validate image format header & end OTA write
    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        fota_increment_retry_count();
        return err;
    }

    // Set new boot partition
    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        fota_increment_retry_count();
        return err;
    }

    // Reset failure counter on success
    fota_reset_retry_count();

    ESP_LOGI(TAG, "Update successfully written and verified! Restarting into new partition %s in 3 seconds...",
             update_partition->label);

    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_restart();

    return ESP_OK;
}
