# 设备 OTA 升级方案

## 1. 目标定位

本方案用于给当前 `ESP32-C3` 固件增加可量产、可回滚、可在网页侧管理的 OTA 升级能力。

最终目标是：

1. 在网页中进入某台设备详情页后，可以进入管理员页面。
2. 管理员上传最终固件 `.bin` 文件。
3. 服务端为指定设备创建 OTA 升级任务。
4. 设备在合适时机下载新固件并完成升级。
5. 升级失败时自动回滚到上一版可用固件。

本方案同时要兼容当前已经存在的低功耗逻辑，尤其是设备会在上报成功后进入 `deep sleep` 的场景。

## 2. 当前项目现状

### 2.1 固件现状

当前主固件路径：

- `01-firmware/00-main-c3-firmware`

当前已确认的基础环境：

- 目标芯片：`ESP32-C3`
- ESP-IDF：当前构建环境可见为 `v5.1.2`
- Flash 容量：`4MB`
- 当前分区表：自定义单应用分区
- 当前固件暂无 OTA 实现

当前 `partitions.csv` 为：

```csv
# Name,     Type, SubType, Offset,   Size,     Flags
nvs,        data, nvs,     0x9000,   0x6000,
phy_init,   data, phy,     0xf000,   0x1000,
factory,    app,  factory, 0x10000,  0x3F0000,
```

这意味着当前设备把几乎整颗 4MB Flash 都给了单个应用分区，适合快速迭代，但不支持安全 OTA 双分区切换。

### 2.2 低功耗现状

当前低功耗逻辑已经落地在：

- `01-firmware/00-main-c3-firmware/main/app/telemetry_app.c`

当前行为是：

1. 设备唤醒后启动主流程。
2. 连接 Wi-Fi 和 MQTT。
3. 采集传感器并上报。
4. 如果启用了低功耗并且发布成功，则执行：
   - `network_service_prepare_for_sleep()`
   - `esp_sleep_enable_timer_wakeup(interval_us)`
   - `esp_deep_sleep_start()`

这说明：

1. 设备大部分时间可能根本不在线。
2. 设备一旦已经进入 `deep sleep`，网页端无法通过网络把它“立刻叫醒”。
3. OTA 触发必须和“设备下次唤醒窗口”配合。

### 2.3 通信现状

当前固件只有上行 MQTT 发布能力，尚未看到稳定的下行命令通道：

- 上行主题：`device/<deviceId>`

当前服务端已有：

- 设备详情页
- 历史数据 API
- 设备状态 API

但当前还没有：

- 固件上传管理
- OTA 任务管理
- 设备 OTA 检查接口
- OTA 下载接口
- OTA 状态页

## 3. 方案总原则

### 3.1 不建议走“浏览器直接把固件推给设备”

不建议的原因：

1. 当前设备多数场景在内网里，网页通常无法直接访问设备 IP。
2. 低功耗设备经常离线，浏览器发起上传时设备可能正在睡眠。
3. 浏览器直传缺少统一审计、权限控制、文件留存和失败重试能力。
4. 后续如果一个固件要升级多台设备，浏览器直传不方便统一调度。

### 3.2 推荐走“服务端托管固件 + 设备主动拉取”

推荐方案：

1. 管理员先把固件上传到服务端。
2. 服务端保存固件文件、版本信息、校验信息和 OTA 任务。
3. 设备在每次唤醒联网后，主动向服务端查询自己是否有待升级任务。
4. 如果有任务，再通过 HTTPS 下载新固件并执行 OTA。

这个方案最适合当前项目，原因是：

1. 设备本来就会周期性唤醒联网。
2. 即使设备睡着，任务也可以先挂在服务端等待。
3. 固件侧只需要新增“查询任务 + 下载升级”流程，不必先引入完整复杂的下行控制体系。
4. 网页管理员权限、文件存储、升级记录都可以放在服务端统一做。

## 4. 推荐整体架构

