const http = require("http");
const fs = require("fs");
const os = require("os");
const path = require("path");
const net = require("net");
const zlib = require("zlib");
const { URL } = require("url");
const { DatabaseSync } = require("node:sqlite");
const aedes = require("aedes")();
const WebSocket = require("ws");
const { getWeatherForecast, invalidateWeatherCache } = require("./lib/weather");

const PORT = Number(process.env.PORT || 3000);
const MQTT_PORT = Number(process.env.MQTT_PORT || 1884);
const MQTT_WS_PORT = Number(process.env.MQTT_WS_PORT || 9001);
const RETENTION_DAYS = 90;
const GATEWAY_BROADCAST_INTERVAL_MS = 30 * 1000;
const SERVER_SAMPLE_INTERVAL_MS = 60 * 1000;   // 每分钟采样一次
const DEVICE_ONLINE_WINDOW_MS = 90 * 1000;
const SERVER_HISTORY_LIMIT = 1440;             // 保留 24 小时（24 × 60）

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
  battery: {
    label: "电池电压",
    metrics: [
      { key: "voltage", label: "电压", unit: "V", color: "#4caf50", axis: "left" },
      { key: "percent", label: "电量", unit: "%", color: "#ff9800", axis: "right" }
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
  battery: "battery"
};
const DEVICE_ID_ALIASES = {
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
  "yard-01": "庭院 1 号",
  "study-01": "书房 1 号",
  "office-01": "办公室 1 号",
  "bedroom-01": "卧室 1 号"
};
const GATEWAY_PING_TOPIC = "garden/gateway/ping";
const GATEWAY_STATUS_PREFIX = "garden/gateway/status/";
const GATEWAY_BROADCAST_TOPIC = "garden/gateway/broadcast";
const YARD_PUMP_CONTROL_TOPIC = "garden/yard/pump/set";
const PUMP_CONTROL_PASSWORD = String(process.env.PUMP_CONTROL_PASSWORD || "1234");

const publicDir = path.join(__dirname, "public");
const dataDir = path.join(__dirname, "data");
const dbPath = path.join(dataDir, "sensor-history.db");

if (!fs.existsSync(dataDir)) {
  fs.mkdirSync(dataDir, { recursive: true });
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
    battery: {
      label: SENSOR_CONFIG.battery.label,
      voltage: null,
      percent: null,
      source: "waiting-for-mqtt",
      topic: `${MQTT_DEVICE_TOPIC_PREFIX}battery-01`,
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

function sendJson(res, statusCode, data) {
  const json = JSON.stringify(data);
  const acceptEncoding = res._req?.headers?.["accept-encoding"] || "";
  const compressed = tryGzip(json, acceptEncoding);

  if (compressed) {
    res.writeHead(statusCode, {
      "Content-Type": "application/json; charset=utf-8",
      "Access-Control-Allow-Origin": "*",
      "Cache-Control": "no-store",
      "Content-Encoding": "gzip",
      "Vary": "Accept-Encoding"
    });
    res.end(compressed);
  } else {
    res.writeHead(statusCode, {
      "Content-Type": "application/json; charset=utf-8",
      "Access-Control-Allow-Origin": "*",
      "Cache-Control": "no-store"
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
      ".json": "application/json; charset=utf-8"
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
  const existing = deviceRegistry.get(deviceName) || {
    device: deviceName,
    alias: deviceName,
    firstSeenAt: new Date().toISOString(),
    sensors: []
  };

  const sensors = new Set(existing.sensors || []);
  if (info.sensorKey) {
    sensors.add(info.sensorKey);
  }

  deviceRegistry.set(deviceName, {
    ...existing,
    alias: info.alias || existing.alias || deviceName,
    clientId: info.clientId || existing.clientId || deviceName,
    lastSeenAt: new Date().toISOString(),
    lastTopic: info.lastTopic || existing.lastTopic || "--",
    sensorKey: info.sensorKey || existing.sensorKey || null,
    source: info.source || existing.source || "mqtt",
    sensors: Array.from(sensors)
  });
}

function getDevicesStatus() {
  const now = Date.now();
  return Array.from(deviceRegistry.values())
    .sort((a, b) => (b.lastSeenAt || "").localeCompare(a.lastSeenAt || ""))
    .map((device) => ({
      ...device,
      online: Boolean(device.lastSeenAt) && now - new Date(device.lastSeenAt).getTime() <= DEVICE_ONLINE_WINDOW_MS,
      lastSeenAgoSeconds: device.lastSeenAt
        ? Math.max(0, Math.round((now - new Date(device.lastSeenAt).getTime()) / 1000))
        : null
    }));
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
      const roundedValue = Number(value.toFixed(metric.unit === "hPa" ? 1 : metric.unit === "V" ? 2 : 1));
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
    label: `${deviceNames[0] ? `${deviceNames[0]} · ` : ""}${series.label} · ${window.label}`,
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
  const payloadSensors = payload.sensors && typeof payload.sensors === "object" ? payload.sensors : {};
  const sensorEntries = Object.entries(payloadSensors)
    .map(([sensorType, sensorPayload]) => ({
      sensorType,
      sensorKey: SENSOR_TYPE_TO_KEY[sensorType],
      sensorPayload
    }))
    .filter(({ sensorKey, sensorPayload }) => sensorKey && sensorPayload && typeof sensorPayload === "object");

  if (sensorEntries.length === 0) {
    return;
  }

  const deviceSensorKeys = new Set();
  rememberDevice(source, {
    alias: getDeviceAlias(source, payload.alias),
    clientId: clientId || source,
    lastTopic: topic,
    source: "sensor-mqtt"
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
      source: `sensor-mqtt:${sensorType}`
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

  if (req.method === "OPTIONS") {
    res.writeHead(204, {
      "Access-Control-Allow-Origin": "*",
      "Access-Control-Allow-Methods": "GET,POST,OPTIONS",
      "Access-Control-Allow-Headers": "Content-Type"
    });
    res.end();
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
