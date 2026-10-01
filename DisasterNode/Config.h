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
// 
// Security Architecture Note:
// To ensure trapped civilians cannot access rescuer command actions (such as clearing
// distress reports or broadcasting fake Anti-Packets), the firmware uses strict compile-time
// role segregation. When compiled as ROLE_CIVILIAN, rescuer routes and administrative
// functions are completely excluded from the HTTP server routing table.
// =====================================================================================
enum NodeRole : uint8_t {
    ROLE_CIVILIAN = 0, // Building node: Hosts victim captive portal (No admin/clear access)
    ROLE_RESCUER  = 1  // Mobile rescuer gateway: Hosts tactical command dashboard
};

// >>> CHOOSE THE ROLE BEFORE COMPILING AND FLASHING TO EACH BOARD <<<
// Set to ROLE_CIVILIAN for building nodes deployed for trapped victims.
// Set to ROLE_RESCUER for mobile rescuer gateway nodes.
// #define CURRENT_NODE_ROLE      ROLE_CIVILIAN 
#define CURRENT_NODE_ROLE      ROLE_RESCUER

// =====================================================================================
// SECTION 1: HARDWARE PIN MAPPING (Heltec Wireless Stick Lite V3 - ESP32-S3)
//
// Hardware Notes:
// The Heltec Wireless Stick Lite V3 couples an ESP32-S3FN8 chip with a Semtech SX1262
// LoRa transceiver over a dedicated SPI bus.
// =====================================================================================

// Semtech SX1262 LoRa SPI & Control Pins
#define LORA_NSS_PIN        8   // SPI Chip Select (CS) - Active LOW
#define LORA_DIO1_PIN      14   // Hardware Interrupt line (Signals RX packet received or TX done)
#define LORA_RESET_PIN     12   // Active-LOW hardware reset pin for SX1262
#define LORA_BUSY_PIN      13   // SX1262 status pin (HIGH when internal state machine is busy)

// SPI Bus Pins for ESP32-S3 (Specific to Heltec V3 board routing)
#define LORA_SCK_PIN        9   // SPI Clock line
#define LORA_MISO_PIN      11   // Master In Slave Out (Radio -> ESP32)
#define LORA_MOSI_PIN      10   // Master Out Slave In (ESP32 -> Radio)

// Board Hardware Control Pins
// NOTE ON VEXT: On Heltec boards, external sensors and the LoRa chip power rail are controlled
// by a P-channel MOSFET on GPIO 36. Driving GPIO 36 LOW activates the 3.3V rail.
#define VEXT_CTRL_PIN      36   // Heltec Vext Power Control (Set LOW to power SX1262 & sensors)
#define BOARD_LED_PIN      35   // Built-in white/orange User LED (Set HIGH to turn ON)

// =====================================================================================
// SECTION 2: LORA RADIO PARAMETERS
// Adjust these to match your local regional spectrum regulations and deployment environment.
// =====================================================================================

// Frequency: Set to regional ISM band (915.0 MHz for US/Australia, 868.0 for Europe, 433.0 for Asia)
#define LORA_FREQUENCY          915.0

// Bandwidth: 125.0 kHz is standard. Lower bandwidth increases radio sensitivity and range.
#define LORA_BANDWIDTH          125.0

// Spreading Factor: SF 6 to 12.
// SF9 provides a balanced trade-off between long-range structural penetration (rubble/walls)
// and low on-air transmission time (~150ms per packet).
#define LORA_SPREADING_FACTOR   9

// Coding Rate: 7 corresponds to 4/7 forward error correction.
// Higher coding rates improve packet recovery when signals pass through heavy concrete/dust.
#define LORA_CODING_RATE        7

// Sync Word: 0x12 designates a private LoRa network. (0x34 is reserved for public LoRaWAN gateways).
#define LORA_SYNC_WORD          0x12

// Output Power: SX1262 supports -9 to +22 dBm. Set to 22 dBm for maximum disaster penetration.
#define LORA_OUTPUT_POWER       22

