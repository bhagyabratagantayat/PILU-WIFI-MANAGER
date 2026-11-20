/**
 * @file pilu_firmware.ino
 * @brief PILU Wi-Fi Manager - Smart ESP32 Wi-Fi Repeater Firmware
 * 
 * Hardware: ESP32 DevKit V1 (ESP32-WROOM-32)
 * Features:
 *  - Simultaneous SoftAP ('PILU') + STA ('AYUSH TECHNOLOGIES')
 *  - Native lwIP NAPT internet packet forwarding
 *  - Guaranteed timestamped Serial Monitor diagnostics
 *  - Interactive Serial CLI for dynamic Wi-Fi configuration
 *  - Non-blocking exponential backoff reconnection
 *  - Independent SoftAP preservation during upstream outages
 * 
 * Foundation:
 *  - lwIP NAPT & ESP32 NAT Router by Daniel Hope, Kenta Ida, Martin Gergeleit
 */

#include <Arduino.h>
#include "config.h"
#include "pilu_log.h"
#include "nvs_storage.h"
#include "nat_router.h"
#include "wifi_manager.h"
#include "web_server.h"
#include "system_monitor.h"
#include "esp_system.h"
#include <WiFi.h>

static PiluConfig s_system_config;
static String s_serial_buffer = "";

/**
 * @brief Parses two arguments from command line with full quote and whitespace support.
 * Supports:
 *   set_wifi "AYUSH TECHNOLOGIES" "My Secret Pass 123"
 *   set_wifi SimpleSSID SimplePass
 *   set_wifi "AYUSH TECHNOLOGIES" ""
 */
static bool parse_two_arguments(const String& input, String& arg1, String& arg2) {
    String str = input;
    str.trim();
    if (str.length() == 0) return false;

    int idx = 0;
    int len = str.length();

    // Skip leading whitespace
    while (idx < len && isspace(str[idx])) idx++;
    if (idx >= len) return false;

    // 1. Parse Arg 1 (SSID)
    if (str[idx] == '\"') {
        idx++; // skip opening quote
        int start = idx;
        while (idx < len && str[idx] != '\"') idx++;
        if (idx >= len) return false; // unclosed quote
        arg1 = str.substring(start, idx);
        idx++; // skip closing quote
    } else {
        int start = idx;
        while (idx < len && !isspace(str[idx])) idx++;
        arg1 = str.substring(start, idx);
    }

    // Skip whitespace between arguments
    while (idx < len && isspace(str[idx])) idx++;
    if (idx >= len) {
        // Only one argument provided (open network / empty password)
        arg2 = "";
        return true;
    }

    // 2. Parse Arg 2 (Password)
    if (str[idx] == '\"') {
        idx++; // skip opening quote
        int start = idx;
        while (idx < len && str[idx] != '\"') idx++;
        arg2 = str.substring(start, idx);
    } else {
        int start = idx;
        while (idx < len && !isspace(str[idx])) idx++;
        arg2 = str.substring(start, idx);
    }

    return true;
}

