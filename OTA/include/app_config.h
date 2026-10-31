#pragma once

#include "driver/gpio.h"
#include "driver/uart.h"

// Hardware pin mapping from Proto_Get_AC_GSM_3_REALY-2.pdf
#define MODEM_UART_NUM          UART_NUM_1
#define MODEM_TX_PIN            GPIO_NUM_27   // ESP32 TX -> N706B RXD (GSM_RX)
#define MODEM_RX_PIN            GPIO_NUM_26   // ESP32 RX <- N706B TXD (GSM_TX)
#define MODEM_PWRKEY_PIN        GPIO_NUM_18   // High pulse drives Q3 to pull PWRKEY low
#define MODEM_RESET_PIN         GPIO_NUM_23   // High pulse drives Q4 to pull RESET low
#define MODEM_BAUDRATE          4800          // Configured to suggested 4800 baud

// Visual Indicators (LEDs on relay board) — IO13, IO25, IO33
#define LED1_PIN                GPIO_NUM_13   // LED 1 — V1 slow blink
#define LED2_PIN                GPIO_NUM_25   // LED 2 — available for V2
#define LED3_PIN                GPIO_NUM_33   // LED 3 — available for V2
#define LED_INDICATOR_PIN       LED1_PIN      // Active indicator (change per version if needed)

// UART Buffer sizes
#define MODEM_UART_RX_BUF_SIZE  4096
#define MODEM_UART_TX_BUF_SIZE  2048

// Cellular Network Defaults
#define MODEM_DEFAULT_APN       "airtelgprs.com"

// FOTA Constraints
#define MAX_OTA_IMAGE_SIZE      (1920 * 1024) // 1,966,080 bytes max

// OTA Manifest Server — update FOTA_MANIFEST_URL before hardware test
// Public tunnel domain reachable by Airtel 4G LTE
#define FOTA_MANIFEST_URL        "my-fota-test.loca.lt"
#define FOTA_MANIFEST_PORT       80
#define FOTA_MANIFEST_PATH       "/manifest.json"
#define FOTA_MANIFEST_BUF_SIZE   512

// FOTA Soak Test Probation Window
// Bench test mode:      120 seconds (2 minutes)
// Stage 1 test window: 7200 seconds (2 hours)
// Stage 2 production: 86400 seconds (24 hours)
#define FOTA_SOAK_TEST_TIME_SEC         120  // Change to 7200 for 2-hour test
#define FOTA_SOAK_CHECK_INTERVAL_SEC     10  // Check health and log progress every 10 seconds
