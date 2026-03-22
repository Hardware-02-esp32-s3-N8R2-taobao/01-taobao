# 龟龟老板的小花园 — 公网网关服务

运行在云服务器（`117.72.55.63`）上的 IoT 网关，通过 Nginx 反向代理直接对外提供 HTTP 服务。

**公网地址：`http://117.72.55.63/`**

---

## 目录结构

```
01-net-display-server/
├── server.js                  # 主服务器（HTTP + 内置 MQTT Broker + SQLite）
├── public/
│   ├── index.html             # 前端仪表盘
│   └── app.js                 # 前端 JS（图表、轮询、设备管理）
├── docs/
│   └── mqtt-topics.md         # MQTT 主题与数据格式文档
├── esp32-demo/
│   └── esp32_http_demo.ino    # ESP32 HTTP 接入示例
├── start-demo.sh              # Linux 本地开发：启动服务
├── start-demo.bat             # Windows 本地开发：启动服务
├── start-demo.ps1             # Windows PowerShell 本地开发：启动服务
├── status-demo.sh             # Linux：检查服务状态
├── stop-demo.sh               # Linux：停止服务
└── systemd/
    ├── netdisplay.service         # 云服务器 systemd 服务（已部署）
    └── nginx-yard-display.conf    # Nginx 反向代理配置（已部署）
```

---

## 云服务器部署信息

| 项目 | 值 |
|------|-----|
| 服务器 IP | `117.72.55.63` |
| 操作系统 | Ubuntu 24.04.2 LTS |
| Node.js | v22.22.1 |
| 部署目录 | `/opt/yard-display` |
| 服务管理 | systemd（`yard-display.service`） |
| 反向代理 | Nginx → `:3000` |

---

## 端口说明

| 端口 | 协议 | 对外 | 用途 |
|------|------|------|------|
| 80 | HTTP | 是（Nginx） | 网页仪表盘 + REST API |
| 3000 | HTTP | 否（仅本机） | Node.js 服务（由 Nginx 转发） |
| 1884 | MQTT (TCP) | 是 | ESP32 传感器接入 |

> MQTT 端口 1884 供局域网内的 ESP32 设备连接，实际使用中 ESP32 与服务器通过公网 IP 直连。

---

## 数据链路

```
ESP32 传感器
  └─ MQTT → 117.72.55.63:1884（内置 Aedes Broker）

用户浏览器
  └─ HTTP → 117.72.55.63:80（Nginx）
              └─ → 127.0.0.1:3000（Node.js）

天气数据
  └─ Open-Meteo API（Node.js 每 30 分钟拉取一次，Nginx 额外缓存 25 分钟）

数据存储
  └─ SQLite：/opt/yard-display/data/sensor-history.db（90 天保留）
```

---

## API 端点

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/` | 前端仪表盘 |
| GET | `/api/sensor/latest` | 所有传感器最新值 |
| GET | `/api/sensor/history` | 历史曲线（`?series=flower&range=24h`） |
| GET | `/api/devices/status` | 设备在线状态 |
| GET | `/api/server/status` | 服务器 CPU / 内存实时状态 |
| GET | `/api/server/history` | 服务器遥测历史 |
| GET | `/api/mqtt/status` | MQTT Broker 状态 |
| GET | `/api/weather/forecast` | 杭州天气预报 |
| POST | `/api/sensor/update` | ESP32 HTTP 上报传感器数据 |
| POST | `/api/device/yard/pump` | 水泵控制（需 `password` 字段） |

---

## 服务器管理命令

```bash
# 查看服务状态
systemctl status yard-display

# 重启服务
systemctl restart yard-display

# 查看日志
journalctl -u yard-display -f

# 更新代码后重启
scp server.js public/index.html public/app.js root@117.72.55.63:/opt/yard-display/public/
scp server.js root@117.72.55.63:/opt/yard-display/
ssh root@117.72.55.63 "systemctl restart yard-display"
```

---

## 本地开发

```powershell
# Windows
.\start-demo.ps1
# 访问 http://localhost:3000
```

```bash
# Linux
./start-demo.sh
```

---

## ESP32 接入

- **MQTT**（推荐）：连接 `117.72.55.63:1884`，推送到 `garden/flower/dht11` 等主题，详见 `docs/mqtt-topics.md`
- **HTTP**：POST 到 `http://117.72.55.63/api/sensor/update`

---

## 环境变量

| 变量 | 默认值 | 说明 |
|------|--------|------|
| `PORT` | 3000 | HTTP 服务端口 |
| `MQTT_PORT` | 1884 | MQTT Broker 端口 |
| `PUMP_CONTROL_PASSWORD` | 1234 | 水泵控制密码 |
