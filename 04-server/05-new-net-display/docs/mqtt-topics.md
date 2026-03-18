# MQTT 主题与消息格式说明

这份文档汇总了当前所有传感器的 MQTT 主题与 JSON 消息格式，后面你可以直接把这一份丢给 ESP32 工程里的 Codex。

## 当前推荐接入方式

由于 ESP32 和服务器不再处于同一局域网，当前推荐使用：

- 云 MQTT Broker：`EMQX Cloud Serverless`
- 连接协议：`MQTTS`
- 端口：`8883`
- 鉴权方式：`用户名 + 密码`
- 数据流向：
  - ESP32 发布数据到云 Broker
  - 服务器作为另一个 MQTT Client 订阅同样的 Topic
  - 网关网页继续使用服务器本地的展示逻辑

推荐原因：

- 不需要把本地 `1883` 直接暴露到公网
- 自带 TLS，适合公网设备上传
- 设备和服务器不在同一局域网时更稳定
- ESP32 和服务器都只是 MQTT Client，接法更标准

## Broker 和“代理”是不是一个意思

不是一个意思。

- `Broker`
  - 是 MQTT 消息服务器本身
  - 负责接收客户端发布的消息，并把消息转发给订阅者
- `Proxy`
  - 一般是网络代理/转发层
  - 负责帮你转发网络连接，但它本身通常不是 MQTT 消息中心

在你这个场景里，`EMQX Cloud` 是 `Broker`，不是普通网络代理。

## 云 Broker 连接参数

你后面创建好云 Broker 后，需要把下面这几个参数填到 ESP32 工程和服务器订阅端里：

- `MQTT_HOST`
  - 示例：`xxxx.ala.cn-hangzhou.emqxsl.cn`
- `MQTT_PORT`
  - 固定：`8883`
- `MQTT_USERNAME`
  - 例如：`esp32-yard`
- `MQTT_PASSWORD`
  - 例如：你在云平台里设置的密码
- `CLIENT_ID`
  - 每个连接必须唯一
  - 例如：
    - ESP32：`yard-node-01`
    - 服务器：`netdisplay-server-01`
- `SNI / server_hostname`
  - 如果你用的是 EMQX Cloud Serverless，TLS 连接时建议把它设置成 MQTT Host

建议：

- ESP32 和服务器都需要登录云 Broker
- 两边都可以使用用户名密码鉴权
- 最简单的方式是：
  - ESP32 用一组账号
  - 服务器订阅端用另一组账号
- 如果你暂时不做 ACL，理论上也可以先共用同一组账号，但不建议长期这么做

## 云 Broker 认证说明

是的，云 MQTT Broker 通常就是通过账户密码来控制接入。

你的理解可以直接记成这样：

1. ESP32 用 `host + 8883 + username + password + clientId` 登录云 Broker
2. ESP32 往 Topic 上传数据
3. 服务器也用 `host + 8883 + username + password + clientId` 登录云 Broker
4. 服务器订阅这些 Topic
5. 服务器收到后更新网页展示

注意：

- `clientId` 必须唯一，不能让 ESP32 和服务器共用同一个 `clientId`
- `username/password` 可以相同，也可以分开
- 如果后面你要做更细权限，推荐给 ESP32 发布端和服务器订阅端分开账号

## 本地局域网 Broker

项目仍然保留本地 Broker 逻辑，但当前它更适合：

- 局域网调试
- 同一网络内的开发验证
- 兼容老的本地直连方式

公网/跨网络设备上传时，优先按上面的云 Broker 方式接入。

## Broker

- 协议：`MQTT`
- 本地地址：`mqtt://<树莓派局域网IP>:1883`
- 本地默认端口：`1883`
- 本地鉴权：当前版本未开启用户名密码，只限局域网使用
- 云端推荐地址：`mqtts://<你的云 Broker Host>:8883`
- 云端推荐鉴权：`用户名 + 密码`
- QoS：建议先用 `0`
- Retain：建议 `false`

## 云 Broker 创建建议

当前推荐创建一套最小可用配置：

