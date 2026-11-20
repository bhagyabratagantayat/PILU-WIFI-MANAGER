#include "wifi_manager.h"
#include "nat_router.h"
#include "config.h"
#include "pilu_log.h"
#include <WiFi.h>
#include "esp_wifi.h"
#include "esp_netif.h"
#include "lwip/sockets.h"

static const char* TAG = "WIFI";

static PiluConfig s_config;
static bool s_sta_connected = false;
static bool s_ap_active = false;
static uint8_t s_ap_client_count = 0;
static int8_t s_sta_rssi = -100;
static uint8_t s_current_channel = 1;
static char s_sta_ip_str[16] = "0.0.0.0";
static char s_sta_gateway_str[16] = "0.0.0.0";

// Dual-Network State Machine
static StaUplinkState s_uplink_state = UPLINK_STATE_DISCONNECTED;
static uint8_t s_primary_fail_count = 0;
static unsigned long s_connect_start_time = 0;
static unsigned long s_backup_connect_time = 0;
static unsigned long s_last_failback_scan = 0;
static uint8_t s_primary_consecutive_seen = 0;
static unsigned long s_failover_cooldown_until = 0;
static bool s_disconnect_ignore = false;

// Reconnection backoff state
static unsigned long s_last_disconnect_time = 0;
static unsigned long s_reconnect_delay_ms = STA_RECONNECT_INITIAL_MS;
static bool s_reconnect_pending = false;

// Internet connectivity check ticker
static unsigned long s_last_internet_check = 0;
static bool s_internet_available = false;
static uint32_t s_last_latency_ms = 0;

// Internal helpers
static void connect_to_network(const char* ssid, const char* pass, StaUplinkState target_state) {
    s_uplink_state = target_state;
    s_connect_start_time = millis();
    strncpy(s_config.sta_ssid, ssid, sizeof(s_config.sta_ssid) - 1);
    s_config.sta_ssid[sizeof(s_config.sta_ssid) - 1] = '\0';

    if (pass) {
        strncpy(s_config.sta_password, pass, sizeof(s_config.sta_password) - 1);
        s_config.sta_password[sizeof(s_config.sta_password) - 1] = '\0';
    } else {
        s_config.sta_password[0] = '\0';
    }

    s_disconnect_ignore = true;
    esp_wifi_disconnect();
    delay(50);
    s_disconnect_ignore = false;
    WiFi.begin(s_config.sta_ssid, strlen(s_config.sta_password) > 0 ? s_config.sta_password : nullptr);
}

static void try_connect_primary() {
    WifiProfile prof;
    if (nvs_storage_get_profile(DEFAULT_STA_SSID, &prof) == ESP_OK && prof.enabled) {
        pilu_log("UPLINK", "Attempting connection to Primary: '%s' (Priority 1)...", prof.ssid);
        connect_to_network(prof.ssid, prof.password, UPLINK_STATE_CONNECTING_PRIMARY);
    } else {
        pilu_log("UPLINK", "Attempting connection to default Primary: '%s'...", DEFAULT_STA_SSID);
        connect_to_network(DEFAULT_STA_SSID, DEFAULT_STA_PASSWORD, UPLINK_STATE_CONNECTING_PRIMARY);
    }
}

static void try_connect_backup() {
    WifiProfile prof;
    if (nvs_storage_get_profile(DEFAULT_BACKUP_STA_SSID, &prof) == ESP_OK && prof.enabled) {
        pilu_log_milestone("FAILOVER TRIGGERED", "Switching to Backup network 'OnePlus Nord CE5' (Priority 2)...");
        connect_to_network(prof.ssid, prof.password, UPLINK_STATE_CONNECTING_BACKUP);
    } else {
        pilu_log_warn("FAILOVER", "Backup profile '%s' not configured. Waiting for admin credentials.", DEFAULT_BACKUP_STA_SSID);
        s_uplink_state = UPLINK_STATE_FAILOVER_BACKOFF;
        s_last_disconnect_time = millis();
    }
}

