/*
 * =====================================================================================
 * File: DisasterNode.ino
 * Project: Heltec Wireless Stick Lite V3 - Disaster Recovery Multi-Node LoRa Network
 * Author: FYP loRA Team (Hiew Jing Hong / Teh Ming Dong)
 * 
 * Description:
 *   Main firmware sketch for Heltec Wireless Stick Lite V3 (ESP32-S3 + SX1262).
 *   Integrates:
 *   1. WiFi Access Point & Captive Portal (Redirects trapped victim Android phones to SOS UI).
 *   2. LittleFS Flash Memory Storage (Store-and-Forward offline backup for unACKed distress logs).
 *   3. RadioLib SX1262 LoRa Driver (CAD collision avoidance, Mesh TTL relaying, Deduplication).
 * 
 * Instructions:
 *   - Open this folder in Arduino IDE or VS Code PlatformIO.
 *   - Install Required Libraries:
 *       1. RadioLib (by Jan Gromeš)
 *       2. ESPAsyncWebServer (by me-no-dev / mathieucarbou)
 *       3. AsyncTCP (by me-no-dev / mathieucarbou)
 *       4. ArduinoJson (by Benoit Blanchon)
 *   - Upload filesystem data using "ESP32 LittleFS Data Upload" tool to flash the `data/` folder.
 *   - Select Board: "Heltec Wireless Stick Lite (V3)" or "ESP32S3 Dev Module".
 * =====================================================================================
 */

//to get the MAC
#include "esp_mac.h"

#include <Arduino.h>
#include "Config.h"
#include "PacketDefs.h"
#include "StorageManager.h"
#include "LoRaMeshManager.h"
#include "WebServerManager.h"

// Global unique Node ID (Generated from lower 2 bytes of ESP32 WiFi MAC address)
uint16_t g_nodeId = 0x0001;

// Core System Component Manager Instances
StorageManager    storageManager;
LoRaMeshManager   meshManager(&storageManager);
WebServerManager  webServerManager(&storageManager, &meshManager);

// Hardware Interrupt Service Routine for Semtech SX1262 DIO1 pin
void IRAM_ATTR setLoRaRxFlag() {
    meshManager.handleInterruptFlag();
}

void setup() {
    // 1. Initialize Serial Monitor for Debugging
    Serial.begin(115200);
    delay(1000);
    Serial.println();
    Serial.println("======================================================================");
    Serial.println("   HELTEC WIRELESS STICK LITE V3 - DISASTER RECOVERY LORA NODE      ");
    Serial.println("======================================================================");

    // 2. Generate Unique Node ID from ESP32 WiFi MAC Address
    // FIX: Initialize WiFi mode before reading MAC so it doesn't return 00:00:00:00:00:00
    WiFi.mode(WIFI_AP);
    uint8_t mac[6];

    // WiFi.macAddress(mac);
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);

    g_nodeId = ((uint16_t)mac[4] << 8) | mac[5];
    Serial.printf("[SYSTEM] Assigned Node ID: 0x%04X (MAC: %02X:%02X:%02X:%02X:%02X:%02X)\n",
                  g_nodeId, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    // 3. Initialize Board User LED
    pinMode(BOARD_LED_PIN, OUTPUT);
    digitalWrite(BOARD_LED_PIN, HIGH); // Turn ON LED during startup sequence

    // 4. Initialize LittleFS Flash Storage File System
    if (!storageManager.begin()) {
        Serial.println("[CRITICAL ERROR] Storage initialization failed!");
    }

    // 5. Initialize Semtech SX1262 LoRa Module via RadioLib
    if (!meshManager.begin()) {
        Serial.println("[CRITICAL ERROR] LoRa Radio initialization failed! Check wiring & Vext pin.");
    }

    // 6. Initialize WiFi Access Point, Captive Portal, and Async HTTP Web Server
    if (!webServerManager.begin()) {
        Serial.println("[CRITICAL ERROR] WiFi / Captive Portal Web Server initialization failed!");
    }

    digitalWrite(BOARD_LED_PIN, LOW); // Turn OFF LED after successful setup
    Serial.println("[SYSTEM SUCCESS] Disaster Node startup complete! System running in background.");
    Serial.println("----------------------------------------------------------------------");
}

void loop() {
    // 1. Process Captive Portal DNS Queries (Redirects Android browser to 192.168.4.1)
    webServerManager.update();

    // 2. Process Incoming / Outgoing LoRa Radio Packets (Interrupt driven)
    meshManager.update();

    // 3. Periodic Diagnostic Log Output (Every 30 seconds)
    static uint32_t lastDiagTime = 0;
    if (millis() - lastDiagTime > 30000) {
        lastDiagTime = millis();
        Serial.printf("[DIAGNOSTIC] Node: 0x%04X | WiFi Clients: %d | Stored Offline SOS: %u | Uptime: %lu s\n",
                      g_nodeId,
                      WiFi.softAPgetStationNum(),
                      storageManager.getStoredMessageCount(),
                      millis() / 1000);
    }
}
