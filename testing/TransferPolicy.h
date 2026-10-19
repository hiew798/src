#pragma once
#include "Config.h"
#include "PacketDefs.h"
static_assert(LORA_SPREADING_FACTOR >= 7 && LORA_SPREADING_FACTOR <= 12, "Airtime model supports SF7..SF12");

// Explicit LoRa header, 16-bit PHY CRC, automatic low-data-rate optimisation.
// Matches SX126x::calculateTimeOnAir for the configured SF7..SF12 LoRa mode.
inline uint32_t frameAirtimeMs(size_t bytes) {
    const double symbolMs = double(uint32_t(1) << LORA_SPREADING_FACTOR) / LORA_BANDWIDTH;
    const int de = symbolMs >= 16.0 ? 1 : 0;
    const int numerator = int(8 * bytes) - 4 * LORA_SPREADING_FACTOR + 28 + 16;
    const int divisor = 4 * (LORA_SPREADING_FACTOR - 2 * de);
    const int coded = numerator > 0 ? (numerator + divisor - 1) / divisor : 0;
    return uint32_t(ceil((LORA_PREAMBLE_LENGTH + 4.25 + 8 + coded * LORA_CODING_RATE) * symbolMs));
}
inline uint8_t ackPathHops(uint8_t ttl) {
    const uint8_t observed = MESH_HOPS - ttl + 1;
    return observed > TRANSFER_MIN_PATH_HOPS ? observed : TRANSFER_MIN_PATH_HOPS;
}
// Timer starts AFTER local transmission. Allow downstream forwarding, return
// ACK airtime, relay jitter and storage/channel contention margin.
inline uint32_t fixedAckWaitMs(uint8_t hops, size_t requestBytes, size_t ackBytes) {
    return (hops - 1) * (frameAirtimeMs(requestBytes) + 1000) +
           hops * (frameAirtimeMs(ackBytes) + 1000) + 2000;
}
inline uint32_t imageListenGapMs(uint8_t hops, size_t bytes) {
    return IMAGE_LISTEN_MS + (hops - 1) * (frameAirtimeMs(bytes) + 1100);
}
inline uint32_t bitmapDataBytes(uint64_t mask, uint32_t imageBytes) {
    uint32_t bytes = 0;
    for (uint16_t i = 0; i < imageChunks(imageBytes); ++i) if (mask & (uint64_t(1) << i)) {
        const uint32_t left = imageBytes - i * CHUNK_BYTES;
        bytes += left < CHUNK_BYTES ? left : CHUNK_BYTES;
    }
    return bytes;
}

// Measure the current size, try a neighbour, and retain the trial only if its
// confirmed bytes/second improve enough. State is local to this image.
struct AdaptiveRatePolicy {
    uint32_t preferredRate = 0;
    uint8_t selected = ADAPTIVE_INITIAL_CHUNKS, preferred = ADAPTIVE_INITIAL_CHUNKS, steady = 0;
    bool measured = false, probing = false, nextProbeUp = true;

    void restart() {
        preferred = selected; preferredRate = 0;
        measured = probing = false; steady = 0; nextProbeUp = true;
    }
    void applyGroupCap(uint8_t cap) {
        if (selected <= cap && preferred <= cap) return;
        selected = preferred = preferred < cap ? preferred : cap;
        restart();
    }
    void recover() {
        selected = preferred = preferred > 1 ? preferred / 2 : 1;
        restart();
    }
    void probe(bool up, uint8_t cap) {
        uint8_t larger = preferred * 2 > cap ? cap : preferred * 2;
        uint8_t smaller = preferred > 1 ? preferred / 2 : 1;
        uint8_t candidate = up ? larger : smaller;
        if (candidate == preferred) { up = !up; candidate = up ? larger : smaller; }
        if (candidate == preferred) return;
        selected = candidate; probing = true; nextProbeUp = !up; steady = 0;
    }
    void observe(uint8_t used, uint32_t rate, bool missing, uint8_t cap) {
        applyGroupCap(cap);
        if (used != selected) return;
        if (probing) {
            const bool wasUp = selected > preferred;
            const bool better = uint64_t(rate) * 100 >
                                uint64_t(preferredRate) * (100 + ADAPTIVE_MIN_GAIN_PERCENT);
            if (better) { preferred = selected; preferredRate = rate; }
            selected = preferred; probing = false; steady = 0;
            if (better && wasUp && !missing && preferred < cap) probe(true, cap);
            return;
        }
        const bool first = !measured;
        preferredRate = first ? rate : uint32_t((uint64_t(preferredRate) * 3 + rate) / 4);
        measured = true;
        // Missing data justifies a smaller trial; it must beat the preferred
        // size's measured rate before becoming the new default.
        if (missing && preferred > 1) probe(false, cap);
        else if (first || ++steady >= ADAPTIVE_PROBE_INTERVAL_GROUPS) probe(nextProbeUp, cap);
    }
};

