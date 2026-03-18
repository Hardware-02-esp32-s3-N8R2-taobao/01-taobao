# 06-server-net-display

## 项目简介

这是一个庭院智能家居的最小可跑 Demo，目前已经实现：

- ESP32 可以向局域网网关上报温湿度
- 网关提供网页和 API
- 局域网内可以直接访问网页
- 通过 Cloudflare Quick Tunnel 可以让公网手机访问网页
- 前端页面已经做成“龟龟老板的小花园”主题
- 传感器页支持历史曲线和 SQLite 数据库存储
- 现在内置了局域网 MQTT Broker，可接 DHT11 / DS18B20 / BMP280 / BH1750

## 当前目录

- [server.js](/e:/10-netdisplay/server.js)：网关 HTTP 服务
- [public/index.html](/e:/10-netdisplay/public/index.html)：前端页面
- [public/app.js](/e:/10-netdisplay/public/app.js)：前端交互和图表逻辑
- [esp32-demo/esp32_http_demo.ino](/e:/10-netdisplay/esp32-demo/esp32_http_demo.ino)：ESP32 最小示例
- [start-demo.ps1](/e:/10-netdisplay/start-demo.ps1)：Windows PowerShell 启动脚本
- [start-demo.bat](/e:/10-netdisplay/start-demo.bat)：Windows 批处理启动脚本
- [start-demo.sh](/e:/10-netdisplay/start-demo.sh)：Linux 启动脚本
- [start-public-tunnel.js](/e:/10-netdisplay/start-public-tunnel.js)：启动公网隧道
- [stop-public-tunnel.js](/e:/10-netdisplay/stop-public-tunnel.js)：关闭公网隧道
- [docs/mqtt-topics.md](/e:/10-netdisplay/docs/mqtt-topics.md)：所有传感器的 MQTT 主题与格式说明

这是一个最小可跑的网关 Demo：

- 网关服务启动后提供网页界面
- 页面会轮询读取最新温湿度数据
- 当前默认使用服务端模拟数据
- ESP32 后续可以直接向网关接口上报真实数据
- 一旦收到真实设备上报，模拟数据会自动停止覆盖

## 运行

```powershell
.\start-demo.ps1
```

或者：

```bat
start-demo.bat
```

如果你直接在 PowerShell 里启动，也可以：

```powershell
& "C:\Program Files\nodejs\node.exe" .\server.js
```

启动后访问：

```text
http://localhost:3000
```

MQTT Broker 同时启动在：

```text
mqtt://局域网IP:1883
```

## 基础操作记录

### 1. 启动本地网关

PowerShell：

```powershell
cd E:\10-netdisplay
.\start-demo.ps1
```

或者直接：

```powershell
cd E:\10-netdisplay
& "C:\Program Files\nodejs\node.exe" .\server.js
```

浏览器访问：

```text
http://localhost:3000
```

### 2. 查看接口是否正常

读取最新传感器数据：

```powershell
Invoke-RestMethod -Uri "http://127.0.0.1:3000/api/sensor/latest"
```

查看历史曲线数据：

```powershell
Invoke-RestMethod -Uri "http://127.0.0.1:3000/api/sensor/history?range=24h"
```

查看花花、鱼鱼、气压站的历史：

```powershell
Invoke-RestMethod -Uri "http://127.0.0.1:3000/api/sensor/history?series=flower&range=24h"
Invoke-RestMethod -Uri "http://127.0.0.1:3000/api/sensor/history?series=fish&range=3d"
Invoke-RestMethod -Uri "http://127.0.0.1:3000/api/sensor/history?series=climate&date=2026-03-15"
Invoke-RestMethod -Uri "http://127.0.0.1:3000/api/sensor/history?series=light&range=24h"
```

查看 MQTT 状态：

```powershell
Invoke-RestMethod -Uri "http://127.0.0.1:3000/api/mqtt/status"
```

查看指定日期：

```powershell
Invoke-RestMethod -Uri "http://127.0.0.1:3000/api/sensor/history?date=2026-03-15"
```

手动模拟 ESP32 上报：

```powershell
Invoke-RestMethod -Method Post `
  -Uri "http://127.0.0.1:3000/api/sensor/update" `
  -ContentType "application/json" `
  -Body '{"temperature":28.6,"humidity":58.2,"source":"esp32-yard-01"}'
```

### 3. 启动公网访问

先启动本地服务，再开一个终端执行：

```powershell
cd E:\10-netdisplay
& "C:\Program Files\nodejs\node.exe" .\start-public-tunnel.js
```

关闭隧道：

```powershell
cd E:\10-netdisplay
& "C:\Program Files\nodejs\node.exe" .\stop-public-tunnel.js
```

### 4. 手机如何访问

- 保持 `server.js` 在运行
- 保持 `cloudflared` 隧道在运行
- 打开 `cloudflared.err.log`，找到 `https://xxxx.trycloudflare.com`
- 手机浏览器直接访问这个地址

