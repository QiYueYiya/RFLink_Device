#include <Arduino.h>
#include <RCSwitch.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <OneButton.h>

// 引脚定义
#define RF_TRANSMIT_PIN 8 // 433MHz发射引脚
#define RF_RECEIVE_PIN 6  // 433MHz接收引脚
#define BUTTON_PIN 10      // 点动开关引脚

// BLE UUIDs
#define SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHAR_NOTIFY_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a0"  // 通知特征 (Read/Notify) - 发送TEXT数据
#define CHAR_COMMAND_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a1" // 命令特征 (Write) - 接收TEXT命令

// 信号接收历史记录
#define MAX_HISTORY 30 // 最多存储30条历史记录
// 连接后等待时间同步超时时间（毫秒）
#define TIME_SYNC_TIMEOUT_MS 3000

// 设备信息
String deviceId = "RFTransmitter";

// 全局对象
RCSwitch mySwitch = RCSwitch();
OneButton button(BUTTON_PIN, true, true);

// BLE对象
BLEServer *pBLEServer = nullptr;
BLECharacteristic *pCharNotify = nullptr;  // 通知特征
BLECharacteristic *pCharCommand = nullptr; // 命令特征
bool bleDeviceConnected = false;
bool bleOldDeviceConnected = false;
uint64_t bleConnectStartMs = 0;
bool timeSyncReceived = false;

// 时间戳偏移（毫秒）- 用于同步真实时间
// 存储方式：realTime = millis() + timeOffset
// 使用uint64_t避免32位unsigned long溢出
uint64_t timeOffset = 0;
bool timeOffsetSynced = false;  // 标记是否已同步过时间

// 信号历史记录结构
struct SignalHistory
{
    unsigned long code;
    unsigned int bitLength;
    unsigned int protocol;
    uint64_t timestamp;  // 使用uint64_t存储毫秒级时间戳，避免溢出
};
SignalHistory signalHistory[MAX_HISTORY];
int historyCount = 0;
int historyIndex = 0;

// 按键长按回调
void onResetLongPress()
{
    Serial.println("按键长按5秒，重启设备");
    delay(50);
    ESP.restart();
}

// 发送BLE通知
void sendBLENotify(const String &type, const String &data)
{
    if (!pCharNotify || !bleDeviceConnected)
        return;

    // 格式: type,data 或 received,code,bits,protocol
    String output = type;
    if (data.length() > 0)
    {
        output += "," + data;
    }

    pCharNotify->setValue(output.c_str());
    pCharNotify->notify();
}

// BLE服务器回调
class MyServerCallbacks : public BLEServerCallbacks
{
    void onConnect(BLEServer *pServer)
    {
        bleDeviceConnected = true;
        Serial.println("BLE设备已连接");
        bleConnectStartMs = millis();
        timeSyncReceived = false;
        // 请求MTU协商，ESP32-C3最大支持512字节
        pServer->updatePeerMTU(pServer->getConnId(), 512);
    }

    void onDisconnect(BLEServer *pServer)
    {
        bleDeviceConnected = false;
        Serial.println("BLE设备已断开");
    }

    void onMtuChanged(BLEServer *pServer, esp_ble_gatts_cb_param_t *param)
    {
        Serial.print("MTU已更新为: ");
        Serial.println(param->mtu.mtu);
    }
};

