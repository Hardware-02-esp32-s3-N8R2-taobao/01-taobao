# 02-led-blink

最基础的板载 LED 闪烁工程。

当前工程假设：

- 目标芯片：`ESP32`
- LED 引脚：`GPIO2`
- 串口监视：`115200`

## 功能

- 每 `500ms` 切换一次 `GPIO2`
- 串口打印当前 LED 状态

## 说明

很多 `DOIT ESP32 DevKit V1` 或兼容板会把板载 LED 接到 `GPIO2`。

但不同厂家版本可能存在两种情况：

- 高电平点亮
- 低电平点亮

如果你看到串口在打印 `LED ON/OFF`，但灯的亮灭刚好反过来，这是正常的板级差异。
这种情况下只需要把 `main.c` 里输出电平反一下即可。

## 终端编译与烧录

```powershell
. E:\Espressif\Initialize-Idf.ps1
cd d:\56-esp32\01-taobao\02-esp32\02-project\02-led-blink
idf.py set-target esp32
idf.py build
idf.py -p COM84 flash
idf.py -p COM84 monitor
```
