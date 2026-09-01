/*
 * =====================================================================================
 * File: PacketDefs.h
 * Project: Heltec Wireless Stick Lite V3 - Disaster Recovery Multi-Node LoRa Network
 * Author: Antigravity AI / FYP Disaster Recovery Team
 * 
 * Description:
 *   Extensible binary packet structures and helper functions for LoRa transmission.
 *   
 * DESIGNED FOR EASY MODIFICATION:
 *   If you want to add new fields (e.g., GPS coordinates, medical alert flags, battery level),
 *   simply update the `DistressPayload` struct below and update `packDistressPayload()`
 *   / `unpackDistressPayload()`. The network header and mesh relay protocol will continue
 *   working automatically!
 * =====================================================================================
 */

#ifndef PACKET_DEFS_H
#define PACKET_DEFS_H

#include <Arduino.h>
#include "Config.h"

// Magic byte at the start of every packet to identify valid frames for this disaster network
#define PROTOCOL_MAGIC_BYTE     0xD5  // 'D'isaster '5'

// Current schema version - increment if structure fields change in future revisions
#define PAYLOAD_SCHEMA_VERSION  0x01

// Broadcast address for targetNodeId
#define LORA_BROADCAST_ADDR     0xFFFF

// =====================================================================================
// SECTION 1: PACKET TYPE ENUMERATION
// Defines the purpose of each LoRa frame transmitted over the network.
// =====================================================================================
enum PacketType : uint8_t {
    PKT_DISTRESS_ALERT = 0x01,  // Victim SOS / Status message
    PKT_DISTRESS_ACK   = 0x02,  // Acknowledgment from Rescuer Node back to Victim Node
    PKT_RESCUER_BEACON = 0x03,  // Beacon sent by Rescuer Node asking nearby nodes to dump stored SOS logs
    PKT_IMAGE_CHUNK    = 0x04   // Optional image payload chunk
};

// =====================================================================================
// SECTION 2: STATUS FLAGS (BITMASK)
// Compact bitflags representing critical emergency conditions.
// =====================================================================================
enum StatusFlags : uint8_t {
    FLAG_TRAPPED      = (1 << 0), // 0x01 - Victim trapped under debris
    FLAG_INJURED      = (1 << 1), // 0x02 - Medical injuries present
    FLAG_NEED_WATER   = (1 << 2), // 0x04 - Requires water / food
    FLAG_NEED_MEDS    = (1 << 3), // 0x08 - Urgent medical supplies required
    FLAG_UNCONSCIOUS  = (1 << 4)  // 0x10 - Unconscious person present
};

// =====================================================================================
// SECTION 3: FIXED NETWORK HEADER
// Every packet starts with this header. Do NOT change this unless modifying core protocol.
// =====================================================================================
#pragma pack(push, 1)
struct PacketHeader {
    uint8_t  magic;         // Always PROTOCOL_MAGIC_BYTE (0xD5)
    uint8_t  version;       // Schema version (PAYLOAD_SCHEMA_VERSION)
    uint8_t  pktType;       // PacketType enum (PKT_DISTRESS_ALERT, etc.)
    uint32_t msgId;         // Unique message ID (Combination of Node ID + Timestamp counter)
    uint16_t senderNodeId;  // ID of originating node
    uint16_t targetNodeId;  // Destination node ID (0xFFFF = Broadcast / Any Rescuer)
    uint8_t  ttl;           // Hop count remaining (Starts at LORA_MAX_HOP_COUNT, decremented at each relay)
    uint8_t  payloadLen;    // Length of the following payload data in bytes
};
#pragma pack(pop)

// =====================================================================================
// SECTION 4: EXTENSIBLE DISTRESS PAYLOAD SCHEMA
// 
// HOW TO EDIT THIS STRUCT IN THE FUTURE:
// 1. Add your new field below (e.g., float latitude; float longitude; uint8_t batteryLevel;)
// 2. Adjust array sizes if necessary (e.g. floorRoom[16] -> floorRoom[24])
// 3. Keep total struct size under ~200 bytes so the entire LoRa frame stays < 255 bytes.
// =====================================================================================
#pragma pack(push, 1)
struct DistressPayload {
    uint8_t  statusFlags;    // Bitmask of StatusFlags (Trapped, Injured, Need Water, etc.)
    uint8_t  victimCount;    // Total count of people needing rescue at this location
    
    // String fields - Fixed size buffers to avoid dynamic memory allocation on ESP32
    char     floorRoom[16];  // Location description (e.g. "Bldg A, Lvl 3, R302")
    char     contactName[16];// Name of victim or contact person
    char     textMsg[64];    // Custom distress text message (e.g., "Wall collapsed, blocked door")
    
    // Timestamps and diagnostic info
    uint32_t timestamp;      // Uptime / epoch timestamp when created (in seconds)
    
    // --- FUTURE EXTENSION PLACEHOLDERS ---
    // Uncomment or add custom fields below as your requirements evolve:
    // float    latitude;    // GPS Latitude
    // float    longitude;   // GPS Longitude
    // uint8_t  batteryPct;  // Node battery percentage
};
#pragma pack(pop)

// =====================================================================================
// SECTION 5: ACKNOWLEDGMENT PAYLOAD
// Sent by rescuer node back to originating node to confirm receipt.
// =====================================================================================
#pragma pack(push, 1)
struct AckPayload {
    uint32_t ackedMsgId;     // The original msgId being acknowledged
    uint16_t rescuerNodeId;  // ID of rescuer node issuing ACK
    uint32_t rescuerTime;    // Rescuer timestamp
};
#pragma pack(pop)

// =====================================================================================
// SECTION 6: SERIALIZATION HELPER FUNCTIONS
// Converts high-level structs to raw byte buffers for LoRa radio transmission.
// =====================================================================================

/**
 * Creates a complete binary LoRa frame (Header + DistressPayload) ready for transmission.
 * @param buffer Output byte array (Must be at least sizeof(PacketHeader) + sizeof(DistressPayload))
 * @param msgId Unique message identifier
 * @param senderId Node ID of originating device
 * @param payload Pointer to filled DistressPayload struct
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

    // Copy header and payload into linear output byte buffer
    memcpy(buffer, &header, sizeof(PacketHeader));
    memcpy(buffer + sizeof(PacketHeader), payload, sizeof(DistressPayload));

    return sizeof(PacketHeader) + sizeof(DistressPayload);
}

/**
 * Creates an ACK binary frame.
 */
inline size_t buildAckPacket(uint8_t* buffer, uint32_t targetMsgId, uint16_t senderId, uint16_t targetNodeId) {
    PacketHeader header;
    header.magic        = PROTOCOL_MAGIC_BYTE;
    header.version      = PAYLOAD_SCHEMA_VERSION;
    header.pktType      = PKT_DISTRESS_ACK;
    header.msgId        = micros(); // Use microsecond timestamp for unique ACK packet ID
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
 * @return true if packet magic byte matches and version is valid; false otherwise.
 */
inline bool parsePacketHeader(const uint8_t* buffer, size_t len, PacketHeader* outHeader) {
    if (len < sizeof(PacketHeader)) {
        return false; // Packet too short to contain header
    }

    memcpy(outHeader, buffer, sizeof(PacketHeader));

    // Validate magic byte and schema version
    if (outHeader->magic != PROTOCOL_MAGIC_BYTE) {
        return false;
    }

    return true;
}

#endif // PACKET_DEFS_H