static void on_arduino_wifi_event(WiFiEvent_t event, WiFiEventInfo_t info) {
    switch (event) {
        case ARDUINO_EVENT_WIFI_AP_START: {
            s_ap_active = true;
            pilu_log_milestone("WIFI AP STARTED", s_config.ap_ssid);

            esp_netif_t* ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
            esp_netif_t* sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
            nat_router_init(ap_netif, sta_netif);
            nat_router_configure_ap_netif(DEFAULT_AP_IP_STR, DEFAULT_AP_NETMASK_STR, DEFAULT_AP_GATEWAY_STR);
            nat_router_enable();
            break;
        }
        case ARDUINO_EVENT_WIFI_AP_STOP: {
            s_ap_active = false;
            pilu_log_warn(TAG, "Hotspot stopped");
            break;
        }
        case ARDUINO_EVENT_WIFI_AP_STACONNECTED: {
            s_ap_client_count++;
            pilu_log(TAG, "Client joined PILU hotspot: MAC %02X:%02X:%02X:%02X:%02X:%02X (Total Clients: %d)",
                     info.wifi_ap_staconnected.mac[0], info.wifi_ap_staconnected.mac[1],
                     info.wifi_ap_staconnected.mac[2], info.wifi_ap_staconnected.mac[3],
                     info.wifi_ap_staconnected.mac[4], info.wifi_ap_staconnected.mac[5],
                     s_ap_client_count);
            break;
        }
        case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED: {
            if (s_ap_client_count > 0) s_ap_client_count--;
            pilu_log(TAG, "Client left PILU hotspot: MAC %02X:%02X:%02X:%02X:%02X:%02X (Remaining Clients: %d)",
                     info.wifi_ap_stadisconnected.mac[0], info.wifi_ap_stadisconnected.mac[1],
                     info.wifi_ap_stadisconnected.mac[2], info.wifi_ap_stadisconnected.mac[3],
                     info.wifi_ap_stadisconnected.mac[4], info.wifi_ap_stadisconnected.mac[5],
                     s_ap_client_count);
            break;
        }
        case ARDUINO_EVENT_WIFI_STA_START: {
            pilu_log_milestone("STA CONNECTING", s_config.sta_ssid);
            break;
        }
        case ARDUINO_EVENT_WIFI_STA_CONNECTED: {
            s_current_channel = info.wifi_sta_connected.channel;
            char details[64];
            snprintf(details, sizeof(details), "SSID: %s | Channel: %d",
                     s_config.sta_ssid, (int)s_current_channel);
            pilu_log_milestone("STA CONNECTED", details);
            break;
        }
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: {
            if (s_disconnect_ignore) {
                s_disconnect_ignore = false;
                pilu_log(TAG, "Ignoring synthetic disconnect event during network switch.");
                break;
            }

            s_sta_connected = false;
            s_internet_available = false;
            s_sta_rssi = -100;
            strncpy(s_sta_ip_str, "0.0.0.0", sizeof(s_sta_ip_str));
            strncpy(s_sta_gateway_str, "0.0.0.0", sizeof(s_sta_gateway_str));

            uint8_t reason = info.wifi_sta_disconnected.reason;
            const char* reason_str = pilu_wifi_reason_to_str(reason);
            pilu_log_warn("WIFI_DISCONNECT", "Disconnected from '%s' | Reason: %s",
                          s_config.sta_ssid, reason_str);

            pilu_log(TAG, "HOTSPOT 'PILU' PRESERVED (Clients remain connected to local subnet 192.168.4.1)");

            // If we just initiated a connection and it's an immediate transient disconnect, ignore it
            if ((millis() - s_connect_start_time < 600) && (reason == 0 || reason == 200 || reason == 8)) {
                pilu_log(TAG, "Ignoring transient disconnect immediately following WiFi.begin()");
                break;
            }

            // Failover state transition handling
            if (s_uplink_state == UPLINK_STATE_CONNECTED_PRIMARY || s_uplink_state == UPLINK_STATE_CONNECTING_PRIMARY) {
                s_primary_fail_count++;
                pilu_log_warn("FAILOVER", "Primary network failure count: %d/%d", s_primary_fail_count, PRIMARY_MAX_RETRIES);

                if (s_primary_fail_count >= PRIMARY_MAX_RETRIES) {
                    s_uplink_state = UPLINK_STATE_CONNECTING_BACKUP;
                    s_reconnect_pending = true;
                    s_last_disconnect_time = millis();
                    s_reconnect_delay_ms = 150;
                } else {
                    s_reconnect_pending = true;
                    s_last_disconnect_time = millis();
                    s_reconnect_delay_ms = 2000;
                }
            } else if (s_uplink_state == UPLINK_STATE_CONNECTED_BACKUP || s_uplink_state == UPLINK_STATE_CONNECTING_BACKUP) {
                pilu_log_warn("FAILOVER", "Backup network '%s' disconnected. Entering backoff.", s_config.sta_ssid);
                s_uplink_state = UPLINK_STATE_FAILOVER_BACKOFF;
                s_reconnect_pending = true;
                s_last_disconnect_time = millis();
                s_reconnect_delay_ms = 8000;
            }
            break;
        }
        case ARDUINO_EVENT_WIFI_STA_GOT_IP: {
            snprintf(s_sta_ip_str, sizeof(s_sta_ip_str), IPSTR, IP2STR(&info.got_ip.ip_info.ip));
            snprintf(s_sta_gateway_str, sizeof(s_sta_gateway_str), IPSTR, IP2STR(&info.got_ip.ip_info.gw));
            s_sta_connected = true;
            s_reconnect_pending = false;

            char ip_details[64];
            snprintf(ip_details, sizeof(ip_details), "%s (GW: %s)", s_sta_ip_str, s_sta_gateway_str);
            pilu_log_milestone("STA IP ADDRESS", ip_details);

            // Transition state machine
            if (strcmp(s_config.sta_ssid, DEFAULT_STA_SSID) == 0) {
                s_uplink_state = UPLINK_STATE_CONNECTED_PRIMARY;
                s_primary_fail_count = 0;
                pilu_log_milestone("UPLINK ACTIVE", "Primary network 'AYUSH TECHNOLOGIES' (Priority 1) CONNECTED");
            } else if (strcmp(s_config.sta_ssid, DEFAULT_BACKUP_STA_SSID) == 0) {
                s_uplink_state = UPLINK_STATE_CONNECTED_BACKUP;
                s_backup_connect_time = millis();
                s_primary_consecutive_seen = 0;
                pilu_log_milestone("UPLINK ACTIVE", "Backup network 'OnePlus Nord CE5' (Priority 2) CONNECTED");
            }

            // Query RSSI
            wifi_ap_record_t ap_info;
            if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
                s_sta_rssi = ap_info.rssi;
                char rssi_str[16];
                snprintf(rssi_str, sizeof(rssi_str), "%d dBm", s_sta_rssi);
                pilu_log_milestone("STA RSSI", rssi_str);
            }

            // Re-assert NAPT and propagate DNS
            nat_router_enable();
            nat_router_sync_dns();

            // Run immediate internet check
            uint32_t rtt = 0;
            if (wifi_manager_check_internet(&rtt)) {
                s_internet_available = true;
                s_last_latency_ms = rtt;
                char rtt_str[32];
                snprintf(rtt_str, sizeof(rtt_str), "ONLINE (1.1.1.1 RTT=%lu ms)", rtt);
                pilu_log_milestone("INTERNET CONNECTIVITY CHECK", rtt_str);
            } else {
                s_internet_available = false;
                pilu_log_milestone("INTERNET CONNECTIVITY CHECK", "LIMITED (Gateway reached, checking public route)");
            }
            break;
        }
        default:
            break;
    }
}

