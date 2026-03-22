# -*- coding: utf-8 -*-
"""
04-同步到服务器.py
将本地 01-net-display-server/ 的核心代码同步到服务器 /opt/yard-display/，
然后重启 systemd 服务，并打印最新日志。

工作流：
  1. 本地修改代码（server.js / public/app.js / lib/*.js 等）
  2. 运行本脚本
  3. 脚本自动上传变更文件、重启服务、验证状态

排除项：node_modules/  data/  *.log  .git
"""

import sys
import os
import stat
import hashlib
import ctypes
import datetime
import io

# ── Windows UTF-8 控制台 ──────────────────────────────────────────────────────
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

# ── 配置 ──────────────────────────────────────────────────────────────────────
HOST        = "117.72.55.63"
PORT        = 22
USER        = "root"
PASS        = "Xk7mN9pLv2R4xQw8"
REMOTE_DIR  = "/opt/yard-display"
SERVICE     = "yard-display"

# 本地代码目录（绝对路径）
LOCAL_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "01-net-display-server")

# 同步白名单：只同步这些路径（相对 LOCAL_DIR），支持目录（含子文件）和单文件
SYNC_INCLUDES = [
    "server.js",
    "package.json",
    "public/index.html",
    "public/app.js",
    "lib",            # 整个 lib/ 目录
]

# 排除关键字（路径包含时跳过）
EXCLUDE_KEYWORDS = ["node_modules", "data", ".git", ".db", ".log"]

# ── ANSI ──────────────────────────────────────────────────────────────────────
R = "\033[0m"; BOLD = "\033[1m"; RED = "\033[91m"; YLW = "\033[93m"
GRN = "\033[92m"; CYN = "\033[96m"; DIM = "\033[2m"; WHT = "\033[97m"

# ─────────────────────────────────────────────────────────────────────────────
def ssh_connect():
    import socket
    sock = socket.create_connection((HOST, PORT), timeout=15)
    t = paramiko.Transport(sock)
    t.connect(username=USER, password=PASS)
    client = paramiko.SSHClient()
    client._transport = t
    return client

def run(client, cmd, timeout=30):
    _, stdout, stderr = client.exec_command(cmd, timeout=timeout)
    out = stdout.read().decode("utf-8", errors="replace").strip()
    err = stderr.read().decode("utf-8", errors="replace").strip()
    return out, err

def md5_local(path):
    h = hashlib.md5()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()

def md5_remote(sftp, rpath):
    try:
        with sftp.file(rpath, "rb") as f:
            h = hashlib.md5()
            while True:
                chunk = f.read(65536)
                if not chunk:
                    break
                h.update(chunk)
            return h.hexdigest()
    except FileNotFoundError:
        return None

def collect_local_files():
    """返回 [(本地绝对路径, 相对路径)] 列表，按白名单过滤。"""
    result = []
    for include in SYNC_INCLUDES:
        local_path = os.path.join(LOCAL_DIR, include.replace("/", os.sep))
        if os.path.isfile(local_path):
            result.append((local_path, include))
        elif os.path.isdir(local_path):
            for root, dirs, files in os.walk(local_path):
                # 排除关键字目录
                dirs[:] = [d for d in dirs
                           if not any(kw in d for kw in EXCLUDE_KEYWORDS)]
                for fname in files:
                    if any(kw in fname for kw in EXCLUDE_KEYWORDS):
                        continue
                    abs_path = os.path.join(root, fname)
                    rel_path = os.path.relpath(abs_path, LOCAL_DIR).replace("\\", "/")
                    result.append((abs_path, rel_path))
    return result

def ensure_remote_dir(sftp, remote_path):
    """递归创建远端目录。"""
    dirs = remote_path.split("/")
    path = ""
    for d in dirs:
        if not d:
            continue
        path = path + "/" + d
        try:
            sftp.stat(path)
        except FileNotFoundError:
            sftp.mkdir(path)

def upload_file(sftp, local_path, remote_path):
    ensure_remote_dir(sftp, remote_path.rsplit("/", 1)[0])
    sftp.put(local_path, remote_path)

