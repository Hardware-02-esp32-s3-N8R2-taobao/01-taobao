# -*- coding: utf-8 -*-
"""
03-服务器内存分析.py
SSH 连接服务器，分析内存占用，输出彩色控制台报告并保存 Markdown 文件。
自动识别可关闭的京东云内置 Agent，给出关闭原因和关闭后预估剩余内存。
"""

import sys
import os
import ctypes
import datetime

# ── Windows UTF-8 控制台 ──────────────────────────────────────────────────────
if sys.platform == "win32":
    ctypes.windll.kernel32.SetConsoleCP(65001)
    ctypes.windll.kernel32.SetConsoleOutputCP(65001)
    sys.stdout.reconfigure(encoding="utf-8")
    sys.stderr.reconfigure(encoding="utf-8")
    # 启用 ANSI 转义序列
    handle = ctypes.windll.kernel32.GetStdHandle(-11)
    mode = ctypes.c_ulong()
    ctypes.windll.kernel32.GetConsoleMode(handle, ctypes.byref(mode))
    ctypes.windll.kernel32.SetConsoleMode(handle, mode.value | 0x0004)

# ── 依赖检查 ──────────────────────────────────────────────────────────────────
try:
    import paramiko
except ImportError:
    print("缺少 paramiko，请先运行: pip install paramiko")
    sys.exit(1)

# ── 服务器配置 ────────────────────────────────────────────────────────────────
HOST = "117.72.55.63"
PORT = 22
USER = "root"
PASS = "Xk7mN9pLv2R4xQw8"

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPORT_PATH = os.path.join(SCRIPT_DIR, "内存分析报告.md")

# ── ANSI 颜色 ─────────────────────────────────────────────────────────────────
RESET  = "\033[0m"
BOLD   = "\033[1m"
RED    = "\033[91m"
YELLOW = "\033[93m"
GREEN  = "\033[92m"
CYAN   = "\033[96m"
BLUE   = "\033[94m"
WHITE  = "\033[97m"
DIM    = "\033[2m"

