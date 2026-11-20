#include "web_server.h"
#include "dashboard_html.h"
#include "nvs_storage.h"
#include "wifi_manager.h"
#include "config.h"
#include "pilu_log.h"
#include <WebServer.h>
#include <WiFi.h>

static const char* TAG = "WEB";

// Derived WebServer class to inspect underlying NetworkServer socket status
class PiluWebServer : public WebServer {
public:
    PiluWebServer(int port = 80) : WebServer(port) {}
    bool isListening() {
        return (bool)_server;
    }
};

static PiluWebServer s_server(80);

// Authentication & Session state
static char s_active_session_token[33] = {0};
static unsigned long s_session_expire_time = 0;

// Rate limiting state
static uint8_t s_failed_login_count = 0;
static unsigned long s_lockout_until = 0;

// Delayed restart state
static bool s_restart_pending = false;
static unsigned long s_restart_timestamp = 0;

// Diagnostic logging helpers for HTTP request lifecycle
static void log_request_begin(const char* handler_name) {
    IPAddress client_ip = s_server.client().remoteIP();
    char req_info[64];
    snprintf(req_info, sizeof(req_info), "%s -> %s", client_ip.toString().c_str(), s_server.uri().c_str());
    pilu_log_milestone("HTTP REQUEST RECEIVED", req_info);

    const char* method_str = "UNKNOWN";
    HTTPMethod m = s_server.method();
    if (m == HTTP_GET) method_str = "GET";
    else if (m == HTTP_POST) method_str = "POST";
    else if (m == HTTP_DELETE) method_str = "DELETE";
    else if (m == HTTP_PUT) method_str = "PUT";
    else if (m == HTTP_OPTIONS) method_str = "OPTIONS";

    pilu_log("HTTP", "REQUEST METHOD: %s", method_str);
    pilu_log("HTTP", "REQUEST PATH: %s", s_server.uri().c_str());
}

static void log_request_finish(int status_code, unsigned long t0) {
    char code_str[16];
    snprintf(code_str, sizeof(code_str), "%d", status_code);
    pilu_log_milestone("HTTP RESPONSE CODE", code_str);
    pilu_log("HTTP", "REQUEST HANDLER COMPLETED: %s (Duration: %lu ms)", s_server.uri().c_str(), millis() - t0);
}

// Minimal JSON String parser helpers
static bool json_extract_string(const String& body, const char* key, char* output, size_t max_len) {
    if (!output || max_len == 0) return false;
    output[0] = '\0';

    String search_key = "\"" + String(key) + "\"";
    int key_pos = body.indexOf(search_key);
    if (key_pos < 0) return false;

    int colon_pos = body.indexOf(':', key_pos + search_key.length());
    if (colon_pos < 0) return false;

    int quote_start = body.indexOf('\"', colon_pos + 1);
    if (quote_start < 0) return false;

    int quote_end = body.indexOf('\"', quote_start + 1);
    if (quote_end < 0) return false;

    String val = body.substring(quote_start + 1, quote_end);
    strncpy(output, val.c_str(), max_len - 1);
    output[max_len - 1] = '\0';
    return true;
}

static bool json_extract_int(const String& body, const char* key, int* output) {
    if (!output) return false;

    String search_key = "\"" + String(key) + "\"";
    int key_pos = body.indexOf(search_key);
    if (key_pos < 0) return false;

    int colon_pos = body.indexOf(':', key_pos + search_key.length());
    if (colon_pos < 0) return false;

    int val_start = colon_pos + 1;
    while (val_start < (int)body.length() && (body[val_start] == ' ' || body[val_start] == '\t')) {
        val_start++;
    }

    int val_end = val_start;
    while (val_end < (int)body.length() && (isdigit(body[val_end]) || body[val_end] == '-')) {
        val_end++;
    }

    if (val_end > val_start) {
        String num_str = body.substring(val_start, val_end);
        *output = num_str.toInt();
        return true;
    }
    return false;
}

