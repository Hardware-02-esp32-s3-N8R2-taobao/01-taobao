#!/usr/bin/env bash
set -euo pipefail

PORT="${PORT:-3000}"

echo "Port status:"
ss -ltnp "( sport = :$PORT )" 2>/dev/null || true

echo
echo "HTTP check:"
curl -fsS "http://127.0.0.1:${PORT}/api/sensor/latest" || echo "Service not responding"
