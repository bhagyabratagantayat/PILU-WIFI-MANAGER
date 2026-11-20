#include "nvs_storage.h"
#include "config.h"
#include "pilu_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>

static const char* TAG = "NVS";

esp_err_t nvs_storage_init() {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        pilu_log_warn(TAG, "NVS partition truncated or updated; erasing and re-initializing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret == ESP_OK) {
        pilu_log_milestone("NVS INITIALIZED", "Namespace 'pilu_wifi' ready");
    } else {
        pilu_log_error(TAG, "Failed to initialize NVS flash: %s", esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t nvs_storage_load_config(PiluConfig* config) {
    if (!config) return ESP_ERR_INVALID_ARG;

    // Apply compiled defaults first
    strncpy(config->ap_ssid, DEFAULT_AP_SSID, sizeof(config->ap_ssid) - 1);
    config->ap_ssid[sizeof(config->ap_ssid) - 1] = '\0';

    strncpy(config->ap_password, DEFAULT_AP_PASSWORD, sizeof(config->ap_password) - 1);
    config->ap_password[sizeof(config->ap_password) - 1] = '\0';

    config->ap_channel = DEFAULT_AP_CHANNEL;
    config->ap_max_clients = DEFAULT_AP_MAX_CLIENTS;
    config->ap_hidden = (DEFAULT_AP_SSID_HIDDEN == 1);

    strncpy(config->sta_ssid, DEFAULT_STA_SSID, sizeof(config->sta_ssid) - 1);
    config->sta_ssid[sizeof(config->sta_ssid) - 1] = '\0';

    strncpy(config->sta_password, DEFAULT_STA_PASSWORD, sizeof(config->sta_password) - 1);
    config->sta_password[sizeof(config->sta_password) - 1] = '\0';

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE_PILU, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        pilu_log(TAG, "No existing NVS configuration found; initialized default profiles");
        return ESP_OK;
    }

    size_t len = sizeof(config->ap_ssid);
    if (nvs_get_str(handle, NVS_KEY_AP_SSID, config->ap_ssid, &len) == ESP_OK) {
        pilu_log(TAG, "Loaded AP SSID from NVS: '%s'", config->ap_ssid);
    }

    len = sizeof(config->ap_password);
    if (nvs_get_str(handle, NVS_KEY_AP_PASS, config->ap_password, &len) == ESP_OK) {
        pilu_log(TAG, "Loaded AP Password from NVS: [PROTECTED]");
    }

    uint8_t chan = 0;
    if (nvs_get_u8(handle, NVS_KEY_AP_CHANNEL, &chan) == ESP_OK && chan >= 1 && chan <= 13) {
        config->ap_channel = chan;
    }

    uint8_t maxc = 0;
    if (nvs_get_u8(handle, NVS_KEY_AP_MAX_CONN, &maxc) == ESP_OK && maxc >= 1 && maxc <= 10) {
        config->ap_max_clients = maxc;
    }

    len = sizeof(config->sta_ssid);
    if (nvs_get_str(handle, NVS_KEY_STA_SSID, config->sta_ssid, &len) == ESP_OK) {
        pilu_log(TAG, "Loaded Upstream STA SSID from NVS: '%s'", config->sta_ssid);
    }

    len = sizeof(config->sta_password);
    if (nvs_get_str(handle, NVS_KEY_STA_PASS, config->sta_password, &len) == ESP_OK) {
        if (strlen(config->sta_password) == 0 && strlen(DEFAULT_STA_PASSWORD) > 0) {
            strncpy(config->sta_password, DEFAULT_STA_PASSWORD, sizeof(config->sta_password) - 1);
            config->sta_password[sizeof(config->sta_password) - 1] = '\0';
        }
        pilu_log(TAG, "Loaded Upstream STA Password from NVS: [PROTECTED]");
    }

    nvs_close(handle);
    return ESP_OK;
}

esp_err_t nvs_storage_save_config(const PiluConfig* config) {
    if (!config) return ESP_ERR_INVALID_ARG;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE_PILU, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        pilu_log_error(TAG, "Failed to open NVS for writing: %s", esp_err_to_name(err));
        return err;
    }

    nvs_set_str(handle, NVS_KEY_AP_SSID, config->ap_ssid);
    nvs_set_str(handle, NVS_KEY_AP_PASS, config->ap_password);
    nvs_set_u8(handle, NVS_KEY_AP_CHANNEL, config->ap_channel);
    nvs_set_u8(handle, NVS_KEY_AP_MAX_CONN, config->ap_max_clients);
    nvs_set_str(handle, NVS_KEY_STA_SSID, config->sta_ssid);
    nvs_set_str(handle, NVS_KEY_STA_PASS, config->sta_password);

    err = nvs_commit(handle);
    nvs_close(handle);

    if (err == ESP_OK) {
        pilu_log(TAG, "Configuration safely committed to NVS");
    } else {
        pilu_log_error(TAG, "NVS commit failed: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t nvs_storage_set_sta_credentials(const char* ssid, const char* password) {
    if (!ssid) return ESP_ERR_INVALID_ARG;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE_PILU, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    nvs_set_str(handle, NVS_KEY_STA_SSID, ssid);
    if (password && strlen(password) > 0) {
        nvs_set_str(handle, NVS_KEY_STA_PASS, password);
    } else {
        nvs_erase_key(handle, NVS_KEY_STA_PASS);
    }

    err = nvs_commit(handle);
    nvs_close(handle);
    pilu_log(TAG, "Saved Upstream STA SSID '%s' (Password: [PROTECTED]) to NVS", ssid);
    return err;
}

esp_err_t nvs_storage_set_ap_credentials(const char* ssid, const char* password) {
    if (!ssid || !password) return ESP_ERR_INVALID_ARG;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE_PILU, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    nvs_set_str(handle, NVS_KEY_AP_SSID, ssid);
    nvs_set_str(handle, NVS_KEY_AP_PASS, password);

    err = nvs_commit(handle);
    nvs_close(handle);
    pilu_log(TAG, "Saved Hotspot SSID '%s' (Password: [PROTECTED]) to NVS", ssid);
    return err;
}

esp_err_t nvs_storage_set_ap_settings(const char* ssid, const char* password, uint8_t channel, uint8_t max_clients) {
    if (!ssid || strlen(ssid) == 0) return ESP_ERR_INVALID_ARG;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE_PILU, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    nvs_set_str(handle, NVS_KEY_AP_SSID, ssid);
    if (password && strlen(password) > 0) {
        nvs_set_str(handle, NVS_KEY_AP_PASS, password);
    }
    if (channel >= 1 && channel <= 13) {
        nvs_set_u8(handle, NVS_KEY_AP_CHANNEL, channel);
    }
    if (max_clients >= 1 && max_clients <= 10) {
        nvs_set_u8(handle, NVS_KEY_AP_MAX_CONN, max_clients);
    }

    err = nvs_commit(handle);
    nvs_close(handle);
    pilu_log(TAG, "Updated SoftAP Settings in NVS (SSID: %s, Chan: %d, MaxClients: %d)",
             ssid, channel, max_clients);
    return err;
}

esp_err_t nvs_storage_get_admin_password(char* buffer, size_t max_len) {
    if (!buffer || max_len == 0) return ESP_ERR_INVALID_ARG;

    strncpy(buffer, DEFAULT_ADMIN_PASSWORD, max_len - 1);
    buffer[max_len - 1] = '\0';

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE_PILU, NVS_READONLY, &handle);
    if (err == ESP_OK) {
        size_t len = max_len;
        if (nvs_get_str(handle, NVS_KEY_ADMIN_PASS, buffer, &len) == ESP_OK) {
            // Admin pass loaded successfully
        }
        nvs_close(handle);
    }
    return ESP_OK;
}

esp_err_t nvs_storage_set_admin_password(const char* new_password) {
    if (!new_password || strlen(new_password) < 6) return ESP_ERR_INVALID_ARG;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE_PILU, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    err = nvs_set_str(handle, NVS_KEY_ADMIN_PASS, new_password);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
        pilu_log(TAG, "Admin password updated successfully in NVS");
    }
    nvs_close(handle);
    return err;
}

esp_err_t nvs_storage_load_profiles(WifiProfile* profiles, size_t max_count, size_t* actual_count) {
    if (!profiles || max_count == 0 || !actual_count) return ESP_ERR_INVALID_ARG;

    *actual_count = 0;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE_PILU, NVS_READONLY, &handle);
    if (err == ESP_OK) {
        size_t required_size = 0;
        err = nvs_get_blob(handle, NVS_KEY_PROFILES, NULL, &required_size);
        if (err == ESP_OK && required_size > 0) {
            size_t count = required_size / sizeof(WifiProfile);
            if (count > max_count) count = max_count;
            size_t read_bytes = count * sizeof(WifiProfile);
            err = nvs_get_blob(handle, NVS_KEY_PROFILES, profiles, &read_bytes);
            if (err == ESP_OK) {
                *actual_count = count;
                nvs_close(handle);

                // Ensure both configured profiles (AYUSH and OnePlus) exist in NVS without overwriting
                bool has_ayush = false;
                bool has_oneplus = false;
                for (size_t i = 0; i < *actual_count; i++) {
                    if (strcmp(profiles[i].ssid, DEFAULT_STA_SSID) == 0) has_ayush = true;
                    if (strcmp(profiles[i].ssid, DEFAULT_BACKUP_STA_SSID) == 0) has_oneplus = true;
                }

                if (!has_oneplus && *actual_count < max_count) {
                    pilu_log(TAG, "Adding '%s' as Priority 2 (Backup) to NVS profiles", DEFAULT_BACKUP_STA_SSID);
                    strncpy(profiles[*actual_count].ssid, DEFAULT_BACKUP_STA_SSID, sizeof(profiles[*actual_count].ssid) - 1);
                    strncpy(profiles[*actual_count].password, DEFAULT_BACKUP_STA_PASSWORD, sizeof(profiles[*actual_count].password) - 1);
                    profiles[*actual_count].priority = WIFI_PRIORITY_SECONDARY;
                    profiles[*actual_count].enabled = true;
                    (*actual_count)++;
                    nvs_storage_save_profiles(profiles, *actual_count);
                }

                return ESP_OK;
            }
        }
        nvs_close(handle);
    }

    // If no profiles in NVS, seed with both configured profiles:
    // Profile 0: AYUSH TECHNOLOGIES (Priority 1 - Primary)
    // Profile 1: OnePlus Nord CE5 (Priority 2 - Backup)
    pilu_log(TAG, "Seeding default Wi-Fi profiles: Primary '%s', Backup '%s'",
             DEFAULT_STA_SSID, DEFAULT_BACKUP_STA_SSID);
    memset(profiles, 0, sizeof(WifiProfile) * max_count);

    strncpy(profiles[0].ssid, DEFAULT_STA_SSID, sizeof(profiles[0].ssid) - 1);
    strncpy(profiles[0].password, DEFAULT_STA_PASSWORD, sizeof(profiles[0].password) - 1);
    profiles[0].priority = WIFI_PRIORITY_PRIMARY;
    profiles[0].enabled = true;

    strncpy(profiles[1].ssid, DEFAULT_BACKUP_STA_SSID, sizeof(profiles[1].ssid) - 1);
    strncpy(profiles[1].password, DEFAULT_BACKUP_STA_PASSWORD, sizeof(profiles[1].password) - 1);
    profiles[1].priority = WIFI_PRIORITY_SECONDARY;
    profiles[1].enabled = true;

    *actual_count = 2;
    nvs_storage_save_profiles(profiles, 2);
    return ESP_OK;
}

