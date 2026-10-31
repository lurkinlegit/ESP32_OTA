#include "modem_n706b.h"
#include "app_config.h"

#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "MODEM_N706B";

esp_err_t modem_init(void) {
    ESP_LOGI(TAG, "Initializing Modem GPIOs and UART...");

    // Configure PWRKEY (GPIO 18) and RESET (GPIO 23)
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << MODEM_PWRKEY_PIN) | (1ULL << MODEM_RESET_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure modem GPIOs: %s", esp_err_to_name(err));
        return err;
    }

    // Default pins LOW (Transistors Q3 and Q4 OFF, PWRKEY_N and RESET_N pulled high by module pull-ups)
    gpio_set_level(MODEM_PWRKEY_PIN, 0);
    gpio_set_level(MODEM_RESET_PIN, 0);

    // Configure UART1
    uart_config_t uart_config = {
        .baud_rate = MODEM_BAUDRATE,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    err = uart_param_config(MODEM_UART_NUM, &uart_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure UART params: %s", esp_err_to_name(err));
        return err;
    }

    err = uart_set_pin(MODEM_UART_NUM, MODEM_TX_PIN, MODEM_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set UART pins: %s", esp_err_to_name(err));
        return err;
    }

    err = uart_driver_install(MODEM_UART_NUM, MODEM_UART_RX_BUF_SIZE, MODEM_UART_TX_BUF_SIZE, 0, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install UART driver: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Modem UART & GPIOs configured successfully.");
    return ESP_OK;
}

esp_err_t modem_power_on(void) {
    ESP_LOGI(TAG, "Checking modem communication on TX=GPIO%d, RX=GPIO%d at %d baud...",
             MODEM_TX_PIN, MODEM_RX_PIN, MODEM_BAUDRATE);

    // Configure pins and baud rate from app_config.h (4800 baud)
    uart_set_pin(MODEM_UART_NUM, MODEM_TX_PIN, MODEM_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_set_baudrate(MODEM_UART_NUM, MODEM_BAUDRATE);

    // Send sync AT commands at configured baud (4800)
    for (int attempt = 1; attempt <= 10; attempt++) {
        uart_flush_input(MODEM_UART_NUM);
        uart_write_bytes(MODEM_UART_NUM, "AT\r\n", 4);
        vTaskDelay(pdMS_TO_TICKS(200));

        uint8_t buf[128];
        int len = uart_read_bytes(MODEM_UART_NUM, buf, sizeof(buf) - 1, pdMS_TO_TICKS(400));
        if (len > 0) {
            buf[len] = '\0';
            ESP_LOGI(TAG, "Modem replied (attempt %d, %d bytes): %s", attempt, len, (char *)buf);
            if (strstr((char *)buf, "OK") || strstr((char *)buf, "AT")) {
                ESP_LOGI(TAG, "Modem sync confirmed at %d baud!", MODEM_BAUDRATE);
                modem_send_cmd("ATE0\r\n", "OK", 1000);
                return ESP_OK;
            }
        }
    }

    // Fallback: Also probe 115200 and 9600 just in case
    int fallback_bauds[] = {115200, 9600};
    for (int b = 0; b < 2; b++) {
        int baud = fallback_bauds[b];
        ESP_LOGW(TAG, "Testing %d baud fallback...", baud);
        uart_set_baudrate(MODEM_UART_NUM, baud);
        for (int attempt = 1; attempt <= 3; attempt++) {
            uart_flush_input(MODEM_UART_NUM);
            uart_write_bytes(MODEM_UART_NUM, "AT\r\n", 4);
            vTaskDelay(pdMS_TO_TICKS(200));

            uint8_t buf[128];
            int len = uart_read_bytes(MODEM_UART_NUM, buf, sizeof(buf) - 1, pdMS_TO_TICKS(300));
            if (len > 0) {
                buf[len] = '\0';
                ESP_LOGI(TAG, "Modem replied at %d baud: %s", baud, (char *)buf);
                modem_send_cmd("ATE0\r\n", "OK", 1000);
                return ESP_OK;
            }
        }
    }

    // Restore configured baud rate
    uart_set_baudrate(MODEM_UART_NUM, MODEM_BAUDRATE);
    ESP_LOGW(TAG, "Modem did not respond to sync sequence. Continuing...");
    return ESP_ERR_TIMEOUT;
}

esp_err_t modem_hardware_reset(void) {
    ESP_LOGW(TAG, "Performing hardware reset pulse via GPIO 23...");
    gpio_set_level(MODEM_RESET_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(500));
    gpio_set_level(MODEM_RESET_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(3000));
    return ESP_OK;
}

esp_err_t modem_send_cmd_get_resp(const char *cmd, char *out_resp, size_t max_len, uint32_t timeout_ms) {
    if (!cmd || !out_resp || max_len == 0) return ESP_ERR_INVALID_ARG;

    // Flush RX buffer before sending
    uart_flush_input(MODEM_UART_NUM);

    int written = uart_write_bytes(MODEM_UART_NUM, cmd, strlen(cmd));
    if (written < 0) {
        ESP_LOGE(TAG, "UART write error");
        return ESP_FAIL;
    }

    size_t total_read = 0;
    out_resp[0] = '\0';
    int64_t start_time = esp_timer_get_time();

    while ((esp_timer_get_time() - start_time) < ((int64_t)timeout_ms * 1000)) {
        size_t available = 0;
        uart_get_buffered_data_len(MODEM_UART_NUM, &available);
        if (available > 0) {
            size_t to_read = (available < (max_len - total_read - 1)) ? available : (max_len - total_read - 1);
            int len = uart_read_bytes(MODEM_UART_NUM, (uint8_t *)&out_resp[total_read], to_read, pdMS_TO_TICKS(50));
            if (len > 0) {
                total_read += len;
                out_resp[total_read] = '\0';
                if (strstr(out_resp, "OK\r\n") || strstr(out_resp, "ERROR\r\n")) {
                    break;
                }
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }

    return (total_read > 0) ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t modem_send_cmd(const char *cmd, const char *expected_resp, uint32_t timeout_ms) {
    char buf[256];
    esp_err_t err = modem_send_cmd_get_resp(cmd, buf, sizeof(buf), timeout_ms);
    if (err != ESP_OK) {
        return err;
    }
    if (expected_resp && strstr(buf, expected_resp) != NULL) {
        return ESP_OK;
    }
    return ESP_FAIL;
}

bool modem_is_alive(uint32_t timeout_ms) {
    return (modem_send_cmd("AT\r\n", "OK", timeout_ms) == ESP_OK);
}

esp_err_t modem_wait_sim_ready(uint32_t timeout_ms) {
    ESP_LOGI(TAG, "Checking SIM card readiness...");
    int64_t start_time = esp_timer_get_time();
    char buf[128];

    while ((esp_timer_get_time() - start_time) < ((int64_t)timeout_ms * 1000)) {
        if (modem_send_cmd_get_resp("AT+CPIN?\r\n", buf, sizeof(buf), 1000) == ESP_OK) {
            if (strstr(buf, "+CPIN: READY")) {
                ESP_LOGI(TAG, "SIM card is ready.");
                return ESP_OK;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    ESP_LOGE(TAG, "SIM card not ready within timeout.");
    return ESP_ERR_TIMEOUT;
}

esp_err_t modem_wait_network_registered(uint32_t timeout_ms) {
    ESP_LOGI(TAG, "Checking cellular network registration...");
    int64_t start_time = esp_timer_get_time();
    char buf[128];

    while ((esp_timer_get_time() - start_time) < ((int64_t)timeout_ms * 1000)) {
        if (modem_send_cmd_get_resp("AT+CREG?\r\n", buf, sizeof(buf), 1000) == ESP_OK) {
            // Status: 1 = Registered home network, 5 = Registered roaming
            if (strstr(buf, "+CREG: 0,1") || strstr(buf, "+CREG: 0,5") ||
                strstr(buf, "+CREG: 1,1") || strstr(buf, "+CREG: 1,5") ||
                strstr(buf, "+CREG: 1")   || strstr(buf, "+CREG: 5")) {
                ESP_LOGI(TAG, "Cellular network registered successfully.");
                return ESP_OK;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    ESP_LOGE(TAG, "Failed to register on network within timeout.");
    return ESP_ERR_TIMEOUT;
}

esp_err_t modem_get_signal_strength(modem_signal_t *sig) {
    if (!sig) return ESP_ERR_INVALID_ARG;
    char buf[128];
    sig->rssi = 99;
    sig->ber = 99;

    if (modem_send_cmd_get_resp("AT+CSQ\r\n", buf, sizeof(buf), 1000) == ESP_OK) {
        char *p = strstr(buf, "+CSQ:");
        if (p) {
            sscanf(p, "+CSQ: %d,%d", &sig->rssi, &sig->ber);
            ESP_LOGI(TAG, "Signal strength: RSSI=%d, BER=%d", sig->rssi, sig->ber);
            return ESP_OK;
        }
    }
    return ESP_FAIL;
}

esp_err_t modem_setup_data_connection(const char *apn) {
    char cmd[128];
    ESP_LOGI(TAG, "Configuring PDP context for APN: %s", apn ? apn : MODEM_DEFAULT_APN);

    snprintf(cmd, sizeof(cmd), "AT+CGDCONT=1,\"IP\",\"%s\"\r\n", apn ? apn : MODEM_DEFAULT_APN);
    if (modem_send_cmd(cmd, "OK", 2000) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set APN in PDP context");
        return ESP_FAIL;
    }

    // Attach to packet service
    modem_send_cmd("AT+CGATT=1\r\n", "OK", 5000);

    // Setup internal IP connection on Neoway
    modem_send_cmd("AT+XIIC=1\r\n", "OK", 5000);
    vTaskDelay(pdMS_TO_TICKS(1000));

    return ESP_OK;
}

static const char *clean_url(const char *url) {
    if (!url) return "";
    if (strncasecmp(url, "http://", 7) == 0) return url + 7;
    if (strncasecmp(url, "https://", 8) == 0) return url + 8;
    return url;
}

esp_err_t modem_http_setup(const char *url, uint16_t port) {
    char cmd[320];
    const char *norm_url = clean_url(url);
    ESP_LOGI(TAG, "Setting up HTTP parameters: URL=%s, Port=%u", norm_url, port);

    // Close any previous pending session
    modem_send_cmd("AT+HTTPCLOSE\r\n", "OK", 1000);

    snprintf(cmd, sizeof(cmd), "AT+HTTPPARA=url,\"%s\"\r\n", norm_url);
    if (modem_send_cmd(cmd, "OK", 2000) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set HTTP URL");
        return ESP_FAIL;
    }

    snprintf(cmd, sizeof(cmd), "AT+HTTPPARA=port,%u\r\n", port);
    if (modem_send_cmd(cmd, "OK", 2000) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set HTTP port");
        return ESP_FAIL;
    }

    if (modem_send_cmd("AT+HTTPSETUP\r\n", "OK", 15000) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to setup HTTP connection");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "HTTP connection configured.");
    return ESP_OK;
}

esp_err_t modem_http_get_chunk(const char *url, uint16_t port, uint32_t offset, uint32_t size, uint8_t *out_buf, uint32_t *out_received, uint32_t timeout_ms) {
    if (!url || !out_buf || !out_received || size == 0) return ESP_ERR_INVALID_ARG;
    *out_received = 0;

    // 1. Establish HTTP connection for this segment (Neoway short connection model)
    esp_err_t err = modem_http_setup(url, port);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Chunk setup failed at offset %lu", (unsigned long)offset);
        return err;
    }

    // 2. Issue GET with offset and size
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "AT+HTTPACTION=0,%lu,%lu\r\n", (unsigned long)offset, (unsigned long)size);

    uart_flush_input(MODEM_UART_NUM);
    uart_write_bytes(MODEM_UART_NUM, cmd, strlen(cmd));

    int64_t start_time = esp_timer_get_time();
    int64_t timeout_us = (int64_t)timeout_ms * 1000;

    // Buffer to detect and discard HTTP response headers (e.g. +HTTPRECV: HTTP/1.1 206 ... \r\n\r\n)
    uint8_t hdr_buf[1024];
    size_t hdr_len = 0;
    bool in_payload = false;
    uint32_t payload_received = 0;

    while ((esp_timer_get_time() - start_time) < timeout_us) {
        size_t available = 0;
        uart_get_buffered_data_len(MODEM_UART_NUM, &available);
        if (available == 0) {
            vTaskDelay(pdMS_TO_TICKS(15));
            continue;
        }

        if (!in_payload) {
            // Read into header inspection buffer
            size_t to_read = available;
            if (to_read > (sizeof(hdr_buf) - hdr_len)) {
                to_read = sizeof(hdr_buf) - hdr_len;
            }
            if (to_read == 0) {
                // Header buffer full without finding \r\n\r\n
                ESP_LOGE(TAG, "HTTP header parsing overflow");
                break;
            }

            int len = uart_read_bytes(MODEM_UART_NUM, &hdr_buf[hdr_len], to_read, pdMS_TO_TICKS(50));
            if (len <= 0) continue;
            hdr_len += len;

            // Search for end of HTTP headers: \r\n\r\n or \n\n
            for (size_t i = 3; i < hdr_len; i++) {
                if (hdr_buf[i - 3] == '\r' && hdr_buf[i - 2] == '\n' &&
                    hdr_buf[i - 1] == '\r' && hdr_buf[i] == '\n') {
                    in_payload = true;
                    size_t leftover = hdr_len - (i + 1);
                    if (leftover > 0) {
                        size_t copy_len = (leftover < size) ? leftover : size;
                        memcpy(out_buf, &hdr_buf[i + 1], copy_len);
                        payload_received = copy_len;
                    }
                    break;
                }
            }
        } else {
            // In pure binary payload reading mode
            size_t needed = size - payload_received;
            if (needed == 0) {
                break; // Fully read expected chunk size
            }
            size_t to_read = (available < needed) ? available : needed;
            int len = uart_read_bytes(MODEM_UART_NUM, &out_buf[payload_received], to_read, pdMS_TO_TICKS(50));
            if (len > 0) {
                payload_received += len;
                if (payload_received >= size) {
                    break;
                }
            }
        }
    }

    // Close session cleanly
    modem_http_close();
    vTaskDelay(pdMS_TO_TICKS(50));

    *out_received = payload_received;
    if (payload_received == size) {
        return ESP_OK;
    }

    ESP_LOGE(TAG, "Chunk at offset %lu incomplete: got %lu of %lu bytes",
             (unsigned long)offset, (unsigned long)payload_received, (unsigned long)size);
    return (payload_received > 0) ? ESP_FAIL : ESP_ERR_TIMEOUT;
}

esp_err_t modem_http_close(void) {
    return modem_send_cmd("AT+HTTPCLOSE\r\n", "OK", 2000);
}

esp_err_t modem_http_get_manifest(const char *url, uint16_t port,
                                   const char *path,
                                   char *out_buf, size_t buf_len) {
    if (!url || !path || !out_buf || buf_len == 0) return ESP_ERR_INVALID_ARG;
    memset(out_buf, 0, buf_len);

    ESP_LOGI(TAG, "Fetching manifest: http://%s:%u%s", url, port, path);

    // Build full URL string with path appended (e.g. "192.168.1.50/manifest.json")
    char full_url[320];
    snprintf(full_url, sizeof(full_url), "%s%s", url, path);

    // Reuse existing HTTP setup function (AT+HTTPPARA + AT+HTTPSETUP)
    esp_err_t err = modem_http_setup(full_url, port);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP setup failed for manifest fetch");
        return err;
    }

    // Standard Neoway HTTP GET command: AT+HTTPACTION=0
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT+HTTPACTION=0\r\n");

    uart_flush_input(MODEM_UART_NUM);
    uart_write_bytes(MODEM_UART_NUM, cmd, strlen(cmd));

    // Collect response bytes into text buffer (10 second overall timeout)
    int64_t start = esp_timer_get_time();
    int64_t last_byte_time = start;
    size_t total = 0;
    bool seen_json_start = false;

    while ((esp_timer_get_time() - start) < (15000LL * 1000)) {
        size_t avail = 0;
        uart_get_buffered_data_len(MODEM_UART_NUM, &avail);
        if (avail > 0) {
            size_t space = (buf_len - 1) - total;
            size_t to_read = (avail < space) ? avail : space;
            int len = uart_read_bytes(MODEM_UART_NUM,
                                      (uint8_t *)&out_buf[total],
                                      to_read,
                                      pdMS_TO_TICKS(50));
            if (len > 0) {
                total += len;
                last_byte_time = esp_timer_get_time();
                if (memchr(out_buf, '{', total) != NULL) {
                    seen_json_start = true;
                }
                // Stop once full JSON object closing brace is observed
                if (seen_json_start && memchr(out_buf, '}', total) != NULL) {
                    vTaskDelay(pdMS_TO_TICKS(100));
                    break;
                }
                if (total >= buf_len - 1) break; // Buffer full
            }
        } else {
            // Only stop if we already saw the JSON and have been silent for 1s,
            // or if 5s passed after command with no json
            if (seen_json_start && (esp_timer_get_time() - last_byte_time) > (1500LL * 1000)) {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(30));
        }
    }

    modem_http_close();
    out_buf[total] = '\0';

    if (total == 0) {
        ESP_LOGE(TAG, "Manifest fetch: empty response (timeout or no server)");
        return ESP_ERR_TIMEOUT;
    }

    // Strip any AT echo or HTTP header lines before the JSON '{'
    char *json_start = strchr(out_buf, '{');
    if (!json_start) {
        ESP_LOGE(TAG, "Manifest has no JSON body. Raw: %s", out_buf);
        return ESP_FAIL;
    }
    if (json_start != out_buf) {
        size_t json_len = total - (size_t)(json_start - out_buf);
        memmove(out_buf, json_start, json_len);
        out_buf[json_len] = '\0';
    }

    ESP_LOGI(TAG, "Manifest (%d bytes): %s", (int)strlen(out_buf), out_buf);
    return ESP_OK;
}
