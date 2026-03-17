#include <Arduino.h>
#include <RCSwitch.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <OneButton.h>

// 引脚定义
#define RF_TRANSMIT_PIN 8    // 433MHz发射引脚
#define RF_TRANSMIT_EN_PIN 9 // 433MHz发射模块使能引脚
#define RF_RECEIVE_PIN 6     // 433MHz接收引脚
#define RF_RECEIVE_EN_PIN 7  // 433MHz接收模块使能引脚
#define BUTTON_PIN 10        // 点动开关引脚

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
bool timeOffsetSynced = false; // 标记是否已同步过时间

// 信号历史记录结构
struct SignalHistory
{
    unsigned long code;
    unsigned int bitLength;
    unsigned int pulseLength;
    uint8_t params[6]; // syncHigh, syncLow, zeroHigh, zeroLow, oneHigh, oneLow
    bool invertedSignal;
    unsigned int repeatCount;
    uint64_t timestamp; // 使用uint64_t存储毫秒级时间戳，避免溢出
};
SignalHistory signalHistory[MAX_HISTORY];
int historyCount = 0;
int historyIndex = 0;

// 待处理的BLE命令（BLE回调运行在独立任务中，通过标志位将命令传递给主循环处理，避免栈溢出）
struct PendingSendParams
{
    unsigned long code;
    unsigned int bitLength;
    uint16_t pulseLength;
    uint8_t params[6]; // syncHigh, syncLow, zeroHigh, zeroLow, oneHigh, oneLow
    bool invertedSignal;
    unsigned int repeatCount;
};
PendingSendParams pendingSendParams;
volatile bool pendingSendReady = false;
volatile bool pendingHistoryQuery = false;

void getCustomParamsFromProtocol(unsigned int protocolId, unsigned int pulseLength, uint8_t params[6], bool &invertedSignal)
{
    // params: [syncHigh, syncLow, zeroHigh, zeroLow, oneHigh, oneLow]
    const uint8_t arr1[6] = {1, 31, 1, 3, 3, 1};
    const uint8_t arr2[6] = {1, 10, 1, 2, 2, 1};
    const uint8_t arr3[6] = {30, 71, 4, 11, 9, 6};
    const uint8_t arr4[6] = {1, 6, 1, 3, 3, 1};
    const uint8_t arr5[6] = {6, 14, 1, 2, 2, 1};
    const uint8_t arr6[6] = {23, 1, 1, 2, 2, 1};
    const uint8_t arr7[6] = {2, 62, 1, 6, 6, 1};
    switch (protocolId)
    {
    case 2:
        memcpy(params, arr2, 6);
        invertedSignal = false;
        break;
    case 3:
        memcpy(params, arr3, 6);
        invertedSignal = false;
        break;
    case 4:
        memcpy(params, arr4, 6);
        invertedSignal = false;
        break;
    case 5:
        memcpy(params, arr5, 6);
        invertedSignal = false;
        break;
    case 6:
        memcpy(params, arr6, 6);
        invertedSignal = true;
        break;
    case 7:
        memcpy(params, arr7, 6);
        invertedSignal = false;
        break;
    case 1:
    default:
        memcpy(params, arr1, 6);
        invertedSignal = false;
        break;
    }

    // 协议未识别或脉宽异常时，仍保证有可用默认值
    if (pulseLength == 0)
    {
        pulseLength = 350;
    }
}

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

bool parseCsvToken(const String &input, int &cursor, String &token)
{
    if (cursor < 0 || cursor > input.length())
        return false;

    int commaPos = input.indexOf(',', cursor);
    if (commaPos < 0)
    {
        token = input.substring(cursor);
        cursor = input.length() + 1;
    }
    else
    {
        token = input.substring(cursor, commaPos);
        cursor = commaPos + 1;
    }

    token.trim();
    return token.length() > 0;
}

bool parseUnsignedLongStrict(const String &token, unsigned long &value)
{
    char *endPtr = nullptr;
    unsigned long parsed = strtoul(token.c_str(), &endPtr, 10);
    if (endPtr == token.c_str() || *endPtr != '\0')
        return false;
    value = parsed;
    return true;
}

bool parseUnsignedIntStrict(const String &token, unsigned int &value)
{
    unsigned long parsed = 0;
    if (!parseUnsignedLongStrict(token, parsed) || parsed > 0xFFFF)
        return false;
    value = (unsigned int)parsed;
    return true;
}

bool parseUInt16Strict(const String &token, uint16_t &value)
{
    unsigned long parsed = 0;
    if (!parseUnsignedLongStrict(token, parsed) || parsed > 0xFFFF)
        return false;
    value = (uint16_t)parsed;
    return true;
}

bool parseUInt8Strict(const String &token, uint8_t &value)
{
    unsigned long parsed = 0;
    if (!parseUnsignedLongStrict(token, parsed) || parsed > 0xFF)
        return false;
    value = (uint8_t)parsed;
    return true;
}

