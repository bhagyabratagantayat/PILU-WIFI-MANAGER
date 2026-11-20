# Phase 2 Debugging Report: Startup Sequence & Diagnostic Engine

## 1. Root Cause Analysis

### Issue A: No Application-Level PILU Startup Logs
1. **`ESP_LOGI` Macro Suppression by Compiler:**
   - In Arduino ESP32 core, `ESP_LOGI`, `ESP_LOGW`, and `ESP_LOGE` are governed by `CORE_DEBUG_LEVEL`.
   - By default, `CORE_DEBUG_LEVEL` is set to `ARDUHAL_LOG_LEVEL_NONE` (0).
   - As a consequence, every `ESP_LOG*` call in `wifi_manager.cpp`, `nat_router.cpp`, `nvs_storage.cpp`, and `system_monitor.cpp` was compiled out into empty no-ops (`do {} while(0)`).
2. **Serial Synchronization Race Condition:**
   - The minimal `Serial.println()` statements in `setup()` ran within the first ~500ms following the ROM bootloader's `entry 0x400805b4`.
   - When the board was flashed via `esptool` over RTS/DTR, the PC terminal client had not yet stabilized or re-opened the port in time to catch those initial bytes.
3. **Loop Silence:**
   - In `loop()`, the periodic status monitor exclusively used `ESP_LOGI`, which was compiled out. Therefore, after `setup()` completed, zero bytes were transmitted to UART0, making the device appear completely unresponsive.

### Issue B: Internet Access Unavailable Despite Hotspot Connection
1. **Empty Default STA Password:**
   - In `config.h`, `DEFAULT_STA_PASSWORD` was set to `""`.
   - On a freshly erased or flashed NVS partition, no credentials existed for `AYUSH TECHNOLOGIES`.
   - The ESP32 attempted to associate with the secured network using an empty password, immediately causing an authentication failure (`WIFI_REASON_AUTH_FAIL`, code 202).
2. **Hidden Disconnect Failures:**
   - Because `WIFI_EVENT_STA_DISCONNECTED` logged through suppressed `ESP_LOGW`, the failure was invisible to the user.
   - Without an upstream IP and default gateway, the ESP32 had no uplink route. Hotspot clients received local IP leases (`192.168.4.x`), but could not forward packets to the internet.

---

## 2. Firmware Enhancements & Architecture

### 2.1 Dedicated Hardware Diagnostic Logger (`pilu_log.h` / `pilu_log.cpp`)
- Replaced all macro-based logging with direct hardware `Serial.printf()` calls with automatic `Serial.flush()`.
- Added microsecond-resolution relative timestamps `[ssssss.mmm]`.
- Implemented milestone tagging (`[MILESTONE] >>> <EVENT> | <DETAILS>`).
- Implemented human-readable decoders for all ESP-IDF Wi-Fi disconnect codes (e.g. `AUTH_FAIL (202)`, `NO_AP_FOUND (201)`, `HANDSHAKE_TIMEOUT (204)`) and hardware reset reasons (`esp_reset_reason()`).

### 2.2 Active Internet Connectivity Check
- Implemented `wifi_manager_check_internet()`: opens a non-blocking TCP socket to `1.1.1.1` on port 53.
- Measures and reports round-trip time (RTT) in milliseconds when connected.

### 2.3 Interactive Serial Monitor CLI
To allow configuring credentials without needing an external programmer or waiting for the Phase 4 web dashboard, an interactive CLI was implemented in `loop()`:
- `set_wifi <SSID> <PASSWORD>`: Saves upstream credentials to NVS and immediately connects. Supports quoted strings: `set_wifi "AYUSH TECHNOLOGIES" "MySecretPassword"`.
- `status`: Triggers an immediate diagnostic printout of heap, clients, RSSI, and NAT state.
- `scan`: Scans 2.4 GHz channels and lists all nearby APs with signal strength in dBm.
- `restart`: Reboots the ESP32.

---

## 3. Compilation Metrics
- **Target**: `esp32:esp32:esp32` (ESP32 Core 3.3.12 / ESP-IDF 5.x)
- **Status**: `SUCCESS (Exit Code 0)`
- **Flash Utilization**: `895,444 bytes` (68% of 1.31 MB partition)
- **RAM Utilization**: `46,160 bytes` (14% of 320 KB RAM)
- **Free Heap Headroom**: **`281,520 bytes`**
- **Output Image**: `PILU-WIFI-MANAGER/firmware/build/pilu_firmware.ino.merged.bin`
