# PILU Wi-Fi Manager (Smart ESP32 Wi-Fi Repeater & Router)

<p align="center">
  <strong>High-Performance Dual-Uplink Wi-Fi Repeater with Native lwIP NAPT & Local Web Dashboard</strong><br>
  Built for ESP32 DevKit V1 (ESP32-WROOM-32)
</p>

---

## Overview

**PILU Wi-Fi Manager** transforms an ESP32 into a dual-network Wi-Fi range extender and intelligent NAT router. Operating simultaneously in Access Point (`SoftAP`) and Station (`STA`) modes, it forwards internet packets using native **lwIP NAPT (Network Address Port Translation)** while hosting a local administration dashboard.

---

## Key Features

- **Dual-Uplink Priority State Machine**:
  - **Priority 1 (Primary)**: Automatically associates with your main Wi-Fi network.
  - **Priority 2 (Backup)**: Seamlessly fails over to a secondary hotspot (e.g. mobile hotspot) if the primary network drops.
  - **Anti-Flapping Failback Hysteresis**: Evaluates primary network recovery using configurable RSSI threshold ($\ge -82\text{ dBm}$) and consecutive-scan stability checks before switching back.
- **Native lwIP NAPT Packet Forwarding**: High-throughput layer-3 packet routing with thread-safe TCPIP core locking.
- **Embedded Local Admin Dashboard**:
  - Hosted directly on the ESP32 at `http://192.168.4.1`.
  - Zero external dependencies (no CDNs, no cloud connection required).
  - 1400-byte chunked streaming avoids lwIP TCP socket buffer exhaustion.
  - Mobile-first responsive UI with real-time RSSI, IP, client count, heap, and uptime monitoring.
- **Interactive Serial CLI**:
  - Full management via USB UART at 115,200 baud (`help`, `status`, `scan`, `set_wifi`, `set_backup`, `restart`).
- **NVS Persistent Storage**: Credentials and Wi-Fi profiles are stored in ESP32 Non-Volatile Storage across reboots.
- **Enterprise Security**: Token-based admin authentication with brute-force rate-limiting and secret masking in logs and APIs.

---

## Hardware Specifications

| Component | Specification |
| :--- | :--- |
| **Microcontroller** | ESP32 DevKit V1 (ESP32-D0WD-V3, Dual-Core 240 MHz) |
| **Flash Memory** | 4 MB SPI Flash (DIO, 80 MHz) |
| **Wi-Fi** | 802.11 b/g/n (2.4 GHz) |
| **Power Supply** | 5V DC via Micro-USB |

---

## Default Network & Dashboard Parameters

| Parameter | Default Value | Notes |
| :--- | :--- | :--- |
| **SoftAP SSID** | `PILU` | Broadcast SSID |
| **SoftAP Password** | `28022006` | WPA2-PSK |
| **Local Gateway IP** | `192.168.4.1` | Subnet: `255.255.255.0` |
| **Admin Dashboard** | `http://192.168.4.1` | Port 80 |
| **Admin Password** | `admin2802` | Configurable via Web UI |

---

## Project Structure

```
PILU-WIFI-MANAGER/
├── firmware/
│   └── pilu_firmware/
│       ├── pilu_firmware.ino      # Main sketch entry point & Serial CLI
│       ├── config.h               # System constants & network parameters
│       ├── wifi_manager.h/.cpp    # AP+STA lifecycle & failover state machine
│       ├── nat_router.h/.cpp      # lwIP NAPT packet translation & DNS sync
│       ├── nvs_storage.h/.cpp     # Persistent profile and config storage
│       ├── web_server.h/.cpp      # HTTP server with chunked HTML streaming & REST APIs
│       ├── dashboard_html.h       # Embedded SPA single-page administration UI
│       ├── system_monitor.h/.cpp  # Periodic background health diagnostics
│       └── pilu_log.h/.cpp        # Timestamped UART logging subsystem
├── documentation/                 # Engineering architecture & validation reports
├── .gitignore                     # Git ignore rules for build artifacts
└── README.md                      # Project documentation
```

---

## Building and Flashing

### Using Arduino CLI
```bash
# Compile firmware
arduino-cli compile --fqbn esp32:esp32:esp32 --output-dir firmware/build firmware/pilu_firmware

# Upload to ESP32 on COM8
arduino-cli upload -p COM8 --fqbn esp32:esp32:esp32 firmware/pilu_firmware
```

---

## Serial CLI Commands

Connect via any serial terminal at **115,200 baud**:

```
help                         : Show available commands
status                       : Display real-time system and link diagnostics
scan                         : Scan nearby 2.4 GHz Wi-Fi networks
set_wifi <SSID> <PASSWORD>   : Configure Priority 1 Primary network and connect
set_backup <SSID> <PASSWORD> : Configure Priority 2 Backup profile in NVS
restart                      : Reboot the ESP32
```

---

## License

Based on lwIP NAPT foundation by Daniel Hope, Kenta Ida, and Martin Gergeleit.
Licensed under the Apache-2.0 / MIT License.