bool parseBool01(const String &token, bool &value)
{
    if (token == "0")
    {
        value = false;
        return true;
    }
    if (token == "1")
    {
        value = true;
        return true;
    }
    return false;
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

            // 解析格式: query,history / time,timestamp / send,code,bits,pulse,syncH,syncL,zeroH,zeroL,oneH,oneL,inverted(0/1),repeat
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
                        // 设置标志，由主循环处理，避免在BLE回调任务中长时间阻塞导致栈溢出
                        pendingHistoryQuery = true;
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
                    int cursor = firstComma + 1;
                    String token;

                    unsigned long code = 0;
                    unsigned int bitLength = 0;
                    uint16_t pulseLength = 0;
                    uint8_t params[6] = {0}; // syncHigh, syncLow, zeroHigh, zeroLow, oneHigh, oneLow
                    bool invertedSignal = false;
                    unsigned int repeatCount = 0;

                    bool ok = true;
                    ok = ok && parseCsvToken(cmdStr, cursor, token) && parseUnsignedLongStrict(token, code);
                    ok = ok && parseCsvToken(cmdStr, cursor, token) && parseUnsignedIntStrict(token, bitLength);
                    ok = ok && parseCsvToken(cmdStr, cursor, token) && parseUInt16Strict(token, pulseLength);
                    for (int i = 0; i < 6; ++i)
                    {
                        ok = ok && parseCsvToken(cmdStr, cursor, token) && parseUInt8Strict(token, params[i]);
                    }
                    ok = ok && parseCsvToken(cmdStr, cursor, token) && parseBool01(token, invertedSignal);
                    ok = ok && parseCsvToken(cmdStr, cursor, token) && parseUnsignedIntStrict(token, repeatCount);

                    if (ok)
                    {
                        bool paramsValid = true;
                        for (int i = 0; i < 6; ++i)
                        {
                            if (params[i] == 0)
                                paramsValid = false;
                        }
                        if (code > 0 && bitLength > 0 && bitLength <= 64 && pulseLength > 0 && paramsValid && repeatCount > 0)
                        {
                            // 存储发送参数，由主循环处理，避免在BLE回调任务中长时间占用导致栈溢出
                            pendingSendParams.code = code;
                            pendingSendParams.bitLength = bitLength;
                            pendingSendParams.pulseLength = pulseLength;
                            memcpy(pendingSendParams.params, params, 6);
                            pendingSendParams.invertedSignal = invertedSignal;
                            pendingSendParams.repeatCount = repeatCount;
                            pendingSendReady = true;
                        }
                        else
                        {
                            sendBLENotify("status", "错误: 参数无效");
                        }
                    }
                    else
                    {
                        sendBLENotify("status", "错误: send参数格式无效");
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
void processReceivedData()
{
    if (mySwitch.available())
    {

        unsigned long receivedCode = mySwitch.getReceivedValue();
        unsigned int bitLength = mySwitch.getReceivedBitlength();
        unsigned int protocol = mySwitch.getReceivedProtocol();
        unsigned int pulseLength = mySwitch.getReceivedDelay();
        uint8_t params[6] = {1, 31, 1, 3, 3, 1}; // syncHigh, syncLow, zeroHigh, zeroLow, oneHigh, oneLow
        bool invertedSignal = false;
        unsigned int repeatCount = 10;

        getCustomParamsFromProtocol(protocol, pulseLength, params, invertedSignal);

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
                        signalHistory[idx].bitLength = bitLength;
                        signalHistory[idx].pulseLength = pulseLength;
                        memcpy(signalHistory[idx].params, params, 6);
                        signalHistory[idx].invertedSignal = invertedSignal;
                        signalHistory[idx].repeatCount = repeatCount;
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
                    signalHistory[historyIndex].pulseLength = pulseLength;
                    memcpy(signalHistory[historyIndex].params, params, 6);
                    signalHistory[historyIndex].invertedSignal = invertedSignal;
                    signalHistory[historyIndex].repeatCount = repeatCount;
                    // 如果已同步时间则使用完整时间戳，否则仅保存millis()值供后续修正
                    signalHistory[historyIndex].timestamp = timeOffsetSynced ? (millis() + timeOffset) : millis();
                    historyIndex = (historyIndex + 1) % MAX_HISTORY;
                    if (historyCount < MAX_HISTORY)
                        historyCount++;
                }
            }
            else
            {
                // 已连接到主机，直接发送完整自定义参数+时间戳给主机
                uint64_t nowTs = timeOffsetSynced ? (millis() + timeOffset) : millis();
                String dataStr = String(receivedCode) + "," + String(bitLength) + "," +
                                 String(pulseLength) + "," + String(params[0]) + "," + String(params[1]) + "," +
                                 String(params[2]) + "," + String(params[3]) + "," + String(params[4]) + "," +
                                 String(params[5]) + "," + String(invertedSignal ? 1 : 0) + "," +
                                 String(repeatCount) + "," + String(nowTs);
                sendBLENotify("recv", dataStr);
            }
        }

        mySwitch.resetAvailable();
    }
}

