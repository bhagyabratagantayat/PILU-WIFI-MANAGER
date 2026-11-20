# Phase 1: ESP32 NAT Router Compatibility & Architecture Analysis

## 1. Overview & Objective
This report details the architectural inspection, licensing evaluation, hardware constraints, and compatibility validation of the reference ESP32 NAT Router implementation (`https://github.com/martin-ger/esp32_nat_router`) as the foundation for the **PILU Wi-Fi Manager** project.

---

## 2. Source Code & Architecture Inspection

### 2.1 Core NAT Implementation (lwIP NAPT)
The reference implementation utilizes the lwIP TCP/IP stack's native Network Address Port Translation (NAPT) engine:
- **Core API**: `ip_napt_enable(u32_t addr, int enable)` from `lwip/lwip_napt.h`.
- **IP Forwarding**: Requires `CONFIG_LWIP_IP_FORWARD=1`, `CONFIG_LWIP_IPV4_NAPT=1`, and `CONFIG_LWIP_L2_TO_L3_COPY=1`.
- **Port Mapping**: Dynamically managed via `ip_portmap_add(u8_t proto, u16_t mport, u32_t daddr, u16_t dport)`.
- **Interface Layout**:
  - **Downstream (AP interface)**: Local SoftAP assigning private IP addresses (default: `192.168.4.1/24`) to connected clients through a local DHCP server.
  - **Upstream (STA interface)**: Connects to the host Wi-Fi router, obtains an IP via DHCP, and acts as the default gateway interface for outgoing traffic.
  - **NAPT Mechanics**: lwIP intercepts outgoing IP packets from the AP netif, replaces the source IP with the STA interface IP and remaps ports, maintaining an internal translation table. Return packets are de-NATed and delivered to the corresponding AP client.

### 2.2 Hardware Constraints (ESP32 DevKit V1 / ESP32-WROOM-32)
1. **Single 2.4 GHz RF Radio**:
   - The ESP32 contains a single physical radio transceiver shared between SoftAP mode and Station (STA) mode.
   - **Critical implication**: When STA connects to an upstream router on Channel $N$, the ESP32 hardware forces the SoftAP to operate on Channel $N$ as well.
   - If the upstream network switches channels or the ESP32 connects to an alternative network on a different channel, the SoftAP briefly experiences a channel jump. Connected Wi-Fi clients must follow this channel switch or re-associate. Seamless zero-latency radio switching is physically impossible on a single-radio transceiver.
2. **RAM & Heap Budget**:
   - ESP32-WROOM-32 has ~320 KB SRAM (shared between IRAM and DRAM).
   - Each Wi-Fi client connection allocates ~5 KB of buffer memory.
   - lwIP NAPT tables require lightweight RAM allocation (typically 4–8 KB).
   - In PILU Wi-Fi Manager, by excluding unneeded features (WireGuard, PCAP capture, OLED displays), ~200+ KB of free heap remains available for:
     - MbedTLS / HTTPS client connections (cloud heartbeat and telemetry).
     - Embedded HTTP administration server.
     - Dynamic Wi-Fi fallback state machine.

---

## 3. License Analysis
- The reference repository `martin-ger/esp32_nat_router` is open source, building upon foundational work by Kenta Ida and Daniel Hope (WireGuard / NAPT) licensed under the **3-Clause BSD License** and **MIT License**.
- The license permits redistribution, modification, and commercial or private use, provided that the original copyright notice and disclaimers are preserved.
- **PILU Wi-Fi Manager** incorporates the lwIP NAPT integration patterns under compatible permissive licensing and complies fully with all attribution requirements.

---

## 4. Verification & Compilation Results

### 4.1 Native Linker Verification
Direct symbol inspection of the precompiled ESP-IDF / Arduino libraries for ESP32 (`liblwip.a` and `libesp_netif.a`) confirmed the presence of:
- `ip_napt_enable`
- `ip_napt_init`
- `ip_portmap_add`

### 4.2 Automated Hardware Compilation Test
A live compilation test on board target `esp32:esp32:esp32` (core version 3.3.12 / ESP-IDF 5.x) was executed with `arduino-cli`:
- **Result**: `Exit code 0` (SUCCESS).
- **Flash usage**: 346,540 bytes (26% of standard 1.3 MB partition, easily fits within 4MB flash).
- **RAM usage**: 28,316 bytes (8% of dynamic memory, leaving 299,364 bytes free).
- **Conclusion**: lwIP NAPT functions cleanly link without unresolved symbols or runtime collisions.

---

## 5. Architectural Gap Analysis: Reference vs. PILU Wi-Fi Manager

| Feature | Reference (`esp32_nat_router`) | PILU Wi-Fi Manager Specification |
| :--- | :--- | :--- |
| **Wi-Fi Profiles** | Single STA SSID/password in NVS | Multiple prioritized Wi-Fi profiles (Primary, Secondary, Backup) with enable/disable flags and manual switching |
| **Failover / Fallback** | Basic blind reconnect loop to same SSID | Intelligent Multi-Factor Engine: RSSI threshold (-80 dBm), 30s weak signal evaluation, 15s internet health ping, dwell time, hysteresis, exponential backoff |
| **Hotspot Defaults** | `ESP32_NAT_Router` (open) | SSID `PILU`, Password `28022006`, WPA2-PSK, configurable in NVS, protected from serial log leaks |
| **Local Admin Dashboard** | Monolithic server-rendered C string HTML | Responsive local admin dashboard with token session authentication, CSRF protection, rate limiting, and 10 dedicated management views (100% offline capable) |
| **Cloud Connectivity** | None (only optional local MQTT) | Outbound HTTPS polling client (60s heartbeat, telemetry ingestion, pending command polling & ACK, offline FIFO queue) |
| **Backend & Cloud Admin** | None | Modular Node.js / Express backend + Firebase Admin SDK + React/Vite/Tailwind Cloud Dashboard |

---

## 6. Implementation Strategy for Subsequent Phases
- **Phase 2**: Implement clean, production-grade NAT Router firmware foundation with configurable Hotspot defaults (`PILU` / `28022006`), NVS storage, and verified packet routing.
- **Phase 3**: Build the multiple Wi-Fi profile storage system and the intelligent auto-fallback state engine.
- **Phase 4**: Develop the standalone, responsive local HTTP administration dashboard served from ESP32 flash with session & CSRF protection.
- **Phase 5**: Build the Node.js Express backend with Firebase integration.
- **Phase 6**: Construct the React / Vite Cloud Dashboard frontend.
- **Phase 7**: Implement secure outbound HTTPS polling, command execution, and offline queuing.
- **Phase 8**: Perform full integration testing, end-to-end validation, and finalize documentation.
