#include "pilu_log.h"
#include <stdarg.h>
#include "esp_wifi_types.h"

static void log_internal(const char* level, const char* tag, const char* format, va_list args) {
    char message[256];
    vsnprintf(message, sizeof(message), format, args);

    unsigned long ms = millis();
    unsigned long sec = ms / 1000;
    unsigned long frac = ms % 1000;

    Serial.printf("[%6lu.%03lu] [%s] [%s] %s\r\n", sec, frac, level, tag, message);
    Serial.flush();
}

void pilu_log(const char* tag, const char* format, ...) {
    va_list args;
    va_start(args, format);
    log_internal("INFO", tag, format, args);
    va_end(args);
}

void pilu_log_warn(const char* tag, const char* format, ...) {
    va_list args;
    va_start(args, format);
    log_internal("WARN", tag, format, args);
    va_end(args);
}

void pilu_log_error(const char* tag, const char* format, ...) {
    va_list args;
    va_start(args, format);
    log_internal("ERROR", tag, format, args);
    va_end(args);
}

void pilu_log_milestone(const char* milestone, const char* details) {
    unsigned long ms = millis();
    unsigned long sec = ms / 1000;
    unsigned long frac = ms % 1000;

    if (details && strlen(details) > 0) {
        Serial.printf("[%6lu.%03lu] [MILESTONE] >>> %-28s | %s\r\n", sec, frac, milestone, details);
    } else {
        Serial.printf("[%6lu.%03lu] [MILESTONE] >>> %s\r\n", sec, frac, milestone);
    }
    Serial.flush();
}

const char* pilu_reset_reason_to_str(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_UNKNOWN:   return "UNKNOWN";
        case ESP_RST_POWERON:   return "POWERON (Normal Power-on or 5V USB plug-in)";
        case ESP_RST_EXT:       return "EXTERNAL_PIN (Reset pin triggered)";
        case ESP_RST_SW:        return "SW_RESET (Software restart via esp_restart)";
        case ESP_RST_PANIC:     return "PANIC (Guru Meditation / Exception)";
        case ESP_RST_INT_WDT:   return "INT_WDT (Interrupt Watchdog Timeout)";
        case ESP_RST_TASK_WDT:  return "TASK_WDT (Task Watchdog Timeout)";
        case ESP_RST_WDT:       return "OTHER_WDT (Other Watchdog)";
        case ESP_RST_DEEPSLEEP: return "DEEP_SLEEP (Wakeup from deep sleep)";
        case ESP_RST_BROWNOUT:  return "BROWNOUT (Unstable supply voltage dropped below threshold)";
        case ESP_RST_SDIO:      return "SDIO (Reset over SDIO)";
        default:                return "UNRECOGNIZED";
    }
}

const char* pilu_wifi_reason_to_str(uint8_t reason) {
    switch (reason) {
        case 1:   return "UNSPECIFIED (1)";
        case 2:   return "AUTH_EXPIRE (2)";
        case 3:   return "AUTH_LEAVE (3)";
        case 4:   return "ASSOC_EXPIRE (4)";
        case 5:   return "ASSOC_TOOMANY (5)";
        case 6:   return "NOT_AUTHED (6)";
        case 7:   return "NOT_ASSOCED (7)";
        case 8:   return "ASSOC_LEAVE (8)";
        case 9:   return "ASSOC_NOT_AUTHED (9)";
        case 10:  return "DISASSOC_PWRCAP_BAD (10)";
        case 11:  return "DISASSOC_SUPCHAN_BAD (11)";
        case 12:  return "BSS_TRANSITION_DISASSOC (12)";
        case 13:  return "IE_INVALID (13)";
        case 14:  return "MIC_FAILURE (14)";
        case 15:  return "4WAY_HANDSHAKE_TIMEOUT (15)";
        case 16:  return "GROUP_KEY_UPDATE_TIMEOUT (16)";
        case 17:  return "IE_IN_4WAY_DIFFERS (17)";
        case 18:  return "GROUP_CIPHER_INVALID (18)";
        case 19:  return "PAIRWISE_CIPHER_INVALID (19)";
        case 20:  return "AKMP_INVALID (20)";
        case 21:  return "UNSUPP_RSN_IE_VERSION (21)";
        case 22:  return "INVALID_RSN_IE_CAP (22)";
        case 23:  return "802_1X_AUTH_FAILED (23)";
        case 24:  return "CIPHER_SUITE_REJECTED (24)";
        case 200: return "BEACON_TIMEOUT (200)";
        case 201: return "NO_AP_FOUND (201 - SSID not in range or wrong name)";
        case 202: return "AUTH_FAIL (202 - WRONG PASSWORD or security mismatch)";
        case 203: return "ASSOC_FAIL (203 - Association rejected by AP)";
        case 204: return "HANDSHAKE_TIMEOUT (204 - WPA handshake failed/timeout)";
        case 205: return "CONNECTION_FAIL (205 - Connection could not be established)";
        case 206: return "AP_TSF_RESET (206)";
        case 207: return "ROAMING (207)";
        default:  return "UNKNOWN_REASON";
    }
}
