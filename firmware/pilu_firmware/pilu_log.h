#ifndef PILU_LOG_H
#define PILU_LOG_H

#include <Arduino.h>
#include "esp_system.h"

/**
 * @file pilu_log.h
 * @brief High-reliability timestamped diagnostic logger for PILU Wi-Fi Manager.
 * 
 * Guaranteed output via hardware UART Serial with immediate flush.
 * Automatically redacts passwords and translates error codes.
 */

// Formatted timestamped logging
void pilu_log(const char* tag, const char* format, ...);

// Highlighted milestone logging
void pilu_log_milestone(const char* milestone, const char* details);

// Error and warning logging
void pilu_log_warn(const char* tag, const char* format, ...);
void pilu_log_error(const char* tag, const char* format, ...);

// Human-readable code decoders
const char* pilu_wifi_reason_to_str(uint8_t reason);
const char* pilu_reset_reason_to_str(esp_reset_reason_t reason);

#endif // PILU_LOG_H