esp_err_t wifi_manager_init(const PiluConfig* config) {
    if (!config) return ESP_ERR_INVALID_ARG;
    memcpy(&s_config, config, sizeof(PiluConfig));

    WiFi.onEvent(on_arduino_wifi_event);
    pilu_log(TAG, "Wi-Fi Manager initialized successfully");
    return ESP_OK;
}

esp_err_t wifi_manager_start() {
    pilu_log_milestone("WIFI AP STARTING", s_config.ap_ssid);

    WiFi.softAPConfig(
        IPAddress(192, 168, 4, 1),
        IPAddress(192, 168, 4, 1),
        IPAddress(255, 255, 255, 0)
    );

    WiFi.mode(WIFI_AP_STA);

    bool ap_ok = WiFi.softAP(
        s_config.ap_ssid,
        strlen(s_config.ap_password) > 0 ? s_config.ap_password : nullptr,
        s_config.ap_channel,
        s_config.ap_hidden ? 1 : 0,
        s_config.ap_max_clients
    );

    if (!ap_ok) {
        pilu_log_error(TAG, "WiFi.softAP failed to start '%s'", s_config.ap_ssid);
        return ESP_FAIL;
    }

    WiFi.setSleep(false);
    esp_wifi_set_ps(WIFI_PS_NONE);
    WiFi.setTxPower(WIFI_POWER_19_5dBm);

    // Initial Priority 1 connection attempt
    try_connect_primary();

    return ESP_OK;
}