esp_err_t nvs_storage_save_profiles(const WifiProfile* profiles, size_t count) {
    if (!profiles && count > 0) return ESP_ERR_INVALID_ARG;
    if (count > MAX_WIFI_PROFILES) count = MAX_WIFI_PROFILES;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE_PILU, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    if (count == 0) {
        nvs_erase_key(handle, NVS_KEY_PROFILES);
    } else {
        size_t bytes = count * sizeof(WifiProfile);
        err = nvs_set_blob(handle, NVS_KEY_PROFILES, profiles, bytes);
    }

    if (err == ESP_OK) {
        err = nvs_commit(handle);
        pilu_log(TAG, "Saved %d Wi-Fi profile(s) to NVS", (int)count);
    }
    nvs_close(handle);
    return err;
}

esp_err_t nvs_storage_add_or_update_profile(const WifiProfile* profile) {
    if (!profile || strlen(profile->ssid) == 0) return ESP_ERR_INVALID_ARG;

    WifiProfile profiles[MAX_WIFI_PROFILES];
    size_t count = 0;
    nvs_storage_load_profiles(profiles, MAX_WIFI_PROFILES, &count);

    bool updated = false;
    for (size_t i = 0; i < count; i++) {
        if (strcmp(profiles[i].ssid, profile->ssid) == 0) {
            // Update existing profile
            if (strlen(profile->password) > 0) {
                strncpy(profiles[i].password, profile->password, sizeof(profiles[i].password) - 1);
                profiles[i].password[sizeof(profiles[i].password) - 1] = '\0';
            }
            profiles[i].priority = profile->priority;
            profiles[i].enabled = profile->enabled;
            updated = true;
            break;
        }
    }

    if (!updated) {
        if (count >= MAX_WIFI_PROFILES) {
            // Replace the last secondary/backup profile if full
            count = MAX_WIFI_PROFILES - 1;
        }
        memcpy(&profiles[count], profile, sizeof(WifiProfile));
        count++;
    }

    return nvs_storage_save_profiles(profiles, count);
}

