/*
 * =====================================================================================
 * File: StorageManager.h
 * Project: Heltec Wireless Stick Lite V3 - Disaster Recovery Multi-Node LoRa Network
 * Author: Antigravity AI / FYP Disaster Recovery Team
 * 
 * Description:
 *   Handles non-volatile flash memory storage using ESP32 LittleFS and NVS Preferences.
 *   
 *   Key Capabilities:
 *   -----------------------------------------------------------------------------------
 *   1. Collision-Proof Persistent Message IDs:
 *      Uses ESP32 NVS (Non-Volatile Storage) to persist a 16-bit sequence counter.
 *      Combined with the 16-bit factory-burned MAC eFuse, message IDs are globally
 *      unique and strictly monotonically increasing across power failures and crashes.
 *      
 *   2. Store-and-Forward Offline Backlog (/unsent_sos.bin):
 *      When an SOS is generated, it is stored in flash memory until an ACK or Anti-Packet
 *      is received. If rescuers are out of range, building nodes retain the alerts
 *      and dump them when a Rescuer Beacon is detected.
 *      
 *   3. Permanent Incident History (/sos_history.bin):
 *      Maintains a permanent log of all local and relayed distress alerts with complete
 *      lifecycle tracking (STATUS_PENDING -> STATUS_ACKNOWLEDGED -> STATUS_RESOLVED).
 *      
 *   4. Batch Anti-Packet Clearance (Purging):
 *      When an Anti-Packet is received over LoRa, matching IDs are atomically removed
 *      from the pending retransmission queue and marked RESOLVED in history.
 * =====================================================================================
 */

#ifndef STORAGE_MANAGER_H
#define STORAGE_MANAGER_H

#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>
#include <Preferences.h>
#include "Config.h"
#include "PacketDefs.h"

// Flash storage binary file paths in LittleFS partition
#define UNACKED_SOS_FILE  "/unsent_sos.bin" // Active queue of unACKed alerts pending retransmission
#define SOS_HISTORY_FILE  "/sos_history.bin" // Permanent historical audit log of all seen incidents

#pragma pack(push, 1)
// Binary record structure saved to LittleFS files
struct StoredMessage {
    uint32_t        msgId;               // Globally unique 32-bit Message ID
    DistressPayload payload;             // Full distress data struct
    uint32_t        timestampSaved;      // Local system millis() when stored
    uint8_t         retryCount;          // Retransmission attempt counter
    bool            acknowledged;        // True if ACK has been received
    uint8_t         status;              // MessageStatus enum (0=PENDING, 1=ACKED, 2=RESOLVED)
    uint16_t        resolvedByRescuerId; // Node ID of rescuer who cleared it
    uint32_t        resolvedTimestamp;   // Timestamp (in seconds) when cleared
};
#pragma pack(pop)

class StorageManager {
private:
    Preferences prefs; // ESP32 Non-Volatile Storage (NVS) abstraction

public:
    StorageManager() {}

    /**
     * Initializes the LittleFS flash storage file system.
     * Auto-formats the partition if unformatted or corrupted on first boot.
     */
    bool begin() {
        Serial.println("[STORAGE] Initializing LittleFS Flash File System...");
        
        // Passing 'true' auto-formats the partition if LittleFS mount fails
        if (!LittleFS.begin(true)) {
            Serial.println("[STORAGE ERROR] LittleFS Mount Failed!");
            return false;
        }

        Serial.printf("[STORAGE] LittleFS mounted successfully. Total: %u bytes, Used: %u bytes\n",
                      LittleFS.totalBytes(), LittleFS.usedBytes());

        // Ensure the pending offline SOS file exists
        if (!LittleFS.exists(UNACKED_SOS_FILE)) {
            File f = LittleFS.open(UNACKED_SOS_FILE, "w");
            if (f) f.close();
        }

        return true;
    }

