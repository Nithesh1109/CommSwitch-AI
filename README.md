# CommSwitch AI

## Overview

CommSwitch AI is a dual ESP32 intelligent communication failover system designed for resilient telemetry transmission using:

- Wi-Fi + MQTT
- LoRa SX1278
- FreeRTOS
- Dynamic RSSI failover logic

The system automatically switches to LoRa before complete Wi-Fi disconnection.

---

## Features

- Dual ESP32 architecture
- MQTT cloud telemetry
- LoRa failover communication
- FreeRTOS multi-core tasks
- OLED diagnostics
- RSSI hysteresis switching
- ACK/retry reliability layer
- Queue-based packet buffering
- Thread-safe sensor access
- Production-style firmware structure

---

## Hardware

### ESP32 DevKit V1
- 2x ESP32

### LoRa Modules
- 2x SX1278 Ra-02

### OLED
- SSD1306 128x64 I2C

---

## Planned Upgrades

- BME280 integration
- OTA updates
- AES encryption
- Mesh LoRa networking
- SD card logging
- Web dashboard

---

## Folder Structure

```text
CommSwitch-AI/
│
├── Firmware/
│   ├── Unit_A_Sender/
│   ├── Unit_B_Receiver/
│   └── Shared/
│
├── Docs/
├── Hardware/
└── TestLogs/
```

---

## Author

Nithesh J  
BTech ECE — REVA University