void wifi_manager_process() {
    unsigned long now = millis();

    // 1. Connection Timeout Monitor (12s timeout during connection attempt)
    if ((s_uplink_state == UPLINK_STATE_CONNECTING_PRIMARY || s_uplink_state == UPLINK_STATE_CONNECTING_BACKUP) && !s_sta_connected) {
        if (now - s_connect_start_time >= PRIMARY_CONNECT_TIMEOUT_MS) {
            pilu_log_warn("UPLINK", "Connection attempt to '%s' timed out (%d ms)",
                          s_config.sta_ssid, PRIMARY_CONNECT_TIMEOUT_MS);
            WiFi.disconnect();

            if (s_uplink_state == UPLINK_STATE_CONNECTING_PRIMARY) {
                s_primary_fail_count++;
                if (s_primary_fail_count >= PRIMARY_MAX_RETRIES) {
                    s_uplink_state = UPLINK_STATE_CONNECTING_BACKUP;
                    s_reconnect_pending = true;
                    s_last_disconnect_time = now;
                    s_reconnect_delay_ms = 150;
                } else {
                    try_connect_primary();
                }
            } else {
                s_uplink_state = UPLINK_STATE_FAILOVER_BACKOFF;
                s_reconnect_pending = true;
                s_last_disconnect_time = now;
                s_reconnect_delay_ms = 8000;
            }
        }
    }

    // 2. Failover Backoff Reconnect Handler
    if (s_reconnect_pending && !s_sta_connected) {
        unsigned long elapsed = now - s_last_disconnect_time;
        if (elapsed >= s_reconnect_delay_ms) {
            s_reconnect_pending = false;
            s_last_disconnect_time = now;

            if (s_uplink_state == UPLINK_STATE_CONNECTING_BACKUP) {
                try_connect_backup();
            } else if (s_uplink_state == UPLINK_STATE_FAILOVER_BACKOFF) {
                s_reconnect_delay_ms = (s_reconnect_delay_ms < 30000) ? (s_reconnect_delay_ms * 2) : 30000;
                pilu_log(TAG, "Backoff elapsed. Retrying dual-network connection cycle...");
                s_primary_fail_count = 0;
                try_connect_primary();
            } else {
                try_connect_primary();
            }
        }
    }

    // 3. Automated Failback Policy (While connected to Backup 'OnePlus Nord CE5')
    if (s_uplink_state == UPLINK_STATE_CONNECTED_BACKUP && s_sta_connected) {
        // Enforce 60-second cooldown on backup before inspecting primary
        if (now - s_backup_connect_time >= FAILBACK_COOLDOWN_MS && now >= s_failover_cooldown_until) {
            if (now - s_last_failback_scan >= FAILBACK_SCAN_INTERVAL_MS) {
                s_last_failback_scan = now;

                // Inspect scan results if available, or trigger an async scan
                int16_t scan_cnt = WiFi.scanComplete();
                if (scan_cnt >= 0) {
                    bool primary_found = false;
                    int primary_rssi = -100;

                    for (int i = 0; i < scan_cnt; i++) {
                        if (WiFi.SSID(i).equals(DEFAULT_STA_SSID)) {
                            primary_found = true;
                            primary_rssi = WiFi.RSSI(i);
                            break;
                        }
                    }
                    WiFi.scanDelete();

                    if (primary_found && primary_rssi >= FAILBACK_RSSI_THRESHOLD) {
                        s_primary_consecutive_seen++;
                        pilu_log("FAILBACK", "Primary 'AYUSH TECHNOLOGIES' seen in range (RSSI: %d dBm, Stability: %d/%d)",
                                 primary_rssi, s_primary_consecutive_seen, FAILBACK_CONSECUTIVE_SEEN);

                        if (s_primary_consecutive_seen >= FAILBACK_CONSECUTIVE_SEEN) {
                            pilu_log_milestone("FAILBACK INITIATED", "Primary network stable! Returning uplink to 'AYUSH TECHNOLOGIES' (Priority 1)...");
                            s_primary_consecutive_seen = 0;
                            try_connect_primary();
                        }
                    } else {
                        s_primary_consecutive_seen = 0;
                        WiFi.scanNetworks(true, true); // trigger fresh async scan
                    }
                } else if (scan_cnt != WIFI_SCAN_RUNNING) {
                    WiFi.scanNetworks(true, true);
                }
            }
        }
    }

    // 4. Periodic Internet Health Check (every 15s when connected)
    if (s_sta_connected) {
        if (now - s_last_internet_check >= 15000) {
            s_last_internet_check = now;
            uint32_t rtt = 0;
            s_internet_available = wifi_manager_check_internet(&rtt);
            if (s_internet_available) {
                s_last_latency_ms = rtt;
            }
            wifi_ap_record_t ap_info;
            if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
                s_sta_rssi = ap_info.rssi;
            }
        }
    }
}

