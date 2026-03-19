const http = require("http");
const fs = require("fs");
const os = require("os");
const path = require("path");
const net = require("net");
const { URL } = require("url");
const { DatabaseSync } = require("node:sqlite");
const aedes = require("aedes")();

const PORT = Number(process.env.PORT || 3000);
const MQTT_PORT = Number(process.env.MQTT_PORT || 1883);
const WEATHER_CACHE_MS = 30 * 60 * 1000;
const RETENTION_DAYS = 90;
const GATEWAY_BROADCAST_INTERVAL_MS = 30 * 1000;
const SERVER_SAMPLE_INTERVAL_MS = 5000;
const DEVICE_ONLINE_WINDOW_MS = 90 * 1000;
const SERVER_HISTORY_LIMIT = 360;
const WEATHER_URL =
  "https://api.open-meteo.com/v1/forecast?latitude=30.25&longitude=119.75&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max&current=temperature_2m,relative_humidity_2m,weather_code&timezone=Asia%2FShanghai&forecast_days=5";

const SENSOR_CONFIG = {
  flower: {
    label: "花花环境",
    metrics: [
      { key: "temperature", label: "花花温度", unit: "°C", color: "#f08b3e", axis: "left" },
      { key: "humidity", label: "花花湿度", unit: "%RH", color: "#38a9d9", axis: "right" }
    ]
  },
  fish: {
    label: "鱼鱼温度",
    metrics: [{ key: "temperature", label: "鱼鱼温度", unit: "°C", color: "#20b77a", axis: "left" }]
  },
  climate: {
    label: "气压站",
    metrics: [
      { key: "temperature", label: "BMP280 温度", unit: "°C", color: "#ff8b5c", axis: "left" },
      { key: "pressure", label: "气压", unit: "hPa", color: "#6f73ff", axis: "right" }
    ]
  },
  light: {
    label: "光照站",
    metrics: [{ key: "illuminance", label: "光照", unit: "lux", color: "#f5b728", axis: "left" }]
  }
};

