# ESP32 + Neoway N706B-CN FOTA Implementation Plan (Hardware-Verified)

## Goal Description
Implement an end-to-end, failsafe Firmware-Over-The-Air (FOTA) update subsystem on **ESP32-WROOM-32E-N16** paired with the **Neoway N706B-CN** cellular module, using ESP-IDF within PlatformIO and the custom [partitions_table.csv](file:///home/mahesh/Documents/Workspace/FOTA/Main/partitions_table.csv).

All hardware pinouts have been extracted directly from the official board schematic [Proto_Get_AC_GSM_3_REALY-2.pdf](file:///home/mahesh/Documents/Workspace/FOTA/Main/Proto_Get_AC_GSM_3_REALY-2.pdf).

---

## Hardware Pinout Discovered from Schematic

From Sheet 1 (ESP32), Sheet 7 (Level Shifter TXS0104ED), and Sheet 9 (Transistor Inverters Q3 & Q4):

| Function | ESP32 GPIO | Net Name | Schematic Routing & Polarity |
| :--- | :--- | :--- | :--- |
| **Cellular TX (ESP32 $\rightarrow$ Modem RX)** | **`GPIO 26`** | `GSM_TX` $\rightarrow$ `UART1_RXD` | Via `TXS0104ED` level shifter to N706 Pin 17 (`MAIN_RXD`) |
| **Cellular RX (Modem TX $\rightarrow$ ESP32)** | **`GPIO 27`** | `GSM_RX` $\leftarrow$ `UART1_TXD` | From N706 Pin 18 (`MAIN_TXD`) via `TXS0104ED` level shifter |
| **Modem PWRKEY** | **`GPIO 18`** | `UC_PWRKEY` | Drives NPN transistor `Q3` base (Active **HIGH** on GPIO18 pulls `PWRKEY_N` LOW) |
| **Modem RESET** | **`GPIO 23`** | `UC_RESET` | Drives NPN transistor `Q4` base (Active **HIGH** on GPIO23 pulls `RESET_N` LOW) |
| **RS485 TX** | `GPIO 17` | `RS485_TXD` | Connects to RS485 Transceiver |
| **RS485 RX** | `GPIO 16` | `RS485_RXD` | Connects to RS485 Transceiver |
| **RS485 DE/RE** | `GPIO 14` / `13` | `RS485_DE` | RS485 direction control |
| **Relay Control** | `GPIO 5` / `4` | `RELAY_CTRL` | Controls onboard relay |

> [!NOTE]
> **Transistor Drive Logic for PWRKEY (`GPIO 18`)**:
> - Neoway N706B `PWRKEY` pin is active-low on the module side.
> - Because it passes through NPN transistor `Q3` (`BC847B`), setting **`GPIO 18 = HIGH` (1)** turns on `Q3` and pulls `PWRKEY_N` **LOW (GND)**.
> - Therefore, to power on the modem: Set `GPIO 18 = HIGH` for 1000 ms, then return `GPIO 18 = LOW`.

---

## ESP-IDF Configuration (`sdkconfig.defaults`)

To configure the project with automatic rollback, sufficient stack, and hardware crypto acceleration, we create `sdkconfig.defaults` with:

```ini
# --- Bootloader and OTA Rollback ---
CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y
CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK=n

# --- Custom Partition Table ---
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions_table.csv"
CONFIG_PARTITION_TABLE_OFFSET=0x8000

# --- Pthread & FreeRTOS Stack Defaults ---
CONFIG_ESP32_PTHREAD_TASK_STACK_SIZE_DEFAULT=4096
CONFIG_FREERTOS_TIMER_TASK_STACK_SIZE=3072

# --- mbedTLS & Cryptography Hardware Acceleration ---
CONFIG_MBEDTLS_HARDWARE_SHA=y
CONFIG_MBEDTLS_HARDWARE_AES=y
CONFIG_MBEDTLS_HARDWARE_MPI=y
CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y
CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_CMN=y

# --- lwIP & PPP Support (for Cellular Data Mode) ---
CONFIG_LWIP_PPP_SUPPORT=y
CONFIG_LWIP_PPP_ENABLE_IPV4=y
CONFIG_LWIP_PPP_PAP_SUPPORT=y
CONFIG_LWIP_PPP_CHAP_SUPPORT=y
```

### Why each setting is required:
1. **`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`**:
   - Instructs the ESP32 2nd stage bootloader to manage state transitions in `otadata`.
   - When a new image is flashed and rebooted, its state is marked as `ESP_OTA_IMG_PENDING_VERIFY`.
   - If the application crashes, triggers a watchdog reset, or fails the diagnostic self-test before calling `esp_ota_mark_app_valid_cancel_rollback()`, the bootloader automatically marks the new image invalid (`ESP_OTA_IMG_INVALID`) and reboots back into the working slot.
2. **`CONFIG_ESP32_PTHREAD_TASK_STACK_SIZE_DEFAULT=4096`**:
   - Prevents stack overflow when running network/modem tasks and cryptographic SHA-256 verification.
3. **`CONFIG_MBEDTLS_HARDWARE_SHA=y`**:
   - Enables the ESP32's internal cryptographic accelerator for SHA-256 calculation, allowing wire-speed hashing of incoming firmware chunks with near-zero CPU overhead.

---

## Proposed Project Structure & Code Changes

```
Main/
├── platformio.ini              # [NEW] PlatformIO environment configuration
├── CMakeLists.txt              # [NEW] Project root CMakeLists
├── sdkconfig.defaults          # [NEW] Rollback, crypto, partition settings
├── partitions_table.csv        # [EXISTING] Verified 1920 KB dual OTA slots
└── main/
    ├── CMakeLists.txt          # [NEW] Component registration
    ├── app_config.h            # [NEW] Hardware GPIOs (26, 27, 18, 23) and parameters
    ├── modem_n706b.h           # [NEW] Modem HAL header
    ├── modem_n706b.c           # [NEW] Power sequencing (Q3/Q4), AT command state machine
    ├── fota_engine.h           # [NEW] OTA update manager header
    ├── fota_engine.c           # [NEW] Chunk download, esp_ota_write, SHA-256 & reboot
    └── main.c                 # [NEW] App entry, rollback self-test, status reporting
```

### Component Breakdown

#### 1. [NEW] `platformio.ini`
```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = espidf
monitor_speed = 115200
board_build.partitions = partitions_table.csv
```

#### 2. [NEW] `main/app_config.h`
```c
#pragma once

// Hardware pin mapping from Proto_Get_AC_GSM_3_REALY-2.pdf
#define MODEM_UART_NUM          UART_NUM_1
#define MODEM_TX_PIN            GPIO_NUM_26   // ESP32 TX -> N706B RXD
#define MODEM_RX_PIN            GPIO_NUM_27   // ESP32 RX <- N706B TXD
#define MODEM_PWRKEY_PIN        GPIO_NUM_18   // High pulse drives Q3 to pull PWRKEY low
#define MODEM_RESET_PIN         GPIO_NUM_23   // High pulse drives Q4 to pull RESET low
#define MODEM_BAUDRATE          115200

// FOTA Constraints
#define MAX_OTA_IMAGE_SIZE      (1920 * 1024) // 1,966,080 bytes max
```

#### 3. [NEW] `main/modem_n706b.c`
- Implements `modem_init()`: Configures `GPIO 18` and `GPIO 23` as outputs, initializes UART1 on `GPIO 26`/`GPIO 27`.
- Implements `modem_power_on()`: Sets `GPIO 18 = 1` for 1000 ms, then `GPIO 18 = 0`.
- Implements `modem_check_ready()`: Sends `AT`, checks `+CPIN: READY`, queries `AT+CREG?` and `AT+CESQ`.
- Implements `modem_http_download_chunk(url, offset, size, buffer)`.

#### 4. [NEW] `main/fota_engine.c`
- Queries target slot: `esp_ota_get_next_update_partition(NULL)`.
- Confirms image size does not exceed 1920 KB.
- Calls `esp_ota_begin()`.
- Iterates chunk-by-chunk over cellular:
  - Fetches chunk from modem.
  - Passes chunk to `esp_ota_write()`.
  - Feeds chunk into `mbedtls_sha256_update()`.
- Validates SHA-256 with server manifest.
- Invokes `esp_ota_end()` and `esp_ota_set_boot_partition()`.
- Restarts ESP32 into new image.

#### 5. [NEW] `main/main.c`
- Initializes NVS.
- Inspects running partition via `esp_ota_get_running_partition()`.
- Checks partition state with `esp_ota_get_state_partition()`:
  - If `ESP_OTA_IMG_PENDING_VERIFY`: runs self-test (verifies modem communication).
  - If self-test passes: calls `esp_ota_mark_app_valid_cancel_rollback()`.
  - If self-test fails: triggers automatic rollback.

---

## Verification Plan

### 1. Build Verification
```bash
/home/mahesh/.platformio-venv/bin/platformio run
```
- Validates clean compilation of the ESP-IDF framework.
- Verifies partition offsets from [partitions_table.csv](file:///home/mahesh/Documents/Workspace/FOTA/Main/partitions_table.csv) are respected: `app0` at `0x10000`, `app1` at `0x1f0000`.

### 2. Hardware Test Steps
1. Flash firmware onto the target board.
2. Observe serial log:
   - Verify `GPIO 18` pulse powers on N706B modem.
   - Verify UART communication on `GPIO 26` / `GPIO 27` at 115200 baud.
   - Confirm active running partition (`app0` or `app1`).
   - Run OTA update and confirm bootloader switches slot upon success, or safely rolls back upon intentional test failure.
