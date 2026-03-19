# 2026-03-19 Net Display Progress

## Scope

- Project root: `F:\01-dev-board\06-esp32s3\YD-ESP32-S3`
- Server project: `04-server/05-new-net-display`
- C3 project: `03-esp32-c3/03-project/02-wifi-mqtt-forward-demo`
- Internal server confirmed: `192.168.2.123`
- Internal server login confirmed on `2026-03-19 23:54:12`

## What Was Investigated

- Checked the front-end device-card click path for the device detail page.
- Checked the server history API path for `/api/sensor/history`.
- Compared public entry behavior with internal server behavior.
- Verified that the actual business server is the internal server and not the public forwarding host.

## Root Causes Found

- Chinese device IDs in the front-end route could fail when the hash value was encoded but not decoded.
- The original data model grouped multiple DHT11 nodes under one shared `flower` history stream.
- The page needed a stable English device ID for matching and routing, while the UI still needed Chinese display names.
- The internal server is the real service endpoint; the public server only forwards traffic.

## Local Code Changes Prepared

### Server-side

- Added per-device latest snapshot support in `04-server/05-new-net-display/server.js`.
- Added device-aware history filtering for `/api/sensor/history`.
- Added compatibility for multiple `device` query values so old Chinese IDs and new English IDs can coexist during migration.

### Front-end

- Updated `04-server/05-new-net-display/public/app.js` so device detail navigation opens directly instead of relying only on hash change behavior.
- Added route decoding and compatibility matching for old Chinese IDs and new English IDs.
- Added device mapping:
  - `yard-01`
  - `study-01`
  - `office-01`
  - `bedroom-01`
- Kept Chinese UI titles for display while allowing English IDs for routing and data binding.
- Added history queries that can target one logical device with both old and new IDs during transition.

### ESP32-C3

- Updated `03-esp32-c3/03-project/02-wifi-mqtt-forward-demo/main/app/device_profile.c`.
- Added mapping from user-facing Chinese device names to stable English `device` IDs and default aliases/sources.
- Current intended ID mapping:
  - `庭院1号` -> `yard-01`
  - `书房1号` -> `study-01`
  - `办公室1号` -> `office-01`
  - `卧室1号` -> `bedroom-01`

## Important Runtime Finding

- Internal server login was successful:
  - host: `192.168.2.123`
  - user: `zerozero`
  - project path: `/home/zerozero/01-code/05-new-net-display`

## Current Git Intent

- Commit only this session's directly related files plus this progress note.
- Do not include unrelated modified files unless explicitly requested later.

## Next Suggested Runtime Step

- Deploy the updated local `04-server/05-new-net-display` files to the internal server path `/home/zerozero/01-code/05-new-net-display`.
- Restart the internal server service there if needed.
- Then re-check `http://117.72.55.63/` through the public forwarding chain.
