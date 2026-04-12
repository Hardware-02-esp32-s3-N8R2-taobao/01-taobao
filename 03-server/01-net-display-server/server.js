const http = require("http");
const crypto = require("crypto");
const fs = require("fs");
const os = require("os");
const path = require("path");
const net = require("net");
const zlib = require("zlib");
const { URL } = require("url");
const { DatabaseSync } = require("node:sqlite");
const aedes = require("aedes")();
const WebSocket = require("ws");
const { fetchRawSensorHistory, fetchSensorHistory, resolveAbsoluteWindow, sanitizeFileName, writeHistoryCsvFile } = require("./lib/history-export");
const { getWeatherForecast, invalidateWeatherCache } = require("./lib/weather");

const PORT = Number(process.env.PORT || 3000);
const MQTT_PORT = Number(process.env.MQTT_PORT || 1884);
const MQTT_WS_PORT = Number(process.env.MQTT_WS_PORT || 9001);
const RETENTION_DAYS = 90;
const GATEWAY_BROADCAST_INTERVAL_MS = 30 * 1000;
const SERVER_SAMPLE_INTERVAL_MS = 60 * 1000;   // 每分钟采样一次
const DEVICE_ONLINE_WINDOW_MS = 90 * 1000;
const SERVER_HISTORY_LIMIT = 1440;             // 保留 24 小时（24 × 60）
const DEVICE_PRESENCE_CONFIG = {
  "explorer-01": {
    lowPowerEnabled: false,
    reportIntervalSec: 300
  },
  "yard-01": {
    lowPowerEnabled: false,
    reportIntervalSec: 300
  },
  "study-01": {
    lowPowerEnabled: false,
    reportIntervalSec: 300
  }
};

const SENSOR_CONFIG = {
  dht11: {
    label: "DHT11 温湿度",
    metrics: [
      { key: "temperature", label: "温度", unit: "°C", color: "#f08b3e", axis: "left" },
      { key: "humidity", label: "湿度", unit: "%RH", color: "#38a9d9", axis: "right" }
    ]
  },
  ds18b20: {
    label: "DS18B20 温度",
    metrics: [{ key: "temperature", label: "温度", unit: "°C", color: "#20b77a", axis: "left" }]
  },
  bmp180: {
    label: "BMP180 气压温度",
    metrics: [
      { key: "temperature", label: "BMP 温度", unit: "°C", color: "#ff8b5c", axis: "left" },
      { key: "pressure", label: "气压", unit: "hPa", color: "#6f73ff", axis: "right" }
    ]
  },
  bmp280: {
    label: "BMP280 环境数据",
    metrics: [
      { key: "temperature", label: "BMP280 温度", unit: "°C", color: "#ff8b5c", axis: "left" },
      { key: "pressure", label: "气压", unit: "hPa", color: "#6f73ff", axis: "right" }
    ]
  },
  shtc3: {
    label: "SHTC3 温湿度",
    metrics: [
      { key: "temperature", label: "温度", unit: "°C", color: "#f08b3e", axis: "left" },
      { key: "humidity", label: "湿度", unit: "%RH", color: "#38a9d9", axis: "right" }
    ]
  },
  bh1750: {
    label: "BH1750 光照",
    metrics: [{ key: "illuminance", label: "光照", unit: "lux", color: "#f5b728", axis: "left" }]
  },
  wifi_signal: {
    label: "WiFi 信号强度",
    metrics: [{ key: "rssi", label: "信号强度", unit: "dBm", color: "#2f7cf6", axis: "left" }]
  },
  battery: {
    label: "电池电压",
    metrics: [
      { key: "voltage", label: "电压", unit: "V", color: "#4caf50", axis: "left" },
      { key: "percent", label: "电量", unit: "%", color: "#ff9800", axis: "right" }
    ]
  },
  max17043: {
    label: "MAX17043 电量计",
    metrics: [
      { key: "voltage", label: "电压", unit: "V", color: "#4caf50", axis: "left" },
      { key: "percent", label: "电量", unit: "%", color: "#ff9800", axis: "right" }
    ]
  },
  ina226: {
    label: "INA226 电流电压",
    metrics: [
      { key: "busVoltage", label: "电压", unit: "V", color: "#3f8cff", axis: "left" },
      { key: "currentMa", label: "电流", unit: "mA", color: "#ff8b5c", axis: "right" },
      { key: "powerMw", label: "功率", unit: "mW", color: "#7a68ff", axis: "right" }
    ]
  }
};

const MQTT_DEVICE_TOPIC_PREFIX = "device/";
const SENSOR_TYPE_TO_KEY = {
  dht11: "dht11",
  ds18b20: "ds18b20",
  bmp180: "bmp180",
  bmp280: "bmp280",
  shtc3: "shtc3",
  bh1750: "bh1750",
  wifi_signal: "wifi_signal",
  soil_moisture: "soil_moisture",
  battery: "battery",
  max17043: "max17043",
  ina226: "ina226"
};
const DEVICE_ID_ALIASES = {
  "explorer-01": "explorer-01",
  "explorer01": "explorer-01",
  "yard-01": "yard-01",
  "yard01": "yard-01",
  "study-01": "study-01",
  "study01": "study-01",
  "office-01": "office-01",
  "office01": "office-01",
  "bedroom-01": "bedroom-01",
  "bedroom01": "bedroom-01"
};
const DEVICE_ALIAS_BY_ID = {
  "explorer-01": "探索者 1 号",
  "yard-01": "庭院 1 号",
  "study-01": "书房 1 号",
  "office-01": "办公室 1 号",
  "bedroom-01": "卧室 1 号"
};
const DEVICE_OPTIONS = [
  { id: "explorer-01", name: "探索者1号", alias: "探索者 1 号" },
  { id: "yard-01", name: "庭院1号", alias: "庭院 1 号" },
  { id: "bedroom-01", name: "卧室1号", alias: "卧室 1 号" },
  { id: "study-01", name: "书房1号", alias: "书房 1 号" },
  { id: "office-01", name: "办公室1号", alias: "办公室 1 号" }
];
const SENSOR_TYPE_OPTIONS = [
  "dht11",
  "ds18b20",
  "bh1750",
  "bmp180",
  "shtc3",
  "soil_moisture",
  "battery",
  "max17043",
  "ina226"
];
const GATEWAY_PING_TOPIC = "garden/gateway/ping";
const GATEWAY_STATUS_PREFIX = "garden/gateway/status/";
const GATEWAY_BROADCAST_TOPIC = "garden/gateway/broadcast";
const YARD_PUMP_CONTROL_TOPIC = "garden/yard/pump/set";
const PUMP_CONTROL_PASSWORD = String(process.env.PUMP_CONTROL_PASSWORD || "1234");
const ADMIN_PASSWORD = String(process.env.ADMIN_PASSWORD || "1234");
const ADMIN_SESSION_TTL_MS = 24 * 60 * 60 * 1000;

const publicDir = path.join(__dirname, "public");
const dataDir = path.join(__dirname, "data");
const repoRootDataDir = path.resolve(__dirname, "..", "..", "data");
const dbPath = path.join(dataDir, "sensor-history.db");
const uploadsDir = path.join(publicDir, "uploads");
const roomUploadsDir = path.join(uploadsDir, "rooms");
const roomPhotoMetaPath = path.join(dataDir, "room-photos.json");
const sensorAliasMetaPath = path.join(dataDir, "device-sensor-aliases.json");
const firmwareDir = path.join(dataDir, "firmware");
const firmwareMetaPath = path.join(dataDir, "firmware-packages.json");
const otaJobsMetaPath = path.join(dataDir, "ota-jobs.json");
const deviceConfigMetaPath = path.join(dataDir, "device-runtime-configs.json");
const deviceConfigJobsMetaPath = path.join(dataDir, "device-config-jobs.json");
const FIRMWARE_PACKAGE_MAGIC = Buffer.from("YDFWPKG1", "utf8");
const DEFAULT_APP_FLASH_OFFSET = 0x10000;

if (!fs.existsSync(dataDir)) {
  fs.mkdirSync(dataDir, { recursive: true });
}
if (!fs.existsSync(repoRootDataDir)) {
  fs.mkdirSync(repoRootDataDir, { recursive: true });
}
if (!fs.existsSync(uploadsDir)) {
  fs.mkdirSync(uploadsDir, { recursive: true });
}
if (!fs.existsSync(roomUploadsDir)) {
  fs.mkdirSync(roomUploadsDir, { recursive: true });
}
if (!fs.existsSync(firmwareDir)) {
  fs.mkdirSync(firmwareDir, { recursive: true });
}

const db = new DatabaseSync(dbPath);
db.exec(`
  CREATE TABLE IF NOT EXISTS metric_readings (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    ts_ms INTEGER NOT NULL,
    recorded_at TEXT NOT NULL,
    recorded_day TEXT NOT NULL,
    sensor_key TEXT NOT NULL,
    metric_key TEXT NOT NULL,
    value REAL NOT NULL,
    unit TEXT NOT NULL,
    topic TEXT NOT NULL,
    source TEXT NOT NULL
  );
  CREATE INDEX IF NOT EXISTS idx_metric_ts ON metric_readings(ts_ms);
  CREATE INDEX IF NOT EXISTS idx_metric_sensor_metric ON metric_readings(sensor_key, metric_key, ts_ms);
  CREATE UNIQUE INDEX IF NOT EXISTS idx_metric_unique_point
  ON metric_readings(ts_ms, sensor_key, metric_key, source, topic);
`);

const insertMetricReadingStmt = db.prepare(`
  INSERT OR IGNORE INTO metric_readings
  (ts_ms, recorded_at, recorded_day, sensor_key, metric_key, value, unit, topic, source)
  VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
`);

const deleteOldMetricStmt = db.prepare(`
  DELETE FROM metric_readings
  WHERE ts_ms < ?
`);

const latestMetricRowsStmt = db.prepare(`
  SELECT sensor_key, metric_key, value, unit, source, topic, recorded_at
  FROM metric_readings
  WHERE id IN (
    SELECT MAX(id)
    FROM metric_readings
    GROUP BY sensor_key, metric_key
  )
`);

const latestMetricRowsByDeviceStmt = db.prepare(`
  SELECT sensor_key, metric_key, value, unit, source, topic, recorded_at
  FROM metric_readings
  WHERE id IN (
    SELECT MAX(id)
    FROM metric_readings
    GROUP BY source, sensor_key, metric_key
  )
`);

const mqttStatus = {
  port: MQTT_PORT,
  startedAt: new Date().toISOString(),
  lastMessageAt: null,
  lastTopic: null,
  lastClientId: null
};
const deviceRegistry = new Map();
const serverHistory = [];
let lastCpuSnapshot = os.cpus().map((cpu) => ({ ...cpu.times }));

function buildDefaultLatestSensors() {
  return {
    dht11: {
      label: SENSOR_CONFIG.dht11.label,
      temperature: null,
      humidity: null,
      source: "waiting-for-mqtt",
      topic: `${MQTT_DEVICE_TOPIC_PREFIX}dht11-01`,
      updatedAt: null
    },
    ds18b20: {
      label: SENSOR_CONFIG.ds18b20.label,
      temperature: null,
      source: "waiting-for-mqtt",
      topic: `${MQTT_DEVICE_TOPIC_PREFIX}ds18b20-01`,
      updatedAt: null
    },
    bmp180: {
      label: SENSOR_CONFIG.bmp180.label,
      temperature: null,
      pressure: null,
      pressureState: "等待气压值",
      source: "waiting-for-mqtt",
      topic: `${MQTT_DEVICE_TOPIC_PREFIX}bmp180-01`,
      updatedAt: null
    },
    bmp280: {
      label: SENSOR_CONFIG.bmp280.label,
      temperature: null,
      pressure: null,
      pressureState: "等待气压值",
      source: "waiting-for-mqtt",
      topic: `${MQTT_DEVICE_TOPIC_PREFIX}bmp280-01`,
      updatedAt: null
    },
    shtc3: {
      label: SENSOR_CONFIG.shtc3.label,
      temperature: null,
      humidity: null,
      source: "waiting-for-mqtt",
      topic: `${MQTT_DEVICE_TOPIC_PREFIX}shtc3-01`,
      updatedAt: null
    },
    bh1750: {
      label: SENSOR_CONFIG.bh1750.label,
      illuminance: null,
      source: "waiting-for-mqtt",
      topic: `${MQTT_DEVICE_TOPIC_PREFIX}bh1750-01`,
      updatedAt: null
    },
    wifi_signal: {
      label: SENSOR_CONFIG.wifi_signal.label,
      rssi: null,
      source: "waiting-for-mqtt",
      topic: `${MQTT_DEVICE_TOPIC_PREFIX}wifi-signal-01`,
      updatedAt: null
    },
    battery: {
      label: SENSOR_CONFIG.battery.label,
      voltage: null,
      percent: null,
      source: "waiting-for-mqtt",
      topic: `${MQTT_DEVICE_TOPIC_PREFIX}battery-01`,
      updatedAt: null
    },
    max17043: {
      label: SENSOR_CONFIG.max17043.label,
      voltage: null,
      percent: null,
      source: "waiting-for-mqtt",
      topic: `${MQTT_DEVICE_TOPIC_PREFIX}max17043-01`,
      updatedAt: null
    },
    ina226: {
      label: SENSOR_CONFIG.ina226.label,
      busVoltage: null,
      currentMa: null,
      powerMw: null,
      source: "waiting-for-mqtt",
      topic: `${MQTT_DEVICE_TOPIC_PREFIX}ina226-01`,
      updatedAt: null
    }
  };
}

