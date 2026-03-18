#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="/home/zerozero/01-code/05-new-net-display"
URL_FILE="$PROJECT_DIR/public-url.txt"

if [[ -f "$URL_FILE" ]]; then
  cat "$URL_FILE"
  exit 0
fi

if systemctl --user --quiet is-enabled netdisplay-tunnel.service 2>/dev/null || systemctl --user --quiet is-active netdisplay-tunnel.service 2>/dev/null; then
  journalctl --user -u netdisplay-tunnel.service --no-pager |
    grep -o 'https://[a-zA-Z0-9-]\+\.trycloudflare\.com' |
    tail -n 1
  exit 0
fi

journalctl -u netdisplay-tunnel.service --no-pager 2>/dev/null |
  grep -o 'https://[a-zA-Z0-9-]\+\.trycloudflare\.com' |
  tail -n 1
