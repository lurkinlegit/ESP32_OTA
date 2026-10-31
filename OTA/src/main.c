#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "app_config.h"
#include "modem_n706b.h"
#include "fota_engine.h"

static const char *TAG = "MAIN_APP";

void app_main(void) {
    ESP_LOGI(TAG, "==================================================");
    ESP_LOGI(TAG, "  ESP32 + Neoway N706B FOTA System");
    ESP_LOGI(TAG, "  App Version: %s (Build: %d)", CURRENT_FIRMWARE_VERSION, CURRENT_FIRMWARE_BUILD);
    ESP_LOGI(TAG, "==================================================");

    // 1. Initialize NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    // 2. Query Running Partition & Rollback Status
    const esp_partition_t *running = esp_ota_get_running_partition();
    ESP_LOGI(TAG, "Current Running Partition: %s (Offset: 0x%08" PRIx32 ", Size: %" PRIu32 " KB)",
             running->label, running->address, running->size / 1024);

    esp_ota_img_states_t ota_state;
    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            ESP_LOGW(TAG, ">>> FIRMWARE IN PENDING_VERIFY STATE! Running self-test diagnostic...");
        } else {
            ESP_LOGI(TAG, "Partition State: %d (Active & Verified)", ota_state);
        }
    }

    // 3. Initialize Neoway N706B Modem Hardware
    ESP_LOGI(TAG, "Initializing Neoway N706B Hardware...");
    err = modem_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Modem initialization failed!");
        return;
    }

    // 4. Power On Modem via GPIO 18 pulse
    err = modem_power_on();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Modem power-on failed! Continuing to app loop...");
        // Do not halt completely — allow LED indicator and app loop to run
        goto fota_done;
    }

    ESP_LOGI(TAG, "Modem is responsive!");

    // 5. Diagnostic Self-Test (Gate 3 in flowchart)
    if (modem_wait_sim_ready(10000) != ESP_OK) {
        ESP_LOGE(TAG, "SIM card not ready. Diagnostic failed.");
        goto fota_done;
    }

    modem_signal_t sig;
    if (modem_get_signal_strength(&sig) == ESP_OK) {
        ESP_LOGI(TAG, "Signal: RSSI=%d, BER=%d", sig.rssi, sig.ber);
    }

    if (modem_wait_network_registered(15000) != ESP_OK) {
        ESP_LOGW(TAG, "Network registration pending or timed out.");
    } else {
        ESP_LOGI(TAG, "Cellular network registered successfully.");
    }

    // If new firmware was booted in PENDING_VERIFY state and self-test passed:
    if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
        ESP_LOGW(TAG, ">>> Self-test passed! Starting probation soak test monitor (%d seconds)...", FOTA_SOAK_TEST_TIME_SEC);
        fota_start_soak_test_monitor();
        ESP_LOGI(TAG, ">>> Firmware running on probation. Rollback remains active during test window.");
    }

    // 6. Setup data connection
    ESP_LOGI(TAG, "Connecting cellular data PDP context...");
    err = modem_setup_data_connection(MODEM_DEFAULT_APN);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Data connection failed. Skipping OTA check.");
        goto fota_done;
    }
    ESP_LOGI(TAG, "Data connection active. Checking for FOTA update...");

    // 7. Fetch manifest.json from server
    {
        char manifest_json[FOTA_MANIFEST_BUF_SIZE];
        err = modem_http_get_manifest(FOTA_MANIFEST_URL,
                                       FOTA_MANIFEST_PORT,
                                       FOTA_MANIFEST_PATH,
                                       manifest_json,
                                       sizeof(manifest_json));
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Manifest fetch failed (%s). No OTA today.", esp_err_to_name(err));
            goto fota_done;
        }

        // 8. Parse manifest fields (build, version, size, sha256, url, port)
        fota_manifest_t manifest;
        err = fota_parse_manifest(manifest_json, &manifest);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Manifest parse failed. Skipping OTA.");
            goto fota_done;
        }

        // 9. Run OTA update — if applied, esp_restart() is called inside fota_start_update().
        //    Execution only continues past here if no update was needed.
        err = fota_start_update(&manifest, CURRENT_FIRMWARE_BUILD);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Firmware already up to date (Build %d).", CURRENT_FIRMWARE_BUILD);
        } else {
            ESP_LOGW(TAG, "OTA update attempt failed: %s", esp_err_to_name(err));
        }
    }

fota_done:
    ESP_LOGI(TAG, "FOTA check complete. Device running normally.");

    // ── VERSION 1: Slow blink — 1s ON / 1s OFF ──────────────────────────────
    // Visual proof V1 is running.
    ESP_LOGI(TAG, "Starting LED indicator — Version 1 slow blink (1s ON / 1s OFF)");
    gpio_reset_pin(LED_INDICATOR_PIN);
    gpio_set_direction(LED_INDICATOR_PIN, GPIO_MODE_OUTPUT);

    while (1) {
        gpio_set_level(LED_INDICATOR_PIN, 1);          // LED ON
        vTaskDelay(pdMS_TO_TICKS(10000));               // 1s
        gpio_set_level(LED_INDICATOR_PIN, 0);          // LED OFF
        vTaskDelay(pdMS_TO_TICKS(10000));               // 1s
    }
}
