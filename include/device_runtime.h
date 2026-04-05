#pragma once

#include <stdint.h>

#include "config.h"
#include "rf_types.h"

class BLEServer;
class BLECharacteristic;

struct DeviceRuntime {
    BLEServer *pBLEServer = nullptr;
    BLECharacteristic *pCharNotify = nullptr;
    BLECharacteristic *pCharCommand = nullptr;

    bool bleDeviceConnected = false;
    bool bleOldDeviceConnected = false;
    uint32_t bleConnectStartMs = 0;
    bool timeSyncReceived = false;

    int64_t timeOffsetMs = 0;
    bool timeOffsetSynced = false;

    SignalHistory signalHistory[kMaxHistory] = {};
    int historyCount = 0;
    int historyIndex = 0;

    PendingSendParams pendingSendParams{};
    volatile bool pendingSendReady = false;
    volatile bool pendingHistoryQuery = false;
    volatile bool pendingTimeSync = false;
    uint64_t pendingTimeSyncValue = 0;

    bool historyQueryInProgress = false;
    int historyQueryCursor = 0;
    int historyQueryStartIdx = 0;
    uint32_t historyQueryLastSendMs = 0;
    uint32_t lastRecvNotifyMs = 0;
};
