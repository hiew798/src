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

    // Passive Link Monitoring
    uint32_t lastNeighborSeenTime;

    // Asynchronous Relay Queue State
    struct RelayQueueItem {
        uint8_t  buffer[256];
        size_t   len;
        uint32_t scheduledTxTime;
    };
    static const uint8_t RELAY_QUEUE_CAPACITY = 4;
    RelayQueueItem relayQueue[RELAY_QUEUE_CAPACITY];
    uint8_t relayQueueHead;
    uint8_t relayQueueTail;
    uint8_t relayQueueCount;

    // SOS Retransmission (ARQ) State for Civilian Nodes
    uint32_t        pendingSosMsgId;
    DistressPayload pendingSosPayload;
    uint8_t         pendingSosRetries;
    uint32_t        lastSosTxTime;
    bool            hasPendingSos;

    // Image Transmission & Selective NACK State
    enum ImageTxState {
        IMG_STATE_IDLE = 0,
        IMG_STATE_SENDING_CHUNKS,
        IMG_STATE_WAIT_ACK_NACK,
        IMG_STATE_RETRYING_MISSING
    };
    ImageTxState imageTxState;
    uint32_t     currentImageMsgId;
    uint16_t     currentImageTotalChunks;
    uint16_t     currentImageNextChunk;
    uint32_t     lastImageChunkTxTime;
    uint8_t      imageRetryRound;
    uint16_t     missingChunksToRetransmit[MAX_NACK_CHUNKS_PER_PKT];
    uint8_t      missingChunksCount;
    uint8_t      missingChunksIndex;

    // Rescuer Incoming Image Tracking
    uint32_t     rxImageMsgId;
    uint16_t     rxImageTotalChunks;
    bool         rxChunkMap[64];

