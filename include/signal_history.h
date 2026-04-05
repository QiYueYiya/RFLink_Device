#pragma once

#include <Arduino.h>
#include <stdint.h>
#include "config.h"
#include "rf_types.h"

enum class SignalUpsertResult {
    kDuplicateWithinWindow,
    kUpdatedExistingOutsideWindow,
    kInsertedNew,
};

uint64_t nowTimestampMs(bool timeOffsetSynced, int64_t timeOffsetMs);

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
    uint32_t duplicateWindowMs);

int historyStartIndex(int historyCount, int historyIndex);

void updateAllHistoryTimestampsWithDelta(
    SignalHistory history[kMaxHistory],
    int historyCount,
    int historyIndex,
    int64_t deltaMs);
