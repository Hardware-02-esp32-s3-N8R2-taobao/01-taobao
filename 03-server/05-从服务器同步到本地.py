# -*- coding: utf-8 -*-
"""
05-从服务器同步到本地.py
将服务器 /opt/yard-display/ 的核心代码文件下载到本地 01-net-display-server/。
每次修复完服务器问题后运行一次，确保本地始终是服务器代码的完整备份。

排除项（不下载）：node_modules/  data/  *.db  *.log
"""

import sys
import os
import socket
import ctypes
import hashlib
import datetime

if sys.platform == "win32":
    ctypes.windll.kernel32.SetConsoleCP(65001)
    ctypes.windll.kernel32.SetConsoleOutputCP(65001)
    sys.stdout.reconfigure(encoding="utf-8")
    sys.stderr.reconfigure(encoding="utf-8")
    handle = ctypes.windll.kernel32.GetStdHandle(-11)
    mode = ctypes.c_ulong()
    ctypes.windll.kernel32.GetConsoleMode(handle, ctypes.byref(mode))
    ctypes.windll.kernel32.SetConsoleMode(handle, mode.value | 0x0004)

try:
    import paramiko
except ImportError:
    sys.exit("缺少 paramiko，请先运行: pip install paramiko")

HOST       = "117.72.55.63"
PORT       = 22
USER       = "root"
PASS       = "Xk7mN9pLv2R4xQw8"
REMOTE_DIR = "/opt/yard-display"

LOCAL_DIR  = os.path.join(os.path.dirname(os.path.abspath(__file__)), "01-net-display-server")

# 从服务器下载的文件列表（相对于 REMOTE_DIR）
SYNC_FILES = [
    "server.js",
    "package.json",
    "package-lock.json",
    "lib/weather.js",
    "public/index.html",
    "public/app.js",
]

R = "\033[0m"; BOLD = "\033[1m"; RED = "\033[91m"; YLW = "\033[93m"
GRN = "\033[92m"; CYN = "\033[96m"; DIM = "\033[2m"; WHT = "\033[97m"

def ssh_connect():
    sock = socket.create_connection((HOST, PORT), timeout=15)
    t = paramiko.Transport(sock)
    t.connect(username=USER, password=PASS)
    c = paramiko.SSHClient()
    c._transport = t
    return c

def md5_of_bytes(data: bytes) -> str:
    return hashlib.md5(data).hexdigest()

def main():
    print(f"\n{BOLD}{CYN}{'='*60}{R}")
    print(f"{BOLD}{CYN}  服务器 → 本地 代码同步  {DIM}{datetime.datetime.now():%Y-%m-%d %H:%M:%S}{R}")
    print(f"{CYN}{'='*60}{R}")
    print(f"  远端: {HOST}:{REMOTE_DIR}")
    print(f"  本地: {LOCAL_DIR}\n")

    print(f"  {DIM}正在连接 {HOST} ...{R}")
    try:
        client = ssh_connect()
    except Exception as e:
        print(f"  {RED}连接失败: {e}{R}")
        sys.exit(1)
    print(f"  {GRN}连接成功{R}\n")

    sftp = client.open_sftp()

    downloaded = []
    skipped    = []
    failed     = []

    print(f"{BOLD}{WHT}[ 文件同步 ]{R}")
    for rel_path in SYNC_FILES:
        remote_path = f"{REMOTE_DIR}/{rel_path}"
        local_path  = os.path.join(LOCAL_DIR, rel_path.replace("/", os.sep))

        # 读取远端文件
        try:
            with sftp.file(remote_path, "rb") as f:
                remote_data = f.read()
        except FileNotFoundError:
            print(f"  {YLW}[SKIP]{R} {rel_path}  {DIM}(服务器上不存在){R}")
            skipped.append(rel_path)
            continue
        except Exception as e:
            print(f"  {RED}[ERR]{R}  {rel_path}: {e}")
            failed.append(rel_path)
            continue

        # 与本地比较
        local_md5 = None
        if os.path.exists(local_path):
            with open(local_path, "rb") as f:
                local_md5 = md5_of_bytes(f.read())
        remote_md5 = md5_of_bytes(remote_data)

        if local_md5 == remote_md5:
            print(f"  {DIM}[=] {rel_path}  (unchanged){R}")
            skipped.append(rel_path)
            continue

        # 写本地文件
        os.makedirs(os.path.dirname(local_path), exist_ok=True)
        with open(local_path, "wb") as f:
            f.write(remote_data)
        tag = f"{GRN}[NEW]{R}" if local_md5 is None else f"{YLW}[UPDATED]{R}"
        print(f"  {tag} {rel_path}  {DIM}({len(remote_data):,} bytes){R}")
        downloaded.append(rel_path)

    sftp.close()
    client.close()

    print(f"\n{CYN}{'='*60}{R}")
    print(f"  {GRN}下载: {len(downloaded)} 个{R}  {DIM}未变: {len(skipped)} 个  失败: {len(failed)} 个{R}")
    if downloaded:
        print(f"  {GRN}本地已是最新版本: {LOCAL_DIR}{R}")
    print(f"{CYN}{'='*60}{R}\n")

if __name__ == "__main__":
    main()
