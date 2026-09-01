/*
 * =====================================================================================
 * File: StorageManager.h
 * Project: Heltec Wireless Stick Lite V3 - Disaster Recovery Multi-Node LoRa Network
 * Author: Antigravity AI / FYP Disaster Recovery Team
 * 
 * Description:
 *   Handles non-volatile flash memory storage using ESP32 LittleFS.
 *   Provides Store-and-Forward capabilities so distress alerts submitted by victims
 *   are preserved across power loss and re-sent automatically when a rescuer comes into range.
 * =====================================================================================
 */

#ifndef STORAGE_MANAGER_H
#define STORAGE_MANAGER_H

#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>
#include "Config.h"
#include "PacketDefs.h"

// Path to offline pending distress messages file
#define UNACKED_SOS_FILE  "/unsent_sos.bin"
#define SOS_HISTORY_FILE  "/sos_history.bin"

struct StoredMessage {
    uint32_t msgId;
    DistressPayload payload;
    uint32_t timestampSaved;
    uint8_t  retryCount;
    bool     acknowledged;
};

class StorageManager {
public:
    StorageManager() {}

    /**
     * Initializes LittleFS flash storage file system.
     * Formats flash memory automatically if LittleFS is uninitialized on first boot.
     */
    bool begin() {
        Serial.println("[STORAGE] Initializing LittleFS Flash File System...");
        
        // true parameter auto-formats LittleFS if file system mounting fails on first boot
        if (!LittleFS.begin(true)) {
            Serial.println("[STORAGE ERROR] LittleFS Mount Failed!");
            return false;
        }

        Serial.printf("[STORAGE] LittleFS Initialized successfully. Total Bytes: %u, Used Bytes: %u\n",
                      LittleFS.totalBytes(), LittleFS.usedBytes());

        // Ensure offline SOS file exists
        if (!LittleFS.exists(UNACKED_SOS_FILE)) {
            File f = LittleFS.open(UNACKED_SOS_FILE, "w");
            if (f) f.close();
        }

        return true;
    }

    /**
     * Saves a new distress message to flash memory for Store-and-Forward backup.
     */
    bool saveUnackedMessage(uint32_t msgId, const DistressPayload& payload) {
        // First check current stored message count to prevent flash memory overflow
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
        record.msgId          = msgId;
        record.payload        = payload;
        record.timestampSaved = millis();
        record.retryCount     = 0;
        record.acknowledged   = false;

        size_t written = file.write((const uint8_t*)&record, sizeof(StoredMessage));
        file.close();

        Serial.printf("[STORAGE] Saved offline SOS [MsgID: 0x%08X] to LittleFS (%u bytes written)\n", msgId, written);
        
        // Also add to permanent history log for rescuer retrieval
        saveToHistory(record);
        return written == sizeof(StoredMessage);
    }

    /**
     * Marks an offline distress message as ACKNOWLEDGED and removes it from the pending send queue.
     */
    bool markMessageAcknowledged(uint32_t ackedMsgId) {
        if (!LittleFS.exists(UNACKED_SOS_FILE)) return false;

        File readFile = LittleFS.open(UNACKED_SOS_FILE, "r");
        if (!readFile) return false;

        // Temporary vector buffer to hold remaining unACKed records
        size_t totalRecords = readFile.size() / sizeof(StoredMessage);
        StoredMessage records[MAX_STORED_OFFLINE_MESSAGES];
        size_t validCount = 0;
        bool found = false;

        for (size_t i = 0; i < totalRecords && i < MAX_STORED_OFFLINE_MESSAGES; i++) {
            StoredMessage rec;
            if (readFile.read((uint8_t*)&rec, sizeof(StoredMessage)) == sizeof(StoredMessage)) {
                if (rec.msgId == ackedMsgId) {
                    found = true; // Drop this record because it has been acknowledged!
                    Serial.printf("[STORAGE] Removed ACKed SOS [MsgID: 0x%08X] from pending queue.\n", ackedMsgId);
                } else {
                    records[validCount++] = rec;
                }
            }
        }
        readFile.close();

        if (!found) return false;

        // Rewrite updated queue without the ACKed record
        File writeFile = LittleFS.open(UNACKED_SOS_FILE, "w");
        if (!writeFile) return false;

        for (size_t i = 0; i < validCount; i++) {
            writeFile.write((const uint8_t*)&records[i], sizeof(StoredMessage));
        }
        writeFile.close();

        return true;
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
     * Appends an entry to historical SOS log (`/sos_history.bin`).
     */
    void saveToHistory(const StoredMessage& rec) {
        File file = LittleFS.open(SOS_HISTORY_FILE, "a");
        if (file) {
            file.write((const uint8_t*)&rec, sizeof(StoredMessage));
            file.close();
        }
    }
};

#endif // STORAGE_MANAGER_H
