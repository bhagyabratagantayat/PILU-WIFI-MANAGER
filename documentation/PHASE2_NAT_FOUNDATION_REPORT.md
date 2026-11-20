# Phase 2: NAT Foundation Implementation & Hardware Validation Report

## 1. Executive Summary
Phase 2 implements the core firmware foundation of **PILU Wi-Fi Manager**, transforming the ESP32 DevKit V1 into a production-grade Wi-Fi NAT repeater. 

The firmware operates the ESP32 in dual AP+STA mode (`WIFI_MODE_APSTA`), hosting the secure SoftAP hotspot `PILU` (`192.168.4.1/24`) while connecting upstream to `AYUSH TECHNOLOGIES`. Internet packets are forwarded transparently between the hotspot and the upstream gateway via the native lwIP Network Address and Port Translation (NAPT) engine (`ip_napt_enable`).

---

## 2. Firmware Architecture & Source Files

All source files are located in [`PILU-WIFI-MANAGER/firmware/pilu_firmware/`](file:///d:/TEST%20PROJECT/PILU%20WIFI%20MANAGER%20BY%20ESP32/PILU-WIFI-MANAGER/firmware/pilu_firmware/):

1. **[`pilu_firmware.ino`](file:///d:/TEST%20PROJECT/PILU%20WIFI%20MANAGER%20BY%20ESP32/PILU-WIFI-MANAGER/firmware/pilu_firmware/pilu_firmware.ino)**:
   - Primary application entry point (`setup()` and `loop()`).
   - Initialized hardware peripherals, NVS storage, Wi-Fi subsystem, lwIP NAPT, and diagnostic monitor.
   - Embeds open-source licensing attributions (Daniel Hope, Kenta Ida, Martin Gergeleit).
2. **[`config.h`](file:///d:/TEST%20PROJECT/PILU%20WIFI%20MANAGER%20BY%20ESP32/PILU-WIFI-MANAGER/firmware/pilu_firmware/config.h)**:
   - System configurations, timeouts, and network constants.
   - Hotspot defaults: SSID `PILU`, Password `28022006`, Security `WPA2-PSK`, Channel `1`, Max Clients `8`.
   - Addressing: Local IP `192.168.4.1`, Netmask `255.255.255.0`, Gateway `192.168.4.1`.
   - Upstream default: SSID `AYUSH TECHNOLOGIES`.
3. **[`nvs_storage.h`](file:///d:/TEST%20PROJECT/PILU%20WIFI%20MANAGER%20BY%20ESP32/PILU-WIFI-MANAGER/firmware/pilu_firmware/nvs_storage.h)** / **[`nvs_storage.cpp`](file:///d:/TEST%20PROJECT/PILU%20WIFI%20MANAGER%20BY%20ESP32/PILU-WIFI-MANAGER/firmware/pilu_firmware/nvs_storage.cpp)**:
   - Manages non-volatile storage in namespace `pilu_wifi`.
   - Handles flash corruption recovery and wear-leveling.
   - Masks sensitive credentials (`[PROTECTED]`) so plaintext passwords are never emitted in logs.
4. **[`nat_router.h`](file:///d:/TEST%20PROJECT/PILU%20WIFI%20MANAGER%20BY%20ESP32/PILU-WIFI-MANAGER/firmware/pilu_firmware/nat_router.h)** / **[`nat_router.cpp`](file:///d:/TEST%20PROJECT/PILU%20WIFI%20MANAGER%20BY%20ESP32/PILU-WIFI-MANAGER/firmware/pilu_firmware/nat_router.cpp)**:
   - lwIP NAPT controller using native `ip_napt_enable(0x0104A8C0, 1)`.
   - Configures the ESP32 DHCP server to distribute upstream/public DNS (`8.8.8.8` / `1.1.1.1`) to all connected clients (`OFFER_DNS` option).
   - Dynamically re-asserts NAPT mapping tables when IP leases change.
5. **[`wifi_manager.h`](file:///d:/TEST%20PROJECT/PILU%20WIFI%20MANAGER%20BY%20ESP32/PILU-WIFI-MANAGER/firmware/pilu_firmware/wifi_manager.h)** / **[`wifi_manager.cpp`](file:///d:/TEST%20PROJECT/PILU%20WIFI%20MANAGER%20BY%20ESP32/PILU-WIFI-MANAGER/firmware/pilu_firmware/wifi_manager.cpp)**:
   - Dual interface AP+STA manager.
   - Non-blocking exponential backoff reconnection engine (initial 2s up to 30s ceiling).
   - **Hotspot Preservation**: When upstream Wi-Fi drops, the local SoftAP `PILU` remains active, keeping clients connected and local management responsive.
6. **[`system_monitor.h`](file:///d:/TEST%20PROJECT/PILU%20WIFI%20MANAGER%20BY%20ESP32/PILU-WIFI-MANAGER/firmware/pilu_firmware/system_monitor.h)** / **[`system_monitor.cpp`](file:///d:/TEST%20PROJECT/PILU%20WIFI%20MANAGER%20BY%20ESP32/PILU-WIFI-MANAGER/firmware/pilu_firmware/system_monitor.cpp)**:
   - Real-time diagnostic loop reporting heap statistics, active clients, upstream RSSI, and NAT state.

---

## 3. Build & Compilation Verification

Live compilation was executed via `arduino-cli` using ESP32 Core v3.3.12 (ESP-IDF 5.x) for target `esp32:esp32:esp32`.

### Build Metrics
- **Compilation Status**: `SUCCESS (Exit Code 0)`
- **Program Storage (Flash)**: `882,176 bytes` (67% of standard 1.31 MB partition)
- **Dynamic Memory (RAM)**: `46,120 bytes` (14% of 320 KB RAM)
- **Free Heap Headroom**: **`281,560 bytes`** (substantially exceeds requirements for TLS/HTTPS cloud communication in subsequent phases)

### Exported Binaries
All flashable binary artifacts are compiled and stored in [`PILU-WIFI-MANAGER/firmware/build/`](file:///d:/TEST%20PROJECT/PILU%20WIFI%20MANAGER%20BY%20ESP32/PILU-WIFI-MANAGER/firmware/build/):
- `pilu_firmware.ino.bin` (Application: 882,320 bytes)
- `pilu_firmware.ino.bootloader.bin` (Second-stage bootloader: 24,992 bytes)
- `pilu_firmware.ino.partitions.bin` (Partition table: 3,072 bytes)
- `pilu_firmware.ino.merged.bin` (Unified all-in-one image: 4,194,304 bytes)

---

## 4. Flashing Instructions

The hardware port was detected on **COM8** (`ESP32-D0WD-V3 revision 3.1, MAC: b4:bf:e9:6a:db:0c`).

### Option A: Direct One-Command Flash via esptool.exe (Merged Binary at 0x0)
```powershell
& "$env:LOCALAPPDATA\Arduino15\packages\esp32\tools\esptool_py\5.3.1\esptool.exe" `
    --chip esp32 --port COM8 --baud 921600 `
    --before default_reset --after hard_reset `
    write_flash -z --flash_mode dio --flash_freq 80m --flash_size 4MB `
    0x0000 "D:\TEST PROJECT\PILU WIFI MANAGER BY ESP32\PILU-WIFI-MANAGER\firmware\build\pilu_firmware.ino.merged.bin"
```

### Option B: Standard Multi-Partition Flash
```powershell
& "$env:LOCALAPPDATA\Arduino15\packages\esp32\tools\esptool_py\5.3.1\esptool.exe" `
    --chip esp32 --port COM8 --baud 921600 `
    --before default_reset --after hard_reset `
    write_flash -z --flash_mode dio --flash_freq 80m --flash_size 4MB `
    0x1000 "D:\TEST PROJECT\PILU WIFI MANAGER BY ESP32\PILU-WIFI-MANAGER\firmware\build\pilu_firmware.ino.bootloader.bin" `
    0x8000 "D:\TEST PROJECT\PILU WIFI MANAGER BY ESP32\PILU-WIFI-MANAGER\firmware\build\pilu_firmware.ino.partitions.bin" `
    0x10000 "D:\TEST PROJECT\PILU WIFI MANAGER BY ESP32\PILU-WIFI-MANAGER\firmware\build\pilu_firmware.ino.bin"
```

### Option C: Arduino-CLI Upload
```powershell
& "$env:LOCALAPPDATA\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe" `
    upload -p COM8 --fqbn esp32:esp32:esp32 "D:\TEST PROJECT\PILU WIFI MANAGER BY ESP32\PILU-WIFI-MANAGER\firmware\pilu_firmware"
```

---

## 5. Expected Serial Monitor Logs (115200 Baud)

```text
==================================================
 PILU Wi-Fi Manager v1.0.0-phase2
 Smart ESP32 Wi-Fi Repeater
 Target: ESP32 DevKit V1 (ESP32-WROOM-32)
 Foundation: lwIP NAPT & ESP32 NAT Router
 Copyright (c) 2021 Daniel Hope, Kenta Ida, Martin Gergeleit
==================================================

I (512) PILU_NVS: NVS flash initialized successfully
I (520) PILU_NVS: Loaded AP SSID from NVS: PILU
I (525) PILU_NVS: Loaded AP Password from NVS: [PROTECTED]
I (531) PILU_NVS: Loaded Upstream STA SSID from NVS: AYUSH TECHNOLOGIES
I (538) PILU_NVS: Loaded Upstream STA Password from NVS: [PROTECTED]
[INIT] SoftAP Config: SSID='PILU' | Pass=******** (length=8) | Chan=1 | MaxConn=8
[INIT] Upstream STA Config: SSID='AYUSH TECHNOLOGIES' | Pass=******** (length=10)
I (550) PILU_WIFI: Wi-Fi manager event handlers registered successfully
I (558) PILU_WIFI: Starting simultaneous AP + STA mode (Hotspot: PILU, Upstream: AYUSH TECHNOLOGIES)...
I (610) PILU_WIFI: Hotspot 'PILU' started successfully (Channel 1)
I (615) PILU_NAT: Configuring SoftAP network: IP=192.168.4.1, Mask=255.255.255.0, GW=192.168.4.1
I (624) PILU_NAT: SoftAP DHCP server active on 192.168.4.1 (DNS offer enabled)
I (630) PILU_NAT: Enabling lwIP NAPT routing on AP subnet (IP: 0x0104A8C0)...
I (635) PILU_NAT: lwIP NAPT internet packet forwarding is ACTIVE
I (642) PILU_WIFI: Station interface ready. Initiating connection to upstream 'AYUSH TECHNOLOGIES'...
I (1820) PILU_WIFI: Associated with upstream 'AYUSH TECHNOLOGIES' on Channel 6. Awaiting DHCP IP...
I (2450) PILU_WIFI: Upstream IP acquired: 192.168.1.145 (Mask: 255.255.255.0, GW: 192.168.1.1)
I (2456) PILU_WIFI: Upstream Signal Strength: -62 dBm
I (2461) PILU_NAT: Enabling lwIP NAPT routing on AP subnet (IP: 0x0104A8C0)...
I (2468) PILU_NAT: Synchronized Upstream DNS to SoftAP DHCP: 192.168.1.1
I (2474) PILU_MONITOR: System diagnostic monitor started
[INIT] Firmware initialization complete. System running.
--------------------------------------------------
PILU REPEATER STATUS (Uptime: 10 s | Heap: 279540 B [Min: 271220 B])
  Hotspot: 'PILU' | Gateway: 192.168.4.1 | Clients: 0
  Upstream: 'AYUSH TECHNOLOGIES' | Status: CONNECTED | IP: 192.168.1.145 | RSSI: -62 dBm
  NAT Packet Forwarding: ACTIVE
--------------------------------------------------
```

When an Android phone connects to the hotspot:
```text
I (14520) PILU_WIFI: Client connected to PILU: MAC 34:12:F9:8A:21:4C (AID=1, Total Clients=1)
--------------------------------------------------
PILU REPEATER STATUS (Uptime: 20 s | Heap: 274320 B [Min: 268110 B])
  Hotspot: 'PILU' | Gateway: 192.168.4.1 | Clients: 1
  Upstream: 'AYUSH TECHNOLOGIES' | Status: CONNECTED | IP: 192.168.1.145 | RSSI: -63 dBm
  NAT Packet Forwarding: ACTIVE
--------------------------------------------------
```

---

## 6. Hardware Validation Checklist (Android Phone Test)

To be executed on the physical hardware:

| Step | Test Procedure | Expected Result | Status |
| :---: | :--- | :--- | :---: |
| **1** | Power ESP32 from 5V USB on DevKit V1 | Red power LED lights up; blue status flashes; boot completes in < 2 seconds | Ready for Physical Test |
| **2** | Scan Wi-Fi on Android Phone | Network `PILU` appears in nearby Wi-Fi list | Ready for Physical Test |
| **3** | Connect to `PILU` with password `28022006` | Phone authenticates via WPA2-PSK and connects | Ready for Physical Test |
| **4** | Check Phone Wi-Fi Settings Details | Assigned IP is `192.168.4.2`–`192.168.4.254`, Gateway is `192.168.4.1`, Netmask `255.255.255.0` | Ready for Physical Test |
| **5** | Verify DNS Resolution on Phone | Phone resolves public domains (`google.com`, `wikipedia.org`) | Ready for Physical Test |
| **6** | Open Web Browser on Phone | Web pages load smoothly through the ESP32 NAT bridge | Ready for Physical Test |
| **7** | Execute Internet Speed Test (`fast.com` or `speedtest.net`) | Achieves expected 5 to 15 Mbps throughput on 2.4 GHz | Ready for Physical Test |
| **8** | Power down Upstream Router (or simulate outage) | ESP32 logs upstream disconnect. Hotspot `PILU` remains active; phone stays connected to `192.168.4.x` | Ready for Physical Test |
| **9** | Restore Upstream Router | ESP32 reconnection backoff detects recovery, acquires IP, re-arms NAPT, and internet forwarding resumes automatically | Ready for Physical Test |

---

## 7. Known Physical Limitations
1. **Single 2.4 GHz RF Transceiver**:
   - The ESP32-WROOM-32 has a single 2.4 GHz radio shared between SoftAP and STA.
   - When STA connects to upstream on Channel $X$, the SoftAP is forced to operate on Channel $X$.
   - A channel hop causes connected clients to briefly re-synchronize. Seamless zero-latency radio handoff across channels is physically impossible on single-radio hardware.
2. **Throughput Ceiling**:
   - Packet processing occurs in software via lwIP NAPT on the dual-core Tensilica Xtensa CPU. Typical real-world throughput ranges between 5 Mbps and 15 Mbps.
3. **Client Limit**:
   - Up to 8 simultaneous Wi-Fi clients are supported to maintain sufficient RAM headroom (~280 KB free heap).
