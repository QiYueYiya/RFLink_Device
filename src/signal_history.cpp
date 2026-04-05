#include "signal_history.h"

#include <string.h>

namespace {
void fillHistoryEntry(
    SignalHistory &entry,
    unsigned long code,
    unsigned int bitLength,
    unsigned int pulseLength,
    const uint8_t params[kProtocolParamLength],
    bool invertedSignal,
    unsigned int repeatCount,
    uint64_t timestampMs) {
    entry.code = code;
    entry.bitLength = bitLength;
    entry.pulseLength = pulseLength;
    memcpy(entry.params, params, kProtocolParamLength);
    entry.invertedSignal = invertedSignal;
    entry.repeatCount = repeatCount;
    entry.timestamp = timestampMs;
}
} // namespace

uint64_t nowTimestampMs(bool timeOffsetSynced, int64_t timeOffsetMs) {
    const int64_t raw = static_cast<int64_t>(millis()) + (timeOffsetSynced ? timeOffsetMs : 0);
    return raw < 0 ? 0ULL : static_cast<uint64_t>(raw);
}

SignalUpsertResult upsertSignalHistory(
    SignalHistory history[kMaxHistory],
    int &historyCount,
    int &historyIndex,
    unsigned long code,
    unsigned int bitLength,
    unsigned int pulseLength,
    const uint8_t params[kProtocolParamLength],
    bool invertedSignal,
    unsigned int repeatCount,
    uint64_t timestampMs,
    uint32_t duplicateWindowMs) {
    for (int i = 0; i < historyCount; ++i) {
        const int idx = (historyCount < kMaxHistory) ? i : (historyIndex + i) % kMaxHistory;
        SignalHistory &entry = history[idx];

        if (entry.code != code) {
            continue;
        }

        const bool withinDuplicateWindow =
            (timestampMs >= entry.timestamp) && ((timestampMs - entry.timestamp) <= duplicateWindowMs);

        if (withinDuplicateWindow) {
            entry.timestamp = timestampMs;
            return SignalUpsertResult::kDuplicateWithinWindow;
        }

        fillHistoryEntry(entry, code, bitLength, pulseLength, params, invertedSignal, repeatCount, timestampMs);
        return SignalUpsertResult::kUpdatedExistingOutsideWindow;
    }

    fillHistoryEntry(history[historyIndex], code, bitLength, pulseLength, params, invertedSignal, repeatCount, timestampMs);
    historyIndex = (historyIndex + 1) % kMaxHistory;
    if (historyCount < kMaxHistory) {
        ++historyCount;
    }

    return SignalUpsertResult::kInsertedNew;
}

int historyStartIndex(int historyCount, int historyIndex) {
    return (historyCount < kMaxHistory) ? 0 : historyIndex;
}

void updateAllHistoryTimestampsWithDelta(
    SignalHistory history[kMaxHistory],
    int historyCount,
    int historyIndex,
    int64_t deltaMs) {
    for (int i = 0; i < historyCount; ++i) {
        const int idx = (historyCount < kMaxHistory) ? i : (historyIndex + i) % kMaxHistory;
        const int64_t raw = static_cast<int64_t>(history[idx].timestamp) + deltaMs;
        history[idx].timestamp = raw < 0 ? 0ULL : static_cast<uint64_t>(raw);
    }
}
