#pragma once
#include <Arduino.h>
#include "TransferConfig.h"

constexpr const char* FIRMWARE_REVISION = TRANSFER_MODE == TRANSFER_ADAPTIVE ? "adaptive-goodput-1" : "radio-rx-guard-2";

#define ROLE_CIVILIAN 0
#define ROLE_RESCUER 1
#ifndef CURRENT_NODE_ROLE
#define CURRENT_NODE_ROLE ROLE_CIVILIAN
#endif
#if CURRENT_NODE_ROLE != ROLE_CIVILIAN && CURRENT_NODE_ROLE != ROLE_RESCUER
#error CURRENT_NODE_ROLE must be ROLE_CIVILIAN or ROLE_RESCUER
#endif
// Keep these settings identical on every node in this mesh.
#define LORA_NSS_PIN 8
#define LORA_DIO1_PIN 14
#define LORA_RESET_PIN 12
#define LORA_BUSY_PIN 13
#define LORA_SCK_PIN 9
#define LORA_MISO_PIN 11
#define LORA_MOSI_PIN 10
#define VEXT_CTRL_PIN 36
#define BOARD_LED_PIN 35
#define LORA_FREQUENCY 915.0
#define LORA_BANDWIDTH 125.0
#define LORA_SPREADING_FACTOR 9
#define LORA_CODING_RATE 7
#define LORA_SYNC_WORD 0x12
#define LORA_OUTPUT_POWER 22
#define LORA_PREAMBLE_LENGTH 8
#define WIFI_AP_PASSWORD "" // Open network on both roles; no Wi-Fi password.
#define WIFI_AP_CHANNEL 6
// Enable ONCE only on a new board whose LittleFS partition has never been formatted.
// Leave false on boards containing reports. A failed mount must not erase their data.
#define FORMAT_FILESYSTEM_ON_MOUNT_FAILURE false
// HTTPS handlers can service LoRa and LittleFS while a request is in progress.
// The default Arduino 8192-byte loop stack overflowed on that combined path.
constexpr size_t LOOP_TASK_STACK_BYTES = 16 * 1024;
constexpr uint8_t MESH_HOPS = 3;
constexpr size_t MAX_RECORDS = 96;
constexpr size_t MAX_IMAGES = 8;
constexpr size_t MAX_IMAGE_BYTES = 8192;
constexpr size_t IMAGE_CHUNK_BYTES = 180;
constexpr uint32_t ACK_WAIT_MS = 15000;
constexpr uint32_t RETRY_PAUSE_MS = 120000;
constexpr uint8_t RETRIES_PER_BURST = 4;
constexpr uint32_t BEACON_INTERVAL_MS = 60000;
extern uint64_t g_nodeId;
