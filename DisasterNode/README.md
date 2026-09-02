# Heltec Wireless Stick Lite V3 - Disaster Recovery Multi-Node LoRa Network

Complete firmware system for the **Heltec Wireless Stick Lite V3 (ESP32-S3 + SX1262 LoRa)** designed for disaster recovery scenarios when cellular and internet infrastructure are down.

---

## Key Features

1. **Automatic WiFi Captive Portal**: When victims connect their Android devices to the open WiFi network (`EMERGENCY_NODE_XXXX`), Android automatically displays a "Sign in to network" pop-up launching the Emergency Distress Web App.
2. **Extensible Binary LoRa Packets (`PacketDefs.h`)**: Binary payloads are versioned and packed into compact structures (~64 bytes) for maximum RF penetration through rubble and concrete. New fields can be easily added as requirements evolve.
3. **Hybrid LoRa Mesh & Store-and-Forward**:
   - **Mesh Relay**: Packets are broadcast over a multi-hop mesh network (`LORA_MAX_HOP_COUNT = 3`) with deduplication to reach rescuers beyond direct radio line-of-sight.
   - **Store-and-Forward**: If rescuers are out of range and no ACK is received, distress messages are saved to local flash memory (`LittleFS`) and automatically transmitted when a rescuer node comes into range.
4. **Asynchronous Non-Blocking Web Server**: Built with `ESPAsyncWebServer` and `AsyncTCP` to handle multiple victim Android phones simultaneously without blocking LoRa transmission.

---

## File Structure & Where to Change Settings

- **`Config.h`**: Centralized configuration file. **Change your parameters here**:
  - `LORA_MAX_HOP_COUNT` (Default: `3` hops)
  - `LORA_ACK_WAIT_TIMEOUT_MS` (Default: `10000` ms)
  - `LORA_MAX_RETRIES` (Default: `3` attempts)
  - `LORA_FREQUENCY` (Default: `915.0` MHz)
  - `LORA_BANDWIDTH` / `LORA_SPREADING_FACTOR` / `LORA_OUTPUT_POWER`
  - `WIFI_AP_SSID_PREFIX`
- **`PacketDefs.h`**: Packet schema definition. **Modify binary payload fields here**. Includes instructions on how to add GPS coordinates, battery level, or custom alert tags.
- **`StorageManager.h`**: Manages LittleFS flash storage for offline SOS logs.
- **`LoRaMeshManager.h`**: Handles Semtech SX1262 radio control via RadioLib, CAD channel activity detection, deduplication ring buffer, and TTL mesh relaying.
- **`WebServerManager.h`**: WiFi SoftAP setup, Captive Portal DNS redirection, and Async REST API handlers.
- **`data/` Folder**: Contains the web app frontend (`index.html`, `style.css`, `app.js`).

---

## Required Arduino IDE Libraries

Before compiling, open **Tools > Manage Libraries** in Arduino IDE and install:

1. **RadioLib** (by Jan Gromeš) - Version 6.x+
2. **ESPAsyncWebServer** (by me-no-dev / mathieucarbou)
3. **AsyncTCP** (by me-no-dev / mathieucarbou)
4. **ArduinoJson** (by Benoit Blanchon) - Version 6.x or 7.x

---

## How to Flash to Board

1. **Board Selection**: In Arduino IDE, select **Heltec Wireless Stick Lite (V3)** or **ESP32S3 Dev Module**.
2. **Upload Sketch**: Compile and upload `DisasterNode.ino` via USB-C.
3. **Upload Web Data to LittleFS**:
   - Install the **Arduino ESP32 LittleFS Filesystem Upload** plugin for Arduino IDE (or use PlatformIO `Upload Filesystem Image`).
   
      - Instructions here (https://randomnerdtutorials.com/arduino-ide-2-install-esp32-littlefs/)
      
   - Run **Tools > ESP32 Sketch Data Upload** to flash the contents of the `data/` folder to ESP32 Flash memory.
4. **Open Serial Monitor**: Set baud rate to `115200` to view node initialization logs, assigned Node ID, and LoRa transmission diagnostics.
