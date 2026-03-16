# 03-imu

基于 `ESP-IDF` 的 `ICM42688` SPI 驱动示例工程。

这个工程先完成两件事：

- 通过 `SPI` 驱动 `ICM42688`
- 通过串口 shell 命令触发采样输出

实现上已经拆开：

- `main/icm42688.c`：纯驱动
- `main/imu_app.c`：设备初始化和采样逻辑
- `main/imu_shell.c`：串口命令交互
- `main/main.c`：程序入口

## 当前接线

按当前工程默认引脚连接：

- `ESP32 GPIO18` -> `ICM42688 SCLK`
- `ESP32 GPIO23` -> `ICM42688 SDI / MOSI`
- `ESP32 GPIO19` -> `ICM42688 SDO / MISO`
- `ESP32 GPIO5` -> `ICM42688 CS`
- `ESP32 3V3` -> `ICM42688 VCC`
- `ESP32 GND` -> `ICM42688 GND`

这版不使用中断引脚，所以：

- `ICM42688 INT1` 不接
- `ICM42688 INT2` 不接

注意：

- 传感器必须工作在 `3.3V`
- 不要接 `5V`

## 默认配置

- SPI 主机：`SPI2_HOST`
- SPI 频率：`1MHz`
- 串口波特率：`115200`

## Shell 命令

连上串口监视器后可以输入：

```text
help
whoami
once
start
start 5 200
status
stop
```

含义：

- `help`：查看帮助
- `whoami`：读取芯片 ID
- `once`：读取一帧数据
- `start`：默认输出 `10` 次，每次间隔 `200ms`
- `start 5 200`：输出 `5` 次，每次间隔 `200ms`
- `status`：看当前是否在连续输出
- `stop`：提前停止

## 预期输出

```text
ICM42688 WHO_AM_I = 0x47
imu temp=28.31C accel[g]=[0.012, -0.004, 0.998] gyro[dps]=[0.18, -0.31, 0.42]
```

## 编译与烧录

```powershell
. E:\Espressif\Initialize-Idf.ps1
cd d:\56-esp32\01-taobao\02-esp32\03-project\03-imu
idf.py set-target esp32
idf.py build
idf.py -p COM84 flash
idf.py -p COM84 monitor
```

## 调试建议

如果 `whoami` 不是 `0x47`，优先检查：

- `CS / SCLK / MOSI / MISO` 是否接反
- 是否接到了 `3.3V`
- 模块本身是否已经把 `AD0/SDO`、`CS` 之类做了板级复用
- 地线是否可靠