    /**
     * Generates a collision-proof 32-bit Message ID using hardware MAC and persistent NVS counter.
     * 
     * NOTE ON THE MONOTONIC SEQUENCE FIX:
     * Previously, message IDs used a volatile RAM variable (`static uint16_t localSeq = 0`).
     * If a node lost power, ran out of battery, or rebooted, localSeq reset to 0, which could
     * cause duplicate message IDs and accidental clearance of unreceived messages!
     * 
     * By persisting the counter in ESP32 NVS flash using Preferences.h:
     * - The counter survives power loss, battery replacement, and firmware crashes.
     * - Upper 16 bits = ESP32 silicon MAC address (guarantees uniqueness across different boards).
     * - Lower 16 bits = Monotonically increasing NVS sequence counter (1, 2, 3, 4...).
     */
    uint32_t getNextUniqueMsgId(uint16_t nodeId) {
        prefs.begin("node_seq", false); // Open "node_seq" namespace in read/write mode
        uint16_t seq = prefs.getUShort("seq", 0) + 1;
        prefs.putUShort("seq", seq);    // Commit incremented sequence counter to flash
        prefs.end();

        uint32_t uniqueId = ((uint32_t)nodeId << 16) | seq;
        Serial.printf("[STORAGE] Generated Collision-Proof MsgID: 0x%08X (Node: 0x%04X, Seq: %u)\n",
                      uniqueId, nodeId, seq);
        return uniqueId;
    }

    /**
     * Saves a new distress message to flash memory for Store-and-Forward backup.
     * Prevents storing duplicates if the message is already present in the queue.
     */
    bool saveUnackedMessage(uint32_t msgId, const DistressPayload& payload) {
        if (isMessageAlreadyStored(msgId)) {
            return false;
        }

        size_t count = getStoredMessageCount();
        if (count >= MAX_STORED_OFFLINE_MESSAGES) {
            Serial.println("[STORAGE WARNING] Offline flash storage full! Cannot save new SOS.");
            return false;
        }

        File file = LittleFS.open(UNACKED_SOS_FILE, "a"); // Append mode
        if (!file) {
            Serial.println("[STORAGE ERROR] Failed to open unsent SOS file for writing!");
            return false;
        }

        StoredMessage record;
        memset(&record, 0, sizeof(StoredMessage));
        record.msgId               = msgId;
        record.payload             = payload;
        record.timestampSaved      = millis();
        record.retryCount          = 0;
        record.acknowledged        = false;
        record.status              = STATUS_PENDING;
        record.resolvedByRescuerId = 0;
        record.resolvedTimestamp   = 0;

        size_t written = file.write((const uint8_t*)&record, sizeof(StoredMessage));
        file.close();

        Serial.printf("[STORAGE] Saved offline SOS [MsgID: 0x%08X] to LittleFS (%u bytes written)\n", msgId, written);
        
        // Also record in permanent history log
        saveToHistory(record);
        return written == sizeof(StoredMessage);
    }

