RFTransmitter
=============

项目简介
-	基于 ESP32-C3 的 433MHz RF 接收/发送 BLE 控制器。
-	通过 BLE 接收控制命令（发送 RF、查询历史、同步时间），并可把接收到的 RF 信号通知到手机。

主要特性
-	433MHz 信号接收与发送（使用 RCSwitch）
-	BLE 服务：读取/通知和写入命令
-	离线时保存信号历史（环形缓冲）
-	连接后未同步时间则自动断开（防止历史时间混乱）
-	GPIO10 上点动开关（按下接 GND），长按 5 秒重启设备（使用 OneButton）

硬件接线
-	433MHz 发射引脚：GPIO8（RF_TRANSMIT_PIN）
-	433MHz 接收引脚：GPIO6（RF_RECEIVE_PIN）
-	点动开关：GPIO10（BUTTON_PIN），按下接 GND，使用上拉输入（active-low）

BLE 协议（简要）
-	Service UUID: 4fafc201-1fb5-459e-8fcc-c5c9c331914b
-	Notify 特征 UUID: beb5483e-36e1-4688-b7f5-ea07361b26a0
-	Command 特征 UUID: beb5483e-36e1-4688-b7f5-ea07361b26a1

命令格式（写入 Command 特征）
-	`time,<timestamp>`
-		同步时间戳（毫秒）。设备计算 timeOffset = receivedTime - millis()，并用其修正历史记录时间。
-	`send,<code>,<bitLength>,<protocol>`
-		发送 433MHz 信号（code 为数字）。
-	`query,history`
-		请求设备发送保存的历史记录（当设备未连接时会保存接收的信号）。

通知格式（Notify）
-	`status,<text>`
-	`history,<code>,<bitLength>,<protocol>,<timestamp>`
-	`recv,<code>,<bitLength>,<protocol>`

历史记录
-	最大条数由源码中的 `MAX_HISTORY` 控制（默认 30）。
-	历史记录在设备未连接时保存；连接并同步时间后可通过 `query,history` 获取。

连接后时间同步策略
-	设备在连接后会等待 `time,<timestamp>` 命令以同步真实时间。
-	如果在超时时间内未收到同步命令，设备会主动断开连接以避免历史记录时间不一致（源码常量 `TIME_SYNC_TIMEOUT_MS` 可调整）。

按键功能
-	GPIO10 使用 OneButton，按下接 GND（active-low）。
-	长按 5 秒会触发 `ESP.restart()`，重启设备。
