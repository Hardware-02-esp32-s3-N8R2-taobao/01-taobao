# ESP32-C3 SuperMini 传感器接线说明

本文档基于原理图 [ESP32-C3-SuperMini-原理图-sudo.is.png](/f:/01-dev-board/06-esp32s3/YD-ESP32-S3/01-firmware/12-esp32-c3-other/01-document/01-ESP32-C3-SuperMini/02-%E5%8E%9F%E7%90%86%E5%9B%BE/ESP32-C3-SuperMini-%E5%8E%9F%E7%90%86%E5%9B%BE-sudo.is.png) 整理，优先使用原理图中的 `H1`、`H2` 排针序号说明接线。

## 1. H1 / H2 排针与 GPIO 对照

### H2 排针

| 排针位 | 信号 | 说明 |
| --- | --- | --- |
| H2-8 | VBUS | USB 5V 输入 |
| H2-7 | GND | 地 |
| H2-6 | 3V3 | 3.3V 电源 |
| H2-5 | GPIO4 / ADC1 / SDA | 推荐作为 I2C SDA |
| H2-4 | GPIO3 | 推荐作为 DHT11 DATA |
| H2-3 | GPIO2 | 推荐作为单总线或普通数字输入 |
| H2-2 | GPIO1 / U0RXD | 可作 ADC/数字 IO，避免与外部 UART 冲突 |
| H2-1 | GPIO0 / U0TXD | 可作 ADC/数字 IO，避免与外部 UART 冲突 |

### H1 排针

| 排针位 | 信号 | 说明 |
| --- | --- | --- |
| H1-1 | GPIO5 / ADC2 / SCL | 推荐作为 I2C SCL |
| H1-2 | GPIO6 | 通用数字 IO |
| H1-3 | GPIO7 | 通用数字 IO |
| H1-4 | GPIO8 / PWM | 板载 LED 所在引脚，必要时可做 PWM |
| H1-5 | GPIO9 / BOOT | 启动相关引脚，不建议接普通传感器 |
| H1-6 | GPIO10 | 通用数字 IO |
| H1-7 | GPIO20 | 通用数字 IO |
| H1-8 | GPIO21 | 通用数字 IO |

## 2. 当前固定推荐接法

### DHT11

- VCC -> `H2-6 (3V3)`
- GND -> `H2-7 (GND)`
- DATA -> `H2-4 (GPIO3)`

说明：

- 固件默认已经改为 `GPIO3`
- 这样可以把 `GPIO4` 留给 I2C 总线

### I2C 总线

- SDA -> `H2-5 (GPIO4)`
- SCL -> `H1-1 (GPIO5)`
- VCC -> `H2-6 (3V3)`
- GND -> `H2-7 (GND)`

说明：

- 所有 I2C 传感器统一挂这两根线上
- 多个 I2C 设备并联时，共用 `SDA/SCL/3V3/GND`

## 3. 各类传感器推荐接线

### DHT11 温湿度

- 数据脚 -> `H2-4 (GPIO3)`
- 电源 -> `H2-6 / H2-7`

### DS18B20 温度

- DATA -> `H2-3 (GPIO2)`
- VCC -> `H2-6 (3V3)`
- GND -> `H2-7 (GND)`
- 额外需要在 `DATA` 和 `3V3` 之间加 `4.7k` 上拉电阻

### BH1750 光照

- SDA -> `H2-5 (GPIO4)`
- SCL -> `H1-1 (GPIO5)`
- VCC -> `H2-6 (3V3)`
- GND -> `H2-7 (GND)`
- ADDR 如需改地址，可按模块说明接 `GND` 或 `3V3`

### BMP180 气压

- SDA -> `H2-5 (GPIO4)`
- SCL -> `H1-1 (GPIO5)`
- VCC -> 优先接模块允许的电压，常见可接 `H2-6 (3V3)`
- GND -> `H2-7 (GND)`

### BMP280 环境数据（旧版兼容说明）

- SDA -> `H2-5 (GPIO4)`
- SCL -> `H1-1 (GPIO5)`
- VCC -> `H2-6 (3V3)`
- GND -> `H2-7 (GND)`

### BME280 温湿压（旧版兼容说明）

- SDA -> `H2-5 (GPIO4)`
- SCL -> `H1-1 (GPIO5)`
- VCC -> `H2-6 (3V3)`
- GND -> `H2-7 (GND)`

### 土壤湿度传感器

模拟型模块推荐：

- AO -> `H2-2 (GPIO1)`
- VCC -> 按模块要求接 `3V3` 或 `VBUS`
- GND -> `H2-7 (GND)`

带数字比较器的模块推荐：

- DO -> `H1-6 (GPIO10)`
- VCC -> 按模块要求接 `3V3` 或 `VBUS`
- GND -> `H2-7 (GND)`

说明：

- 若模块输出电压可能高于 `3.3V`，必须先做分压或电平转换
- 若同时使用模拟型和外部 UART，优先避免占用 `GPIO0/GPIO1`

### 雨滴传感器

模拟量读取推荐：

- AO -> `H2-1 (GPIO0)`
- VCC -> 按模块要求接 `3V3` 或 `VBUS`
- GND -> `H2-7 (GND)`

数字量触发推荐：

- DO -> `H1-2 (GPIO6)`
- VCC -> 按模块要求接 `3V3` 或 `VBUS`
- GND -> `H2-7 (GND)`

## 4. 统一接线建议

- `GPIO3` 固定留给 `DHT11`
- `GPIO4 + GPIO5` 固定留给 `I2C`
- `GPIO2` 优先留给 `DS18B20`
- `GPIO0 / GPIO1` 优先留给 ADC 类传感器
- `GPIO6 / GPIO10 / GPIO20 / GPIO21` 优先留给普通数字传感器
- `GPIO8` 连着板载 LED，不建议优先分给传感器
- `GPIO9` 是 `BOOT` 引脚，不建议接普通传感器

## 5. 接线时的注意事项

- 全部传感器必须与开发板共地
- 只支持 `3.3V` IO 的引脚，禁止直接输入 `5V`
- I2C 设备不要重复地址，重复时需要改地址焊点或换总线方案
- 模拟传感器如果输出范围不确定，先测电压再接入 ADC 引脚
- 若后续需要外接 UART 设备，避免占用 `H2-1`、`H2-2`

## 6. 当前项目默认口径

- `DHT11` 默认引脚：`H2-4 / GPIO3`
- `I2C` 默认总线：`H2-5 / GPIO4 (SDA)` + `H1-1 / GPIO5 (SCL)`

当前工程已切换到 `BMP180` 驱动；后续如果固件新增 `BH1750`、`BMP180`、`DS18B20` 等传感器，优先按本文档的排针定义接线。
