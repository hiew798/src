/*
 * =====================================================================================
 * File: PacketDefs.h
 * Project: Heltec Wireless Stick Lite V3 - Disaster Recovery Multi-Node LoRa Network
 * Author: Antigravity AI / FYP Disaster Recovery Team
 * 
 * Description:
 *   Extensible binary packet structures and serialization helpers for LoRa transmission.
 *   
 * DESIGNED FOR EASY EXTENSION & MODIFICATION:
 *   If you want to add new fields in the future (e.g., GPS coordinates, medical alert flags,
 *   battery level), simply update the `DistressPayload` struct below.
 *   The fixed `PacketHeader`, Anti-Packet clearing mechanism, and mesh relay engine are
 *   decoupled from the payload contents and will continue working automatically!
 * 
 * CRITICAL C++ COMPILER PACKING NOTE (#pragma pack):
 *   Notice the use of `#pragma pack(push, 1)` and `#pragma pack(pop)`.
 *   By default, 32-bit microcontrollers (like ESP32) insert invisible padding bytes
 *   between struct fields to align them to 4-byte boundaries. `#pragma pack(push, 1)`
 *   forces the compiler to pack every byte contiguously without padding.
 *   This ensures the exact byte layout sent over radio matches byte-for-byte on the receiver.
 * =====================================================================================
 */

#ifndef PACKET_DEFS_H
#define PACKET_DEFS_H

#include <Arduino.h>
#include "Config.h"

// Magic byte at the start of every frame to identify valid disaster network packets
// and immediately reject stray or noise LoRa frames
#define PROTOCOL_MAGIC_BYTE     0xD5  // 'D'isaster '5'

// Current schema version - increment if structure fields change in future revisions
#define PAYLOAD_SCHEMA_VERSION  0x02

// Special broadcast destination address (0xFFFF = Broadcast to any listening node/rescuer)
#define LORA_BROADCAST_ADDR     0xFFFF

// =====================================================================================
// SECTION 1: PACKET TYPE ENUMERATION
// Defines the functional purpose of each LoRa frame transmitted over the network.
// =====================================================================================
enum PacketType : uint8_t {
    PKT_DISTRESS_ALERT = 0x01,  // Victim SOS / Status message (Carries DistressPayload)
    PKT_DISTRESS_ACK   = 0x02,  // Point-to-point Acknowledgment from Rescuer to Victim Node
    PKT_RESCUER_BEACON = 0x03,  // Beacon sent by Rescuer Node asking nearby nodes to dump stored SOS logs
    PKT_IMAGE_CHUNK    = 0x04,  // Optional chunked image payload (for low-res photos)
    PKT_CLEAR_MESSAGES = 0x05   // Anti-Packet / Rescuer Invalidation (Purges cleared alerts from mesh)
};

// =====================================================================================
// SECTION 2: STATUS FLAGS & LIFECYCLE STATE
// Compact bitflags representing critical emergency conditions.
// =====================================================================================
enum StatusFlags : uint8_t {
    FLAG_TRAPPED      = (1 << 0), // 0x01 (Bit 0): Victim trapped under rubble/debris
    FLAG_INJURED      = (1 << 1), // 0x02 (Bit 1): Medical injuries / severe bleeding present
    FLAG_NEED_WATER   = (1 << 2), // 0x04 (Bit 2): Requires urgent drinking water / food
    FLAG_NEED_MEDS    = (1 << 3), // 0x08 (Bit 3): Urgent prescription or trauma medication required
    FLAG_UNCONSCIOUS  = (1 << 4)  // 0x10 (Bit 4): Unconscious person present at location
};

// Incident lifecycle status for local storage tracking
enum MessageStatus : uint8_t {
    STATUS_PENDING      = 0x00, // Awaiting rescue / unacknowledged
    STATUS_ACKNOWLEDGED = 0x01, // Rescuer confirmed receipt of signal
    STATUS_RESOLVED     = 0x02  // Incident resolved / victim rescued (Purged from pending retransmission)
};

