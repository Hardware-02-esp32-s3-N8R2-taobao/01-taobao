# 01-uart-test

最基础的 ESP-IDF 串口测试工程。

当前工程特性：

- 串口每秒打印一次心跳日志
- 目标芯片：`ESP32`
- 当前测试串口：`COM84`
- 适合用来验证编译环境、烧录链路和串口监视器

## 1. 在 VS Code 中打开工程

建议直接打开这个工程目录：

- `d:\56-esp32\01-taobao\02-esp32\02-project\01-uart-test`

如果你装了 `ESP-IDF` 的 VS Code 插件，打开后先确认：

1. 左下角或 `ESP-IDF` 面板里能看到当前 IDF 环境
2. 串口选择为 `COM84`
3. 目标芯片是 `esp32`

## 2. 重新编译

在 VS Code 里有两种常用方式。

### 方式一：命令面板

1. 按 `Ctrl+Shift+P`
2. 输入 `ESP-IDF: Build your project`
3. 回车执行

### 方式二：ESP-IDF 侧边栏

1. 点击左侧 `ESP-IDF` 图标
2. 找到 `Build your project`
3. 点击执行

如果编译成功，底部终端会看到类似：

```text
Project build complete.
```

## 3. 烧录到开发板

### 先确认

- 开发板已经接到电脑
- 串口是 `COM84`
- 板子是 `ESP32`

### 在 VS Code 中烧录

1. 按 `Ctrl+Shift+P`
2. 输入 `ESP-IDF: Select Port to Use`
3. 选择 `COM84`
4. 再输入 `ESP-IDF: Flash your project`
5. 回车执行

如果烧录成功，终端里通常会看到类似：

```text
Hash of data verified.
Hard resetting via RTS pin...
```

## 4. 查看串口输出

### 在 VS Code 中打开串口监视器

1. 按 `Ctrl+Shift+P`
2. 输入 `ESP-IDF: Monitor your device`
3. 选择 `COM84`

默认波特率使用：

- `115200`

打开后你会看到类似输出：

```text
I (...) basic_demo: ESP32 basic serial demo starting
I (...) basic_demo: Heartbeat 0
hello from esp32 on COM84 heartbeat=0
```

## 5. 常用完整流程

如果你修改了代码，最常见流程就是：

1. `Build your project`
2. `Flash your project`
3. `Monitor your device`

## 6. 终端命令方式

如果你不想点 VS Code 菜单，也可以在 PowerShell 里执行：

```powershell
. E:\Espressif\Initialize-Idf.ps1
cd d:\56-esp32\01-taobao\02-esp32\02-project\01-uart-test
idf.py set-target esp32
idf.py build
idf.py -p COM84 flash
idf.py -p COM84 monitor
```

## 7. 常见问题

### 找不到串口

优先检查：

- 数据线是否是可传数据的
- 设备管理器里是否还能看到 `COM84`
- 板子是否重新枚举成了别的 `COM` 号

### 烧录失败

优先尝试：

- 重新插拔 USB
- 再次确认目标芯片是 `esp32`
- 确认没有别的串口工具占用 `COM84`

### 监视器没有输出

优先检查：

- 波特率是否为 `115200`
- 是否打开了正确串口
- 是否刚烧录完成后板子已经自动复位