// Preamble Length: Standard 8 symbols ensures the receiving SX1262 wakes up and locks onto the frame.
#define LORA_PREAMBLE_LENGTH    8

// =====================================================================================
// SECTION 3: NETWORK & MESH TIMING VARIABLES (EASILY ADJUSTABLE)
// =====================================================================================

// Maximum number of hops a packet can travel across the mesh network before being dropped.
// Prevents packets from propagating forever. Each intermediate relay decrements this value by 1.
const uint8_t LORA_MAX_HOP_COUNT = 3;

// Time to wait for a rescuer ACK (in milliseconds) before marking an SOS for offline flash storage.
const uint32_t LORA_ACK_WAIT_TIMEOUT_MS = 10000; // 10 seconds

// Maximum retransmission attempts for an SOS alert before falling back to local storage.
const uint8_t LORA_MAX_RETRIES = 3;

// Delay between retransmission retries (in milliseconds).
const uint32_t LORA_RETRY_DELAY_MS = 2000; // 2 seconds

// Size of the ring buffer used to remember recently received message IDs.
// If a node receives a packet ID already in this cache, it drops the packet, preventing infinite echo loops.
const uint8_t DEDUPLICATION_CACHE_SIZE = 32;

// Maximum number of offline distress messages stored in LittleFS flash memory when no ACK is received.
// Prevents flash storage exhaustion.
const uint16_t MAX_STORED_OFFLINE_MESSAGES = 50;

// Rescuer Beacon broadcast interval (in milliseconds) when operating in Rescuer Gateway mode.
const uint32_t RESCUER_BEACON_INTERVAL_MS = 15000; // 15 seconds

// Maximum number of incident IDs that can be cleared in a single batch Anti-Packet frame.
// Keeping this at 8 ensures the total packet size stays well under 100 bytes.
#define MAX_CLEAR_BATCH_SIZE    8

// =====================================================================================
// SECTION 3.1: IMAGE TRANSMISSION VARIABLES
// =====================================================================================

// Max bytes of image payload per chunk to keep packet size under LoRa's 255 byte limit
#define MAX_IMAGE_CHUNK_DATA_LEN 180

// Delay between transmitting sequential chunks of an image (in milliseconds).
// Prevents flooding the radio spectrum and allows high-priority SOS packets to interleave.
// NOTE: Must be > (relay jitter max 450ms + CAD ~50ms + TX ~200ms) = ~700ms for multi-hop
// relay to complete before the next chunk arrives. Set to 800ms for safe margin.
const uint32_t IMAGE_CHUNK_TX_INTERVAL_MS = 800;

// =====================================================================================
// SECTION 4: WIFI ACCESS POINT & CAPTIVE PORTAL SETTINGS
// =====================================================================================

// SSID Prefixes: Distinct prefixes allow users to easily tell node roles apart in WiFi settings
#define CIVILIAN_AP_SSID_PREFIX "EMERGENCY_NODE_" // SSID for civilian building nodes
#define RESCUER_AP_SSID_PREFIX  "RESCUER_CMD_"    // SSID for mobile rescuer gateway nodes

// WiFi Password: Empty string creates an open network so victims can connect instantly without credentials
#define WIFI_AP_PASSWORD        ""

// WiFi Channel: Channel 6 is standard 2.4 GHz channel with broad mobile device compatibility
#define WIFI_AP_CHANNEL         6

// Maximum concurrent WiFi client devices connected to this single node simultaneously
#define WIFI_MAX_CONNECTIONS    10

// Networking Ports
#define DNS_PORT                53  // UDP Port 53: Captive Portal DNS redirection
#define HTTP_PORT               80  // TCP Port 80: Standard Web Server port

// =====================================================================================
// SECTION 5: UNIQUE NODE IDENTITY
// Assigned dynamically in setup() from the ESP32 hardware MAC address eFuse.
// =====================================================================================
extern uint16_t g_nodeId;

#endif // CONFIG_H