# ── 已知可关闭的京东云 Agent（按 RSS 降序） ───────────────────────────────────
# 字段：comm 匹配子串、显示名、估算 RSS(MB)、关闭原因、风险级别、关闭命令
STOPPABLE_SERVICES = [
    {
        "match":   "openclaw-gatewa",
        "name":    "openclaw-gateway（京东云网络代理）",
        "rss_est": 510,
        "reason":  (
            "京东云内置 NAT 网关代理，负责 VPC 内网流量转发。"
            "对于不使用京东云 VPC 内网互联的单机 VPS，此服务完全不必要。"
            "停止后不影响公网 SSH / HTTP 访问，不影响本机出站 NAT，不影响任何自建服务。"
        ),
        "risk":    "低",
        # 注意：此服务是用户级 systemd（root 用户），需要 --user 和 XDG_RUNTIME_DIR
        "stop":    "XDG_RUNTIME_DIR=/run/user/0 systemctl --user stop openclaw-gateway",
        "disable": "XDG_RUNTIME_DIR=/run/user/0 systemctl --user disable openclaw-gateway",
    },
    {
        "match":   "jdog-kunlunmirr",
        "name":    "jdog-kunlunmirror（京东云镜像同步 Agent）",
        "rss_est": 59,
        "reason":  (
            "京东云镜像仓库同步 Agent，用于在多节点间同步容器镜像。"
            "单机 VPS 不使用京东云容器镜像服务时，此 Agent 无实际用途。"
            "停止后不影响 SSH、HTTP 及任何自建服务。"
            "实际由 jdog_service 托管（进程名为 jdog-watchdog + jdog-kunlunmirror + jdog-monitor）。"
        ),
        "risk":    "低",
        "stop":    "systemctl stop jdog_service",
        "disable": "systemctl disable jdog_service",
    },
    {
        "match":   "MonitorPlugin",
        "name":    "MonitorPlugin（京东云监控插件）",
        "rss_est": 20,
        "reason":  (
            "京东云控制台监控数据采集插件，向京东云上报 CPU/内存/磁盘指标。"
            "停止后京东云控制台监控图表将无数据，但机器本身完全正常运行。"
            "如不依赖京东云控制台告警，可安全停止。"
            "MonitorPlugin 是 JCSAgentCore 的子插件，由 jcs-agent-core 服务统一管理。"
        ),
        "risk":    "中（京东云控制台监控失效）",
        "stop":    "systemctl stop jcs-agent-core",
        "disable": "systemctl disable jcs-agent-core",
    },
    {
        "match":   "ifrit-agent",
        "name":    "ifrit-agent（京东云运维 Agent）",
        "rss_est": 15,
        "reason":  (
            "京东云运维控制 Agent，支持通过控制台执行远程命令、一键运维等功能。"
            "如果只通过 SSH 手动管理服务器，此 Agent 可停止。"
            "停止后无法使用京东云控制台的「远程连接」和「运维编排」功能。"
            "实际由 ifritd 服务托管（进程名为 ifrit-supervise + ifrit-agent）。"
        ),
        "risk":    "中（京东云控制台远程连接失效）",
        "stop":    "systemctl stop ifritd",
        "disable": "systemctl disable ifritd",
    },
    {
        "match":   "JCSAgentCore",
        "name":    "JCSAgentCore（京东云核心 Agent）",
        "rss_est": 14,
        "reason":  (
            "京东云实例核心管控 Agent，负责实例生命周期管理（重置密码、重装系统等）。"
            "停止后上述控制台操作将不可用，但日常运行不受影响。"
            "注：MonitorPlugin 也是此服务的子插件，stop jcs-agent-core 会同时停止两者。"
        ),
        "risk":    "中（京东云控制台实例管理部分失效）",
        "stop":    "# 已包含在 jcs-agent-core 中，无需单独停止",
        "disable": "# 已包含在 jcs-agent-core 中，无需单独 disable",
    },
]

# ─────────────────────────────────────────────────────────────────────────────
def run(client, cmd):
    _, stdout, stderr = client.exec_command(cmd, timeout=30)
    out = stdout.read().decode("utf-8", errors="replace")
    return out.strip()

def parse_meminfo(raw):
    info = {}
    for line in raw.splitlines():
        parts = line.split()
        if len(parts) >= 2:
            key = parts[0].rstrip(":")
            val = int(parts[1])   # kB
            info[key] = val
    return info

def parse_ps(raw):
    """返回 list of dict: pid, comm, rss_kb, vsz_kb, cmd"""
    procs = []
    for line in raw.splitlines()[1:]:   # 跳过标题
        parts = line.split(None, 4)
        if len(parts) < 4:
            continue
        try:
            procs.append({
                "pid":    parts[0],
                "comm":   parts[1],
                "rss_kb": int(parts[2]),
                "vsz_kb": int(parts[3]),
                "cmd":    parts[4] if len(parts) > 4 else "",
            })
        except ValueError:
            pass
    return procs

def kb_to_mb(kb):
    return kb / 1024

def fmt_mb(mb):
    return f"{mb:.1f} MB"

def bar(pct, width=20):
    filled = int(pct / 100 * width)
    filled = max(0, min(width, filled))
    if pct >= 80:
        color = RED
    elif pct >= 60:
        color = YELLOW
    else:
        color = GREEN
    return color + "#" * filled + DIM + "-" * (width - filled) + RESET

def risk_color(risk):
    if risk.startswith("低"):
        return GREEN
    if risk.startswith("中"):
        return YELLOW
    return RED

