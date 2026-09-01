/*
 * =====================================================================================
 * File: LoRaMeshManager.h
 * Project: Heltec Wireless Stick Lite V3 - Disaster Recovery Multi-Node LoRa Network
 * Author: Antigravity AI / FYP Disaster Recovery Team
 * 
 * Description:
 *   LoRa Radio & Hybrid Mesh Network Manager using the RadioLib library for Semtech SX1262.
 *   
 *   Features:
 *   - Automatic Heltec V3 Vext power rail control (GPIO 36).
 *   - SX1262 LoRa radio initialization (Freq, SF, BW, CR, Sync Word, Output Power).
 *   - CSMA / CAD (Channel Activity Detection) before transmission to avoid packet collisions.
 *   - Message Deduplication Ring Buffer (cache recently relayed message IDs).
 *   - Controlled Flooding Relay (Decrements TTL and re-transmits if TTL > 1).
 *   - Non-blocking interrupt-driven packet reception.
 * =====================================================================================
 */

#ifndef LORA_MESH_MANAGER_H
#define LORA_MESH_MANAGER_H

#include <Arduino.h>
#include <SPI.h>
#include <RadioLib.h>
#include "Config.h"
#include "PacketDefs.h"
#include "StorageManager.h"

// Forward declaration of interrupt service routine
void IRAM_ATTR setLoRaRxFlag();

class LoRaMeshManager {
private:
    SX1262* radio;
    StorageManager* storage;
    
    // Deduplication cache ring buffer to store recent message IDs
    uint32_t seenMsgCache[DEDUPLICATION_CACHE_SIZE];
    uint8_t  cacheIndex;

    // Last received packet RSSI & SNR metrics for link quality monitoring
    float lastRssi;
    float lastSnr;

    // Flag set by hardware DIO1 interrupt when a radio packet arrives
    volatile bool packetReceivedFlag;

public:
    LoRaMeshManager(StorageManager* storageMgr) 
        : radio(nullptr), storage(storageMgr), cacheIndex(0), lastRssi(0), lastSnr(0), packetReceivedFlag(false) {
        memset(seenMsgCache, 0, sizeof(seenMsgCache));
    }

    /**
     * Powers up the Heltec V3 LoRa radio rail and configures the SX1262 chip via RadioLib.
     */
    bool begin() {
        Serial.println("[LORA] Powering up Heltec V3 Vext power rail (GPIO 36)...");
        
        // Heltec V3 Vext Control: GPIO 36 MUST be driven LOW to activate 3.3V power to SX1262 radio!
        pinMode(VEXT_CTRL_PIN, OUTPUT);
        digitalWrite(VEXT_CTRL_PIN, LOW); 
        delay(100); // Allow power rail voltage to stabilize

        // Initialize SPI bus for ESP32-S3 pins
        SPI.begin(LORA_SCK_PIN, LORA_MISO_PIN, LORA_MOSI_PIN, LORA_NSS_PIN);

        // Instantiate RadioLib SX1262 Module
        // Module(cs, irq, rst, gpio)
        radio = new SX1262(new Module(LORA_NSS_PIN, LORA_DIO1_PIN, LORA_RESET_PIN, LORA_BUSY_PIN));

        Serial.println("[LORA] Initializing Semtech SX1262 module with RadioLib...");
        int state = radio->begin(
            LORA_FREQUENCY,
            LORA_BANDWIDTH,
            LORA_SPREADING_FACTOR,
            LORA_CODING_RATE,
            LORA_SYNC_WORD,
            LORA_OUTPUT_POWER,
            LORA_PREAMBLE_LENGTH
        );

        if (state != RADIOLIB_ERR_NONE) {
            Serial.printf("[LORA ERROR] SX1262 initialization failed! Error code: %d\n", state);
            return false;
        }

        Serial.printf("[LORA SUCCESS] Radio ready! Freq: %.1f MHz, SF: %d, BW: %.1f kHz, Power: %d dBm\n",
                      LORA_FREQUENCY, LORA_SPREADING_FACTOR, LORA_BANDWIDTH, LORA_OUTPUT_POWER);

        // Enable interrupt trigger on DIO1 rising edge when packet received
        radio->setDio1Action(setLoRaRxFlag);

        // Put radio into continuous background receive mode
        startListening();

        return true;
    }

