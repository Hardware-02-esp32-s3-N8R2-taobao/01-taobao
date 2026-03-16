# IDF DS18B20 Demo

这是一个给 YD-ESP32-S3 准备的 `DS18B20 + OLED` 最小演示工程。

## GPIO 选择

`DS18B20` 我这里推荐接到：

- `DQ` -> `GPIO4`

原因是：

- `GPIO5 / GPIO6` 已经给 OLED 的 I2C 用了
- `GPIO48` 是板载 RGB
- `GPIO0` 是 `BOOT`
- `GPIO19 / GPIO20` 是原生 USB

所以 `GPIO4` 是当前最干净、最好用的一个口。

## 接线

### OLED

- `OLED VCC` -> `3V3`
- `OLED GND` -> `GND`
- `OLED SDA` -> `GPIO5`
- `OLED SCL` -> `GPIO6`

### DS18B20

如果你手上是普通三脚 `TO-92` 封装，平面朝向自己时，常见脚位是：

- 左脚 `GND`
- 中脚 `DQ`
- 右脚 `VDD`

接线如下：

- `DS18B20 GND` -> `GND`
- `DS18B20 DQ` -> `GPIO4`
- `DS18B20 VDD` -> `3V3`
- `4.7K` 电阻一端接 `GPIO4`
- `4.7K` 电阻另一端接 `3V3`

如果你的是防水探头版本，通常是：

- 黑线 -> `GND`
- 黄线/白线 -> `GPIO4`
- 红线 -> `3V3`
- 同样需要 `4.7K` 上拉到 `3V3`

## 功能

- 初始化 OLED
- 用 `GPIO4` 驱动 `DS18B20`
- 每 3 秒读取一次温度
- 把温度显示到 OLED
- 同时通过串口输出 JSON 日志

## 构建和烧录

```powershell
cd F:\01-dev-board\06-esp32s3\YD-ESP32-S3\02-pr\idf-ds18b20-demo
. D:\02-software-stash-cache\02-esp32-idf\Initialize-Idf.ps1
idf.py set-target esp32s3
idf.py build
idf.py -p COM4 flash
idf.py -p COM4 monitor
```

## 串口输出示例

```json
{"seq":1,"sensor":"DS18B20","temp_c":24.56}
```

## 没有识别到 DS18B20 时

屏幕会显示 `NO SENSOR`，串口会提示你重点检查：

- `GPIO4`
- `3V3 / GND`
- `4.7K` 上拉电阻