bool wifi_manager_check_internet(uint32_t* round_trip_ms) {
    if (!s_sta_connected) return false;

    unsigned long t0 = millis();
    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) return false;

    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 400000; // 400ms non-blocking timeout for low-latency web serving
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    struct sockaddr_in target;
    memset(&target, 0, sizeof(target));
    target.sin_family = AF_INET;
    target.sin_port = htons(53); // DNS port on 1.1.1.1
    target.sin_addr.s_addr = inet_addr("1.1.1.1");

    int res = connect(sock, (struct sockaddr*)&target, sizeof(target));
    close(sock);

    if (res == 0) {
        if (round_trip_ms) *round_trip_ms = millis() - t0;
        return true;
    }
    return false;
}

bool wifi_manager_is_internet_connected() {
    return s_internet_available && s_sta_connected;
}

uint32_t wifi_manager_get_internet_latency_ms() {
    return s_last_latency_ms;
}

uint8_t wifi_manager_get_channel() {
    return s_current_channel;
}

StaUplinkState wifi_manager_get_uplink_state() {
    return s_uplink_state;
}

const char* wifi_manager_get_uplink_state_str() {
    switch (s_uplink_state) {
        case UPLINK_STATE_CONNECTED_PRIMARY:   return "CONNECTED_PRIMARY";
        case UPLINK_STATE_CONNECTED_BACKUP:    return "CONNECTED_BACKUP";
        case UPLINK_STATE_CONNECTING_PRIMARY:  return "CONNECTING_PRIMARY";
        case UPLINK_STATE_CONNECTING_BACKUP:   return "CONNECTING_BACKUP";
        case UPLINK_STATE_FAILOVER_BACKOFF:    return "FAILOVER_BACKOFF";
        case UPLINK_STATE_SCANNING_BEFORE_CONNECT: return "SCANNING";
        default: return "DISCONNECTED";
    }
}

uint8_t wifi_manager_get_active_priority() {
    if (s_uplink_state == UPLINK_STATE_CONNECTED_PRIMARY) return 1;
    if (s_uplink_state == UPLINK_STATE_CONNECTED_BACKUP) return 2;
    return 0;
}

esp_err_t wifi_manager_update_sta_credentials(const char* ssid, const char* password) {
    if (!ssid || strlen(ssid) == 0) return ESP_ERR_INVALID_ARG;

    strncpy(s_config.sta_ssid, ssid, sizeof(s_config.sta_ssid) - 1);
    s_config.sta_ssid[sizeof(s_config.sta_ssid) - 1] = '\0';

    if (password) {
        strncpy(s_config.sta_password, password, sizeof(s_config.sta_password) - 1);
        s_config.sta_password[sizeof(s_config.sta_password) - 1] = '\0';
    } else {
        s_config.sta_password[0] = '\0';
    }

    nvs_storage_set_sta_credentials(s_config.sta_ssid, s_config.sta_password);

    pilu_log(TAG, "Upstream Wi-Fi updated to '%s' (Password length=%u). Reconnecting...",
             s_config.sta_ssid, (unsigned int)strlen(s_config.sta_password));

    wifi_manager_trigger_reconnect();
    return ESP_OK;
}

esp_err_t wifi_manager_set_backup_credentials(const char* ssid, const char* password) {
    if (!ssid || strlen(ssid) == 0) return ESP_ERR_INVALID_ARG;

    WifiProfile prof;
    memset(&prof, 0, sizeof(WifiProfile));
    strncpy(prof.ssid, ssid, sizeof(prof.ssid) - 1);
    if (password) {
        strncpy(prof.password, password, sizeof(prof.password) - 1);
    }
    prof.priority = WIFI_PRIORITY_SECONDARY;
    prof.enabled = true;

    esp_err_t err = nvs_storage_add_or_update_profile(&prof);
    if (err == ESP_OK) {
        pilu_log(TAG, "Saved Backup profile '%s' (Priority 2) to NVS", ssid);
    }
    return err;
}

