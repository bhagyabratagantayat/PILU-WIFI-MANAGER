#ifndef PILU_NAT_ROUTER_H
#define PILU_NAT_ROUTER_H

#include <Arduino.h>
#include "esp_err.h"
#include "esp_netif.h"
#include "lwip/ip_addr.h"

/**
 * @file nat_router.h
 * @brief lwIP NAPT initialization, AP/STA routing configuration, and DNS distribution.
 */

esp_err_t nat_router_init(esp_netif_t* ap_netif, esp_netif_t* sta_netif);
esp_err_t nat_router_configure_ap_netif(const char* ip_str, const char* netmask_str, const char* gw_str);
esp_err_t nat_router_enable();
esp_err_t nat_router_disable();
esp_err_t nat_router_sync_dns();
bool nat_router_is_active();
uint32_t nat_router_get_ap_ip();

#endif // PILU_NAT_ROUTER_H
