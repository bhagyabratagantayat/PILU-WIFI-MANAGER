#ifndef PILU_NVS_STORAGE_H
#define PILU_NVS_STORAGE_H

#include <Arduino.h>
#include "esp_err.h"
#include "config.h"

/**
 * @file nvs_storage.h
 * @brief Persistent configuration and Wi-Fi profile storage using ESP32 NVS.
 */

// Profile priority enumeration
enum WifiPriority {
    WIFI_PRIORITY_PRIMARY = 0,
    WIFI_PRIORITY_SECONDARY = 1,
    WIFI_PRIORITY_BACKUP = 2
};

struct WifiProfile {
    char ssid[33];
    char password[65];
    uint8_t priority; // WifiPriority: 0=Primary, 1=Secondary, 2=Backup
    bool enabled;
};

struct PiluConfig {
    char ap_ssid[33];
    char ap_password[65];
    uint8_t ap_channel;
    uint8_t ap_max_clients;
    bool ap_hidden;

    char sta_ssid[33];
    char sta_password[65];
};

esp_err_t nvs_storage_init();
esp_err_t nvs_storage_load_config(PiluConfig* config);
esp_err_t nvs_storage_save_config(const PiluConfig* config);
esp_err_t nvs_storage_set_sta_credentials(const char* ssid, const char* password);
esp_err_t nvs_storage_set_ap_credentials(const char* ssid, const char* password);
esp_err_t nvs_storage_set_ap_settings(const char* ssid, const char* password, uint8_t channel, uint8_t max_clients);

// Local Admin Dashboard Authentication
esp_err_t nvs_storage_get_admin_password(char* buffer, size_t max_len);
esp_err_t nvs_storage_set_admin_password(const char* new_password);

// Multi-Profile Storage Management
esp_err_t nvs_storage_load_profiles(WifiProfile* profiles, size_t max_count, size_t* actual_count);
esp_err_t nvs_storage_save_profiles(const WifiProfile* profiles, size_t count);
esp_err_t nvs_storage_add_or_update_profile(const WifiProfile* profile);
esp_err_t nvs_storage_delete_profile(const char* ssid);
esp_err_t nvs_storage_get_profile(const char* ssid, WifiProfile* profile);
esp_err_t nvs_storage_get_profile_by_priority(uint8_t priority, WifiProfile* profile);

// Helper for security: mask sensitive credentials for logs and APIs
void nvs_storage_mask_secret(const char* secret, char* buffer, size_t buf_len);

#endif // PILU_NVS_STORAGE_H
