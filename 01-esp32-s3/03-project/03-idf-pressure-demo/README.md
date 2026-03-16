# IDF Pressure Demo

这是一个给 YD-ESP32-S3 准备的最小气压计演示工程。

工程默认复用你现在已经在用的 I2C 接口：

- `SDA` -> `GPIO5`
- `SCL` -> `GPIO6`

同时支持把 OLED 和气压计挂在同一条 I2C 总线上：

- OLED 常见地址：`0x3C` / `0x3D`
- 气压计常见地址：`0x76` / `0x77`

## 当前假设

根据你给的图片，这个模块外形很像常见的 `BMP180 / BMP280 / BME280` 系列压力传感器模块。

为了降低误判风险，这个 demo 做了自动探测：

- `BMP180`：芯片 ID `0x55`
- `BMP280`：芯片 ID `0x58`
- `BME280`：芯片 ID `0x60`

## 接线

如果你的 OLED 还接着，可以和气压计并联在同一条 I2C 总线上：

- `OLED VCC` -> `3V3`
- `OLED GND` -> `GND`
- `OLED SDA` -> `GPIO5`
- `OLED SCL` -> `GPIO6`
- `Pressure VCC` -> `3V3`
- `Pressure GND` -> `GND`
- `Pressure SDA` -> `GPIO5`
- `Pressure SCL` -> `GPIO6`

## 功能

- 自动扫描 I2C 设备
- 自动识别 `BMP180 / BMP280 / BME280`
- 每 3 秒读取一次温度和气压
- 计算近似海拔
- 如果检测到 OLED，则显示：
  - 传感器型号
  - 气压 `hPa`
  - 温度 `C`
  - 海拔 `m`
- 同时通过串口输出一条 JSON 格式的“上报”日志

## 构建和烧录

```powershell
cd F:\01-dev-board\06-esp32s3\YD-ESP32-S3\02-pr\idf-pressure-demo
. D:\02-software-stash-cache\02-esp32-idf\Initialize-Idf.ps1
idf.py set-target esp32s3
idf.py build
idf.py -p COM4 flash
idf.py -p COM4 monitor
```

## 串口输出示例

```json
{"seq":1,"chip":"BMP180","temp_c":24.80,"pressure_pa":100812.00,"pressure_hpa":1008.12,"altitude_m":42.13}
```

## 如果没有识别到传感器

程序会先打印 I2C 扫描结果，方便你判断：

- 是否真的接到了 `GPIO5 / GPIO6`
- 供电是否是 `3V3`
- 模块是不是别的型号

如果你后面补一张模块正反面的完整照片，我也可以再把驱动进一步收窄到具体型号。