# ─────────────────────────────────────────────────────────────────────────────
def collect_data(client):
    data = {}
    print(f"  {DIM}读取 /proc/meminfo ...{RESET}")
    data["meminfo_raw"] = run(client, "cat /proc/meminfo")
    data["meminfo"] = parse_meminfo(data["meminfo_raw"])

    print(f"  {DIM}读取进程列表 ...{RESET}")
    data["ps_raw"] = run(client, "ps -eo pid,comm,rss,vsz,cmd --sort=-rss | head -30")
    data["procs"] = parse_ps(data["ps_raw"])

    print(f"  {DIM}读取 vmstat ...{RESET}")
    data["vmstat"] = run(client, "vmstat -s")

    print(f"  {DIM}读取 swap 信息 ...{RESET}")
    data["swap_info"] = run(client, "swapon --show 2>/dev/null || echo '(无 Swap)'")

    print(f"  {DIM}读取系统信息 ...{RESET}")
    data["hostname"] = run(client, "hostname")
    data["os_info"]  = run(client, "cat /etc/os-release | grep PRETTY_NAME | cut -d= -f2 | tr -d '\"'")
    data["uptime"]   = run(client, "uptime -p")

    return data

# ─────────────────────────────────────────────────────────────────────────────
def analyze(data):
    m = data["meminfo"]

    total_mb     = kb_to_mb(m.get("MemTotal", 0))
    free_mb      = kb_to_mb(m.get("MemFree", 0))
    avail_mb     = kb_to_mb(m.get("MemAvailable", 0))
    cached_mb    = kb_to_mb(m.get("Cached", 0) + m.get("Buffers", 0))
    slab_mb      = kb_to_mb(m.get("Slab", 0))
    slab_rec_mb  = kb_to_mb(m.get("SReclaimable", 0))
    slab_unr_mb  = kb_to_mb(m.get("SUnreclaim", 0))
    anon_mb      = kb_to_mb(m.get("AnonPages", 0))
    swap_total   = kb_to_mb(m.get("SwapTotal", 0))
    swap_free    = kb_to_mb(m.get("SwapFree", 0))
    swap_used    = swap_total - swap_free

    used_real_mb = total_mb - avail_mb   # 真实占用（不含可回收缓存）
    avail_pct    = avail_mb / total_mb * 100 if total_mb else 0
    used_pct     = used_real_mb / total_mb * 100 if total_mb else 0

    # 匹配可关闭服务的实际 RSS
    stoppable = []
    for svc in STOPPABLE_SERVICES:
        matched = [p for p in data["procs"] if svc["match"] in p["comm"]]
        actual_rss = sum(p["rss_kb"] for p in matched) / 1024 if matched else svc["rss_est"]
        stoppable.append({**svc, "actual_rss": actual_rss, "matched": matched})

    return {
        "total_mb": total_mb, "free_mb": free_mb, "avail_mb": avail_mb,
        "cached_mb": cached_mb, "slab_mb": slab_mb,
        "slab_rec_mb": slab_rec_mb, "slab_unr_mb": slab_unr_mb,
        "anon_mb": anon_mb,
        "swap_total": swap_total, "swap_used": swap_used,
        "used_real_mb": used_real_mb, "used_pct": used_pct,
        "avail_pct": avail_pct,
        "stoppable": stoppable,
    }

