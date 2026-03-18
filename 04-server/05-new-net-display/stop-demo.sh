#!/usr/bin/env bash
set -euo pipefail

PORT="${PORT:-3000}"

PIDS="$(
  ss -ltnp "( sport = :$PORT )" 2>/dev/null |
    grep -o 'pid=[0-9]\+' |
    cut -d= -f2 |
    sort -u
)"

if [[ -z "${PIDS}" ]]; then
  echo "No process is listening on port $PORT"
  exit 0
fi

for pid in $PIDS; do
  kill "$pid"
  echo "Stopped process $pid on port $PORT"
done