```text
管理员网页
  -> 上传固件到服务端
  -> 创建某台设备的 OTA 任务

服务端
  -> 保存 firmware.bin 与 metadata
  -> 记录 ota job / ota status
  -> 提供 OTA 查询 API
  -> 提供固件下载 URL

ESP32-C3 设备
  -> 每次唤醒联网后查询是否有 OTA
  -> 若有任务，则暂停进入低功耗
  -> 下载到 OTA 分区
  -> 重启进入新固件
  -> 首次启动自检成功后确认新固件有效
  -> 失败则自动回滚
```

## 5. Flash 分区方案

### 5.1 当前分区的问题

当前只有一个超大的 `factory` 分区，因此：

1. 新固件没有独立写入目标区。
2. 升级过程中如果直接覆盖当前运行固件，掉电风险很高。
3. 无法利用 ESP-IDF 标准 OTA 回滚机制。

### 5.2 推荐分区目标

对当前 `4MB Flash`，推荐改成“双 OTA 槽位 + otadata”的标准方案，并去掉 `factory` 分区。

推荐分区如下：

```csv
# Name,     Type, SubType, Offset,   Size,      Flags
nvs,        data, nvs,     0x9000,   0x4000,
otadata,    data, ota,     0xD000,   0x2000,
phy_init,   data, phy,     0xF000,   0x1000,
ota_0,      app,  ota_0,   0x10000,  0x1F0000,
ota_1,      app,  ota_1,   0x200000, 0x1F0000,
```

说明：

1. `otadata` 是 OTA 必需分区，用于记录下一次启动选择哪个 OTA 槽位。
2. `ota_0` 和 `ota_1` 各约 `1.94MB`。
3. 当前编译产物大小约 `1,034,752 B`，放进 `0x1F0000` 分区仍有较充足余量。
4. 去掉 `factory` 分区后，首次烧录可直接写入 `ota_0`；当 `otadata` 为空时，ESP-IDF 会从第一个 OTA 槽位启动。

### 5.3 为什么不保留 factory

在 4MB Flash 下，如果同时保留：

1. `factory`
2. `ota_0`
3. `ota_1`

那么每个 app 分区可用空间会明显被压缩。考虑到当前固件已经超过 `1MB`，继续保留 `factory` 没有必要，反而会压缩 OTA 可行性。

因此当前阶段更合适的是：

1. 生产烧录时只烧 `ota_0`
2. 正式升级时在 `ota_0 / ota_1` 间切换

## 6. 固件 OTA 实施方案

### 6.1 固件侧推荐能力

建议新增一个 `ota_service`，职责包括：

1. 查询服务端是否存在当前设备的待升级任务。
2. 判断当前是否允许进入 OTA。
3. 调用 `esp_https_ota` 执行升级。
4. 保存升级中状态，避免重启后状态丢失。
5. 首次启动执行自检并确认新固件有效。

建议新增模块：

- `main/app/ota_service.c`
- `main/include/ota_service.h`

### 6.2 设备每次唤醒后的流程

建议把 OTA 检查放在“Wi-Fi 已连通，且准备发布数据之前或之后”的固定阶段。

推荐流程：

1. 设备唤醒。
2. 初始化网络。
3. 连接 Wi-Fi。
4. 可选：连接 MQTT 并上报一次“设备已在线”状态。
5. 调用 OTA 检查接口，例如：
   - `GET /api/device/ota/check?deviceId=study-01&fwVersion=1.0.3`
6. 如果没有待升级任务：
   - 继续正常采样、上报、再按原逻辑休眠。
7. 如果有待升级任务：
   - 进入 OTA 模式。
   - 暂停本次 deep sleep。
   - 下载并写入非当前运行分区。
   - 切换 boot 分区。
   - 重启。

### 6.3 推荐使用 ESP-IDF 标准 OTA 能力

建议使用：

- `esp_https_ota`
- `esp_ota_mark_app_valid_cancel_rollback()`
- `esp_ota_mark_app_invalid_rollback_and_reboot()`

推荐开启：

- `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`

这样可以实现：

1. 新固件写入非运行分区。
2. 重启后只试运行一次。
3. 如果新固件首次启动没有完成确认，则自动回滚。

### 6.4 首次启动自检建议

新固件第一次启动后，应尽快做最小自检，不要拖很久。

建议自检项：

