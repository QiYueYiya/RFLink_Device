#pragma once

#include <Arduino.h>
#include <stdint.h>
#include "config.h"
#include "rf_types.h"

bool applyTimeSyncFromCommand(
    const String &timeText,
    int64_t &timeOffsetMs,
    bool &timeOffsetSynced,
    bool &timeSyncReceived,
    SignalHistory history[kMaxHistory],
    int historyCount,
    int historyIndex);
