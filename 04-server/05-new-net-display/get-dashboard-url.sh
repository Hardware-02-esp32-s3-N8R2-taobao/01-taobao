#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IP="$("$SCRIPT_DIR/get-local-ip.sh")"

if [[ -z "$IP" ]]; then
  echo "Unable to determine local IPv4 address" >&2
  exit 1
fi

printf 'http://%s:3000\n' "$IP"