当前这次调试拿到的临时地址是：

```text
https://nation-centers-champagne-columns.trycloudflare.com
```

注意：这是临时地址，重新启动隧道后大概率会变化。

### 5. ESP32 如何接入

- 修改 [esp32-demo/esp32_http_demo.ino](/e:/10-netdisplay/esp32-demo/esp32_http_demo.ino) 里的 WiFi 名称和密码
- 把 `GATEWAY_URL` 改成你的网关机器在局域网里的地址
- 烧录后，ESP32 会每 5 秒上报一次温湿度

示例：

```cpp
const char* GATEWAY_URL = "http://192.168.1.20:3000/api/sensor/update";
```

### 5.1 现在推荐的 MQTT 接入方式

- MQTT Broker 地址：`mqtt://树莓派局域网IP:1883`
- 推荐主题和 JSON 格式说明见：
  [docs/mqtt-topics.md](/e:/10-netdisplay/docs/mqtt-topics.md)

当前约定的主题：

- `garden/flower/dht11`
- `garden/fish/ds18b20`
- `garden/climate/bmp280`
- `garden/light/bh1750`

如果你要把文档直接丢给 ESP32 工程里的 Codex，现在只需要这一份：

- [docs/mqtt-topics.md](/e:/10-netdisplay/docs/mqtt-topics.md)

### 6. 树莓派部署

当前已经部署到树莓派：

```text
/home/zerozero/01-code/05-new-net-display
```

连接方式：

```bash
ssh raspberry-bcm2835
```

在树莓派上启动本地服务：

```bash
cd /home/zerozero/01-code/05-new-net-display
./start-demo.sh
```

如果提示 `EADDRINUSE`，说明服务已经在运行，不需要重复启动。可以先查看状态：

```bash
cd /home/zerozero/01-code/05-new-net-display
./status-demo.sh
```

如果你确实想重启服务：

```bash
cd /home/zerozero/01-code/05-new-net-display
./stop-demo.sh
./start-demo.sh
```

在树莓派上启动公网隧道：

```bash
cd /home/zerozero/01-code/05-new-net-display
/home/zerozero/.local/bin/node start-public-tunnel.js
```

如果你只是想看当前公网地址：

```bash
cd /home/zerozero/01-code/05-new-net-display
tail -n 20 cloudflared.err.log
```

当前这次树莓派部署拿到的公网地址是：

```text
https://porcelain-fact-tropical-recognised.trycloudflare.com
```

### 7. 固定局域网 IP 与开机自启

目前建议这样理解：

- `固定局域网 IP`：可以直接在树莓派上配置，例如固定 `wlan0 = 192.168.1.226`
- `固定公网地址`：Quick Tunnel 做不到固定，后续需要 Cloudflare 命名 Tunnel + 你自己的域名

本仓库已经准备好了 systemd 服务文件：

- [`systemd/netdisplay.service`](/e:/10-netdisplay/systemd/netdisplay.service)
- [`systemd/netdisplay-tunnel.service`](/e:/10-netdisplay/systemd/netdisplay-tunnel.service)

启用后，树莓派开机会自动：

- 启动网页服务
- 启动 Cloudflare 隧道

当前公网地址会自动写入：

```text
/home/zerozero/01-code/05-new-net-display/public-url.txt
```

你可以直接一条命令拿到最新地址：

```bash
ssh raspberry-bcm2835 "cat /home/zerozero/01-code/05-new-net-display/public-url.txt"
```

在 Windows 项目目录里也可以直接运行：

```powershell
.\get-garden-url.ps1
```

或者：

```bat
get-garden-url.bat
```

## 相关知识

### 远程修改树莓派代码怎么做

这次我是这样做的：

- 在本地 Windows 仓库里修改文件
- 通过 `scp` 把改好的文件同步到树莓派目录
- 通过 `ssh raspberry-bcm2835` 在树莓派上重启服务并验证接口

你自己以后也可以这样做，常见有三种方式：

1. 本地改代码 + `scp` 上传  
   这是最轻量的方式，树莓派压力最小，适合现在这个项目。

2. 本地 Git 提交后，在树莓派 `git pull`  
   适合你后面改动越来越多的时候，版本管理会更清晰。

3. 用 VS Code Remote SSH 直接连树莓派改  
   这个不是特别重，代码量不大时树莓派 3 也能用，只是会比纯 `scp` 稍慢一点。