static void handle_options() {
    s_server.sendHeader("Access-Control-Allow-Origin", "*");
    s_server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    s_server.sendHeader("Access-Control-Allow-Headers", "Content-Type, Authorization, X-Requested-With, X-Auth-Token");
    s_server.send(204);
}

static void send_json_response(int code, const char* json_str, unsigned long t0) {
    s_server.sendHeader("Access-Control-Allow-Origin", "*");
    s_server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    s_server.sendHeader("Access-Control-Allow-Headers", "Content-Type, Authorization, X-Requested-With, X-Auth-Token");
    s_server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
    s_server.send(code, "application/json", json_str);
    log_request_finish(code, t0);
}

static bool is_authenticated() {
    if (strlen(s_active_session_token) == 0) return false;
    if (millis() > s_session_expire_time) {
        s_active_session_token[0] = '\0';
        return false;
    }

    // Check header
    if (s_server.hasHeader("X-Auth-Token")) {
        String header_token = s_server.header("X-Auth-Token");
        if (header_token.equals(s_active_session_token)) {
            s_session_expire_time = millis() + SESSION_TIMEOUT_MS;
            return true;
        }
    }

    // Check cookie
    if (s_server.hasHeader("Cookie")) {
        String cookie = s_server.header("Cookie");
        int pos = cookie.indexOf("PiluSession=");
        if (pos >= 0) {
            String token = cookie.substring(pos + 12);
            int end = token.indexOf(';');
            if (end >= 0) token = token.substring(0, end);
            token.trim();
            if (token.equals(s_active_session_token)) {
                s_session_expire_time = millis() + SESSION_TIMEOUT_MS;
                return true;
            }
        }
    }

    return false;
}

// ---------------- Route Handlers ----------------

/**
 * @brief Streams the Single Page Application HTML in 1400-byte segments.
 * Eliminates TCP send buffer overflow (CONFIG_LWIP_TCP_SND_BUF_DEFAULT = 5744 bytes).
 */
static void handle_root() {
    unsigned long t0 = millis();
    log_request_begin("handle_root");

    s_server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    s_server.sendHeader("Pragma", "no-cache");
    s_server.sendHeader("Expires", "0");

    size_t total_len = strlen_P(INDEX_HTML);
    s_server.setContentLength(total_len);
    s_server.send(200, "text/html", "");

    size_t offset = 0;
    const size_t CHUNK_SIZE = 1400; // Optimal chunk size matching lwIP TCP MSS (1436 bytes)
    while (offset < total_len) {
        size_t to_send = (total_len - offset > CHUNK_SIZE) ? CHUNK_SIZE : (total_len - offset);
        s_server.sendContent_P(INDEX_HTML + offset, to_send);
        offset += to_send;
        delay(1); // Yield for lwIP TCP thread to flush packet over RF and receive ACK
    }

    log_request_finish(200, t0);
}

/**
 * @brief Handles Android captive portal detection and unknown path redirects.
 */
static void handle_not_found() {
    unsigned long t0 = millis();
    log_request_begin("handle_not_found");

    String uri = s_server.uri();

    // Android & Apple captive portal check endpoints
    if (uri.indexOf("generate_204") >= 0 || uri.indexOf("gen_204") >= 0) {
        s_server.send(204, "text/plain", "");
        log_request_finish(204, t0);
        return;
    }

    if (uri.endsWith(".ico")) {
        s_server.send(204, "image/x-icon", "");
        log_request_finish(204, t0);
        return;
    }

    // Redirect any unexpected browser path to the root dashboard
    s_server.sendHeader("Location", "http://192.168.4.1/");
    s_server.send(302, "text/plain", "Redirecting to PILU Dashboard...");
    log_request_finish(302, t0);
}