// 处理待发送的433MHz信号（从BLE回调传递过来，避免在BLE任务中长时间阻塞）
void processSendData()
{
    if (pendingSendReady)
    {
        // 使能发射模块
        digitalWrite(RF_TRANSMIT_EN_PIN, HIGH);
        delay(10); // MOS管导通延时，确保模块上电
        pendingSendReady = false;
        // 适配 params[6] 构造协议
        RCSwitch::Protocol customProtocol = {
            pendingSendParams.pulseLength,
            {pendingSendParams.params[0], pendingSendParams.params[1]}, // syncHigh, syncLow
            {pendingSendParams.params[2], pendingSendParams.params[3]}, // zeroHigh, zeroLow
            {pendingSendParams.params[4], pendingSendParams.params[5]}, // oneHigh, oneLow
            pendingSendParams.invertedSignal};
        mySwitch.setProtocol(customProtocol);
        mySwitch.setRepeatTransmit(pendingSendParams.repeatCount);
        mySwitch.send(pendingSendParams.code, pendingSendParams.bitLength);
        Serial.print("BLE发送自定义433MHz信号: ");
        Serial.print(pendingSendParams.code);
        Serial.print(" (位长度: ");
        Serial.print(pendingSendParams.bitLength);
        Serial.print(", 脉宽: ");
        Serial.print(pendingSendParams.pulseLength);
        Serial.print(", 重发: ");
        Serial.print(pendingSendParams.repeatCount);
        Serial.println(")");
        String msg = "已发送: " + String(pendingSendParams.code);
        sendBLENotify("status", msg);
        // 关闭发射模块
        digitalWrite(RF_TRANSMIT_EN_PIN, LOW);
    }
}

// 处理历史记录查询（从BLE回调传递过来，避免在BLE任务中循环delay导致栈溢出）
void processHistoryQueryData()
{
    if (pendingHistoryQuery && bleDeviceConnected)
    {
        pendingHistoryQuery = false;
        if (historyCount == 0)
        {
            sendBLENotify("status", "无历史记录");
        }
        else
        {
            sendBLENotify("status", "历史记录: " + String(historyCount));
            delay(50);
            int startIdx = (historyCount < MAX_HISTORY) ? 0 : historyIndex;
            for (int i = 0; i < historyCount; i++)
            {
                int idx = (startIdx + i) % MAX_HISTORY;
                String histData = String(signalHistory[idx].code) + "," +
                                  String(signalHistory[idx].bitLength) + "," +
                                  String(signalHistory[idx].pulseLength) + "," +
                                  String(signalHistory[idx].params[0]) + "," +
                                  String(signalHistory[idx].params[1]) + "," +
                                  String(signalHistory[idx].params[2]) + "," +
                                  String(signalHistory[idx].params[3]) + "," +
                                  String(signalHistory[idx].params[4]) + "," +
                                  String(signalHistory[idx].params[5]) + "," +
                                  String(signalHistory[idx].invertedSignal ? 1 : 0) + "," +
                                  String(signalHistory[idx].repeatCount) + "," +
                                  String(signalHistory[idx].timestamp);
                sendBLENotify("history", histData);
                delay(50);
            }
        }
    }
}

void processBLEConnectionState()
{
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
}

void setup()
{
    Serial.begin(9600);
    delay(1000);
    Serial.println("\n\n=== RF 433MHz 控制器启动 ===");
    Serial.print("设备ID: ");
    Serial.println(deviceId);

    // 设置433MHz收发器
    // 初始化发射/接收模块使能引脚（MOS管控制）
    pinMode(RF_TRANSMIT_EN_PIN, OUTPUT);
    digitalWrite(RF_TRANSMIT_EN_PIN, LOW); // 默认低电平关闭发射模块
    pinMode(RF_RECEIVE_EN_PIN, OUTPUT);
    digitalWrite(RF_RECEIVE_EN_PIN, HIGH); // 默认高电平使能

    mySwitch.enableTransmit(RF_TRANSMIT_PIN);
    mySwitch.enableReceive(RF_RECEIVE_PIN);
    Serial.print("发射引脚: GPIO");
    Serial.println(RF_TRANSMIT_PIN);
    Serial.print("接收引脚: GPIO");
    Serial.println(RF_RECEIVE_PIN);
    Serial.print("发射模块使能引脚: GPIO");
    Serial.println(RF_TRANSMIT_EN_PIN);
    Serial.print("接收模块使能引脚: GPIO");
    Serial.println(RF_RECEIVE_EN_PIN);
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
    processBLEConnectionState();
    // 处理接收到的433MHz信号
    processReceivedData();
    // 处理待发送的433MHz信号
    processSendData();
    // 处理待处理的历史记录查询
    processHistoryQueryData();

    delay(10);
}
