#include "nat_router.h"
#include "config.h"
#include "pilu_log.h"
#include "esp_wifi.h"
#include "lwip/opt.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/tcpip.h"
#include "lwip/lwip_napt.h"
#include "dhcpserver/dhcpserver.h"
#include "dhcpserver/dhcpserver_options.h"

static const char* TAG = "NAT";

static esp_netif_t* s_ap_netif = nullptr;
static esp_netif_t* s_sta_netif = nullptr;
static uint32_t s_ap_ip_addr = 0;
static bool s_nat_active = false;

esp_err_t nat_router_init(esp_netif_t* ap_netif, esp_netif_t* sta_netif) {
    s_ap_netif = ap_netif;
    s_sta_netif = sta_netif;
    pilu_log_milestone("NAT INITIALIZATION", "lwIP NAPT subsystem linked");
    return ESP_OK;
}

esp_err_t nat_router_configure_ap_netif(const char* ip_str, const char* netmask_str, const char* gw_str) {
    if (!s_ap_netif) {
        pilu_log_error(TAG, "AP netif pointer is null");
        return ESP_ERR_INVALID_STATE;
    }

    pilu_log(TAG, "Configuring SoftAP network parameters: IP=%s Mask=%s GW=%s", ip_str, netmask_str, gw_str);

    // Stop DHCP server before updating network addressing
    esp_netif_dhcps_stop(s_ap_netif);

    esp_netif_ip_info_t ip_info;
    memset(&ip_info, 0, sizeof(ip_info));
    ip_info.ip.addr = esp_ip4addr_aton(ip_str);
    ip_info.netmask.addr = esp_ip4addr_aton(netmask_str);
    ip_info.gw.addr = esp_ip4addr_aton(gw_str);

    s_ap_ip_addr = ip_info.ip.addr;

    esp_err_t err = esp_netif_set_ip_info(s_ap_netif, &ip_info);
    if (err != ESP_OK) {
        pilu_log_error(TAG, "esp_netif_set_ip_info failed: %s", esp_err_to_name(err));
        return err;
    }

    pilu_log_milestone("AP IP ADDRESS", ip_str);

    // Enable DNS server offer option in DHCP
    dhcps_offer_t dhcps_dns_val = OFFER_DNS;
    err = esp_netif_dhcps_option(s_ap_netif, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER, &dhcps_dns_val, sizeof(dhcps_dns_val));
    if (err != ESP_OK) {
        pilu_log_warn(TAG, "esp_netif_dhcps_option returned: %s", esp_err_to_name(err));
    }

    // Configure initial fallback DNS server (8.8.8.8)
    esp_netif_dns_info_t dns_info;
    memset(&dns_info, 0, sizeof(dns_info));
    dns_info.ip.u_addr.ip4.addr = esp_ip4addr_aton(DEFAULT_PRIMARY_DNS_STR);
    dns_info.ip.type = ESP_IPADDR_TYPE_V4;
    esp_netif_set_dns_info(s_ap_netif, ESP_NETIF_DNS_MAIN, &dns_info);

    pilu_log_milestone("DNS CONFIGURATION", "Default public DNS (8.8.8.8 / 1.1.1.1) offered in DHCP");

    // Restart DHCP server
    err = esp_netif_dhcps_start(s_ap_netif);
    if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED) {
        pilu_log_error(TAG, "Failed to start AP DHCP server: %s", esp_err_to_name(err));
        return err;
    }

    pilu_log_milestone("DHCP STATUS", "AP DHCP server ACTIVE (192.168.4.2 - 192.168.4.254)");
    return ESP_OK;
}

esp_err_t nat_router_enable() {
    if (s_ap_ip_addr == 0) {
        s_ap_ip_addr = esp_ip4addr_aton(DEFAULT_AP_IP_STR);
    }

    pilu_log(TAG, "Activating lwIP NAPT on subnet IP 0x%08X (%s)...",
             (unsigned int)s_ap_ip_addr, DEFAULT_AP_IP_STR);
    
    // Call lwIP NAPT engine with TCPIP core lock
    LOCK_TCPIP_CORE();
    ip_napt_enable(s_ap_ip_addr, 1);
    UNLOCK_TCPIP_CORE();
    s_nat_active = true;

    pilu_log_milestone("NAPT ENABLED", "lwIP packet translation and forwarding is ACTIVE");
    return ESP_OK;
}

esp_err_t nat_router_disable() {
    if (s_ap_ip_addr != 0 && s_nat_active) {
        pilu_log(TAG, "Disabling lwIP NAPT...");
        LOCK_TCPIP_CORE();
        ip_napt_enable(s_ap_ip_addr, 0);
        UNLOCK_TCPIP_CORE();
        s_nat_active = false;
        pilu_log_warn(TAG, "lwIP NAPT disabled");
    }
    return ESP_OK;
}

esp_err_t nat_router_sync_dns() {
    if (!s_ap_netif || !s_sta_netif) return ESP_ERR_INVALID_STATE;

    esp_netif_dns_info_t sta_dns;
    memset(&sta_dns, 0, sizeof(sta_dns));

    esp_err_t err = esp_netif_get_dns_info(s_sta_netif, ESP_NETIF_DNS_MAIN, &sta_dns);
    if (err == ESP_OK && sta_dns.ip.u_addr.ip4.addr != 0) {
        esp_netif_set_dns_info(s_ap_netif, ESP_NETIF_DNS_MAIN, &sta_dns);
        char dns_str[16];
        snprintf(dns_str, sizeof(dns_str), IPSTR, IP2STR(&sta_dns.ip.u_addr.ip4));
        pilu_log_milestone("DNS CONFIGURATION", dns_str);
    } else {
        sta_dns.ip.u_addr.ip4.addr = esp_ip4addr_aton(DEFAULT_PRIMARY_DNS_STR);
        sta_dns.ip.type = ESP_IPADDR_TYPE_V4;
        esp_netif_set_dns_info(s_ap_netif, ESP_NETIF_DNS_MAIN, &sta_dns);
        pilu_log_milestone("DNS CONFIGURATION", DEFAULT_PRIMARY_DNS_STR);
    }

    return ESP_OK;
}

bool nat_router_is_active() {
    return s_nat_active;
}

uint32_t nat_router_get_ap_ip() {
    return s_ap_ip_addr;
}