# ─────────────────────────────────────────────────────────────────────────────
def main():
    print(f"\n{BOLD}{CYN}{'='*60}{R}")
    print(f"{BOLD}{CYN}  代码同步到服务器  {DIM}{datetime.datetime.now():%Y-%m-%d %H:%M:%S}{R}")
    print(f"{CYN}{'='*60}{R}\n")

    # 1. 连接
    print(f"  {DIM}正在连接 {HOST} ...{R}")
    try:
        client = ssh_connect()
    except Exception as e:
        print(f"  {RED}连接失败: {e}{R}")
        sys.exit(1)
    print(f"  {GRN}连接成功{R}\n")

    sftp = client.open_sftp()

    # 2. 检查服务当前状态
    print(f"{BOLD}{WHT}[ 1. 当前服务状态 ]{R}")
    status_out, _ = run(client, f"systemctl is-active {SERVICE} 2>&1")
    color = GRN if status_out == "active" else RED
    print(f"  {SERVICE}: {color}{BOLD}{status_out}{R}")
    if status_out != "active":
        log_out, _ = run(client, f"journalctl -u {SERVICE} -n 15 --no-pager 2>&1")
        print(f"\n  {YLW}最近日志：{R}")
        for line in log_out.splitlines():
            print(f"  {DIM}{line}{R}")
    print()

    # 3. 收集本地文件并比对
    print(f"{BOLD}{WHT}[ 2. 比对本地 vs 服务器文件 ]{R}")
    local_files = collect_local_files()
    if not local_files:
        print(f"  {RED}未找到本地文件，请检查 LOCAL_DIR 路径：{LOCAL_DIR}{R}")
        sys.exit(1)

    to_upload = []   # [(local_abs, rel_path, reason)]
    unchanged = []

    for local_abs, rel_path in local_files:
        remote_abs = f"{REMOTE_DIR}/{rel_path}"
        remote_md5 = md5_remote(sftp, remote_abs)
        local_md5  = md5_local(local_abs)
        if remote_md5 is None:
            to_upload.append((local_abs, rel_path, "NEW"))
        elif remote_md5 != local_md5:
            to_upload.append((local_abs, rel_path, "CHANGED"))
        else:
            unchanged.append(rel_path)

    # 打印状态
    for rel in unchanged:
        print(f"  {DIM}[=] {rel}{R}")
    for local_abs, rel, reason in to_upload:
        tag = f"{GRN}[NEW]{R}" if reason == "NEW" else f"{YLW}[CHANGED]{R}"
        size = os.path.getsize(local_abs)
        print(f"  {tag} {rel}  {DIM}({size:,} bytes){R}")

    print()

    # 4. 上传
    if not to_upload:
        print(f"  {GRN}本地与服务器完全一致，无需上传。{R}\n")
        need_restart = False
    else:
        print(f"{BOLD}{WHT}[ 3. 上传 {len(to_upload)} 个文件 ]{R}")
        for local_abs, rel_path, _ in to_upload:
            remote_abs = f"{REMOTE_DIR}/{rel_path}"
            print(f"  上传 {CYN}{rel_path}{R} ...", end=" ", flush=True)
            try:
                upload_file(sftp, local_abs, remote_abs)
                print(f"{GRN}OK{R}")
            except Exception as e:
                print(f"{RED}失败: {e}{R}")
        need_restart = True
        print()

    sftp.close()

    # 5. 重启服务
    if need_restart:
        print(f"{BOLD}{WHT}[ 4. 重启服务 ]{R}")
        print(f"  执行: {YLW}systemctl restart {SERVICE}{R} ...", end=" ", flush=True)
        out, err = run(client, f"systemctl restart {SERVICE}", timeout=30)
        # 等待启动
        import time; time.sleep(3)
        active, _ = run(client, f"systemctl is-active {SERVICE}")
        if active == "active":
            print(f"{GRN}OK — 服务已运行{R}")
        else:
            print(f"{RED}失败 (状态: {active}){R}")
        print()
    else:
        print(f"{BOLD}{WHT}[ 4. 重启服务 ]{R}")
        print(f"  {DIM}文件无变化，跳过重启。{R}\n")

    # 6. 最终状态 + 最新日志
    print(f"{BOLD}{WHT}[ 5. 最终状态与日志 ]{R}")
    active, _ = run(client, f"systemctl is-active {SERVICE}")
    color = GRN if active == "active" else RED
    print(f"  服务状态: {color}{BOLD}{active}{R}")
    log_out, _ = run(client, f"journalctl -u {SERVICE} -n 20 --no-pager 2>&1")
    print(f"\n  {DIM}最近 20 行日志：{R}")
    for line in log_out.splitlines():
        line_color = RED if "error" in line.lower() or "fail" in line.lower() else DIM
        print(f"  {line_color}{line}{R}")
    print()

    client.close()

    # 7. 汇总
    print(f"{CYN}{'='*60}{R}")
    if active == "active":
        print(f"  {GRN}{BOLD}同步完成，服务正在运行{R}")
        print(f"  {GRN}网页: http://{HOST}/{R}")
    else:
        print(f"  {RED}{BOLD}服务未正常启动，请查看上方日志排查{R}")
    print(f"{CYN}{'='*60}{R}\n")

if __name__ == "__main__":
    main()
