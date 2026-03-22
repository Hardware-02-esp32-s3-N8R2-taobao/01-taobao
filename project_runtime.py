from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
import platform
import shutil
import subprocess
import sys
import uuid
from pathlib import Path
from typing import Any
from glob import glob


PROJECT_ROOT = Path(__file__).resolve().parent
STATE_PATH = PROJECT_ROOT / ".project-machine-state.json"
CONFIG_PATH = PROJECT_ROOT / ".project-machine-config.json"


def utc_now() -> str:
    return dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def load_json(path: Path, default: dict[str, Any]) -> dict[str, Any]:
    if not path.exists():
        return json.loads(json.dumps(default))
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (json.JSONDecodeError, OSError):
        return json.loads(json.dumps(default))


def save_json(path: Path, data: dict[str, Any]) -> None:
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def read_windows_machine_guid() -> str:
    if os.name != "nt":
        return ""
    try:
        completed = subprocess.run(
            [
                "reg",
                "query",
                r"HKLM\SOFTWARE\Microsoft\Cryptography",
                "/v",
                "MachineGuid",
            ],
            capture_output=True,
            text=True,
            check=False,
        )
    except OSError:
        return ""
    if completed.returncode != 0:
        return ""
    for line in completed.stdout.splitlines():
        if "MachineGuid" in line:
            parts = [part for part in line.split() if part]
            if parts:
                return parts[-1].strip()
    return ""


def get_machine_id() -> str:
    hostname = platform.node() or os.environ.get("COMPUTERNAME", "") or "unknown-host"
    machine_guid = read_windows_machine_guid()
    mac = f"{uuid.getnode():012x}"
    raw = f"{hostname}|{machine_guid}|{mac}"
    digest = hashlib.sha1(raw.encode("utf-8")).hexdigest()[:12]
    return f"{hostname}-{digest}".lower()


def ensure_machine_registration(context: str = "") -> dict[str, Any]:
    machine_id = get_machine_id()
    state = load_json(STATE_PATH, {"machines": {}})
    entry = state["machines"].get(machine_id, {})
    entry.update(
        {
            "machineId": machine_id,
            "hostname": platform.node() or os.environ.get("COMPUTERNAME", ""),
            "projectPath": str(PROJECT_ROOT),
            "lastSeenAt": utc_now(),
        }
    )
    if context:
        entry["lastContext"] = context
    state["machines"][machine_id] = entry
    save_json(STATE_PATH, state)

    config = load_json(CONFIG_PATH, {"machines": {}})
    config_entry = config["machines"].get(machine_id, {})
    config_entry.setdefault("projectPath", str(PROJECT_ROOT))
    config_entry.setdefault("notes", "per-machine tool overrides")
    config_entry.setdefault("idfPath", "")
    config_entry.setdefault("idfPythonEnvPath", "")
    config_entry.setdefault("idfPyPath", "")
    config_entry.setdefault("nodePath", "")
    config_entry.setdefault("cloudflaredPath", "")
    config["machines"][machine_id] = config_entry
    save_json(CONFIG_PATH, config)
    return entry


def get_machine_config() -> dict[str, Any]:
    machine_id = get_machine_id()
    config = load_json(CONFIG_PATH, {"machines": {}})
    return config["machines"].get(machine_id, {})


def detect_python_launcher() -> str:
    for name in ("py", "python", "python3"):
        path = shutil.which(name)
        if path:
            return path
    return ""


def detect_node_path() -> str:
    config = get_machine_config()
    configured = config.get("nodePath", "")
    if configured and Path(configured).exists():
        return configured
    from_path = shutil.which("node")
    if from_path:
        return from_path
    candidates = [
        r"C:\Program Files\nodejs\node.exe",
        r"C:\Program Files (x86)\nodejs\node.exe",
    ]
    for candidate in candidates:
        if Path(candidate).exists():
            return candidate
    return "node"


def detect_cloudflared_path() -> str:
    config = get_machine_config()
    configured = config.get("cloudflaredPath", "")
    if configured and Path(configured).exists():
        return configured
    from_path = shutil.which("cloudflared")
    if from_path:
        return from_path
    candidates = [
        r"C:\Program Files\cloudflared\cloudflared.exe",
        r"C:\Users\10243\AppData\Local\Microsoft\WinGet\Packages\Cloudflare.cloudflared_Microsoft.Winget.Source_8wekyb3d8bbwe\cloudflared.exe",
        "/home/zerozero/.local/bin/cloudflared",
        "/usr/local/bin/cloudflared",
        "/usr/bin/cloudflared",
    ]
    for candidate in candidates:
        if Path(candidate).exists():
            return candidate
    return "cloudflared"


