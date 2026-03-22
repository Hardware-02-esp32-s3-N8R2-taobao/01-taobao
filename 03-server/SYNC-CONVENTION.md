# 服务器代码同步约定

> **本文件是给 Claude/Agent 的操作规范。每次修复服务器问题后，必须按照此约定执行代码同步。**

---

## 一、核心原则

| 原则 | 说明 |
|------|------|
| **本地优先修改** | 所有代码变更必须先在本地完成，再推送到服务器 |
| **服务器是运行环境** | 服务器 `/opt/yard-display/` 只运行代码，不直接编辑 |
| **双向同步** | 推送用 `04-`，拉取用 `05-`；每次修复完都要拉取一次确保本地最新 |
| **数据库不同步** | `data/sensor-history.db` 只在服务器，不备份到本地 |

---

## 二、文件目录映射

```
本地（开发工作区）                       服务器（运行工作区）
─────────────────────────────────────────────────────────────
03-server/01-net-display-server/    ↔   /opt/yard-display/
  server.js                         ↔     server.js
  package.json                      ↔     package.json
  package-lock.json                 ↔     package-lock.json
  lib/weather.js                    ↔     lib/weather.js
  public/index.html                 ↔     public/index.html
  public/app.js                     ↔     public/app.js
  systemd/netdisplay.service        →     /etc/systemd/system/yard-display.service（部署参考）
  systemd/nginx-yard-display.conf   →     /etc/nginx/sites-enabled/default（部署参考）

不同步（仅服务器）：
  /opt/yard-display/data/           ×     数据库，不备份
  /opt/yard-display/node_modules/   ×     服务器独立安装
```

---

## 三、日常操作脚本

| 脚本 | 命令 | 用途 |
|------|------|------|
| 打开网页 | `python 01-打开网页.py` | 检测服务是否在线，在线则打开浏览器 |
| SSH 登录 | `python 02-ssh登录服务器.py` | 弹出 PowerShell SSH 终端 |
| 内存分析 | `python 03-服务器内存分析.py` | 分析服务器内存占用，生成报告 |
| **本地→服务器** | `python 04-同步到服务器.py` | 上传变更文件，自动重启服务，打印日志 |
| **服务器→本地** | `python 05-从服务器同步到本地.py` | 拉取服务器最新代码到本地备份 |

所有脚本均在目录 `F:\01-dev-board\06-esp32s3\YD-ESP32-S3\03-server\`

---

## 四、标准修复流程（Agent 必须遵守）

```
发现问题
  │
  ▼
① 运行 04-同步到服务器.py
   → 查看"当前服务状态"和"最近日志"，定位问题原因
  │
  ▼
② 在本地修改代码
   → 只改 03-server/01-net-display-server/ 下的文件
   → 不要直接 SSH 进服务器改文件
  │
  ▼
③ 再次运行 04-同步到服务器.py
   → 自动上传变更文件 + 重启 yard-display 服务
   → 确认"服务状态: active"和日志无报错
  │
  ▼
④ 运行 01-打开网页.py
   → 确认返回"服务在线，正在打开浏览器"
  │
  ▼
⑤ 运行 05-从服务器同步到本地.py      ← 必须执行！
   → 将服务器最终代码同步回本地，保持本地备份最新
```

> ⚠️ **第 ⑤ 步不能省略。** 每次修复完成后必须执行一次服务器→本地同步，确保本地代码与服务器完全一致，以备换服务器时快速重新部署。

---

## 五、服务器基本信息

| 项目 | 值 |
|------|-----|
| 公网地址 | `http://117.72.55.63/` |
| 服务器 | 京东云 VPS，Ubuntu 24.04.2 LTS |
| Node.js 部署目录 | `/opt/yard-display/` |
| systemd 服务名 | `yard-display.service` |
| HTTP 端口 | 80（Nginx 反向代理 → 3000） |
| MQTT 端口 | 1884（Aedes 内嵌 Broker） |
| Nginx 配置 | `/etc/nginx/sites-enabled/default` |

---

## 六、换服务器快速重新部署步骤

> 当服务器迁移或重建时，按以下步骤从本地代码重新部署。

```bash
# 1. 新服务器安装 Node.js（v22+）、nginx
curl -fsSL https://deb.nodesource.com/setup_22.x | bash -
apt install -y nodejs nginx

# 2. 创建部署目录
mkdir -p /opt/yard-display/data

# 3. 上传代码（本地运行）
#    修改 04-同步到服务器.py 中的 HOST/PASS 后运行
python 04-同步到服务器.py

# 4. 在服务器上安装依赖
cd /opt/yard-display && npm ci --omit=dev

# 5. 安装 systemd 服务
cp systemd/netdisplay.service /etc/systemd/system/yard-display.service
systemctl daemon-reload
systemctl enable yard-display
systemctl start yard-display

# 6. 配置 Nginx
cp systemd/nginx-yard-display.conf /etc/nginx/sites-enabled/default
nginx -t && systemctl reload nginx

# 7. 验证
curl -I http://localhost/
```

---

## 七、常见问题排查

| 现象 | 排查步骤 |
|------|------|
| `01-打开网页.py` 提示无响应 | 先运行 `04-同步到服务器.py` 查看服务状态和日志 |
| 服务 inactive/failed | 查看 `journalctl -u yard-display -n 30` 找具体报错 |
| 代码改了但网页没变 | 确认运行了 `04-同步到服务器.py` 且有文件上传 |
| 内存不足 | 运行 `03-服务器内存分析.py` 查看占用并关闭京东云 Agent |
| nginx 502 Bad Gateway | yard-display 服务未启动，检查 `systemctl status yard-display` |

---

*最后更新：2026-03-22*
