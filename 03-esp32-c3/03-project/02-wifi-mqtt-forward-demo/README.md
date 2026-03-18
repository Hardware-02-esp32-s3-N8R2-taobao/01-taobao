# 02-wifi-mqtt-forward-demo

本文件记录本次 session 中与 `ESP32-C3 SuperMini` `WiFi + MQTT + DHT11` 项目相关的关键需求、部署信息、账号密码、接线方式、主题设计和消息格式，方便后续继续维护。

## 1. 项目需求

本项目的目标是让一块 `ESP32-C3 SuperMini` 开发板：

- 连接指定 WiFi
- 读取 `DHT11` 温湿度
- 每隔 `3` 秒通过 `MQTT` 向服务器发送一次数据
- 数据最终进入内网服务器 `192.168.2.123` 上部署的项目
- 公网服务器只负责转发，不承担业务主服务
- 串口同步打印温湿度，方便和服务器页面数值对照

本次排查中已确认：

- `DHT11` 实际接在 `GPIO4`
- 之前读不到数据的原因是接线错误
- 现在 `DHT11` 温湿度读取已经恢复正常
- `OLED` 当前不接，且已从本工程运行逻辑中屏蔽

## 2. 当前工程内容

本工程路径：

- `F:\01-dev-board\06-esp32s3\YD-ESP32-S3\03-esp32-c3\03-project\02-wifi-mqtt-forward-demo`

当前功能：

- 自动连接 WiFi
- 自动连接 MQTT
- 读取 `DHT11`
- 每 `3000 ms` 发布一次温湿度 JSON
- 串口打印：
  - `DHT11 sample: temperature=... humidity=...`
  - `publish ok: {...}`
  - 失败时打印错误信息

## 3. WiFi 信息

- SSID: `Ermao`
- Password: `gf666666`

## 4. 公网服务器信息

公网服务器仅用于公网入口与转发。

- IP: `117.72.55.63`
- SSH 用户名: `root`
- SSH 密码: `1qaz2wsx@`

公网服务器当前承担的角色：

- 提供 HTTP 公网访问入口
- 提供 MQTT 公网入口
- 作为反向隧道落点
- 将公网访问转发到内网服务器 `192.168.2.123`

## 5. 内网服务器信息

内网服务器是真正运行项目服务的机器。

- IP: `192.168.2.123`
- SSH 用户名: `zerozero`
- SSH 密码: `00@Wuxian`

项目路径：

- `/home/zerozero/01-code/05-new-net-display`

已确认服务信息：

- HTTP 服务端口: `3000`
- 项目内置 MQTT 服务端口: `1883`

## 6. 本地同步代码目录

已经将内网项目同步到本地：

- `F:\01-dev-board\06-esp32s3\YD-ESP32-S3\04-server\05-new-net-display`

## 7. 公网访问方式

当前公网访问内网项目的方式：

- HTTP 访问地址: `http://117.72.55.63/`

说明：

- 公网服务器上的 `Nginx` 负责反向代理
- 代理目标通过反向隧道连接到 `192.168.2.123:3000`
- 也就是公网服务器本身只做入口与转发

## 8. MQTT 转发关系

### 8.1 公网服务器上的 MQTT

公网服务器上已经部署了 MQTT broker：

- TCP MQTT: `117.72.55.63:1883`
- WebSocket MQTT: `117.72.55.63:9001`
- 用户名: `c3client`
- 密码: `C3mqtt@2026`

说明：

- 这是公网 broker
- 后续如果其他设备需要统一接入公网 MQTT，可直接用它

### 8.2 当前 C3 工程实际连接的 MQTT

本工程当前使用的是“公网只做转发”的链路：

- MQTT URI: `mqtt://117.72.55.63:11883`
- MQTT 用户名: 空
- MQTT 密码: 空

说明：