    /**
     * Places the SX1262 into non-blocking reception mode.
     */
    void startListening() {
        if (!radio) return;
        int state = radio->startReceive();
        if (state != RADIOLIB_ERR_NONE) {
            Serial.printf("[LORA ERROR] Failed to start receive mode! Code: %d\n", state);
        }
    }

    /**
     * Called when hardware interrupt sets `packetReceivedFlag`.
     */
    void handleInterruptFlag() {
        packetReceivedFlag = true;
    }

    /**
     * Main background loop processing task for LoRa events. Call this in main `loop()`.
     */
    void update() {
        if (!packetReceivedFlag) return;
        packetReceivedFlag = false; // Reset flag

        // Read raw buffer from SX1262
        size_t len = radio->getPacketLength();
        if (len < sizeof(PacketHeader)) {
            startListening();
            return;
        }

        uint8_t buffer[256];
        int state = radio->readData(buffer, len);

        if (state == RADIOLIB_ERR_NONE) {
            lastRssi = radio->getRSSI();
            lastSnr  = radio->getSNR();
            
            Serial.printf("[LORA RX] Received frame (%d bytes) | RSSI: %.1f dBm | SNR: %.1f dB\n",
                          len, lastRssi, lastSnr);

            // Process packet through protocol state machine
            processIncomingFrame(buffer, len);
        } else {
            Serial.printf("[LORA ERROR] Packet read failed with error code: %d\n", state);
        }

        // Resume background reception
        startListening();
    }

    /**
     * Transmits a binary byte array over LoRa with Channel Activity Detection (CAD).
     */
    bool sendRawPacket(const uint8_t* data, size_t len) {
        if (!radio) return false;

        // Perform CAD (CSMA collision avoidance)
        Serial.println("[LORA TX] Checking channel activity (CAD)...");
        int cadState = radio->scanChannel();
        if (cadState == RADIOLIB_PREAMBLE_DETECTED) {
            Serial.println("[LORA TX] Channel busy! Waiting random delay before re-trying...");
            delay(random(200, 800)); // Random backoff
        }

        // Transmit packet synchronously
        digitalWrite(BOARD_LED_PIN, HIGH); // Flash LED during TX
        int txState = radio->transmit((uint8_t*)data, len);
        digitalWrite(BOARD_LED_PIN, LOW);

        if (txState == RADIOLIB_ERR_NONE) {
            Serial.printf("[LORA TX SUCCESS] Sent %u bytes successfully.\n", len);
            startListening(); // Re-enable RX mode
            return true;
        } else {
            Serial.printf("[LORA TX ERROR] Transmission failed! Error code: %d\n", txState);
            startListening();
            return false;
        }
    }

    /**
     * High-level function to broadcast a victim DistressPayload across the mesh network.
     */
    bool broadcastDistressAlert(uint32_t msgId, const DistressPayload& payload) {
        uint8_t buffer[sizeof(PacketHeader) + sizeof(DistressPayload)];
        size_t pktSize = buildDistressPacket(buffer, msgId, g_nodeId, &payload);

        // Add message ID to our own deduplication cache so we don't process our own echoed packet
        markMsgAsSeen(msgId);

        Serial.printf("[LORA MESH] Broadcasting Distress SOS [MsgID: 0x%08X] over LoRa (Hops remaining: %d)...\n",
                      msgId, LORA_MAX_HOP_COUNT);

        return sendRawPacket(buffer, pktSize);
    }

    /**
     * Checks if a message ID has been processed recently to prevent infinite relay loops.
     */
    bool isDuplicateMsg(uint32_t msgId) {
        for (uint8_t i = 0; i < DEDUPLICATION_CACHE_SIZE; i++) {
            if (seenMsgCache[i] == msgId) return true;
        }
        return false;
    }

    /**
     * Records a message ID into the ring buffer cache.
     */
    void markMsgAsSeen(uint32_t msgId) {
        seenMsgCache[cacheIndex] = msgId;
        cacheIndex = (cacheIndex + 1) % DEDUPLICATION_CACHE_SIZE;
    }

