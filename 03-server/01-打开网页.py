from __future__ import annotations

import webbrowser
import urllib.request

PUBLIC_URL = "http://117.72.55.63/"


def check_online(url: str, timeout: int = 12) -> bool:
    try:
        with urllib.request.urlopen(url, timeout=timeout) as r:
            return r.status == 200
    except Exception:
        return False


def main() -> int:
    print(f"正在检测服务器: {PUBLIC_URL}")
    if check_online(PUBLIC_URL):
        print("服务在线，正在打开浏览器...")
        webbrowser.open(PUBLIC_URL)
    else:
        print("⚠ 服务器无响应！")
        print("  请运行  04-同步到服务器.py  检查并修复服务状态。")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