# ─────────────────────────────────────────────────────────────────────────────
def print_report(data, a):
    now = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    m   = data["meminfo"]

    print()
    print(f"{BOLD}{CYAN}{'='*60}{RESET}")
    print(f"{BOLD}{CYAN}  服务器内存分析报告{RESET}  {DIM}{now}{RESET}")
    print(f"{CYAN}{'='*60}{RESET}")
    print(f"  主机: {BOLD}{data['hostname']}{RESET}  ({HOST})")
    print(f"  系统: {data['os_info']}")
    print(f"  运行: {data['uptime']}")
    print()

    # ── 内存总览 ──────────────────────────────────────────────────────────────
    print(f"{BOLD}{WHITE}[ 一、内存总览 ]{RESET}")
    print(f"  总内存        : {BOLD}{fmt_mb(a['total_mb'])}{RESET}")
    print(f"  实际可用      : {GREEN if a['avail_pct']>30 else YELLOW if a['avail_pct']>15 else RED}"
          f"{fmt_mb(a['avail_mb'])} ({a['avail_pct']:.1f}%){RESET}"
          f"  {bar(100 - a['avail_pct'])}")
    print(f"  真实占用      : {fmt_mb(a['used_real_mb'])} ({a['used_pct']:.1f}%)")
    print(f"  页缓存+Buffers: {fmt_mb(a['cached_mb'])}  {DIM}(可被系统自动回收){RESET}")
    print(f"  Slab 内核缓存 : {fmt_mb(a['slab_mb'])}  {DIM}(可回收 {fmt_mb(a['slab_rec_mb'])} / 不可回收 {fmt_mb(a['slab_unr_mb'])}){RESET}")
    print(f"  匿名页(堆/栈) : {fmt_mb(a['anon_mb'])}")
    if a["swap_total"] > 0:
        print(f"  Swap          : {fmt_mb(a['swap_used'])} / {fmt_mb(a['swap_total'])} 已用")
    print()

    # ── 主要进程 ──────────────────────────────────────────────────────────────
    print(f"{BOLD}{WHITE}[ 二、内存占用 TOP 10 进程 ]{RESET}")
    print(f"  {'排名':>4}  {'PID':>7}  {'进程名':<20}  {'RSS':>9}  {'VSZ':>10}")
    print(f"  {'-'*4}  {'-'*7}  {'-'*20}  {'-'*9}  {'-'*10}")
    for i, p in enumerate(data["procs"][:10], 1):
        rss_mb = kb_to_mb(p["rss_kb"])
        vsz_mb = kb_to_mb(p["vsz_kb"])
        is_stoppable = any(s["match"] in p["comm"] for s in STOPPABLE_SERVICES)
        tag = f" {YELLOW}[可关]{RESET}" if is_stoppable else ""
        print(f"  {i:>4}  {p['pid']:>7}  {p['comm']:<20}  {fmt_mb(rss_mb):>9}  {fmt_mb(vsz_mb):>10}{tag}")
    print()

    # ── 可关闭服务分析 ────────────────────────────────────────────────────────
    print(f"{BOLD}{WHITE}[ 三、可关闭的京东云内置 Agent ]{RESET}")
    print(f"  {DIM}这些服务由京东云预装，与您自建的 Node.js/服务无关，均可安全停止。{RESET}")
    print()

    cumulative_freed = 0.0
    avail_now = a["avail_mb"]

    for idx, svc in enumerate(a["stoppable"], 1):
        rss = svc["actual_rss"]
        cumulative_freed += rss
        avail_after = avail_now + cumulative_freed
        rc = risk_color(svc["risk"])

        print(f"  {BOLD}{CYAN}▶ {idx}. {svc['name']}{RESET}")
        print(f"     实际 RSS  : {BOLD}{RED}{fmt_mb(rss)}{RESET}")
        print(f"     风险等级  : {rc}{svc['risk']}{RESET}")
        print(f"     为什么能关: {DIM}{svc['reason']}{RESET}")
        print(f"     停止命令  : {YELLOW}{svc['stop']}{RESET}")
        print(f"     永久禁用  : {YELLOW}{svc['disable']}{RESET}")
        print(f"     关闭后可用: 当前 {GREEN}{fmt_mb(avail_now)}{RESET} → "
              f"释放 {GREEN}+{fmt_mb(cumulative_freed)}{RESET} → "
              f"{BOLD}{GREEN}{fmt_mb(avail_after)}{RESET}  "
              f"{DIM}(若全部关闭){RESET}")
        print()

    total_freed = sum(s["actual_rss"] for s in a["stoppable"])
    final_avail = avail_now + total_freed
    print(f"  {BOLD}汇总：若关闭全部 {len(a['stoppable'])} 个 Agent，"
          f"可释放约 {GREEN}{fmt_mb(total_freed)}{RESET}{BOLD}，"
          f"可用内存从 {avail_now:.0f} MB → {GREEN}{final_avail:.0f} MB{RESET}")
    print()

    # ── 快速操作命令 ─────────────────────────────────────────────────────────
    print(f"{BOLD}{WHITE}[ 四、一键关闭命令（SSH 执行）]{RESET}")
    stop_all = " && ".join(s["stop"] for s in a["stoppable"])
    print(f"  {YELLOW}{stop_all}{RESET}")
    print()
    print(f"  {DIM}若要永久禁用（重启后不再启动），逐条执行 disable 命令。{RESET}")
    print(f"  {DIM}建议先只停止 openclaw-gateway 观察效果，再决定是否禁用其他 Agent。{RESET}")
    print()

    print(f"{CYAN}{'='*60}{RESET}")
    print(f"{DIM}  报告已保存至: {REPORT_PATH}{RESET}")
    print(f"{CYAN}{'='*60}{RESET}")
    print()