function createDefaultSensorState(sensorKey) {
  return { ...buildDefaultLatestSensors()[sensorKey] };
}

function canonicalizeDeviceId(rawValue) {
  const raw = String(rawValue || "").trim();
  if (!raw) {
    return "mqtt-client";
  }
  const normalized = raw.toLowerCase().replace(/\s+/g, "");
  return DEVICE_ID_ALIASES[raw] || DEVICE_ID_ALIASES[normalized] || raw;
}

function getDeviceAlias(deviceId, payloadAlias) {
  return String(payloadAlias || DEVICE_ALIAS_BY_ID[deviceId] || deviceId);
}

let latestSensors = loadLatestSensors();
let latestSensorsByDevice = loadLatestSensorsByDevice();

// Shared gzip helper: returns compressed Buffer or null if client doesn't accept gzip
function tryGzip(content, acceptEncoding) {
  if (!acceptEncoding.includes("gzip")) return null;
  return zlib.gzipSync(content);
}

// In-memory cache for compressed static files — avoids re-compressing on every request
const gzipFileCache = new Map();
const ROOM_IDS = ["all", "explorer", "yard", "study", "office", "bedroom", "server", "outdoor"];
const ROOM_PHOTO_SIZE_LIMIT = 8 * 1024 * 1024;

function readJsonFile(filePath, fallback) {
  try {
    return JSON.parse(fs.readFileSync(filePath, "utf8"));
  } catch (_error) {
    return fallback;
  }
}

function writeJsonFile(filePath, data) {
  fs.writeFileSync(filePath, JSON.stringify(data, null, 2) + "\n", "utf8");
}

function loadFirmwarePackages() {
  const raw = readJsonFile(firmwareMetaPath, []);
  return Array.isArray(raw) ? raw : [];
}

function saveFirmwarePackages(packages) {
  writeJsonFile(firmwareMetaPath, packages);
}

function loadOtaJobs() {
  const raw = readJsonFile(otaJobsMetaPath, []);
  return Array.isArray(raw) ? raw : [];
}

function saveOtaJobs(jobs) {
  writeJsonFile(otaJobsMetaPath, jobs);
}

function loadDeviceRuntimeConfigs() {
  const raw = readJsonFile(deviceConfigMetaPath, {});
  return raw && typeof raw === "object" ? raw : {};
}

function saveDeviceRuntimeConfigs(configs) {
  writeJsonFile(deviceConfigMetaPath, configs);
}

function loadDeviceConfigJobs() {
  const raw = readJsonFile(deviceConfigJobsMetaPath, []);
  return Array.isArray(raw) ? raw : [];
}

function saveDeviceConfigJobs(jobs) {
  writeJsonFile(deviceConfigJobsMetaPath, jobs);
}

function loadRoomPhotoMap() {
  const raw = readJsonFile(roomPhotoMetaPath, {});
  if (!raw || typeof raw !== "object") {
    return {};
  }
  return raw;
}

function saveRoomPhotoMap(roomPhotos) {
  writeJsonFile(roomPhotoMetaPath, roomPhotos);
}

function loadDeviceSensorAliasMap() {
  const raw = readJsonFile(sensorAliasMetaPath, {});
  if (!raw || typeof raw !== "object") {
    return {};
  }
  return raw;
}

function saveDeviceSensorAliasMap(sensorAliases) {
  writeJsonFile(sensorAliasMetaPath, sensorAliases);
}

let deviceSensorAliases = loadDeviceSensorAliasMap();
let firmwarePackages = loadFirmwarePackages();
let otaJobs = loadOtaJobs();
let deviceRuntimeConfigs = loadDeviceRuntimeConfigs();
let deviceConfigJobs = loadDeviceConfigJobs();
const adminSessions = new Map();

function createAdminSession() {
  const token = crypto.randomBytes(24).toString("hex");
  adminSessions.set(token, {
    token,
    createdAt: Date.now(),
    expiresAt: Date.now() + ADMIN_SESSION_TTL_MS
  });
  return token;
}

function purgeExpiredAdminSessions() {
  const now = Date.now();
  for (const [token, session] of adminSessions.entries()) {
    if (!session || session.expiresAt <= now) {
      adminSessions.delete(token);
    }
  }
}

function parseCookies(req) {
  const raw = String(req?.headers?.cookie || "");
  return raw.split(";").reduce((acc, item) => {
    const [key, ...rest] = item.split("=");
    if (!key || rest.length === 0) {
      return acc;
    }
    acc[key.trim()] = decodeURIComponent(rest.join("=").trim());
    return acc;
  }, {});
}

function isAdminAuthenticated(req) {
  purgeExpiredAdminSessions();
  const token = parseCookies(req).admin_session;
  if (!token) {
    return false;
  }
  const session = adminSessions.get(token);
  if (!session || session.expiresAt <= Date.now()) {
    adminSessions.delete(token);
    return false;
  }
  session.expiresAt = Date.now() + ADMIN_SESSION_TTL_MS;
  return true;
}

function requireAdmin(req, res) {
  if (isAdminAuthenticated(req)) {
    return true;
  }
  sendJson(res, 401, { message: "admin login required" });
  return false;
}

function getRequestBaseUrl(req) {
  const host = String(req?.headers?.host || `127.0.0.1:${PORT}`).trim();
  return `http://${host}`;
}

function sanitizeFirmwareText(value, maxLength = 120) {
  return String(value || "")
    .replace(/\s+/g, " ")
    .trim()
    .slice(0, maxLength);
}

function inferFirmwareVersionFromFileName(fileName) {
  const raw = sanitizeFirmwareText(fileName || "", 120);
  if (!raw) {
    return "";
  }
  const match = raw.match(/(?:_v|[-_])(\d+\.\d+\.\d+)\.bin$/i) || raw.match(/(\d+\.\d+\.\d+)/);
  return match?.[1] || "";
}

function clampOtaProgress(value, fallback = 0) {
  const numeric = Number(value);
  if (!Number.isFinite(numeric)) {
    return Math.max(0, Math.min(100, Number(fallback) || 0));
  }
  return Math.max(0, Math.min(100, numeric));
}

function deriveOtaProgressPercent(status, existingProgress = 0, explicitProgress = null) {
  if (explicitProgress != null && explicitProgress !== "") {
    return clampOtaProgress(explicitProgress, existingProgress);
  }

  const fallback = clampOtaProgress(existingProgress, 0);
  switch (String(status || "").trim()) {
    case "pending":
      return Math.max(fallback, 5);
    case "downloading":
      return Math.max(fallback, 15);
    case "rebooting":
      return Math.max(fallback, 95);
    case "success":
      return 100;
    case "rolled_back":
    case "failed":
    case "replaced":
      return fallback;
    default:
      return fallback;
  }
}

function decodeFirmwarePayload(payload) {
  const rawBase64 = String(payload.dataBase64 || "").trim();
  const dataUrl = String(payload.dataUrl || "").trim();
  let base64 = rawBase64;
  if (!base64 && dataUrl.startsWith("data:application/octet-stream;base64,")) {
    base64 = dataUrl.split(",")[1] || "";
  }
  if (!base64) {
    throw new Error("firmware data is required");
  }
  const buffer = Buffer.from(base64, "base64");
  if (!buffer.length) {
    throw new Error("empty firmware payload");
  }
  return buffer;
}

function extractFirmwareEmbeddedMetadata(buffer) {
  if (!Buffer.isBuffer(buffer) || !buffer.length) {
    return { version: "", notes: "" };
  }

  const head = buffer.subarray(0, Math.min(buffer.length, 64 * 1024));
  const marker = Buffer.from("YDOTA_META:", "utf8");
  const markerIndex = head.indexOf(marker);
  if (markerIndex < 0) {
    return { version: "", notes: "" };
  }

  let endIndex = head.indexOf(0, markerIndex);
  if (endIndex < 0) {
    endIndex = Math.min(head.length, markerIndex + 512);
  }
  const raw = head.subarray(markerIndex + marker.length, endIndex).toString("utf8").trim();
  if (!raw) {
    return { version: "", notes: "" };
  }

  const [versionRaw, ...noteParts] = raw.split("|");
  return {
    version: sanitizeFirmwareText(versionRaw || "", 32),
    notes: sanitizeFirmwareText(noteParts.join("|") || "", 240)
  };
}

function parseFirmwarePackage(buffer, sourceName = "firmware.bin") {
  if (!Buffer.isBuffer(buffer) || !buffer.length) {
    throw new Error("empty firmware payload");
  }

  if (!buffer.subarray(0, FIRMWARE_PACKAGE_MAGIC.length).equals(FIRMWARE_PACKAGE_MAGIC)) {
    const embedded = extractFirmwareEmbeddedMetadata(buffer);
    return {
      packageFormat: "raw-app-bin",
      schema: 0,
      version: sanitizeFirmwareText(embedded.version || inferFirmwareVersionFromFileName(sourceName), 32),
      notes: sanitizeFirmwareText(embedded.notes || "", 240),
      chip: "esp32c3",
      flashSettings: {},
      otaSegmentRole: "app",
      supportsFullFlash: false,
      segments: [
        {
          name: "app",
          role: "app",
          flashOffset: DEFAULT_APP_FLASH_OFFSET,
          fileOffset: 0,
          size: buffer.length,
          sha256: crypto.createHash("sha256").update(buffer).digest("hex")
        }
      ]
    };
  }

  if (buffer.length < FIRMWARE_PACKAGE_MAGIC.length + 4) {
    throw new Error("firmware package header is incomplete");
  }

  const manifestSize = buffer.readUInt32LE(FIRMWARE_PACKAGE_MAGIC.length);
  const manifestStart = FIRMWARE_PACKAGE_MAGIC.length + 4;
  const manifestEnd = manifestStart + manifestSize;
  if (manifestEnd > buffer.length) {
    throw new Error("firmware package manifest is truncated");
  }

  let manifest;
  try {
    manifest = JSON.parse(buffer.subarray(manifestStart, manifestEnd).toString("utf8"));
  } catch (_error) {
    throw new Error("firmware package manifest is invalid");
  }

  const payloadBase = manifestEnd;
  const segments = Array.isArray(manifest.segments) ? manifest.segments.map((entry) => {
    const payloadOffset = Number(entry.payloadOffset || 0);
    const size = Number(entry.size || 0);
    const fileOffset = payloadBase + payloadOffset;
    const fileEnd = fileOffset + size;
    if (!Number.isFinite(fileOffset) || !Number.isFinite(size) || fileOffset < payloadBase || fileEnd > buffer.length) {
      throw new Error(`segment out of range: ${entry.name || entry.role || "segment"}`);
    }
    const segmentBuffer = buffer.subarray(fileOffset, fileEnd);
    return {
      name: String(entry.name || entry.role || "segment"),
      role: String(entry.role || entry.name || "segment"),
      flashOffset: Number(entry.flashOffset || 0),
      fileOffset,
      size,
      sha256: String(entry.sha256 || crypto.createHash("sha256").update(segmentBuffer).digest("hex"))
    };
  }) : [];

  const roleSet = new Set(segments.map((segment) => String(segment.role || "").toLowerCase()));
  return {
    packageFormat: String(manifest.format || "yd-esp32-firmware-package"),
    schema: Number(manifest.schema || 1),
    version: sanitizeFirmwareText(manifest.packageVersion || "", 32),
    notes: sanitizeFirmwareText(manifest.releaseNotes || "", 240),
    chip: sanitizeFirmwareText(manifest.chip || "esp32c3", 32),
    flashSettings: manifest.flashSettings && typeof manifest.flashSettings === "object" ? manifest.flashSettings : {},
    otaSegmentRole: sanitizeFirmwareText(manifest.otaSegmentRole || "app", 32) || "app",
    supportsFullFlash: roleSet.has("bootloader") && roleSet.has("partition-table") && roleSet.has("app"),
    segments
  };
}