static void process_serial_command(const String& cmd_line) {
    String line = cmd_line;
    line.trim();
    if (line.length() == 0) return;

    if (line.equalsIgnoreCase("help") || line.equalsIgnoreCase("?")) {
        Serial.println("\r\n==================================================");
        Serial.println("       PILU WI-FI MANAGER - SERIAL COMMAND CLI     ");
        Serial.println("==================================================");
        Serial.println("  help                         : Show available commands");
        Serial.println("  status                       : Display real-time system and link diagnostics");
        Serial.println("  scan                         : Scan nearby 2.4 GHz Wi-Fi networks");
        Serial.println("  set_wifi <SSID> <PASSWORD>   : Save Primary credentials to NVS (Priority 1)");
        Serial.println("  set_backup <SSID> <PASSWORD> : Save Backup credentials to NVS (Priority 2)");
        Serial.println("  restart                      : Reboot the ESP32");
        Serial.println();
        Serial.println("  Usage Examples:");
        Serial.println("    set_wifi \"AYUSH TECHNOLOGIES\" \"YourSecretPassword\"");
        Serial.println("    set_backup \"OnePlus Nord CE5\" \"BackupPassword123\"");
        Serial.println("==================================================\r\n");
    } else if (line.equalsIgnoreCase("status")) {
        system_monitor_print_status();
    } else if (line.equalsIgnoreCase("restart") || line.equalsIgnoreCase("reboot")) {
        pilu_log("CMD", "Reboot command received from serial. Restarting ESP32...");
        delay(200);
        esp_restart();
    } else if (line.equalsIgnoreCase("scan")) {
        pilu_log("SCAN", "Starting 2.4 GHz Wi-Fi scan for nearby networks...");
        int n = WiFi.scanNetworks(false, true); // synchronous scan
        pilu_log("SCAN", "Scan finished. Found %d networks:", n);
        for (int i = 0; i < n; i++) {
            Serial.printf("  [%02d] SSID: %-28s | RSSI: %3d dBm | Chan: %2d | Security: %s\r\n",
                          i + 1, WiFi.SSID(i).c_str(), WiFi.RSSI(i), WiFi.channel(i),
                          WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "OPEN" : "WPA2/WPA3");
        }
        Serial.println();
        WiFi.scanDelete();
    } else if (line.startsWith("set_wifi ") || line.startsWith("SET_WIFI ") || line.equalsIgnoreCase("set_wifi")) {
        int first_space = line.indexOf(' ');
        if (first_space < 0) {
            pilu_log_error("CMD_ERROR", "Missing parameters for set_wifi command!");
            Serial.println("  Usage:   set_wifi \"<SSID>\" \"<PASSWORD>\"");
            Serial.println("  Example: set_wifi \"AYUSH TECHNOLOGIES\" \"MyPassword123\"");
            return;
        }

        String rest = line.substring(first_space + 1);
        String new_ssid = "";
        String new_pass = "";

        if (!parse_two_arguments(rest, new_ssid, new_pass)) {
            pilu_log_error("CMD_ERROR", "Failed to parse arguments! Check quotes.");
            Serial.println("  Usage:   set_wifi \"<SSID>\" \"<PASSWORD>\"");
            Serial.println("  Example: set_wifi \"AYUSH TECHNOLOGIES\" \"MyPassword123\"");
            return;
        }

        // Validate SSID
        if (new_ssid.length() < 1 || new_ssid.length() > 32) {
            pilu_log_error("CMD_ERROR", "Invalid SSID length (%d chars). Must be 1 to 32 characters.", new_ssid.length());
            return;
        }

        // Validate Password (if non-empty)
        if (new_pass.length() > 0 && new_pass.length() < 8) {
            pilu_log_error("CMD_ERROR", "Invalid WPA2 password length (%d chars). Must be at least 8 characters or empty.", new_pass.length());
            return;
        }

        pilu_log("CMD", "Updating Upstream Wi-Fi to SSID '%s' (Password: [PROTECTED], Length=%d)...",
                 new_ssid.c_str(), new_pass.length());

        esp_err_t err = wifi_manager_update_sta_credentials(new_ssid.c_str(), new_pass.c_str());
        if (err == ESP_OK) {
            pilu_log("CMD", "Credentials saved to NVS. Connection attempt in progress...");
        } else {
            pilu_log_error("CMD", "Failed to update Wi-Fi credentials: %s", esp_err_to_name(err));
        }
    } else if (line.startsWith("set_backup ") || line.startsWith("SET_BACKUP ") || line.equalsIgnoreCase("set_backup")) {
        int first_space = line.indexOf(' ');
        if (first_space < 0) {
            pilu_log_error("CMD_ERROR", "Missing parameters for set_backup command!");
            Serial.println("  Usage:   set_backup \"<SSID>\" \"<PASSWORD>\"");
            Serial.println("  Example: set_backup \"OnePlus Nord CE5\" \"BackupPassword123\"");
            return;
        }

        String rest = line.substring(first_space + 1);
        String new_ssid = "";
        String new_pass = "";

        if (!parse_two_arguments(rest, new_ssid, new_pass)) {
            pilu_log_error("CMD_ERROR", "Failed to parse arguments! Check quotes.");
            Serial.println("  Usage:   set_backup \"<SSID>\" \"<PASSWORD>\"");
            Serial.println("  Example: set_backup \"OnePlus Nord CE5\" \"BackupPassword123\"");
            return;
        }

        if (new_ssid.length() < 1 || new_ssid.length() > 32) {
            pilu_log_error("CMD_ERROR", "Invalid SSID length (%d chars).", new_ssid.length());
            return;
        }

        pilu_log("CMD", "Updating Backup Wi-Fi profile '%s' (Password: [PROTECTED], Length=%d)...",
                 new_ssid.c_str(), new_pass.length());

        esp_err_t err = wifi_manager_set_backup_credentials(new_ssid.c_str(), new_pass.c_str());
        if (err == ESP_OK) {
            pilu_log("CMD", "Backup Wi-Fi profile saved to NVS (Priority 2).");
        } else {
            pilu_log_error("CMD", "Failed to save backup profile: %s", esp_err_to_name(err));
        }
    } else {
        pilu_log_warn("CMD", "Unrecognized command: '%s'. Type 'help' for instructions.", line.c_str());
    }
}

