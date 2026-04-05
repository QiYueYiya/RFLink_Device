#include "ble_manager.h"

#include <cstring>

#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>

#include "signal_history.h"
#include "time_sync.h"

namespace {
DeviceRuntime *g_runtime = nullptr;

bool parseParamString(
    const String &paramStr,
    unsigned long &code,
    unsigned int &bitLength,
    uint16_t &pulseLength,
    uint8_t params[kProtocolParamLength],
    bool &invertedSignal,
    unsigned int &repeatCount) {
    int startIndex = 0;
    int endIndex = 0;
    unsigned long tempValues[kSendCommandFieldCount] = {0};
    int valueIndex = 0;

    while (endIndex != -1 && valueIndex < kSendCommandFieldCount) {
        endIndex = paramStr.indexOf(',', startIndex);
        const String field = paramStr.substring(startIndex, endIndex == -1 ? paramStr.length() : endIndex);
        startIndex = endIndex + 1;
        tempValues[valueIndex] = strtoul(field.c_str(), nullptr, 10);
        valueIndex++;
    }

    if (valueIndex != kSendCommandFieldCount) {
        return false;
    }

    constexpr unsigned long kMaxUInt16 = 65535UL;
    constexpr unsigned long kMaxUInt8 = 255UL;

    code = tempValues[0];

    if (tempValues[1] > kMaxUInt16) {
        return false;
    }
    bitLength = static_cast<unsigned int>(tempValues[1]);

    if (tempValues[2] > kMaxUInt16) {
        return false;
    }
    pulseLength = static_cast<uint16_t>(tempValues[2]);

    for (int i = 0; i < kProtocolParamLength; ++i) {
        if (tempValues[3 + i] > kMaxUInt8) {
            return false;
        }
        params[i] = static_cast<uint8_t>(tempValues[3 + i]);
    }

    if (tempValues[9] != 0 && tempValues[9] != 1) {
        return false;
    }
    invertedSignal = static_cast<bool>(tempValues[9]);

    if (tempValues[10] > kMaxUInt16) {
        return false;
    }
    repeatCount = static_cast<unsigned int>(tempValues[10]);

    return true;
}

void startHistoryQuery(DeviceRuntime &runtime) {
    runtime.pendingHistoryQuery = false;

    if (!runtime.bleDeviceConnected) {
        runtime.historyQueryInProgress = false;
        return;
    }

    if (runtime.historyCount == 0) {
        bleSendNotify(runtime, "status", "无历史记录");
        runtime.historyQueryInProgress = false;
        return;
    }

    char statusBuffer[48];
    snprintf(statusBuffer, sizeof(statusBuffer), "历史记录: %d", runtime.historyCount);
    bleSendNotify(runtime, "status", statusBuffer);
    runtime.historyQueryInProgress = true;
    runtime.historyQueryCursor = 0;
    runtime.historyQueryStartIdx = historyStartIndex(runtime.historyCount, runtime.historyIndex);
    runtime.historyQueryLastSendMs = 0;
}

class MyServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer *pServer) override {
        if (g_runtime == nullptr) {
            return;
        }

        g_runtime->bleDeviceConnected = true;
        g_runtime->bleConnectStartMs = millis();
        g_runtime->timeSyncReceived = false;
        Serial.println("BLE设备已连接");
        pServer->updatePeerMTU(pServer->getConnId(), 512);
    }

    void onDisconnect(BLEServer *pServer) override {
        (void)pServer;
        if (g_runtime == nullptr) {
            return;
        }

        g_runtime->bleDeviceConnected = false;
        g_runtime->historyQueryInProgress = false;
        Serial.println("BLE设备已断开");
    }

    void onMtuChanged(BLEServer *pServer, esp_ble_gatts_cb_param_t *param) override {
        (void)pServer;
        Serial.print("MTU已更新为: ");
        Serial.println(param->mtu.mtu);
    }
};

class CommandCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) override {
        if (g_runtime == nullptr) {
            return;
        }

        const std::string value = pCharacteristic->getValue();
        if (value.empty()) {
            return;
        }