function getFirmwareOtaSegment(parsedPackage, buffer) {
  const otaRole = String(parsedPackage?.otaSegmentRole || "app").trim().toLowerCase();
  const segment = (parsedPackage?.segments || []).find((item) => String(item.role || "").toLowerCase() === otaRole)
    || (parsedPackage?.segments || []).find((item) => String(item.name || "").toLowerCase() === otaRole);
  if (!segment) {
    throw new Error(`ota segment missing: ${otaRole}`);
  }
  const fileOffset = Number(segment.fileOffset || 0);
  const fileEnd = fileOffset + Number(segment.size || 0);
  if (!Number.isFinite(fileOffset) || !Number.isFinite(fileEnd) || fileOffset < 0 || fileEnd > buffer.length) {
    throw new Error(`ota segment out of range: ${segment.name || segment.role || otaRole}`);
  }
  return {
    ...segment,
    data: buffer.subarray(fileOffset, fileEnd)
  };
}

function saveFirmwarePackage(payload) {
  const requestedVersion = sanitizeFirmwareText(payload.version || "", 32);
  const originalFileName = sanitizeFirmwareText(payload.fileName || "firmware.bin", 120) || "firmware.bin";
  const buffer = decodeFirmwarePayload(payload);
  const parsedPackage = parseFirmwarePackage(buffer, originalFileName);
  const embeddedVersion = sanitizeFirmwareText(parsedPackage.version || "", 32);
  const inferredVersion = inferFirmwareVersionFromFileName(originalFileName);
  if (requestedVersion && inferredVersion && requestedVersion !== inferredVersion) {
    throw new Error(`firmware version mismatch: input=${requestedVersion}, file=${inferredVersion}`);
  }
  if (requestedVersion && embeddedVersion && requestedVersion !== embeddedVersion) {
    throw new Error(`firmware version mismatch: input=${requestedVersion}, embedded=${embeddedVersion}`);
  }
  if (inferredVersion && embeddedVersion && inferredVersion !== embeddedVersion) {
    throw new Error(`firmware version mismatch: file=${inferredVersion}, embedded=${embeddedVersion}`);
  }
  const version = embeddedVersion || inferredVersion || requestedVersion;
  if (!version) {
    throw new Error("version is required");
  }

  const notes = sanitizeFirmwareText(payload.notes || parsedPackage.notes || "", 240);
  const ext = path.extname(originalFileName || "").toLowerCase() || ".bin";
  const firmwareId = `fw_${Date.now()}_${crypto.randomBytes(4).toString("hex")}`;
  const storedFileName = `${firmwareId}${ext === ".bin" ? ".bin" : ext}`;
  const absolutePath = path.join(firmwareDir, storedFileName);
  const sha256 = crypto.createHash("sha256").update(buffer).digest("hex");
  const otaSegment = getFirmwareOtaSegment(parsedPackage, buffer);

  fs.writeFileSync(absolutePath, buffer);

  const entry = {
    id: firmwareId,
    version,
    notes,
    originalFileName,
    storedFileName,
    size: buffer.length,
    sha256,
    packageFormat: parsedPackage.packageFormat,
    schema: parsedPackage.schema,
    chip: parsedPackage.chip,
    flashSettings: parsedPackage.flashSettings,
    otaSegmentRole: parsedPackage.otaSegmentRole,
    otaSize: otaSegment.size,
    otaSha256: otaSegment.sha256,
    supportsFullFlash: Boolean(parsedPackage.supportsFullFlash),
    segments: (parsedPackage.segments || []).map((segment) => ({
      name: segment.name,
      role: segment.role,
      flashOffset: segment.flashOffset,
      size: segment.size,
      sha256: segment.sha256
    })),
    uploadedAt: new Date().toISOString()
  };

  firmwarePackages = [entry, ...firmwarePackages.filter((item) => item.id !== firmwareId)];
  saveFirmwarePackages(firmwarePackages);
  return entry;
}

function getFirmwarePackage(firmwareId) {
  return firmwarePackages.find((item) => item.id === firmwareId) || null;
}

function listFirmwarePackages() {
  return [...firmwarePackages].sort((a, b) => String(b.uploadedAt || "").localeCompare(String(a.uploadedAt || "")));
}

function markPreviousJobsReplaced(deviceId) {
  otaJobs = otaJobs.map((job) => {
    if (job.deviceId !== deviceId) {
      return job;
    }
    if (!["pending", "downloading", "rebooting"].includes(job.status)) {
      return job;
    }
    return {
      ...job,
      status: "replaced",
      finishedAt: new Date().toISOString(),
      message: "replaced by a newer ota job"
    };
  });
}

function createOtaJob(payload) {
  const deviceId = canonicalizeDeviceId(payload.deviceId || "");
  if (!DEVICE_ALIAS_BY_ID[deviceId]) {
    throw new Error("invalid deviceId");
  }

  const firmware = getFirmwarePackage(payload.firmwareId);
  if (!firmware) {
    throw new Error("firmware package not found");
  }

  markPreviousJobsReplaced(deviceId);

  const job = {
    id: `ota_${Date.now()}_${crypto.randomBytes(4).toString("hex")}`,
    deviceId,
    firmwareId: firmware.id,
    targetVersion: firmware.version,
    status: "pending",
    force: Boolean(payload.force),
    createdAt: new Date().toISOString(),
    startedAt: null,
    finishedAt: null,
    progressPercent: 0,
    message: sanitizeFirmwareText(payload.message || "waiting for device wakeup", 240),
    token: crypto.randomBytes(18).toString("hex"),
    lastReportedVersion: null,
    history: []
  };

  otaJobs = [job, ...otaJobs];
  saveOtaJobs(otaJobs);
  return job;
}

function getOtaJobsForDevice(deviceId) {
  return otaJobs
    .filter((job) => job.deviceId === deviceId)
    .sort((a, b) => String(b.createdAt || "").localeCompare(String(a.createdAt || "")));
}

function getLatestOtaJobForDevice(deviceId) {
  return getOtaJobsForDevice(deviceId)[0] || null;
}

function getActiveOtaJobForDevice(deviceId, currentVersion = "") {
  return getOtaJobsForDevice(deviceId).find((job) => {
    if (!["pending", "downloading", "rebooting"].includes(job.status)) {
      return false;
    }
    if (!job.force && currentVersion && job.targetVersion === currentVersion) {
      return false;
    }
    return Boolean(getFirmwarePackage(job.firmwareId));
  }) || null;
}

function getDeviceOptionById(deviceId) {
  return DEVICE_OPTIONS.find((item) => item.id === deviceId) || DEVICE_OPTIONS[0];
}

function normalizeDeviceName(deviceId, requestedName = "") {
  const matched = DEVICE_OPTIONS.find((item) => item.name === String(requestedName || "").trim());
  return matched || getDeviceOptionById(deviceId);
}

function normalizeSensorList(sensorValues) {
  if (!Array.isArray(sensorValues)) {
    return [];
  }
  const sensors = [];
  const seen = new Set();
  sensorValues.forEach((item) => {
    const sensorKey = canonicalizeSensorKey(item);
    if (!sensorKey || !SENSOR_TYPE_OPTIONS.includes(sensorKey) || seen.has(sensorKey)) {
      return;
    }
    seen.add(sensorKey);
    sensors.push(sensorKey);
  });
  return sensors;
}

function normalizeLowPowerPayload(payload, fallback = {}) {
  const enabled = Boolean(payload?.enabled ?? fallback.enabled ?? false);
  const intervalCandidate = Number(payload?.intervalSec ?? payload?.interval_sec ?? fallback.intervalSec ?? fallback.interval_sec ?? 300);
  const intervalSec = Number.isFinite(intervalCandidate)
    ? Math.max(10, Math.min(86400, Math.round(intervalCandidate)))
    : 300;
  return {
    enabled,
    intervalSec
  };
}

function getDefaultDeviceRuntimeConfig(deviceId) {
  const option = getDeviceOptionById(deviceId);
  const presenceConfig = DEVICE_PRESENCE_CONFIG[deviceId] || {};
  const remembered = deviceRegistry.get(deviceId) || null;
  return {
    deviceId,
    deviceName: option.name,
    deviceAlias: option.alias,
    sensors: normalizeSensorList(remembered?.sensors || []),
    lowPower: normalizeLowPowerPayload({
      enabled: presenceConfig.lowPowerEnabled,
      intervalSec: presenceConfig.reportIntervalSec || 300
    }),
    updatedAt: remembered?.lastSeenAt || null,
    source: "inferred"
  };
}

function getDeviceRuntimeConfig(deviceId) {
  const saved = deviceRuntimeConfigs[deviceId];
  if (saved && typeof saved === "object") {
    const option = normalizeDeviceName(deviceId, saved.deviceName);
    return {
      deviceId,
      deviceName: option.name,
      deviceAlias: option.alias,
      sensors: normalizeSensorList(saved.sensors || []),
      lowPower: normalizeLowPowerPayload(saved.lowPower, getDefaultDeviceRuntimeConfig(deviceId).lowPower),
      updatedAt: saved.updatedAt || null,
      source: saved.source || "device-report"
    };
  }
  return getDefaultDeviceRuntimeConfig(deviceId);
}

function saveDeviceRuntimeConfig(deviceId, payload, source = "device-report", fwVersion = null) {
  const option = normalizeDeviceName(deviceId, payload?.deviceName);
  const config = {
    deviceId,
    deviceName: option.name,
    deviceAlias: option.alias,
    sensors: normalizeSensorList(payload?.sensors || []),
    lowPower: normalizeLowPowerPayload(payload?.lowPower || {}),
    updatedAt: new Date().toISOString(),
    source
  };
  deviceRuntimeConfigs[deviceId] = config;
  saveDeviceRuntimeConfigs(deviceRuntimeConfigs);
  if (source !== "web-desired") {
    rememberDevice(deviceId, {
      alias: config.deviceAlias,
      source: source === "device-report" ? "device-config-report" : "device-config",
      sensorKeys: config.sensors,
      fwVersion: sanitizeFirmwareText(fwVersion || "", 32),
      lowPowerEnabled: config.lowPower.enabled,
      reportIntervalSec: config.lowPower.intervalSec
    });
  }
  return config;
}

function markPreviousConfigJobsReplaced(deviceId) {
  deviceConfigJobs = deviceConfigJobs.map((job) => {
    if (job.deviceId !== deviceId || job.status !== "pending") {
      return job;
    }
    return {
      ...job,
      status: "replaced",
      finishedAt: new Date().toISOString(),
      message: "replaced by a newer config job"
    };
  });
}

function createDeviceConfigJob(payload) {
  const deviceId = canonicalizeDeviceId(payload.deviceId || "");
  if (!DEVICE_ALIAS_BY_ID[deviceId]) {
    throw new Error("invalid deviceId");
  }

  const deviceOption = normalizeDeviceName(deviceId, payload.deviceName);
  const sensors = normalizeSensorList(payload.sensors || []);
  const lowPower = normalizeLowPowerPayload(payload.lowPower || {}, getDeviceRuntimeConfig(deviceId).lowPower);

  markPreviousConfigJobsReplaced(deviceId);

  const job = {
    id: `cfg_${Date.now()}_${crypto.randomBytes(4).toString("hex")}`,
    deviceId,
    status: "pending",
    config: {
      deviceId,
      deviceName: deviceOption.name,
      deviceAlias: deviceOption.alias,
      sensors
    },
    lowPower,
    message: sanitizeFirmwareText(payload.message || "web admin config push", 240),
    createdAt: new Date().toISOString(),
    finishedAt: null,
    history: []
  };

  saveDeviceRuntimeConfig(deviceId, {
    deviceName: deviceOption.name,
    sensors,
    lowPower
  }, "web-desired");

  deviceConfigJobs = [job, ...deviceConfigJobs];
  saveDeviceConfigJobs(deviceConfigJobs);
  return job;
}

function getDeviceConfigJobsForDevice(deviceId) {
  return deviceConfigJobs
    .filter((job) => job.deviceId === deviceId)
    .sort((a, b) => String(b.createdAt || "").localeCompare(String(a.createdAt || "")));
}

function getActiveDeviceConfigJobForDevice(deviceId) {
  return getDeviceConfigJobsForDevice(deviceId).find((job) => job.status === "pending") || null;
}

function updateDeviceConfigJobStatus(payload) {
  const reportedDeviceId = canonicalizeDeviceId(payload.deviceId || "");
  const jobId = sanitizeFirmwareText(payload.jobId || "", 80);
  const status = sanitizeFirmwareText(payload.status || "", 40);
  const message = sanitizeFirmwareText(payload.message || "", 240);
  if (!reportedDeviceId || !jobId || !status) {
    throw new Error("deviceId, jobId and status are required");
  }

  const index = deviceConfigJobs.findIndex((job) => job.id === jobId);
  if (index < 0) {
    throw new Error("config job not found");
  }

  const existing = deviceConfigJobs[index];
  const finished = ["success", "failed", "replaced"].includes(status);
  const nextJob = {
    ...existing,
    status,
    message: message || existing.message,
    finishedAt: finished ? new Date().toISOString() : null,
    history: [
      ...(existing.history || []),
      {
        status,
        message,
        at: new Date().toISOString()
      }
    ].slice(-20)
  };

  if ((payload.config && typeof payload.config === "object") || (payload.lowPower && typeof payload.lowPower === "object")) {
    const targetDeviceId = canonicalizeDeviceId(payload.config?.deviceId || existing.config?.deviceId || reportedDeviceId);
    const runtimeConfig = saveDeviceRuntimeConfig(targetDeviceId, {
      ...(payload.config || existing.config || {}),
      lowPower: payload.lowPower || existing.lowPower || {}
    }, "device-report", payload.fwVersion);
    nextJob.appliedConfig = runtimeConfig;
  }

  deviceConfigJobs[index] = nextJob;
  saveDeviceConfigJobs(deviceConfigJobs);
  return nextJob;
}

