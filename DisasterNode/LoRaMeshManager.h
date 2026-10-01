/*
 * =====================================================================================
 * File: LoRaMeshManager.h
 * Project: Heltec Wireless Stick Lite V3 - Disaster Recovery Multi-Node LoRa Network
 * Author: Antigravity AI / FYP Disaster Recovery Team
 * 
 * Description:
 *   LoRa Radio & Hybrid Mesh Network Manager using RadioLib for Semtech SX1262.
 *   
 *   Key Capabilities & Mesh Networking Concepts:
 *   -----------------------------------------------------------------------------------
 *   1. Hardware Power Rail Control (Vext):
 *      Heltec boards power the SX1262 and external sensors via a P-channel MOSFET on
 *      GPIO 36. Driving GPIO 36 LOW supplies 3.3V to the radio.
 *      
 *   2. CAD (Channel Activity Detection / CSMA):
 *      Before transmitting a packet, the SX1262 performs CAD to check if another node
 *      in the mesh is currently transmitting preamble symbols. If the channel is busy,
 *      the node applies a random backoff delay (200-800ms) to avoid packet collisions.
 *      
 *   3. Controlled Flooding Relay (TTL-based Multi-hop):
 *      Packets originate with TTL = LORA_MAX_HOP_COUNT (3). Every intermediate node
 *      that receives the packet decrements TTL by 1 and re-transmits it after a short
 *      random jitter delay (100-450ms) to prevent simultaneous collision storms.
 *      When TTL reaches 1, relaying stops.
 *      
 *   4. Deduplication Ring Buffer Cache:
 *      To prevent endless circular loops (Node A -> Node B -> Node A -> Node B...),
 *      every node remembers the last 32 message IDs it has processed or relayed.
 *      If an incoming packet's msgId matches an entry in the cache, it is immediately dropped.
 *      
 *   5. Anti-Packet Mesh Invalidation (PKT_CLEAR_MESSAGES):
 *      When a rescuer marks an incident resolved, an Anti-Packet is broadcast through
 *      the mesh. Intermediate building nodes clear the incident from their offline
 *      LittleFS retransmission queue and relay the Anti-Packet to deeper nodes.
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

// Forward declaration of hardware interrupt service routine in DisasterNode.ino
void IRAM_ATTR setLoRaRxFlag();

class LoRaMeshManager {
private:
    SX1262* radio;              // RadioLib SX1262 module pointer
    StorageManager* storage;    // Pointer to persistent LittleFS/NVS storage manager
    
    // Deduplication ring buffer: Stores the last DEDUPLICATION_CACHE_SIZE (32) seen message IDs
    uint32_t seenMsgCache[DEDUPLICATION_CACHE_SIZE];
    uint8_t  cacheIndex;        // Circular pointer to the next insertion slot

    // Radio link diagnostics
    float lastRssi;             // Received Signal Strength Indicator in dBm (e.g. -65 dBm)
    float lastSnr;              // Signal-to-Noise Ratio in dB (e.g. +8.5 dB)

    // Hardware interrupt flag set by DIO1 rising edge
    volatile bool packetReceivedFlag;

    // Image Transmission Queue State
    uint32_t currentImageMsgId;
    uint16_t currentImageTotalChunks;
    uint16_t currentImageNextChunk;
    uint32_t lastImageChunkTxTime;
    bool     isTransmittingImage;

public:
    LoRaMeshManager(StorageManager* storageMgr) 
        : radio(nullptr), storage(storageMgr), cacheIndex(0), lastRssi(0), lastSnr(0), packetReceivedFlag(false),
          currentImageMsgId(0), currentImageTotalChunks(0), currentImageNextChunk(0), lastImageChunkTxTime(0), isTransmittingImage(false) {
        memset(seenMsgCache, 0, sizeof(seenMsgCache));
    }

    /**
     * Powers up the Heltec V3 LoRa radio rail and configures the SX1262 chip via RadioLib.
     */
    bool begin() {
        Serial.println("[LORA] Powering up Heltec V3 Vext power rail (GPIO 36)...");
        
        // Heltec V3 Vext Control: GPIO 36 MUST be driven LOW to supply 3.3V to the SX1262!
        pinMode(VEXT_CTRL_PIN, OUTPUT);
        digitalWrite(VEXT_CTRL_PIN, LOW); 
        delay(100); // Allow power rail voltage to stabilize

        // Initialize dedicated hardware SPI bus for ESP32-S3 pins
        SPI.begin(LORA_SCK_PIN, LORA_MISO_PIN, LORA_MOSI_PIN, LORA_NSS_PIN);

        // Instantiate RadioLib SX1262 Module: Module(cs, irq, rst, busy)
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

        // Attach hardware interrupt on DIO1 rising edge (packet received or TX complete)
        radio->setDio1Action(setLoRaRxFlag);

        // Put radio into continuous background receive mode
        startListening();

        return true;
    }

    /**
     * Places the SX1262 into non-blocking background receive mode.
     */
    void startListening() {
        if (!radio) return;
        int state = radio->startReceive();
        if (state != RADIOLIB_ERR_NONE) {
            Serial.printf("[LORA ERROR] Failed to start receive mode! Code: %d\n", state);
        }
    }

    /**
     * Called by the hardware DIO1 interrupt service routine (ISR).
     */
    void handleInterruptFlag() {
        packetReceivedFlag = true;
    }

    /**
     * Main background loop processing task for LoRa events. Call this in main loop().
     */
    void update() {
        // 1. Process Image Transmission Queue (only if radio is idle from RX)
        if (!packetReceivedFlag && isTransmittingImage && (millis() - lastImageChunkTxTime >= IMAGE_CHUNK_TX_INTERVAL_MS)) {
            processImageTxQueue();
        }

        if (!packetReceivedFlag) return;
        packetReceivedFlag = false; // Reset interrupt flag

        // Check length of received radio packet
        size_t len = radio->getPacketLength();
        if (len < sizeof(PacketHeader)) {
            startListening(); // Re-arm radio for next frame
            return;
        }

        uint8_t buffer[256];
        int state = radio->readData(buffer, len);

        if (state == RADIOLIB_ERR_NONE) {
            lastRssi = radio->getRSSI();
            lastSnr  = radio->getSNR();
            
            Serial.printf("[LORA RX] Received frame (%d bytes) | RSSI: %.1f dBm | SNR: %.1f dB\n",
                          len, lastRssi, lastSnr);

            // Pass packet to protocol state machine
            processIncomingFrame(buffer, len);
        } else {
            Serial.printf("[LORA ERROR] Packet read failed with code: %d\n", state);
        }

        // Always re-arm the radio in continuous receive mode after handling an event
        startListening();
    }

    /**
     * Transmits a binary byte array over LoRa with Channel Activity Detection (CAD).
     */
    bool sendRawPacket(const uint8_t* data, size_t len) {
        if (!radio) return false;

        // Perform CAD (CSMA collision avoidance check)
        Serial.println("[LORA TX] Checking channel activity (CAD)...");
        int cadState = radio->scanChannel();
        if (cadState == RADIOLIB_PREAMBLE_DETECTED) {
            Serial.println("[LORA TX] Channel busy! Backing off with random delay...");
            delay(random(200, 800)); // Random backoff to avoid synchronized collisions
        }

        // Visual indicator: Flash user LED during transmission
        digitalWrite(BOARD_LED_PIN, HIGH);
        int txState = radio->transmit((uint8_t*)data, len);
        digitalWrite(BOARD_LED_PIN, LOW);

        if (txState == RADIOLIB_ERR_NONE) {
            Serial.printf("[LORA TX SUCCESS] Sent %u bytes successfully.\n", len);
            startListening(); // Re-enable receive mode
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

        // Record message ID in deduplication cache so this node drops its own reflected echo
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

        // Generate a unique packet ID for this Anti-Packet to prevent relay loops
        uint32_t uniquePktId = storage ? storage->getNextUniqueMsgId(g_nodeId) : micros();
        uint8_t buffer[sizeof(PacketHeader) + sizeof(ClearMessagePayload)];
        size_t pktSize = buildClearPacket(buffer, uniquePktId, rescuerId, msgIds, count);

        markMsgAsSeen(uniquePktId);

        Serial.printf("[LORA MESH] Broadcasting Anti-Packet [PktID: 0x%08X] clearing %u incidents...\n",
                      uniquePktId, count);

        return sendRawPacket(buffer, pktSize);
    }

    /**
     * Sends a single-hop Rescuer Proximity Beacon to command nearby building nodes
     * to dump their stored offline SOS queues.
     */
    bool sendRescuerBeacon() {
        PacketHeader header;
        header.magic        = PROTOCOL_MAGIC_BYTE;
        header.version      = PAYLOAD_SCHEMA_VERSION;
        header.pktType      = PKT_RESCUER_BEACON;
        header.msgId        = storage ? storage->getNextUniqueMsgId(g_nodeId) : micros();
        header.senderNodeId = g_nodeId;
        header.targetNodeId = LORA_BROADCAST_ADDR;
        header.ttl          = 1; // Proximity beacon: 1 hop only
        header.payloadLen   = 0;

        markMsgAsSeen(header.msgId);
        Serial.printf("[LORA RESCUER] Sending Proximity Poll Beacon (0x%08X)...\n", header.msgId);

        return sendRawPacket((const uint8_t*)&header, sizeof(PacketHeader));
    }

    /**
     * Checks if a message ID has been seen recently in the circular ring buffer.
     */
    bool isDuplicateMsg(uint32_t msgId) {
        for (uint8_t i = 0; i < DEDUPLICATION_CACHE_SIZE; i++) {
            if (seenMsgCache[i] == msgId) return true;
        }
        return false;
    }

    /**
     * Records a message ID into the circular ring buffer cache.
     */
    void markMsgAsSeen(uint32_t msgId) {
        seenMsgCache[cacheIndex] = msgId;
        cacheIndex = (cacheIndex + 1) % DEDUPLICATION_CACHE_SIZE;
    }

    /**
     * Queues an image in LittleFS to be chunked and transmitted sequentially over LoRa.
     */
    void queueImageTransmission(uint32_t msgId) {
        if (!storage || !storage->hasImage(msgId)) return;
        File f = storage->getFullImageFile(msgId);
        if (!f) return;
        size_t size = f.size();
        f.close();
        
        currentImageMsgId = msgId;
        currentImageTotalChunks = (size + MAX_IMAGE_CHUNK_DATA_LEN - 1) / MAX_IMAGE_CHUNK_DATA_LEN;
        currentImageNextChunk = 0;
        lastImageChunkTxTime = 0;
        isTransmittingImage = true;
        
        Serial.printf("[LORA TX QUEUE] Queued image 0x%08X (%u bytes, %u chunks)\n", msgId, size, currentImageTotalChunks);
    }

    float getLastRssi() const { return lastRssi; }
    float getLastSnr()  const { return lastSnr; }

private:
    /**
     * Reads the next image chunk from storage and transmits it.
     */
    void processImageTxQueue() {
        File f = storage->getFullImageFile(currentImageMsgId);
        if (!f) {
            Serial.println("[LORA TX IMAGE ERROR] Could not open image file. Aborting.");
            isTransmittingImage = false;
            return;
        }

        f.seek(currentImageNextChunk * MAX_IMAGE_CHUNK_DATA_LEN);
        ImageChunkPayload chunkPayload;
        chunkPayload.imageMsgId = currentImageMsgId;
        chunkPayload.chunkIndex = currentImageNextChunk;
        chunkPayload.totalChunks = currentImageTotalChunks;
        
        size_t bytesRead = f.read(chunkPayload.chunkData, MAX_IMAGE_CHUNK_DATA_LEN);
        chunkPayload.chunkDataLen = bytesRead;
        f.close();
        
        uint8_t txBuf[256];
        uint32_t chunkPktId = storage ? storage->getNextUniqueMsgId(g_nodeId) : micros();
        size_t pktSize = buildImageChunkPacket(txBuf, chunkPktId, g_nodeId, &chunkPayload);
        
        markMsgAsSeen(chunkPktId);
        
        Serial.printf("[LORA TX IMAGE] Sending chunk %u/%u for MsgID 0x%08X...\n", 
                        currentImageNextChunk+1, currentImageTotalChunks, currentImageMsgId);
                        
        sendRawPacket(txBuf, pktSize);
        
        lastImageChunkTxTime = millis();
        currentImageNextChunk++;
        
        if (currentImageNextChunk >= currentImageTotalChunks) {
            Serial.println("[LORA TX IMAGE] Finished transmitting all chunks.");
            isTransmittingImage = false;
        }
    }

    /**
     * Protocol state machine for parsing and handling received LoRa frames.
     */
    void processIncomingFrame(const uint8_t* buffer, size_t len) {
        PacketHeader header;
        if (!parsePacketHeader(buffer, len, &header)) {
            Serial.println("[LORA PROTOCOL] Dropped invalid packet (bad magic byte or short header).");
            return;
        }

        // Deduplication Check: Drop packet if we have already received or relayed it
        if (isDuplicateMsg(header.msgId)) {
            Serial.printf("[LORA MESH] Dropped duplicate packet [MsgID: 0x%08X]\n", header.msgId);
            return;
        }
        markMsgAsSeen(header.msgId);

        // Protocol Dispatcher by Packet Type
        switch (header.pktType) {
            case PKT_DISTRESS_ALERT: {
                if (len < sizeof(PacketHeader) + sizeof(DistressPayload)) break;
                
                const DistressPayload* payload = (const DistressPayload*)(buffer + sizeof(PacketHeader));
                Serial.printf("[LORA SOS RX] Node: 0x%04X | Loc: %s | Victims: %d | Msg: %s\n",
                              header.senderNodeId, payload->floorRoom, payload->victimCount, payload->textMsg);

                // Save to local flash memory so rescuers connecting to this node can view it
                if (storage) {
                    storage->saveUnackedMessage(header.msgId, *payload);
                }

                // Controlled Flooding Relay: Re-transmit across mesh if hops remain
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

                // Invalidate matching alerts from pending queue and update history log
                if (storage) {
                    storage->batchMarkMessagesResolved(clearPkt->clearedMsgIds, clearPkt->count,
                                                       clearPkt->rescuerNodeId, clearPkt->clearTimestamp);
                }

                // Relay Anti-Packet across mesh so deeper nodes also clear their backlogs
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

                // Remove from local offline pending queue
                if (storage) {
                    storage->markMessageAcknowledged(ack->ackedMsgId);
                }

                // If ACK target is another node deeper in the building, relay it forward
                if (header.targetNodeId != g_nodeId && header.targetNodeId != LORA_BROADCAST_ADDR && header.ttl > 1) {
                    relayMeshPacket(buffer, len);
                }
                break;
            }

            case PKT_RESCUER_BEACON: {
                Serial.printf("[LORA BEACON RX] Rescuer 0x%04X in range! Flushing unACKed SOS backlog...\n",
                              header.senderNodeId);
                // Dump all stored offline alerts over radio to the rescuer
                flushOfflineStorageToRescuer();
                break;
            }

            case PKT_IMAGE_CHUNK: {
                if (len < sizeof(PacketHeader) + sizeof(ImageChunkPayload)) break;
                
                const ImageChunkPayload* chunk = (const ImageChunkPayload*)(buffer + sizeof(PacketHeader));
                Serial.printf("[LORA IMAGE RX] Rcvd Chunk %u/%u for MsgID 0x%08X (Len: %u)\n",
                              chunk->chunkIndex + 1, chunk->totalChunks, chunk->imageMsgId, chunk->chunkDataLen);

                if (storage) {
                    storage->saveImageChunk(chunk->imageMsgId, chunk->chunkIndex, chunk->chunkData, chunk->chunkDataLen);
                }

                // Relay chunk across mesh ONLY from intermediate (civilian) nodes.
                // The rescuer node MUST NOT relay image chunks: the blocking relay TX
                // (random jitter delay + CAD + transmit) takes 300–800ms, which is longer
                // than the civilian's IMAGE_CHUNK_TX_INTERVAL_MS (250ms). This causes the
                // next chunk to arrive while the radio is still transmitting, dropping it
                // silently at the hardware layer — resulting in only even-indexed chunks
                // being received on the rescuer.
if (CURRENT_NODE_ROLE == ROLE_CIVILIAN) {
                if (header.ttl > 1) {
                    relayMeshPacket(buffer, len);
                }
            }

                break;
            }

            default:
                Serial.printf("[LORA PROTOCOL] Unknown packet type: 0x%02X\n", header.pktType);
                break;
        }
    }

    /**
     * Decrements packet TTL and relays the frame across the mesh network with random jitter.
     */
    void relayMeshPacket(const uint8_t* originalBuffer, size_t len) {
        uint8_t relayBuf[256];
        memcpy(relayBuf, originalBuffer, len);

        PacketHeader* hdr = (PacketHeader*)relayBuf;
        hdr->ttl--; // Decrement hop counter

        Serial.printf("[LORA MESH RELAY] Relaying PktID 0x%08X (Type: 0x%02X, New TTL: %d)...\n",
                      hdr->msgId, hdr->pktType, hdr->ttl);

        // Random jitter delay (100-450ms) prevents all neighbor nodes from broadcasting at the exact same millisecond
        delay(random(100, 450));
        sendRawPacket(relayBuf, len);
    }

    /**
     * Iterates through all stored offline messages in LittleFS and transmits them over LoRa.
     */
    void flushOfflineStorageToRescuer() {
        if (!storage) return;

        size_t count = storage->getStoredMessageCount();
        Serial.printf("[LORA FLUSH] Found %u offline messages stored. Transmitting...\n", count);

        for (size_t i = 0; i < count; i++) {
            StoredMessage rec;
            if (storage->getStoredMessageByIndex(i, rec)) {
                broadcastDistressAlert(rec.msgId, rec.payload);
                delay(1200); // 1.2-second pause between frames to prevent channel congestion
            }
        }
    }
};

#endif // LORA_MESH_MANAGER_H