        DeviceRuntime &runtime = *g_runtime;
        const String cmdStr = String(value.c_str());
        if (kEnableVerboseRuntimeLog) {
            Serial.print("BLE命令: ");
            Serial.println(cmdStr);
        }

        const int firstComma = cmdStr.indexOf(',');
        if (firstComma <= 0) {
            bleSendNotify(runtime, "status", "错误: 命令格式无效");
            return;
        }

        const String cmd = cmdStr.substring(0, firstComma);

        if (cmd == "query") {
            const String queryType = cmdStr.substring(firstComma + 1);
            if (queryType == "history") {
                runtime.pendingHistoryQuery = true;
                return;
            }
            bleSendNotify(runtime, "status", "错误: 不支持的查询类型");
            return;
        }

        if (cmd == "time") {
            const String timeText = cmdStr.substring(firstComma + 1);
            const bool ok = applyTimeSyncFromCommand(
                timeText,
                runtime.timeOffsetMs,
                runtime.timeOffsetSynced,
                runtime.timeSyncReceived,
                runtime.signalHistory,
                runtime.historyCount,
                runtime.historyIndex);

            if (!ok) {
                bleSendNotify(runtime, "status", "错误: 时间戳无效");
                return;
            }

            Serial.print("时间偏移(毫秒): ");
            Serial.println(static_cast<long long>(runtime.timeOffsetMs));
            bleSendNotify(runtime, "status", "时间已同步");
            return;
        }

        if (cmd == "send") {
            const String paramStr = cmdStr.substring(firstComma + 1);

            unsigned long code = 0;
            unsigned int bitLength = 0;
            uint16_t pulseLength = 0;
            uint8_t params[kProtocolParamLength] = {0};
            bool invertedSignal = false;
            unsigned int repeatCount = 0;

            const bool parsed = parseParamString(
                paramStr,
                code,
                bitLength,
                pulseLength,
                params,
                invertedSignal,
                repeatCount);

            if (!parsed) {
                bleSendNotify(runtime, "status", "错误: send参数格式无效");
                return;
            }

            bool paramsValid = true;
            for (int i = 0; i < kProtocolParamLength; ++i) {
                if (params[i] == 0) {
                    paramsValid = false;
                    break;
                }
            }

            if (!(code > 0 && bitLength > 0 && bitLength <= 64 && pulseLength > 0 && paramsValid && repeatCount > 0)) {
                bleSendNotify(runtime, "status", "错误: 参数无效");
                return;
            }

            runtime.pendingSendParams.code = code;
            runtime.pendingSendParams.bitLength = bitLength;
            runtime.pendingSendParams.pulseLength = pulseLength;
            memcpy(runtime.pendingSendParams.params, params, kProtocolParamLength);
            runtime.pendingSendParams.invertedSignal = invertedSignal;
            runtime.pendingSendParams.repeatCount = repeatCount;
            runtime.pendingSendReady = true;
            return;
        }

        bleSendNotify(runtime, "status", "错误: 命令不存在");
    }
};
} // namespace

void bleSendNotify(DeviceRuntime &runtime, const char *type, const char *data) {
    if (runtime.pCharNotify == nullptr || !runtime.bleDeviceConnected) {
        return;
    }

    if (strcmp(type, "recv") == 0) {
        const uint32_t now = millis();
        if (runtime.lastRecvNotifyMs != 0 && (now - runtime.lastRecvNotifyMs) < kRecvNotifyMinIntervalMs) {
            return;
        }
        runtime.lastRecvNotifyMs = now;
    }

    char payload[320];
    if (data != nullptr && data[0] != '\0') {
        snprintf(payload, sizeof(payload), "%s,%s", type, data);
    } else {
        snprintf(payload, sizeof(payload), "%s", type);
    }

    runtime.pCharNotify->setValue(payload);
    runtime.pCharNotify->notify();
}

void bleSendNotify(DeviceRuntime &runtime, const String &type, const String &data) {
    bleSendNotify(runtime, type.c_str(), data.c_str());
}