- `117.72.55.63:11883` 不是独立 broker
- 这个端口通过反向隧道转发到 `192.168.2.123:1883`
- 因此 `ESP32-C3` 实际上是把数据送进内网项目自己的 MQTT 服务

## 9. MQTT 主题设计

当前与本工程直接相关的主题：

- 发布主题: `garden/flower/dht11`

这个主题对应：

- 庭院 1 号设备
- 花花环境页

当前工程没有额外订阅主题，主要是单向上报数据。

## 10. MQTT 消息格式

当前代码实际发送的 JSON 格式如下：

```json
{
  "device": "esp32-c3-supermini",
  "source": "esp32-c3-public",
  "temperature": 25.0,
  "humidity": 60.0,
  "rssi": -48,
  "ip": "192.168.1.135",
  "ts": 1
}
```

字段说明：

- `device`: 设备名，固定为 `esp32-c3-supermini`
- `source`: 来源标识，固定为 `esp32-c3-public`
- `temperature`: 温度，单位摄氏度
- `humidity`: 湿度，单位 `%RH`
- `rssi`: 当前 WiFi 信号强度
- `ip`: 板子当前 STA 模式拿到的局域网 IP
- `ts`: 当前发布计数，从 `1` 开始递增

代码中的实际拼接格式为：

```c
{"device":"esp32-c3-supermini","source":"esp32-c3-public","temperature":%.1f,"humidity":%.1f,"rssi":%d,"ip":"%s","ts":%lu}
```

## 11. 硬件接线

本次确认可用的接线如下：

- `DHT11 VCC -> 3V3`
- `DHT11 GND -> GND`
- `DHT11 DATA -> GPIO4`

补充说明：

- 本板当前不接 `OLED`
- `OLED` 相关逻辑已经从本工程主流程中移除

## 12. 串口调试信息

当前串口能看到的关键日志包括：

- `DHT11 data pin: GPIO4`
- `DHT11 sample: temperature=... C humidity=... %RH`
- `publish ok: {...}`
- `waiting dht11/wifi/mqtt...`
- `DHT11 read failed: ...`

用途：

- 对照传感器本地读数
- 对照服务器页面显示值
- 判断是 WiFi、MQTT 还是传感器故障

## 13. 当前固件配置

关键配置来自 `main/include/app_config.h`：

- `APP_WIFI_SSID = "Ermao"`
- `APP_WIFI_PASSWORD = "gf666666"`
- `APP_MQTT_URI = "mqtt://117.72.55.63:11883"`
- `APP_MQTT_USERNAME = ""`
- `APP_MQTT_PASSWORD = ""`
- `APP_MQTT_TOPIC_TELEMETRY = "garden/flower/dht11"`
- `APP_PUBLISH_INTERVAL_MS = 3000`
- `APP_DHT11_GPIO = GPIO_NUM_4`

## 14. 编译与烧录

```powershell
$env:IDF_PYTHON_ENV_PATH="D:\02-software-stash-cache\02-esp32-idf\python_env\idf5.1_py3.11_env"
$env:IDF_PATH="D:\02-software-stash-cache\02-esp32-idf\frameworks\esp-idf-v5.1.2"
. $env:IDF_PATH\export.ps1
cd F:\01-dev-board\06-esp32s3\YD-ESP32-S3\03-esp32-c3\03-project\02-wifi-mqtt-forward-demo
idf.py set-target esp32c3
idf.py build
idf.py -p COM7 flash
idf.py -p COM7 monitor
```

## 15. 本次 session 关键结论

- `ESP32-C3 SuperMini` 工程已完成 `DHT11 + WiFi + MQTT` 打通
- 传感器问题最终确认为接线错误，不是代码问题
- WiFi 正常可连
- MQTT 发布链路已按“公网转发到内网服务”方式配置
- 当前温湿度已经可以正常读取
- 后续如果需要恢复 `OLED`，建议新开分支或单独加回模块，不要影响当前稳定的 `DHT11` 版本