static void handle_auth_login() {
    unsigned long t0 = millis();
    log_request_begin("handle_auth_login");

    if (millis() < s_lockout_until) {
        unsigned long remaining = (s_lockout_until - millis()) / 1000;
        char resp[96];
        snprintf(resp, sizeof(resp), "{\"success\":false,\"message\":\"Too many failed attempts. Locked out for %lu seconds.\"}", remaining);
        send_json_response(429, resp, t0);
        return;
    }

    String body = s_server.arg("plain");
    char pass[65] = {0};
    if (!json_extract_string(body, "password", pass, sizeof(pass))) {
        send_json_response(400, "{\"success\":false,\"message\":\"Password parameter missing\"}", t0);
        return;
    }

    char stored_admin_pass[65] = {0};
    nvs_storage_get_admin_password(stored_admin_pass, sizeof(stored_admin_pass));

    if (strcmp(pass, stored_admin_pass) == 0) {
        s_failed_login_count = 0;
        uint32_t r1 = esp_random();
        uint32_t r2 = esp_random();
        snprintf(s_active_session_token, sizeof(s_active_session_token), "%08x%08x%08x%08x",
                 (unsigned int)r1, (unsigned int)r2, (unsigned int)(r1 ^ r2), (unsigned int)(r1 + r2));
        s_session_expire_time = millis() + SESSION_TIMEOUT_MS;

        String cookie_header = "PiluSession=" + String(s_active_session_token) + "; Path=/; HttpOnly; SameSite=Lax";
        s_server.sendHeader("Set-Cookie", cookie_header);

        char resp[128];
        snprintf(resp, sizeof(resp), "{\"success\":true,\"token\":\"%s\"}", s_active_session_token);
        send_json_response(200, resp, t0);
        pilu_log(TAG, "Admin authenticated successfully from client %s", s_server.client().remoteIP().toString().c_str());
    } else {
        s_failed_login_count++;
        pilu_log_warn(TAG, "Failed login attempt (%d/5) from %s", s_failed_login_count, s_server.client().remoteIP().toString().c_str());
        if (s_failed_login_count >= 5) {
            s_lockout_until = millis() + 60000; // 60s lockout
            s_failed_login_count = 0;
            send_json_response(429, "{\"success\":false,\"message\":\"Rate limit exceeded. Locked out for 60 seconds.\"}", t0);
        } else {
            send_json_response(401, "{\"success\":false,\"message\":\"Invalid admin password\"}", t0);
        }
    }
}

static void handle_auth_logout() {
    unsigned long t0 = millis();
    log_request_begin("handle_auth_logout");
    s_active_session_token[0] = '\0';
    s_session_expire_time = 0;
    s_server.sendHeader("Set-Cookie", "PiluSession=; Path=/; Expires=Thu, 01 Jan 1970 00:00:00 GMT");
    send_json_response(200, "{\"success\":true,\"message\":\"Logged out\"}", t0);
}

static void handle_auth_check() {
    unsigned long t0 = millis();
    log_request_begin("handle_auth_check");
    bool auth = is_authenticated();
    char resp[64];
    snprintf(resp, sizeof(resp), "{\"authenticated\":%s}", auth ? "true" : "false");
    send_json_response(200, resp, t0);
}