void bleSetup(DeviceRuntime &runtime) {
    g_runtime = &runtime;

    Serial.println("初始化BLE...");

    BLEDevice::init(kDeviceId);
    BLEDevice::setMTU(512);

    runtime.pBLEServer = BLEDevice::createServer();
    runtime.pBLEServer->setCallbacks(new MyServerCallbacks());

    BLEService *pService = runtime.pBLEServer->createService(kServiceUuid);

    runtime.pCharNotify = pService->createCharacteristic(
        kNotifyCharUuid,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
    runtime.pCharNotify->addDescriptor(new BLE2902());

    runtime.pCharCommand = pService->createCharacteristic(
        kCommandCharUuid,
        BLECharacteristic::PROPERTY_WRITE);
    runtime.pCharCommand->setCallbacks(new CommandCallbacks());

    pService->start();

    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(kServiceUuid);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06);
    pAdvertising->setMinPreferred(0x12);
    BLEDevice::startAdvertising();

    Serial.println("BLE服务已启动，等待连接...");
}

void bleProcessConnectionState(DeviceRuntime &runtime) {
    if (!runtime.bleDeviceConnected && runtime.bleOldDeviceConnected) {
        delay(500);
        if (runtime.pBLEServer != nullptr) {
            runtime.pBLEServer->startAdvertising();
            if (kEnableVerboseRuntimeLog) {
                Serial.println("开始BLE广播");
            }
        }
        runtime.bleOldDeviceConnected = runtime.bleDeviceConnected;
        runtime.historyQueryInProgress = false;
    }

    if (runtime.bleDeviceConnected && !runtime.bleOldDeviceConnected) {
        runtime.bleOldDeviceConnected = runtime.bleDeviceConnected;
        bleSendNotify(runtime, "status", "就绪");
    }

    if (runtime.bleDeviceConnected && !runtime.timeSyncReceived) {
        if (millis() - runtime.bleConnectStartMs > kTimeSyncTimeoutMs) {
            if (kEnableVerboseRuntimeLog) {
                Serial.println("未收到时间同步命令，断开连接");
            }
            if (runtime.pBLEServer != nullptr) {
                runtime.pBLEServer->disconnect(runtime.pBLEServer->getConnId());
                runtime.bleDeviceConnected = false;
            }
        }
    }
}

void bleProcessHistoryQuery(DeviceRuntime &runtime) {
    if (runtime.pendingHistoryQuery) {
        startHistoryQuery(runtime);
    }

    if (!runtime.historyQueryInProgress) {
        return;
    }

    if (!runtime.bleDeviceConnected) {
        runtime.historyQueryInProgress = false;
        return;
    }

    const uint32_t now = millis();
    if (runtime.historyQueryLastSendMs != 0 && (now - runtime.historyQueryLastSendMs) < kHistoryQueryIntervalMs) {
        return;
    }

    if (runtime.historyQueryCursor >= runtime.historyCount) {
        runtime.historyQueryInProgress = false;
        return;
    }

    const int idx = (runtime.historyQueryStartIdx + runtime.historyQueryCursor) % kMaxHistory;

    char dataBuffer[256];
    snprintf(
        dataBuffer,
        sizeof(dataBuffer),
        "%lu,%u,%u,%d,%d,%d,%d,%d,%d,%d,%u,%llu",
        runtime.signalHistory[idx].code,
        runtime.signalHistory[idx].bitLength,
        runtime.signalHistory[idx].pulseLength,
        runtime.signalHistory[idx].params[0],
        runtime.signalHistory[idx].params[1],
        runtime.signalHistory[idx].params[2],
        runtime.signalHistory[idx].params[3],
        runtime.signalHistory[idx].params[4],
        runtime.signalHistory[idx].params[5],
        runtime.signalHistory[idx].invertedSignal ? 1 : 0,
        runtime.signalHistory[idx].repeatCount,
        static_cast<unsigned long long>(runtime.signalHistory[idx].timestamp));

    bleSendNotify(runtime, "history", String(dataBuffer));
    runtime.historyQueryCursor++;
    runtime.historyQueryLastSendMs = now;
}
