#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
USER_UNIT_DIR="$HOME/.config/systemd/user"

mkdir -p "$USER_UNIT_DIR"
cp "$SCRIPT_DIR/systemd-user/netdisplay.service" "$USER_UNIT_DIR/netdisplay.service"
cp "$SCRIPT_DIR/systemd-user/netdisplay-tunnel.service" "$USER_UNIT_DIR/netdisplay-tunnel.service"

systemctl --user daemon-reload
systemctl --user enable --now netdisplay.service

echo "Installed user service: netdisplay.service"
echo "To start the tunnel later, run:"
echo "  systemctl --user enable --now netdisplay-tunnel.service"
