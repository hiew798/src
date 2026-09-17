/*
 * =====================================================================================
 * File: DisasterNode.ino
 * Project: Heltec Wireless Stick Lite V3 - Disaster Recovery Multi-Node LoRa Network
 * Author: Antigravity AI / FYP Disaster Recovery Team
 * 
 * Description:
 *   Main firmware sketch for Heltec Wireless Stick Lite V3 (ESP32-S3 + SX1262).
 *   
 *   Configurable Roles (Set in Config.h):
 *   - ROLE_CIVILIAN: Hosts victim captive portal (SOS form, offline emergency guides).
 *   - ROLE_RESCUER:  Hosts tactical triage dashboard (Backlog poll, Anti-Packet clear).
 * =====================================================================================
 */

#include <Arduino.h>
#include "Config.h"
#include "PacketDefs.h"
#include "StorageManager.h"
#include "LoRaMeshManager.h"
#include "WebServerManager.h"

// Global unique Node ID
uint16_t g_nodeId = 0x0001;

StorageManager    storageManager;
LoRaMeshManager   meshManager(&storageManager);
WebServerManager  webServerManager(&storageManager, &meshManager);

void IRAM_ATTR setLoRaRxFlag() {
    meshManager.handleInterruptFlag();
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println();
    Serial.println("======================================================================");
    Serial.println("   HELTEC WIRELESS STICK LITE V3 - DISASTER RECOVERY LORA NODE      ");
    Serial.println("======================================================================");
    Serial.printf("[SYSTEM ROLE] Configured Mode: %s\n",
                  CURRENT_NODE_ROLE == ROLE_RESCUER ? "RESCUER GATEWAY" : "CIVILIAN NODE");

    // 1. Initialize WiFi mode first so hardware MAC address loads from eFuse correctly
    WiFi.mode(WIFI_AP);
    uint8_t mac[6];
    WiFi.macAddress(mac);
    g_nodeId = ((uint16_t)mac[4] << 8) | mac[5];
    Serial.printf("[SYSTEM] Assigned Node ID: 0x%04X (MAC: %02X:%02X:%02X:%02X:%02X:%02X)\n",
                  g_nodeId, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    // 2. Initialize Board User LED
    pinMode(BOARD_LED_PIN, OUTPUT);
    digitalWrite(BOARD_LED_PIN, HIGH);

    // 3. Initialize LittleFS Flash File System & NVS Preferences
    if (!storageManager.begin()) {
        Serial.println("[CRITICAL ERROR] Storage initialization failed!");
    }

    // 4. Initialize Semtech SX1262 LoRa Module via RadioLib
    if (!meshManager.begin()) {
        Serial.println("[CRITICAL ERROR] LoRa Radio initialization failed!");
    }

    // 5. Initialize WiFi SoftAP, Captive Portal DNS, and Role-Segregated Web Server
    if (!webServerManager.begin()) {
        Serial.println("[CRITICAL ERROR] Web Server initialization failed!");
    }

    digitalWrite(BOARD_LED_PIN, LOW);
    Serial.println("[SYSTEM SUCCESS] Disaster Node startup complete!");
    Serial.println("----------------------------------------------------------------------");
}

void loop() {
    // 1. Process Captive Portal DNS Queries
    webServerManager.update();

    // 2. Process LoRa Mesh Radio Events
    meshManager.update();

    // 3. Periodic Diagnostic Log Output (Every 30 seconds)
    static uint32_t lastDiagTime = 0;
    if (millis() - lastDiagTime > 30000) {
        lastDiagTime = millis();
        Serial.printf("[DIAGNOSTIC] Node: 0x%04X | Role: %s | Clients: %d | Pending SOS: %u | Uptime: %lu s\n",
                      g_nodeId,
                      CURRENT_NODE_ROLE == ROLE_RESCUER ? "RESCUER" : "CIVILIAN",
                      WiFi.softAPgetStationNum(),
                      storageManager.getStoredMessageCount(),
                      millis() / 1000);
    }
}
