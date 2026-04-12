from __future__ import annotations

import hashlib
import os
import shutil
import subprocess
import sys
import venv
from importlib.util import find_spec
from pathlib import Path


ROOT_DIR = Path(__file__).resolve().parent
APP_DIR = ROOT_DIR / "01-c3-monitor"
VENV_DIR = APP_DIR / ".venv"
STAMP_FILE = VENV_DIR / ".requirements.sha256"
REQUIREMENTS_FILE = APP_DIR / "requirements.txt"
PROJECT_ROOT = ROOT_DIR.parent
sys.path.insert(0, str(PROJECT_ROOT))
EXPECTED_PYTHON = (3, 13)
EXPECTED_PYTHON_TEXT = f"{EXPECTED_PYTHON[0]}.{EXPECTED_PYTHON[1]}"

from project_runtime import ensure_machine_registration  # noqa: E402


def python_in_venv() -> Path:
    return VENV_DIR / "Scripts" / "python.exe"


def expected_version_ok(version_info: tuple[int, int]) -> bool:
    return version_info[:2] == EXPECTED_PYTHON


def find_expected_python_launcher() -> list[str]:
    candidates = [
        ["py", f"-{EXPECTED_PYTHON_TEXT}"],
        [f"python{EXPECTED_PYTHON_TEXT}"],
        ["python"],
        ["python3"],
    ]
    for candidate in candidates:
        try:
            completed = subprocess.run(
                [*candidate, "--version"],
                capture_output=True,
                text=True,
                check=False,
            )
        except OSError:
            continue
        if completed.returncode != 0:
            continue
        output = (completed.stdout or completed.stderr).strip()
        if output.startswith(f"Python {EXPECTED_PYTHON_TEXT}."):
            return candidate
    return []


def maybe_relaunch_with_expected_python() -> int | None:
    if expected_version_ok(sys.version_info):
        return None
    launcher = find_expected_python_launcher()
    if not launcher:
        print(
            f"当前运行此脚本的 Python 版本是 {sys.version_info.major}.{sys.version_info.minor}，"
            f"需要 Python {EXPECTED_PYTHON_TEXT}。"
        )
        print("请先安装对应版本，或使用启动上位机.bat 启动。")
        return 1
    print(f"检测到当前不是 Python {EXPECTED_PYTHON_TEXT}，正在切换解释器后重启...")
    return subprocess.call([*launcher, str(Path(__file__).resolve())])


def venv_python_version() -> tuple[int, int] | None:
    python_exe = python_in_venv()
    if not python_exe.exists():
        return None
    completed = subprocess.run(
        [str(python_exe), "-c", "import sys; print(f'{sys.version_info[0]}.{sys.version_info[1]}')"],
        capture_output=True,
        text=True,
        check=False,
    )
    if completed.returncode != 0:
        return None
    value = completed.stdout.strip()
    try:
        major, minor = value.split(".", 1)
        return int(major), int(minor)
    except ValueError:
        return None


def recreate_venv() -> None:
    if VENV_DIR.exists():
        print(f"检测到虚拟环境版本不匹配，正在重建为 Python {EXPECTED_PYTHON_TEXT}...")
        shutil.rmtree(VENV_DIR)


def ensure_venv() -> None:
    if python_in_venv().exists():
        return
    print("正在创建虚拟环境...")
    launcher = find_expected_python_launcher()
    if launcher:
        completed = subprocess.run([*launcher, "-m", "venv", str(VENV_DIR)], check=False)
        if completed.returncode == 0:
            return
    builder = venv.EnvBuilder(with_pip=True)
    builder.create(VENV_DIR)


def requirements_hash() -> str:
    return hashlib.sha256(REQUIREMENTS_FILE.read_bytes()).hexdigest()


def venv_is_ready() -> bool:
    if not python_in_venv().exists() or not STAMP_FILE.exists():
        return False
    return STAMP_FILE.read_text(encoding="utf-8").strip() == requirements_hash()


def install_requirements() -> int:
    python_exe = python_in_venv()
    print("正在安装依赖...")
    if run([str(python_exe), "-m", "pip", "install", "-r", "requirements.txt"]) != 0:
        return 1
    STAMP_FILE.write_text(requirements_hash(), encoding="utf-8")
    return 0


def current_python_has_deps() -> bool:
    return (
        find_spec("PySide6") is not None
        and find_spec("serial") is not None
        and find_spec("esptool") is not None
    )


def launch_in_process() -> int:
    os.chdir(APP_DIR)
    if str(APP_DIR) not in sys.path:
        sys.path.insert(0, str(APP_DIR))
    from main import main as app_main

    return app_main()


def launch_in_venv() -> int:
    python_exe = python_in_venv()
    return subprocess.call([str(python_exe), "main.py"], cwd=APP_DIR)


def run(cmd: list[str]) -> int:
    completed = subprocess.run(cmd, cwd=APP_DIR, check=False)
    return completed.returncode


def main() -> int:
    ensure_machine_registration("app-launch")
    relaunch_exit = maybe_relaunch_with_expected_python()
    if relaunch_exit is not None:
        return relaunch_exit
    if not APP_DIR.exists():
        print(f"未找到上位机目录: {APP_DIR}")
        return 1

    if current_python_has_deps():
        print("正在使用当前 Python 直接启动上位机...")
        return launch_in_process()

    current_venv_version = venv_python_version()
    if current_venv_version and not expected_version_ok(current_venv_version):
        recreate_venv()

    ensure_venv()
    if not venv_is_ready():
        if install_requirements() != 0:
            return 1

    print(f"正在从 Python {EXPECTED_PYTHON_TEXT} 虚拟环境启动上位机...")
    return launch_in_venv()


if __name__ == "__main__":
    raise SystemExit(main())