- 服务商：`EMQX Cloud Serverless`
- Region：选离设备和服务器都较近的区域
- Port：`8883`
- TLS：开启
- Authentication：添加用户名密码

建议至少创建两组客户端身份：

- `esp32-publisher`
  - 用途：ESP32 上传数据
- `server-subscriber`
  - 用途：服务器订阅数据

如果云平台支持 ACL，建议后续这样分：

- `esp32-publisher`
  - 允许发布：
    - `garden/flower/dht11`
    - `garden/fish/ds18b20`
    - `garden/climate/bmp280`
    - `garden/light/bh1750`
    - `garden/gateway/ping`
- `server-subscriber`
  - 允许订阅：
    - `garden/#`

## 网关心跳 / OLED 服务器状态

如果 ESP32 想在 OLED 上显示“树莓派服务是否正常、公网是否可访问”，建议用这套下行回包协议。

### 机制说明

1. ESP32 连上 MQTT 后，先订阅两个主题：
   - `garden/gateway/status/<device>`
   - `garden/gateway/broadcast`
2. ESP32 主动向 `garden/gateway/ping` 发一个 ping 包
3. 树莓派收到后，会向 `garden/gateway/status/<device>` 回一个心跳包
4. 树莓派还会每 30 秒向 `garden/gateway/broadcast` 发一个广播心跳
5. ESP32 收到心跳后，就可以在 OLED 上显示：
   - MQTT 正常
   - 服务器在线
   - 公网是否可访问
   - 当前公网地址
   - 树莓派温度

### ESP32 需要订阅的主题

- 定向回包：`garden/gateway/status/<device>`
- 广播心跳：`garden/gateway/broadcast`

例如你的设备名是 `guigui-1`，就订阅：

```text
garden/gateway/status/guigui-1
garden/gateway/broadcast
```

### ESP32 主动 ping 的主题

- Topic：`garden/gateway/ping`

消息示例：

```json
{
  "device": "guigui-1",
  "alias": "龟龟1号",
  "ts": "2026-03-15T20:10:30+08:00"
}
```

字段说明：

- `device`
  - 类型：`string`
  - 是否必填：建议必填
  - 说明：设备名，用来决定树莓派回复到哪个定向主题
- `alias`
  - 类型：`string`
  - 是否必填：否
  - 说明：设备显示名，用于网页设备状态页展示
- `ts`
  - 类型：`string | number`
  - 是否必填：否
  - 说明：设备发起 ping 的时间

### 树莓派回复的心跳包格式

树莓派会发到：

```text
garden/gateway/status/<device>
```

或者周期性发到：

```text
garden/gateway/broadcast
```

消息示例：

```json
{
  "type": "gateway-heartbeat",
  "reason": "reply",
  "targetDevice": "guigui-1",
  "timestamp": "2026-03-15T20:10:31+08:00",
  "serverOnline": true,
  "mqttOnline": true,
  "httpOnline": true,
  "publicUrlAvailable": true,
  "publicUrl": "https://xxxx.trycloudflare.com",
  "httpPort": 3000,
  "mqttPort": 1883,
  "hostname": "raspberrypi",
  "uptimeSeconds": 18520,
  "cpuTemperatureC": 46.8
}
```

字段说明：

- `type`
  - 固定值：`gateway-heartbeat`
- `reason`
  - 可能值：
    - `startup`
    - `broadcast`
    - `reply`
- `targetDevice`
  - 定向回包时是设备名
  - 广播包时可能为 `null`
- `timestamp`
  - 树莓派生成心跳包的时间
- `serverOnline`
  - `true` 表示网关服务正常
- `mqttOnline`
  - `true` 表示 MQTT Broker 正常
- `httpOnline`
  - `true` 表示网页/API 服务正常
- `publicUrlAvailable`
  - `true` 表示当前已拿到公网地址
- `publicUrl`
  - 当前公网地址，没有时可能为 `null`
- `httpPort`
  - HTTP 服务端口，默认 `3000`
- `mqttPort`
  - MQTT 服务端口，默认 `1883`
