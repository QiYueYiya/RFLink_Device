#include "time_sync.h"

#include <stdlib.h>

#include "signal_history.h"

namespace {
constexpr int64_t kMaxAbsTimeOffsetMs = 315360000000LL; // 10 years

int64_t clampOffset(int64_t offsetMs) {
    if (offsetMs > kMaxAbsTimeOffsetMs) {
        return kMaxAbsTimeOffsetMs;
    }
    if (offsetMs < -kMaxAbsTimeOffsetMs) {
        return -kMaxAbsTimeOffsetMs;
    }
    return offsetMs;
}
} // namespace

bool applyTimeSyncFromCommand(
    const String &timeText,
    int64_t &timeOffsetMs,
    bool &timeOffsetSynced,
    bool &timeSyncReceived,
    SignalHistory history[kMaxHistory],
    int historyCount,
    int historyIndex) {
    const char *raw = timeText.c_str();
    char *endPtr = nullptr;
    const unsigned long long receivedTime = strtoull(raw, &endPtr, 10);

    if (raw == endPtr || (endPtr != nullptr && *endPtr != '\0') || receivedTime == 0ULL) {
        return false;
    }

    const int64_t newOffset = clampOffset(
        static_cast<int64_t>(receivedTime) - static_cast<int64_t>(millis()));

    // 首次同步时，把之前以 millis 记下的历史时间整体平移到真实时间轴。
    if (!timeOffsetSynced && historyCount > 0) {
        updateAllHistoryTimestampsWithDelta(history, historyCount, historyIndex, newOffset);
    }

    timeOffsetMs = newOffset;
    timeOffsetSynced = true;
    timeSyncReceived = true;
    return true;
}
