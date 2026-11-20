#ifndef PILU_WIFI_MANAGER_H
#define PILU_WIFI_MANAGER_H

#include <Arduino.h>
#include "esp_err.h"
#include "esp_wifi.h"
#include "nvs_storage.h"

/**
 * @file wifi_manager.h
 * @brief Dual-network priority-based Wi-Fi manager with automated failover,
 *        failback hysteresis, and SoftAP preservation.
 */

// Connection State Enum
enum StaUplinkState {
    UPLINK_STATE_DISCONNECTED = 0,
    UPLINK_STATE_SCANNING_BEFORE_CONNECT,
    UPLINK_STATE_CONNECTING_PRIMARY,
    UPLINK_STATE_CONNECTING_BACKUP,
    UPLINK_STATE_CONNECTED_PRIMARY,
    UPLINK_STATE_CONNECTED_BACKUP,
    UPLINK_STATE_FAILOVER_BACKOFF
};

esp_err_t wifi_manager_init(const PiluConfig* config);
esp_err_t wifi_manager_start();
void wifi_manager_process();

bool wifi_manager_is_sta_connected();
bool wifi_manager_is_ap_active();
int8_t wifi_manager_get_sta_rssi();
uint8_t wifi_manager_get_ap_client_count();
uint8_t wifi_manager_get_channel();

// Dual Network & Uplink Status
StaUplinkState wifi_manager_get_uplink_state();
const char* wifi_manager_get_uplink_state_str();
uint8_t wifi_manager_get_active_priority(); // 1 = Primary, 2 = Backup, 0 = None

void wifi_manager_get_sta_ip(char* buffer, size_t max_len);
void wifi_manager_get_gateway_ip(char* buffer, size_t max_len);
void wifi_manager_get_ap_ip(char* buffer, size_t max_len);
void wifi_manager_get_sta_ssid(char* buffer, size_t max_len);
void wifi_manager_get_ap_ssid(char* buffer, size_t max_len);

// Reconnection & Profile Management
void wifi_manager_trigger_reconnect();
esp_err_t wifi_manager_update_sta_credentials(const char* ssid, const char* password);
esp_err_t wifi_manager_switch_profile(const char* ssid);
esp_err_t wifi_manager_set_backup_credentials(const char* ssid, const char* password);

// SoftAP Hotspot Management
esp_err_t wifi_manager_update_ap_settings(const char* ssid, const char* password, uint8_t channel, uint8_t max_clients);
esp_err_t wifi_manager_get_connected_clients(wifi_sta_list_t* client_list);

// Non-blocking 2.4 GHz Wi-Fi Scanning
int16_t wifi_manager_start_scan();
int16_t wifi_manager_get_scan_status();
void wifi_manager_delete_scan();

// Internet connectivity status & latency
bool wifi_manager_check_internet(uint32_t* round_trip_ms);
bool wifi_manager_is_internet_connected();
uint32_t wifi_manager_get_internet_latency_ms();

#endif // PILU_WIFI_MANAGER_H