- `hostname`
  - 树莓派主机名
- `uptimeSeconds`
  - 树莓派运行时长，单位秒
- `cpuTemperatureC`
  - 树莓派 CPU 温度

### ESP32 OLED 推荐显示逻辑

建议分成 4 行左右：

```text
MQTT: OK
Server: OK
Public: OK
URL: trycloudflare
```

也可以显示得更完整一点：

```text
MQTT OK / HTTP OK
Public: ON
CPU: 46.8C
Host: raspberrypi
```

### ESP32 判定规则建议

- 如果 60 秒内没收到 `status/<device>` 或 `broadcast`
  - OLED 显示：`Server Lost`
- 如果收到了心跳，但 `publicUrlAvailable = false`
  - OLED 显示：`Public Off`
- 如果 `serverOnline = true` 且 `mqttOnline = true`
  - OLED 显示：`Gateway OK`

### ESP32 侧流程建议

1. 连上 Wi-Fi
2. 连上 MQTT
3. 订阅：
   - `garden/gateway/status/<device>`
   - `garden/gateway/broadcast`
4. 发布一次：
   - `garden/gateway/ping`
5. 定时每 20 到 30 秒再发一次 ping
6. OLED 根据最近收到的心跳更新时间和字段状态做显示

## 通用 JSON 规则

- 消息体统一使用 `UTF-8 JSON`
- 数值字段统一传数字，不要传字符串
- 时间字段可选，优先用 `ts`
- `ts` 支持 ISO8601、Unix 秒时间戳、Unix 毫秒时间戳
- `device` 建议填写设备唯一标识，网页和服务端会记录来源
- `alias` 建议填写设备显示名，网页设备状态页会优先显示它

## 设备身份命名建议

为了让设备状态页能区分每块 ESP32，建议每台设备都带这两个字段：

- `device`
  - 用途：唯一标识
  - 示例：`guigui-1`、`guigui-2`、`yard-node-1`
- `alias`
  - 用途：用户可读名称
  - 示例：`龟龟1号`、`龟龟2号`、`东侧花架节点`

推荐最小身份写法：

```json
{
  "device": "guigui-1",
  "alias": "龟龟1号"
}
```

在线判定规则：

- 90 秒内收到该设备的心跳或传感器消息：`在线`
- 超过 90 秒未收到：`离线`

## ESP32 公网接入最小配置模板

下面是一份给 ESP32 工程用的最小参数模板，你后面可以直接替换成真实值：

```cpp
const char* MQTT_HOST = "your-emqx-host.ala.cn-hangzhou.emqxsl.cn";
const int MQTT_PORT = 8883;
const char* MQTT_USERNAME = "esp32-publisher";
const char* MQTT_PASSWORD = "replace-with-real-password";
const char* MQTT_CLIENT_ID = "yard-node-01";
```

如果是 TLS 连接，ESP32 侧还需要：

- 云 Broker 的 CA 证书，或者使用芯片 SDK 支持的证书校验方式
- `server_hostname` / SNI 设置成你的 `MQTT_HOST`

## 服务器订阅端最小配置模板

服务器作为订阅端时，同样需要一组连接参数：

```text
MQTT_HOST=your-emqx-host.ala.cn-hangzhou.emqxsl.cn
MQTT_PORT=8883
MQTT_USERNAME=server-subscriber
MQTT_PASSWORD=replace-with-real-password
MQTT_CLIENT_ID=netdisplay-server-01
```

## Topic 一览

- `garden/flower/dht11`
- `garden/fish/ds18b20`
- `garden/climate/bmp280`
- `garden/light/bh1750`
- `garden/yard/pump/set`
- `garden/gateway/ping`
- `garden/gateway/status/<device>`
- `garden/gateway/broadcast`

## 1. DHT11 花花环境

- Topic：`garden/flower/dht11`
- 用途：空气温度、空气湿度
- 页面显示位置：
  - `传感器页 -> 花花温度`
  - `传感器页 -> 花花湿度`

消息示例：

