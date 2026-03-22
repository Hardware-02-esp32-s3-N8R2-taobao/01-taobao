# -*- coding: utf-8 -*-
"""
SSH 登录服务器
首次运行：自动生成 RSA 密钥对并上传公钥到服务器（之后无需输密码）
后续运行：直接弹出新 PowerShell 窗口并建立 SSH 连接
"""

from __future__ import annotations

import subprocess
import sys
import time
from pathlib import Path

try:
    import paramiko
except ImportError:
    sys.exit("缺少 paramiko，请先运行: pip install paramiko")

HOST = "117.72.55.63"
USER = "root"
PASS = "Xk7mN9pLv2R4xQw8"

# 密钥存放路径（在用户 .ssh 目录下，避免和其他密钥冲突）
KEY_PATH = Path.home() / ".ssh" / "yard_server_rsa"


def ensure_local_key() -> paramiko.RSAKey:
    """若本地还没有密钥则生成，返回 RSAKey 对象。"""
    if KEY_PATH.exists():
        return paramiko.RSAKey.from_private_key_file(str(KEY_PATH))

    print("首次运行：正在生成 SSH 密钥对 ...")
    key = paramiko.RSAKey.generate(2048)
    KEY_PATH.parent.mkdir(parents=True, exist_ok=True)
    key.write_private_key_file(str(KEY_PATH))
    KEY_PATH.chmod(0o600)
    print(f"  私钥已保存：{KEY_PATH}")
    return key


def _transport_connect() -> paramiko.SSHClient:
    """用底层 Transport 连接，避免 paramiko 的 'No existing session' bug。"""
    import socket
    sock = socket.create_connection((HOST, 22), timeout=20)
    transport = paramiko.Transport(sock)
    transport.connect(username=USER, password=PASS)
    ssh = paramiko.SSHClient()
    ssh._transport = transport
    return ssh


def ensure_server_key(key: paramiko.RSAKey) -> None:
    """将公钥追加到服务器 authorized_keys（已存在则跳过）。"""
    pub_key_line = f"ssh-rsa {key.get_base64()} yard-server-auto"

    ssh = _transport_connect()

    _, stdout, _ = ssh.exec_command("cat ~/.ssh/authorized_keys 2>/dev/null || true")
    existing = stdout.read().decode()

    if key.get_base64() in existing:
        print("  服务器已有公钥，跳过上传。")
    else:
        print("  正在上传公钥到服务器 ...")
        cmds = [
            "mkdir -p ~/.ssh && chmod 700 ~/.ssh",
            f"echo '{pub_key_line}' >> ~/.ssh/authorized_keys",
            "chmod 600 ~/.ssh/authorized_keys",
        ]
        for cmd in cmds:
            ssh.exec_command(cmd)
            time.sleep(0.3)
        print("  公钥上传完成。")

    ssh.close()


def open_powershell() -> None:
    """弹出新 PowerShell 窗口，使用密钥直接 SSH 连接服务器。"""
    key_path_str = str(KEY_PATH).replace("\\", "/")
    ssh_cmd = (
        f"Write-Host '正在连接 {USER}@{HOST} ...' -ForegroundColor Cyan; "
        f"ssh -i '{key_path_str}' "
        f"-o StrictHostKeyChecking=no "
        f"-o ServerAliveInterval=60 "
        f"{USER}@{HOST}"
    )
    subprocess.Popen(
        ["powershell.exe", "-NoExit", "-Command", ssh_cmd],
        creationflags=subprocess.CREATE_NEW_CONSOLE,
    )
    print("PowerShell 窗口已打开，正在建立连接 ...")


def main() -> int:
    print(f"目标服务器：{USER}@{HOST}")
    key = ensure_local_key()
    ensure_server_key(key)
    open_powershell()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