// =====================================================================================
// SECTION 3: FIXED NETWORK HEADER
// Every packet starts with this header. Do NOT change this unless modifying core protocol.
// Total Header Size: 12 bytes
// =====================================================================================
#pragma pack(push, 1)
struct PacketHeader {
    uint8_t  magic;         // Byte 0: Always PROTOCOL_MAGIC_BYTE (0xD5)
    uint8_t  version;       // Byte 1: Schema version (PAYLOAD_SCHEMA_VERSION)
    uint8_t  pktType;       // Byte 2: PacketType enum (PKT_DISTRESS_ALERT, PKT_CLEAR_MESSAGES, etc.)
    uint32_t msgId;         // Bytes 3-6: Unique message ID (16-bit Node ID | 16-bit persistent NVS Sequence)
    uint16_t senderNodeId;  // Bytes 7-8: Node ID of originating device
    uint16_t targetNodeId;  // Bytes 9-10: Destination node ID (0xFFFF = Broadcast)
    uint8_t  ttl;           // Byte 11: Hop count remaining (Starts at LORA_MAX_HOP_COUNT, decremented at each relay)
    uint8_t  payloadLen;    // Byte 12: Length of the payload struct following this header in bytes
};
#pragma pack(pop)

// =====================================================================================
// SECTION 4: EXTENSIBLE DISTRESS PAYLOAD SCHEMA
// 
// HOW TO EDIT THIS STRUCT IN THE FUTURE:
// 1. Add your new field below (e.g. float latitude; float longitude; uint8_t batteryPct;).
// 2. Adjust array sizes if necessary (e.g. floorRoom[16] -> floorRoom[24]).
// 3. Keep total struct size under ~200 bytes so the entire LoRa frame stays < 255 bytes.
// =====================================================================================
#pragma pack(push, 1)
struct DistressPayload {
    uint8_t  statusFlags;    // Bitmask of StatusFlags (Trapped, Injured, Need Water, etc.)
    uint8_t  victimCount;    // Total count of people needing rescue at this location
    
    // Fixed size character buffers avoid dynamic heap fragmentation on ESP32
    char     floorRoom[16];  // Location description (e.g. "Bldg A, Lvl 3, R302")
    char     contactName[16];// Name of victim or contact person
    char     textMsg[64];    // Custom distress text message
    
    uint32_t timestamp;      // Uptime / epoch timestamp when created (in seconds)
    
    // --- FUTURE EXTENSION PLACEHOLDERS ---
    // Uncomment or add custom fields below as requirements evolve:
    // float    latitude;    // GPS Latitude
    // float    longitude;   // GPS Longitude
    // uint8_t  batteryPct;  // Node battery percentage
};
#pragma pack(pop)

// =====================================================================================
// SECTION 5: ACKNOWLEDGMENT & ANTI-PACKET (CLEAR) PAYLOADS
// =====================================================================================
#pragma pack(push, 1)
// Direct ACK sent by rescuer node back to a specific victim node
struct AckPayload {
    uint32_t ackedMsgId;     // The original msgId being acknowledged
    uint16_t rescuerNodeId;  // ID of rescuer node issuing ACK
    uint32_t rescuerTime;    // Rescuer timestamp (in seconds)
};

// Anti-Packet (Vaccine / Invalidation Frame)
// Carries a batch of up to MAX_CLEAR_BATCH_SIZE (8) message IDs that have been resolved
// so all intermediate building nodes delete them from their retransmission backlogs.
struct ClearMessagePayload {
    uint16_t rescuerNodeId;                       // Rescuer node performing clearance
    uint32_t clearTimestamp;                      // Timestamp of clearance
    uint8_t  count;                               // Number of message IDs in this batch (1..MAX_CLEAR_BATCH_SIZE)
    uint32_t clearedMsgIds[MAX_CLEAR_BATCH_SIZE]; // Array of message IDs being invalidated
};
#pragma pack(pop)

// =====================================================================================
// SECTION 6: SERIALIZATION HELPER FUNCTIONS
// Converts high-level structs to raw byte buffers for LoRa radio transmission.
// =====================================================================================

/**
 * Builds a complete binary LoRa frame (Header + DistressPayload) ready for transmission.
 * @param buffer Output byte array (Must be at least sizeof(PacketHeader) + sizeof(DistressPayload))
 * @param msgId Unique collision-proof message identifier
 * @param senderId Node ID of originating device
 * @param payload Pointer to populated DistressPayload struct
 * @return Total bytes written to buffer
 */
