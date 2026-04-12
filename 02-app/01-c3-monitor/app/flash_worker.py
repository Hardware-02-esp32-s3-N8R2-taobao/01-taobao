from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path

from PySide6 import QtCore

from firmware_package import FirmwarePackageError, load_firmware_package


class FullFlashWorker(QtCore.QObject):
    finished = QtCore.Signal(bool, str)
    log_line = QtCore.Signal(str)

    def __init__(self, firmware_path: str, port_name: str, baudrate: int) -> None:
        super().__init__()
        self._firmware_path = firmware_path
        self._port_name = port_name
        self._baudrate = baudrate

    def _resolve_esptool_cmd(self) -> list[str]:
        candidates: list[list[str]] = [[sys.executable, "-m", "esptool"]]

        try:
            from project_runtime import get_idf_command

            idf_cmd, _env = get_idf_command()
            if idf_cmd and idf_cmd[0].lower().endswith("python.exe"):
                candidates.append([idf_cmd[0], "-m", "esptool"])
        except Exception:
            pass

        tried: list[str] = []
        seen: set[tuple[str, ...]] = set()
        for candidate in candidates:
            key = tuple(candidate)
            if key in seen:
                continue
            seen.add(key)
            tried.append(" ".join(candidate))
            try:
                completed = subprocess.run(
                    [*candidate, "version"],
                    capture_output=True,
                    text=True,
                    check=False,
                    encoding="utf-8",
                    errors="replace",
                )
            except OSError:
                continue
            if completed.returncode == 0:
                return candidate

        raise RuntimeError(
            "未找到可用的 esptool 运行环境。已尝试："
            + "；".join(tried)
        )

    @QtCore.Slot()
    def run(self) -> None:
        temp_dir: tempfile.TemporaryDirectory[str] | None = None
        try:
            package = load_firmware_package(self._firmware_path)
            if not package.supports_full_flash:
                raise RuntimeError("当前固件文件不包含 bootloader/partition/app，无法执行空板全烧录")

            temp_dir = tempfile.TemporaryDirectory(prefix="yd-c3-flash-")
            temp_path = Path(temp_dir.name)
            segment_args: list[str] = []

            for segment in sorted(package.segments, key=lambda item: item.flash_offset):
                data = package.extract_segment_bytes(segment.role)
                target_path = temp_path / f"{segment.flash_offset:08x}_{segment.name}.bin"
                target_path.write_bytes(data)
                segment_args.extend([hex(segment.flash_offset), str(target_path)])

            flash_settings = package.flash_settings or {}
            chip = str(package.chip or "esp32c3").strip() or "esp32c3"
            cmd = [
                *self._resolve_esptool_cmd(),
                "--chip",
                chip,
                "--port",
                self._port_name,
                "--baud",
                str(self._baudrate),
                "--before",
                "default_reset",
                "--after",
                "hard_reset",
                "write_flash",
                "-z",
            ]
            if flash_settings.get("flash_mode"):
                cmd.extend(["--flash_mode", str(flash_settings["flash_mode"])])
            if flash_settings.get("flash_freq"):
                cmd.extend(["--flash_freq", str(flash_settings["flash_freq"])])
            if flash_settings.get("flash_size"):
                cmd.extend(["--flash_size", str(flash_settings["flash_size"])])
            cmd.extend(segment_args)

            self.log_line.emit("准备开始全烧录：")
            self.log_line.emit(" ".join(cmd))

            completed = subprocess.Popen(
                cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                encoding="utf-8",
                errors="replace",
            )
            assert completed.stdout is not None
            for line in completed.stdout:
                text = line.rstrip()
                if text:
                    self.log_line.emit(text)

            return_code = completed.wait()
            if return_code != 0:
                raise RuntimeError(f"esptool 执行失败，退出码 {return_code}")

            self.finished.emit(True, "全烧录完成，设备已写入 bootloader / partition / app")
        except (FirmwarePackageError, OSError, RuntimeError) as exc:
            self.finished.emit(False, str(exc))
        finally:
            if temp_dir is not None:
                temp_dir.cleanup()
