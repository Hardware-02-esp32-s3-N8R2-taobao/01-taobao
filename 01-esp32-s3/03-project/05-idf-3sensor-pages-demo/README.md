# IDF 3 Sensor Pages Demo

这是一个给 YD-ESP32-S3 准备的四传感器分页显示工程。

它会把下面四个传感器整合到同一块 OLED 上，页面由外接按钮手动翻页：

1. 气压计页面
2. `DS18B20` 页面
3. `DHT11` 页面
4. `BH1750` 页面

## GPIO 分配

### 传感器和显示器

- OLED I2C `SDA` -> `GPIO5`
- OLED I2C `SCL` -> `GPIO6`
- 气压计 `SDA` -> `GPIO5`
- 气压计 `SCL` -> `GPIO6`
- `DS18B20 DQ` -> `GPIO7`
- `DHT11 DATA` -> `GPIO4`
- `BH1750 SDA` -> `GPIO5`
- `BH1750 SCL` -> `GPIO6`

### 开发板上已经占用或建议保留的 IO

- 翻页按钮 -> `GPIO15`
- 板载 RGB -> `GPIO48`
- `BOOT` 按键 -> `GPIO0`
- 原生 USB -> `GPIO19 / GPIO20`

这样分配的原因是：

- `GPIO5 / GPIO6` 已经稳定用于 OLED 和 I2C 气压计
- `BH1750` 也是 I2C 设备，可以直接并在 `GPIO5 / GPIO6`
- `GPIO7` 继续给 `DS18B20` 使用，方便和你现在的实际接线保持一致
- `GPIO4` 改给 `DHT11` 使用，不影响 I2C、按钮或板载资源
- `GPIO15` 可以稳定作为普通输入口，用来接翻页按钮比较合适

## 接线总表

| 模块 | 引脚 | 开发板连接 |
| --- | --- | --- |
| OLED | VCC | `3V3` |
| OLED | GND | `GND` |
| OLED | SDA | `GPIO5` |
| OLED | SCL | `GPIO6` |
| 气压计 | VCC | `3V3` |
| 气压计 | GND | `GND` |
| 气压计 | SDA | `GPIO5` |
| 气压计 | SCL | `GPIO6` |
| BH1750 / GY-302 | VCC | `3V3` |
| BH1750 / GY-302 | GND | `GND` |
| BH1750 / GY-302 | SDA | `GPIO5` |
| BH1750 / GY-302 | SCL | `GPIO6` |
| DS18B20 | VDD | `3V3` |
| DS18B20 | GND | `GND` |
| DS18B20 | DQ | `GPIO7` |
| DHT11 | VCC | `3V3` |
| DHT11 | GND | `GND` |
| DHT11 | DATA | `GPIO4` |
| 翻页按钮 | 一端 | `GPIO15` |
| 翻页按钮 | 另一端 | `GND` |

## IO 口说明

- `GPIO5 / GPIO6`：整套工程唯一的 I2C 总线，同时挂了 OLED、气压计和 `BH1750`
- `GPIO7`：单总线温度传感器 `DS18B20`
- `GPIO4`：单线数字温湿度传感器 `DHT11`
- `GPIO15`：外接翻页按钮输入，使用内部上拉
- `GPIO48`：板载 `WS2812 RGB`，本工程启动时会主动关灯，不建议再外接别的设备
- `GPIO0`：开发板 `BOOT` 键，保留作下载模式
- `GPIO19 / GPIO20`：ESP32-S3 原生 USB 数据口，不建议用于普通 GPIO

## I2C 总线说明

- OLED、气压计和 `BH1750` 是并联在同一组 `GPIO5 / GPIO6` 上的
- 所有这些模块都必须共地，也就是都接到同一个 `GND`
- 电源统一建议接 `3V3`
- 常见 I2C 地址：
- OLED 通常是 `0x3C` 或 `0x3D`
- `GY-302 / BH1750` 在 `ADDR` 没接时通常是 `0x23`
- `BH1750` 如果 `ADDR` 拉高，通常会变成 `0x5C`
- 气压计常见是 `0x77`，也可能因模块不同而变化
- 同一条 I2C 总线上地址不能冲突，只要地址不同，就可以一起工作

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

当前工程里已经兼容了之前接过的 I2C 气压计模块，继续和 OLED、`BH1750` 共用同一组 I2C 引脚。

### BH1750 / GY-302

- `VCC` -> `3V3`
- `GND` -> `GND`
- `SDA` -> `GPIO5`
- `SCL` -> `GPIO6`

你这个 `GY-302` 模块的 `ADDR` 没有接时，默认一般是 `0x23`。
程序里我同时兼容了：

- `0x23`：`ADDR` 悬空或接地
- `0x5C`：`ADDR` 接高电平

### DS18B20

- `GND` -> `GND`
- `DQ` -> `GPIO7`
- `VDD` -> `3V3`
- `GPIO7` 和 `3V3` 之间加 `4.7K` 上拉

如果你用的是已经做成小模块的 `DS18B20`，很多模块板上已经带了上拉电阻，但裸管一般需要你自己加。

### DHT11

- `GND` -> `GND`
- `DATA` -> `GPIO4`
- `VCC` -> `3V3`
- `GPIO4` 和 `3V3` 之间建议加 `10K` 上拉

如果你手上是 DHT11 小模块板，通常板上已经带了上拉电阻，也能直接接。

### 翻页按钮

- 按钮一端 -> `GPIO15`
- 按钮另一端 -> `GND`
- 程序已经开启了 `GPIO15` 内部上拉，普通轻触按钮可以直接接
- 按下时输入变低电平，系统就翻到下一页

## 当前整机连线建议

- `3V3`：同时供电给 OLED、气压计、`BH1750`、`DS18B20`、`DHT11`
- `GND`：所有模块共地
- `GPIO5`：I2C `SDA`
- `GPIO6`：I2C `SCL`
- `GPIO7`：`DS18B20`
- `GPIO4`：`DHT11`
- `GPIO15`：外接翻页按钮
- `GPIO0`：保留，不接传感器和按钮

## 翻页方式

- 按一下接在 `GPIO15` 上的按钮，OLED 就切到下一页
- 页面不会再自动轮播
- 传感器数据仍然会每 3 秒刷新一次
- `BOOT(GPIO0)` 继续保留给下载模式和刷机使用

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

### 第 4 页

- `BH1750`
- 光照 `lux`

`BH1750` 本身是光照传感器，不提供温度数据，所以这一页只显示照度。

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