1. 主任务是否成功启动。
2. NVS 是否正常打开。
3. Wi-Fi 初始化是否成功。
4. 关键设备配置是否可读取。
5. 至少一次事件循环和定时器工作正常。

满足后调用：

- `esp_ota_mark_app_valid_cancel_rollback()`

若失败则调用：

- `esp_ota_mark_app_invalid_rollback_and_reboot()`

## 7. OTA 与低功耗的配合方案

### 7.1 关键结论

如果模块已经处于 `deep sleep`：

1. 无法通过网页立即发起网络 OTA。
2. 只能等待它下一次定时唤醒，或者依赖外部中断把它唤醒。

这点必须在产品逻辑上接受，不能把网页按钮设计成“点了就马上升级”，否则会造成误解。

### 7.2 推荐策略

管理员下发 OTA 任务后：

1. 服务端把任务状态标记为 `pending`。
2. 网页显示“等待设备下次唤醒”。
3. 设备下次醒来联网时主动查询任务。
4. 一旦发现待升级任务，本次唤醒周期内不再进入深睡。
5. OTA 完成后重启。
6. 新固件自检通过后，恢复正常低功耗节奏。

### 7.3 固件需要新增的“低功耗豁免”状态

建议新增一个运行态标志，例如：

- `ota_in_progress`
- `ota_pending`
- `maintenance_mode`

当这些状态为真时：

1. 禁止执行 `enter_low_power_sleep()`
2. 禁止在 OTA 过程中关闭 Wi-Fi
3. 允许更长的联网窗口

也就是说，当前这段逻辑需要改成“低功耗开启且没有 OTA 任务时才允许 sleep”。

### 7.4 电池和低电量保护

如果设备是电池供电，建议增加 OTA 前置判断：

1. 电量百分比低于阈值时不允许 OTA
2. 电池电压低于阈值时不允许 OTA
3. 若外部供电存在，可优先在充电/供电稳定时 OTA

建议初始阈值：

1. 电量 `< 30%` 不升级
2. 或电压低于你当前电池平台定义的安全阈值时不升级

服务端页面应显示：

1. `pending`
2. `blocked_low_battery`
3. `downloading`
4. `rebooting`
5. `success`
6. `rolled_back`

### 7.5 长周期休眠场景

如果设备低功耗周期很长，例如数小时：

1. 下发 OTA 后可能要等很久才开始执行。
2. 网页应根据“最后在线时间 + 低功耗周期”给出预计等待时间。

如果业务需要“尽快升级”，建议提供两种管理方式：

1. 管理员先把该设备低功耗周期改短，再等待下一次唤醒 OTA
2. 现场人工触发唤醒或重启

## 8. 服务端与网页方案

### 8.1 固件文件管理

服务端需要新增固件仓库能力。

建议保存内容：

1. 固件文件原始名
2. 固件内部版本号
3. 适用芯片，如 `esp32c3`
4. SHA256
5. 文件大小
6. 上传时间
7. 上传管理员
8. 发布说明

文件建议存放位置：

- `03-server/01-net-display-server/data/firmware/`

不要直接混在现有图片上传目录里。

### 8.2 OTA 任务模型

建议至少有两类数据表或 JSON 持久化结构：

1. `firmware_packages`
2. `ota_jobs`

`firmware_packages` 记录固件包本身。

`ota_jobs` 记录某台设备要不要升级到哪个固件，以及当前状态。

一个 `ota_job` 建议字段：

1. `jobId`
2. `deviceId`
3. `firmwareId`
4. `fromVersion`
5. `targetVersion`
6. `status`
7. `createdAt`
8. `startedAt`
9. `finishedAt`
10. `resultMessage`

### 8.3 推荐 API

管理员侧 API：

1. `POST /api/admin/firmware/upload`
2. `GET /api/admin/firmware/list`
3. `POST /api/admin/device/:deviceId/ota`
4. `GET /api/admin/device/:deviceId/ota/history`

设备侧 API：

1. `GET /api/device/ota/check?deviceId=...&fwVersion=...`
2. `POST /api/device/ota/report`
3. `GET /api/device/ota/download/:jobId?token=...`

设备检查接口返回建议：

