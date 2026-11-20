#ifndef PILU_CONFIG_H
#define PILU_CONFIG_H

#include <Arduino.h>

/**
 * @file config.h
 * @brief Default system, Wi-Fi, and NAT configuration parameters.
 * 
 * Hardware: ESP32 DevKit V1 (ESP32-WROOM-32)
 * Project: PILU Wi-Fi Manager
 * 
 * Foundation based on lwIP NAPT and ESP32 NAT Router by:
 * Copyright (c) 2021 Daniel Hope (www.floorsense.nz)
 * Copyright (c) 2021 Kenta Ida (fuga@fugafuga.org)
 * Copyright (c) 2021-2024 Martin Gergeleit (mafrige06@gmail.com)
 */

// Firmware Information
#define PILU_FIRMWARE_VERSION       "1.0.0-phase3"
#define PILU_PROJECT_NAME           "PILU Wi-Fi Manager"
#define PILU_PROJECT_SUBTITLE       "Smart ESP32 Wi-Fi Repeater"

// Serial Baud Rate
#define PILU_SERIAL_BAUD_RATE       115200

// Default Local Admin Dashboard Credentials
#define DEFAULT_ADMIN_PASSWORD      "admin2802"
#define SESSION_TIMEOUT_MS          (15 * 60 * 1000) // 15 minutes
#define MAX_WIFI_PROFILES           5

// Default SoftAP (Hotspot) Parameters
#define DEFAULT_AP_SSID             "PILU"
#define DEFAULT_AP_PASSWORD         "28022006"
#define DEFAULT_AP_CHANNEL          1
#define DEFAULT_AP_MAX_CLIENTS      8
#define DEFAULT_AP_SSID_HIDDEN      0

// SoftAP Network Addressing (192.168.4.0/24)
#define DEFAULT_AP_IP_STR           "192.168.4.1"
#define DEFAULT_AP_NETMASK_STR      "255.255.255.0"
#define DEFAULT_AP_GATEWAY_STR      "192.168.4.1"

// Upstream Default Parameters (Configurable in NVS)
#define DEFAULT_STA_SSID            "AYUSH TECHNOLOGIES"
#define DEFAULT_STA_PASSWORD        "AYUSHTECH"  // Initial default credentials provided by administrator
#define DEFAULT_BACKUP_STA_SSID     "OnePlus Nord CE5"
#define DEFAULT_BACKUP_STA_PASSWORD "" // Pending user credential input

// Dual Network Failover & Hysteresis Parameters
#define PRIMARY_CONNECT_TIMEOUT_MS  12000 // 12 seconds per connection attempt
#define PRIMARY_MAX_RETRIES         2     // Max retries before failing over to backup
#define FAILBACK_COOLDOWN_MS        60000 // Minimum time on backup before checking primary (60s)
#define FAILBACK_SCAN_INTERVAL_MS   30000 // Scan interval while on backup (30s)
#define FAILBACK_RSSI_THRESHOLD     -82   // Minimum RSSI to consider primary stable
#define FAILBACK_CONSECUTIVE_SEEN   2     // Require primary seen in 2 consecutive scans

// Fallback Public DNS Servers for Hotspot Clients
#define DEFAULT_PRIMARY_DNS_STR     "8.8.8.8"
#define DEFAULT_SECONDARY_DNS_STR   "1.1.1.1"

// Reconnection Timing Parameters (in milliseconds)
#define STA_RECONNECT_INITIAL_MS    2000
#define STA_RECONNECT_MAX_BACKOFF_MS 30000

// Monitoring and Diagnostics
#define SYSTEM_STATUS_INTERVAL_MS   10000

// NVS Storage Configuration
#define NVS_NAMESPACE_PILU          "pilu_wifi"
#define NVS_KEY_AP_SSID             "ap_ssid"
#define NVS_KEY_AP_PASS             "ap_pass"
#define NVS_KEY_STA_SSID            "sta_ssid"
#define NVS_KEY_STA_PASS            "sta_pass"
#define NVS_KEY_AP_CHANNEL          "ap_chan"
#define NVS_KEY_AP_MAX_CONN         "ap_maxconn"
#define NVS_KEY_ADMIN_PASS          "admin_pass"
#define NVS_KEY_PROFILES            "wifi_profiles"

#endif // PILU_CONFIG_H