public:
    LoRaMeshManager(StorageManager* storageMgr) 
        : radio(nullptr), storage(storageMgr), cacheIndex(0), lastRssi(0), lastSnr(0), packetReceivedFlag(false),
          lastNeighborSeenTime(0), relayQueueHead(0), relayQueueTail(0), relayQueueCount(0),
          pendingSosMsgId(0), pendingSosRetries(0), lastSosTxTime(0), hasPendingSos(false),
          imageTxState(IMG_STATE_IDLE), currentImageMsgId(0), currentImageTotalChunks(0), currentImageNextChunk(0),
          lastImageChunkTxTime(0), imageRetryRound(0), missingChunksCount(0), missingChunksIndex(0),
          rxImageMsgId(0), rxImageTotalChunks(0) {
        memset(seenMsgCache, 0, sizeof(seenMsgCache));
        memset(&pendingSosPayload, 0, sizeof(pendingSosPayload));
        memset(missingChunksToRetransmit, 0, sizeof(missingChunksToRetransmit));
        memset(rxChunkMap, 0, sizeof(rxChunkMap));
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
        // 1. Process Image Transmission State Machine (only if radio is idle from RX)
        if (!packetReceivedFlag && imageTxState != IMG_STATE_IDLE) {
            processImageTxQueue();
        }

        // 2. Process Asynchronous Relay Queue (only if radio is idle and not transmitting images)
        if (!packetReceivedFlag && imageTxState == IMG_STATE_IDLE && relayQueueCount > 0) {
            processRelayQueue();
        }

        // 3. Process SOS Retries (ARQ) for civilian nodes
        if (!packetReceivedFlag && CURRENT_NODE_ROLE == ROLE_CIVILIAN && hasPendingSos && imageTxState == IMG_STATE_IDLE && relayQueueCount == 0) {
            processSosRetries();
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
     * Broadcasts a victim DistressPayload across the mesh network and registers it for ARQ.
     */
    bool broadcastDistressAlert(uint32_t msgId, const DistressPayload& payload) {
        if (CURRENT_NODE_ROLE == ROLE_CIVILIAN) {
            pendingSosMsgId = msgId;
            pendingSosPayload = payload;
            pendingSosRetries = 0;
            lastSosTxTime = millis();
            hasPendingSos = true;
        }
        return sendDistressAlertRaw(msgId, payload);
    }

    /**
     * Sends raw Distress SOS frame over LoRa without modifying ARQ tracking.
     */
    bool sendDistressAlertRaw(uint32_t msgId, const DistressPayload& payload) {
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
     * Queues an image in LittleFS to be chunked and transmitted sequentially over LoRa with NACK support.
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
        imageRetryRound = 0;
        missingChunksCount = 0;
        missingChunksIndex = 0;
        imageTxState = IMG_STATE_SENDING_CHUNKS;
        
        Serial.printf("[LORA TX QUEUE] Queued image 0x%08X (%u bytes, %u chunks, paced interval: %ums)\n",
                      msgId, size, currentImageTotalChunks, IMAGE_CHUNK_TX_INTERVAL_MS);
    }

    bool isNeighborInRange() const {
        return (lastNeighborSeenTime > 0) && (millis() - lastNeighborSeenTime <= NODE_IN_RANGE_TIMEOUT_MS);
    }

    bool isTransmittingImage() const {
        return imageTxState != IMG_STATE_IDLE;
    }

    float getLastRssi() const { return lastRssi; }
    float getLastSnr()  const { return lastSnr; }

private:
    /**
     * Checks if pending SOS alert should be retransmitted (up to 3 retries if neighbor in range).
     */
    void processSosRetries() {
        if (!hasPendingSos) return;

        if (millis() - lastSosTxTime >= LORA_RETRY_DELAY_MS) {
            if (isNeighborInRange() && pendingSosRetries < LORA_MAX_RETRIES) {
                pendingSosRetries++;
                lastSosTxTime = millis();
                Serial.printf("[SOS ARQ] Retrying SOS 0x%08X (Attempt %u/%u, neighbor in range)...\n",
                              pendingSosMsgId, pendingSosRetries, LORA_MAX_RETRIES);
                sendDistressAlertRaw(pendingSosMsgId, pendingSosPayload);
            } else {
                if (!isNeighborInRange()) {
                    Serial.printf("[SOS ARQ] No neighbor in range for SOS 0x%08X. Halting active retries (kept in flash).\n",
                                  pendingSosMsgId);
                } else {
                    Serial.printf("[SOS ARQ] Exhausted %u retries for SOS 0x%08X. Halting active retries (kept in flash).\n",
                                  LORA_MAX_RETRIES, pendingSosMsgId);
                }
                hasPendingSos = false;
            }
        }
    }

    /**
     * Relays queued packets non-blockingly once their random jitter delay expires.
     */
    void processRelayQueue() {
        if (relayQueueCount == 0) return;
        RelayQueueItem& item = relayQueue[relayQueueHead];
        if (millis() >= item.scheduledTxTime) {
            PacketHeader* hdr = (PacketHeader*)item.buffer;
            Serial.printf("[LORA MESH RELAY] Relaying queued PktID 0x%08X (Type: 0x%02X, TTL: %d)...\n",
                          hdr->msgId, hdr->pktType, hdr->ttl);
            sendRawPacket(item.buffer, item.len);
            relayQueueHead = (relayQueueHead + 1) % RELAY_QUEUE_CAPACITY;
            relayQueueCount--;
        }
    }

    /**
     * Enqueues a packet for asynchronous relay with random jitter (100-350ms).
     */
    void queueMeshRelay(const uint8_t* originalBuffer, size_t len) {
        if (CURRENT_NODE_ROLE != ROLE_CIVILIAN) return;

        if (relayQueueCount >= RELAY_QUEUE_CAPACITY) {
            Serial.println("[LORA MESH RELAY] Queue full, dropping oldest relay frame.");
            relayQueueHead = (relayQueueHead + 1) % RELAY_QUEUE_CAPACITY;
            relayQueueCount--;
        }

        RelayQueueItem& item = relayQueue[relayQueueTail];
        memcpy(item.buffer, originalBuffer, len);
        item.len = len;

        PacketHeader* hdr = (PacketHeader*)item.buffer;
        hdr->ttl--; // Decrement hop counter

        item.scheduledTxTime = millis() + random(100, 350);

        relayQueueTail = (relayQueueTail + 1) % RELAY_QUEUE_CAPACITY;
        relayQueueCount++;

        Serial.printf("[LORA MESH RELAY] Enqueued PktID 0x%08X (Type: 0x%02X, New TTL: %d, Delay: %lums)\n",
                      hdr->msgId, hdr->pktType, hdr->ttl, (unsigned long)(item.scheduledTxTime - millis()));
    }

    /**
     * Reads a single chunk from flash and broadcasts it.
     */
    void sendSingleImageChunk(uint32_t msgId, uint16_t chunkIndex, uint16_t totalChunks) {
        File f = storage->getFullImageFile(msgId);
        if (!f) {
            Serial.println("[LORA TX IMAGE ERROR] Could not open image file.");
            imageTxState = IMG_STATE_IDLE;
            return;
        }

        f.seek(chunkIndex * MAX_IMAGE_CHUNK_DATA_LEN);
        ImageChunkPayload chunkPayload;
        chunkPayload.imageMsgId = msgId;
        chunkPayload.chunkIndex = chunkIndex;
        chunkPayload.totalChunks = totalChunks;
        
        size_t bytesRead = f.read(chunkPayload.chunkData, MAX_IMAGE_CHUNK_DATA_LEN);
        chunkPayload.chunkDataLen = bytesRead;
        f.close();
        
        uint8_t txBuf[256];
        uint32_t chunkPktId = storage ? storage->getNextUniqueMsgId(g_nodeId) : micros();
        size_t pktSize = buildImageChunkPacket(txBuf, chunkPktId, g_nodeId, &chunkPayload);
        
        markMsgAsSeen(chunkPktId);
        
        Serial.printf("[LORA TX IMAGE] Sending chunk %u/%u for MsgID 0x%08X (Len: %u)...\n", 
                      chunkIndex + 1, totalChunks, msgId, (unsigned int)bytesRead);
                        
        sendRawPacket(txBuf, pktSize);
    }

    /**
     * Broadcasts an image poll packet asking the receiver to ACK or NACK.
     */
    void sendImagePoll(uint32_t imageMsgId, uint16_t totalChunks) {
        uint8_t txBuf[sizeof(PacketHeader) + sizeof(ImagePollPayload)];
        uint32_t pollPktId = storage ? storage->getNextUniqueMsgId(g_nodeId) : micros();
        size_t pktSize = buildImagePollPacket(txBuf, pollPktId, g_nodeId, imageMsgId, totalChunks);
        markMsgAsSeen(pollPktId);

        Serial.printf("[LORA TX IMAGE] Sending Image Poll for MsgID 0x%08X (Round %u/%u)...\n",
                      imageMsgId, imageRetryRound + 1, IMAGE_MAX_RETRY_ROUNDS);
        sendRawPacket(txBuf, pktSize);
    }

    /**
     * State machine for chunk transmission, burst polling, and selective NACK retransmission.
     */
    void processImageTxQueue() {
        if (imageTxState == IMG_STATE_SENDING_CHUNKS) {
            if (millis() - lastImageChunkTxTime < IMAGE_CHUNK_TX_INTERVAL_MS) return;

            if (currentImageNextChunk < currentImageTotalChunks) {
                sendSingleImageChunk(currentImageMsgId, currentImageNextChunk, currentImageTotalChunks);
                lastImageChunkTxTime = millis();
                currentImageNextChunk++;
            } else {
                // Sent all chunks! Poll rescuer for ACK or NACK
                sendImagePoll(currentImageMsgId, currentImageTotalChunks);
                imageTxState = IMG_STATE_WAIT_ACK_NACK;
                lastImageChunkTxTime = millis();
            }
        } else if (imageTxState == IMG_STATE_RETRYING_MISSING) {
            if (millis() - lastImageChunkTxTime < IMAGE_CHUNK_TX_INTERVAL_MS) return;

            if (missingChunksIndex < missingChunksCount) {
                uint16_t chunkIdx = missingChunksToRetransmit[missingChunksIndex];
                Serial.printf("[LORA TX IMAGE RETRY] Retransmitting missing chunk %u (%u/%u)...\n",
                              chunkIdx + 1, missingChunksIndex + 1, missingChunksCount);
                sendSingleImageChunk(currentImageMsgId, chunkIdx, currentImageTotalChunks);
                lastImageChunkTxTime = millis();
                missingChunksIndex++;
            } else {
                // Sent all missing chunks! Re-poll
                sendImagePoll(currentImageMsgId, currentImageTotalChunks);
                imageTxState = IMG_STATE_WAIT_ACK_NACK;
                lastImageChunkTxTime = millis();
            }
        } else if (imageTxState == IMG_STATE_WAIT_ACK_NACK) {
            if (millis() - lastImageChunkTxTime >= IMAGE_ACK_WAIT_TIMEOUT_MS) {
                if (isNeighborInRange() && imageRetryRound < IMAGE_MAX_RETRY_ROUNDS) {
                    imageRetryRound++;
                    lastImageChunkTxTime = millis();
                    Serial.printf("[LORA TX IMAGE] Timeout waiting for ACK/NACK. Re-polling (Attempt %u/%u)...\n",
                                  imageRetryRound + 1, IMAGE_MAX_RETRY_ROUNDS);
                    sendImagePoll(currentImageMsgId, currentImageTotalChunks);
                } else {
                    if (!isNeighborInRange()) {
                        Serial.println("[LORA TX IMAGE] No neighbor in range. Aborting image transfer.");
                    } else {
                        Serial.println("[LORA TX IMAGE] Max retry rounds reached without ACK. Aborting.");
                    }
                    imageTxState = IMG_STATE_IDLE;
                }
            }
        }
    }

    /**
     * Rescuer sends point-to-point ACK for SOS alert.
     */
    void sendDistressAck(uint32_t targetMsgId, uint16_t targetNodeId) {
        uint8_t ackBuf[sizeof(PacketHeader) + sizeof(AckPayload)];
        size_t ackSize = buildAckPacket(ackBuf, targetMsgId, g_nodeId, targetNodeId);
        markMsgAsSeen(micros());
        Serial.printf("[LORA RESCUER] Sending ACK for MsgID 0x%08X to Node 0x%04X\n", targetMsgId, targetNodeId);
        sendRawPacket(ackBuf, ackSize);
    }

    /**
     * Rescuer handles incoming Image Poll by returning either IMAGE_ACK or IMAGE_NACK.
     */
    void handleImagePoll(uint32_t imageMsgId, uint16_t totalChunks, uint16_t senderNodeId) {
        uint16_t missing[MAX_NACK_CHUNKS_PER_PKT];
        uint8_t missingCount = 0;

        if (rxImageMsgId == imageMsgId) {
            uint16_t checkLimit = totalChunks < 64 ? totalChunks : 64;
            for (uint16_t i = 0; i < checkLimit; i++) {
                if (!rxChunkMap[i]) {
                    if (missingCount < MAX_NACK_CHUNKS_PER_PKT) {
                        missing[missingCount++] = i;
                    }
                }
            }
        } else {
            uint16_t checkLimit = totalChunks < MAX_NACK_CHUNKS_PER_PKT ? totalChunks : MAX_NACK_CHUNKS_PER_PKT;
            for (uint16_t i = 0; i < checkLimit; i++) {
                missing[missingCount++] = i;
            }
        }

        if (missingCount == 0 && rxImageMsgId == imageMsgId) {
            Serial.printf("[LORA RESCUER] Full image 0x%08X verified! Sending IMAGE_ACK to Node 0x%04X\n",
                          imageMsgId, senderNodeId);
            uint8_t txBuf[sizeof(PacketHeader) + sizeof(ImageAckPayload)];
            uint32_t ackPktId = storage ? storage->getNextUniqueMsgId(g_nodeId) : micros();
            size_t pktSize = buildImageAckPacket(txBuf, ackPktId, g_nodeId, imageMsgId, senderNodeId);
            markMsgAsSeen(ackPktId);
            sendRawPacket(txBuf, pktSize);
        } else {
            Serial.printf("[LORA RESCUER] Image 0x%08X missing %u chunks. Sending IMAGE_NACK to Node 0x%04X\n",
                          imageMsgId, missingCount, senderNodeId);
            uint8_t txBuf[sizeof(PacketHeader) + sizeof(ImageNackPayload)];
            uint32_t nackPktId = storage ? storage->getNextUniqueMsgId(g_nodeId) : micros();
            size_t pktSize = buildImageNackPacket(txBuf, nackPktId, g_nodeId, imageMsgId, senderNodeId, missing, missingCount);
            markMsgAsSeen(nackPktId);
            sendRawPacket(txBuf, pktSize);
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

        bool knownPacket = true;

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

                // If Rescuer: send point-to-point ACK back to the reporting victim node
                if (CURRENT_NODE_ROLE == ROLE_RESCUER) {
                    sendDistressAck(header.msgId, header.senderNodeId);
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
                break;
            }

            case PKT_DISTRESS_ACK: {
                if (len < sizeof(PacketHeader) + sizeof(AckPayload)) break;
                
                const AckPayload* ack = (const AckPayload*)(buffer + sizeof(PacketHeader));
                Serial.printf("[LORA ACK RX] MsgID 0x%08X acknowledged by Rescuer 0x%04X!\n",
                              ack->ackedMsgId, ack->rescuerNodeId);

                // Check if this matches our active pending SOS
                if (hasPendingSos && ack->ackedMsgId == pendingSosMsgId) {
                    Serial.printf("[SOS ARQ] Confirmed receipt of SOS 0x%08X! Stopping retries.\n", pendingSosMsgId);
                    hasPendingSos = false;
                }

                // Remove from local offline pending queue
                if (storage) {
                    storage->markMessageAcknowledged(ack->ackedMsgId);
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

                // If Rescuer: track received chunk in bitmask
                if (CURRENT_NODE_ROLE == ROLE_RESCUER) {
                    if (rxImageMsgId != chunk->imageMsgId) {
                        rxImageMsgId = chunk->imageMsgId;
                        rxImageTotalChunks = chunk->totalChunks;
                        memset(rxChunkMap, 0, sizeof(rxChunkMap));
                    }
                    if (chunk->chunkIndex < 64) {
                        rxChunkMap[chunk->chunkIndex] = true;
                    }
                }

                if (storage) {
                    storage->saveImageChunk(chunk->imageMsgId, chunk->chunkIndex, chunk->chunkData, chunk->chunkDataLen);
                }
                break;
            }

            case PKT_IMAGE_POLL: {
                if (len < sizeof(PacketHeader) + sizeof(ImagePollPayload)) break;
                const ImagePollPayload* poll = (const ImagePollPayload*)(buffer + sizeof(PacketHeader));
                Serial.printf("[LORA IMAGE POLL RX] Poll for MsgID 0x%08X from Node 0x%04X (Total: %u chunks)\n",
                              poll->imageMsgId, poll->senderNodeId, poll->totalChunks);

                if (CURRENT_NODE_ROLE == ROLE_RESCUER) {
                    handleImagePoll(poll->imageMsgId, poll->totalChunks, poll->senderNodeId);
                }
                break;
            }

            case PKT_IMAGE_ACK: {
                if (len < sizeof(PacketHeader) + sizeof(ImageAckPayload)) break;
                const ImageAckPayload* ack = (const ImageAckPayload*)(buffer + sizeof(PacketHeader));
                Serial.printf("[LORA IMAGE ACK RX] Received Image ACK for MsgID 0x%08X from Rescuer 0x%04X!\n",
                              ack->imageMsgId, ack->rescuerNodeId);

                if (imageTxState == IMG_STATE_WAIT_ACK_NACK && ack->imageMsgId == currentImageMsgId) {
                    Serial.println("[LORA TX IMAGE SUCCESS] Image transfer fully confirmed by Rescuer!");
                    imageTxState = IMG_STATE_IDLE;
                }
                break;
            }

            case PKT_IMAGE_NACK: {
                if (len < sizeof(PacketHeader) + sizeof(ImageNackPayload)) break;
                const ImageNackPayload* nack = (const ImageNackPayload*)(buffer + sizeof(PacketHeader));
                Serial.printf("[LORA IMAGE NACK RX] Received Image NACK for MsgID 0x%08X (%u chunks missing)\n",
                              nack->imageMsgId, nack->missingCount);

                if (imageTxState == IMG_STATE_WAIT_ACK_NACK && nack->imageMsgId == currentImageMsgId) {
                    if (imageRetryRound < IMAGE_MAX_RETRY_ROUNDS && nack->missingCount > 0) {
                        imageRetryRound++;
                        missingChunksCount = nack->missingCount;
                        missingChunksIndex = 0;
                        for (uint8_t i = 0; i < missingChunksCount; i++) {
                            missingChunksToRetransmit[i] = nack->missingChunks[i];
                        }
                        imageTxState = IMG_STATE_RETRYING_MISSING;
                        lastImageChunkTxTime = millis();
                        Serial.printf("[LORA TX IMAGE] Entering retry round %u/%u to retransmit %u missing chunks.\n",
                                      imageRetryRound, IMAGE_MAX_RETRY_ROUNDS, missingChunksCount);
                    } else {
                        Serial.println("[LORA TX IMAGE] Exhausted max retry rounds or invalid NACK. Aborting image transfer.");
                        imageTxState = IMG_STATE_IDLE;
                    }
                }
                break;
            }

            default:
                Serial.printf("[LORA PROTOCOL] Unknown packet type: 0x%02X\n", header.pktType);
                knownPacket = false;
                break;
        }

        // Passive Link Monitoring: Any valid frame marks a neighbor in range
        lastNeighborSeenTime = millis();

        // Mesh Relaying for civilian nodes:
        // Relays recognized packets across the mesh if hops remain, unless addressed specifically to this node
        if (knownPacket && CURRENT_NODE_ROLE == ROLE_CIVILIAN) {
            bool addressedToSelf = (header.targetNodeId != LORA_BROADCAST_ADDR && header.targetNodeId == g_nodeId);
            if (!addressedToSelf && header.ttl > 1) {
                queueMeshRelay(buffer, len);
            }
        }
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
                sendDistressAlertRaw(rec.msgId, rec.payload);
                delay(1200); // 1.2-second pause between frames to prevent channel congestion
            }
        }
    }
};

#endif // LORA_MESH_MANAGER_H
