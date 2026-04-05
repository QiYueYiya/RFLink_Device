#include "rf_controller.h"

#include <Arduino.h>

#include "ble_manager.h"
#include "protocol_tables.h"
#include "signal_history.h"

void rfSetTransmitEnable(RCSwitch &rfSwitch, bool enable) {
    if (enable) {
        digitalWrite(kRfTransmitEnablePin, HIGH);
        rfSwitch.enableTransmit(kRfTransmitPin);
        Serial.println("RF发射模块已开启");
    } else {
        rfSwitch.disableTransmit();
        pinMode(kRfTransmitPin, INPUT);
        digitalWrite(kRfTransmitEnablePin, LOW);
        Serial.println("RF发射模块已关闭");
    }
}

void rfSetReceiveEnable(RCSwitch &rfSwitch, bool enable) {
    if (enable) {
        digitalWrite(kRfReceiveEnablePin, HIGH);
        rfSwitch.enableReceive(kRfReceivePin);
        Serial.println("RF接收模块已开启");
    } else {
        rfSwitch.disableReceive();
        pinMode(kRfReceivePin, INPUT);
        digitalWrite(kRfReceiveEnablePin, LOW);
        Serial.println("RF接收模块已关闭");
    }
}

void rfProcessReceivedData(DeviceRuntime &runtime, RCSwitch &rfSwitch) {
    if (!rfSwitch.available()) {
        return;
    }

    const unsigned long receivedCode = rfSwitch.getReceivedValue();
    const unsigned int bitLength = rfSwitch.getReceivedBitlength();
    const unsigned int protocol = rfSwitch.getReceivedProtocol();
    unsigned int pulseLength = rfSwitch.getReceivedDelay();
    uint8_t params[kProtocolParamLength] = {1, 31, 1, 3, 3, 1};
    bool invertedSignal = false;
    const unsigned int repeatCount = 10;

    getCustomParamsFromProtocol(protocol, params, invertedSignal);
    if (pulseLength == 0) {
        pulseLength = 350;
    }

    if (receivedCode >= 1000) {
        if (kEnableVerboseRuntimeLog) {
            Serial.print("接收到433MHz信号: ");
            Serial.print(receivedCode);
            Serial.print(" (位长度: ");
            Serial.print(bitLength);
            Serial.print(", 协议: ");
            Serial.print(protocol);
            Serial.println(")");
        }

        const uint64_t timestampNow = nowTimestampMs(runtime.timeOffsetSynced, runtime.timeOffsetMs);
        const SignalUpsertResult upsertResult = upsertSignalHistory(
            runtime.signalHistory,
            runtime.historyCount,
            runtime.historyIndex,
            receivedCode,
            bitLength,
            pulseLength,
            params,
            invertedSignal,
            repeatCount,
            timestampNow,
            kDuplicateWindowMs);

        if (runtime.bleDeviceConnected && upsertResult != SignalUpsertResult::kDuplicateWithinWindow) {
            char dataBuffer[128];
            snprintf(
                dataBuffer,
                sizeof(dataBuffer),
                "%lu,%u,%u,%d,%d,%d,%d,%d,%d,%d,%u,%llu",
                receivedCode,
                bitLength,
                pulseLength,
                params[0],
                params[1],
                params[2],
                params[3],
                params[4],
                params[5],
                invertedSignal ? 1 : 0,
                repeatCount,
                static_cast<unsigned long long>(timestampNow));

            bleSendNotify(runtime, "recv", String(dataBuffer));
        }
    }

    rfSwitch.resetAvailable();
}

void rfProcessSendData(DeviceRuntime &runtime, RCSwitch &rfSwitch) {
    if (!runtime.pendingSendReady) {
        return;
    }

    runtime.pendingSendReady = false;
    rfSetTransmitEnable(rfSwitch, true);
    delay(10);

    // params[6] -> RCSwitch::Protocol 的一一映射。
    RCSwitch::Protocol customProtocol = {
        runtime.pendingSendParams.pulseLength,
        {runtime.pendingSendParams.params[0], runtime.pendingSendParams.params[1]},
        {runtime.pendingSendParams.params[2], runtime.pendingSendParams.params[3]},
        {runtime.pendingSendParams.params[4], runtime.pendingSendParams.params[5]},
        runtime.pendingSendParams.invertedSignal};

    rfSwitch.setProtocol(customProtocol);
    rfSwitch.setRepeatTransmit(runtime.pendingSendParams.repeatCount);
    rfSwitch.send(runtime.pendingSendParams.code, runtime.pendingSendParams.bitLength);

    if (kEnableVerboseRuntimeLog) {
        Serial.print("BLE发送自定义433MHz信号: ");
        Serial.print(runtime.pendingSendParams.code);
        Serial.print(" (位长度: ");
        Serial.print(runtime.pendingSendParams.bitLength);
        Serial.print(", 脉宽: ");
        Serial.print(runtime.pendingSendParams.pulseLength);
        Serial.print(", 重发: ");
        Serial.print(runtime.pendingSendParams.repeatCount);
        Serial.println(")");
    }

    char statusBuffer[48];
    snprintf(statusBuffer, sizeof(statusBuffer), "已发送: %lu", runtime.pendingSendParams.code);
    bleSendNotify(runtime, "status", statusBuffer);
    rfSetTransmitEnable(rfSwitch, false);
}
