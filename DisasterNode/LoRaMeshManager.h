/*
 * =====================================================================================
 * File: LoRaMeshManager.h
 * Project: Heltec Wireless Stick Lite V3 - Disaster Recovery Multi-Node LoRa Network
 * Author: Antigravity AI / FYP Disaster Recovery Team
 * 
 * Description:
 *   LoRa Radio & Hybrid Mesh Network Manager using RadioLib for Semtech SX1262.
 *   
 *   Features:
 *   - Automatic Heltec V3 Vext power rail control (GPIO 36).
 *   - SX1262 LoRa radio initialization and CAD channel activity detection.
 *   - Message Deduplication Ring Buffer.
 *   - Controlled Flooding Relay (Decrements TTL and re-transmits if TTL > 1).
 *   - Anti-Packet / Rescuer Invalidation Broadcast & Mesh Relay (PKT_CLEAR_MESSAGES).
 *   - Rescuer Beacon Polling.
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
        
        pinMode(VEXT_CTRL_PIN, OUTPUT);
        digitalWrite(VEXT_CTRL_PIN, LOW); 
        delay(100);

        SPI.begin(LORA_SCK_PIN, LORA_MISO_PIN, LORA_MOSI_PIN, LORA_NSS_PIN);

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

        radio->setDio1Action(setLoRaRxFlag);
        startListening();

        return true;
    }

    void startListening() {
        if (!radio) return;
        int state = radio->startReceive();
        if (state != RADIOLIB_ERR_NONE) {
            Serial.printf("[LORA ERROR] Failed to start receive mode! Code: %d\n", state);
        }
    }

    void handleInterruptFlag() {
        packetReceivedFlag = true;
    }

    void update() {
        if (!packetReceivedFlag) return;
        packetReceivedFlag = false;

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

            processIncomingFrame(buffer, len);
        } else {
            Serial.printf("[LORA ERROR] Packet read failed with code: %d\n", state);
        }

        startListening();
    }

    /**
     * Transmits a binary byte array over LoRa with Channel Activity Detection (CAD).
     */
    bool sendRawPacket(const uint8_t* data, size_t len) {
        if (!radio) return false;

        Serial.println("[LORA TX] Checking channel activity (CAD)...");
        int cadState = radio->scanChannel();
        if (cadState == RADIOLIB_PREAMBLE_DETECTED) {
            Serial.println("[LORA TX] Channel busy! Backing off...");
            delay(random(200, 800));
        }

        digitalWrite(BOARD_LED_PIN, HIGH);
        int txState = radio->transmit((uint8_t*)data, len);
        digitalWrite(BOARD_LED_PIN, LOW);

        if (txState == RADIOLIB_ERR_NONE) {
            Serial.printf("[LORA TX SUCCESS] Sent %u bytes successfully.\n", len);
            startListening();
            return true;
        } else {
            Serial.printf("[LORA TX ERROR] Transmission failed! Error code: %d\n", txState);
            startListening();
            return false;
        }
    }

    /**
     * Broadcasts a victim DistressPayload across the mesh network.
     */
    bool broadcastDistressAlert(uint32_t msgId, const DistressPayload& payload) {
        uint8_t buffer[sizeof(PacketHeader) + sizeof(DistressPayload)];
        size_t pktSize = buildDistressPacket(buffer, msgId, g_nodeId, &payload);

        markMsgAsSeen(msgId);

        Serial.printf("[LORA MESH] Broadcasting Distress SOS [MsgID: 0x%08X] over LoRa (TTL: %d)...\n",
                      msgId, LORA_MAX_HOP_COUNT);

        return sendRawPacket(buffer, pktSize);
    }

    /**
     * Broadcasts an Anti-Packet (PKT_CLEAR_MESSAGES) to invalidate resolved alerts across the entire mesh.
     */
    bool broadcastClearAlert(const uint32_t* msgIds, uint8_t count, uint16_t rescuerId) {
        if (!msgIds || count == 0) return false;

        uint32_t uniquePktId = storage ? storage->getNextUniqueMsgId(g_nodeId) : micros();
        uint8_t buffer[sizeof(PacketHeader) + sizeof(ClearMessagePayload)];
        size_t pktSize = buildClearPacket(buffer, uniquePktId, rescuerId, msgIds, count);

        markMsgAsSeen(uniquePktId);

        Serial.printf("[LORA MESH] Broadcasting Anti-Packet [PktID: 0x%08X] clearing %u incidents...\n",
                      uniquePktId, count);

        return sendRawPacket(buffer, pktSize);
    }

    /**
     * Sends a Rescuer Beacon to poll nearby civilian nodes for their offline SOS queues.
     */
    bool sendRescuerBeacon() {
        PacketHeader header;
        header.magic        = PROTOCOL_MAGIC_BYTE;
        header.version      = PAYLOAD_SCHEMA_VERSION;
        header.pktType      = PKT_RESCUER_BEACON;
        header.msgId        = storage ? storage->getNextUniqueMsgId(g_nodeId) : micros();
        header.senderNodeId = g_nodeId;
        header.targetNodeId = LORA_BROADCAST_ADDR;
        header.ttl          = 1; // Single-hop local proximity poll
        header.payloadLen   = 0;

        markMsgAsSeen(header.msgId);
        Serial.printf("[LORA RESCUER] Sending Proximity Poll Beacon (0x%08X)...\n", header.msgId);

        return sendRawPacket((const uint8_t*)&header, sizeof(PacketHeader));
    }

    bool isDuplicateMsg(uint32_t msgId) {
        for (uint8_t i = 0; i < DEDUPLICATION_CACHE_SIZE; i++) {
            if (seenMsgCache[i] == msgId) return true;
        }
        return false;
    }

    void markMsgAsSeen(uint32_t msgId) {
        seenMsgCache[cacheIndex] = msgId;
        cacheIndex = (cacheIndex + 1) % DEDUPLICATION_CACHE_SIZE;
    }

    float getLastRssi() const { return lastRssi; }
    float getLastSnr()  const { return lastSnr; }

