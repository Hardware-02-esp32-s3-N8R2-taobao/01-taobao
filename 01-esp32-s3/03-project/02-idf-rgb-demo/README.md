# IDF RGB Demo

这是一个给 YD-ESP32-S3 准备的最小 ESP-IDF C 工程。

特性：

- 目标芯片：`ESP32-S3`
- 日志输出：`USB Serial/JTAG`
- 板载灯：`WS2812`
- 数据引脚：`GPIO48`
- 实测容量：`8MB Flash + 2MB PSRAM`

运行效果：

- 串口日志会循环打印当前颜色
- 板载 RGB 灯会按红、绿、蓝、白顺序循环

默认烧录端口按当前机器连接情况使用 `COM3`。

## 本机实测结果

- 芯片：`ESP32-S3 rev v0.2`
- 外部 Flash：`8MB`
- 内部/封装 PSRAM：`2MB`
- 原生 USB 串口：会在不同阶段显示为 `COM3` 或 `COM4`

## 常用命令

先进入工程目录：

- `F:\01-dev-board\06-esp32s3\YD-ESP32-S3\02-pr\idf-rgb-demo`

然后在 PowerShell 里执行：

```powershell
. D:\02-software-stash-cache\02-esp32-idf\Initialize-Idf.ps1
idf.py build
idf.py -p COM3 flash
idf.py -p COM3 monitor
```

如果 `COM3` 不通，就到设备管理器看一下当前是 `COM4` 还是别的端口，再替换命令里的端口号。
