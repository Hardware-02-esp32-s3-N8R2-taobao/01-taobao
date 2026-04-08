#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
06-完成提醒.py
通用本地提醒脚本：在 Windows 上弹出置顶消息框，并播放提示音。

用法：
  python 06-完成提醒.py --title "部署完成" --message "网页已经更新到服务器" --level success
"""

from __future__ import annotations

import argparse
import sys


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="显示本地完成提醒")
    parser.add_argument("--title", default="Codex 提醒", help="弹窗标题")
    parser.add_argument("--message", default="任务已完成", help="弹窗内容")
    parser.add_argument(
        "--level",
        choices=["info", "success", "warning", "error"],
        default="info",
        help="提醒级别"
    )
    return parser.parse_args()


def show_windows_message(title: str, message: str, level: str) -> int:
    import ctypes
    import winsound

    icon_map = {
        "info": 0x40,
        "success": 0x40,
        "warning": 0x30,
        "error": 0x10,
    }
    beep_map = {
        "info": winsound.MB_ICONASTERISK,
        "success": winsound.MB_OK,
        "warning": winsound.MB_ICONEXCLAMATION,
        "error": winsound.MB_ICONHAND,
    }

    winsound.MessageBeep(beep_map.get(level, winsound.MB_OK))
    flags = icon_map.get(level, 0x40) | 0x00001000
    return ctypes.windll.user32.MessageBoxW(None, message, title, flags)


def main() -> int:
    args = parse_args()
    if sys.platform == "win32":
        show_windows_message(args.title, args.message, args.level)
        return 0

    print(f"{args.title}: {args.message}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
