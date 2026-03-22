# YD-ESP32-S3

这是一个围绕 `ESP32-C3 SuperMini` 搭建的传感器上报与展示项目。

当前项目的核心目标是：

- 让 `ESP32-C3` 采集传感器数据
- 通过 `Wi-Fi + MQTT` 把数据上报到服务端
- 通过网页展示最新状态和历史数据
- 通过电脑端上位机查看串口日志和设备运行状态

当前已经落地的主链路是：

- 固件端：`DHT11 + Wi-Fi + MQTT + 串口日志`
- App 端：`Python + Qt` 串口监控工具
- 服务端：`Node.js` 网页与数据展示服务

## 目录结构

```text
YD-ESP32-S3/
├── 00-requirement/   # 正式需求、架构、协议文档
├── 01-firmware/      # 固件工程与烧录脚本
├── 02-app/           # 电脑端上位机
├── 03-server/        # 网页服务端与网页入口
└── README.md
```

当前主要目录说明：

- `00-requirement`
  - 保存正式文档，包含固件需求、网页架构、App 架构、通信协议和目录说明
- `01-firmware/00-main-c3-firmware`
  - 当前主固件工程
- `01-firmware/flash_firmware.py`
  - 当前统一烧录入口脚本
- `02-app/01-c3-monitor`
  - 当前桌面串口监控上位机
- `03-server/01-net-display-server`
  - 当前网页服务端与前端页面

## 项目文档

如果你想先了解设计和约定，建议优先看：

- [00-项目目录说明.md](/f:/01-dev-board/06-esp32s3/YD-ESP32-S3/00-requirement/00-%E9%A1%B9%E7%9B%AE%E7%9B%AE%E5%BD%95%E8%AF%B4%E6%98%8E.md)
- [01-ESP32-C3-固件需求与架构.md](/f:/01-dev-board/06-esp32s3/YD-ESP32-S3/00-requirement/01-ESP32-C3-%E5%9B%BA%E4%BB%B6%E9%9C%80%E6%B1%82%E4%B8%8E%E6%9E%B6%E6%9E%84.md)
- [02-网页架构.md](/f:/01-dev-board/06-esp32s3/YD-ESP32-S3/00-requirement/02-%E7%BD%91%E9%A1%B5%E6%9E%B6%E6%9E%84.md)
- [03-App实现与架构.md](/f:/01-dev-board/06-esp32s3/YD-ESP32-S3/00-requirement/03-App%E5%AE%9E%E7%8E%B0%E4%B8%8E%E6%9E%B6%E6%9E%84.md)
- [04-通信协议说明.md](/f:/01-dev-board/06-esp32s3/YD-ESP32-S3/00-requirement/04-%E9%80%9A%E4%BF%A1%E5%8D%8F%E8%AE%AE%E8%AF%B4%E6%98%8E.md)

## 当前主固件

当前主固件位于：

- `01-firmware/00-main-c3-firmware`

当前固件已实现：

- 自动连接 Wi-Fi
- 自动连接 MQTT
- 读取 `DHT11`
- 周期上报数据
- 串口输出调试日志

当前已确认接线：

- `DHT11 VCC -> 3V3`
- `DHT11 GND -> GND`
- `DHT11 DATA -> GPIO4`

## 如何使用

### 1. 烧录固件

推荐直接使用仓库里的烧录脚本：

```powershell
& C:/Users/10243/AppData/Local/Microsoft/WindowsApps/python3.13.exe .\01-firmware\flash_firmware.py
```

如果串口不是 `COM3`，可以这样指定：

```powershell
& C:/Users/10243/AppData/Local/Microsoft/WindowsApps/python3.13.exe .\01-firmware\flash_firmware.py --port COM7
```

如果需要烧录后直接打开串口监视：

```powershell
& C:/Users/10243/AppData/Local/Microsoft/WindowsApps/python3.13.exe .\01-firmware\flash_firmware.py --port COM7 --monitor
```

### 2. 手动编译固件

主工程目录中仍保留原始编译入口：

```powershell
& C:/Users/10243/AppData/Local/Microsoft/WindowsApps/python3.13.exe .\01-firmware\00-main-c3-firmware\build_firmware.py
```

### 3. 启动电脑端上位机

推荐直接运行：

- `02-app/启动上位机.bat`

或者：

```powershell
& C:/Users/10243/AppData/Local/Microsoft/WindowsApps/python3.13.exe .\02-app\启动上位机.py
```

上位机主要用于：

- 查看串口连接状态
- 查看 Wi-Fi / MQTT 状态
- 查看最近一次传感器值
- 查看最近一次上报载荷
- 查看完整原始日志

### 4. 打开网页端

推荐直接运行：

- `03-server/打开网页.bat`

或者：

```powershell
& C:/Users/10243/AppData/Local/Microsoft/WindowsApps/python3.13.exe .\03-server\打开网页.py
```

网页服务端主体位于：

- `03-server/01-net-display-server`

## 当前通信方式

当前项目的主要通信方式如下：

- `C3 -> 服务端`
  - `Wi-Fi + MQTT + JSON`
- `C3 -> App`
  - `USB Serial / UART + 文本日志`
- `服务端 -> 网页`
  - `HTTP API`

当前主固件已使用过的 MQTT 主题：

- `garden/flower/dht11`

当前主固件实际连接的 MQTT 入口：

- `mqtt://117.72.55.63:1884`

说明：

- 当前真实生效的是 `MQTT TCP 1884`
- 不是旧的 `ws://117.72.55.63/mqtt`

## 当前状态

当前仓库更偏向“可持续整理和扩展的主工程”，不是一次性 demo 目录。

已经收敛的原则是：

- 需求和协议统一写入 `00-requirement`
- 主固件只保留一个主工程
- 烧录入口统一到 `01-firmware/flash_firmware.py`
- App、Server、Firmware 各自独立

如果后续继续扩展多传感器、多设备或 BLE 配网，建议先更新 `00-requirement` 中的正式文档，再进入实现。