const MQTT_TOPIC_CONFIG = {
  "garden/flower/dht11": { sensorKey: "flower", fields: ["temperature", "humidity"] },
  "garden/fish/ds18b20": { sensorKey: "fish", fields: ["temperature"] },
  "garden/climate/bmp280": { sensorKey: "climate", fields: ["temperature", "pressure"] },
  "garden/light/bh1750": { sensorKey: "light", fields: ["illuminance"] }
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
  CREATE TABLE IF NOT EXISTS sensor_readings (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    ts_ms INTEGER NOT NULL,
    recorded_at TEXT NOT NULL,
    recorded_day TEXT NOT NULL,
    temperature REAL NOT NULL,
    humidity REAL NOT NULL,
    source TEXT NOT NULL
  );
  CREATE INDEX IF NOT EXISTS idx_sensor_ts ON sensor_readings(ts_ms);
  CREATE INDEX IF NOT EXISTS idx_sensor_day ON sensor_readings(recorded_day);

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

const insertLegacyReadingStmt = db.prepare(`
  INSERT INTO sensor_readings (ts_ms, recorded_at, recorded_day, temperature, humidity, source)
  VALUES (?, ?, ?, ?, ?, ?)
`);

const insertMetricReadingStmt = db.prepare(`
  INSERT OR IGNORE INTO metric_readings
  (ts_ms, recorded_at, recorded_day, sensor_key, metric_key, value, unit, topic, source)
  VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
`);

const deleteOldLegacyStmt = db.prepare(`
  DELETE FROM sensor_readings
  WHERE ts_ms < ?
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

const selectLatestLegacyStmt = db.prepare(`
  SELECT temperature, humidity, source, recorded_at AS updatedAt
  FROM sensor_readings
  ORDER BY ts_ms DESC
  LIMIT 1
`);

let weatherCache = {
  expiresAt: 0,
  data: null
};

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
  const now = new Date().toISOString();
  return {
    flower: {
      label: SENSOR_CONFIG.flower.label,
      temperature: 26.3,
      humidity: 61.5,
      source: "demo-simulator",
      topic: "demo/flower",
      updatedAt: now
    },
    fish: {
      label: SENSOR_CONFIG.fish.label,
      temperature: null,
      source: "waiting-for-mqtt",
      topic: "garden/fish/ds18b20",
      updatedAt: null
    },
    climate: {
      label: SENSOR_CONFIG.climate.label,
      temperature: null,
      pressure: null,
      pressureState: "等待气压值",
      source: "waiting-for-mqtt",
      topic: "garden/climate/bmp280",
      updatedAt: null
    },
    light: {
      label: SENSOR_CONFIG.light.label,
      illuminance: null,
      source: "waiting-for-mqtt",
      topic: "garden/light/bh1750",
      updatedAt: null
    }
  };
}

function createDefaultSensorState(sensorKey) {
  return { ...buildDefaultLatestSensors()[sensorKey] };
}

let latestSensors = loadLatestSensors();
let latestSensorsByDevice = loadLatestSensorsByDevice();
let flowerDemoMode = latestSensors.flower.source === "demo-simulator";

function sendJson(res, statusCode, data) {
  res.writeHead(statusCode, {
    "Content-Type": "application/json; charset=utf-8",
    "Access-Control-Allow-Origin": "*",
    "Cache-Control": "no-store"
  });
  res.end(JSON.stringify(data));
}

function sendFile(res, filePath) {
  fs.readFile(filePath, (err, content) => {
    if (err) {
      res.writeHead(404, { "Content-Type": "text/plain; charset=utf-8" });
      res.end("Not Found");
      return;
    }

    const ext = path.extname(filePath).toLowerCase();
    const contentTypeMap = {
      ".html": "text/html; charset=utf-8",
      ".css": "text/css; charset=utf-8",
      ".js": "application/javascript; charset=utf-8",
      ".json": "application/json; charset=utf-8"
    };

    res.writeHead(200, {
      "Content-Type": contentTypeMap[ext] || "application/octet-stream",
      "Cache-Control": [".html", ".js", ".css"].includes(ext) ? "no-store" : "public, max-age=3600"
    });
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

function getWeatherDescription(code) {
  const codeMap = {
    0: "晴朗",
    1: "大致晴",
    2: "多云",
    3: "阴天",
    45: "有雾",
    48: "冻雾",
    51: "毛毛雨",
    53: "小毛雨",
    55: "中毛雨",
    56: "冻毛雨",
    57: "强冻毛雨",
    61: "小雨",
    63: "中雨",
    65: "大雨",
    66: "冻雨",
    67: "强冻雨",
    71: "小雪",
    73: "中雪",
    75: "大雪",
    77: "雪粒",
    80: "阵雨",
    81: "较强阵雨",
    82: "强阵雨",
    85: "阵雪",
    86: "强阵雪",
    95: "雷暴",
    96: "雷暴夹小冰雹",
    99: "雷暴夹大冰雹"
  };

  return codeMap[code] || "花园天气保密中";
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

function persistLegacyFlowerReading(reading) {
  const tsMs = normalizeTs(reading.updatedAt);
  insertLegacyReadingStmt.run(
    tsMs,
    reading.updatedAt,
    toShanghaiDay(tsMs),
    reading.temperature,
    reading.humidity,
    reading.source
  );
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

function recordSensorPayload(sensorKey, payload, meta) {
  const config = SENSOR_CONFIG[sensorKey];
  if (!config) {
    throw new Error(`unknown sensor: ${sensorKey}`);
  }

  const updatedAt = new Date(normalizeTs(payload.ts || payload.timestamp || meta.updatedAt)).toISOString();
  const deviceName = payload.device || meta.device || meta.source || "mqtt-client";
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
    const value = Number(payload[metric.key]);
    if (Number.isFinite(value)) {
      const roundedValue = Number(value.toFixed(metric.unit === "hPa" ? 1 : 1));
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

  if (sensorKey === "climate") {
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
    const sensor = sensors[row.sensor_key];
    if (!sensor) {
      return;
    }
    sensor[row.metric_key] = row.value;
    sensor.source = row.source;
    sensor.topic = row.topic;
    sensor.updatedAt = row.recorded_at;
  });

  const legacy = selectLatestLegacyStmt.get();
  if (legacy && !rows.some((row) => row.sensor_key === "flower")) {
    sensors.flower.temperature = legacy.temperature;
    sensors.flower.humidity = legacy.humidity;
    sensors.flower.source = legacy.source;
    sensors.flower.topic = "legacy/http";
    sensors.flower.updatedAt = legacy.updatedAt;
  }

  sensors.climate.pressureState = getPressureState(sensors.climate.pressure);
  return sensors;
}

function loadLatestSensorsByDevice() {
  const devices = {};
  const rows = latestMetricRowsByDeviceStmt.all();

  rows.forEach((row) => {
    const deviceName = row.source;
    if (!deviceName) {
      return;
    }
    if (!devices[deviceName]) {
      devices[deviceName] = {};
    }
    if (!devices[deviceName][row.sensor_key]) {
      devices[deviceName][row.sensor_key] = createDefaultSensorState(row.sensor_key);
    }

    const sensor = devices[deviceName][row.sensor_key];
    sensor[row.metric_key] = row.value;
    sensor.source = deviceName;
    sensor.topic = row.topic;
    sensor.updatedAt = row.recorded_at;
  });

  Object.values(devices).forEach((deviceSensors) => {
    if (deviceSensors.climate) {
      deviceSensors.climate.pressureState = getPressureState(deviceSensors.climate.pressure);
    }
  });

  return devices;
}

function migrateLegacyFlowerHistory() {
  db.exec(`
    INSERT OR IGNORE INTO metric_readings
    (ts_ms, recorded_at, recorded_day, sensor_key, metric_key, value, unit, topic, source)
    SELECT ts_ms, recorded_at, recorded_day, 'flower', 'temperature', temperature, '°C', 'legacy/http', source
    FROM sensor_readings;

    INSERT OR IGNORE INTO metric_readings
    (ts_ms, recorded_at, recorded_day, sensor_key, metric_key, value, unit, topic, source)
    SELECT ts_ms, recorded_at, recorded_day, 'flower', 'humidity', humidity, '%RH', 'legacy/http', source
    FROM sensor_readings;
  `);
}

function cleanupOldReadings() {
  const cutoff = Date.now() - RETENTION_DAYS * 24 * 60 * 60 * 1000;
  deleteOldLegacyStmt.run(cutoff);
  deleteOldMetricStmt.run(cutoff);
}

function buildLatestResponse() {
  const flower = latestSensors.flower;
  return {
    temperature: flower.temperature,
    humidity: flower.humidity,
    source: flower.source,
    updatedAt: flower.updatedAt,
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

function makeDemoFlowerReading() {
  const baseTemp = Number.isFinite(latestSensors.flower.temperature) ? latestSensors.flower.temperature : 26.3;
  const baseHumidity = Number.isFinite(latestSensors.flower.humidity) ? latestSensors.flower.humidity : 61.5;
  const nextTemperature = Math.max(10, Math.min(40, baseTemp + (Math.random() - 0.5) * 0.6));
  const nextHumidity = Math.max(20, Math.min(95, baseHumidity + (Math.random() - 0.5) * 1.4));
  const updatedAt = new Date().toISOString();

  latestSensors.flower = {
    ...latestSensors.flower,
    temperature: Number(nextTemperature.toFixed(1)),
    humidity: Number(nextHumidity.toFixed(1)),
    source: "demo-simulator",
    topic: "demo/flower",
    updatedAt
  };

  persistLegacyFlowerReading(latestSensors.flower);
  persistMetricValue("flower", "temperature", latestSensors.flower.temperature, {
    unit: "°C",
    topic: "demo/flower",
    source: "demo-simulator",
    updatedAt
  });
  persistMetricValue("flower", "humidity", latestSensors.flower.humidity, {
    unit: "%RH",
    topic: "demo/flower",
    source: "demo-simulator",
    updatedAt
  });
}

function updateDemoReading() {
  if (!flowerDemoMode) {
    return;
  }
  makeDemoFlowerReading();
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

async function getWeatherForecast() {
  if (weatherCache.data && weatherCache.expiresAt > Date.now()) {
    return weatherCache.data;
  }

  const response = await fetch(WEATHER_URL);
  if (!response.ok) {
    throw new Error(`weather request failed: ${response.status}`);
  }

  const payload = await response.json();
  const daily = payload.daily || {};
  const current = payload.current || {};
  const forecast = (daily.time || []).map((date, index) => ({
    date,
    weatherCode: daily.weather_code?.[index] ?? null,
    weatherText: getWeatherDescription(daily.weather_code?.[index]),
    tempMax: daily.temperature_2m_max?.[index] ?? null,
    tempMin: daily.temperature_2m_min?.[index] ?? null,
    rainProbability: daily.precipitation_probability_max?.[index] ?? null
  }));

  const data = {
    location: "杭州临安区",
    source: "Open-Meteo",
    current: {
      temperature: current.temperature_2m ?? null,
      humidity: current.relative_humidity_2m ?? null,
      weatherCode: current.weather_code ?? null,
      weatherText: getWeatherDescription(current.weather_code),
      time: current.time || new Date().toISOString()
    },
    forecast,
    updatedAt: new Date().toISOString()
  };

  weatherCache = {
    data,
    expiresAt: Date.now() + WEATHER_CACHE_MS
  };

  return data;
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
  const seriesKey = parsedUrl.searchParams.get("series") || "flower";
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
    device: "yardHub",
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

function handleLegacyHttpPayload(payload) {
  const temperature = Number(payload.temperature);
  const humidity = Number(payload.humidity);
  if (!Number.isFinite(temperature) || !Number.isFinite(humidity)) {
    throw new Error("temperature and humidity must be numbers");
  }

  const updatedAt = new Date(normalizeTs(payload.ts || payload.timestamp || payload.updatedAt)).toISOString();
  const source = payload.source || "esp32-http";
  latestSensors.flower = {
    ...latestSensors.flower,
    temperature: Number(temperature.toFixed(1)),
    humidity: Number(humidity.toFixed(1)),
    source,
    topic: "http/api/sensor/update",
    updatedAt
  };
  flowerDemoMode = false;

  persistLegacyFlowerReading(latestSensors.flower);
  persistMetricValue("flower", "temperature", latestSensors.flower.temperature, {
    unit: "°C",
    topic: "http/api/sensor/update",
    source,
    updatedAt
  });
  persistMetricValue("flower", "humidity", latestSensors.flower.humidity, {
    unit: "%RH",
    topic: "http/api/sensor/update",
    source,
    updatedAt
  });

  return latestSensors.flower;
}

function handleMqttPacket(topic, payloadBuffer, clientId) {
  if (topic === GATEWAY_PING_TOPIC) {
    handleGatewayPing(payloadBuffer, clientId);
    return;
  }

  const config = MQTT_TOPIC_CONFIG[topic];
  if (!config) {
    return;
  }

  let payload;
  try {
    payload = JSON.parse(payloadBuffer.toString("utf8"));
  } catch (error) {
    console.warn(`[mqtt] invalid json on ${topic}`);
    return;
  }

  const source = payload.device || payload.source || clientId || "mqtt-client";
  rememberDevice(source, {
    alias: payload.alias || source,
    clientId: clientId || source,
    lastTopic: topic,
    sensorKey: config.sensorKey,
    source: "sensor-mqtt"
  });
  const reading = recordSensorPayload(config.sensorKey, payload, {
    source,
    topic,
    updatedAt: new Date().toISOString()
  });

  if (config.sensorKey === "flower") {
    flowerDemoMode = false;
    if (Number.isFinite(reading.temperature) && Number.isFinite(reading.humidity)) {
      persistLegacyFlowerReading({
        temperature: reading.temperature,
        humidity: reading.humidity,
        source,
        updatedAt: reading.updatedAt
      });
    }
  }

  mqttStatus.lastMessageAt = new Date().toISOString();
  mqttStatus.lastTopic = topic;
  mqttStatus.lastClientId = clientId || source;
}

migrateLegacyFlowerHistory();

if (!latestSensors.flower.updatedAt) {
  makeDemoFlowerReading();
}

cleanupOldReadings();
sampleServerTelemetry();
setInterval(updateDemoReading, 3000);
setInterval(cleanupOldReadings, 12 * 60 * 60 * 1000);
setInterval(sampleServerTelemetry, SERVER_SAMPLE_INTERVAL_MS);
setInterval(() => {
  publishGatewayStatus(GATEWAY_BROADCAST_TOPIC, buildGatewayHeartbeat(null, "broadcast"), true);
}, GATEWAY_BROADCAST_INTERVAL_MS);

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

const server = http.createServer((req, res) => {
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
      topics: Object.entries(MQTT_TOPIC_CONFIG).map(([topic, config]) => ({
        topic,
        sensorKey: config.sensorKey,
        fields: config.fields
      })),
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

  if (req.method === "POST" && parsedUrl.pathname === "/api/sensor/update") {
    let body = "";
    req.on("data", (chunk) => {
      body += chunk;
    });
    req.on("end", () => {
      try {
        const payload = JSON.parse(body || "{}");
        const reading = handleLegacyHttpPayload(payload);
        sendJson(res, 200, {
          message: "ok",
          data: reading
        });
      } catch (error) {
        sendJson(res, 400, {
          message: error.message || "invalid json payload"
        });
      }
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