function getDeviceAdminSummary(deviceId) {
  return {
    options: {
      deviceNames: DEVICE_OPTIONS.map((item) => item.name),
      sensorTypes: SENSOR_TYPE_OPTIONS,
      lowPower: {
        minIntervalSec: 10,
        maxIntervalSec: 86400
      }
    },
    currentConfig: getDeviceRuntimeConfig(deviceId),
    activeConfigJob: getActiveDeviceConfigJobForDevice(deviceId),
    configJobs: getDeviceConfigJobsForDevice(deviceId).slice(0, 10),
    ...getDeviceAdminOtaSummary(deviceId)
  };
}

function updateOtaJobStatus(payload) {
  const deviceId = canonicalizeDeviceId(payload.deviceId || "");
  const jobId = sanitizeFirmwareText(payload.jobId || "", 80);
  const status = sanitizeFirmwareText(payload.status || "", 40);
  const message = sanitizeFirmwareText(payload.message || "", 240);
  const fwVersion = sanitizeFirmwareText(payload.fwVersion || "", 32);
  const progressPercent = payload.progressPercent;
  if (!deviceId || !jobId || !status) {
    throw new Error("deviceId, jobId and status are required");
  }

  const index = otaJobs.findIndex((job) => job.id === jobId && job.deviceId === deviceId);
  if (index < 0) {
    throw new Error("ota job not found");
  }

  const existing = otaJobs[index];
  const nowIso = new Date().toISOString();
  const nextProgress = deriveOtaProgressPercent(status, existing.progressPercent, progressPercent);
  const next = {
    ...existing,
    status,
    message: message || existing.message,
    lastReportedVersion: fwVersion || existing.lastReportedVersion,
    progressPercent: nextProgress,
    startedAt: existing.startedAt || (status === "downloading" ? nowIso : existing.startedAt),
    finishedAt: ["success", "rolled_back", "failed", "replaced"].includes(status) ? nowIso : existing.finishedAt,
    history: [
      ...(Array.isArray(existing.history) ? existing.history : []),
      {
        status,
        message,
        fwVersion,
        progressPercent: nextProgress,
        at: nowIso
      }
    ].slice(-20)
  };

  otaJobs[index] = next;
  saveOtaJobs(otaJobs);

  if ((payload.config && typeof payload.config === "object") || (payload.lowPower && typeof payload.lowPower === "object")) {
    saveDeviceRuntimeConfig(deviceId, {
      ...(payload.config || {}),
      lowPower: payload.lowPower || {}
    }, "device-report", fwVersion);
  }

  return next;
}

function reconcileOtaSuccessFromDeviceVersion(deviceId, fwVersion) {
  const normalizedDeviceId = canonicalizeDeviceId(deviceId || "");
  const normalizedVersion = sanitizeFirmwareText(fwVersion || "", 32);
  if (!normalizedDeviceId || !normalizedVersion) {
    return null;
  }

  const activeJob = getOtaJobsForDevice(normalizedDeviceId).find((job) => ["pending", "downloading", "rebooting"].includes(job.status));
  if (!activeJob) {
    return null;
  }
  if (activeJob.targetVersion !== normalizedVersion) {
    return null;
  }

  if (activeJob.status === "success") {
    return activeJob;
  }

  return updateOtaJobStatus({
    deviceId: normalizedDeviceId,
    jobId: activeJob.id,
    status: "success",
    message: "device reported target firmware version after ota reboot",
    fwVersion: normalizedVersion
  });
}

function buildOtaCheckResponse(req, deviceId, currentVersion) {
  const job = getActiveOtaJobForDevice(deviceId, currentVersion);
  if (!job) {
    return { hasUpdate: false };
  }

  const firmware = getFirmwarePackage(job.firmwareId);
  if (!firmware) {
    return { hasUpdate: false };
  }

  const baseUrl = getRequestBaseUrl(req);
  return {
    hasUpdate: true,
    jobId: job.id,
    targetVersion: job.targetVersion,
    size: firmware.otaSize || firmware.size,
    sha256: firmware.otaSha256 || firmware.sha256,
    force: Boolean(job.force),
    url: `${baseUrl}/api/device/ota/download/${encodeURIComponent(job.id)}?token=${encodeURIComponent(job.token)}`
  };
}

function getDeviceAdminOtaSummary(deviceId) {
  const jobs = getOtaJobsForDevice(deviceId);
  const decoratedJobs = jobs.slice(0, 10).map((job) => ({
    ...job,
    firmware: getFirmwarePackage(job.firmwareId)
  }));
  const activeJob = jobs.find((job) => ["pending", "downloading", "rebooting"].includes(job.status)) || null;
  return {
    activeJob: activeJob ? { ...activeJob, firmware: getFirmwarePackage(activeJob.firmwareId) } : null,
    jobs: decoratedJobs
  };
}

function getSensorDisplayLabel(deviceId, sensorKey, fallbackLabel = null) {
  const alias = deviceSensorAliases?.[deviceId]?.[sensorKey];
  if (typeof alias === "string" && alias.trim()) {
    return alias.trim();
  }
  return fallbackLabel || SENSOR_CONFIG[sensorKey]?.label || sensorKey;
}

function getMetricColumnLabel(deviceId, sensorKey, metric) {
  return `${getSensorDisplayLabel(deviceId, sensorKey, SENSOR_CONFIG[sensorKey]?.label || sensorKey)}_${metric.label}_${metric.unit}`;
}

function sanitizeSensorAliasInput(value) {
  return String(value || "")
    .replace(/\s+/g, " ")
    .trim()
    .slice(0, 60);
}

function getDeviceSensorAliasPayload(deviceId = null) {
  if (deviceId) {
    return deviceSensorAliases?.[deviceId] || {};
  }
  return deviceSensorAliases;
}

function saveDeviceSensorAliases(payload) {
  const deviceId = canonicalizeDeviceId(payload.deviceId || "");
  if (!deviceId || !DEVICE_ALIAS_BY_ID[deviceId]) {
    throw new Error("invalid deviceId");
  }

  const aliases = payload.aliases;
  if (!aliases || typeof aliases !== "object" || Array.isArray(aliases)) {
    throw new Error("aliases must be an object");
  }

  const next = {};
  Object.entries(aliases).forEach(([sensorKey, alias]) => {
    const canonicalSensorKey = canonicalizeSensorKey(sensorKey);
    if (!canonicalSensorKey || !SENSOR_CONFIG[canonicalSensorKey]) {
      return;
    }
    const normalized = sanitizeSensorAliasInput(alias);
    if (normalized) {
      next[canonicalSensorKey] = normalized;
    }
  });

  if (Object.keys(next).length > 0) {
    deviceSensorAliases = {
      ...deviceSensorAliases,
      [deviceId]: next
    };
  } else if (deviceSensorAliases[deviceId]) {
    const { [deviceId]: _removed, ...rest } = deviceSensorAliases;
    deviceSensorAliases = rest;
  }

  saveDeviceSensorAliasMap(deviceSensorAliases);

  return {
    deviceId,
    aliases: deviceSensorAliases[deviceId] || {}
  };
}

function sanitizeUploadFileName(name) {
  return String(name || "photo")
    .toLowerCase()
    .replace(/[^a-z0-9._-]+/g, "-")
    .replace(/-+/g, "-")
    .replace(/^-|-$/g, "") || "photo";
}

function collectRequestBody(req) {
  return new Promise((resolve, reject) => {
    const chunks = [];
    let total = 0;
    req.on("data", (chunk) => {
      total += chunk.length;
      if (total > ROOM_PHOTO_SIZE_LIMIT * 2) {
        reject(new Error("payload too large"));
        req.destroy();
        return;
      }
      chunks.push(chunk);
    });
    req.on("end", () => resolve(Buffer.concat(chunks).toString("utf8")));
    req.on("error", reject);
  });
}

function parseDataUrlImage(dataUrl) {
  const match = /^data:(image\/(png|jpeg|jpg|webp));base64,(.+)$/i.exec(String(dataUrl || ""));
  if (!match) {
    throw new Error("only png/jpeg/webp images are supported");
  }
  const mimeType = match[1].toLowerCase();
  const ext = mimeType === "image/png" ? "png" : mimeType.includes("webp") ? "webp" : "jpg";
  const buffer = Buffer.from(match[3], "base64");
  if (!buffer.length) {
    throw new Error("empty image payload");
  }
  if (buffer.length > ROOM_PHOTO_SIZE_LIMIT) {
    throw new Error("image too large");
  }
  return { mimeType, ext, buffer };
}

function buildRoomPhotoResponse() {
  const roomPhotos = loadRoomPhotoMap();
  const photos = {};
  ROOM_IDS.forEach((roomId) => {
    const entry = roomPhotos[roomId];
    if (!entry || typeof entry !== "object" || !entry.url) {
      photos[roomId] = null;
      return;
    }
    photos[roomId] = {
      roomId,
      url: entry.url,
      fileName: entry.fileName || null,
      uploadedAt: entry.uploadedAt || null
    };
  });
  return { photos };
}

function removePreviousRoomPhoto(roomId, roomPhotos) {
  const previous = roomPhotos[roomId];
  if (!previous?.fileName) {
    return;
  }
  const previousPath = path.join(roomUploadsDir, previous.fileName);
  if (fs.existsSync(previousPath)) {
    fs.unlinkSync(previousPath);
  }
}

function saveRoomPhoto(roomId, originalName, dataUrl) {
  if (!ROOM_IDS.includes(roomId)) {
    throw new Error("invalid room id");
  }

  const { ext, buffer } = parseDataUrlImage(dataUrl);
  const safeBase = sanitizeUploadFileName(path.parse(originalName || `${roomId}-photo`).name);
  const fileName = `${roomId}-${Date.now()}-${safeBase}.${ext}`;
  const absolutePath = path.join(roomUploadsDir, fileName);
  const publicUrl = `/uploads/rooms/${fileName}`;
  const roomPhotos = loadRoomPhotoMap();

  removePreviousRoomPhoto(roomId, roomPhotos);
  fs.writeFileSync(absolutePath, buffer);
  roomPhotos[roomId] = {
    roomId,
    fileName,
    url: publicUrl,
    uploadedAt: new Date().toISOString()
  };
  saveRoomPhotoMap(roomPhotos);
  return roomPhotos[roomId];
}

function sendJson(res, statusCode, data, extraHeaders = {}) {
  const json = JSON.stringify(data);
  const acceptEncoding = res._req?.headers?.["accept-encoding"] || "";
  const compressed = tryGzip(json, acceptEncoding);

  if (compressed) {
    res.writeHead(statusCode, {
      "Content-Type": "application/json; charset=utf-8",
      "Access-Control-Allow-Origin": "*",
      "Cache-Control": "no-store",
      "Content-Encoding": "gzip",
      "Vary": "Accept-Encoding",
      ...extraHeaders
    });
    res.end(compressed);
  } else {
    res.writeHead(statusCode, {
      "Content-Type": "application/json; charset=utf-8",
      "Access-Control-Allow-Origin": "*",
      "Cache-Control": "no-store",
      ...extraHeaders
    });
    res.end(json);
  }
}

function sendFile(res, filePath) {
  fs.readFile(filePath, (err, content) => {
    if (err) {
      res.writeHead(404, { "Content-Type": "text/plain; charset=utf-8" });
      res.end("Not Found");
      return;
    }

    const ext = path.extname(filePath).toLowerCase();
    const compressible = [".html", ".css", ".js", ".json"].includes(ext);
    const contentTypeMap = {
      ".html": "text/html; charset=utf-8",
      ".css": "text/css; charset=utf-8",
      ".js": "application/javascript; charset=utf-8",
      ".json": "application/json; charset=utf-8",
      ".png": "image/png",
      ".jpg": "image/jpeg",
      ".jpeg": "image/jpeg",
      ".webp": "image/webp"
    };

    const headers = {
      "Content-Type": contentTypeMap[ext] || "application/octet-stream",
      "Cache-Control": [".html", ".js", ".css"].includes(ext) ? "no-store" : "public, max-age=3600",
      "Vary": "Accept-Encoding"
    };

    if (compressible) {
      let cached = gzipFileCache.get(filePath);
      if (!cached) {
        cached = zlib.gzipSync(content);
        gzipFileCache.set(filePath, cached);
      }
      const acceptEncoding = res._req?.headers?.["accept-encoding"] || "";
      if (acceptEncoding.includes("gzip")) {
        res.writeHead(200, { ...headers, "Content-Encoding": "gzip" });
        res.end(cached);
        return;
      }
    }
    res.writeHead(200, headers);
    res.end(content);
  });
}