如果你担心树莓派带不动，建议优先用：

- Windows 本地写代码
- `scp` 上传
- `ssh` 重启服务

常用命令示例：

```powershell
scp server.js public\index.html raspberry-bcm2835:/home/zerozero/01-code/05-new-net-display/
ssh raspberry-bcm2835
```

树莓派里重启：

```bash
cd /home/zerozero/01-code/05-new-net-display
./stop-demo.sh
nohup ./start-demo.sh > server.out.log 2> server.err.log < /dev/null &
```

### 整体链路

这个项目当前走的是：

`ESP32 -> 局域网网关(Node.js) -> 网页前端 -> Cloudflare 临时公网隧道 -> 手机浏览器`

这条链路的优点：

- 上手快，适合先跑通 demo
- ESP32 端只需要会发 HTTP POST
- 网关前后端都在一台机器上，调试简单
- 不需要立刻买云服务器

### 为什么先用 HTTP，而不是 MQTT

当前先用 HTTP POST，是因为：

- ESP32 好写
- 后端简单
- 接口能直接拿浏览器和 Postman 调试

后面如果设备数量多、需要订阅消息、离线重连、QoS，再升级到 MQTT 更合适。

### 网关在做什么

网关的职责有三件：

- 接收 ESP32 上报的温湿度
- 保存“当前最新值”
- 把最新值通过网页展示给用户
- 读取树莓派服务器状态
- 拉取杭州临安区天气预报
- 把温湿度持久化到 SQLite 历史库

### 传感器历史数据库

现在传感器数据会保存在：

```text
data/sensor-history.db
```

目前页面支持：

- 最近 24 小时曲线
- 最近 3 天曲线
- 最近 7 天曲线
- 指定某一天查看历史

后端会自动做时间桶聚合，这样 3 天、7 天查看时不会因为点太多把浏览器拖慢。

正式版通常还会继续增加：

- 数据库存历史记录
- 告警逻辑
- 用户登录
- 多设备管理

### 公网访问为什么能工作

Cloudflare Tunnel 的原理可以简单理解为：

- 你的电脑主动连到 Cloudflare
- Cloudflare 给你一个公网域名
- 手机访问这个域名时，请求再通过隧道转发回你的本地服务

这样就不需要自己折腾路由器端口映射。

### 为什么临时域名会变

你现在用的是 Quick Tunnel，也就是临时隧道：

- 适合 demo 和测试
- 域名不是固定的
- 服务稳定性没有正式命名 Tunnel 高

以后如果要长期使用，建议升级成：

- 固定域名
- 正式 Cloudflare Tunnel
- 或者 Nginx + HTTPS + 自己域名

## 公网访问

先启动网关服务，再启动公网隧道：

```powershell
& "C:\Program Files\nodejs\node.exe" .\server.js
```

另开一个终端执行：

```powershell
& "C:\Program Files\nodejs\node.exe" .\start-public-tunnel.js
```

启动后，Cloudflare 会把日志写入：

- `cloudflared.log`
- `cloudflared.err.log`

公网地址会出现在 `cloudflared.log` 里，域名形如：

```text
https://xxxx.trycloudflare.com
```

也可以直接查看 `cloudflared.err.log`，当前版本的 cloudflared 会把连接建立信息和公网地址打到这里。

## 接口

### 读取最新数据

```http
GET /api/sensor/latest
```

返回示例：

```json
{
  "temperature": 26.3,
  "humidity": 61.5,
  "source": "demo-simulator",
  "updatedAt": "2026-03-15T08:00:00.000Z"
}
```

### ESP32 上报数据

```http
POST /api/sensor/update
Content-Type: application/json
```

请求体示例：

```json
{
  "temperature": 28.6,
  "humidity": 58.2,
  "source": "esp32-yard-01"
}
```

## ESP32 示例

仓库里已经放了一个最小上报示例：

[`esp32-demo/esp32_http_demo.ino`](./esp32-demo/esp32_http_demo.ino)

你只需要修改：

- WiFi 名称和密码
- `GATEWAY_URL` 为你的网关局域网地址

然后烧录到 ESP32，就会每 5 秒往网关发送一次温湿度数据。

## 推荐整体方案

第一阶段先这样做：

- ESP32 使用 HTTP POST 把数据发到局域网网关
- 网关用 Node.js 提供 API 和网页
- 公网访问先通过反向代理或内网穿透接到网关

后续正式版建议：

- ESP32 -> MQTT Broker 或 HTTP API
- 网关 -> 数据库存储历史数据
- 前端 -> 实时图表、设备状态、告警
- 公网入口 -> Nginx + HTTPS，或 Cloudflare Tunnel / Tailscale Funnel
