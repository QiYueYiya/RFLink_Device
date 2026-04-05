#pragma once

#include <stdint.h>

// 统一硬件引脚配置，避免散落的魔法数字。
constexpr uint8_t kRfTransmitPin = 6;
constexpr uint8_t kRfTransmitEnablePin = 7;
constexpr uint8_t kRfReceivePin = 9;
constexpr uint8_t kRfReceiveEnablePin = 8;
constexpr uint8_t kButtonPin = 10;

// BLE GATT 配置。
constexpr char kServiceUuid[] = "4fafc201-1fb5-459e-8fcc-c5c9c331914b";
constexpr char kNotifyCharUuid[] = "beb5483e-36e1-4688-b7f5-ea07361b26a0";
constexpr char kCommandCharUuid[] = "beb5483e-36e1-4688-b7f5-ea07361b26a1";

// 设备元信息。
constexpr char kDeviceId[] = "RFLink";

// 运行参数。
constexpr int kMaxHistory = 100;
constexpr uint32_t kTimeSyncTimeoutMs = 3000;
constexpr uint32_t kLoopDelayMs = 10;
constexpr uint32_t kHistoryQueryIntervalMs = 50;
constexpr uint32_t kDuplicateWindowMs = 500;
constexpr uint32_t kRecvNotifyMinIntervalMs = 40;

// 调试日志开关：上线场景可关闭高频日志，降低串口输出带来的阻塞。
constexpr bool kEnableVerboseRuntimeLog = false;

// 参数长度约定。
constexpr int kProtocolParamLength = 6;
constexpr int kSendCommandFieldCount = 11;
