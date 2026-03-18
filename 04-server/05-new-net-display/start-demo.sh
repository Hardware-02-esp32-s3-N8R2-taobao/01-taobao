#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export PATH="/home/zerozero/.local/bin:$PATH"

cd "$SCRIPT_DIR"
exec node server.js