def _first_existing(paths: list[str]) -> str:
    for candidate in paths:
        if candidate and Path(candidate).exists():
            return candidate
    return ""


def detect_idf_path() -> str:
    config = get_machine_config()
    configured = config.get("idfPath", "")
    if configured and Path(configured).exists():
        return configured

    env_path = os.environ.get("IDF_PATH", "")
    if env_path and Path(env_path).exists():
        return env_path

    candidate_patterns = [
        r"D:\02-software-stash-cache\02-esp32-idf\frameworks\esp-idf-v*",
        r"C:\Espressif\frameworks\esp-idf-v*",
        r"C:\Users\*\Espressif\frameworks\esp-idf-v*",
    ]
    candidates: list[str] = []
    for pattern in candidate_patterns:
        candidates.extend(sorted(glob(pattern), reverse=True))
    return _first_existing(candidates)


def detect_idf_python_env_path(idf_path: str = "") -> str:
    config = get_machine_config()
    configured = config.get("idfPythonEnvPath", "")
    if configured and Path(configured).exists():
        return configured

    env_path = os.environ.get("IDF_PYTHON_ENV_PATH", "")
    if env_path and Path(env_path).exists():
        python_exe = Path(env_path) / "Scripts" / "python.exe"
        if python_exe.exists():
            return env_path

    base_candidates: list[str] = []
    idf_root = ""
    if idf_path:
        idf_root = str(Path(idf_path).resolve().parents[1])
        base_candidates.append(str(Path(idf_root) / "python_env"))

    idf_tools_path = os.environ.get("IDF_TOOLS_PATH", "")
    if idf_tools_path and Path(idf_tools_path).exists():
        base_candidates.append(str(Path(idf_tools_path) / "python_env"))
        base_candidates.append(idf_tools_path)

    base_candidates.extend(
        [
            r"D:\02-software-stash-cache\02-esp32-idf\python_env",
            r"C:\Espressif\python_env",
        ]
    )

    env_candidates: list[str] = []
    for base in base_candidates:
        if not base:
            continue
        env_candidates.extend(sorted(glob(str(Path(base) / "idf*_env")), reverse=True))

    for candidate in env_candidates:
        python_exe = Path(candidate) / "Scripts" / "python.exe"
        if python_exe.exists():
            return candidate
    return ""


def detect_idf_py_path(idf_path: str = "") -> str:
    config = get_machine_config()
    configured = config.get("idfPyPath", "")
    if configured and Path(configured).exists():
        return configured

    from_path = shutil.which("idf.py")
    if from_path:
        return from_path

    if idf_path:
        candidate = Path(idf_path) / "tools" / "idf.py"
        if candidate.exists():
            return str(candidate)
    return ""


def _latest_subdir(base: Path) -> str:
    if not base.exists() or not base.is_dir():
        return ""
    subdirs = sorted([p for p in base.iterdir() if p.is_dir()], reverse=True)
    if not subdirs:
        return ""
    return str(subdirs[0])


def detect_idf_tools_root(idf_path: str = "") -> str:
    env_tools = os.environ.get("IDF_TOOLS_PATH", "")
    if env_tools and Path(env_tools).exists():
        return env_tools

    candidates: list[str] = []
    if idf_path:
        idf_resolved = Path(idf_path).resolve()
        if len(idf_resolved.parents) >= 2:
            candidates.append(str(idf_resolved.parents[1] / "tools"))
    candidates.extend(
        [
            r"D:\02-software-stash-cache\02-esp32-idf\tools",
            r"C:\Espressif\tools",
        ]
    )
    return _first_existing(candidates)


