#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="/home/zerozero/01-code/05-new-net-display"
URL_FILE="$PROJECT_DIR/public-url.txt"
LOG_FILE="$PROJECT_DIR/cloudflared-runtime.log"
NODE_PATH="$(command -v node || true)"
PATH_DIRS="/home/zerozero/.local/bin:/usr/local/bin:/usr/bin"
export PATH="$PATH_DIRS:$PATH"

find_cloudflared() {
  local candidate
  for candidate in \
    "${CLOUDFLARED_BIN:-}" \
    /home/zerozero/.local/bin/cloudflared \
    /usr/local/bin/cloudflared \
    /usr/bin/cloudflared; do
    if [[ -n "$candidate" && -x "$candidate" ]]; then
      printf '%s\n' "$candidate"
      return 0
    fi
  done
  command -v cloudflared 2>/dev/null || return 1
}

mkdir -p "$PROJECT_DIR"
: > "$LOG_FILE"

CLOUDFLARED_BIN="$(find_cloudflared || true)"

if [[ -z "$CLOUDFLARED_BIN" ]]; then
  echo "cloudflared not found" | tee -a "$LOG_FILE"
  exit 1
fi

"$CLOUDFLARED_BIN" tunnel --url http://localhost:3000 --protocol http2 --no-autoupdate 2>&1 |
  while IFS= read -r line; do
    printf '%s\n' "$line" | tee -a "$LOG_FILE"
    if [[ "$line" =~ https://[a-zA-Z0-9-]+\.trycloudflare\.com ]]; then
      printf '%s\n' "${BASH_REMATCH[0]}" > "$URL_FILE"
    fi
  done