# ─────────────────────────────────────────────────────────────────────────────
def build_markdown(data, a):
    now = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    m   = data["meminfo"]
    lines = []

    lines.append(f"# 服务器内存分析报告\n")
    lines.append(f"生成时间：{now}")
    lines.append(f"服务器：`{data['hostname']}`（`{HOST}`）")
    lines.append(f"系统：{data['os_info']}")
    lines.append(f"运行时长：{data['uptime']}\n")
    lines.append("---\n")

    # 一、总览
    lines.append("## 一、内存总览\n")
    lines.append("| 项目 | 数值 | 说明 |")
    lines.append("|------|------|------|")
    lines.append(f"| 总内存 | {fmt_mb(a['total_mb'])} | 物理 RAM |")
    lines.append(f"| MemFree | {fmt_mb(a['free_mb'])} | 完全空闲，未被任何用途占用 |")
    lines.append(f"| **MemAvailable** | **{fmt_mb(a['avail_mb'])}** | **实际可用**（含可回收缓存），这才是真正剩余 |")
    lines.append(f"| 页缓存+Buffers | {fmt_mb(a['cached_mb'])} | 文件页缓存（可被回收） |")
    lines.append(f"| Slab | {fmt_mb(a['slab_mb'])} | 内核对象缓存（SReclaimable 部分可回收） |")
    lines.append(f"| SReclaimable | {fmt_mb(a['slab_rec_mb'])} | Slab 中可回收部分 |")
    lines.append(f"| SUnreclaim | {fmt_mb(a['slab_unr_mb'])} | Slab 中不可回收部分 |")
    lines.append(f"| 匿名页(AnonPages) | {fmt_mb(a['anon_mb'])} | 进程匿名映射（堆、栈）|")
    lines.append(f"| SwapTotal | {fmt_mb(a['swap_total'])} | Swap 总量 |")
    lines.append(f"| SwapUsed | {fmt_mb(a['swap_used'])} | Swap 已用 |")
    lines.append("")
    lines.append("> **说明**：`free -h` 显示的 used 包含 buffers/cache，")
    lines.append("> 看起来内存很满，实际可用（MemAvailable）才是有意义的剩余量。\n")
    lines.append("---\n")

    # 二、进程排名
    lines.append("## 二、进程内存排名（RSS 实际物理内存）\n")
    lines.append("| 排名 | PID | 进程名 | RSS | VSZ | 可关闭 |")
    lines.append("|------|-----|--------|-----|-----|--------|")
    for i, p in enumerate(data["procs"][:15], 1):
        rss_mb = kb_to_mb(p["rss_kb"])
        vsz_mb = kb_to_mb(p["vsz_kb"])
        stoppable_flag = "✅ 可关" if any(s["match"] in p["comm"] for s in STOPPABLE_SERVICES) else ""
        lines.append(f"| {i} | {p['pid']} | `{p['comm']}` | {fmt_mb(rss_mb)} | {fmt_mb(vsz_mb)} | {stoppable_flag} |")
    lines.append("")
    lines.append("> RSS = 实际驻留物理内存；VSZ = 虚拟地址空间大小（不代表真实占用）。\n")
    lines.append("---\n")

    # 三、可关闭服务
    lines.append("## 三、可关闭的京东云内置 Agent\n")
    lines.append("这些服务由京东云预装，与自建服务无关，均可安全停止。\n")

    avail_now = a["avail_mb"]
    cumulative = 0.0

    for idx, svc in enumerate(a["stoppable"], 1):
        rss = svc["actual_rss"]
        cumulative += rss
        avail_after = avail_now + cumulative

        lines.append(f"### {idx}. {svc['name']}\n")
        lines.append(f"| 项目 | 内容 |")
        lines.append(f"|------|------|")
        lines.append(f"| 实际 RSS | {fmt_mb(rss)} |")
        lines.append(f"| 风险等级 | {svc['risk']} |")
        lines.append(f"| 关闭原因 | {svc['reason']} |")
        lines.append(f"| 停止命令 | `{svc['stop']}` |")
        lines.append(f"| 永久禁用 | `{svc['disable']}` |")
        lines.append(f"| 关闭后可用 | 当前 {fmt_mb(avail_now)} → 释放 +{fmt_mb(cumulative)} → **{fmt_mb(avail_after)}**（若全部关闭）|")
        lines.append("")

    total_freed = sum(s["actual_rss"] for s in a["stoppable"])
    lines.append(f"**汇总**：关闭全部 {len(a['stoppable'])} 个 Agent，可释放约 **{fmt_mb(total_freed)}**，"
                 f"可用内存从 {avail_now:.0f} MB 提升至 **{avail_now + total_freed:.0f} MB**。\n")
    lines.append("---\n")

    # 四、一键命令
    lines.append("## 四、一键关闭命令\n")
    lines.append("```bash")
    for svc in a["stoppable"]:
        lines.append(svc["stop"])
    lines.append("```\n")
    lines.append("若要永久禁用（重启后不再启动）：\n")
    lines.append("```bash")
    for svc in a["stoppable"]:
        lines.append(svc["disable"])
    lines.append("```\n")
    lines.append("> 建议先只停止 `openclaw-gateway` 观察效果，再决定是否禁用其他 Agent。\n")
    lines.append("---\n")

    # 五、vmstat
    lines.append("## 五、vmstat 补充\n")
    lines.append("```")
    lines.append(data["vmstat"])
    lines.append("```\n")

    # 六、Swap
    lines.append("## 六、Swap 状态\n")
    lines.append("```")
    lines.append(data["swap_info"])
    lines.append("```\n")

    return "\n".join(lines)

