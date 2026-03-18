# 02-wifi-mqtt-forward-demo

这是一个给 `ESP32-C3 SuperMini` 准备的 `WiFi + MQTT` 最小演示工程。

## 默认配置

- WiFi SSID: `Ermao`
- WiFi Password: `gf666666`
- MQTT Forward Entry: `mqtt://117.72.55.63:11883`
- 这个端口是公网服务器反向转发到 `192.168.2.123:1883`
- 当前不需要 MQTT 账号密码
- Publish Topic: `garden/flower/dht11`
- DHT11 Data GPIO: `GPIO4`
- OLED I2C: `SDA=GPIO8` `SCL=GPIO9`

## 功能

- 上电自动连接 WiFi
- 读取 `DHT11`
- 连上公网 MQTT 转发入口
- 每 `3` 秒发布一次真实温湿度数据
- 数据内容包含：
  - 板卡名称
  - DHT11 温度
  - DHT11 湿度
  - RSSI
  - 本地 IP

## 说明

公网服务器上的 MQTT broker 已部署完成在 `117.72.55.63:1883`。

而这个工程为了满足“公网只做转发，实际运行仍在 `192.168.2.123`”这一目标，默认直接连接 `117.72.55.63:11883`。
这个端口会反向转发到 `192.168.2.123` 上项目内置的 MQTT 服务，topic 也已对齐为 `garden/flower/dht11`。

## 接线建议

- `DHT11 VCC` -> `3V3`
- `DHT11 GND` -> `GND`
- `DHT11 DATA` -> `GPIO4`

选择 `GPIO4` 的原因：

- 避开板载 OLED 的 `GPIO8/9`
- 避开原生 USB 相关脚
- 避开启动相关脚

## 编译烧录

```powershell
$env:IDF_PYTHON_ENV_PATH="D:\02-software-stash-cache\02-esp32-idf\python_env\idf5.1_py3.11_env"
. $env:IDF_PATH\export.ps1
cd f:\01-dev-board\06-esp32s3\YD-ESP32-S3\03-esp32-c3\03-project\02-wifi-mqtt-forward-demo
idf.py set-target esp32c3
idf.py build
idf.py -p COM6 flash
idf.py -p COM6 monitor
```