static void handle_status() {
    unsigned long t0 = millis();
    log_request_begin("handle_status");

    if (!is_authenticated()) {
        send_json_response(401, "{\"error\":\"Unauthorized\"}", t0);
        return;
    }

    char sta_ip[16] = "0.0.0.0";
    char sta_gw[16] = "0.0.0.0";
    char sta_ssid[33] = "";
    char ap_ssid[33] = "";

    wifi_manager_get_sta_ip(sta_ip, sizeof(sta_ip));
    wifi_manager_get_gateway_ip(sta_gw, sizeof(sta_gw));
    wifi_manager_get_sta_ssid(sta_ssid, sizeof(sta_ssid));
    wifi_manager_get_ap_ssid(ap_ssid, sizeof(ap_ssid));

    bool sta_conn = wifi_manager_is_sta_connected();
    int8_t rssi = wifi_manager_get_sta_rssi();
    uint8_t chan = wifi_manager_get_channel();
    uint8_t clients = wifi_manager_get_ap_client_count();
    bool net_ok = wifi_manager_is_internet_connected();
    uint32_t rtt = wifi_manager_get_internet_latency_ms();
    uint32_t heap = esp_get_free_heap_size();
    unsigned long uptime = millis() / 1000;
    uint8_t uplink_priority = wifi_manager_get_active_priority();
    const char* uplink_state = wifi_manager_get_uplink_state_str();

    char resp[512];
    snprintf(resp, sizeof(resp),
        "{\"sta_connected\":%s,\"sta_ssid\":\"%s\",\"sta_rssi\":%d,\"sta_ip\":\"%s\",\"sta_gw\":\"%s\","
        "\"sta_channel\":%u,\"ap_ssid\":\"%s\",\"ap_clients\":%u,\"ap_channel\":1,\"ap_max_clients\":8,"
        "\"internet_online\":%s,\"internet_rtt_ms\":%u,\"free_heap\":%u,\"uptime_sec\":%lu,\"version\":\"%s\","
        "\"uplink_priority\":%u,\"uplink_state\":\"%s\",\"primary_ssid\":\"%s\",\"backup_ssid\":\"%s\"}",
        sta_conn ? "true" : "false", sta_ssid, (int)rssi, sta_ip, sta_gw,
        (unsigned int)chan, ap_ssid, (unsigned int)clients,
        net_ok ? "true" : "false", (unsigned int)rtt, (unsigned int)heap, uptime, PILU_FIRMWARE_VERSION,
        (unsigned int)uplink_priority, uplink_state, DEFAULT_STA_SSID, DEFAULT_BACKUP_STA_SSID);

    send_json_response(200, resp, t0);
}

static void handle_profiles_get() {
    unsigned long t0 = millis();
    log_request_begin("handle_profiles_get");

    if (!is_authenticated()) {
        send_json_response(401, "{\"error\":\"Unauthorized\"}", t0);
        return;
    }

    WifiProfile profiles[MAX_WIFI_PROFILES];
    size_t count = 0;
    nvs_storage_load_profiles(profiles, MAX_WIFI_PROFILES, &count);

    char cur_ssid[33] = "";
    wifi_manager_get_sta_ssid(cur_ssid, sizeof(cur_ssid));

    String json = "{\"current_ssid\":\"" + String(cur_ssid) + "\",\"profiles\":[";
    for (size_t i = 0; i < count; i++) {
        if (i > 0) json += ",";
        json += "{\"ssid\":\"" + String(profiles[i].ssid) + "\",";
        json += "\"priority\":" + String(profiles[i].priority) + ",";
        json += "\"enabled\":" + String(profiles[i].enabled ? "true" : "false") + "}";
    }
    json += "]}";

    send_json_response(200, json.c_str(), t0);
}

static void handle_profiles_post() {
    unsigned long t0 = millis();
    log_request_begin("handle_profiles_post");

    if (!is_authenticated()) {
        send_json_response(401, "{\"error\":\"Unauthorized\"}", t0);
        return;
    }

    String body = s_server.arg("plain");
    WifiProfile prof;
    memset(&prof, 0, sizeof(WifiProfile));

    if (!json_extract_string(body, "ssid", prof.ssid, sizeof(prof.ssid)) || strlen(prof.ssid) == 0) {
        send_json_response(400, "{\"success\":false,\"message\":\"SSID is required\"}", t0);
        return;
    }

    json_extract_string(body, "password", prof.password, sizeof(prof.password));

    int prio = 0;
    if (json_extract_int(body, "priority", &prio)) {
        prof.priority = (uint8_t)prio;
    } else {
        prof.priority = WIFI_PRIORITY_SECONDARY;
    }
    prof.enabled = true;

    esp_err_t err = nvs_storage_add_or_update_profile(&prof);
    if (err == ESP_OK) {
        send_json_response(200, "{\"success\":true,\"message\":\"Profile saved\"}", t0);
    } else {
        send_json_response(500, "{\"success\":false,\"message\":\"Failed to save profile to NVS\"}", t0);
    }
}

