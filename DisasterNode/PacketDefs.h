#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>
#include "TransferConfig.h"

// Modes 1/2 keep wire protocol 3. Modes 3/4 require protocol 4 on all nodes.
// Stored records retain their existing version and layout in all four modes.
constexpr uint16_t MESH_MAGIC = 0xD5A3;
constexpr uint8_t MESH_VERSION = GROUPED_TRANSFER ? 4 : 3;
constexpr size_t AUTH_TAG_BYTES = 8;
constexpr size_t MAX_PAYLOAD = 192;
constexpr size_t CHUNK_BYTES = 180;
enum PacketKind : uint8_t { SOS = 1, CHAT = 2, IMAGE = 3, CHUNK = 4, RESOLVE = 5, ACK = 6, BEACON = 7,
    GROUP_CHUNK = 8, BITMAP_QUERY = 9, BITMAP_ACK = 10 };
enum Delivery : uint8_t { DELIVERY_PENDING = 0, DELIVERY_RECEIVED = 1, DELIVERY_RELAY = 2 };
enum AckFlags : uint8_t { ACK_COMPLETE = 1, ACK_RESTART = 2 };
constexpr uint16_t META_ACK = 0xffff;
#pragma pack(push, 1)
struct PacketHeader {
    uint16_t magic;
    uint8_t version, kind;
    uint64_t packetId, source, target, objectId;
    uint8_t ttl;
    uint16_t length;
};
struct SosPayload {
    uint8_t flags, people, gps;
    float latitude, longitude;
    uint32_t timestamp;
    char location[48], name[32], text[96];
};
struct ChatPayload { uint64_t report; uint32_t timestamp; char text[160]; };
struct ImagePayload { uint64_t report; uint32_t bytes, crc; };
struct ChunkPayload { uint16_t index; uint8_t count; uint8_t data[CHUNK_BYTES]; };
struct ResolvePayload { uint64_t report; uint32_t timestamp; };
struct AckPayload { uint8_t kind; uint16_t chunk; uint8_t flags; };
// Echo the exact request packet ID: delayed responses cannot replace newer progress.
struct BitmapAckPayload { uint64_t request, bitmap; uint8_t flags; };
#pragma pack(pop)
static_assert(sizeof(SosPayload) <= MAX_PAYLOAD, "SOS payload too large");
static_assert(sizeof(PacketHeader) + sizeof(ChunkPayload) + AUTH_TAG_BYTES <= 255, "LoRa frame too large");

inline uint32_t crcUpdate(uint32_t c, const uint8_t* data, size_t n) {
    while (n--) {
        c ^= *data++;
        for (uint8_t b=0; b<8; ++b) c = (c >> 1) ^ (0xedb88320U & (0U - (c & 1)));
    }
    return c;
}
inline uint32_t crc32(const void* data, size_t n) { return ~crcUpdate(0xffffffffU, (const uint8_t*)data, n); }
inline bool terminated(const char* s, size_t n) { return memchr(s, 0, n) != nullptr; }
inline uint16_t imageChunks(uint32_t bytes) { return (bytes + CHUNK_BYTES - 1) / CHUNK_BYTES; }
inline uint64_t completeMask(uint32_t bytes) { return (uint64_t(1) << imageChunks(bytes)) - 1; }
inline uint8_t bitmapCount(uint64_t bits) { uint8_t n=0; while(bits) { bits &= bits-1; ++n; } return n; }
inline bool validPayload(const PacketHeader& h, const uint8_t* data) {
    switch (h.kind) {
    case SOS: {
        if (h.length != sizeof(SosPayload)) return false;
        SosPayload p; memcpy(&p, data, sizeof(p));
        return p.people >= 1 && p.people <= 99 && p.flags <= 31 && p.gps <= 1 &&
            (!p.gps || (isfinite(p.latitude) && isfinite(p.longitude) && fabs(p.latitude)<=90 && fabs(p.longitude)<=180)) &&
            terminated(p.location, sizeof(p.location)) && terminated(p.name, sizeof(p.name)) && terminated(p.text, sizeof(p.text));
    }
    case CHAT: {
        if (h.length != sizeof(ChatPayload)) return false;
        ChatPayload p; memcpy(&p, data, sizeof(p));
        return p.report && p.text[0] && terminated(p.text, sizeof(p.text));
    }
    case IMAGE: {
        if (h.length != sizeof(ImagePayload)) return false;
        ImagePayload p; memcpy(&p, data, sizeof(p));
        return p.report == h.objectId && p.bytes >= 4 && p.bytes <= 8192;
    }
    case GROUP_CHUNK:
        if (!GROUPED_TRANSFER || !h.target) return false;
        // Same 180-byte data payload; group chunks do not elicit individual ACKs.
        // fall through
    case CHUNK: {
        if (h.length < 4 || h.length > sizeof(ChunkPayload)) return false;
        ChunkPayload p{}; memcpy(&p, data, h.length);
        return p.count >= 1 && p.count <= CHUNK_BYTES && p.index < 46 && h.length == 3 + p.count;
    }
    case RESOLVE: {
        if (h.length != sizeof(ResolvePayload)) return false;
        ResolvePayload p; memcpy(&p, data, sizeof(p)); return p.report != 0;
    }
    case ACK: {
        if (h.length != sizeof(AckPayload) || !h.target) return false;
        AckPayload p; memcpy(&p, data, sizeof(p));
        return (p.kind == SOS || p.kind == CHAT || p.kind == IMAGE || p.kind == RESOLVE) && p.flags <= 2 &&
            (p.chunk == META_ACK || (p.kind == IMAGE && p.chunk < 46));
    }
    case BEACON: return h.length == 0 && h.target == 0;
    case BITMAP_QUERY: return GROUPED_TRANSFER && h.target && h.length == 0;
    case BITMAP_ACK: {
        if (!GROUPED_TRANSFER || !h.target || h.length != sizeof(BitmapAckPayload)) return false;
        BitmapAckPayload p; memcpy(&p, data, sizeof(p));
        return p.request && p.flags <= ACK_COMPLETE && !(p.bitmap >> 46);
    }
    default: return false;
    }
}
inline bool parseFrame(const uint8_t* frame, size_t n, PacketHeader& h) {
    if (n < sizeof(h) + AUTH_TAG_BYTES || n > 255) return false;
    memcpy(&h, frame, sizeof(h));
    return h.magic == MESH_MAGIC && h.version == MESH_VERSION && h.packetId && h.objectId && h.source &&
        h.ttl >= 1 && h.ttl <= 3 && h.length <= MAX_PAYLOAD && n == sizeof(h) + h.length + AUTH_TAG_BYTES &&
        validPayload(h, frame + sizeof(h));
}