struct GroupTransfer {
    uint64_t id = 0, batchMask = 0, remaining = 0, request = 0;
    uint32_t requestEnded = 0, srtt = 0, variation = 0, rto = 0;
    uint8_t window = ADAPTIVE_INITIAL_CHUNKS, pathHops = MESH_HOPS;
    bool needManifest = true, awaiting = false;
#if TRANSFER_MODE == TRANSFER_ADAPTIVE
    AdaptiveRatePolicy ratePolicy;
    uint32_t groupStarted = 0;
    uint8_t batchWindow = 0, unanswered = 0;
    bool measuring = false, sampleEligible = false;
#endif

    void timeout() {
#if TRANSFER_MODE == TRANSFER_ADAPTIVE
        if (unanswered < 255) ++unanswered;
        if (rto) rto = rto > TRANSFER_RTO_MAX_MS / 2 ? TRANSFER_RTO_MAX_MS : rto * 2;
        if (unanswered == ADAPTIVE_SILENT_REQUESTS) {
            ratePolicy.recover(); window = ratePolicy.selected;
            sampleEligible = false;
        }
#endif
    }
    void feedback(uint32_t sample, uint32_t floor) {
        if (!srtt) { srtt = sample; variation = sample / 2; }
        else {
            const uint32_t error = srtt > sample ? srtt - sample : sample - srtt;
            variation = (3 * variation + error) / 4;
            srtt = (7 * srtt + sample) / 8;
        }
        uint32_t estimate = srtt + 4 * variation + 500;
        rto = estimate < floor ? floor : estimate > TRANSFER_RTO_MAX_MS ? TRANSFER_RTO_MAX_MS : estimate;
    }
    static uint8_t groupCap(uint32_t chunkAirtime) {
        uint32_t cap = chunkAirtime ? ADAPTIVE_AIRTIME_BUDGET_MS / chunkAirtime : 1;
        if (!cap) cap = 1; // A single packet cannot be interrupted mid-air.
        return cap < ADAPTIVE_MAX_CHUNKS ? uint8_t(cap) : ADAPTIVE_MAX_CHUNKS;
    }
    uint8_t groupSize(bool urgent, uint32_t chunkAirtime) {
        if (TRANSFER_MODE == TRANSFER_FIXED) return FIXED_GROUP_CHUNKS;
#if TRANSFER_MODE == TRANSFER_ADAPTIVE
        ratePolicy.applyGroupCap(groupCap(chunkAirtime)); window = ratePolicy.selected;
#endif
        return urgent ? 1 : window;
    }
    void beginMeasurement(uint32_t now, uint8_t chosen, bool eligible) {
#if TRANSFER_MODE == TRANSFER_ADAPTIVE
        groupStarted = now; batchWindow = chosen; measuring = true; sampleEligible = eligible;
#endif
    }
    void ignoreMeasurement() {
#if TRANSFER_MODE == TRANSFER_ADAPTIVE
        sampleEligible = false;
#endif
    }
    void endMeasurement() {
#if TRANSFER_MODE == TRANSFER_ADAPTIVE
        measuring = sampleEligible = false;
#endif
    }
    bool finishMeasurement(uint32_t now, uint32_t newBytes, bool missing, uint8_t cap) {
#if TRANSFER_MODE == TRANSFER_ADAPTIVE
        const uint32_t elapsed = now - groupStarted; // Also handles millis() rollover.
        const bool use = measuring && sampleEligible && elapsed && batchWindow == ratePolicy.selected;
        if (use) {
            const uint32_t rate = uint32_t(uint64_t(newBytes) * 1000 / elapsed);
            ratePolicy.observe(batchWindow, rate, missing, cap);
            window = ratePolicy.selected;
        }
        endMeasurement();
        return use;
#else
        return false;
#endif
    }
};