function formatBytes(bytes) {
  const units = ["B", "KB", "MB", "GB", "TB"];
  let value = bytes;
  let unitIndex = 0;

  while (value >= 1024 && unitIndex < units.length - 1) {
    value /= 1024;
    unitIndex += 1;
  }

  return `${value.toFixed(value >= 10 ? 1 : 2)} ${units[unitIndex]}`;
}

function formatUptime(seconds) {
  const days = Math.floor(seconds / 86400);
  const hours = Math.floor((seconds % 86400) / 3600);
  const minutes = Math.floor((seconds % 3600) / 60);
  return `${days}天 ${hours}小时 ${minutes}分钟`;
}

function readTextFile(filePath) {
  try {
    return fs.readFileSync(filePath, "utf8").trim();
  } catch (error) {
    return null;
  }
}

function getCpuTemperature() {
  const rawValue = readTextFile("/sys/class/thermal/thermal_zone0/temp");
  if (!rawValue) {
    return null;
  }

  const value = Number(rawValue);
  if (!Number.isFinite(value)) {
    return null;
  }

  return Number((value / 1000).toFixed(1));
}

function getDeviceModel() {
  return (
    readTextFile("/proc/device-tree/model") ||
    readTextFile("/sys/firmware/devicetree/base/model") ||
    os.hostname()
  );
}

function cloneCpuTimes(times) {
  return {
    user: times.user,
    nice: times.nice,
    sys: times.sys,
    idle: times.idle,
    irq: times.irq
  };
}

function calculateCpuPercentages(previous, current) {
  return current.map((cpu, index) => {
    const prev = previous[index] || cloneCpuTimes(cpu.times);
    const curr = cpu.times;
    const idleDelta = curr.idle - prev.idle;
    const totalDelta =
      curr.user - prev.user +
      (curr.nice - prev.nice) +
      (curr.sys - prev.sys) +
      idleDelta +
      (curr.irq - prev.irq);

    if (totalDelta <= 0) {
      return 0;
    }

    return Number((((totalDelta - idleDelta) / totalDelta) * 100).toFixed(1));
  });
}

function sampleServerTelemetry() {
  const cpus = os.cpus();
  const perCoreUsage = calculateCpuPercentages(lastCpuSnapshot, cpus);
  lastCpuSnapshot = cpus.map((cpu) => cloneCpuTimes(cpu.times));

  const totalMemory = os.totalmem();
  const usedMemoryPercent = Number((((totalMemory - os.freemem()) / totalMemory) * 100).toFixed(1));
  const cpuTemperatureC = getCpuTemperature();

  serverHistory.push({
    tsMs: Date.now(),
    perCoreUsage,
    avgCpuUsage:
      perCoreUsage.length === 0
        ? 0
        : Number((perCoreUsage.reduce((sum, value) => sum + value, 0) / perCoreUsage.length).toFixed(1)),
    memoryUsedPercent: usedMemoryPercent,
    cpuTemperatureC
  });

  while (serverHistory.length > SERVER_HISTORY_LIMIT) {
    serverHistory.shift();
  }
}

function rememberDevice(deviceName, info) {
  if (!deviceName) {
    return;
  }
  const presenceConfig = DEVICE_PRESENCE_CONFIG[deviceName] || null;
  const existing = deviceRegistry.get(deviceName) || {
    device: deviceName,
    alias: deviceName,
    firstSeenAt: new Date().toISOString(),
    sensors: []
  };

  const sensors = Array.isArray(info.sensorKeys)
    ? info.sensorKeys.filter(Boolean)
    : (() => {
        const nextSensors = new Set(existing.sensors || []);
        if (info.sensorKey) {
          nextSensors.add(info.sensorKey);
        }
        return Array.from(nextSensors);
      })();

  const nextDevice = {
    ...existing,
    alias: info.alias || existing.alias || deviceName,
    clientId: info.clientId || existing.clientId || deviceName,
    lastSeenAt: new Date().toISOString(),
    lastTopic: info.lastTopic || existing.lastTopic || "--",
    sensorKey: info.sensorKey || existing.sensorKey || null,
    source: info.source || existing.source || "mqtt",
    sensors,
    fwVersion: info.fwVersion || existing.fwVersion || null,
    lowPowerEnabled: typeof info.lowPowerEnabled === "boolean"
      ? info.lowPowerEnabled
      : (presenceConfig?.lowPowerEnabled ?? existing.lowPowerEnabled ?? false),
    reportIntervalSec: Number.isFinite(Number(info.reportIntervalSec))
      ? Number(info.reportIntervalSec)
      : (presenceConfig?.reportIntervalSec ?? existing.reportIntervalSec ?? null)
  };

  deviceRegistry.set(deviceName, nextDevice);
  reconcileOtaSuccessFromDeviceVersion(deviceName, nextDevice.fwVersion);
}

function getDeviceOnlineWindowMs(device) {
  const reportIntervalSec = Number(device?.reportIntervalSec);
  if (device?.lowPowerEnabled && Number.isFinite(reportIntervalSec) && reportIntervalSec >= 10) {
    return Math.max(
      DEVICE_ONLINE_WINDOW_MS,
      (reportIntervalSec + 180) * 1000,
      reportIntervalSec * 3 * 1000
    );
  }
  return DEVICE_ONLINE_WINDOW_MS;
}

function getDevicesStatus() {
  const now = Date.now();
  return Array.from(deviceRegistry.values())
    .sort((a, b) => (b.lastSeenAt || "").localeCompare(a.lastSeenAt || ""))
    .map((device) => {
      const onlineWindowMs = getDeviceOnlineWindowMs(device);
      return {
        ...device,
        onlineWindowMs,
        online: Boolean(device.lastSeenAt) && now - new Date(device.lastSeenAt).getTime() <= onlineWindowMs,
        lastSeenAgoSeconds: device.lastSeenAt
          ? Math.max(0, Math.round((now - new Date(device.lastSeenAt).getTime()) / 1000))
          : null
      };
    });
}

function getServerHistory() {
  return {
    sampleIntervalMs: SERVER_SAMPLE_INTERVAL_MS,
    points: serverHistory,
    cpuCoreCount: lastCpuSnapshot.length
  };
}

function getPublicUrl() {
  const candidates = [
    path.join(__dirname, "public-url.txt"),
    path.join(__dirname, "cloudflared-url.txt")
  ];
  for (const candidate of candidates) {
    const value = readTextFile(candidate);
    if (value && value.startsWith("http")) {
      return value;
    }
  }
  return null;
}

function getPressureState(pressure) {
  if (!Number.isFinite(pressure)) {
    return "等待气压值";
  }
  if (pressure < 1000) {
    return "偏低";
  }
  if (pressure > 1025) {
    return "偏高";
  }
  return "正常";
}

function toShanghaiDay(tsMs) {
  const date = new Date(tsMs + 8 * 60 * 60 * 1000);
  return date.toISOString().slice(0, 10);
}

function normalizeTs(tsValue) {
  if (!tsValue) {
    return Date.now();
  }
  if (typeof tsValue === "number" && Number.isFinite(tsValue)) {
    return tsValue < 10_000_000_000 ? tsValue * 1000 : tsValue;
  }
  const parsed = new Date(tsValue).getTime();
  return Number.isFinite(parsed) ? parsed : Date.now();
}

function persistMetricValue(sensorKey, metricKey, value, meta) {
  const tsMs = normalizeTs(meta.updatedAt);
  insertMetricReadingStmt.run(
    tsMs,
    new Date(tsMs).toISOString(),
    toShanghaiDay(tsMs),
    sensorKey,
    metricKey,
    value,
    meta.unit,
    meta.topic,
    meta.source
  );
}

function canonicalizeSensorKey(rawSensorKey) {
  return SENSOR_TYPE_TO_KEY[String(rawSensorKey || "").trim()] || null;
}

function getOrCreateDeviceSensors(deviceName) {
  if (!deviceName) {
    return null;
  }
  if (!latestSensorsByDevice[deviceName]) {
    latestSensorsByDevice[deviceName] = {};
  }
  return latestSensorsByDevice[deviceName];
}

function getOrCreateDeviceSensorState(deviceName, sensorKey) {
  const deviceSensors = getOrCreateDeviceSensors(deviceName);
  if (!deviceSensors) {
    return createDefaultSensorState(sensorKey);
  }
  if (!deviceSensors[sensorKey]) {
    deviceSensors[sensorKey] = createDefaultSensorState(sensorKey);
  }
  return deviceSensors[sensorKey];
}

function pruneDeviceSensorStates(deviceName, sensorKeys) {
  const deviceSensors = getOrCreateDeviceSensors(deviceName);
  if (!deviceSensors || !Array.isArray(sensorKeys)) {
    return;
  }

  const allowedSensorKeys = new Set(sensorKeys.filter(Boolean));
  Object.keys(deviceSensors).forEach((sensorKey) => {
    if (!allowedSensorKeys.has(sensorKey)) {
      delete deviceSensors[sensorKey];
    }
  });
}

function resolveSensorPayload(sensorKey, payload) {
  if (!payload || typeof payload !== "object") {
    return {};
  }

  if (!payload.sensors || typeof payload.sensors !== "object") {
    return payload;
  }

  const candidate = payload.sensors[sensorKey];
  return candidate && typeof candidate === "object" ? candidate : {};
}

function parseMqttDeviceTopic(topic) {
  if (typeof topic !== "string" || !topic.startsWith(MQTT_DEVICE_TOPIC_PREFIX)) {
    return null;
  }

  const suffix = topic.slice(MQTT_DEVICE_TOPIC_PREFIX.length).trim();
  if (!suffix || suffix.includes("/")) {
    return null;
  }

  return canonicalizeDeviceId(suffix);
}

function recordSensorPayload(sensorKey, payload, meta) {
  const config = SENSOR_CONFIG[sensorKey];
  if (!config) {
    throw new Error(`unknown sensor: ${sensorKey}`);
  }

  const updatedAt = new Date().toISOString();
  const deviceName = canonicalizeDeviceId(payload.device || meta.device || "mqtt-client");
  const sensorPayload = resolveSensorPayload(sensorKey, payload);
  const next = {
    ...latestSensors[sensorKey],
    source: deviceName,
    topic: meta.topic,
    updatedAt
  };
  const deviceNext = {
    ...getOrCreateDeviceSensorState(deviceName, sensorKey),
    source: deviceName,
    topic: meta.topic,
    updatedAt
  };

  config.metrics.forEach((metric) => {
    const value = Number(sensorPayload[metric.key]);
    if (Number.isFinite(value)) {
      const roundedValue = Number(
        value.toFixed(
          metric.unit === "hPa" ? 1 :
          metric.unit === "dBm" ? 0 :
          metric.unit === "V" ? 3 :
          metric.unit === "mA" || metric.unit === "mW" ? 3 :
          1
        )
      );
      next[metric.key] = roundedValue;
      deviceNext[metric.key] = roundedValue;
      persistMetricValue(sensorKey, metric.key, next[metric.key], {
        unit: metric.unit,
        topic: meta.topic,
        source: deviceName,
        updatedAt
      });
    }
  });

  if (sensorKey === "bmp180" || sensorKey === "bmp280") {
    next.pressureState = getPressureState(next.pressure);
    deviceNext.pressureState = getPressureState(deviceNext.pressure);
  }

  latestSensors[sensorKey] = next;
  latestSensorsByDevice[deviceName][sensorKey] = deviceNext;
  return deviceNext;
}

function loadLatestSensors() {
  const sensors = buildDefaultLatestSensors();
  const rows = latestMetricRowsStmt.all();

  rows.forEach((row) => {
    const sensorKey = canonicalizeSensorKey(row.sensor_key);
    const sensor = sensors[sensorKey];
    if (!sensor) {
      return;
    }
    sensor[row.metric_key] = row.value;
    sensor.source = row.source;
    sensor.topic = row.topic;
    sensor.updatedAt = row.recorded_at;
  });

  sensors.bmp180.pressureState = getPressureState(sensors.bmp180.pressure);
  sensors.bmp280.pressureState = getPressureState(sensors.bmp280.pressure);
  return sensors;
}