# ─────────────────────────────────────────────────────────────────────────────
def ssh_connect():
    """
    用底层 Transport 建立连接，绕过 paramiko 在某些 OpenSSH 服务端的
    'No existing session' 问题（算法协商时序 bug）。
    """
    import socket
    sock = socket.create_connection((HOST, PORT), timeout=15)
    transport = paramiko.Transport(sock)
    transport.connect(username=USER, password=PASS)
    client = paramiko.SSHClient()
    client._transport = transport
    return client


def main():
    print(f"\n{BOLD}{CYAN}正在连接服务器 {HOST} ...{RESET}")
    try:
        client = ssh_connect()
    except Exception as e:
        print(f"{RED}连接失败: {e}{RESET}")
        sys.exit(1)
    print(f"{GREEN}连接成功{RESET}，开始采集数据...\n")

    try:
        data = collect_data(client)
    finally:
        client.close()

    print(f"{GREEN}数据采集完毕，开始分析...{RESET}\n")
    a = analyze(data)

    # 控制台报告
    print_report(data, a)

    # 保存 Markdown
    md = build_markdown(data, a)
    with open(REPORT_PATH, "w", encoding="utf-8") as f:
        f.write(md)
    print(f"{GREEN}Markdown 报告已保存: {REPORT_PATH}{RESET}\n")

if __name__ == "__main__":
    main()
