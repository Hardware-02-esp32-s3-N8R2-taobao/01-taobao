# IDF 3 Sensor Pages Demo

这是一个给 YD-ESP32-S3 准备的三传感器分页显示工程。

它会把下面三个传感器整合到同一块 OLED 上，并每 3 秒自动切换一页：

1. 气压计页面
2. `DS18B20` 页面
3. `DHT11` 页面

## GPIO 分配

- OLED I2C `SDA` -> `GPIO5`
- OLED I2C `SCL` -> `GPIO6`
- `DS18B20 DQ` -> `GPIO4`
- `DHT11 DATA` -> `GPIO7`

这样分配的原因是：

- `GPIO5 / GPIO6` 已经稳定用于 OLED 和 I2C 气压计
- `GPIO4` 已经验证过适合 `DS18B20`
- `GPIO7` 不和 `RGB(GPIO48)`、`BOOT(GPIO0)`、USB 或前面两个传感器冲突

## 接线

### OLED

- `VCC` -> `3V3`
- `GND` -> `GND`
- `SDA` -> `GPIO5`
- `SCL` -> `GPIO6`

### 气压计

- `VCC` -> `3V3`
- `GND` -> `GND`
- `SDA` -> `GPIO5`
- `SCL` -> `GPIO6`

### DS18B20

- `GND` -> `GND`
- `DQ` -> `GPIO4`
- `VDD` -> `3V3`
- `GPIO4` 和 `3V3` 之间加 `4.7K` 上拉

### DHT11

- `GND` -> `GND`
- `DATA` -> `GPIO7`
- `VCC` -> `3V3`
- `GPIO7` 和 `3V3` 之间建议加 `10K` 上拉

如果你手上是 DHT11 小模块板，通常板上已经带了上拉电阻，也能直接接。

## 页面内容

### 第 1 页

- 气压计型号
- 气压 `hPa`
- 温度 `C`
- 海拔 `m`

### 第 2 页

- `DS18B20`
- 温度 `C`

### 第 3 页

- `DHT11`
- 温度 `C`
- 湿度 `%`

## 构建和烧录

```powershell
cd F:\01-dev-board\06-esp32s3\YD-ESP32-S3\02-pr\idf-3sensor-pages-demo
. D:\02-software-stash-cache\02-esp32-idf\Initialize-Idf.ps1
idf.py set-target esp32s3
idf.py build
idf.py -p COM4 flash
idf.py -p COM4 monitor
```

## 串口日志

程序也会同步输出每个传感器的采样结果，便于调试和后续“上报”。