def get_idf_extra_tool_paths(idf_tools_root: str) -> list[str]:
    if not idf_tools_root:
        return []

    tools_base = Path(idf_tools_root) / "tools"
    candidates = []

    cmake_dir = _latest_subdir(tools_base / "cmake")
    if cmake_dir:
        candidates.append(str(Path(cmake_dir) / "bin"))

    ninja_dir = _latest_subdir(tools_base / "ninja")
    if ninja_dir:
        candidates.append(ninja_dir)

    idf_exe_dir = _latest_subdir(tools_base / "idf-exe")
    if idf_exe_dir:
        candidates.append(idf_exe_dir)

    ccache_dir = _latest_subdir(tools_base / "ccache")
    if ccache_dir:
        candidates.append(ccache_dir)

    riscv_dir = _latest_subdir(tools_base / "riscv32-esp-elf")
    if riscv_dir:
        candidates.append(str(Path(riscv_dir) / "riscv32-esp-elf" / "bin"))

    riscv_gdb_dir = _latest_subdir(tools_base / "riscv32-esp-elf-gdb")
    if riscv_gdb_dir:
        candidates.append(str(Path(riscv_gdb_dir) / "riscv32-esp-elf-gdb" / "bin"))

    xtensa_esp32_dir = _latest_subdir(tools_base / "xtensa-esp32-elf")
    if xtensa_esp32_dir:
        candidates.append(str(Path(xtensa_esp32_dir) / "xtensa-esp32-elf" / "bin"))

    xtensa_esp32s2_dir = _latest_subdir(tools_base / "xtensa-esp32s2-elf")
    if xtensa_esp32s2_dir:
        candidates.append(str(Path(xtensa_esp32s2_dir) / "xtensa-esp32s2-elf" / "bin"))

    xtensa_esp32s3_dir = _latest_subdir(tools_base / "xtensa-esp32s3-elf")
    if xtensa_esp32s3_dir:
        candidates.append(str(Path(xtensa_esp32s3_dir) / "xtensa-esp32s3-elf" / "bin"))

    xtensa_gdb_dir = _latest_subdir(tools_base / "xtensa-esp-elf-gdb")
    if xtensa_gdb_dir:
        candidates.append(str(Path(xtensa_gdb_dir) / "xtensa-esp-elf-gdb" / "bin"))

    openocd_dir = _latest_subdir(tools_base / "openocd-esp32")
    if openocd_dir:
        candidates.append(str(Path(openocd_dir) / "openocd-esp32" / "bin"))

    return [path for path in candidates if path]


def get_idf_command() -> tuple[list[str], dict[str, str]]:
    env = os.environ.copy()
    env["PYTHONUTF8"] = "1"
    env["PYTHONIOENCODING"] = "utf-8"
    idf_path = detect_idf_path()
    idf_python_env_path = detect_idf_python_env_path(idf_path)
    idf_py_path = detect_idf_py_path(idf_path)
    idf_tools_root = detect_idf_tools_root(idf_path)

    if idf_path:
        env["IDF_PATH"] = idf_path
    if idf_tools_root:
        env["IDF_TOOLS_PATH"] = idf_tools_root
    if idf_python_env_path:
        env["IDF_PYTHON_ENV_PATH"] = idf_python_env_path

    path_parts: list[str] = []
    if idf_python_env_path:
        scripts_dir = Path(idf_python_env_path) / "Scripts"
        path_parts.append(str(scripts_dir))
    if idf_path:
        path_parts.append(str(Path(idf_path) / "tools"))
    path_parts.extend(get_idf_extra_tool_paths(idf_tools_root))
    if path_parts:
        env["PATH"] = os.pathsep.join(path_parts + [env.get("PATH", "")])

    if idf_py_path:
        idf_script = Path(idf_py_path)
        python_exe = ""
        if idf_python_env_path:
            candidate = Path(idf_python_env_path) / "Scripts" / "python.exe"
            if candidate.exists():
                python_exe = str(candidate)
        if python_exe:
            return [python_exe, str(idf_script)], env
        return [str(idf_script)], env

    return ["idf.py"], env


def cli_register(args: argparse.Namespace) -> int:
    entry = ensure_machine_registration(args.context or "")
    if not args.quiet:
        print(json.dumps(entry, ensure_ascii=False))
    return 0


def cli_tool_path(args: argparse.Namespace) -> int:
    ensure_machine_registration(f"tool:{args.tool}")
    mapping = {
        "node": detect_node_path,
        "cloudflared": detect_cloudflared_path,
        "python": detect_python_launcher,
    }
    resolver = mapping.get(args.tool)
    if not resolver:
        print("")
        return 1
    print(resolver())
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Project runtime helper")
    subparsers = parser.add_subparsers(dest="command", required=True)

    register = subparsers.add_parser("register")
    register.add_argument("--context", default="")
    register.add_argument("--quiet", action="store_true")
    register.set_defaults(func=cli_register)

    tool = subparsers.add_parser("tool-path")
    tool.add_argument("tool", choices=["node", "cloudflared", "python"])
    tool.set_defaults(func=cli_tool_path)

    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
