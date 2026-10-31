#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int rssi;
    int ber;
} modem_signal_t;

/**
 * @brief Initialize modem UART interface and control GPIO pins
 */
esp_err_t modem_init(void);

/**
 * @brief Power cycle or power on the Neoway N706B modem
 *        Drives GPIO 18 HIGH for 1000ms to activate Q3 (pulls PWRKEY_N low).
 */
esp_err_t modem_power_on(void);

/**
 * @brief Hardware reset pulse on GPIO 23 (activates Q4 to pull RESET_N low).
 */
esp_err_t modem_hardware_reset(void);

/**
 * @brief Send an AT command and check if expected response string is received within timeout
 */
esp_err_t modem_send_cmd(const char *cmd, const char *expected_resp, uint32_t timeout_ms);

/**
 * @brief Send an AT command and return response string buffer
 */
esp_err_t modem_send_cmd_get_resp(const char *cmd, char *out_resp, size_t max_len, uint32_t timeout_ms);

/**
 * @brief Check if modem is responsive to AT commands
 */
bool modem_is_alive(uint32_t timeout_ms);

/**
 * @brief Wait for SIM card ready (+CPIN: READY)
 */
esp_err_t modem_wait_sim_ready(uint32_t timeout_ms);

/**
 * @brief Query network registration (AT+CREG?)
 * @return ESP_OK if registered (status 1 or 5), ESP_FAIL otherwise
 */
esp_err_t modem_wait_network_registered(uint32_t timeout_ms);

/**
 * @brief Query current signal strength (AT+CSQ)
 */
esp_err_t modem_get_signal_strength(modem_signal_t *sig);

/**
 * @brief Setup APN and attach to GPRS/LTE data service
 */
esp_err_t modem_setup_data_connection(const char *apn);

/**
 * @brief Configure HTTP URL and Port using AT+HTTPPARA
 */
esp_err_t modem_http_setup(const char *url, uint16_t port);

/**
 * @brief Fetch a specific HTTP byte chunk using Neoway AT+HTTPACTION=0,<offset>,<size>
 *        Establishes HTTP connection, strips response headers, returns only pure binary data.
 * @param url Full URL or path to file
 * @param port HTTP server port
 * @param offset Start byte offset
 * @param size Chunk size in bytes
 * @param out_buf Buffer to store downloaded bytes
 * @param out_received Pointer to store number of bytes actually received
 * @param timeout_ms Response timeout
 */
esp_err_t modem_http_get_chunk(const char *url, uint16_t port, uint32_t offset, uint32_t size, uint8_t *out_buf, uint32_t *out_received, uint32_t timeout_ms);

/**
 * @brief Close HTTP connection using AT+HTTPCLOSE
 */
esp_err_t modem_http_close(void);

/**
 * @brief Download manifest.json text from an HTTP server.
 *        Reuses AT+HTTPPARA / AT+HTTPSETUP / AT+HTTPACTION flow.
 *        Strips any AT echo / HTTP headers, returns only the JSON body.
 * @param url      Server IP or hostname (e.g. "192.168.1.50")
 * @param port     TCP port (usually 80)
 * @param path     URL path (e.g. "/manifest.json")
 * @param out_buf  Buffer to write JSON text into
 * @param buf_len  Size of out_buf (minimum 256, recommend 512)
 * @return ESP_OK on success, ESP_ERR_TIMEOUT / ESP_FAIL on error
 */
esp_err_t modem_http_get_manifest(const char *url, uint16_t port,
                                   const char *path,
                                   char *out_buf, size_t buf_len);

#ifdef __cplusplus
}
#endif
