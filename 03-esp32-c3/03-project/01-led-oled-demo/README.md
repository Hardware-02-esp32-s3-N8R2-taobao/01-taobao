# 01-led-oled-demo

这是一个给 `ESP32-C3 SuperMini` 准备的最小 `LED + OLED` 演示工程。

工程已经按模块拆分，方便后面继续扩展：

- `main/app_main.c`：应用入口
- `main/led/`：LED 驱动
- `main/oled/`：SSD1306 OLED 驱动
- `main/app/`：界面渲染逻辑
- `main/include/`：公共头文件

## 默认假设

- 目标芯片：`ESP32-C3`
- 板载 LED：`GPIO8`
- OLED 型号：`SSD1306 128x64 I2C`
- OLED 地址：自动探测 `0x3C` / `0x3D`

## OLED 接线

- `OLED VCC` -> `3V3`
- `OLED GND` -> `GND`
- `OLED SDA` -> `GPIO4`
- `OLED SCL` -> `GPIO5`

## 功能

- 板载 LED 每 `500ms` 闪烁一次
- 如果检测到 OLED，会显示动态状态页：
  - `ESP32-C3`
  - `LED:ON/OFF`
  - `OLED:ACTIVE`
  - `COUNT`
  - 右侧动态进度条，会随着闪烁计数移动
- 如果没有接 OLED，程序仍会继续运行，仅串口打印提示

## 编译烧录

```powershell
. $env:IDF_PATH\export.ps1
cd f:\01-dev-board\06-esp32s3\YD-ESP32-S3\03-esp32-c3\03-project\01-led-oled-demo
idf.py set-target esp32c3
idf.py build
idf.py -p COM6 flash
idf.py -p COM6 monitor
```
