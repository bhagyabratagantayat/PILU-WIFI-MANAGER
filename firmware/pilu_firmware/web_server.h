#ifndef PILU_WEB_SERVER_H
#define PILU_WEB_SERVER_H

#include <Arduino.h>
#include "esp_err.h"

/**
 * @file web_server.h
 * @brief Embedded Web Server for the PILU Local Administration Dashboard.
 * 
 * Provides:
 *  - Responsive Single Page Application served from PROGMEM
 *  - REST API for status, Wi-Fi scanning, multi-profile management, and SoftAP control
 *  - Token-based session authentication and brute-force rate limiting
 */

esp_err_t web_server_init();
void web_server_process();

#endif // PILU_WEB_SERVER_H