esp_err_t nvs_storage_delete_profile(const char* ssid) {
    if (!ssid || strlen(ssid) == 0) return ESP_ERR_INVALID_ARG;

    WifiProfile profiles[MAX_WIFI_PROFILES];
    size_t count = 0;
    nvs_storage_load_profiles(profiles, MAX_WIFI_PROFILES, &count);

    WifiProfile new_profiles[MAX_WIFI_PROFILES];
    size_t new_count = 0;

    for (size_t i = 0; i < count; i++) {
        if (strcmp(profiles[i].ssid, ssid) != 0) {
            memcpy(&new_profiles[new_count], &profiles[i], sizeof(WifiProfile));
            new_count++;
        }
    }

    return nvs_storage_save_profiles(new_profiles, new_count);
}

esp_err_t nvs_storage_get_profile(const char* ssid, WifiProfile* profile) {
    if (!ssid || !profile) return ESP_ERR_INVALID_ARG;

    WifiProfile profiles[MAX_WIFI_PROFILES];
    size_t count = 0;
    nvs_storage_load_profiles(profiles, MAX_WIFI_PROFILES, &count);

    for (size_t i = 0; i < count; i++) {
        if (strcmp(profiles[i].ssid, ssid) == 0) {
            memcpy(profile, &profiles[i], sizeof(WifiProfile));
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t nvs_storage_get_profile_by_priority(uint8_t priority, WifiProfile* profile) {
    if (!profile) return ESP_ERR_INVALID_ARG;

    WifiProfile profiles[MAX_WIFI_PROFILES];
    size_t count = 0;
    nvs_storage_load_profiles(profiles, MAX_WIFI_PROFILES, &count);

    for (size_t i = 0; i < count; i++) {
        if (profiles[i].enabled && profiles[i].priority == priority) {
            memcpy(profile, &profiles[i], sizeof(WifiProfile));
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

void nvs_storage_mask_secret(const char* secret, char* buffer, size_t buf_len) {
    if (!buffer || buf_len == 0) return;
    if (!secret || strlen(secret) == 0) {
        snprintf(buffer, buf_len, "[NOT SET / EMPTY]");
        return;
    }
    snprintf(buffer, buf_len, "******** (len=%u)", (unsigned int)strlen(secret));
}
