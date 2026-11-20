#include "system_monitor.h"
#include "wifi_manager.h"
#include "nat_router.h"
#include "config.h"
#include "pilu_log.h"
#include "esp_system.h"

static unsigned long s_last_monitor_tick = 0;

void system_monitor_init() {
    s_last_monitor_tick = millis();
    pilu_log("MONITOR", "Continuous diagnostic monitor started (interval: %d s)", SYSTEM_STATUS_INTERVAL_MS / 1000);
}

void system_monitor_process() {
    unsigned long now = millis();
    if (now - s_last_monitor_tick >= SYSTEM_STATUS_INTERVAL_MS) {
        s_last_monitor_tick = now;
        system_monitor_print_status();
    }
}

void system_monitor_print_status() {
    uint32_t free_heap = esp_get_free_heap_size();
    uint32_t min_heap = esp_get_minimum_free_heap_size();
    uint32_t uptime_s = system_monitor_get_uptime_seconds();

    char sta_ip[16] = {0};
    char sta_ssid[33] = {0};
    char ap_ssid[33] = {0};

    wifi_manager_get_sta_ip(sta_ip, sizeof(sta_ip));
    wifi_manager_get_sta_ssid(sta_ssid, sizeof(sta_ssid));
    wifi_manager_get_ap_ssid(ap_ssid, sizeof(ap_ssid));

    bool sta_conn = wifi_manager_is_sta_connected();
    int8_t rssi = wifi_manager_get_sta_rssi();
    uint8_t clients = wifi_manager_get_ap_client_count();
    bool nat_ok = nat_router_is_active();

    pilu_log("STATUS", "==================================================");
    pilu_log("STATUS", "UPTIME: %lu s | FREE HEAP: %lu B | MIN HEAP: %lu B", uptime_s, free_heap, min_heap);
    pilu_log("STATUS", "HOTSPOT: '%s' | AP IP: %s | CLIENTS: %u / %d",
             ap_ssid, DEFAULT_AP_IP_STR, clients, DEFAULT_AP_MAX_CLIENTS);

    if (sta_conn) {
        pilu_log("STATUS", "UPSTREAM: '%s' | STATE: CONNECTED | IP: %s | RSSI: %d dBm",
                 sta_ssid, sta_ip, rssi);
    } else {
        pilu_log_warn("STATUS", "UPSTREAM: '%s' | STATE: DISCONNECTED / RETRYING", sta_ssid);
    }

    pilu_log("STATUS", "NAT ROUTER: %s", nat_ok ? "ACTIVE (Internet forwarding ready)" : "STANDBY (Waiting for upstream IP)");
    pilu_log("STATUS", "==================================================");
}

uint32_t system_monitor_get_free_heap() {
    return esp_get_free_heap_size();
}

uint32_t system_monitor_get_min_free_heap() {
    return esp_get_minimum_free_heap_size();
}

uint32_t system_monitor_get_uptime_seconds() {
    return millis() / 1000;
}