    /**
     * Atomically invalidates and clears a batch of resolved Message IDs across LittleFS storage.
     * 
     * How Anti-Packet Clearance Works:
     * 1. Scans the pending retransmission file (/unsent_sos.bin) and filters out any records
     *    matching the cleared IDs. Rewrites the file without those records so the node stops
     *    wasting LoRa airtime attempting to re-send resolved incidents.
     * 2. Opens the history log (/sos_history.bin) in read-write mode ("r+") and updates the
     *    matching record's status to STATUS_RESOLVED, recording the rescuer ID and timestamp.
     */
    uint8_t batchMarkMessagesResolved(const uint32_t* msgIds, uint8_t count, uint16_t rescuerId, uint32_t timestamp) {
        if (!msgIds || count == 0) return 0;

        uint8_t purgedCount = 0;

        // Step 1: Purge matching records from Pending Retransmission File (/unsent_sos.bin)
        // ---------------------------------------------------------------------------------
        // CRITICAL BUG FIX (STACK OVERFLOW FIX):
        // Previously, this function allocated `StoredMessage remaining[50]` on the stack.
        // Since sizeof(StoredMessage) is ~120 bytes, 50 * 120 = 6,000 bytes on the stack!
        // In Arduino ESP32, the loopTask has a strict 8,192-byte stack limit.
        // Pushing 6,000 bytes onto the stack caused:
        // "Guru Meditation Error: Core 1 panic'ed (Unhandled debug exception)
        //  Debug exception reason: Stack canary watchpoint triggered (loopTask)"
        // 
        // We now stream records one-by-one into a temporary file `/temp_sos.bin` (using only
        // 120 bytes on the stack) and atomically rename it to `/unsent_sos.bin`.
        // ---------------------------------------------------------------------------------
        if (LittleFS.exists(UNACKED_SOS_FILE)) {
            File readFile = LittleFS.open(UNACKED_SOS_FILE, "r");
            File tempFile = LittleFS.open("/temp_sos.bin", "w");
            if (readFile && tempFile) {
                StoredMessage rec; // Uses only 120 bytes of stack memory!
                while (readFile.read((uint8_t*)&rec, sizeof(StoredMessage)) == sizeof(StoredMessage)) {
                    bool shouldDrop = false;
                    for (uint8_t k = 0; k < count; k++) {
                        if (rec.msgId == msgIds[k]) {
                            shouldDrop = true;
                            purgedCount++;
                            Serial.printf("[STORAGE] Purged resolved SOS [MsgID: 0x%08X] from pending queue.\n", rec.msgId);
                            break;
                        }
                    }
                    if (!shouldDrop) {
                        tempFile.write((const uint8_t*)&rec, sizeof(StoredMessage));
                    }
                }
                readFile.close();
                tempFile.close();

                // Atomically replace original file with pruned temporary file
                LittleFS.remove(UNACKED_SOS_FILE);
                LittleFS.rename("/temp_sos.bin", UNACKED_SOS_FILE);
            } else {
                if (readFile) readFile.close();
                if (tempFile) tempFile.close();
            }
        }

        // Step 2: Update status in permanent history log (/sos_history.bin)
        if (LittleFS.exists(SOS_HISTORY_FILE)) {
            File histFile = LittleFS.open(SOS_HISTORY_FILE, "r+"); // Open in-place read/write
            if (histFile) {
                size_t total = histFile.size() / sizeof(StoredMessage);
                for (size_t i = 0; i < total; i++) {
                    StoredMessage rec;
                    histFile.seek(i * sizeof(StoredMessage));
                    if (histFile.read((uint8_t*)&rec, sizeof(StoredMessage)) == sizeof(StoredMessage)) {
                        for (uint8_t k = 0; k < count; k++) {
                            if (rec.msgId == msgIds[k]) {
                                rec.status              = STATUS_RESOLVED;
                                rec.acknowledged        = true;
                                rec.resolvedByRescuerId = rescuerId;
                                rec.resolvedTimestamp   = timestamp;
                                histFile.seek(i * sizeof(StoredMessage));
                                histFile.write((const uint8_t*)&rec, sizeof(StoredMessage));
                                Serial.printf("[STORAGE] Updated History Record [MsgID: 0x%08X] to STATUS_RESOLVED.\n", rec.msgId);
                                break;
                            }
                        }
                    }
                }
                histFile.close();
            }
        }

        return purgedCount;
    }

    /**
     * Marks a single message as resolved. Convenience wrapper around batchMarkMessagesResolved().
     */
    bool markMessageResolved(uint32_t msgId, uint16_t rescuerId, uint32_t timestamp) {
        return batchMarkMessagesResolved(&msgId, 1, rescuerId, timestamp) > 0;
    }

    /**
     * Marks an offline distress message as ACKNOWLEDGED.
     */
    bool markMessageAcknowledged(uint32_t ackedMsgId) {
        return markMessageResolved(ackedMsgId, 0, millis() / 1000);
    }

    /**
     * Retrieves the count of currently stored unACKed offline distress messages.
     */
    size_t getStoredMessageCount() {
        if (!LittleFS.exists(UNACKED_SOS_FILE)) return 0;
        File file = LittleFS.open(UNACKED_SOS_FILE, "r");
        if (!file) return 0;
        size_t count = file.size() / sizeof(StoredMessage);
        file.close();
        return count;
    }

    /**
     * Reads a specific unACKed message record by index.
     */
    bool getStoredMessageByIndex(size_t index, StoredMessage& outRecord) {
        if (!LittleFS.exists(UNACKED_SOS_FILE)) return false;
        File file = LittleFS.open(UNACKED_SOS_FILE, "r");
        if (!file) return false;

        if (!file.seek(index * sizeof(StoredMessage))) {
            file.close();
            return false;
        }

        size_t readLen = file.read((uint8_t*)&outRecord, sizeof(StoredMessage));
        file.close();
        return readLen == sizeof(StoredMessage);
    }

