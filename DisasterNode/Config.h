/*
 * =====================================================================================
 * File: Config.h
 * Project: Heltec Wireless Stick Lite V3 - Disaster Recovery Multi-Node LoRa Network
 * Author: Antigravity AI / FYP Disaster Recovery Team
 * 
 * Description:
 *   Centralized configuration header file. Contains all easily adjustable variables,
 *   hardware pin assignments for the Heltec V3 board (ESP32-S3 + SX1262), WiFi AP
 *   credentials, timer thresholds, network roles, and LoRa radio parameters.
 * =====================================================================================
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// =====================================================================================
// SECTION 0: NODE ROLE CONFIGURATION (COMPILE-TIME SECURITY)
// Choose whether this firmware build is for a civilian building node or a rescuer gateway.
// =====================================================================================
enum NodeRole : uint8_t {
    ROLE_CIVILIAN = 0, // Building node: Hosts victim captive portal (No admin/clear access)
    ROLE_RESCUER  = 1  // Mobile rescuer gateway: Hosts tactical command dashboard
};

// >>> CHOOSE THE ROLE BEFORE COMPILING AND FLASHING <<<
#define CURRENT_NODE_ROLE       ROLE_CIVILIAN

// =====================================================================================
// SECTION 1: HARDWARE PIN MAPPING (Heltec Wireless Stick Lite V3 - ESP32-S3)
// =====================================================================================

// Semtech SX1262 LoRa SPI & Control Pins
#define LORA_NSS_PIN        8   // SPI Chip Select (CS)
#define LORA_DIO1_PIN      14   // Interrupt 1 (IRQ for RX/TX done)
#define LORA_RESET_PIN     12   // Hardware Reset Pin
#define LORA_BUSY_PIN      13   // SX1262 Busy Status Pin

// SPI Bus Pins for ESP32-S3
#define LORA_SCK_PIN        9   // SPI Clock
#define LORA_MISO_PIN      11   // SPI Master In Slave Out
#define LORA_MOSI_PIN      10   // SPI Master Out Slave In

// Board Hardware Control Pins
#define VEXT_CTRL_PIN      36   // Heltec Vext Power Control (Set LOW to turn ON power to LoRa module & sensors)
#define BOARD_LED_PIN      35   // Built-in User LED on Heltec V3 (Set HIGH to turn ON)

// =====================================================================================
// SECTION 2: LORA RADIO PARAMETERS
// Adjust these to match your local regional spectrum regulations and channel needs.
// =====================================================================================

#define LORA_FREQUENCY          915.0   // Frequency in MHz (e.g. 915.0 for US/AU, 868.0 for EU, 433.0 for AS)
#define LORA_BANDWIDTH          125.0   // Bandwidth in kHz (125.0, 250.0, 500.0). Lower = more sensitivity & range.
#define LORA_SPREADING_FACTOR   9       // SF 6 to 12. Higher SF = longer range, slower data rate & higher power.
#define LORA_CODING_RATE        7       // 5 (4/5), 6 (4/6), 7 (4/7), 8 (4/8). Higher = better error correction.
#define LORA_SYNC_WORD          0x12    // Sync Word (0x12 for private networks, 0x34 for public LoRaWAN)
#define LORA_OUTPUT_POWER       22      // Output Power in dBm (SX1262 supports -9 to +22 dBm)
#define LORA_PREAMBLE_LENGTH    8       // Preamble symbol length

// =====================================================================================
// SECTION 3: NETWORK & MESH TIMING VARIABLES (EASILY ADJUSTABLE)
// =====================================================================================

// Maximum number of hops a packet can travel across the mesh network before being dropped
const uint8_t LORA_MAX_HOP_COUNT = 3;

// Time to wait for a rescuer ACK (in milliseconds) before marking SOS as pending local storage
const uint32_t LORA_ACK_WAIT_TIMEOUT_MS = 10000; // 10 seconds

// Maximum retransmission attempts for an SOS alert before falling back to local storage
const uint8_t LORA_MAX_RETRIES = 3;

// Delay between retransmission retries (in milliseconds)
const uint32_t LORA_RETRY_DELAY_MS = 2000; // 2 seconds

// Size of the ring buffer used to remember recently received message IDs (prevents infinite mesh relay loops)
const uint8_t DEDUPLICATION_CACHE_SIZE = 32;

// Maximum number of offline distress messages stored in LittleFS flash memory when no ACK is received
const uint16_t MAX_STORED_OFFLINE_MESSAGES = 50;

// Rescuer Beacon broadcast interval (in milliseconds) when operating in Rescuer Node mode
const uint32_t RESCUER_BEACON_INTERVAL_MS = 15000; // 15 seconds

// Maximum number of incident IDs that can be cleared in a single batch Anti-Packet frame
#define MAX_CLEAR_BATCH_SIZE    8

// =====================================================================================
// SECTION 4: WIFI ACCESS POINT & CAPTIVE PORTAL SETTINGS
// =====================================================================================

#define CIVILIAN_AP_SSID_PREFIX "EMERGENCY_NODE_" // SSID for civilian building nodes
#define RESCUER_AP_SSID_PREFIX  "RESCUER_CMD_"    // SSID for mobile rescuer gateway nodes
#define WIFI_AP_PASSWORD        ""                // Open WiFi network (no password)
#define WIFI_AP_CHANNEL         6                 // WiFi channel (1 to 13)
#define WIFI_MAX_CONNECTIONS    10                // Maximum concurrent WiFi client devices

#define DNS_PORT                53                // Standard DNS port for Captive Portal redirection
#define HTTP_PORT               80                // Standard Web Server HTTP port

// =====================================================================================
// SECTION 5: UNIQUE NODE IDENTITY
// Default node ID generated from ESP32 MAC address at runtime.
// =====================================================================================
extern uint16_t g_nodeId;

#endif // CONFIG_H