// BLE命令回调 - 统一处理所有命令
class CommandCallbacks : public BLECharacteristicCallbacks
{
    void onWrite(BLECharacteristic *pCharacteristic)
    {
        std::string value = pCharacteristic->getValue();
        if (value.length() > 0)
        {
            String cmdStr = String(value.c_str());
            Serial.print("BLE命令: ");
            Serial.println(cmdStr);

            // 解析格式: send,code,bitLength,protocol 或 query,history
            int firstComma = cmdStr.indexOf(',');

            if (firstComma > 0)
            {
                String cmd = cmdStr.substring(0, firstComma);

                // query命令 - 查询信息
                if (cmd == "query")
                {
                    String queryType = cmdStr.substring(firstComma + 1);

                    if (queryType == "history")
                    {
                        Serial.println("BLE查询历史记录");
                        if (historyCount == 0)
                        {
                            sendBLENotify("status", "无历史记录");
                        }
                        else
                        {
                            // 发送历史记录数量
                            sendBLENotify("status", "历史记录: " + String(historyCount));
                            delay(50);

                            // 从最旧的记录开始发送
                            int startIdx = (historyCount < MAX_HISTORY) ? 0 : historyIndex;
                            for (int i = 0; i < historyCount; i++)
                            {
                                int idx = (startIdx + i) % MAX_HISTORY;
                                String histData = String(signalHistory[idx].code) + "," +
                                                  String(signalHistory[idx].bitLength) + "," +
                                                  String(signalHistory[idx].protocol) + "," +
                                                  String(signalHistory[idx].timestamp);
                                sendBLENotify("history", histData);
                                delay(50); // 给BLE时间发送
                            }
                        }
                    }
                    else
                    {
                        sendBLENotify("status", "错误: 不支持的查询类型");
                    }
                }
                // time命令 - 同步时间
                else if (cmd == "time")
                {
                    String timeStr = cmdStr.substring(firstComma + 1);
                    uint64_t receivedTime = strtoull(timeStr.c_str(), nullptr, 10);
                    
                    if (receivedTime > 0)
                    {
                        // 计算时间偏移：真实时间 = millis() + timeOffset
                        uint64_t newTimeOffset = receivedTime - millis();
                        
                        // 如果这是第一次同步时间，需要更新之前保存的历史记录
                        if (!timeOffsetSynced && historyCount > 0)
                        {
                            Serial.println("更新历史记录时间戳...");
                            // 用新的timeOffset更新所有历史记录
                            for (int i = 0; i < historyCount; i++)
                            {
                                int idx = (historyCount < MAX_HISTORY) ? i : (historyIndex + i) % MAX_HISTORY;
                                signalHistory[idx].timestamp += newTimeOffset;
                            }
                        }
                        
                        timeOffset = newTimeOffset;
                        timeOffsetSynced = true;
                        timeSyncReceived = true;
                        
                        Serial.print("BLE同步时间戳: ");
                        Serial.println(receivedTime);
                        Serial.print("时间偏移: ");
                        Serial.println(timeOffset);
                        
                        sendBLENotify("status", "时间已同步");
                    }
                    else
                    {
                        sendBLENotify("status", "错误: 时间戳无效");
                    }
                }
                // send命令 - 发送RF信号
                else if (cmd == "send")
                {
                    int secondComma = cmdStr.indexOf(',', firstComma + 1);
                    int thirdComma = cmdStr.indexOf(',', secondComma + 1);

                    if (secondComma > firstComma && thirdComma > secondComma)
                    {
                        unsigned long code = strtoul(cmdStr.substring(firstComma + 1, secondComma).c_str(), nullptr, 10);
                        unsigned int bitLength = (unsigned int)atoi(cmdStr.substring(secondComma + 1, thirdComma).c_str());
                        unsigned int protocol = (unsigned int)atoi(cmdStr.substring(thirdComma + 1).c_str());

                        if (code > 0 && bitLength > 0 && protocol > 0)
                        {
                            mySwitch.setProtocol(protocol);
                            mySwitch.send(code, bitLength);

                            Serial.print("BLE发送433MHz信号: ");
                            Serial.print(code);
                            Serial.print(" (位长度: ");
                            Serial.print(bitLength);
                            Serial.print(", 协议: ");
                            Serial.print(protocol);
                            Serial.println(")");
                            String msg = "已发送: " + String(code);
                            sendBLENotify("status", msg);
                        }
                        else
                        {
                            sendBLENotify("status", "错误: 参数无效");
                        }
                    }
                }
                else
                {
                    sendBLENotify("status", "错误: 命令不存在");
                }
            }
            else
            {
                sendBLENotify("status", "错误: 命令格式无效");
            }
        }
    }
};

// 设置BLE
void setupBLE()
{
    Serial.println("初始化BLE...");

    BLEDevice::init(deviceId.c_str());
    BLEDevice::setMTU(512);
    pBLEServer = BLEDevice::createServer();
    pBLEServer->setCallbacks(new MyServerCallbacks());

    BLEService *pService = pBLEServer->createService(SERVICE_UUID);

    // 通知特征 (Read/Notify) - 发送JSON数据到手机
    pCharNotify = pService->createCharacteristic(
        CHAR_NOTIFY_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
    pCharNotify->addDescriptor(new BLE2902());

    // 命令特征 (Write) - 接收手机发送的JSON命令
    pCharCommand = pService->createCharacteristic(
        CHAR_COMMAND_UUID,
        BLECharacteristic::PROPERTY_WRITE);
    pCharCommand->setCallbacks(new CommandCallbacks());

    pService->start();

    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06);
    pAdvertising->setMinPreferred(0x12);
    BLEDevice::startAdvertising();

    Serial.println("BLE服务已启动，等待连接...");
}

