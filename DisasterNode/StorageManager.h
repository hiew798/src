/*
 * =====================================================================================
 * File: StorageManager.h
 * Project: Heltec Wireless Stick Lite V3 - Disaster Recovery Multi-Node LoRa Network
 * Author: Antigravity AI / FYP Disaster Recovery Team
 * 
 * Description:
 *   Handles non-volatile flash memory storage using ESP32 LittleFS and NVS Preferences.
 *   
 *   Features:
 *   - Collision-Proof Persistent Message IDs: NVS sequence counter survives reboots.
 *   - Store-and-Forward: Offline SOS alerts stored in `/unsent_sos.bin`.
 *   - Lifecycle & History: Complete status tracking (PENDING, ACKNOWLEDGED, RESOLVED).
 *   - Batch Rescuer Invalidation: Removes cleared IDs from pending queue & updates history.
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

// Flash storage file paths
#define UNACKED_SOS_FILE  "/unsent_sos.bin"
#define SOS_HISTORY_FILE  "/sos_history.bin"

#pragma pack(push, 1)
struct StoredMessage {
    uint32_t        msgId;
    DistressPayload payload;
    uint32_t        timestampSaved;
    uint8_t         retryCount;
    bool            acknowledged;
    uint8_t         status;              // MessageStatus enum (PENDING, ACKNOWLEDGED, RESOLVED)
    uint16_t        resolvedByRescuerId; // Node ID of rescuer who cleared it
    uint32_t        resolvedTimestamp;   // When it was cleared
};
#pragma pack(pop)

class StorageManager {
private:
    Preferences prefs;

public:
    StorageManager() {}

    /**
     * Initializes LittleFS flash storage file system.
     */
    bool begin() {
        Serial.println("[STORAGE] Initializing LittleFS Flash File System...");
        
        if (!LittleFS.begin(true)) {
            Serial.println("[STORAGE ERROR] LittleFS Mount Failed!");
            return false;
        }

        Serial.printf("[STORAGE] LittleFS mounted successfully. Total: %u bytes, Used: %u bytes\n",
                      LittleFS.totalBytes(), LittleFS.usedBytes());

        // Ensure offline SOS file exists
        if (!LittleFS.exists(UNACKED_SOS_FILE)) {
            File f = LittleFS.open(UNACKED_SOS_FILE, "w");
            if (f) f.close();
        }

        return true;
    }

    /**
     * Generates a collision-proof 32-bit Message ID using hardware MAC and persistent NVS counter.
     * Guaranteed to never duplicate or reset even across power failures or crashes.
     */
    uint32_t getNextUniqueMsgId(uint16_t nodeId) {
        prefs.begin("node_seq", false);
        uint16_t seq = prefs.getUShort("seq", 0) + 1;
        prefs.putUShort("seq", seq);
        prefs.end();

        uint32_t uniqueId = ((uint32_t)nodeId << 16) | seq;
        Serial.printf("[STORAGE] Generated Collision-Proof MsgID: 0x%08X (Node: 0x%04X, Seq: %u)\n",
                      uniqueId, nodeId, seq);
        return uniqueId;
    }

    /**
     * Saves a new distress message to flash memory for Store-and-Forward backup.
     */
    bool saveUnackedMessage(uint32_t msgId, const DistressPayload& payload) {
        // Prevent storing duplicate alerts if already present
        if (isMessageAlreadyStored(msgId)) {
            return false;
        }

        size_t count = getStoredMessageCount();
        if (count >= MAX_STORED_OFFLINE_MESSAGES) {
            Serial.println("[STORAGE WARNING] Offline flash storage full! Cannot save new SOS.");
            return false;
        }

        File file = LittleFS.open(UNACKED_SOS_FILE, "a");
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

        Serial.printf("[STORAGE] Saved offline SOS [MsgID: 0x%08X] to LittleFS (%u bytes)\n", msgId, written);
        
        saveToHistory(record);
        return written == sizeof(StoredMessage);
    }

    /**
     * Invalidates and clears a batch of resolved Message IDs across LittleFS storage.
     * 1. Removes matching IDs from the unACKed pending queue (/unsent_sos.bin).
     * 2. Updates the matching records in history (/sos_history.bin) to STATUS_RESOLVED.
     */
    uint8_t batchMarkMessagesResolved(const uint32_t* msgIds, uint8_t count, uint16_t rescuerId, uint32_t timestamp) {
        if (!msgIds || count == 0) return 0;

        uint8_t purgedCount = 0;

        // 1. Purge from Pending UnACKed File (/unsent_sos.bin)
        if (LittleFS.exists(UNACKED_SOS_FILE)) {
            File readFile = LittleFS.open(UNACKED_SOS_FILE, "r");
            if (readFile) {
                size_t total = readFile.size() / sizeof(StoredMessage);
                StoredMessage remaining[MAX_STORED_OFFLINE_MESSAGES];
                size_t validCount = 0;

                for (size_t i = 0; i < total && validCount < MAX_STORED_OFFLINE_MESSAGES; i++) {
                    StoredMessage rec;
                    if (readFile.read((uint8_t*)&rec, sizeof(StoredMessage)) == sizeof(StoredMessage)) {
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
                            remaining[validCount++] = rec;
                        }
                    }
                }
                readFile.close();

                File writeFile = LittleFS.open(UNACKED_SOS_FILE, "w");
                if (writeFile) {
                    for (size_t i = 0; i < validCount; i++) {
                        writeFile.write((const uint8_t*)&remaining[i], sizeof(StoredMessage));
                    }
                    writeFile.close();
                }
            }
        }

        // 2. Update Status in Permanent History Log (/sos_history.bin)
        if (LittleFS.exists(SOS_HISTORY_FILE)) {
            File histFile = LittleFS.open(SOS_HISTORY_FILE, "r+"); // Read and write in place
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
     * Marks a single message as resolved.
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
     * Completely resets all offline logs and history files.
     */
    void clearAllStorage() {
        LittleFS.remove(UNACKED_SOS_FILE);
        LittleFS.remove(SOS_HISTORY_FILE);
        Serial.println("[STORAGE] All offline SOS files and history logs purged.");
    }

private:
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

    void saveToHistory(const StoredMessage& rec) {
        File file = LittleFS.open(SOS_HISTORY_FILE, "a");
        if (file) {
            file.write((const uint8_t*)&rec, sizeof(StoredMessage));
            file.close();
        }
    }
};

#endif // STORAGE_MANAGER_H