static void handle_profiles_delete() {
    unsigned long t0 = millis();
    log_request_begin("handle_profiles_delete");

    if (!is_authenticated()) {
        send_json_response(401, "{\"error\":\"Unauthorized\"}", t0);
        return;
    }

    String body = s_server.arg("plain");
    char ssid[33] = {0};
    if (!json_extract_string(body, "ssid", ssid, sizeof(ssid))) {
        send_json_response(400, "{\"success\":false,\"message\":\"SSID required\"}", t0);
        return;
    }

    esp_err_t err = nvs_storage_delete_profile(ssid);
    if (err == ESP_OK) {
        send_json_response(200, "{\"success\":true,\"message\":\"Profile deleted\"}", t0);
    } else {
        send_json_response(500, "{\"success\":false,\"message\":\"Failed to delete profile\"}", t0);
    }
}

static void handle_profiles_switch() {
    unsigned long t0 = millis();
    log_request_begin("handle_profiles_switch");

    if (!is_authenticated()) {
        send_json_response(401, "{\"error\":\"Unauthorized\"}", t0);
        return;
    }

    String body = s_server.arg("plain");
    char ssid[33] = {0};
    if (!json_extract_string(body, "ssid", ssid, sizeof(ssid))) {
        send_json_response(400, "{\"success\":false,\"message\":\"SSID required\"}", t0);
        return;
    }

    esp_err_t err = wifi_manager_switch_profile(ssid);
    if (err == ESP_OK) {
        send_json_response(200, "{\"success\":true,\"message\":\"Connecting to requested profile\"}", t0);
    } else {
        send_json_response(500, "{\"success\":false,\"message\":\"Profile switch failed\"}", t0);
    }
}

static void handle_profiles_priority() {
    unsigned long t0 = millis();
    log_request_begin("handle_profiles_priority");

    if (!is_authenticated()) {
        send_json_response(401, "{\"error\":\"Unauthorized\"}", t0);
        return;
    }

    String body = s_server.arg("plain");
    char ssid[33] = {0};
    int prio = 0;

    if (!json_extract_string(body, "ssid", ssid, sizeof(ssid)) || !json_extract_int(body, "priority", &prio)) {
        send_json_response(400, "{\"success\":false,\"message\":\"SSID and priority required\"}", t0);
        return;
    }

    WifiProfile prof;
    if (nvs_storage_get_profile(ssid, &prof) == ESP_OK) {
        prof.priority = (uint8_t)prio;
        nvs_storage_add_or_update_profile(&prof);
        send_json_response(200, "{\"success\":true,\"message\":\"Priority updated\"}", t0);
    } else {
        send_json_response(404, "{\"success\":false,\"message\":\"Profile not found\"}", t0);
    }
}

static void handle_scan() {
    unsigned long t0 = millis();
    log_request_begin("handle_scan");

    if (!is_authenticated()) {
        send_json_response(401, "{\"error\":\"Unauthorized\"}", t0);
        return;
    }

    if (s_server.method() == HTTP_POST) {
        wifi_manager_start_scan();
        send_json_response(200, "{\"status\":\"started\"}", t0);
        return;
    }

    int16_t status = wifi_manager_get_scan_status();
    if (status == WIFI_SCAN_RUNNING) {
        send_json_response(200, "{\"status\":\"running\"}", t0);
        return;
    } else if (status < 0) {
        send_json_response(200, "{\"status\":\"idle\",\"networks\":[]}", t0);
        return;
    }

    String json = "{\"status\":\"complete\",\"networks\":[";
    for (int i = 0; i < status; i++) {
        if (i > 0) json += ",";
        json += "{\"ssid\":\"" + WiFi.SSID(i) + "\",";
        json += "\"rssi\":" + String(WiFi.RSSI(i)) + ",";
        json += "\"channel\":" + String((int)WiFi.channel(i)) + ",";
        json += "\"secure\":" + String(WiFi.encryptionType(i) != WIFI_AUTH_OPEN ? "true" : "false") + "}";
    }
    json += "]}";

    send_json_response(200, json.c_str(), t0);
    wifi_manager_delete_scan();
}