function loadLatestSensorsByDevice() {
  const devices = {};
  const rows = latestMetricRowsByDeviceStmt.all();

  rows.forEach((row) => {
    const deviceName = row.source;
    const sensorKey = canonicalizeSensorKey(row.sensor_key);
    if (!deviceName) {
      return;
    }
    if (!sensorKey) {
      return;
    }
    if (!devices[deviceName]) {
      devices[deviceName] = {};
    }
    if (!devices[deviceName][sensorKey]) {
      devices[deviceName][sensorKey] = createDefaultSensorState(sensorKey);
    }

    const sensor = devices[deviceName][sensorKey];
    sensor[row.metric_key] = row.value;
    sensor.source = deviceName;
    sensor.topic = row.topic;
    sensor.updatedAt = row.recorded_at;
  });

  Object.values(devices).forEach((deviceSensors) => {
    if (deviceSensors.bmp180) {
      deviceSensors.bmp180.pressureState = getPressureState(deviceSensors.bmp180.pressure);
    }
    if (deviceSensors.bmp280) {
      deviceSensors.bmp280.pressureState = getPressureState(deviceSensors.bmp280.pressure);
    }
  });

  return devices;
}

function purgeObsoleteData() {
  db.exec(`
    DELETE FROM metric_readings
    WHERE sensor_key IN ('flower', 'fish', 'climate', 'light')
       OR topic IN ('legacy/http', 'http/api/sensor/update', 'demo/dht11')
       OR source = 'demo-simulator';
  `);
  db.exec(`DROP TABLE IF EXISTS sensor_readings;`);
}

function cleanupOldReadings() {
  const cutoff = Date.now() - RETENTION_DAYS * 24 * 60 * 60 * 1000;
  deleteOldMetricStmt.run(cutoff);
}

function buildLatestResponse() {
  const dht11 = latestSensors.dht11;
  return {
    temperature: dht11.temperature,
    humidity: dht11.humidity,
    source: dht11.source,
    updatedAt: dht11.updatedAt,
    sensors: latestSensors,
    deviceSensors: latestSensorsByDevice,
    sensorAliases: deviceSensorAliases,
    mqtt: {
      port: mqttStatus.port,
      lastMessageAt: mqttStatus.lastMessageAt,
      lastTopic: mqttStatus.lastTopic,
      lastClientId: mqttStatus.lastClientId
    },
    devices: getDevicesStatus()
  };
}

function getLocalIpAddresses() {
  const nets = os.networkInterfaces();
  const addrs = [];
  if (process.env.PUBLIC_IP) {
    addrs.push(process.env.PUBLIC_IP);
  }
  for (const name of Object.keys(nets)) {
    for (const net of nets[name]) {
      if (net.family === "IPv4" && !net.internal) {
        addrs.push(net.address);
      }
    }
  }
  return addrs;
}

function getServerStatus() {
  const cpus = os.cpus();
  const totalMemory = os.totalmem();
  const freeMemory = os.freemem();
  const usedMemory = totalMemory - freeMemory;

  return {
    hostname: os.hostname(),
    deviceModel: getDeviceModel(),
    platform: `${os.platform()} ${os.arch()}`,
    ipAddresses: getLocalIpAddresses(),
    cpuModel: cpus[0]?.model || "unknown",
    cpuCores: cpus.length,
    loadAverage: os.loadavg().map((value) => Number(value.toFixed(2))),
    uptimeSeconds: os.uptime(),
    uptimeText: formatUptime(os.uptime()),
    memory: {
      totalBytes: totalMemory,
      freeBytes: freeMemory,
      usedBytes: usedMemory,
      usedPercent: Number(((usedMemory / totalMemory) * 100).toFixed(1)),
      totalText: formatBytes(totalMemory),
      freeText: formatBytes(freeMemory),
      usedText: formatBytes(usedMemory)
    },
    cpuTemperatureC: getCpuTemperature(),
    cpuCurrentUsage: serverHistory[serverHistory.length - 1]?.avgCpuUsage ?? 0,
    perCoreUsage: serverHistory[serverHistory.length - 1]?.perCoreUsage || new Array(cpus.length).fill(0),
    mqtt: {
      port: mqttStatus.port,
      lastMessageAt: mqttStatus.lastMessageAt,
      lastTopic: mqttStatus.lastTopic,
      lastClientId: mqttStatus.lastClientId
    },
    updatedAt: new Date().toISOString()
  };
}

function resolveHistoryWindow(parsedUrl) {
  const date = parsedUrl.searchParams.get("date");
  const range = parsedUrl.searchParams.get("range") || "1h";
  const now = Date.now();

  if (date) {
    const start = new Date(`${date}T00:00:00+08:00`).getTime();
    if (!Number.isFinite(start)) {
      throw new Error("invalid date");
    }

    return {
      mode: "date",
      label: date,
      startTs: start,
      endTs: start + 24 * 60 * 60 * 1000,
      bucketMinutes: 5
    };
  }

  const rangeMap = {
    "1h": { durationMs: 60 * 60 * 1000, bucketMinutes: 0.25, label: "最近1小时" },
    "3h": { durationMs: 3 * 60 * 60 * 1000, bucketMinutes: 0.5, label: "最近3小时" },
    "6h": { durationMs: 6 * 60 * 60 * 1000, bucketMinutes: 1, label: "最近6小时" },
    "24h": { durationMs: 24 * 60 * 60 * 1000, bucketMinutes: 3, label: "最近24小时" },
    "3d": { durationMs: 3 * 24 * 60 * 60 * 1000, bucketMinutes: 15, label: "最近3天" },
    "7d": { durationMs: 7 * 24 * 60 * 60 * 1000, bucketMinutes: 30, label: "最近7天" }
  };

  const config = rangeMap[range];
  if (!config) {
    throw new Error("invalid range");
  }

  return {
    mode: "range",
    label: config.label,
    startTs: now - config.durationMs,
    endTs: now,
    bucketMinutes: config.bucketMinutes
  };
}

function resolveSeriesConfig(parsedUrl) {
  const seriesKey = canonicalizeSensorKey(parsedUrl.searchParams.get("series") || "dht11");
  const config = SENSOR_CONFIG[seriesKey];
  if (!config) {
    throw new Error("invalid series");
  }
  return {
    key: seriesKey,
    label: config.label,
    metrics: config.metrics
  };
}

function buildHistoryExportResult({ deviceId, sensorKey, metricKey = null, range = "24h", date = "" }) {
  if (!sensorKey) {
    throw new Error("sensorKey is required");
  }

  const history = fetchRawSensorHistory({
    db,
    sensorConfigMap: SENSOR_CONFIG,
    sensorKey,
    deviceNames: deviceId ? [deviceId] : [],
    range,
    date,
    metricKey,
    resolveSensorLabel: getSensorDisplayLabel
  });
  const deviceLabel = DEVICE_ALIAS_BY_ID[deviceId] || deviceId || "未命名设备";
  const saved = writeHistoryCsvFile({
    exportRootDir: repoRootDataDir,
    deviceLabel,
    history,
    metricKey
  });

  return {
    message: "history exported",
    deviceId: deviceId || null,
    deviceLabel,
    sensorKey: history.sensorKey,
    sensorLabel: history.sensorLabel,
    metricKey: saved.metricKey,
    metricLabel: saved.metricLabel,
    range: date ? null : range,
    date: date || null,
    rowCount: saved.rowCount,
    filePath: saved.filePath,
    relativePath: path.relative(path.resolve(__dirname, "..", ".."), saved.filePath).split(path.sep).join("/")
  };
}

function getRawSensorHistory(parsedUrl) {
  const seriesKey = canonicalizeSensorKey(parsedUrl.searchParams.get("series") || "dht11");
  const metricKey = String(parsedUrl.searchParams.get("metric") || "").trim() || null;
  const deviceNames = parsedUrl.searchParams
    .getAll("device")
    .flatMap((value) => String(value || "").split(","))
    .map((value) => value.trim())
    .filter(Boolean);
  const range = String(parsedUrl.searchParams.get("range") || "24h").trim();
  const date = String(parsedUrl.searchParams.get("date") || "").trim();

  return fetchRawSensorHistory({
    db,
    sensorConfigMap: SENSOR_CONFIG,
    sensorKey: seriesKey,
    deviceNames,
    range,
    date,
    metricKey,
    resolveSensorLabel: getSensorDisplayLabel
  });
}