void setup() {
    // 0. Ensure CPU runs at maximum 240 MHz clock frequency for packet routing performance
    setCpuFrequencyMhz(240);

    // 1. Initialize hardware Serial UART0 at 115200 baud
    Serial.begin(PILU_SERIAL_BAUD_RATE);
    
    // Allow CP2102 / CH340 host USB bridge to stabilize
    delay(1200);

    Serial.println();
    Serial.println("==================================================");
    Serial.printf(" %s v%s\n", PILU_PROJECT_NAME, PILU_FIRMWARE_VERSION);
    Serial.printf(" %s\n", PILU_PROJECT_SUBTITLE);
    Serial.println(" Target Hardware: ESP32 DevKit V1 (ESP32-WROOM-32)");
    Serial.println(" Foundation: lwIP NAPT & ESP32 NAT Router");
    Serial.println(" Copyright (c) 2021 Daniel Hope, Kenta Ida, Martin Gergeleit");
    Serial.println("==================================================");
    Serial.println();

    pilu_log_milestone("BOOT", "ESP32 DevKit V1 startup sequence initiated");
    pilu_log_milestone("SERIAL INITIALIZED", "UART0 configured at 115200 Baud (8N1)");

    // 2. Query hardware reset reason
    esp_reset_reason_t rst_reason = esp_reset_reason();
    pilu_log("BOOT", "Hardware Reset Reason: %s", pilu_reset_reason_to_str(rst_reason));

    // 3. Initialize NVS storage
    esp_err_t err = nvs_storage_init();
    if (err != ESP_OK) {
        pilu_log_error("MAIN", "NVS initialization failed: %s", esp_err_to_name(err));
    }

    // 4. Load configuration
    nvs_storage_load_config(&s_system_config);

    char masked_ap_pass[32] = {0};
    char masked_sta_pass[32] = {0};
    nvs_storage_mask_secret(s_system_config.ap_password, masked_ap_pass, sizeof(masked_ap_pass));
    nvs_storage_mask_secret(s_system_config.sta_password, masked_sta_pass, sizeof(masked_sta_pass));

    pilu_log("CONFIG", "SoftAP Settings: SSID='%s' | Pass=%s | Chan=%d | MaxClients=%d",
             s_system_config.ap_ssid, masked_ap_pass, s_system_config.ap_channel, s_system_config.ap_max_clients);
    pilu_log("CONFIG", "Upstream STA Settings: SSID='%s' | Pass=%s",
             s_system_config.sta_ssid, masked_sta_pass);

    // 5. Initialize Wi-Fi subsystem (AP+STA mode)
    err = wifi_manager_init(&s_system_config);
    if (err != ESP_OK) {
        pilu_log_error("MAIN", "Wi-Fi Manager init failed: %s", esp_err_to_name(err));
    }

    // 6. Start Wi-Fi interfaces
    err = wifi_manager_start();
    if (err != ESP_OK) {
        pilu_log_error("MAIN", "Wi-Fi Manager start failed: %s", esp_err_to_name(err));
    }

    // 7. Start Local Administration Web Server (http://192.168.4.1)
    err = web_server_init();
    if (err != ESP_OK) {
        pilu_log_error("MAIN", "Web Server initialization failed: %s", esp_err_to_name(err));
    }

    // 8. Initialize background system monitor
    system_monitor_init();

    pilu_log_milestone("SYSTEM READY", "Hotspot 'PILU' active | Admin UI: http://192.168.4.1 | CLI active");
    Serial.println();
}

void loop() {
    // 1. Process local web server client requests
    web_server_process();

    // 2. Process Wi-Fi reconnection state machine
    wifi_manager_process();

    // 3. Process background diagnostic monitor
    system_monitor_process();

    // 4. Read interactive Serial console commands
    while (Serial.available() > 0) {
        char c = (char)Serial.read();
        if (c == '\r') continue;
        if (c == '\n') {
            process_serial_command(s_serial_buffer);
            s_serial_buffer = "";
        } else {
            if (s_serial_buffer.length() < 128) {
                s_serial_buffer += c;
            }
        }
    }

    // Yield for FreeRTOS scheduler (1ms for low-latency packet forwarding)
    delay(1);
}
