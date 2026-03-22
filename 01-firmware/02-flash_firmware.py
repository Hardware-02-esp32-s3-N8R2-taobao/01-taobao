from __future__ import annotations

import argparse
import subprocess
import sys
import tkinter as tk
from pathlib import Path
from tkinter import messagebox, ttk

try:
    from serial.tools import list_ports
except ModuleNotFoundError:
    list_ports = None

SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_DIR = SCRIPT_DIR / "00-main-c3-firmware"
PROJECT_ROOT = SCRIPT_DIR.parent
sys.path.insert(0, str(PROJECT_ROOT))

from project_runtime import ensure_machine_registration, get_idf_command  # noqa: E402


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Flash ESP32 firmware with idf.py")
    parser.add_argument("--port", default="", help="Serial port, for example COM7")
    parser.add_argument(
        "--monitor",
        action="store_true",
        help="Open serial monitor after flashing",
    )
    return parser


def get_serial_ports() -> list[tuple[str, str]]:
    if list_ports is not None:
        ports = []
        for port in sorted(list_ports.comports(), key=lambda item: item.device):
            desc = port.description or "Unknown device"
            ports.append((port.device, desc))
        return ports

    completed = subprocess.run(
        [
            "powershell",
            "-NoProfile",
            "-Command",
            "[System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object",
        ],
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        check=False,
    )
    if completed.returncode != 0:
        return []
    ports = []
    for line in completed.stdout.splitlines():
        name = line.strip()
        if name:
            ports.append((name, "Serial Port"))
    return ports


def choose_serial_port(ports: list[tuple[str, str]]) -> str:
    if not ports:
        return ""

    root = tk.Tk()
    root.title("选择烧录串口")
    root.resizable(False, False)

    selected = tk.StringVar(value=ports[0][0])

    ttk.Label(root, text="请选择要烧录的串口：").pack(padx=16, pady=(14, 8), anchor="w")

    combo = ttk.Combobox(
        root,
        state="readonly",
        width=48,
        values=[f"{device}  |  {desc}" for device, desc in ports],
    )
    combo.current(0)
    combo.pack(padx=16, pady=(0, 12))

    button_frame = ttk.Frame(root)
    button_frame.pack(padx=16, pady=(0, 14), fill="x")

    def on_ok() -> None:
        index = combo.current()
        if index < 0:
            messagebox.showwarning("未选择串口", "请先选择一个串口。", parent=root)
            return
        selected.set(ports[index][0])
        root.destroy()

    def on_cancel() -> None:
        selected.set("")
        root.destroy()

    ttk.Button(button_frame, text="取消", command=on_cancel).pack(side="right")
    ttk.Button(button_frame, text="确定", command=on_ok).pack(side="right", padx=(0, 8))

    root.update_idletasks()
    width = root.winfo_width()
    height = root.winfo_height()
    x = (root.winfo_screenwidth() - width) // 2
    y = (root.winfo_screenheight() - height) // 2
    root.geometry(f"{width}x{height}+{x}+{y}")
    root.mainloop()
    return selected.get()


def resolve_port(requested_port: str) -> str:
    ports = get_serial_ports()
    available_names = {name for name, _ in ports}

    if requested_port and requested_port in available_names:
        return requested_port

    if requested_port and requested_port not in available_names:
        print(f"指定串口 `{requested_port}` 当前不可用，将改为弹窗选择。")

    chosen = choose_serial_port(ports)
    return chosen


def main() -> int:
    args = build_parser().parse_args()
    ensure_machine_registration("firmware-flash")
    port = resolve_port(args.port)
    if not port:
        print("未选择可用串口，已取消烧录。")
        return 1
    idf_cmd, env = get_idf_command()
    cmd = [*idf_cmd, "-p", port, "flash"]
    if args.monitor:
        cmd.append("monitor")
    print(f"Flashing firmware in: {PROJECT_DIR}")
    print("Command:", " ".join(cmd))
    try:
        completed = subprocess.run(cmd, cwd=PROJECT_DIR, env=env, check=False)
    except FileNotFoundError:
        print("idf.py not found. Please configure this machine in .project-machine-config.json or open an ESP-IDF shell.")
        return 1
    return completed.returncode


if __name__ == "__main__":
    raise SystemExit(main())