static void handle_hotspot() {
    unsigned long t0 = millis();
    log_request_begin("handle_hotspot");

    if (!is_authenticated()) {
        send_json_response(401, "{\"error\":\"Unauthorized\"}", t0);
        return;
    }

    String body = s_server.arg("plain");
    char ssid[33] = {0};
    char pass[65] = {0};
    int chan = 1;
    int maxc = 8;

    if (!json_extract_string(body, "ssid", ssid, sizeof(ssid)) || strlen(ssid) == 0) {
        send_json_response(400, "{\"success\":false,\"message\":\"Hotspot SSID required\"}", t0);
        return;
    }

    json_extract_string(body, "password", pass, sizeof(pass));
    json_extract_int(body, "channel", &chan);
    json_extract_int(body, "max_clients", &maxc);

    esp_err_t err = wifi_manager_update_ap_settings(ssid, pass, (uint8_t)chan, (uint8_t)maxc);
    if (err == ESP_OK) {
        send_json_response(200, "{\"success\":true,\"message\":\"Hotspot updated and applied\"}", t0);
    } else {
        send_json_response(500, "{\"success\":false,\"message\":\"Failed to update hotspot\"}", t0);
    }
}

static void handle_clients() {
    unsigned long t0 = millis();
    log_request_begin("handle_clients");

    if (!is_authenticated()) {
        send_json_response(401, "{\"error\":\"Unauthorized\"}", t0);
        return;
    }

    wifi_sta_list_t sta_list;
    memset(&sta_list, 0, sizeof(sta_list));
    wifi_manager_get_connected_clients(&sta_list);

    String json = "{\"count\":" + String(sta_list.num) + ",\"clients\":[";
    for (int i = 0; i < sta_list.num; i++) {
        if (i > 0) json += ",";
        char mac_str[18];
        snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
                 sta_list.sta[i].mac[0], sta_list.sta[i].mac[1], sta_list.sta[i].mac[2],
                 sta_list.sta[i].mac[3], sta_list.sta[i].mac[4], sta_list.sta[i].mac[5]);
        json += "{\"mac\":\"" + String(mac_str) + "\"}";
    }
    json += "]}";

    send_json_response(200, json.c_str(), t0);
}

static void handle_admin_password() {
    unsigned long t0 = millis();
    log_request_begin("handle_admin_password");

    if (!is_authenticated()) {
        send_json_response(401, "{\"error\":\"Unauthorized\"}", t0);
        return;
    }

    String body = s_server.arg("plain");
    char cur_pass[65] = {0};
    char new_pass[65] = {0};

    if (!json_extract_string(body, "current_password", cur_pass, sizeof(cur_pass)) ||
        !json_extract_string(body, "new_password", new_pass, sizeof(new_pass))) {
        send_json_response(400, "{\"success\":false,\"message\":\"Current and new password required\"}", t0);
        return;
    }

    char stored_pass[65] = {0};
    nvs_storage_get_admin_password(stored_pass, sizeof(stored_pass));

    if (strcmp(cur_pass, stored_pass) != 0) {
        send_json_response(401, "{\"success\":false,\"message\":\"Current password incorrect\"}", t0);
        return;
    }

    if (strlen(new_pass) < 6) {
        send_json_response(400, "{\"success\":false,\"message\":\"New password must be at least 6 characters\"}", t0);
        return;
    }

    esp_err_t err = nvs_storage_set_admin_password(new_pass);
    if (err == ESP_OK) {
        send_json_response(200, "{\"success\":true,\"message\":\"Admin password updated successfully\"}", t0);
    } else {
        send_json_response(500, "{\"success\":false,\"message\":\"Failed to save admin password in NVS\"}", t0);
    }
}

static void handle_restart() {
    unsigned long t0 = millis();
    log_request_begin("handle_restart");

    if (!is_authenticated()) {
        send_json_response(401, "{\"error\":\"Unauthorized\"}", t0);
        return;
    }

    send_json_response(200, "{\"success\":true,\"message\":\"Restarting ESP32...\"}", t0);
    pilu_log(TAG, "Reboot triggered via Web Admin API");
    s_restart_pending = true;
    s_restart_timestamp = millis() + 800;
}

// ---------------- Initialization & Loop ----------------

