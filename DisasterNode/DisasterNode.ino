/*
 * =====================================================================================
 * File: DisasterNode.ino
 * Project: Heltec Wireless Stick Lite V3 - Disaster Recovery Multi-Node LoRa Network
 * Author: Antigravity AI / FYP Disaster Recovery Team
 * 
 * Description:
 *   Main firmware sketch for the Heltec Wireless Stick Lite V3 (ESP32-S3 + SX1262 LoRa).
 *   
 *   Configurable Node Roles (Set in Config.h):
 *   -----------------------------------------------------------------------------------
 *   1. ROLE_CIVILIAN (Default for building nodes):
 *      - Creates an open WiFi Access Point ("EMERGENCY_NODE_XXXX").
 *      - Runs a Captive Portal DNS Server (intercepts port 53, prompts Android "Sign in to network").
 *      - Serves mobile-friendly Emergency Web Portal directly from LittleFS flash storage.
 *      - Allows trapped civilians to select emergency status tags (Trapped, Injured, etc.)
 *        and broadcast SOS packets across the LoRa mesh.
 *      - Excludes all administrative and clearance routes to prevent unauthorized access.
 *      
 *   2. ROLE_RESCUER (For mobile search and rescue teams):
 *      - Creates a Rescuer Command AP ("RESCUER_CMD_XXXX").
 *      - Serves the Tactical Command Console at http://192.168.4.1/.
 *      - Enables rescuers to view all active building incidents sorted by priority.
 *      - Emits Anti-Packets (PKT_CLEAR_MESSAGES) to invalidate resolved alerts and purge
 *        backlogs across all building nodes in the mesh.
 *      - Sends Rescuer Proximity Beacons to trigger offline node storage dumps.
 * 
 * Instructions:
 *   - Open this sketch in Arduino IDE (or VS Code + PlatformIO).
 *   - Required Libraries (Install via Tools > Manage Libraries):
 *       1. RadioLib (by Jan Gromeš) - Version 6.x+
 *       2. ESPAsyncWebServer (by me-no-dev / mathieucarbou)
 *       3. AsyncTCP (by me-no-dev / mathieucarbou)
 *       4. ArduinoJson (by Benoit Blanchon) - Version 6.x or 7.x
 *   - For Civilian Nodes: Upload the data/ folder to LittleFS flash using the
 *     "ESP32 LittleFS Data Upload" tool.
 *   - Board Selection: "Heltec Wireless Stick Lite (V3)" or "ESP32S3 Dev Module".
 * =====================================================================================
 */

#include <Arduino.h>
#include "Config.h"
#include "PacketDefs.h"
#include "StorageManager.h"
#include "LoRaMeshManager.h"
#include "WebServerManager.h"

//to get the MAC
#include "esp_mac.h"

// =====================================================================================
// GLOBAL VARIABLES & SYSTEM INSTANCES
// =====================================================================================

// Global unique Node ID: Generated at boot from the lower 2 bytes of the ESP32 hardware MAC
uint16_t g_nodeId = 0x0001;

// Storage Manager: Encapsulates LittleFS flash files and NVS Preferences for persistent counters
StorageManager    storageManager;

// LoRa Mesh Manager: Controls Semtech SX1262 radio, CAD collision avoidance, and TTL mesh relaying
LoRaMeshManager   meshManager(&storageManager);

// Web Server Manager: Manages SoftAP, Captive Portal DNS redirection, and Async REST routes
WebServerManager  webServerManager(&storageManager, &meshManager);

/**
 * Hardware Interrupt Service Routine (ISR) for the Semtech SX1262 DIO1 pin.
 * 
 * NOTE ON IRAM_ATTR:
 * In ESP32 Arduino, ISR functions MUST be tagged with `IRAM_ATTR`. This places the code
 * directly into Instruction RAM rather than flash cache. If an interrupt fires while flash
 * is being written (such as during LittleFS file saves), code in flash cannot be accessed
 * and causes a fatal crash. IRAM_ATTR guarantees immediate, safe execution.
 */
void IRAM_ATTR setLoRaRxFlag() {
    // Notify the mesh manager that a radio packet or event has occurred on DIO1
    meshManager.handleInterruptFlag();
}

