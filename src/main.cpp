#include <Arduino.h>
#include <RCSwitch.h>

#include "ble_manager.h"
#include "button_handler.h"
#include "config.h"
#include "device_runtime.h"
#include "rf_controller.h"

namespace {
RCSwitch g_rfSwitch;
ButtonHandler g_buttonHandler(kButtonPin);
DeviceRuntime g_runtime;
} // namespace

void setup() {
    Serial.begin(9600);
    delay(1000);

    Serial.println("\n\n=== RF 433MHz 控制器启动 ===");
    Serial.print("设备ID: ");
    Serial.println(kDeviceId);

    pinMode(kRfTransmitEnablePin, OUTPUT);
    rfSetTransmitEnable(g_rfSwitch, false);
    Serial.print("发射引脚: GPIO");
    Serial.println(kRfTransmitPin);
    Serial.print("发射模块使能引脚: GPIO");
    Serial.println(kRfTransmitEnablePin);

    pinMode(kRfReceiveEnablePin, OUTPUT);
    rfSetReceiveEnable(g_rfSwitch, true);
    Serial.print("接收引脚: GPIO");
    Serial.println(kRfReceivePin);
    Serial.print("接收模块使能引脚: GPIO");
    Serial.println(kRfReceiveEnablePin);
    Serial.println("433MHz收发器已初始化");

    bleSetup(g_runtime);

    g_buttonHandler.setup();

    Serial.println("\n=== 初始化完成 ===\n");
}

void loop() {
    g_buttonHandler.tick();

    // 主循环只做调度，复杂逻辑已经下沉到 BLE/RF 模块。
    bleProcessConnectionState(g_runtime);
    rfProcessReceivedData(g_runtime, g_rfSwitch);
    rfProcessSendData(g_runtime, g_rfSwitch);
    bleProcessHistoryQuery(g_runtime);

    delay(kLoopDelayMs);
}