    /**
     * Returns link metrics for web dashboard.
     */
    float getLastRssi() const { return lastRssi; }
    float getLastSnr()  const { return lastSnr; }

private:
    /**
     * Protocol state machine for parsing and handling received LoRa frames.
     */
    void processIncomingFrame(const uint8_t* buffer, size_t len) {
        PacketHeader header;
        if (!parsePacketHeader(buffer, len, &header)) {
            Serial.println("[LORA PROTOCOL] Dropped invalid packet (bad magic byte or short header).");
            return;
        }

        // Check deduplication cache
        if (isDuplicateMsg(header.msgId)) {
            Serial.printf("[LORA MESH] Dropped duplicate packet [MsgID: 0x%08X]\n", header.msgId);
            return;
        }
        markMsgAsSeen(header.msgId);

        // Handle Packet Types
        switch (header.pktType) {
            case PKT_DISTRESS_ALERT: {
                if (len < sizeof(PacketHeader) + sizeof(DistressPayload)) break;
                
                const DistressPayload* payload = (const DistressPayload*)(buffer + sizeof(PacketHeader));
                Serial.printf("[LORA SOS RECEIVED] From Node: 0x%04X | Location: %s | Victims: %d | Msg: %s\n",
                              header.senderNodeId, payload->floorRoom, payload->victimCount, payload->textMsg);

                // Save received SOS to local storage history so rescuers connected to this node can view it!
                if (storage) {
                    storage->saveUnackedMessage(header.msgId, *payload);
                }

                // Relay Packet across mesh if TTL remaining > 1
                if (header.ttl > 1) {
                    relayMeshPacket(buffer, len);
                }
                break;
            }

            case PKT_DISTRESS_ACK: {
                if (len < sizeof(PacketHeader) + sizeof(AckPayload)) break;
                
                const AckPayload* ack = (const AckPayload*)(buffer + sizeof(PacketHeader));
                Serial.printf("[LORA ACK RECEIVED] Original MsgID 0x%08X acknowledged by Rescuer Node 0x%04X!\n",
                              ack->ackedMsgId, ack->rescuerNodeId);

                // Remove from pending offline flash storage queue
                if (storage) {
                    storage->markMessageAcknowledged(ack->ackedMsgId);
                }

                // If ACK target is another node, relay it forward
                if (header.targetNodeId != g_nodeId && header.targetNodeId != LORA_BROADCAST_ADDR && header.ttl > 1) {
                    relayMeshPacket(buffer, len);
                }
                break;
            }

            case PKT_RESCUER_BEACON: {
                Serial.printf("[LORA BEACON RECEIVED] Rescuer Node 0x%04X in range! Dumping stored offline SOS queue...\n",
                              header.senderNodeId);
                
                // Flush all stored offline SOS messages over LoRa
                flushOfflineStorageToRescuer();
                break;
            }

            default:
                Serial.printf("[LORA PROTOCOL] Unknown packet type: 0x%02X\n", header.pktType);
                break;
        }
    }

    /**
     * Decrements packet TTL and relays frame across the mesh network.
     */
    void relayMeshPacket(const uint8_t* originalBuffer, size_t len) {
        // Create modifiable copy of the packet
        uint8_t relayBuf[256];
        memcpy(relayBuf, originalBuffer, len);

        PacketHeader* hdr = (PacketHeader*)relayBuf;
        hdr->ttl--; // Decrement hop count

        Serial.printf("[LORA MESH RELAY] Relaying MsgID 0x%08X (New TTL: %d)...\n", hdr->msgId, hdr->ttl);

        // Random jitter delay before relaying to avoid simultaneous collision with other relaying nodes
        delay(random(100, 500));

        sendRawPacket(relayBuf, len);
    }

    /**
     * Iterates through stored offline messages in LittleFS and transmits them over LoRa.
     */
    void flushOfflineStorageToRescuer() {
        if (!storage) return;

        size_t count = storage->getStoredMessageCount();
        Serial.printf("[LORA FLUSH] Found %u offline messages stored in Flash. Transmitting to Rescuer...\n", count);

        for (size_t i = 0; i < count; i++) {
            StoredMessage rec;
            if (storage->getStoredMessageByIndex(i, rec)) {
                broadcastDistressAlert(rec.msgId, rec.payload);
                delay(1000); // 1 second gap between packet flushes
            }
        }
    }
};

#endif // LORA_MESH_MANAGER_H