// 处理433MHz接收
void handleRFReceive()
{
    if (mySwitch.available())
    {

        unsigned long receivedCode = mySwitch.getReceivedValue();
        unsigned int bitLength = mySwitch.getReceivedBitlength();
        unsigned int protocol = mySwitch.getReceivedProtocol();

        if (receivedCode >= 1000)
        {
            Serial.print("接收到433MHz信号: ");
            Serial.print(receivedCode);
            Serial.print(" (位长度: ");
            Serial.print(bitLength);
            Serial.print(", 协议: ");
            Serial.print(protocol);
            Serial.println(")");

            // 只有未连接到主机时才保存历史记录（并做去重：若存在则仅更新时间戳，保留位置）
            if (!bleDeviceConnected)
            {
                // 检查是否已有相同信号，若有则仅更新时间戳
                bool found = false;
                for (int i = 0; i < historyCount; i++)
                {
                    int idx = (historyCount < MAX_HISTORY) ? i : (historyIndex + i) % MAX_HISTORY;
                    if (signalHistory[idx].code == receivedCode)
                    {
                        signalHistory[idx].timestamp = timeOffsetSynced ? (millis() + timeOffset) : millis();
                        found = true;
                        break;
                    }
                }

                if (!found)
                {
                    // 保存到历史记录（环形缓冲）
                    signalHistory[historyIndex].code = receivedCode;
                    signalHistory[historyIndex].bitLength = bitLength;
                    signalHistory[historyIndex].protocol = protocol;
                    // 如果已同步时间则使用完整时间戳，否则仅保存millis()值供后续修正
                    signalHistory[historyIndex].timestamp = timeOffsetSynced ? (millis() + timeOffset) : millis();
                    historyIndex = (historyIndex + 1) % MAX_HISTORY;
                    if (historyCount < MAX_HISTORY)
                        historyCount++;
                }
            }
            else
            {
                // 已连接到主机，直接发送信号给主机
                String dataStr = String(receivedCode) + "," + String(bitLength) + "," + String(protocol);
                sendBLENotify("recv", dataStr);
            }
        }

        mySwitch.resetAvailable();
    }
}

void setup()
{
    Serial.begin(9600);
    delay(1000);
    Serial.println("\n\n=== RF 433MHz 控制器启动 ===");
    Serial.print("设备ID: ");
    Serial.println(deviceId);

    // 设置433MHz收发器
    mySwitch.enableTransmit(RF_TRANSMIT_PIN);
    mySwitch.enableReceive(RF_RECEIVE_PIN);
    Serial.print("发射引脚: GPIO");
    Serial.println(RF_TRANSMIT_PIN);
    Serial.print("接收引脚: GPIO");
    Serial.println(RF_RECEIVE_PIN);
    Serial.println("433MHz收发器已初始化");

    // 初始化BLE
    setupBLE();

    // 初始化按键（长按5秒重启）
    button.setPressMs(5000);
    button.attachLongPressStop(onResetLongPress);

    Serial.println("\n=== 初始化完成 ===\n");
}

void loop()
{
    // 处理按键
    button.tick();

    // 处理BLE连接状态变化
    if (!bleDeviceConnected && bleOldDeviceConnected)
    {
        delay(500);                     // 给蓝牙栈一点时间准备
        pBLEServer->startAdvertising(); // 重新开始广播
        Serial.println("开始BLE广播");
        bleOldDeviceConnected = bleDeviceConnected;
    }
    if (bleDeviceConnected && !bleOldDeviceConnected)
    {
        bleOldDeviceConnected = bleDeviceConnected;
        // 新设备连接时，发送当前状态
        sendBLENotify("status", "就绪");
    }

    // 连接后若未收到时间同步命令则断开
    if (bleDeviceConnected && !timeSyncReceived)
    {
        if (millis() - bleConnectStartMs > TIME_SYNC_TIMEOUT_MS)
        {
            Serial.println("未收到时间同步命令，断开连接");
            if (pBLEServer)
            {
                pBLEServer->disconnect(pBLEServer->getConnId());
            }
        }
    }

    // 处理433MHz接收
    handleRFReceive();

    delay(10);
}
