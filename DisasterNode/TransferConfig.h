#pragma once
#include <stdint.h>

// Select the same mode on the civilian, rescuer and every relay.
#define TRANSFER_BASELINE 1
#define TRANSFER_TUNED 2
#define TRANSFER_FIXED 3
#define TRANSFER_ADAPTIVE 4
#ifndef TRANSFER_MODE
#define TRANSFER_MODE TRANSFER_ADAPTIVE
#endif
#if TRANSFER_MODE < 1 || TRANSFER_MODE > 4
#error TRANSFER_MODE must be 1, 2, 3 or 4
#endif

constexpr bool GROUPED_TRANSFER = TRANSFER_MODE >= TRANSFER_FIXED;
constexpr uint8_t FIXED_GROUP_CHUNKS = 4;
constexpr uint8_t ADAPTIVE_INITIAL_CHUNKS = 2;
constexpr uint8_t ADAPTIVE_MAX_CHUNKS = 8;
// Probe a neighbouring size after this many measured groups at the preferred size.
constexpr uint8_t ADAPTIVE_PROBE_INTERVAL_GROUPS = 3;
constexpr uint8_t ADAPTIVE_MIN_GAIN_PERCENT = 5;
// One missing reply only retries the query. Repeated silence permits a smaller group.
constexpr uint8_t ADAPTIVE_SILENT_REQUESTS = 2;
// Total data airtime in an adaptive group; listening gaps are additional.
constexpr uint32_t ADAPTIVE_AIRTIME_BUDGET_MS = 8000;
constexpr uint32_t IMAGE_LISTEN_MS = 200;
constexpr uint32_t URGENT_HOLD_MS = 2000;
// ACK TTL estimates the return path. Increase this for known asymmetric routes.
constexpr uint8_t TRANSFER_MIN_PATH_HOPS = 1;
constexpr uint32_t TRANSFER_RTO_MAX_MS = 60000;
#ifndef EXPERIMENT_LOGGING
#define EXPERIMENT_LOGGING 1
#endif

inline const char* transferModeName() {
    return TRANSFER_MODE == 1 ? "baseline" : TRANSFER_MODE == 2 ? "tuned" :
           TRANSFER_MODE == 3 ? "fixed" : "adaptive";
}
static_assert(FIXED_GROUP_CHUNKS >= 1 && FIXED_GROUP_CHUNKS <= 8, "Group size must be 1..8");
static_assert(ADAPTIVE_INITIAL_CHUNKS >= 1 && ADAPTIVE_INITIAL_CHUNKS <= ADAPTIVE_MAX_CHUNKS,
              "Initial adaptive group must fit its maximum");
static_assert(ADAPTIVE_PROBE_INTERVAL_GROUPS >= 1 && ADAPTIVE_SILENT_REQUESTS >= 2,
              "Adaptive probes must be bounded and one timeout must not shrink a group");
static_assert(TRANSFER_MIN_PATH_HOPS >= 1 && TRANSFER_MIN_PATH_HOPS <= 3, "Path hint must be 1..3");