esp_err_t web_server_init() {
    pilu_log_milestone("WEB SERVER INITIALIZING", "Binding port 80 and registering API routes");

    // Collect specific headers for session authentication
    const char* header_keys[] = {"Cookie", "X-Auth-Token", "User-Agent"};
    size_t header_keys_size = sizeof(header_keys) / sizeof(char*);
    s_server.collectHeaders(header_keys, header_keys_size);

    // Front-end SPA routes (all mapped to chunked HTML handler)
    s_server.on("/", HTTP_GET, handle_root);
    s_server.on("/index.html", HTTP_GET, handle_root);
    s_server.on("/setup", HTTP_GET, handle_root);
    s_server.on("/config", HTTP_GET, handle_root);
    s_server.on("/login", HTTP_GET, handle_root);

    // Captive portal & 404 handler
    s_server.onNotFound(handle_not_found);

    // Auth API
    s_server.on("/api/auth/login", HTTP_POST, handle_auth_login);
    s_server.on("/api/auth/logout", HTTP_POST, handle_auth_logout);
    s_server.on("/api/auth/check", HTTP_GET, handle_auth_check);

    // Status API
    s_server.on("/api/status", HTTP_GET, handle_status);

    // Profiles API
    s_server.on("/api/profiles", HTTP_GET, handle_profiles_get);
    s_server.on("/api/profiles", HTTP_POST, handle_profiles_post);
    s_server.on("/api/profiles/delete", HTTP_POST, handle_profiles_delete);
    s_server.on("/api/profiles/switch", HTTP_POST, handle_profiles_switch);
    s_server.on("/api/profiles/priority", HTTP_POST, handle_profiles_priority);

    // Scanner API
    s_server.on("/api/scan", HTTP_GET, handle_scan);
    s_server.on("/api/scan", HTTP_POST, handle_scan);

    // Hotspot API
    s_server.on("/api/hotspot", HTTP_POST, handle_hotspot);

    // Clients API
    s_server.on("/api/clients", HTTP_GET, handle_clients);

    // System API
    s_server.on("/api/admin/password", HTTP_POST, handle_admin_password);
    s_server.on("/api/system/restart", HTTP_POST, handle_restart);

    // CORS preflight OPTIONS handlers (Firebase Hosting compatibility)
    s_server.on("/api/auth/login", HTTP_OPTIONS, handle_options);
    s_server.on("/api/auth/logout", HTTP_OPTIONS, handle_options);
    s_server.on("/api/auth/check", HTTP_OPTIONS, handle_options);
    s_server.on("/api/status", HTTP_OPTIONS, handle_options);
    s_server.on("/api/profiles", HTTP_OPTIONS, handle_options);
    s_server.on("/api/profiles/priority", HTTP_OPTIONS, handle_options);
    s_server.on("/api/profiles/switch", HTTP_OPTIONS, handle_options);
    s_server.on("/api/profiles/delete", HTTP_OPTIONS, handle_options);
    s_server.on("/api/scan", HTTP_OPTIONS, handle_options);
    s_server.on("/api/hotspot", HTTP_OPTIONS, handle_options);
    s_server.on("/api/clients", HTTP_OPTIONS, handle_options);
    s_server.on("/api/admin/password", HTTP_OPTIONS, handle_options);
    s_server.on("/api/system/restart", HTTP_OPTIONS, handle_options);

    s_server.begin();

    if (s_server.isListening()) {
        pilu_log_milestone("WEB SERVER STARTED", "Listening on port 80 (Dual AP+STA active)");
        pilu_log_milestone("WEB SERVER IP", "http://192.168.4.1 (Access via PILU Hotspot)");
    } else {
        pilu_log_error(TAG, "WebServer failed to bind on port 80!");
        return ESP_FAIL;
    }

    return ESP_OK;
}

void web_server_process() {
    s_server.handleClient();

    if (s_restart_pending && millis() >= s_restart_timestamp) {
        pilu_log("SYSTEM", "Executing scheduled restart now...");
        delay(100);
        esp_restart();
    }
}