private:
    void processIncomingFrame(const uint8_t* buffer, size_t len) {
        PacketHeader header;
        if (!parsePacketHeader(buffer, len, &header)) {
            Serial.println("[LORA PROTOCOL] Dropped invalid packet (bad magic byte or short header).");
            return;
        }

        if (isDuplicateMsg(header.msgId)) {
            Serial.printf("[LORA MESH] Dropped duplicate packet [MsgID: 0x%08X]\n", header.msgId);
            return;
        }
        markMsgAsSeen(header.msgId);

        switch (header.pktType) {
            case PKT_DISTRESS_ALERT: {
                if (len < sizeof(PacketHeader) + sizeof(DistressPayload)) break;
                
                const DistressPayload* payload = (const DistressPayload*)(buffer + sizeof(PacketHeader));
                Serial.printf("[LORA SOS RX] Node: 0x%04X | Loc: %s | Victims: %d | Msg: %s\n",
                              header.senderNodeId, payload->floorRoom, payload->victimCount, payload->textMsg);

                if (storage) {
                    storage->saveUnackedMessage(header.msgId, *payload);
                }

                if (header.ttl > 1) {
                    relayMeshPacket(buffer, len);
                }
                break;
            }

            case PKT_CLEAR_MESSAGES: {
                if (len < sizeof(PacketHeader) + sizeof(ClearMessagePayload)) break;

                const ClearMessagePayload* clearPkt = (const ClearMessagePayload*)(buffer + sizeof(PacketHeader));
                Serial.printf("[LORA CLEAR RX] Anti-Packet received! Rescuer 0x%04X cleared %u incidents.\n",
                              clearPkt->rescuerNodeId, clearPkt->count);

                if (storage) {
                    storage->batchMarkMessagesResolved(clearPkt->clearedMsgIds, clearPkt->count,
                                                       clearPkt->rescuerNodeId, clearPkt->clearTimestamp);
                }

                // Relay Anti-Packet across mesh so all building nodes clear their backlogs
                if (header.ttl > 1) {
                    relayMeshPacket(buffer, len);
                }
                break;
            }

            case PKT_DISTRESS_ACK: {
                if (len < sizeof(PacketHeader) + sizeof(AckPayload)) break;
                
                const AckPayload* ack = (const AckPayload*)(buffer + sizeof(PacketHeader));
                Serial.printf("[LORA ACK RX] MsgID 0x%08X acknowledged by Rescuer 0x%04X!\n",
                              ack->ackedMsgId, ack->rescuerNodeId);

                if (storage) {
                    storage->markMessageAcknowledged(ack->ackedMsgId);
                }

                if (header.targetNodeId != g_nodeId && header.targetNodeId != LORA_BROADCAST_ADDR && header.ttl > 1) {
                    relayMeshPacket(buffer, len);
                }
                break;
            }

            case PKT_RESCUER_BEACON: {
                Serial.printf("[LORA BEACON RX] Rescuer 0x%04X in range! Flushing unACKed SOS backlog...\n",
                              header.senderNodeId);
                flushOfflineStorageToRescuer();
                break;
            }

            default:
                Serial.printf("[LORA PROTOCOL] Unknown packet type: 0x%02X\n", header.pktType);
                break;
        }
    }

    void relayMeshPacket(const uint8_t* originalBuffer, size_t len) {
        uint8_t relayBuf[256];
        memcpy(relayBuf, originalBuffer, len);

        PacketHeader* hdr = (PacketHeader*)relayBuf;
        hdr->ttl--;

        Serial.printf("[LORA MESH RELAY] Relaying PktID 0x%08X (Type: 0x%02X, New TTL: %d)...\n",
                      hdr->msgId, hdr->pktType, hdr->ttl);

        delay(random(100, 450));
        sendRawPacket(relayBuf, len);
    }

    void flushOfflineStorageToRescuer() {
        if (!storage) return;

        size_t count = storage->getStoredMessageCount();
        Serial.printf("[LORA FLUSH] Found %u offline messages stored. Transmitting...\n", count);

        for (size_t i = 0; i < count; i++) {
            StoredMessage rec;
            if (storage->getStoredMessageByIndex(i, rec)) {
                broadcastDistressAlert(rec.msgId, rec.payload);
                delay(1200);
            }
        }
    }
};

#endif // LORA_MESH_MANAGER_H