    /**
     * Retrieves the total count of messages in permanent history.
     */
    size_t getHistoryMessageCount() {
        if (!LittleFS.exists(SOS_HISTORY_FILE)) return 0;
        File file = LittleFS.open(SOS_HISTORY_FILE, "r");
        if (!file) return 0;
        size_t count = file.size() / sizeof(StoredMessage);
        file.close();
        return count;
    }

    /**
     * Reads a historical message record by index.
     */
    bool getHistoryMessageByIndex(size_t index, StoredMessage& outRecord) {
        if (!LittleFS.exists(SOS_HISTORY_FILE)) return false;
        File file = LittleFS.open(SOS_HISTORY_FILE, "r");
        if (!file) return false;

        if (!file.seek(index * sizeof(StoredMessage))) {
            file.close();
            return false;
        }

        size_t readLen = file.read((uint8_t*)&outRecord, sizeof(StoredMessage));
        file.close();
        return readLen == sizeof(StoredMessage);
    }

    /**
     * Completely resets all offline logs and history files without touching web UI files.
     */
    void clearAllStorage() {
        LittleFS.remove(UNACKED_SOS_FILE);
        LittleFS.remove(SOS_HISTORY_FILE);
        Serial.println("[STORAGE] All offline SOS files and history logs purged.");
    }

private:
    /**
     * Checks if a message ID is already present in the pending retransmission queue.
     */
    bool isMessageAlreadyStored(uint32_t msgId) {
        if (!LittleFS.exists(UNACKED_SOS_FILE)) return false;
        File file = LittleFS.open(UNACKED_SOS_FILE, "r");
        if (!file) return false;

        size_t total = file.size() / sizeof(StoredMessage);
        for (size_t i = 0; i < total; i++) {
            StoredMessage rec;
            if (file.read((uint8_t*)&rec, sizeof(StoredMessage)) == sizeof(StoredMessage)) {
                if (rec.msgId == msgId) {
                    file.close();
                    return true;
                }
            }
        }
        file.close();
        return false;
    }

    /**
     * Appends a new entry to the permanent history audit log (/sos_history.bin).
     */
    void saveToHistory(const StoredMessage& rec) {
        File file = LittleFS.open(SOS_HISTORY_FILE, "a");
        if (file) {
            file.write((const uint8_t*)&rec, sizeof(StoredMessage));
            file.close();
        }
    }

public:
    // =====================================================================================
    // IMAGE STORAGE FUNCTIONS (OPTION A: SEQUENTIAL REASSEMBLY)
    // =====================================================================================

    /**
     * Saves an incoming image chunk directly to a combined binary file.
     * Used by Rescuer Node receiving chunks over LoRa.
     */
    void saveImageChunk(uint32_t msgId, uint16_t chunkIndex, const uint8_t* data, uint8_t dataLen) {
        char filename[32];
        snprintf(filename, sizeof(filename), "/img_%08X.bin", msgId);

        File file;
        if (chunkIndex == 0) {
            file = LittleFS.open(filename, "w"); // Chunk 0: initialize/overwrite file
        } else {
            if (LittleFS.exists(filename)) {
                file = LittleFS.open(filename, "r+"); // Existing file: open read/write
            } else {
                file = LittleFS.open(filename, "w"); // First chunk seen was >0: create file
            }
        }

        if (!file) {
            Serial.println("[STORAGE ERROR] Failed to open image file for writing!");
            return;
        }

        // Seek to the exact byte offset for this chunk
        file.seek(chunkIndex * MAX_IMAGE_CHUNK_DATA_LEN);
        file.write(data, dataLen);
        file.flush(); // Flush buffer to ensure LittleFS updates directory metadata st_size
        file.close();
        
        Serial.printf("[STORAGE] Saved Image Chunk %u (Len: %u) for MsgID 0x%08X\n", chunkIndex+1, dataLen, msgId);
    }

    /**
     * Reads a full reassembled image from storage to stream to HTTP clients.
     */
    File getFullImageFile(uint32_t msgId) {
        char filename[32];
        snprintf(filename, sizeof(filename), "/img_%08X.bin", msgId);
        return LittleFS.open(filename, "r");
    }

    /**
     * Checks if an image exists for a given message ID.
     */
    bool hasImage(uint32_t msgId) {
        char filename[32];
        snprintf(filename, sizeof(filename), "/img_%08X.bin", msgId);
        return LittleFS.exists(filename);
    }
};

#endif // STORAGE_MANAGER_H
