# 安卓配网 App

目录：`05-andriod`

这个工程用于给 ESP32-C3 设备做 SoftAP 配网，配套固件会在 30 秒内无法连上已保存 Wi‑Fi 时，自动开启形如 `YD-PROV-XXXXXX` 的热点，并让 LED 快闪。

## 使用方式

1. 在 Android Studio 中打开 `05-andriod`
2. 等待 Gradle 同步完成
3. 安装到安卓手机
4. 当设备进入配网模式后，先在手机系统 Wi‑Fi 里连接设备热点
5. 打开 App，填写家里 Wi‑Fi 的 `SSID / 密码`
6. 点击“开始配网”

默认设备热点密码：`gf666666`

## 说明

- App 基于乐鑫官方 `esp-idf-provisioning-android` 库
- 当前先实现最小可用版本：手动输入或自动识别设备热点名，然后下发 Wi‑Fi 凭据
- 后续可以继续补：
  - 扫码识别设备热点
  - 自动发现附近 `YD-PROV-*` 热点
  - 批量保存多个 Wi‑Fi