esp_err_t wifi_manager_switch_profile(const char* ssid) {
    if (!ssid || strlen(ssid) == 0) return ESP_ERR_INVALID_ARG;

    WifiProfile prof;
    esp_err_t err = nvs_storage_get_profile(ssid, &prof);
    if (err != ESP_OK) {
        pilu_log_error(TAG, "Cannot switch: profile '%s' not found in NVS", ssid);
        return err;
    }

    StaUplinkState target_state = (prof.priority == WIFI_PRIORITY_PRIMARY) ? UPLINK_STATE_CONNECTING_PRIMARY : UPLINK_STATE_CONNECTING_BACKUP;
    connect_to_network(prof.ssid, prof.password, target_state);
    return ESP_OK;
}

esp_err_t wifi_manager_update_ap_settings(const char* ssid, const char* password, uint8_t channel, uint8_t max_clients) {
    if (!ssid || strlen(ssid) == 0) return ESP_ERR_INVALID_ARG;

    strncpy(s_config.ap_ssid, ssid, sizeof(s_config.ap_ssid) - 1);
    s_config.ap_ssid[sizeof(s_config.ap_ssid) - 1] = '\0';

    if (password && strlen(password) >= 8) {
        strncpy(s_config.ap_password, password, sizeof(s_config.ap_password) - 1);
        s_config.ap_password[sizeof(s_config.ap_password) - 1] = '\0';
    }

    if (channel >= 1 && channel <= 13) s_config.ap_channel = channel;
    if (max_clients >= 1 && max_clients <= 10) s_config.ap_max_clients = max_clients;

    nvs_storage_set_ap_settings(s_config.ap_ssid, s_config.ap_password, s_config.ap_channel, s_config.ap_max_clients);

    WiFi.softAP(
        s_config.ap_ssid,
        strlen(s_config.ap_password) > 0 ? s_config.ap_password : nullptr,
        s_config.ap_channel,
        0,
        s_config.ap_max_clients
    );

    pilu_log(TAG, "SoftAP dynamically reconfigured: SSID='%s', Chan=%d, MaxClients=%d",
             s_config.ap_ssid, s_config.ap_channel, s_config.ap_max_clients);
    return ESP_OK;
}

esp_err_t wifi_manager_get_connected_clients(wifi_sta_list_t* client_list) {
    if (!client_list) return ESP_ERR_INVALID_ARG;
    return esp_wifi_ap_get_sta_list(client_list);
}

int16_t wifi_manager_start_scan() {
    pilu_log(TAG, "Starting non-blocking Wi-Fi scan...");
    return WiFi.scanNetworks(true, true);
}

int16_t wifi_manager_get_scan_status() {
    return WiFi.scanComplete();
}

void wifi_manager_delete_scan() {
    WiFi.scanDelete();
}

bool wifi_manager_is_sta_connected() {
    return s_sta_connected;
}

bool wifi_manager_is_ap_active() {
    return s_ap_active;
}

int8_t wifi_manager_get_sta_rssi() {
    return s_sta_rssi;
}

uint8_t wifi_manager_get_ap_client_count() {
    return s_ap_client_count;
}

void wifi_manager_get_sta_ip(char* buffer, size_t max_len) {
    if (!buffer || max_len == 0) return;
    strncpy(buffer, s_sta_ip_str, max_len - 1);
    buffer[max_len - 1] = '\0';
}

void wifi_manager_get_gateway_ip(char* buffer, size_t max_len) {
    if (!buffer || max_len == 0) return;
    strncpy(buffer, s_sta_gateway_str, max_len - 1);
    buffer[max_len - 1] = '\0';
}

void wifi_manager_get_ap_ip(char* buffer, size_t max_len) {
    if (!buffer || max_len == 0) return;
    strncpy(buffer, DEFAULT_AP_IP_STR, max_len - 1);
    buffer[max_len - 1] = '\0';
}

void wifi_manager_get_sta_ssid(char* buffer, size_t max_len) {
    if (!buffer || max_len == 0) return;
    strncpy(buffer, s_config.sta_ssid, max_len - 1);
    buffer[max_len - 1] = '\0';
}

void wifi_manager_get_ap_ssid(char* buffer, size_t max_len) {
    if (!buffer || max_len == 0) return;
    strncpy(buffer, s_config.ap_ssid, max_len - 1);
    buffer[max_len - 1] = '\0';
}

void wifi_manager_trigger_reconnect() {
    s_primary_fail_count = 0;
    try_connect_primary();
}