// =====================================================================================
// SETUP FUNCTION: Hardware & Subsystem Initialization
// =====================================================================================
void setup() {
    // 1. Initialize Serial Monitor for debugging diagnostics
    Serial.begin(115200);
    delay(1000); // Allow serial line voltage to settle
    Serial.println();
    Serial.println("======================================================================");
    Serial.println("   HELTEC WIRELESS STICK LITE V3 - DISASTER RECOVERY LORA NODE      ");
    Serial.println("======================================================================");
    Serial.printf("[SYSTEM ROLE] Configured Mode: %s\n",
                  CURRENT_NODE_ROLE == ROLE_RESCUER ? "RESCUER GATEWAY" : "CIVILIAN NODE");

    // 2. Initialize Hardware Identity (MAC Address)
    // ---------------------------------------------------------------------------------
    // CRITICAL BUG FIX NOTE:
    // On ESP32, calling `WiFi.macAddress()` before explicitly initializing the WiFi module
    // returns an uninitialized array of zeros (00:00:00:00:00:00), causing all nodes to
    // have the exact same Node ID (0x0000) and SSID!
    // We explicitly call `WiFi.mode(WIFI_AP)` first, which forces the ESP32 to load its
    // factory-burned silicon MAC address from eFuse memory.
    // ---------------------------------------------------------------------------------
    WiFi.mode(WIFI_AP);
    uint8_t mac[6];
    
    // WiFi.macAddress(mac); // Does't work, use the built-in ESP API instead
     esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    
    g_nodeId = ((uint16_t)mac[4] << 8) | mac[5];
    Serial.printf("[SYSTEM] Assigned Node ID: 0x%04X (MAC: %02X:%02X:%02X:%02X:%02X:%02X)\n",
                  g_nodeId, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    // 3. Initialize Board User LED (GPIO 35 on Heltec V3)
    pinMode(BOARD_LED_PIN, OUTPUT);
    digitalWrite(BOARD_LED_PIN, HIGH); // Turn LED ON to indicate boot/initialization in progress

    // 4. Initialize LittleFS Flash Storage & NVS Preferences
    // Formats the flash partition if unformatted, mounts /unsent_sos.bin and /sos_history.bin,
    // and initializes the persistent message sequence counter in NVS memory.
    if (!storageManager.begin()) {
        Serial.println("[CRITICAL ERROR] LittleFS Storage initialization failed!");
    }

    // 5. Initialize Semtech SX1262 LoRa Module via RadioLib
    // Enables Vext power rail (GPIO 36 LOW), starts SPI bus (SCK:9, MISO:11, MOSI:10, NSS:8),
    // configures frequency, spreading factor, bandwidth, coding rate, and attaches DIO1 ISR.
    if (!meshManager.begin()) {
        Serial.println("[CRITICAL ERROR] LoRa Radio initialization failed! Check pins and Vext.");
    }

    // 6. Initialize WiFi Access Point, Captive Portal DNS Server, and HTTP Routes
    // Configures SoftAP (EMERGENCY_NODE_XXXX or RESCUER_CMD_XXXX), starts DNS server on port 53
    // to intercept all domains (*), and registers role-segregated REST endpoints.
    if (!webServerManager.begin()) {
        Serial.println("[CRITICAL ERROR] Web Server initialization failed!");
    }

    // Turn LED OFF to signify successful startup and ready state
    digitalWrite(BOARD_LED_PIN, LOW);
    Serial.println("[SYSTEM SUCCESS] Disaster Node startup complete! Listening on LoRa & WiFi.");
    Serial.println("----------------------------------------------------------------------");
}

// =====================================================================================
// MAIN LOOP: Cooperative Multi-Tasking Event Loop
// =====================================================================================
void loop() {
    // 1. Process Captive Portal DNS Requests
    // Intercepts domain lookup requests from connected mobile devices (e.g. connectivitycheck.gstatic.com)
    // and answers with the node's IP (192.168.4.1), prompting the OS "Sign in to network" dialog.
    webServerManager.update();

    // 2. Process LoRa Radio Interrupt Events & Mesh Protocol
    // Checks if the DIO1 interrupt flag was raised. Reads received LoRa frames, processes
    // SOS alerts, Anti-Packets, ACKs, decrements TTL, and performs mesh relaying.
    meshManager.update();

    // 3. Periodic Diagnostic Log Output (Every 30 seconds)
    // Outputs vital health metrics to Serial Monitor: client count, stored offline backlog, and uptime.
    static uint32_t lastDiagTime = 0;
    if (millis() - lastDiagTime > 30000) {
        lastDiagTime = millis();
        Serial.printf("[DIAGNOSTIC] Node: 0x%04X | Role: %s | WiFi Clients: %d | Pending SOS: %u | Uptime: %lu s\n",
                      g_nodeId,
                      CURRENT_NODE_ROLE == ROLE_RESCUER ? "RESCUER" : "CIVILIAN",
                      WiFi.softAPgetStationNum(),
                      storageManager.getStoredMessageCount(),
                      millis() / 1000);
    }
}