```json
{
  "device": "guigui-1",
  "alias": "龟龟1号",
  "temperature": 25.6,
  "humidity": 63.0,
  "ts": "2026-03-15T17:20:30+08:00"
}
```

字段说明：

- `device`
  - 类型：`string`
  - 是否必填：否
  - 说明：设备名，建议写 ESP32 名称
- `alias`
  - 类型：`string`
  - 是否必填：否
  - 说明：设备显示名，例如 `龟龟1号`
- `temperature`
  - 类型：`number`
  - 是否必填：是
  - 单位：`°C`
  - 说明：花花环境空气温度
- `humidity`
  - 类型：`number`
  - 是否必填：是
  - 单位：`%RH`
  - 说明：花花环境空气湿度
- `ts`
  - 类型：`string | number`
  - 是否必填：否
  - 说明：时间戳，可传 ISO8601、秒级时间戳、毫秒级时间戳

服务端处理：

- 更新花花温度
- 更新花花湿度
- 写入历史数据库
- 同时兼容旧版温湿度接口展示

最小示例：

```cpp
publish("garden/flower/dht11", "{\"device\":\"esp32-yard-01\",\"temperature\":25.6,\"humidity\":63.0}");
```

## 2. DS18B20 鱼鱼温度

- Topic：`garden/fish/ds18b20`
- 用途：鱼缸水温
- 页面显示位置：`传感器页 -> 鱼鱼温度`

消息示例：

```json
{
  "device": "guigui-2",
  "alias": "龟龟2号",
  "temperature": 23.8,
  "ts": "2026-03-15T17:20:30+08:00"
}
```

字段说明：

- `device`
  - 类型：`string`
  - 是否必填：否
  - 说明：设备名，建议写 ESP32 名称
- `alias`
  - 类型：`string`
  - 是否必填：否
  - 说明：设备显示名，例如 `龟龟2号`
- `temperature`
  - 类型：`number`
  - 是否必填：是
  - 单位：`°C`
  - 说明：鱼缸水温
- `ts`
  - 类型：`string | number`
  - 是否必填：否
  - 说明：时间戳，可传 ISO8601、秒级时间戳、毫秒级时间戳

服务端处理：

- 更新鱼鱼温度
- 写入历史数据库
- 历史曲线可切到 `鱼鱼温度`

最小示例：

```cpp
publish("garden/fish/ds18b20", "{\"device\":\"esp32-fish-01\",\"temperature\":23.8}");
```

## 3. BMP280 气压站

- Topic：`garden/climate/bmp280`
- 用途：BMP280 温度与气压
- 页面显示位置：
  - `传感器页 -> BMP280 温度`
  - `传感器页 -> 气压值`
  - `传感器页 -> 气压状态`

消息示例：

```json
{
  "device": "guigui-3",
  "alias": "龟龟3号",
  "temperature": 24.1,
  "pressure": 1007.8,
  "ts": "2026-03-15T17:20:30+08:00"
}
```

字段说明：

- `device`
  - 类型：`string`
  - 是否必填：否
  - 说明：设备名，建议写 ESP32 名称
- `alias`
  - 类型：`string`
  - 是否必填：否
  - 说明：设备显示名，例如 `龟龟3号`
- `temperature`
  - 类型：`number`
  - 是否必填：是
  - 单位：`°C`
  - 说明：BMP280 温度
- `pressure`
  - 类型：`number`
  - 是否必填：是
  - 单位：`hPa`
  - 说明：当前气压
- `ts`
  - 类型：`string | number`
  - 是否必填：否
  - 说明：时间戳，可传 ISO8601、秒级时间戳、毫秒级时间戳

服务端处理：

- 更新 BMP280 温度
- 更新气压值
- 更新气压状态
- 写入历史数据库
- 历史曲线可切到 `气压站`

气压状态判定：

- `< 1000 hPa`：`偏低`
- `1000 ~ 1025 hPa`：`正常`
- `> 1025 hPa`：`偏高`

最小示例：

```cpp
publish("garden/climate/bmp280", "{\"device\":\"esp32-bmp280-01\",\"temperature\":24.1,\"pressure\":1007.8}");
```

