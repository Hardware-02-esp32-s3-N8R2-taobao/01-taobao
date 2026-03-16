# YD-ESP32-S3 最简单下载与调试说明

这块板子底部有两个 Type-C 口，按照仓库里的板图说明可以这样区分：

- 左侧 Type-C：`ESP32-S3 USB&OTG`，这是 ESP32-S3 原生 USB 口。
- 右侧 Type-C：`USB to serial CH343P`，这是 USB 转串口口。

如果你现在只是想“先把程序烧进去，再看运行结果”，最简单的做法是：

- 只用右侧 `CH343P` 这个口。
- 装好驱动后，用 `Thonny + MicroPython 固件`。
- 通过 Thonny 直接刷固件、直接看串口输出、直接保存脚本到板子里。

## 1. 先准备什么

仓库里已经给好了需要用到的文件：

- CH343 驱动：
  - `01-document/0-public-USB to serial CH343 driver(important)/1-Windows-CH343SER.EXE`
- MicroPython 固件：
  - `01-document/1-MPY-firmware/YD-ESP32-S3-N8R2-MPY-V1.1.bin`
  - `01-document/1-MPY-firmware/YD-ESP32-S3-N8R8-MPY-V1.1.bin`
  - `01-document/1-MPY-firmware/YD-ESP32-S3-N16R8-MPY-V1.1.bin`
- Thonny 下载入口：
  - `01-document/3-MPY-Thonny/Micropython-IDE-Thonny.html`

## 2. 两个 USB 口怎么用

建议你先这样记：

- 右侧 CH343P 口：
  - 用于最省心的下载、串口日志、REPL 调试。
  - Windows 下一般会识别成一个 `COM` 口。
  - 新手优先用这个口。
- 左侧原生 USB 口：
  - 更偏向 ESP32-S3 的原生 USB/OTG 功能开发。
  - 以后你要玩 USB 设备、USB Host、TinyUSB、JTAG/USB CDC 一类功能时再用。
  - 这次最简单测试可以先完全不接它。

一句话版本：

- 想下载和调试，插右边。
- 左边这次先不用。

## 3. 固件怎么选

仓库给了 3 个 MicroPython 固件，分别对应不同内存版本：

- `N8R2`
- `N8R8`
- `N16R8`

你需要选和自己板子一致的版本。

如果你的购买链接、标签或者丝印写的是：

- `N8R2`，就刷 `YD-ESP32-S3-N8R2-MPY-V1.1.bin`
- `N8R8`，就刷 `YD-ESP32-S3-N8R8-MPY-V1.1.bin`
- `N16R8`，就刷 `YD-ESP32-S3-N16R8-MPY-V1.1.bin`

如果你一时分不清型号，先回看购买页面是最稳妥的。

## 4. 最简单下载步骤

### 第一步：安装驱动

运行：

- `01-document/0-public-USB to serial CH343 driver(important)/1-Windows-CH343SER.EXE`

装完后，把数据线插到板子的右侧 `CH343P` 口。

如果设备管理器里出现新的 `COM` 口，说明驱动和连线基本正常。

### 第二步：安装 Thonny

打开：

- `01-document/3-MPY-Thonny/Micropython-IDE-Thonny.html`

它会跳转到 Thonny 官网，下载安装即可。

### 第三步：用 Thonny 刷 MicroPython

打开 Thonny 后：

1. 点击 `工具 -> 选项 -> 解释器`
2. 解释器选择 `MicroPython (ESP32)`
3. 端口选择刚才出现的那个 `COM` 口
4. 点击 `安装或更新 MicroPython`
5. 选择 `本地固件`
6. 选中仓库里的对应 `.bin` 文件
7. 开始安装

如果安装过程中提示连接失败，或者一直进不了下载模式，就这样操作：

1. 按住板上的 `BOOT`
2. 短按一下 `RST`
3. 在 Thonny 里再次点击安装
4. 等开始下载后再松开 `BOOT`

## 5. 最简单调试方式

最简单的调试方式也是用右侧 `CH343P` 口，不需要额外工具：

- 在 Thonny 的 `Shell` 窗口看 `print()` 输出
- 在 Thonny 里直接运行脚本
- 把脚本保存到开发板里，文件名保存成 `main.py`

这样板子每次上电都会自动运行。

## 6. Demo 说明

`02-pr/main.py` 是一个最小测试 demo，基于仓库里的现成示例整理而来，使用了：

- 板载 RGB 灯：`GPIO48`
- 板载 `BOOT` 按键：`GPIO0`

运行效果：

- 默认循环显示红、绿、蓝三种颜色
- 按下 `BOOT` 键时，灯会快速闪白色一次
- Thonny Shell 会打印 `BOOT pressed`

## 7. Demo 怎么放到板子里

刷好 MicroPython 后，在 Thonny 里：

1. 打开 `02-pr/main.py`
2. 点击运行，先确认能跑
3. 再点击 `文件 -> 另存为`
4. 选择 `MicroPython device`
5. 文件名保存为 `main.py`

以后重新上电，demo 会自动启动。

## 8. 看到什么算成功

满足下面任意几条，基本就说明板子通了：

- Thonny 能连接到板子
- Shell 里没有报连接错误
- RGB 灯开始变色
- 按下 `BOOT` 键时出现白色闪烁
- Shell 里打印出 `BOOT pressed`

## 9. 常见问题

### 看不到串口

优先检查：

- 是否插的是右侧 `CH343P` 口
- CH343 驱动是否已经安装
- 数据线是否是可传数据的，不是纯充电线

### 能上电但不能刷

优先尝试：

- 按住 `BOOT` 后再点 `RST`
- 换一个 USB 口
- 换一根数据线

### 左侧口能不能用来开发

可以，但不建议作为第一次上手路径。

这次先把右侧串口口跑通，后面再考虑左侧原生 USB 的玩法，会轻松很多。