```json
{
  "hasUpdate": true,
  "jobId": "ota_20260410_001",
  "targetVersion": "1.2.0",
  "sha256": "xxxx",
  "size": 1034752,
  "url": "https://your-server/api/device/ota/download/ota_20260410_001?token=xxxxx",
  "force": false,
  "minBatteryPercent": 30,
  "notes": "修复网络重连与新增 OTA 功能"
}
```

### 8.4 网页交互建议

在设备详情页中新增一个管理员页签，例如：

- `管理员`
- 或 `OTA 升级`

这个页签里至少要有：

1. 当前设备固件版本
2. 当前 OTA 状态
3. 上传固件入口
4. 选择目标固件入口
5. 发起升级按钮
6. 升级日志与结果

页面状态文案建议：

1. `当前设备正在休眠，等待下次唤醒后开始 OTA`
2. `设备已进入 OTA 模式，正在下载固件`
3. `新固件已写入，等待设备重启`
4. `新固件已确认生效`
5. `升级失败，已自动回滚`

## 9. 管理员权限建议

既然你明确希望“管理员进入的页面”才能操作 OTA，那么这里不能只做前端隐藏。

最低建议：

1. 服务端增加管理员登录态
2. 上传固件和创建 OTA 任务的 API 必须校验管理员身份
3. 设备侧下载接口不能裸奔公开，至少要带短期有效 token

如果当前项目还不准备上完整用户系统，第一阶段可以先做：

1. 服务端环境变量中的管理员密码
2. 登录成功后写 session cookie
3. 只有管理员 session 才能看到 OTA 页面并调用管理接口

## 10. 实施阶段建议

### 10.1 第一阶段：先打通最小闭环

目标：

1. 改分区表为 `ota_0 + ota_1 + otadata`
2. 固件侧增加 HTTPS OTA 下载能力
3. 服务端支持固件上传
4. 服务端支持单设备 OTA 任务
5. 网页详情页新增管理员 OTA 页签
6. 设备在每次唤醒后主动查询是否要升级

这一阶段先不追求批量升级、断点续传和签名校验。

### 10.2 第二阶段：补稳定性

目标：

1. 首次启动自检 + 回滚
2. OTA 进度上报
3. 低电量阻断
4. 升级失败原因展示
5. 设备休眠等待文案

### 10.3 第三阶段：补安全和运维能力

目标：

1. 固件签名校验
2. 更严格的 HTTPS 证书校验
3. 批量升级
4. 分批灰度发布
5. 可恢复的断点续传

## 11. 需要改动的主要代码位置

### 11.1 固件侧

预计主要涉及：

- `01-firmware/00-main-c3-firmware/partitions.csv`
- `01-firmware/00-main-c3-firmware/sdkconfig.defaults`
- `01-firmware/00-main-c3-firmware/main/app/telemetry_app.c`
- `01-firmware/00-main-c3-firmware/main/app/device_profile.c`
- `01-firmware/00-main-c3-firmware/main/include/device_profile.h`
- `01-firmware/00-main-c3-firmware/main/network/network_service.c`
- 新增 `ota_service.*`

### 11.2 服务端与网页

预计主要涉及：

- `03-server/01-net-display-server/server.js`
- `03-server/01-net-display-server/public/app.js`
- `03-server/01-net-display-server/public/index.html`

## 12. 本方案的关键结论

1. 当前必须先把单应用分区改成双 OTA 分区，否则不能做标准安全 OTA。
2. 对当前低功耗设备，正确方式不是“网页直接推固件到设备”，而是“服务端挂任务，设备下次唤醒后主动拉取”。
3. 如果设备已经在 `deep sleep`，网页不能立刻让它开始升级，只能等待下次唤醒或依赖外部唤醒。
4. OTA 期间必须临时禁止再次进入低功耗。
5. 必须启用回滚确认机制，避免新固件启动失败后设备变砖。

## 13. 参考资料

本方案参考了 ESP-IDF 官方 OTA 与分区表文档：

1. OTA 总体机制：
   - https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/ota.html
2. HTTPS OTA：
   - https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/esp_https_ota.html
3. 分区表：
   - https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/partition-tables.html

