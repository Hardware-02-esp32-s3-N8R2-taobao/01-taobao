# C3 Serial Monitor

这是一个给 `ESP32-C3` 用的上位机，基于 `Python + Qt`。

它会通过串口连接开发板，持续解析当前固件已经输出的日志，并汇总成下面这些状态：

- 串口连接状态
- 板子启动状态
- WiFi 状态、IP、RSSI
- MQTT / 服务器连接状态
- DHT11 传感器最近一次温湿度
- 最近一次上报载荷
- 原始日志窗口

当前版本不依赖固件新增协议，直接兼容现有日志格式：

- `WiFi connected, ip=...`
- `WiFi disconnected, reason=...`
- `MQTT connected`
- `MQTT disconnected`
- `DHT11 sample: temperature=... humidity=...`
- `publish ok: {...}`
- `waiting dht11/wifi/mqtt...`

## 运行

推荐直接双击：

`02-app\启动上位机.bat`

脚本会自动：

1. 创建本地虚拟环境
2. 安装依赖
3. 启动上位机

## 后续扩展

如果后面你要做真正的“查询指令”，可以继续在固件里增加串口命令，这个上位机可以直接扩展成命令面板。
