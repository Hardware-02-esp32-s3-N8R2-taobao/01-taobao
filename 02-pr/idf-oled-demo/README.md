# IDF OLED Demo

这是一个给 YD-ESP32-S3 准备的 0.96 寸 I2C OLED 最小 ESP-IDF 演示工程。

默认按常见的 `SSD1306 128x64 I2C OLED` 模块编写，默认 I2C 地址为 `0x3C`。

## 接线

OLED 常见引脚是：

- `VCC`
- `GND`
- `SCL`
- `SDA`

推荐你接到这块板子上：

- `OLED VCC` -> `3V3`
- `OLED GND` -> `GND`
- `OLED SDA` -> `GPIO5`
- `OLED SCL` -> `GPIO6`

选择这组口的原因是它们都在板边排针上，且避开了：

- 原生 USB 使用的 `GPIO19/20`
- 板载 RGB 使用的 `GPIO48`
- `BOOT` 按键使用的 `GPIO0`
- 常规串口下载使用的 `TX/RX`

## 运行效果

屏幕点亮后会显示：

- 标题：`YD-ESP32-S3`
- `Temp`
- `Humi`
- `Refresh`

其中温度和湿度是模拟数据，每 `3` 秒刷新一次。

## 说明

- 如果你的 OLED 模块地址不是 `0x3C`，而是 `0x3D`，请改 [main.c](F:/01-dev-board/06-esp32s3/YD-ESP32-S3/02-pr/idf-oled-demo/main/main.c) 里的 `OLED_I2C_ADDR`
- 这个版本没有使用 LVGL，而是直接驱动 SSD1306，结构更简单，更适合做最小验证