## 4. BH1750 光照

- Topic：`garden/light/bh1750`
- 用途：环境光照强度
- 页面显示位置：`光照页 -> 光照强度`

消息示例：

```json
{
  "device": "guigui-4",
  "alias": "龟龟4号",
  "illuminance": 12850,
  "ts": "2026-03-15T17:20:30+08:00"
}
```

字段说明：

- `device`
  - 类型：`string`
  - 是否必填：否
  - 说明：设备名，建议写 ESP32 名称
- `alias`
  - 类型：`string`
  - 是否必填：否
  - 说明：设备显示名，例如 `龟龟4号`
- `illuminance`
  - 类型：`number`
  - 是否必填：是
  - 单位：`lux`
  - 说明：当前光照强度
- `ts`
  - 类型：`string | number`
  - 是否必填：否
  - 说明：时间戳，可传 ISO8601、秒级时间戳、毫秒级时间戳

服务端处理：

- 更新光照页中的光照强度
- 写入历史数据库
- 历史曲线可切到 `光照站`

最小示例：

```cpp
publish("garden/light/bh1750", "{\"device\":\"esp32-light-01\",\"illuminance\":12850}");
```

## 5. 水泵控制

- Topic：`garden/yard/pump/set`
- 用途：控制庭院 1 号设备上的 ESP32 某个 IO 口，驱动水泵继电器
- 页面显示位置：`庭院 1 号设备 -> 水泵控制`

建议 ESP32 侧订阅：

```text
garden/yard/pump/set
```

消息示例：

```json
{
  "type": "pump-command",
  "device": "yardHub",
  "action": "pulse",
  "durationSeconds": 5,
  "topic": "garden/yard/pump/set",
  "timestamp": "2026-03-16T16:45:00+08:00",
  "source": "web-console"
}
```

字段说明：

- `type`
  - 固定值：`pump-command`
- `device`
  - 类型：`string`
  - 说明：目标设备标识，当前固定为 `yardHub`
- `action`
  - 固定值：`pulse`
  - 说明：表示拉高一次 IO，持续指定秒数后自动关闭
- `durationSeconds`
  - 类型：`number`
  - 是否必填：是
  - 单位：`秒`
  - 说明：浇灌时长，建议 ESP32 侧自行再做最大时长保护
- `topic`
  - 类型：`string`
  - 说明：当前控制主题，便于调试打印
- `timestamp`
  - 类型：`string`
  - 说明：网关发出指令的时间
- `source`
  - 类型：`string`
  - 说明：当前固定为 `web-console`

网页控制流程：

1. 在网页里填写浇灌时间，单位秒
2. 输入口令，默认 `1234`
3. 网关校验口令通过后，发布 MQTT 消息到 `garden/yard/pump/set`
4. ESP32 收到后控制对应 IO 口拉高
5. 持续 `durationSeconds` 秒后自动拉低，停止水泵

ESP32 侧伪代码建议：

```cpp
if (topic == "garden/yard/pump/set") {
  DynamicJsonDocument doc(256);
  deserializeJson(doc, payload);
  if (doc["action"] == "pulse") {
    int seconds = doc["durationSeconds"] | 0;
    if (seconds > 0 && seconds <= 600) {
      digitalWrite(PUMP_PIN, HIGH);
      delay(seconds * 1000);
      digitalWrite(PUMP_PIN, LOW);
    }
  }
}
```

安全建议：

- 网页口令只在 HTTP 控制入口校验，不下发到 MQTT
- ESP32 最好自己再限制最大浇灌时长，例如不超过 `600` 秒
- 如果水泵是继电器控制，建议继电器默认上电为关闭态
- 最好增加本地硬件防呆，例如超时断电

## 服务端接口

```text
GET /api/sensor/latest
GET /api/sensor/history?series=flower&range=24h
GET /api/sensor/history?series=fish&range=3d
GET /api/sensor/history?series=climate&date=2026-03-15
GET /api/sensor/history?series=light&range=24h
GET /api/mqtt/status
GET /api/server/history
GET /api/devices/status
```
