from __future__ import annotations

import json
import shutil
import re
import subprocess
import sys
from datetime import datetime
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_DIR = SCRIPT_DIR / "00-main-c3-firmware"
PROJECT_ROOT = SCRIPT_DIR.parent
BUILD_DIR = PROJECT_DIR / "build"
REPORT_PATH = SCRIPT_DIR / "03-build-report.md"
PROJECT_DESCRIPTION_PATH = BUILD_DIR / "project_description.json"
FLASHER_ARGS_PATH = BUILD_DIR / "flasher_args.json"
SDKCONFIG_PATH = PROJECT_DIR / "sdkconfig"
PARTITIONS_CSV_PATH = PROJECT_DIR / "partitions.csv"
APP_CONFIG_HEADER = PROJECT_DIR / "main" / "include" / "app_config.h"
SHORT_BIN_PREFIX = "yd-c3"
sys.path.insert(0, str(PROJECT_ROOT))

from firmware_package import build_firmware_package  # noqa: E402
from project_runtime import ensure_machine_registration, get_idf_command  # noqa: E402


def run_idf_command(
    idf_cmd: list[str], env: dict[str, str], *args: str, echo_output: bool = True
) -> subprocess.CompletedProcess[str]:
    cmd = [*idf_cmd, *args]
    completed = subprocess.run(
        cmd,
        cwd=PROJECT_DIR,
        env=env,
        check=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    should_echo = echo_output or completed.returncode != 0
    if should_echo and completed.stdout:
        print(completed.stdout, end="")
    if should_echo and completed.stderr:
        print(completed.stderr, end="", file=sys.stderr)
    return completed


def parse_size_summary(size_output: str) -> dict[str, int | float]:
    patterns = {
        "iram_used": r"Used stat D/IRAM:\s+(\d+)\s+bytes\s+\(\s*(\d+)\s+remain,\s*([\d.]+)% used\)",
        "data_size": r"\.data size:\s+(\d+)\s+bytes",
        "bss_size": r"\.bss\s+size:\s+(\d+)\s+bytes",
        "text_size": r"\.text size:\s+(\d+)\s+bytes",
        "flash_used": r"Used Flash size\s*:\s+(\d+)\s+bytes",
        "flash_text": r"\.text:\s+(\d+)\s+bytes",
        "flash_rodata": r"\.rodata:\s+(\d+)\s+bytes",
        "image_size": r"Total image size:\s+(\d+)\s+bytes",
    }

    result: dict[str, int | float] = {}

    iram_match = re.search(patterns["iram_used"], size_output)
    if iram_match:
        result["iram_used"] = int(iram_match.group(1))
        result["iram_remain"] = int(iram_match.group(2))
        result["iram_percent"] = float(iram_match.group(3))

    for key in ("data_size", "bss_size", "text_size", "flash_used", "flash_text", "flash_rodata", "image_size"):
        match = re.search(patterns[key], size_output)
        if match:
            result[key] = int(match.group(1))
    return result


def parse_binary_sizes(output: str) -> dict[str, int | float]:
    result: dict[str, int | float] = {}

    app_match = re.search(
        r"binary size (0x[0-9a-fA-F]+) bytes\. Smallest app partition is (0x[0-9a-fA-F]+) bytes\. (0x[0-9a-fA-F]+) bytes \((\d+)%\) free",
        output,
    )
    if app_match:
        result["app_bin_size"] = int(app_match.group(1), 16)
        result["app_partition_size"] = int(app_match.group(2), 16)
        result["app_partition_free"] = int(app_match.group(3), 16)
        result["app_partition_free_percent"] = int(app_match.group(4))

    boot_match = re.search(
        r"Bootloader binary size (0x[0-9a-fA-F]+) bytes\. (0x[0-9a-fA-F]+) bytes \((\d+)%\) free",
        output,
    )
    if boot_match:
        result["bootloader_bin_size"] = int(boot_match.group(1), 16)
        result["bootloader_free"] = int(boot_match.group(2), 16)
        result["bootloader_free_percent"] = int(boot_match.group(3))
    return result


def parse_top_archives(size_components_output: str, limit: int = 8) -> list[tuple[str, int]]:
    rows: list[tuple[str, int]] = []
    for line in size_components_output.splitlines():
        if not line.strip().startswith("lib"):
            continue
        parts = line.split()
        if len(parts) < 2:
            continue
        name = parts[0]
        try:
            total = int(parts[-1])
        except ValueError:
            continue
        rows.append((name, total))
    rows.sort(key=lambda item: item[1], reverse=True)
    return rows[:limit]


def fmt_bytes(value: int | float | None) -> str:
    if value is None:
        return "未知"
    return f"{int(value):,} B"


def fmt_kb(value: int | float | None) -> str:
    if value is None:
        return "未知"
    return f"{int(value) / 1024:.1f} KB"


def load_json_file(path: Path) -> dict:
    if not path.exists():
        return {}
    return json.loads(path.read_text(encoding="utf-8"))


def load_sdkconfig(path: Path) -> dict[str, str]:
    config: dict[str, str] = {}
    if not path.exists():
        return config
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        config[key] = value.strip().strip('"')
    return config


def parse_size_value(raw: str) -> int:
    value = raw.strip()
    if value.endswith(("K", "k")):
        return int(value[:-1], 0) * 1024
    if value.endswith(("M", "m")):
        return int(value[:-1], 0) * 1024 * 1024
    return int(value, 0)


def parse_partitions(path: Path) -> list[dict[str, int | str]]:
    partitions: list[dict[str, int | str]] = []
    if not path.exists():
        return partitions

    last_end = 0
    for raw_line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = raw_line.split("#", 1)[0].strip()
        if not line:
            continue
        parts = [part.strip() for part in line.split(",")]
        if len(parts) < 5:
            continue

        name, part_type, subtype, offset_raw, size_raw = parts[:5]
        offset = parse_size_value(offset_raw) if offset_raw else last_end
        size = parse_size_value(size_raw)
        end = offset + size
        partitions.append(
            {
                "name": name,
                "type": part_type,
                "subtype": subtype,
                "offset": offset,
                "size": size,
                "end": end,
            }
        )
        last_end = end
    return partitions


def bar(percent: float | int | None, width: int = 24) -> str:
    if percent is None:
        return "未知"
    value = max(0.0, min(float(percent), 100.0))
    filled = round(width * value / 100.0)
    return f"[{'#' * filled}{'-' * (width - filled)}] {value:.1f}%"


def load_app_firmware_version(path: Path) -> str:
    if not path.exists():
        return "unknown"
    content = path.read_text(encoding="utf-8", errors="replace")
    match = re.search(r'#define[ \t]+APP_FIRMWARE_VERSION[ \t]+"([^"]+)"', content)
    if not match:
        return "unknown"
    return match.group(1)


def load_app_release_notes(path: Path) -> str:
    if not path.exists():
        return ""
    content = path.read_text(encoding="utf-8", errors="replace")
    match = re.search(r'#define[ \t]+APP_FIRMWARE_RELEASE_NOTES[ \t]+"([^"]*)"', content)
    if not match:
        return ""
    return match.group(1)


def sync_versioned_firmware_bin(project_name: str, firmware_version: str) -> Path | None:
    app_bin_path = BUILD_DIR / f"{project_name}.bin"
    if not app_bin_path.exists():
        return None

    for stale_path in BUILD_DIR.glob(f"{project_name}_v*.bin"):
        if stale_path.name != f"{project_name}_v{firmware_version}.bin":
            stale_path.unlink(missing_ok=True)

    versioned_bin_path = BUILD_DIR / f"{project_name}_v{firmware_version}.bin"
    shutil.copy2(app_bin_path, versioned_bin_path)
    return versioned_bin_path


def publish_short_firmware_bin(source_path: Path, firmware_version: str) -> Path:
    for stale_path in SCRIPT_DIR.glob(f"{SHORT_BIN_PREFIX}_v*.bin"):
        if stale_path.name != f"{SHORT_BIN_PREFIX}_v{firmware_version}.bin":
            stale_path.unlink(missing_ok=True)

    short_bin_path = SCRIPT_DIR / f"{SHORT_BIN_PREFIX}_v{firmware_version}.bin"
    shutil.copy2(source_path, short_bin_path)
    return short_bin_path


def build_upgrade_package(
    project_name: str,
    firmware_version: str,
    release_notes: str,
    project_description: dict[str, object],
    flasher_args: dict[str, object],
) -> Path:
    for stale_path in BUILD_DIR.glob(f"{project_name}_package_v*.bin"):
        if stale_path.name != f"{project_name}_package_v{firmware_version}.bin":
            stale_path.unlink(missing_ok=True)

    flash_files = flasher_args.get("flash_files") or {}
    if not isinstance(flash_files, dict):
        raise RuntimeError("flasher_args.json 缺少 flash_files")

    segment_priority = {
        "bootloader": 0,
        "partition-table": 1,
        "otadata": 2,
        "app": 3,
    }
    name_map = {
        "bootloader": "bootloader",
        "partition-table": "partition-table",
        "otadata": "otadata",
        "app": "app",
    }

    segment_entries: list[dict[str, object]] = []
    for offset_text, relative_path in sorted(flash_files.items(), key=lambda item: int(item[0], 0)):
        role = "segment"
        relative_name = str(relative_path).replace("\\", "/")
        for candidate_key, candidate_name in name_map.items():
            candidate_entry = flasher_args.get(candidate_key) or {}
            if isinstance(candidate_entry, dict) and str(candidate_entry.get("file") or "").replace("\\", "/") == relative_name:
                role = candidate_name
                break
        segment_entries.append(
            {
                "name": Path(str(relative_path)).name,
                "role": role,
                "flash_offset": int(offset_text, 0),
                "path": BUILD_DIR / str(relative_path),
            }
        )

    segment_entries.sort(
        key=lambda item: (
            segment_priority.get(str(item.get("role") or ""), 99),
            int(item.get("flash_offset") or 0),
        )
    )

    package_path = BUILD_DIR / f"{project_name}_package_v{firmware_version}.bin"
    return build_firmware_package(
        package_path,
        package_version=firmware_version,
        release_notes=release_notes,
        chip=str(project_description.get("target") or "esp32c3"),
        flash_settings=dict(flasher_args.get("flash_settings") or {}),
        segments=segment_entries,
        ota_segment_role="app",
        source_name=package_path.name,
    )


def build_report(build_output: str, size_output: str, size_components_output: str) -> str:
    binary_info = parse_binary_sizes(build_output + "\n" + size_output + "\n" + size_components_output)
    size_info = parse_size_summary(size_output)
    top_archives = parse_top_archives(size_components_output)
    project_description = load_json_file(PROJECT_DESCRIPTION_PATH)
    flasher_args = load_json_file(FLASHER_ARGS_PATH)
    sdkconfig = load_sdkconfig(SDKCONFIG_PATH)
    partitions = parse_partitions(PARTITIONS_CSV_PATH)

    flash_settings = flasher_args.get("flash_settings", {})
    chip_target = project_description.get("target", "未知")
    project_name = project_description.get("project_name", "未知")
    project_version = project_description.get("project_version", "未知")
    min_rev = project_description.get("min_rev") or "未知"
    flash_size_cfg = flash_settings.get("flash_size") or sdkconfig.get("CONFIG_ESPTOOLPY_FLASHSIZE", "未知")
    flash_mode_cfg = flash_settings.get("flash_mode") or sdkconfig.get("CONFIG_ESPTOOLPY_FLASHMODE", "未知")
    flash_freq_cfg = flash_settings.get("flash_freq") or sdkconfig.get("CONFIG_ESPTOOLPY_FLASHFREQ", "未知")
    cpu_freq = sdkconfig.get("CONFIG_ESP32C3_DEFAULT_CPU_FREQ_MHZ", "未知")
    partition_table_name = sdkconfig.get("CONFIG_PARTITION_TABLE_FILENAME", "未知")
    partition_table_offset = int(sdkconfig.get("CONFIG_PARTITION_TABLE_OFFSET", "0x8000"), 0)
    partition_table_size = 0x1000
    flash_total_bytes_map = {
        "1MB": 1 * 1024 * 1024,
        "2MB": 2 * 1024 * 1024,
        "4MB": 4 * 1024 * 1024,
        "8MB": 8 * 1024 * 1024,
        "16MB": 16 * 1024 * 1024,
        "32MB": 32 * 1024 * 1024,
    }
    flash_total_bytes = flash_total_bytes_map.get(str(flash_size_cfg), None)

    iram_used = size_info.get("iram_used")
    iram_remain = size_info.get("iram_remain")
    iram_total = None
    if isinstance(iram_used, int) and isinstance(iram_remain, int):
        iram_total = iram_used + iram_remain

    data_size = size_info.get("data_size")
    bss_size = size_info.get("bss_size")
    static_ram = None
    if isinstance(data_size, int) and isinstance(bss_size, int):
        static_ram = data_size + bss_size

    app_partition_size = binary_info.get("app_partition_size")
    app_partition_free = binary_info.get("app_partition_free")
    app_partition_used_percent = None
    if isinstance(app_partition_size, int) and isinstance(app_partition_free, int):
        app_partition_used_percent = 100 - (app_partition_free / app_partition_size * 100)

    factory_partition = next((part for part in partitions if part["type"] == "app" and part["subtype"] == "factory"), None)
    nvs_partition = next((part for part in partitions if part["name"] == "nvs"), None)
    phy_partition = next((part for part in partitions if part["name"] == "phy_init"), None)

    lines = [
        "# 固件构建报告",
        "",
        f"- 生成时间：`{datetime.now().strftime('%Y-%m-%d %H:%M:%S')}`",
        f"- 工程目录：`{PROJECT_DIR}`",
        "",
        "## 识别结果",
        "",
        f"- 项目名：`{project_name}`",
        f"- 项目版本：`{project_version}`",
        f"- 芯片目标：`{chip_target}`",
        f"- 板卡定位：`ESP32-C3 SuperMini`",
        f"- 最低芯片版本要求：`rev {min_rev}`",
        f"- CPU 主频配置：`{cpu_freq} MHz`",
        f"- Flash 配置：`{flash_size_cfg}` / `{flash_mode_cfg}` / `{flash_freq_cfg}`",
        f"- 分区表来源：`{partition_table_name}`",
        "",
        "## 总结",
        "",
        f"- 应用固件大小：`{fmt_kb(binary_info.get('app_bin_size'))}`",
        f"- 应用分区大小：`{fmt_kb(binary_info.get('app_partition_size'))}`",
        f"- 应用分区剩余：`{fmt_kb(binary_info.get('app_partition_free'))}`，约 `{binary_info.get('app_partition_free_percent', '未知')}%`",
        f"- Bootloader 大小：`{fmt_kb(binary_info.get('bootloader_bin_size'))}`",
        f"- Bootloader 剩余：`{fmt_kb(binary_info.get('bootloader_free'))}`，约 `{binary_info.get('bootloader_free_percent', '未知')}%`",
        f"- Flash 实际使用：`{fmt_kb(size_info.get('flash_used'))}`",
        f"- 镜像总大小：`{fmt_kb(size_info.get('image_size'))}`",
        f"- IRAM 使用：`{fmt_kb(size_info.get('iram_used'))}`，剩余 `{fmt_kb(size_info.get('iram_remain'))}`，约 `{size_info.get('iram_percent', '未知')}%`",
        f"- 已初始化数据 `.data`：`{fmt_kb(size_info.get('data_size'))}`",
        f"- 未初始化数据 `.bss`：`{fmt_kb(size_info.get('bss_size'))}`",
        f"- RAM 中可执行代码 `.text`：`{fmt_kb(size_info.get('text_size'))}`",
        f"- Flash 中代码 `.text`：`{fmt_kb(size_info.get('flash_text'))}`",
        f"- Flash 中常量 `.rodata`：`{fmt_kb(size_info.get('flash_rodata'))}`",
        "",
        "## 核心资源可视化",
        "",
        f"- 应用分区占用：`{fmt_kb(binary_info.get('app_bin_size'))}` / `{fmt_kb(app_partition_size)}`  {bar(app_partition_used_percent)}",
        f"- Bootloader 区占用：`{fmt_kb(binary_info.get('bootloader_bin_size'))}` / `{fmt_kb((binary_info.get('bootloader_bin_size') or 0) + (binary_info.get('bootloader_free') or 0))}`  {bar(100 - binary_info.get('bootloader_free_percent', 0) if isinstance(binary_info.get('bootloader_free_percent'), int) else None)}",
        f"- IRAM 占用：`{fmt_kb(iram_used)}` / `{fmt_kb(iram_total)}`  {bar(size_info.get('iram_percent'))}",
        f"- RAM 静态占用(.data + .bss)：`{fmt_kb(static_ram)}`",
        "",
        "## 存储布局",
        "",
        "- 当前 `bootloader`、`partition table`、`app(用户固件)` 都位于同一颗外部 SPI Flash 中，只是地址分区不同，不是三颗独立存储器。",
        f"- Bootloader 偏移：`{flasher_args.get('bootloader', {}).get('offset', '未知')}`，文件：`{flasher_args.get('bootloader', {}).get('file', '未知')}`",
        f"- 分区表偏移：`0x{partition_table_offset:X}`，文件：`{flasher_args.get('partition-table', {}).get('file', '未知')}`",
        f"- 用户固件偏移：`{flasher_args.get('app', {}).get('offset', '未知')}`，文件：`{flasher_args.get('app', {}).get('file', '未知')}`",
        "- Bootloader 负责上电启动、基础硬件初始化、读取分区表并跳转到用户应用。",
        "- 用户通常不需要频繁改 bootloader，但它并不是完全不可修改；重新编译并烧录 bootloader 区后，它的内容可以被更新。",
        "- 风险上，bootloader 区比用户应用区更敏感，改坏后会导致整板无法正常启动，因此日常开发应优先改用户应用区。",
        "",
        "## 内部空间说明",
        "",
        f"- 当前 Flash 总配置：`{flash_size_cfg}`，它是这块板子用于存放 bootloader、分区表、应用固件和其他持久化内容的外部非易失存储。",
        f"- 当前应用分区大小：`{fmt_kb(app_partition_size)}`，这是用户主固件真正能使用的主要固件空间。",
        f"- 当前 IRAM 总量（按本次链接结果可见）：`{fmt_kb(iram_total)}`，其中已使用 `{fmt_kb(iram_used)}`。",
        f"- 当前 RAM 静态占用（`.data + .bss`）：`{fmt_kb(static_ram)}`，这部分会在程序运行时常驻内存。",
        "",
        "## 分区尺寸说明",
        "",
        "- 用户固件从 `0x10000` 开始，是因为 `0x0 ~ 0xFFFF` 这段前置空间已经分给了 bootloader、partition table、NVS 和 PHY 初始化数据。",
        "- 之前用户区虽然从 `0x10000` 开始，但总大小只有 `1 MB`，根因不是地址不够，而是工程原先使用了 ESP-IDF 默认 `single_app` 分区表，默认只给 factory app 分配 `0x100000`。",
        "- 这次已经改成自定义分区表，在保留最基础系统分区后，把剩余容量全部给了单一 `factory` 用户固件区。",
        "",
        "## 第二页：字节明细",
        "",
        f"- 应用固件大小：`{fmt_bytes(binary_info.get('app_bin_size'))}`",
        f"- 应用分区大小：`{fmt_bytes(binary_info.get('app_partition_size'))}`",
        f"- 应用分区剩余：`{fmt_bytes(binary_info.get('app_partition_free'))}`，约 `{binary_info.get('app_partition_free_percent', '未知')}%`",
        f"- Bootloader 大小：`{fmt_bytes(binary_info.get('bootloader_bin_size'))}`",
        f"- Bootloader 剩余：`{fmt_bytes(binary_info.get('bootloader_free'))}`，约 `{binary_info.get('bootloader_free_percent', '未知')}%`",
        f"- Flash 实际使用：`{fmt_bytes(size_info.get('flash_used'))}`",
        f"- 镜像总大小：`{fmt_bytes(size_info.get('image_size'))}`",
        f"- IRAM 使用：`{fmt_bytes(size_info.get('iram_used'))}`，剩余 `{fmt_bytes(size_info.get('iram_remain'))}`，约 `{size_info.get('iram_percent', '未知')}%`",
        f"- 已初始化数据 `.data`：`{fmt_bytes(size_info.get('data_size'))}`",
        f"- 未初始化数据 `.bss`：`{fmt_bytes(size_info.get('bss_size'))}`",
        f"- RAM 中可执行代码 `.text`：`{fmt_bytes(size_info.get('text_size'))}`",
        f"- Flash 中代码 `.text`：`{fmt_bytes(size_info.get('flash_text'))}`",
        f"- Flash 中常量 `.rodata`：`{fmt_bytes(size_info.get('flash_rodata'))}`",
        "",
        "## 怎么理解",
        "",
        "- `应用固件大小`：最终烧录到应用分区的主固件 `.bin` 文件大小。",
        "- `应用分区剩余`：当前分区还能再容纳多少空间，数值越小，后续加功能越容易爆分区。",
        "- `bootloader 区`：上电后最先执行的小程序，负责把系统带起来并跳转到用户固件。",
        "- `IRAM 使用`：内部指令 RAM 的占用情况，过高会影响链接和运行。",
        "- `RAM`：更偏泛指运行时内存；报告里的 `.data + .bss` 主要反映静态 RAM 占用。",
        "- `IRAM`：是 RAM 的一个子区域，偏向放需要更快执行或必须放在内部执行存储器中的代码，不等同于全部 RAM。",
        "- `.data + .bss`：主要反映 RAM 静态占用，越高说明运行时常驻内存越紧张。",
        "- `Flash .text + .rodata`：主要反映程序代码和常量占用 Flash 的情况。",
        "- `Flash` 不是这里说的 `ROM`。ESP32-C3 仍然有芯片内部 ROM（Boot ROM、部分 ROM 函数），但你现在编译出来的 bootloader 和应用固件都主要存放在外部 Flash 里。",
        "- 可以简单理解为：现代 ESP32 项目里，用户可更新的固件基本都在 Flash，不在不可修改的 ROM 中。",
        "",
        "## 存储分区尺寸表",
        "",
    ]

    if flash_total_bytes is not None:
        lines.append(f"- 外部 Flash 总容量：`{fmt_kb(flash_total_bytes)}`（`{fmt_bytes(flash_total_bytes)}`）")
    else:
        lines.append(f"- 外部 Flash 总容量：`{flash_size_cfg}`")

    lines.append(f"- Bootloader 保留区域：起点 `0x0`，到分区表之前结束，当前 bootloader 文件大小约 `{fmt_kb(binary_info.get('bootloader_bin_size'))}`。")
    lines.append(f"- Partition Table：起点 `0x{partition_table_offset:X}`，固定大小 `{fmt_kb(partition_table_size)}`。")
    if nvs_partition:
        lines.append(
            f"- `nvs`：起点 `0x{int(nvs_partition['offset']):X}`，大小 `{fmt_kb(int(nvs_partition['size']))}`，结束 `0x{int(nvs_partition['end']):X}`。"
        )
    if phy_partition:
        lines.append(
            f"- `phy_init`：起点 `0x{int(phy_partition['offset']):X}`，大小 `{fmt_kb(int(phy_partition['size']))}`，结束 `0x{int(phy_partition['end']):X}`。"
        )
    if factory_partition:
        factory_size = int(factory_partition["size"])
        lines.append(
            f"- `factory` 用户固件区：起点 `0x{int(factory_partition['offset']):X}`，大小 `{fmt_kb(factory_size)}`，结束 `0x{int(factory_partition['end']):X}`。"
        )
        app_bin_size = binary_info.get("app_bin_size")
        if isinstance(app_bin_size, int):
            lines.append(
                f"- 当前用户固件在该分区中的实际占比：`{fmt_kb(app_bin_size)}` / `{fmt_kb(factory_size)}`，约 `{app_bin_size / factory_size * 100:.1f}%`。"
            )

    lines.extend(
        [
            "",
            "## 最大空间占用模块",
            "",
        ]
    )

    if top_archives:
        for name, total in top_archives:
            lines.append(f"- `{name}`：`{total / 1024:.1f} KB`（`{total:,} B`）")
    else:
        lines.append("- 未解析到组件占用明细")

    lines.extend(
        [
            "",
            "## Flash 主要消耗来源",
            "",
            "- 从本次构建结果看，Flash 资源的大头主要不是你自己的 `main`，而是联网和安全相关基础库。",
            "- 最大的几类占用依次集中在：`mbedcrypto`、`lwip`、`net80211`、`libc`、`wpa_supplicant`。",
            "- 这说明当前 Flash 压力主要来自：`TLS/加密`、`TCP/IP 协议栈`、`Wi-Fi 驱动与 802.11 协议`、`C 标准库`。",
            "- 你自己的主业务代码当前在 `libmain.a` 中，约 `12.1 KB`，不是当前 Flash 逼近上限的主要来源。",
            "",
        ]
    )

    lines.extend(
        [
            "",
            "## 构建判断",
            "",
        ]
    )

    free_percent = binary_info.get("app_partition_free_percent")
    iram_percent = size_info.get("iram_percent")
    if isinstance(free_percent, int):
        if free_percent <= 10:
            lines.append("- 当前应用分区余量已经比较紧，后续新增功能前建议优先关注 Flash 占用。")
        else:
            lines.append("- 当前应用分区仍有一定余量，可以继续迭代，但建议持续跟踪构建报告。")
    else:
        lines.append("- 本次未成功解析应用分区余量，请检查构建输出。")

    if isinstance(iram_percent, float):
        if iram_percent >= 80:
            lines.append("- IRAM 占用已经偏高，后续若再增加底层驱动或高频代码，建议重点检查 IRAM 分布。")
        else:
            lines.append("- 当前 IRAM 占用处于可接受范围。")
    else:
        lines.append("- 本次未成功解析 IRAM 占用，请检查 size 输出。")

    lines.extend(
        [
            "",
            "## 已确认后续动作",
            "",
            "- 已接受：后续新增功能前，优先关注 Flash 占用与应用分区余量。",
            "- 已接受：当前 IRAM 占用暂时视为可接受范围，后续继续跟踪，不作为当前阻塞项。",
            "- 执行方式：每次运行 `01-build_firmware.py` 后，先查看本报告中的应用分区剩余、Flash 使用量和最大空间占用模块。",
            "",
            "## 优化建议页",
            "",
            "### 1. 如果优先想减 Flash，先看哪些库",
            "",
            "- 第一优先级：`mbedcrypto`、`mbedtls`、`wpa_supplicant`。这些通常和 TLS、证书、加密算法、WPA/WPA3 能力有关，收益通常最大。",
            "- 第二优先级：`lwip`、`net80211`、`libpp`。这些属于网络栈和 Wi-Fi 协议栈，能不能减取决于你是否还能继续保留当前联网能力。",
            "- 第三优先级：检查 `libmain.a`。虽然当前它不是最大头，但这是你自己最容易直接控制的业务代码区域。",
            "",
            "### 2. 如果应用分区太小，怎么扩",
            "",
            f"- 当前已经切换为自定义单应用分区表，应用分区约 `{fmt_kb(app_partition_size)}`。",
            "- 如果后续功能继续增长，最直接的方法不是先死抠代码，而是继续调整分区表，重新规划用户固件区和其他数据分区的比例。",
            "- 常见方向：减少额外数据分区、取消 OTA 双分区、或者重新规划 `nvs / spiffs / ota` 空间。",
            "- 需要一起检查：`sdkconfig` 中的分区表配置、`partitions.csv` 或默认分区表来源，以及烧录后是否还保留你需要的持久化区域。",
            "",
            "### 3. 当前最值得先排查的配置项",
            "",
            "- `mbedTLS` 证书包：当前启用了完整证书包，这通常会明显增加 Flash 占用。",
            "- `MQTT / TLS / WebSocket Secure`：如果实际不需要安全连接或部分传输方式，可以检查是否有裁剪空间。",
            "- `WPA3 / 扩展 Wi-Fi 能力`：如果实际场景不需要，相关能力可能可以裁剪。",
            "- `HTTP / HTTPS / Provisioning / 文件系统` 等未使用组件：如果工程没真正用到，值得排查是否被默认拉入。",
            "- 日志级别与调试能力：正式版可考虑减小日志、关闭部分调试选项。",
            "",
            "### 4. 推荐优化顺序",
            "",
            "- 第一步：先看 `最大空间占用模块`，确认是不是网络和加密库导致空间紧张。",
            "- 第二步：排查是否有“开着但没用”的功能组件。",
            f"- 第三步：如果仍然不够，再考虑继续调整分区表；当前应用区已经扩到约 `{fmt_kb(app_partition_size)}`。",
            "- 第四步：最后再进入业务代码级别的细抠优化。",
        ]
    )

    return "\n".join(lines) + "\n"


def main() -> int:
    ensure_machine_registration("firmware-build")
    idf_cmd, env = get_idf_command()
    print(f"Building firmware in: {PROJECT_DIR}")

    try:
        print("阶段 1/3：构建固件...")
        build_completed = run_idf_command(idf_cmd, env, "build", echo_output=False)
    except FileNotFoundError:
        print("idf.py not found. Please configure this machine in .project-machine-config.json or open an ESP-IDF shell.")
        return 1

    if build_completed.returncode != 0:
        return build_completed.returncode

    print("阶段 2/3：生成尺寸统计...")
    size_completed = run_idf_command(idf_cmd, env, "size", echo_output=False)
    if size_completed.returncode != 0:
        return size_completed.returncode

    size_components_completed = run_idf_command(idf_cmd, env, "size-components", echo_output=False)
    if size_components_completed.returncode != 0:
        return size_components_completed.returncode

    print("阶段 3/3：整理报告与发布文件...")
    report = build_report(build_completed.stdout, size_completed.stdout, size_components_completed.stdout)
    REPORT_PATH.write_text(report, encoding="utf-8")
    project_description = load_json_file(PROJECT_DESCRIPTION_PATH)
    flasher_args = load_json_file(FLASHER_ARGS_PATH)
    project_name = str(project_description.get("project_name") or PROJECT_DIR.name)
    firmware_version = load_app_firmware_version(APP_CONFIG_HEADER)
    release_notes = load_app_release_notes(APP_CONFIG_HEADER)
    versioned_bin_path = sync_versioned_firmware_bin(project_name, firmware_version)
    package_path = build_upgrade_package(
        project_name,
        firmware_version,
        release_notes,
        project_description,
        flasher_args,
    )
    short_bin_path = publish_short_firmware_bin(package_path, firmware_version)

    print("构建完成。")
    print(f"报告文件已生成：{REPORT_PATH}")
    if versioned_bin_path is not None:
        print(f"版本固件已更新：{versioned_bin_path}")
    print(f"统一升级包已生成：{package_path}")
    print(f"短文件名固件已发布：{short_bin_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