function csvEscape(value) {
  if (value == null) {
    return "";
  }
  const text = String(value);
  if (/[",\r\n]/.test(text)) {
    return `"${text.replace(/"/g, "\"\"")}"`;
  }
  return text;
}

function buildMultiSensorRawExport(payload) {
  const deviceId = String(payload.deviceId || "").trim();
  const sensorKeys = Array.isArray(payload.sensorKeys)
    ? payload.sensorKeys.map((value) => canonicalizeSensorKey(value)).filter(Boolean)
    : [];
  const startAt = String(payload.startAt || "").trim();
  const endAt = String(payload.endAt || "").trim();

  if (!deviceId) {
    throw new Error("deviceId is required");
  }
  if (!sensorKeys.length) {
    throw new Error("at least one sensor must be selected");
  }

  const window = resolveAbsoluteWindow({ startAt, endAt });
  const selectedMetrics = sensorKeys.flatMap((sensorKey) => {
    const config = SENSOR_CONFIG[sensorKey];
    if (!config) {
      return [];
    }
    return config.metrics.map((metric) => ({
      sensorKey,
      sensorLabel: getSensorDisplayLabel(deviceId, sensorKey, config.label),
      metricKey: metric.key,
      metricLabel: metric.label,
      unit: metric.unit,
      columnKey: `${sensorKey}.${metric.key}`,
      columnLabel: getMetricColumnLabel(deviceId, sensorKey, metric)
    }));
  });

  const sensorPlaceholders = sensorKeys.map(() => "?").join(", ");
  const rows = db.prepare(`
    SELECT
      ts_ms AS tsMs,
      recorded_at AS recordedAt,
      sensor_key AS sensorKey,
      metric_key AS metricKey,
      value
    FROM metric_readings
    WHERE source = ?
      AND sensor_key IN (${sensorPlaceholders})
      AND ts_ms >= ?
      AND ts_ms < ?
    ORDER BY ts_ms ASC, sensor_key ASC, metric_key ASC
  `).all(deviceId, ...sensorKeys, window.startTs, window.endTs);

  const MERGE_WINDOW_MS = 2000;
  const points = [];
  rows.forEach((row) => {
    const pointKey = `${row.sensorKey}.${row.metricKey}`;
    const lastPoint = points[points.length - 1];
    if (lastPoint && row.tsMs - lastPoint.tsMs <= MERGE_WINDOW_MS) {
      lastPoint[pointKey] = row.value;
      return;
    }
    points.push({
      tsMs: row.tsMs,
      recordedAt: row.recordedAt || new Date(row.tsMs).toISOString(),
      [pointKey]: row.value
    });
  });
  const header = [
    "recorded_at",
    ...selectedMetrics.map((item) => item.columnLabel)
  ];
  const csvLines = [
    header.map(csvEscape).join(","),
    ...points.map((point) => [
      point.recordedAt,
      ...selectedMetrics.map((item) => point[item.columnKey] ?? "")
    ].map(csvEscape).join(","))
  ];

  const deviceLabel = DEVICE_ALIAS_BY_ID[deviceId] || deviceId;
  const sensorLabelText = sensorKeys
    .map((sensorKey) => getSensorDisplayLabel(deviceId, sensorKey, SENSOR_CONFIG[sensorKey]?.label || sensorKey))
    .join("+");
  const startLabel = startAt.replace(/[:T]/g, "-");
  const endLabel = endAt.replace(/[:T]/g, "-");
  const fileName = `${sanitizeFileName(`${deviceLabel}_${startLabel}_${endLabel}_${sensorLabelText}`)}.csv`;

  return {
    fileName,
    csvText: `\ufeff${csvLines.join("\n")}\n`,
    rowCount: points.length,
    deviceId,
    deviceLabel,
    sensorKeys
  };
}

function getSensorHistory(parsedUrl) {
  const window = resolveHistoryWindow(parsedUrl);
  const series = resolveSeriesConfig(parsedUrl);
  const deviceNames = parsedUrl.searchParams
    .getAll("device")
    .flatMap((value) => String(value || "").split(","))
    .map((value) => value.trim())
    .filter(Boolean);
  const metricKeys = series.metrics.map((metric) => metric.key);
  const bucketMs = window.bucketMinutes * 60 * 1000;
  const placeholders = metricKeys.map(() => "?").join(", ");
  const deviceClause = deviceNames.length
    ? `AND source IN (${deviceNames.map(() => "?").join(", ")})`
    : "";
  const historyParams = deviceNames.length
    ? [series.key, ...metricKeys, ...deviceNames, window.startTs, window.endTs, bucketMs]
    : [series.key, ...metricKeys, window.startTs, window.endTs, bucketMs];

  const rows = db.prepare(`
    SELECT
      MIN(ts_ms) AS tsMs,
      metric_key AS metricKey,
      ROUND(AVG(value), 1) AS value,
      COUNT(*) AS sampleCount
    FROM metric_readings
    WHERE sensor_key = ?
      AND metric_key IN (${placeholders})
      ${deviceClause}
      AND ts_ms >= ?
      AND ts_ms < ?
    GROUP BY CAST(ts_ms / ? AS INTEGER), metric_key
    ORDER BY tsMs ASC
  `).all(...historyParams);

  const pointsByTs = new Map();
  rows.forEach((row) => {
    const existing = pointsByTs.get(row.tsMs) || {
      tsMs: row.tsMs,
      sampleCount: 0
    };
    existing[row.metricKey] = row.value;
    existing.sampleCount = Math.max(existing.sampleCount, row.sampleCount);
    pointsByTs.set(row.tsMs, existing);
  });

  const statsParams = deviceNames.length
    ? [series.key, ...metricKeys, ...deviceNames, window.startTs, window.endTs]
    : [series.key, ...metricKeys, window.startTs, window.endTs];

  const statsRows = db.prepare(`
    SELECT
      metric_key AS metricKey,
      ROUND(MIN(value), 1) AS minValue,
      ROUND(MAX(value), 1) AS maxValue,
      ROUND(AVG(value), 1) AS avgValue,
      COUNT(*) AS sampleCount
    FROM metric_readings
    WHERE sensor_key = ?
      AND metric_key IN (${placeholders})
      ${deviceClause}
      AND ts_ms >= ?
      AND ts_ms < ?
    GROUP BY metric_key
  `).all(...statsParams);

  const stats = {};
  series.metrics.forEach((metric) => {
    const matched = statsRows.find((row) => row.metricKey === metric.key);
    stats[metric.key] = {
      label: metric.label,
      unit: metric.unit,
      min: matched?.minValue ?? null,
      max: matched?.maxValue ?? null,
      avg: matched?.avgValue ?? null,
      sampleCount: matched?.sampleCount ?? 0
    };
  });

  return {
    mode: window.mode,
    label: `${deviceNames[0] ? `${deviceNames[0]} · ` : ""}${getSensorDisplayLabel(deviceNames[0] || null, series.key, series.label)} · ${window.label}`,
    series: series.key,
    device: deviceNames[0] || null,
    devices: deviceNames,
    startTs: window.startTs,
    endTs: window.endTs,
    bucketMinutes: window.bucketMinutes,
    metrics: series.metrics,
    points: Array.from(pointsByTs.values()),
    stats
  };
}

function buildGatewayHeartbeat(targetDevice, reason = "heartbeat") {
  const publicUrl = getPublicUrl();
  return {
    type: "gateway-heartbeat",
    reason,
    targetDevice: targetDevice || null,
    timestamp: new Date().toISOString(),
    serverOnline: true,
    mqttOnline: true,
    httpOnline: true,
    publicUrlAvailable: Boolean(publicUrl),
    publicUrl,
    httpPort: PORT,
    mqttPort: MQTT_PORT,
    hostname: os.hostname(),
    uptimeSeconds: os.uptime(),
    cpuTemperatureC: getCpuTemperature()
  };
}

function publishGatewayStatus(topic, payload, retain = false) {
  aedes.publish({
    topic,
    payload: Buffer.from(JSON.stringify(payload), "utf8"),
    qos: 0,
    retain
  });
}

function handleGatewayPing(payloadBuffer, clientId) {
  let payload = {};
  try {
    payload = JSON.parse(payloadBuffer.toString("utf8") || "{}");
  } catch (error) {
    payload = {};
  }

  const device = payload.device || clientId;
  if (!device) {
    return;
  }
  rememberDevice(device, {
    alias: payload.alias || device,
    clientId: clientId || device,
    lastTopic: GATEWAY_PING_TOPIC,
    source: "gateway-ping"
  });

  mqttStatus.lastMessageAt = new Date().toISOString();
  mqttStatus.lastTopic = GATEWAY_PING_TOPIC;
  mqttStatus.lastClientId = clientId || device;

  publishGatewayStatus(
    `${GATEWAY_STATUS_PREFIX}${device}`,
    buildGatewayHeartbeat(device, "reply"),
    false
  );
}

function publishPumpCommand(durationSeconds) {
  const payload = {
    type: "pump-command",
    device: "yard-01",
    action: "pulse",
    durationSeconds,
    topic: YARD_PUMP_CONTROL_TOPIC,
    timestamp: new Date().toISOString(),
    source: "web-console"
  };

  aedes.publish({
    topic: YARD_PUMP_CONTROL_TOPIC,
    payload: Buffer.from(JSON.stringify(payload), "utf8"),
    qos: 0,
    retain: false
  });

  mqttStatus.lastMessageAt = new Date().toISOString();
  mqttStatus.lastTopic = YARD_PUMP_CONTROL_TOPIC;
  mqttStatus.lastClientId = "web-console";

  return payload;
}

function handleMqttPacket(topic, payloadBuffer, clientId) {
  if (topic === GATEWAY_PING_TOPIC) {
    handleGatewayPing(payloadBuffer, clientId);
    return;
  }

  const topicDevice = parseMqttDeviceTopic(topic);
  if (!topicDevice) {
    return;
  }

  let payload;
  try {
    payload = JSON.parse(payloadBuffer.toString("utf8"));
  } catch (error) {
    console.warn(`[mqtt] invalid json on ${topic}`);
    return;
  }

  const source = canonicalizeDeviceId(payload.device || topicDevice || clientId || "mqtt-client");
  const fwVersion = sanitizeFirmwareText(payload.fwVersion || "", 32);
  if ((payload.config && typeof payload.config === "object") || (payload.lowPower && typeof payload.lowPower === "object")) {
    saveDeviceRuntimeConfig(source, {
      ...(payload.config || {}),
      lowPower: payload.lowPower || {}
    }, "device-report", fwVersion);
  }
  const payloadSensors = payload.sensors && typeof payload.sensors === "object" ? payload.sensors : {};
  const sensorEntries = Object.entries(payloadSensors)
    .map(([sensorType, sensorPayload]) => ({
      sensorType,
      sensorKey: SENSOR_TYPE_TO_KEY[sensorType],
      sensorPayload
    }))
    .filter(({ sensorKey, sensorPayload }) => sensorKey && sensorPayload && typeof sensorPayload === "object");

  if (sensorEntries.length === 0) {
    rememberDevice(source, {
      alias: getDeviceAlias(source, payload.alias),
      clientId: clientId || source,
      lastTopic: topic,
      source: "sensor-mqtt:config",
      fwVersion,
      lowPowerEnabled: payload.lowPower?.enabled,
      reportIntervalSec: payload.lowPower?.intervalSec
    });
    return;
  }

  const deviceSensorKeys = new Set();
  pruneDeviceSensorStates(source, sensorEntries.map(({ sensorKey }) => sensorKey));
  rememberDevice(source, {
    alias: getDeviceAlias(source, payload.alias),
    clientId: clientId || source,
    lastTopic: topic,
    source: "sensor-mqtt",
    sensorKeys: sensorEntries.map(({ sensorKey }) => sensorKey),
    fwVersion,
    lowPowerEnabled: payload.lowPower?.enabled,
    reportIntervalSec: payload.lowPower?.intervalSec
  });

  sensorEntries.forEach(({ sensorKey, sensorType }) => {
    const reading = recordSensorPayload(sensorKey, payload, {
      source,
      topic,
      updatedAt: new Date().toISOString()
    });
    deviceSensorKeys.add(sensorKey);

    rememberDevice(source, {
      alias: getDeviceAlias(source, payload.alias),
      clientId: clientId || source,
      lastTopic: topic,
      sensorKey,
      source: `sensor-mqtt:${sensorType}`,
      fwVersion,
      lowPowerEnabled: payload.lowPower?.enabled,
      reportIntervalSec: payload.lowPower?.intervalSec
    });
  });

  mqttStatus.lastMessageAt = new Date().toISOString();
  mqttStatus.lastTopic = topic;
  mqttStatus.lastClientId = clientId || source;
}

purgeObsoleteData();
latestSensors = loadLatestSensors();
latestSensorsByDevice = loadLatestSensorsByDevice();
cleanupOldReadings();
sampleServerTelemetry();
setInterval(cleanupOldReadings, 12 * 60 * 60 * 1000);
setInterval(sampleServerTelemetry, SERVER_SAMPLE_INTERVAL_MS);
setInterval(() => {
  publishGatewayStatus(GATEWAY_BROADCAST_TOPIC, buildGatewayHeartbeat(null, "broadcast"), true);
}, GATEWAY_BROADCAST_INTERVAL_MS);

// 启动时预热天气缓存，避免首次请求等待外网
getWeatherForecast().catch((err) => {
  console.warn("[weather] 预热失败，将在首次请求时重试:", err.message);
});
// 每25分钟主动刷新缓存（比30分钟过期提前5分钟）
setInterval(() => {
  invalidateWeatherCache();
  getWeatherForecast().catch((err) => {
    console.warn("[weather] 后台刷新失败，等待下次重试:", err.message);
  });
}, 25 * 60 * 1000);

aedes.on("client", (client) => {
  mqttStatus.lastClientId = client?.id || mqttStatus.lastClientId;
});

aedes.on("publish", (packet, client) => {
  if (!client || !packet?.topic || packet.topic.startsWith("$SYS")) {
    return;
  }
  handleMqttPacket(packet.topic, packet.payload, client.id);
});

const mqttServer = net.createServer(aedes.handle);
mqttServer.listen(MQTT_PORT, () => {
  console.log(`MQTT broker listening on mqtt://0.0.0.0:${MQTT_PORT}`);
  publishGatewayStatus(GATEWAY_BROADCAST_TOPIC, buildGatewayHeartbeat(null, "startup"), true);
});

const mqttWsServer = new WebSocket.Server({ port: MQTT_WS_PORT });
mqttWsServer.on("connection", (socket) => {
  aedes.handle(WebSocket.createWebSocketStream(socket));
});
console.log(`MQTT WebSocket broker listening on ws://0.0.0.0:${MQTT_WS_PORT}`);

const server = http.createServer((req, res) => {
  res._req = req;
  const parsedUrl = new URL(req.url, `http://${req.headers.host}`);
  const otaDownloadMatch = /^\/api\/device\/ota\/download\/([^/]+)$/.exec(parsedUrl.pathname);

  if (req.method === "OPTIONS") {
    res.writeHead(204, {
      "Access-Control-Allow-Origin": "*",
      "Access-Control-Allow-Methods": "GET,POST,OPTIONS",
      "Access-Control-Allow-Headers": "Content-Type"
    });
    res.end();
    return;
  }

  if (req.method === "GET" && parsedUrl.pathname === "/api/admin/session") {
    sendJson(res, 200, { authenticated: isAdminAuthenticated(req) });
    return;
  }

  if (req.method === "POST" && parsedUrl.pathname === "/api/admin/login") {
    collectRequestBody(req)
      .then((bodyText) => {
        const payload = JSON.parse(bodyText || "{}");
        if (String(payload.password || "") !== ADMIN_PASSWORD) {
          sendJson(res, 401, { message: "invalid admin password" });
          return;
        }
        const token = createAdminSession();
        sendJson(
          res,
          200,
          { authenticated: true },
          {
            "Set-Cookie": `admin_session=${token}; Path=/; HttpOnly; SameSite=Lax; Max-Age=${Math.floor(ADMIN_SESSION_TTL_MS / 1000)}`
          }
        );
      })
      .catch((error) => {
        sendJson(res, 400, { message: error.message || "invalid login payload" });
      });
    return;
  }

  if (req.method === "POST" && parsedUrl.pathname === "/api/admin/logout") {
    const token = parseCookies(req).admin_session;
    if (token) {
      adminSessions.delete(token);
    }
    sendJson(res, 200, { authenticated: false }, { "Set-Cookie": "admin_session=; Path=/; HttpOnly; SameSite=Lax; Max-Age=0" });
    return;
  }

  if (req.method === "GET" && parsedUrl.pathname === "/api/device/ota/check") {
    const deviceId = canonicalizeDeviceId(parsedUrl.searchParams.get("deviceId") || "");
    const fwVersion = sanitizeFirmwareText(parsedUrl.searchParams.get("fwVersion") || "", 32);
    if (!DEVICE_ALIAS_BY_ID[deviceId]) {
      sendJson(res, 400, { message: "invalid deviceId" });
      return;
    }
    sendJson(res, 200, buildOtaCheckResponse(req, deviceId, fwVersion));
    return;
  }

  if (otaDownloadMatch && req.method === "GET") {
    const jobId = decodeURIComponent(otaDownloadMatch[1]);
    const token = String(parsedUrl.searchParams.get("token") || "").trim();
    const job = otaJobs.find((item) => item.id === jobId) || null;
    const firmware = job ? getFirmwarePackage(job.firmwareId) : null;
    if (!job || !firmware || token !== job.token) {
      sendJson(res, 404, { message: "ota package not found" });
      return;
    }

    const absolutePath = path.join(firmwareDir, firmware.storedFileName);
    if (!fs.existsSync(absolutePath)) {
      sendJson(res, 404, { message: "firmware file missing" });
      return;
    }

    let otaBuffer;
    try {
      const packageBuffer = fs.readFileSync(absolutePath);
      const parsedPackage = parseFirmwarePackage(packageBuffer, firmware.originalFileName || firmware.storedFileName);
      otaBuffer = getFirmwareOtaSegment(parsedPackage, packageBuffer).data;
    } catch (error) {
      sendJson(res, 500, { message: error.message || "firmware parse failed" });
      return;
    }

    res.writeHead(200, {
      "Content-Type": "application/octet-stream",
      "Content-Length": otaBuffer.length,
      "Cache-Control": "no-store"
    });
    res.end(otaBuffer);
    return;
  }

  if (req.method === "POST" && parsedUrl.pathname === "/api/device/ota/report") {
    collectRequestBody(req)
      .then((bodyText) => {
        const payload = JSON.parse(bodyText || "{}");
        const updatedJob = updateOtaJobStatus(payload);
        rememberDevice(updatedJob.deviceId, {
          alias: DEVICE_ALIAS_BY_ID[updatedJob.deviceId],
          lastTopic: "http:ota-report",
          source: "ota-report",
          fwVersion: sanitizeFirmwareText(payload.fwVersion || "", 32)
        });
        sendJson(res, 200, { message: "ota status updated", job: updatedJob });
      })
      .catch((error) => {
        sendJson(res, 400, { message: error.message || "invalid ota report payload" });
      });
    return;
  }

  if (req.method === "GET" && parsedUrl.pathname === "/api/device/config/check") {
    const deviceId = canonicalizeDeviceId(parsedUrl.searchParams.get("deviceId") || "");
    if (!DEVICE_ALIAS_BY_ID[deviceId]) {
      sendJson(res, 400, { message: "invalid deviceId" });
      return;
    }

    const activeJob = getActiveDeviceConfigJobForDevice(deviceId);
    sendJson(res, 200, {
      deviceId,
      hasUpdate: Boolean(activeJob),
      job: activeJob
        ? {
            id: activeJob.id,
            config: activeJob.config,
            lowPower: activeJob.lowPower,
            message: activeJob.message,
            createdAt: activeJob.createdAt
          }
        : null
    });
    return;
  }

  if (req.method === "POST" && parsedUrl.pathname === "/api/device/config/report") {
    collectRequestBody(req)
      .then((bodyText) => {
        const payload = JSON.parse(bodyText || "{}");
        const updatedJob = updateDeviceConfigJobStatus(payload);
        sendJson(res, 200, { message: "config status updated", job: updatedJob });
      })
      .catch((error) => {
        sendJson(res, 400, { message: error.message || "invalid config report payload" });
      });
    return;
  }

  if (req.method === "GET" && parsedUrl.pathname === "/api/sensor/latest") {
    sendJson(res, 200, buildLatestResponse());
    return;
  }

  if (req.method === "GET" && parsedUrl.pathname === "/api/sensor/history") {
    try {
      sendJson(res, 200, getSensorHistory(parsedUrl));
    } catch (error) {
      sendJson(res, 400, { message: error.message });
    }
    return;
  }

  if (req.method === "GET" && parsedUrl.pathname === "/api/sensor/history/raw") {
    try {
      sendJson(res, 200, getRawSensorHistory(parsedUrl));
    } catch (error) {
      sendJson(res, 400, { message: error.message });
    }
    return;
  }

  if (req.method === "GET" && parsedUrl.pathname === "/api/mqtt/status") {
    sendJson(res, 200, {
      ...mqttStatus,
      topics: [
        {
          pattern: `${MQTT_DEVICE_TOPIC_PREFIX}<deviceId>`,
          description: "每个设备一个 MQTT 主题，设备数据通过 payload.sensors 逐个展开解析"
        }
      ],
      gatewayTopics: {
        ping: GATEWAY_PING_TOPIC,
        statusPrefix: GATEWAY_STATUS_PREFIX,
        broadcast: GATEWAY_BROADCAST_TOPIC
      }
    });
    return;
  }

  if (req.method === "GET" && parsedUrl.pathname === "/api/devices/status") {
    sendJson(res, 200, {
      onlineWindowMs: DEVICE_ONLINE_WINDOW_MS,
      devices: getDevicesStatus()
    });
    return;
  }

  if (req.method === "GET" && parsedUrl.pathname === "/api/server/status") {
    sendJson(res, 200, getServerStatus());
    return;
  }

  if (req.method === "GET" && parsedUrl.pathname === "/api/server/history") {
    sendJson(res, 200, getServerHistory());
    return;
  }

  if (req.method === "GET" && parsedUrl.pathname === "/api/public-url") {
    const url = getPublicUrl();
    if (!url) {
      sendJson(res, 404, { message: "public url not ready" });
      return;
    }
    sendJson(res, 200, { url });
    return;
  }

  if (req.method === "GET" && parsedUrl.pathname === "/api/weather/forecast") {
    getWeatherForecast()
      .then((data) => sendJson(res, 200, data))
      .catch((error) =>
        sendJson(res, 500, {
          message: "weather service unavailable",
          detail: error.message
        })
      );
    return;
  }

  if (req.method === "GET" && parsedUrl.pathname === "/api/room-photos") {
    sendJson(res, 200, buildRoomPhotoResponse());
    return;
  }

  if (req.method === "GET" && parsedUrl.pathname === "/api/device-sensor-aliases") {
    const deviceId = String(parsedUrl.searchParams.get("deviceId") || "").trim();
    sendJson(res, 200, {
      aliases: getDeviceSensorAliasPayload(deviceId ? canonicalizeDeviceId(deviceId) : null)
    });
    return;
  }

  if (req.method === "GET" && parsedUrl.pathname === "/api/device-ota") {
    const deviceId = canonicalizeDeviceId(parsedUrl.searchParams.get("deviceId") || "");
    if (!DEVICE_ALIAS_BY_ID[deviceId]) {
      sendJson(res, 400, { message: "invalid deviceId" });
      return;
    }
    sendJson(res, 200, {
      deviceId,
      ...getDeviceAdminOtaSummary(deviceId)
    });
    return;
  }

  if (req.method === "GET" && parsedUrl.pathname === "/api/device-admin") {
    const deviceId = canonicalizeDeviceId(parsedUrl.searchParams.get("deviceId") || "");
    if (!DEVICE_ALIAS_BY_ID[deviceId]) {
      sendJson(res, 400, { message: "invalid deviceId" });
      return;
    }
    sendJson(res, 200, {
      deviceId,
      ...getDeviceAdminSummary(deviceId)
    });
    return;
  }

  if (req.method === "POST" && parsedUrl.pathname === "/api/device-admin/config") {
    collectRequestBody(req)
      .then((bodyText) => {
        const payload = JSON.parse(bodyText || "{}");
        const job = createDeviceConfigJob(payload);
        sendJson(res, 200, {
          message: "config job created",
          job,
          currentConfig: getDeviceRuntimeConfig(job.deviceId)
        });
      })
      .catch((error) => {
        sendJson(res, 400, { message: error.message || "invalid config payload" });
      });
    return;
  }

  if (req.method === "POST" && parsedUrl.pathname === "/api/device-ota/upload-and-start") {
    collectRequestBody(req)
      .then((bodyText) => {
        const payload = JSON.parse(bodyText || "{}");
        const firmware = saveFirmwarePackage(payload);
        const job = createOtaJob({
          deviceId: payload.deviceId,
          firmwareId: firmware.id,
          force: payload.force,
          message: payload.notes || firmware.notes || payload.message || "created from direct ota page"
        });
        sendJson(res, 200, {
          message: "firmware uploaded and ota job created",
          firmware,
          job
        });
      })
      .catch((error) => {
        sendJson(res, 400, { message: error.message || "invalid ota upload payload" });
      });
    return;
  }

  if (req.method === "GET" && parsedUrl.pathname === "/api/admin/firmware/list") {
    if (!requireAdmin(req, res)) {
      return;
    }
    sendJson(res, 200, { packages: listFirmwarePackages() });
    return;
  }

  if (req.method === "POST" && parsedUrl.pathname === "/api/admin/firmware/upload") {
    if (!requireAdmin(req, res)) {
      return;
    }
    collectRequestBody(req)
      .then((bodyText) => {
        const payload = JSON.parse(bodyText || "{}");
        const entry = saveFirmwarePackage(payload);
        sendJson(res, 200, { message: "firmware uploaded", firmware: entry });
      })
      .catch((error) => {
        sendJson(res, 400, { message: error.message || "invalid firmware payload" });
      });
    return;
  }

  if (req.method === "POST" && parsedUrl.pathname === "/api/admin/device-ota") {
    if (!requireAdmin(req, res)) {
      return;
    }
    collectRequestBody(req)
      .then((bodyText) => {
        const payload = JSON.parse(bodyText || "{}");
        const job = createOtaJob(payload);
        sendJson(res, 200, { message: "ota job created", job });
      })
      .catch((error) => {
        sendJson(res, 400, { message: error.message || "invalid ota job payload" });
      });
    return;
  }

  if (req.method === "GET" && parsedUrl.pathname === "/api/admin/device-ota") {
    if (!requireAdmin(req, res)) {
      return;
    }
    const deviceId = canonicalizeDeviceId(parsedUrl.searchParams.get("deviceId") || "");
    if (!DEVICE_ALIAS_BY_ID[deviceId]) {
      sendJson(res, 400, { message: "invalid deviceId" });
      return;
    }
    sendJson(res, 200, {
      deviceId,
      ...getDeviceAdminOtaSummary(deviceId)
    });
    return;
  }

  if (req.method === "POST" && parsedUrl.pathname === "/api/device-sensor-aliases") {
    collectRequestBody(req)
      .then((bodyText) => {
        const payload = JSON.parse(bodyText || "{}");
        sendJson(res, 200, {
          message: "sensor aliases saved",
          ...saveDeviceSensorAliases(payload)
        });
      })
      .catch((error) => {
        sendJson(res, 400, {
          message: error.message || "invalid alias payload"
        });
      });
    return;
  }

  if (req.method === "POST" && parsedUrl.pathname === "/api/room-photos/upload") {
    collectRequestBody(req)
      .then((bodyText) => {
        const payload = JSON.parse(bodyText || "{}");
        const roomId = String(payload.roomId || "").trim();
        const fileName = String(payload.fileName || "photo");
        const dataUrl = String(payload.dataUrl || "");
        const saved = saveRoomPhoto(roomId, fileName, dataUrl);
        sendJson(res, 200, {
          message: "room photo uploaded",
          photo: saved
        });
      })
      .catch((error) => {
        sendJson(res, 400, {
          message: error.message || "invalid upload payload"
        });
      });
    return;
  }

  if (req.method === "POST" && parsedUrl.pathname === "/api/export/history") {
    collectRequestBody(req)
      .then((bodyText) => {
        const payload = JSON.parse(bodyText || "{}");
        const result = buildHistoryExportResult({
          deviceId: String(payload.deviceId || "").trim(),
          sensorKey: String(payload.sensorKey || "").trim(),
          metricKey: payload.metricKey ? String(payload.metricKey).trim() : null,
          range: String(payload.range || "24h").trim(),
          date: String(payload.date || "").trim()
        });
        sendJson(res, 200, result);
      })
      .catch((error) => {
        sendJson(res, 400, {
          message: error.message || "invalid export payload"
        });
      });
    return;
  }

  if (req.method === "POST" && parsedUrl.pathname === "/api/export/history/multi") {
    collectRequestBody(req)
      .then((bodyText) => {
        const payload = JSON.parse(bodyText || "{}");
        sendJson(res, 200, buildMultiSensorRawExport(payload));
      })
      .catch((error) => {
        sendJson(res, 400, {
          message: error.message || "invalid multi export payload"
        });
      });
    return;
  }

  if (req.method === "POST" && parsedUrl.pathname === "/api/device/yard/pump") {
    let body = "";
    req.on("data", (chunk) => {
      body += chunk;
    });
    req.on("end", () => {
      try {
        const payload = JSON.parse(body || "{}");
        const durationSeconds = Number(payload.durationSeconds);
        const password = String(payload.password || "");

        if (!Number.isFinite(durationSeconds) || durationSeconds <= 0 || durationSeconds > 600) {
          sendJson(res, 400, {
            message: "durationSeconds must be a number between 1 and 600"
          });
          return;
        }

        if (password !== PUMP_CONTROL_PASSWORD) {
          sendJson(res, 403, {
            message: "invalid password"
          });
          return;
        }

        const command = publishPumpCommand(Math.round(durationSeconds));
        sendJson(res, 200, {
          message: "pump command published",
          command
        });
      } catch (error) {
        sendJson(res, 400, {
          message: error.message || "invalid json payload"
        });
      }
    });
    return;
  }

  const filePath =
    parsedUrl.pathname === "/"
      ? path.join(publicDir, "index.html")
      : path.join(publicDir, parsedUrl.pathname);

  sendFile(res, filePath);
});

server.listen(PORT, () => {
  console.log(`HTTP dashboard running at http://localhost:${PORT}`);
});