inline size_t buildDistressPacket(uint8_t* buffer, uint32_t msgId, uint16_t senderId, const DistressPayload* payload) {
    PacketHeader header;
    header.magic        = PROTOCOL_MAGIC_BYTE;
    header.version      = PAYLOAD_SCHEMA_VERSION;
    header.pktType      = PKT_DISTRESS_ALERT;
    header.msgId        = msgId;
    header.senderNodeId = senderId;
    header.targetNodeId = LORA_BROADCAST_ADDR;
    header.ttl          = LORA_MAX_HOP_COUNT;
    header.payloadLen   = sizeof(DistressPayload);

    // Contiguously copy header and payload into linear output byte buffer
    memcpy(buffer, &header, sizeof(PacketHeader));
    memcpy(buffer + sizeof(PacketHeader), payload, sizeof(DistressPayload));

    return sizeof(PacketHeader) + sizeof(DistressPayload);
}

/**
 * Builds an Anti-Packet (PKT_CLEAR_MESSAGES) frame to invalidate resolved SOS messages across the mesh.
 * @param buffer Output byte array
 * @param uniquePktId Unique message ID for this Anti-Packet (prevents relay loops)
 * @param rescuerId ID of rescuer issuing clearance
 * @param msgIds Array of incident Message IDs being cleared
 * @param count Number of IDs in array (1 to MAX_CLEAR_BATCH_SIZE)
 * @return Total bytes written to buffer
 */
inline size_t buildClearPacket(uint8_t* buffer, uint32_t uniquePktId, uint16_t rescuerId, const uint32_t* msgIds, uint8_t count) {
    if (count > MAX_CLEAR_BATCH_SIZE) count = MAX_CLEAR_BATCH_SIZE;

    ClearMessagePayload clearPayload;
    clearPayload.rescuerNodeId  = rescuerId;
    clearPayload.clearTimestamp = millis() / 1000;
    clearPayload.count          = count;
    memset(clearPayload.clearedMsgIds, 0, sizeof(clearPayload.clearedMsgIds));
    for (uint8_t i = 0; i < count; i++) {
        clearPayload.clearedMsgIds[i] = msgIds[i];
    }

    PacketHeader header;
    header.magic        = PROTOCOL_MAGIC_BYTE;
    header.version      = PAYLOAD_SCHEMA_VERSION;
    header.pktType      = PKT_CLEAR_MESSAGES;
    header.msgId        = uniquePktId;
    header.senderNodeId = rescuerId;
    header.targetNodeId = LORA_BROADCAST_ADDR;
    header.ttl          = LORA_MAX_HOP_COUNT;
    header.payloadLen   = sizeof(ClearMessagePayload);

    memcpy(buffer, &header, sizeof(PacketHeader));
    memcpy(buffer + sizeof(PacketHeader), &clearPayload, sizeof(ClearMessagePayload));

    return sizeof(PacketHeader) + sizeof(ClearMessagePayload);
}

/**
 * Builds a point-to-point ACK binary frame.
 */
inline size_t buildAckPacket(uint8_t* buffer, uint32_t targetMsgId, uint16_t senderId, uint16_t targetNodeId) {
    PacketHeader header;
    header.magic        = PROTOCOL_MAGIC_BYTE;
    header.version      = PAYLOAD_SCHEMA_VERSION;
    header.pktType      = PKT_DISTRESS_ACK;
    header.msgId        = micros();
    header.senderNodeId = senderId;
    header.targetNodeId = targetNodeId;
    header.ttl          = LORA_MAX_HOP_COUNT;
    header.payloadLen   = sizeof(AckPayload);

    AckPayload ack;
    ack.ackedMsgId     = targetMsgId;
    ack.rescuerNodeId  = senderId;
    ack.rescuerTime    = millis() / 1000;

    memcpy(buffer, &header, sizeof(PacketHeader));
    memcpy(buffer + sizeof(PacketHeader), &ack, sizeof(AckPayload));

    return sizeof(PacketHeader) + sizeof(AckPayload);
}

/**
 * Parses and validates an incoming LoRa raw byte array into a PacketHeader structure.
 * @return true if packet is long enough and magic byte matches; false otherwise.
 */
inline bool parsePacketHeader(const uint8_t* buffer, size_t len, PacketHeader* outHeader) {
    if (len < sizeof(PacketHeader)) {
        return false; // Packet too short to contain header
    }

    memcpy(outHeader, buffer, sizeof(PacketHeader));

    // Validate magic byte
    if (outHeader->magic != PROTOCOL_MAGIC_BYTE) {
        return false;
    }

    return true;
}

#endif // PACKET_DEFS_H
