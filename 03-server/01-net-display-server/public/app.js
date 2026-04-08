const roomConfigs = [
  { id: "all", name: "全屋", description: "查看全部设备" },
  { id: "yard", name: "庭院", description: "庭院节点和植物相关设备" },
  { id: "study", name: "书房", description: "书房温湿度监测" },
  { id: "office", name: "办公室", description: "办公室温湿度监测" },
  { id: "bedroom", name: "卧室", description: "卧室温湿度监测" },
  { id: "server", name: "机柜", description: "网关和服务运行状态" },
  { id: "outdoor", name: "气象站", description: "室外天气和气象数据" }
];

const sensorCatalog = {
  dht11: {
    key: "dht11",
    title: "DHT11 温湿度",
    subtitle: "空气温度与相对湿度",
    icon: "🌼",
    metrics: [
      { key: "temperature", label: "温度", unit: "°C", color: "#ff8a65" },
      { key: "humidity", label: "湿度", unit: "%RH", color: "#5ea6ff" }
    ]
  },
  ds18b20: {
    key: "ds18b20",
    title: "DS18B20 温度",
    subtitle: "单总线温度传感器",
    icon: "🐟",
    metrics: [{ key: "temperature", label: "温度", unit: "°C", color: "#49bf8f" }]
  },
  bmp180: {
    key: "bmp180",
    title: "BMP180 气压温度",
    subtitle: "温度与气压",
    icon: "🎈",
    metrics: [
      { key: "temperature", label: "BMP 温度", unit: "°C", color: "#ff9966" },
      { key: "pressure", label: "气压", unit: "hPa", color: "#8368ff" }
    ]
  },
  bmp280: {
    key: "bmp280",
    title: "BMP280 环境数据",
    subtitle: "温度与气压",
    icon: "🎈",
    metrics: [
      { key: "temperature", label: "BMP280 温度", unit: "°C", color: "#ff9966" },
      { key: "pressure", label: "气压", unit: "hPa", color: "#8368ff" }
    ]
  },
  shtc3: {
    key: "shtc3",
    title: "SHTC3 温湿度",
    subtitle: "空气温度与相对湿度",
    icon: "💧",
    metrics: [
      { key: "temperature", label: "温度", unit: "°C", color: "#ff8a65" },
      { key: "humidity", label: "湿度", unit: "%RH", color: "#5ea6ff" }
    ]
  },
  bh1750: {
    key: "bh1750",
    title: "BH1750 光照",
    subtitle: "环境照度",
    icon: "💡",
    metrics: [{ key: "illuminance", label: "光照", unit: "lux", color: "#f4ba41" }]
  },
  battery: {
    key: "battery",
    title: "电池电压",
    subtitle: "锂电池电量与电压",
    icon: "🔋",
    metrics: [
      { key: "voltage", label: "电压", unit: "V", color: "#4caf50" },
      { key: "percent", label: "电量", unit: "%", color: "#ff9800" }
    ]
  },
  max17043: {
    key: "max17043",
    title: "MAX17043 电量计",
    subtitle: "电池电量计与电压",
    icon: "🔋",
    metrics: [
      { key: "voltage", label: "电压", unit: "V", color: "#4caf50" },
      { key: "percent", label: "电量", unit: "%", color: "#ff9800" }
    ]
  },
  ina226: {
    key: "ina226",
    title: "INA226 电流电压",
    subtitle: "电压、电流与功率",
    icon: "📈",
    metrics: [
      { key: "busVoltage", label: "电压", unit: "V", color: "#3f8cff" },
      { key: "currentMa", label: "电流", unit: "mA", color: "#ff8b5c" },
      { key: "powerMw", label: "功率", unit: "mW", color: "#7a68ff" }
    ]
  }
};

const deviceCatalog = {
  "yard-01": {
    id: "yard-01",
    title: "庭院 1 号",
    subtitle: "庭院温湿度节点",
    room: "yard",
    type: "iot-device",
    lowPowerEnabled: true,
    reportIntervalSec: 300,
    icon: "🪴",
    accentClass: "accent-flower",
    matchIds: ["yard-01"],
    historyDevices: ["yard-01"],
    sensors: [
      { key: "dht11", title: "DHT11 温湿度", subtitle: "空气温度与相对湿度", icon: "🌼", metricLabels: { temperature: "温度", humidity: "湿度" } },
      { key: "battery", title: "电池电压", subtitle: "锂电池电量与电压", icon: "🔋", metricLabels: { voltage: "电压", percent: "电量" } }
    ],
    summary: "庭院 ESP32-C3 节点，挂载 DHT11 温湿度传感器，内置锂电池供电。"
  },
  "study-01": {
    id: "study-01",
    title: "书房 1 号",
    subtitle: "书房温湿度节点",
    room: "study",
    type: "iot-device",
    lowPowerEnabled: true,
    reportIntervalSec: 300,
    icon: "📚",
    accentClass: "accent-flower",
    matchIds: ["study-01"],
    historyDevices: ["study-01"],
    sensors: [
      { key: "dht11", title: "DHT11 温湿度", subtitle: "空气温度与相对湿度", icon: "🌡️", metricLabels: { temperature: "温度", humidity: "湿度" } },
      { key: "max17043", title: "MAX17043 电量计", subtitle: "电池电量与电压", icon: "🔋", metricLabels: { voltage: "电压", percent: "电量" } },
      { key: "ina226", title: "INA226 电流电压", subtitle: "电压、电流与功率", icon: "📈", metricLabels: { busVoltage: "电压", currentMa: "电流", powerMw: "功率" } }
    ],
    summary: "书房 ESP32-C3 节点，支持温湿度、电池电量计与电流电压监测。"
  },
  "office-01": {
    id: "office-01",
    title: "办公室 1 号",
    subtitle: "办公室温湿度节点",
    room: "office",
    type: "iot-device",
    icon: "💼",
    accentClass: "accent-flower",
    matchIds: ["office-01"],
    historyDevices: ["office-01"],
    sensors: [{ key: "dht11", title: "DHT11 温湿度", subtitle: "空气温度与相对湿度", icon: "🌡️", metricLabels: { temperature: "温度", humidity: "湿度" } }],
    summary: "办公室 ESP32-C3 节点，挂载 DHT11 温湿度传感器。"
  },
  "bedroom-01": {
    id: "bedroom-01",
    title: "卧室 1 号",
    subtitle: "卧室温湿度节点",
    room: "bedroom",
    type: "iot-device",
    icon: "🛏️",
    accentClass: "accent-flower",
    matchIds: ["bedroom-01"],
    historyDevices: ["bedroom-01"],
    sensors: [{ key: "dht11", title: "DHT11 温湿度", subtitle: "空气温度与相对湿度", icon: "🌡️", metricLabels: { temperature: "温度", humidity: "湿度" } }],
    summary: "卧室 ESP32-C3 节点，挂载 DHT11 温湿度传感器。"
  },
  server: {
    id: "server",
    title: "网关主机",
    subtitle: "HTTP / MQTT / SQLite 服务",
    room: "server",
    type: "server",
    icon: "🖥️",
    accentClass: "accent-server",
    summary: "网关本身也算一台设备，方便和 IoT 节点统一管理。"
  },
  weather: {
    id: "weather",
    title: "气象站",
    subtitle: "Open-Meteo 预报汇总",
    room: "outdoor",
    type: "weather",
    icon: "🌦️",
    accentClass: "accent-weather",
    summary: "天气服务单独作为一个设备入口，后续替换成真实气象站也不需要换交互。"
  }
};

// 默认在线判定窗口（与服务端 DEVICE_ONLINE_WINDOW_MS 保持一致）
const SENSOR_ONLINE_WINDOW_MS = 90 * 1000;

// 传感器异常阈值 — 超出范围时在设备卡片和详情页展示告警标记
const ALERT_THRESHOLDS = {
  dht11: {
    temperature: { warnHigh: 35, alertHigh: 40, warnLow: 5, alertLow: 0 },
    humidity:    { warnHigh: 90, alertHigh: 95, warnLow: 20, alertLow: 10 }
  },
  ds18b20: {
    temperature: { warnHigh: 30, alertHigh: 35, warnLow: 10, alertLow: 5 }
  },
  bmp180: {
    temperature: { warnHigh: 40, alertHigh: 50, warnLow: -5, alertLow: -10 }
  },
  bmp280: {
    temperature: { warnHigh: 40, alertHigh: 50, warnLow: -5, alertLow: -10 }
  },
  shtc3: {
    temperature: { warnHigh: 35, alertHigh: 40, warnLow: 5, alertLow: 0 },
    humidity:    { warnHigh: 90, alertHigh: 95, warnLow: 20, alertLow: 10 }
  },
  battery: {
    voltage: { warnLow: 3.3, alertLow: 3.1 },
    percent: { warnLow: 20, alertLow: 10 }
  },
  max17043: {
    voltage: { warnLow: 3.3, alertLow: 3.1 },
    percent: { warnLow: 20, alertLow: 10 }
  },
  ina226: {
    busVoltage: { warnLow: 3.3, alertLow: 3.1 }
  }
};

const appState = {
  activeRoom: "all",
  activeDeviceId: null,
  activeDevicePageKey: null,
  activeSensorKey: null,
  latestSensor: null,
  serverStatus: null,
  serverHistory: null,
  devicesStatus: null,
  weather: null,
  historyQuery: { range: "24h", date: "" },
  historyCache: new Map(),
  historyMeta: { metrics: [], bucketMinutes: 10, stats: {} },
  historyPoints: [],
  historyViewStart: 0,
  historyViewEnd: 0,
  hoverIndexByMetric: {},
  hoverGuideXByMetric: {},
  refreshAt: null,
  roomPhotos: {},
  exportDialog: {
    open: false,
    sensorKeys: [],
    startAt: "",
    endAt: "",
    submitting: false
  },
  sensorAliasEditor: {
    submitting: false,
    status: ""
  }
};

const els = {
  overviewView: document.getElementById("overviewView"),
  detailView: document.getElementById("detailView"),
  detailPanel: document.getElementById("detailPanel"),
  locationName: document.getElementById("locationName"),
  locationSubtitle: document.getElementById("locationSubtitle"),
  roomTabs: document.getElementById("roomTabs"),
  roomOverview: document.getElementById("roomOverview"),
  deviceGrid: document.getElementById("deviceGrid"),
  currentRoomLabel: document.getElementById("currentRoomLabel"),
  onlineDeviceCount: document.getElementById("onlineDeviceCount"),
  lastRefreshText: document.getElementById("lastRefreshText"),
  backToOverviewBtn: document.getElementById("backToOverviewBtn")
};

function escapeHtml(value) {
  return String(value ?? "")
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;");
}

function formatTime(value) {
  if (!value) return "--";
  const date = new Date(value);
  return Number.isNaN(date.getTime()) ? "--" : date.toLocaleString("zh-CN", { hour12: false });
}

function formatDateLabel(value) {
  const date = new Date(`${value}T00:00:00+08:00`);
  return Number.isNaN(date.getTime()) ? value : date.toLocaleDateString("zh-CN", { month: "numeric", day: "numeric", weekday: "short" });
}

function formatPointTime(tsMs) {
  return new Date(tsMs).toLocaleString("zh-CN", {
    month: "numeric",
    day: "numeric",
    hour: "2-digit",
    minute: "2-digit",
    hour12: false
  });
}

function formatMetricValue(value, unit) {
  if (value == null || Number.isNaN(Number(value))) return "--";
  if (unit === "lux") return `${Math.round(Number(value))} ${unit}`;
  if (unit === "V") return `${Number(value).toFixed(3)} ${unit}`;
  if (unit === "mA" || unit === "mW") return `${Number(value).toFixed(3)} ${unit}`;
  return `${Number(value).toFixed(1)} ${unit}`;
}

function formatSensorMetricValue(sensorKey, metricKey, value, unit) {
  if (value == null || Number.isNaN(Number(value))) return "--";
  if ((sensorKey === "battery" || sensorKey === "max17043") && metricKey === "percent") {
    return `${Math.round(Number(value))} ${unit}`;
  }
  return formatMetricValue(value, unit);
}

async function fetchJson(url, options = {}) {
  const response = await fetch(url, { cache: "no-store", ...options });
  if (!response.ok) throw new Error(url);
  return response.json();
}

async function postJson(url, payload) {
  const response = await fetch(url, {
    method: "POST",
    cache: "no-store",
    headers: {
      "Content-Type": "application/json"
    },
    body: JSON.stringify(payload)
  });
  const data = await response.json().catch(() => ({}));
  if (!response.ok) {
    throw new Error(data.message || url);
  }
  return data;
}

function isRecentSensorUpdate(updatedAt) {
  if (!updatedAt) return false;
  const tsMs = new Date(updatedAt).getTime();
  return Number.isFinite(tsMs) && Date.now() - tsMs <= SENSOR_ONLINE_WINDOW_MS;
}

function getDeviceStatusEntry(deviceId) {
  return getDevicesStatusMap().get(deviceId) || null;
}

function getDeviceOnlineWindowMs(deviceId) {
  const deviceStatus = getDeviceStatusEntry(deviceId);
  const dynamicWindowMs = Number(deviceStatus?.onlineWindowMs);
  if (Number.isFinite(dynamicWindowMs) && dynamicWindowMs > 0) {
    return dynamicWindowMs;
  }

  const reportIntervalSec = Number(deviceCatalog[deviceId]?.reportIntervalSec);
  if (deviceCatalog[deviceId]?.lowPowerEnabled && Number.isFinite(reportIntervalSec) && reportIntervalSec >= 10) {
    return Math.max(
      SENSOR_ONLINE_WINDOW_MS,
      (reportIntervalSec + 180) * 1000,
      reportIntervalSec * 3 * 1000
    );
  }

  return SENSOR_ONLINE_WINDOW_MS;
}

function isRecentDeviceSensorUpdate(updatedAt, deviceId) {
  if (!updatedAt) return false;
  const tsMs = new Date(updatedAt).getTime();
  return Number.isFinite(tsMs) && Date.now() - tsMs <= getDeviceOnlineWindowMs(deviceId);
}

// 返回 "alert" | "warn" | null，用于颜色编码和告警标记
function getMetricAlertLevel(sensorKey, metricKey, value) {
  if (value == null) return null;
  const thresholds = ALERT_THRESHOLDS[sensorKey]?.[metricKey];
  if (!thresholds) return null;
  const v = Number(value);
  if (Number.isNaN(v)) return null;
  if (v >= (thresholds.alertHigh ?? Infinity) || v <= (thresholds.alertLow ?? -Infinity)) return "alert";
  if (v >= (thresholds.warnHigh ?? Infinity) || v <= (thresholds.warnLow ?? -Infinity)) return "warn";
  return null;
}

function normalizeSensorRef(sensorRef) {
  if (typeof sensorRef === "string") {
    return { key: sensorRef };
  }
  return sensorRef || { key: "" };
}

function getDeviceSensorAlias(deviceId, sensorKey) {
  return appState.latestSensor?.sensorAliases?.[deviceId]?.[sensorKey] || "";
}

function getSensorDisplayTitle(deviceId, sensorKey, fallbackTitle = "") {
  return getDeviceSensorAlias(deviceId, sensorKey) || fallbackTitle || sensorCatalog[sensorKey]?.title || sensorKey;
}

function getMetricCsvColumnLabel(deviceId, sensorKey, metric) {
  return `${getSensorDisplayTitle(deviceId, sensorKey, sensorCatalog[sensorKey]?.title || sensorKey)}_${metric.label}_${metric.unit}`;
}

function getDeviceMatchIds(catalog) {
  return catalog?.matchIds?.length ? catalog.matchIds : [catalog?.id].filter(Boolean);
}

function buildDynamicSensorRefs(deviceId, catalog) {
  const deviceStatus = getDevicesStatusMap().get(deviceId);
  const statusSensorKeys = (deviceStatus?.sensors || []).filter((sensorKey) => sensorCatalog[sensorKey]);
  if (statusSensorKeys.length > 0) {
    return statusSensorKeys.map((sensorKey) => ({ key: sensorKey }));
  }

  const matchIds = getDeviceMatchIds(catalog);
  const sensorKeys = new Set();

  matchIds.forEach((matchId) => {
    const sensors = appState.latestSensor?.deviceSensors?.[matchId];
    if (sensors && typeof sensors === "object") {
      Object.keys(sensors).forEach((sensorKey) => {
        if (sensorCatalog[sensorKey]) {
          sensorKeys.add(sensorKey);
        }
      });
    }
  });

  if (sensorKeys.size === 0) {
    return catalog?.sensors || [];
  }

  return Array.from(sensorKeys).map((sensorKey) => ({ key: sensorKey }));
}

function getSensorSnapshot(sensorRef, deviceId) {
  const ref = normalizeSensorRef(sensorRef);
  const sensorKey = ref.key;
  const catalog = deviceCatalog[deviceId];
  const matchedSensor = getDeviceMatchIds(catalog)
    .map((matchId) => appState.latestSensor?.deviceSensors?.[matchId]?.[sensorKey])
    .find(Boolean);
  const sensor = matchedSensor || {};
  const sensorDef = sensorCatalog[sensorKey];
  return {
    key: sensorKey,
    title: getSensorDisplayTitle(deviceId, sensorKey, ref.title || sensorDef.title),
    subtitle: ref.subtitle || sensorDef.subtitle,
    icon: ref.icon || sensorDef.icon,
    metrics: sensorDef.metrics.map((metric) => ({
      ...metric,
      label: ref.metricLabels?.[metric.key] || metric.label,
      value: sensor[metric.key] ?? null,
      alertLevel: getMetricAlertLevel(sensorKey, metric.key, sensor[metric.key] ?? null)
    })),
    updatedAt: sensor.updatedAt,
    topic: sensor.topic || "--",
    source: sensor.source && sensor.source !== "waiting-for-mqtt" ? sensor.source : "等待设备上报",
    online: isRecentDeviceSensorUpdate(sensor.updatedAt, deviceId),
    pressureState: sensor.pressureState || null,
    raw: sensor
  };
}

function getActiveSensorSnapshot() {
  if (!appState.activeDeviceId || !appState.activeSensorKey) {
    return null;
  }
  const snapshot = getDeviceSnapshot(appState.activeDeviceId);
  return snapshot?.sensors?.find((sensor) => sensor.key === appState.activeSensorKey) || null;
}

function getDevicesStatusMap() {
  const devices = appState.devicesStatus?.devices || [];
  return new Map(devices.map((device) => [device.device, device]));
}

function getIotDevicePresence(catalog, sensors) {
  const devicesMap = getDevicesStatusMap();
  const matched = getDeviceMatchIds(catalog)
    .map((id) => devicesMap.get(id))
    .find(Boolean) ||
    sensors.map((sensor) => devicesMap.get(sensor.raw?.source)).find(Boolean) ||
    null;

  const sensorOnline = sensors.some((sensor) => sensor.online);
  return {
    online: matched?.online ?? sensorOnline,
    statusText: matched?.online ?? sensorOnline ? "在线" : "超时未上报",
    lastSeenAgoSeconds: matched?.lastSeenAgoSeconds ?? null
  };
}

function getSensorMetricCardLabel(sensor, metric, compact) {
  if (!compact) {
    return metric.label;
  }
  const sensorName = String(sensor?.title || "")
    .trim()
    .split(/\s+/)[0] || sensor?.key || "";
  return `${sensorName} ${metric.label}`.trim();
}

function buildDeviceSummaryMetrics(sensors) {
  const onlineSensors = sensors.filter((sensor) => sensor.online);
  const visibleSensors = onlineSensors.length ? onlineSensors : sensors;
  const compactLabels = visibleSensors.length > 1;
  const metrics = [];

  const pushMetric = (sensorKey, metricKey) => {
    const sensor = visibleSensors.find((item) => item.key === sensorKey);
    const metric = sensor?.metrics?.find((item) => item.key === metricKey && item.value != null);
    if (!metric) return;
    metrics.push({
      ...metric,
      label: getSensorMetricCardLabel(sensor, metric, compactLabels),
      sensorKey
    });
  };

  // 电池设备卡片优先展示电量/电压，避免被温湿度等指标挤掉。
  pushMetric("shtc3", "temperature");
  pushMetric("shtc3", "humidity");
  pushMetric("max17043", "percent");
  pushMetric("max17043", "voltage");
  pushMetric("ina226", "currentMa");
  pushMetric("ina226", "busVoltage");
  pushMetric("battery", "percent");
  pushMetric("battery", "voltage");
  pushMetric("bh1750", "illuminance");
  pushMetric("bmp180", "pressure");
  pushMetric("bmp280", "pressure");
  pushMetric("bmp180", "temperature");
  pushMetric("bmp280", "temperature");

  visibleSensors.forEach((sensor) => {
    sensor.metrics
      .filter((metric) => metric.value != null)
      .forEach((metric) => {
        const exists = metrics.some((item) => item.sensorKey === sensor.key && item.key === metric.key);
        if (!exists) {
          metrics.push({
            ...metric,
            label: getSensorMetricCardLabel(sensor, metric, compactLabels),
            sensorKey: sensor.key
          });
        }
      });
  });

  return metrics.slice(0, onlineSensors.length ? 6 : 2);
}

function getOnlineSensorCount(sensors) {
  return sensors.filter((sensor) => sensor.online).length;
}

function getDeviceSensorCountLabel(snapshot) {
  const sensorCount = snapshot.sensors.length;
  const onlineSensorCount = getOnlineSensorCount(snapshot.sensors);
  if (onlineSensorCount > 0) {
    return `在线 ${onlineSensorCount} / 共 ${sensorCount} 个传感器`;
  }
  return `${sensorCount} 个传感器`;
}

function getDeviceUpdatedLabel(snapshot) {
  const onlineUpdatedAt = snapshot.sensors
    .filter((sensor) => sensor.online && sensor.updatedAt)
    .map((sensor) => sensor.updatedAt)
    .sort()
    .slice(-1)[0];
  const updatedAt = onlineUpdatedAt || snapshot.updatedAt;
  return updatedAt ? `更新 ${formatTime(updatedAt)}` : "等待数据";
}

function getDeviceHistoryHint(snapshot, catalog) {
  if (catalog?.type !== "iot-device") {
    return "";
  }
  if (snapshot?.online) {
    return "";
  }
  return `<span>离线可查看历史</span>`;
}

function getDeviceSnapshot(deviceId) {
  const catalog = deviceCatalog[deviceId];
  if (!catalog) return null;

  if (catalog.type === "iot-device") {
    const sensorRefs = buildDynamicSensorRefs(deviceId, catalog);
    const sensors = sensorRefs.map((sensorRef) => getSensorSnapshot(sensorRef, deviceId));
    const presence = getIotDevicePresence(catalog, sensors);
    const lastUpdated = sensors
      .map((sensor) => sensor.updatedAt)
      .filter(Boolean)
      .sort()
      .slice(-1)[0] || null;
    return {
      id: deviceId,
      type: catalog.type,
      online: presence.online,
      statusText: presence.statusText,
      lastSeenAgoSeconds: presence.lastSeenAgoSeconds,
      updatedAt: lastUpdated,
      sensors,
      summaryMetrics: buildDeviceSummaryMetrics(sensors)
    };
  }

  if (catalog.type === "server") {
    const server = appState.serverStatus || {};
    return {
      id: deviceId,
      type: catalog.type,
      online: Boolean(server.updatedAt),
      updatedAt: server.updatedAt,
      summaryMetrics: [
        { label: "CPU 占用", value: server.cpuCurrentUsage, unit: "%" },
        { label: "内存占用", value: server.memory?.usedPercent, unit: "%" }
      ],
      raw: server
    };
  }

  if (catalog.type === "weather") {
    const weather = appState.weather || {};
    const current = weather.current || {};
    return {
      id: deviceId,
      type: catalog.type,
      online: Boolean(weather.updatedAt),
      updatedAt: weather.updatedAt,
      summaryMetrics: [
        { label: "当前温度", value: current.temperature, unit: "°C" },
        { label: "当前湿度", value: current.humidity, unit: "%" }
      ],
      raw: weather
    };
  }

  return null;
}

function getVisibleDevices() {
  return Object.keys(deviceCatalog)
    .map((deviceId) => ({ catalog: deviceCatalog[deviceId], snapshot: getDeviceSnapshot(deviceId) }))
    .filter(({ catalog }) => appState.activeRoom === "all" || catalog.room === appState.activeRoom);
}

function getOnlineDeviceCount() {
  return Object.keys(deviceCatalog)
    .filter((deviceId) => {
      const type = deviceCatalog[deviceId].type;
      return type !== "server" && type !== "weather";
    })
    .map((deviceId) => getDeviceSnapshot(deviceId))
    .filter((snapshot) => snapshot?.online).length;
}

function renderRoomTabs() {
  els.roomTabs.innerHTML = roomConfigs
    .map((room) => `<button class="room-tab ${room.id === appState.activeRoom ? "active" : ""}" data-room="${room.id}">${room.name}</button>`)
    .join("");
  els.roomTabs.querySelectorAll("[data-room]").forEach((button) => {
    button.addEventListener("click", () => {
      appState.activeRoom = button.dataset.room;
      renderOverview();
    });
  });
}

function getUploadableRooms() {
  return roomConfigs.filter((room) => !["server", "outdoor"].includes(room.id));
}

function canUploadRoom(roomId) {
  return getUploadableRooms().some((room) => room.id === roomId);
}

function getRoomPhoto(roomId) {
  return appState.roomPhotos?.[roomId] || null;
}

function getOverviewTargetRoomId() {
  return appState.activeRoom;
}

function getRoomDisplayName(roomId) {
  return roomConfigs.find((room) => room.id === roomId)?.name || roomId;
}

function escapeAttribute(value) {
  return String(value ?? "")
    .replaceAll("&", "&amp;")
    .replaceAll("\"", "&quot;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;");
}

function renderRoomPhotoCard(roomId, photo, options = {}) {
  const roomName = getRoomDisplayName(roomId);
  const backgroundStyle = photo?.url
    ? ` style="background-image:url('${escapeAttribute(photo.url)}');"`
    : "";
  const helperText = photo?.uploadedAt
    ? `最近上传：${formatTime(photo.uploadedAt)}`
    : "上传你本地拍摄的照片后，这里会一直展示。";
  return `
    <article class="photo-showcase-card ${options.compact ? "is-compact" : ""}" data-photo-room="${roomId}">
      <div class="photo-showcase-media ${photo?.url ? "has-photo" : "is-empty"}"${backgroundStyle}>
        ${photo?.url ? "" : `<div class="photo-empty-copy">这里还没有 ${roomName} 的照片</div>`}
      </div>
      <div class="photo-showcase-copy">
        <div>
          <div class="photo-showcase-eyebrow">${roomName} 照片位</div>
          <div class="photo-showcase-subtitle">${helperText}</div>
        </div>
      </div>
    </article>
  `;
}

function renderRoomPhotoUploadToolbar(roomId, includeSelector = false) {
  if (!includeSelector && !canUploadRoom(roomId)) {
    return "";
  }
  return `
    <div class="photo-upload-toolbar">
      <div class="photo-upload-target">${getRoomDisplayName(roomId)}</div>
      <button class="ghost-btn" id="roomPhotoUploadBtn">上传照片</button>
    </div>
  `;
}

async function uploadRoomPhoto(roomId, file, button) {
  const reader = new FileReader();
  reader.onload = async () => {
    try {
      button.disabled = true;
      button.textContent = "上传中...";
      const response = await fetchJson("/api/room-photos/upload", {
        method: "POST",
        headers: {
          "Content-Type": "application/json"
        },
        body: JSON.stringify({
          roomId,
          fileName: file.name,
          dataUrl: String(reader.result || "")
        })
      });
      appState.roomPhotos[roomId] = response.photo || null;
      renderOverview();
    } catch (error) {
      window.alert(`上传失败：${error.message || "请稍后再试"}`);
      button.disabled = false;
      button.textContent = "上传照片";
    }
  };
  reader.readAsDataURL(file);
}

function bindRoomPhotoUploadEvents() {
  const button = document.getElementById("roomPhotoUploadBtn");
  if (!button) {
    return;
  }
  button.addEventListener("click", () => {
    const roomId = getOverviewTargetRoomId();
    const input = document.createElement("input");
    input.type = "file";
    input.accept = "image/png,image/jpeg,image/webp";
    input.addEventListener("change", () => {
      const file = input.files?.[0];
      if (!file) {
        return;
      }
      uploadRoomPhoto(roomId, file, button);
    }, { once: true });
    input.click();
  });
}

function renderRoomOverview() {
  const roomId = getOverviewTargetRoomId();
  if (["server", "outdoor"].includes(roomId)) {
    els.roomOverview.innerHTML = "";
    return;
  }
  els.roomOverview.innerHTML = `
    <section class="photo-gallery-card">
      <div class="photo-gallery-head">
        <div>
          <div class="photo-showcase-eyebrow">${getRoomDisplayName(roomId)} 照片位</div>
          <div class="photo-showcase-subtitle">${canUploadRoom(roomId) ? "" : "这个位置不提供上传入口，只展示已保存的照片。"}</div>
        </div>
        ${renderRoomPhotoUploadToolbar(roomId, false)}
      </div>
      ${renderRoomPhotoCard(roomId, getRoomPhoto(roomId))}
    </section>
  `;
  bindRoomPhotoUploadEvents();
}

function getStatusClass(snapshot) {
  return snapshot?.online ? "is-online" : "is-offline";
}

function getStatusText(snapshot) {
  return snapshot?.statusText || (snapshot?.online ? "在线" : "离线");
}

function renderDeviceGrid() {
  const visibleDevices = getVisibleDevices();
  els.deviceGrid.innerHTML = visibleDevices
    .map(({ catalog, snapshot }) => {
      const metrics = snapshot.summaryMetrics || [];
      const metricHtml = metrics.map((metric) => {
        const alertClass = metric.alertLevel ? ` metric-value-${metric.alertLevel}` : "";
        return `
          <div class="metric-pill">
            <div class="metric-label">${metric.label}</div>
            <div class="metric-value${alertClass}">${formatSensorMetricValue(metric.sensorKey, metric.key, metric.value, metric.unit)}</div>
          </div>
        `;
      }).join("");
      const deviceMeta = catalog.type === "iot-device"
        ? `<span>${getDeviceSensorCountLabel(snapshot)}</span>`
        : `<span>${catalog.type === "server" ? "系统设备" : "服务设备"}</span>`;
      const historyHint = getDeviceHistoryHint(snapshot, catalog);
      return `
        <article class="device-card ${catalog.accentClass}" data-open-device="${catalog.id}">
          <div class="device-card-header">
            <div class="device-icon">${catalog.icon}</div>
            <button class="device-open" data-open-device="${catalog.id}" aria-label="打开${catalog.title}">›</button>
          </div>
          <div class="device-title">${catalog.title}</div>
          <div class="device-subtitle">${catalog.subtitle}</div>
          <div class="device-metrics">${metricHtml || '<div class="metric-pill"><div class="metric-label">当前数据</div><div class="metric-value">--</div></div>'}</div>
          <div class="device-meta">
            ${catalog.type === "iot-device" ? `<span class="device-status ${getStatusClass(snapshot)}"><span class="status-dot"></span>${getStatusText(snapshot)}</span>` : ""}
            ${deviceMeta}
            ${historyHint}
            <span>${catalog.type === "iot-device" ? getDeviceUpdatedLabel(snapshot) : (snapshot.updatedAt ? `更新 ${formatTime(snapshot.updatedAt)}` : "等待数据")}</span>
          </div>
        </article>
      `;
    })
    .join("");

  els.deviceGrid.querySelectorAll("[data-open-device]").forEach((button) => {
    button.addEventListener("click", (event) => {
      event.preventDefault();
      event.stopPropagation();
      openDevice(button.dataset.openDevice);
    });
  });
}

function renderOverview() {
  const room = roomConfigs.find((item) => item.id === appState.activeRoom) || roomConfigs[0];
  els.locationName.textContent = appState.activeRoom === "all" ? "龟龟老板庭院站" : `${room.name} 实景照片与设备`;
  els.locationSubtitle.textContent = "";
  els.currentRoomLabel.textContent = room.name;
  els.onlineDeviceCount.textContent = String(getOnlineDeviceCount());
  els.lastRefreshText.textContent = appState.refreshAt ? `最近刷新：${formatTime(appState.refreshAt)}` : "正在读取网关数据...";
  renderRoomTabs();
  renderRoomOverview();
  renderDeviceGrid();
}

function getHistoryCacheKey(deviceId, sensorKey, query) {
  return `${deviceId}:${sensorKey}:${query.date || query.range}`;
}

function getVisibleHistoryPoints() {
  return appState.historyPoints.slice(appState.historyViewStart, appState.historyViewEnd);
}

function sanitizeDownloadFileName(value) {
  return String(value || "")
    .replace(/[<>:"/\\|?*\u0000-\u001f]/g, "-")
    .replace(/\s+/g, " ")
    .trim() || "export";
}

function getCurrentHistoryRangeLabel() {
  return appState.historyQuery.date || appState.historyQuery.range || "24h";
}

function toDateTimeLocalValue(input) {
  const date = input ? new Date(input) : new Date();
  if (!Number.isFinite(date.getTime())) {
    return "";
  }
  const year = date.getFullYear();
  const month = String(date.getMonth() + 1).padStart(2, "0");
  const day = String(date.getDate()).padStart(2, "0");
  const hour = String(date.getHours()).padStart(2, "0");
  const minute = String(date.getMinutes()).padStart(2, "0");
  return `${year}-${month}-${day}T${hour}:${minute}`;
}

function escapeAttribute(value) {
  return String(value ?? "")
    .replaceAll("&", "&amp;")
    .replaceAll("\"", "&quot;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;");
}

function buildMetricHistoryCsv(deviceId, sensorKey, metric, points) {
  const rows = [
    [
      "recorded_at",
      getMetricCsvColumnLabel(deviceId, sensorKey, metric),
      "sample_count"
    ].join(",")
  ];

  points.forEach((point) => {
    if (point?.[metric.key] == null) {
      return;
    }
    rows.push([
      point.recordedAt || new Date(point.tsMs).toISOString(),
      point[metric.key],
      point.sampleCount ?? 0
    ].join(","));
  });

  return `\ufeff${rows.join("\n")}\n`;
}

async function saveCsvWithPicker(fileName, csvText) {
  const csvBlob = new Blob([csvText], { type: "text/csv;charset=utf-8" });
  if (typeof window.showSaveFilePicker === "function" && window.isSecureContext) {
    const handle = await window.showSaveFilePicker({
      suggestedName: fileName,
      types: [
        {
          description: "CSV 文件",
          accept: {
            "text/csv": [".csv"]
          }
        }
      ]
    });
    const writable = await handle.createWritable();
    await writable.write(csvBlob);
    await writable.close();
    return { mode: "picker" };
  }

  const objectUrl = URL.createObjectURL(csvBlob);
  const link = document.createElement("a");
  link.href = objectUrl;
  link.download = fileName;
  document.body.appendChild(link);
  link.click();
  link.remove();
  window.setTimeout(() => URL.revokeObjectURL(objectUrl), 1000);
  return { mode: "download" };
}

async function exportVisibleMetricCsv(deviceId, sensorKey, metricKey) {
  const deviceTitle = deviceCatalog[deviceId]?.title || deviceId || "设备";
  const sensorTitle = getSensorDisplayTitle(deviceId, sensorKey, sensorCatalog[sensorKey]?.title || sensorKey || "传感器");
  const metric = (appState.historyMeta.metrics || []).find((item) => item.key === metricKey);
  if (!metric) {
    throw new Error("未找到当前图表指标");
  }

  const params = new URLSearchParams();
  params.set("series", sensorKey);
  const catalog = deviceCatalog[deviceId];
  (catalog?.historyDevices || [deviceId]).forEach((matchId) => params.append("device", matchId));
  params.set("metric", metricKey);
  if (appState.historyQuery.date) params.set("date", appState.historyQuery.date);
  else params.set("range", appState.historyQuery.range || "24h");

  const rawHistory = await fetchJson(`/api/sensor/history/raw?${params.toString()}`);
  const rawPoints = (rawHistory.points || []).filter((point) => point?.[metric.key] != null);
  if (!rawPoints.length) {
    throw new Error("当前时间范围内没有可导出的数据");
  }

  const rangeLabel = getCurrentHistoryRangeLabel();
  const fileName = sanitizeDownloadFileName(
    `${deviceTitle}_${sensorTitle}_${metric.label}_${rangeLabel}.csv`
  );
  const result = await saveCsvWithPicker(fileName, buildMetricHistoryCsv(deviceId, sensorKey, metric, rawPoints));
  return {
    fileName,
    rowCount: rawPoints.length,
    mode: result.mode
  };
}

function openDeviceExportDialog(deviceId) {
  const now = new Date();
  const start = new Date(now.getTime() - 24 * 60 * 60 * 1000);
  appState.exportDialog = {
    open: true,
    sensorKeys: [],
    startAt: toDateTimeLocalValue(start),
    endAt: toDateTimeLocalValue(now),
    submitting: false
  };
  renderExportDialog();
}

function closeDeviceExportDialog() {
  appState.exportDialog = {
    open: false,
    sensorKeys: [],
    startAt: "",
    endAt: "",
    submitting: false
  };
  renderExportDialog();
}

async function saveSensorAliases(deviceId, aliases) {
  return postJson("/api/device-sensor-aliases", {
    deviceId,
    aliases
  });
}

function renderExportDialog() {
  const root = document.getElementById("exportModalRoot");
  if (!root) return;

  if (!appState.exportDialog.open || !appState.activeDeviceId) {
    root.innerHTML = "";
    return;
  }

  const deviceId = appState.activeDeviceId;
  const catalog = deviceCatalog[deviceId];
  const snapshot = getDeviceSnapshot(deviceId);
  const sensors = snapshot?.sensors || [];
  root.innerHTML = `
    <div class="modal-backdrop" data-export-modal-close="mask">
      <div class="modal-card" role="dialog" aria-modal="true" aria-labelledby="multiExportTitle">
        <div class="modal-head">
          <div>
            <div class="detail-block-title" id="multiExportTitle">导出多传感器 CSV</div>
            <div class="detail-helper">当前设备：${escapeHtml(catalog?.title || deviceId)}</div>
          </div>
          <button class="ghost-btn" type="button" data-export-modal-close="button">关闭</button>
        </div>
        <div class="modal-section">
          <div class="modal-label">选择传感器</div>
          <div class="modal-sensor-grid">
            ${sensors.map((sensor) => `
              <label class="modal-check">
                <input
                  type="checkbox"
                  value="${escapeAttribute(sensor.key)}"
                  data-export-sensor
                  ${appState.exportDialog.sensorKeys.includes(sensor.key) ? "checked" : ""}
                />
                <span>${escapeHtml(sensor.title)}</span>
              </label>
            `).join("")}
          </div>
        </div>
        <div class="modal-section">
          <div class="modal-label">开始时间</div>
          <input class="date-input modal-input" id="multiExportStartAt" type="datetime-local" value="${escapeAttribute(appState.exportDialog.startAt)}" />
        </div>
        <div class="modal-section">
          <div class="modal-label">结束时间</div>
          <input class="date-input modal-input" id="multiExportEndAt" type="datetime-local" value="${escapeAttribute(appState.exportDialog.endAt)}" />
        </div>
        <div class="modal-actions">
          <button class="ghost-btn" type="button" id="multiExportSelectAllBtn">全选</button>
          <button class="ghost-btn" type="button" id="multiExportClearBtn">清空</button>
          <button class="range-btn active" type="button" id="multiExportSubmitBtn">
            ${appState.exportDialog.submitting ? "导出中..." : "导出 CSV"}
          </button>
        </div>
        <p class="footer-note" id="multiExportStatus">每一行会按设备原始上报时间合并所选传感器的数据。</p>
      </div>
    </div>
  `;
  bindExportDialogEvents();
}

function bindExportDialogEvents() {
  const root = document.getElementById("exportModalRoot");
  if (!root) return;

  root.querySelectorAll("[data-export-modal-close]").forEach((element) => {
    element.addEventListener("click", (event) => {
      if (element.dataset.exportModalClose === "mask" && event.target !== element) {
        return;
      }
      closeDeviceExportDialog();
    });
  });

  root.querySelectorAll("[data-export-sensor]").forEach((checkbox) => {
    checkbox.addEventListener("change", () => {
      appState.exportDialog.sensorKeys = Array.from(root.querySelectorAll("[data-export-sensor]:checked"))
        .map((input) => input.value);
    });
  });

  root.querySelector("#multiExportStartAt")?.addEventListener("change", (event) => {
    appState.exportDialog.startAt = event.target.value;
  });
  root.querySelector("#multiExportEndAt")?.addEventListener("change", (event) => {
    appState.exportDialog.endAt = event.target.value;
  });

  root.querySelector("#multiExportSelectAllBtn")?.addEventListener("click", () => {
    root.querySelectorAll("[data-export-sensor]").forEach((checkbox) => {
      checkbox.checked = true;
    });
    appState.exportDialog.sensorKeys = Array.from(root.querySelectorAll("[data-export-sensor]")).map((input) => input.value);
  });

  root.querySelector("#multiExportClearBtn")?.addEventListener("click", () => {
    root.querySelectorAll("[data-export-sensor]").forEach((checkbox) => {
      checkbox.checked = false;
    });
    appState.exportDialog.sensorKeys = [];
  });

  root.querySelector("#multiExportSubmitBtn")?.addEventListener("click", async () => {
    const statusEl = root.querySelector("#multiExportStatus");
    const sensorKeys = Array.from(root.querySelectorAll("[data-export-sensor]:checked")).map((input) => input.value);
    const startAt = root.querySelector("#multiExportStartAt")?.value || "";
    const endAt = root.querySelector("#multiExportEndAt")?.value || "";
    if (!sensorKeys.length) {
      if (statusEl) statusEl.textContent = "请至少勾选一个传感器。";
      return;
    }
    if (!startAt || !endAt) {
      if (statusEl) statusEl.textContent = "请选择完整的开始和结束时间。";
      return;
    }

    try {
      appState.exportDialog.submitting = true;
      appState.exportDialog.sensorKeys = sensorKeys;
      appState.exportDialog.startAt = startAt;
      appState.exportDialog.endAt = endAt;
      renderExportDialog();
      const response = await postJson("/api/export/history/multi", {
        deviceId: appState.activeDeviceId,
        sensorKeys,
        startAt,
        endAt
      });
      await saveCsvWithPicker(response.fileName, response.csvText);
      closeDeviceExportDialog();
      const historyStatus = document.getElementById("historyExportStatus");
      if (historyStatus) {
        historyStatus.textContent = `已导出：${response.fileName}（${response.rowCount} 行）`;
      }
    } catch (error) {
      appState.exportDialog.submitting = false;
      renderExportDialog();
      const nextStatusEl = document.getElementById("multiExportStatus");
      if (nextStatusEl) {
        nextStatusEl.textContent = `导出失败：${error.message}`;
      }
    }
  });
}

function resetChartZoom() {
  appState.historyViewStart = 0;
  appState.historyViewEnd = appState.historyPoints.length;
  appState.hoverIndexByMetric = {};
  renderHistoryPanels();
}

function updateHistoryHeader(data) {
  const titleEl = document.getElementById("historyTitle");
  const summaryEl = document.getElementById("historySummary");
  const legendEl = document.getElementById("historyLegend");
  if (!titleEl || !summaryEl || !legendEl) return;
  const activeSensor = getActiveSensorSnapshot();
  titleEl.textContent = activeSensor ? `${activeSensor.title} 历史曲线` : (data.label || "传感器历史曲线");
  summaryEl.textContent = (appState.historyMeta.metrics || []).map((metric) => {
    const stats = data.stats?.[metric.key];
    if (!stats?.sampleCount) return `${metric.label}：等待上报`;
    return `${metric.label}：均值 ${stats.avg}${metric.unit}，范围 ${stats.min}${metric.unit} ~ ${stats.max}${metric.unit}`;
  }).join("；");
  legendEl.innerHTML = (appState.historyMeta.metrics || []).map((metric) => `<span>${metric.label}</span>`).join(" · ");
}

function renderHistoryLoading(sensorKey) {
  const activeSensor = getActiveSensorSnapshot();
  appState.historyMeta = {
    metrics: activeSensor?.metrics || sensorCatalog[sensorKey]?.metrics || [],
    bucketMinutes: 10,
    stats: {}
  };
  appState.historyPoints = [];
  appState.historyViewStart = 0;
  appState.historyViewEnd = 0;
  const historyPanelsEl = document.getElementById("historyPanels");
  if (!historyPanelsEl) return;
  historyPanelsEl.innerHTML = appState.historyMeta.metrics.map((metric) => `
    <div class="history-panel">
      <div class="history-panel-head">
        <div class="history-panel-title">${metric.label}</div>
        <div class="history-panel-meta">加载中</div>
      </div>
      <div class="chart-wrap"></div>
      <div class="history-panel-note">正在读取 ${metric.label} 的历史数据。</div>
    </div>
  `).join("");
}

function buildMetricScale(metric, points) {
  const values = points.map((point) => point[metric.key]).filter((value) => value != null);
  if (!values.length) return null;
  const minValue = Math.min(...values);
  const maxValue = Math.max(...values);
  const padding = metric.unit === "lux" ? Math.max(10, (maxValue - minValue) * 0.1) : Math.max(1, (maxValue - minValue) * 0.12);
  return { min: minValue - padding, max: maxValue + padding };
}

function getMetricExtrema(points, metric) {
  let minPoint = null;
  let maxPoint = null;
  points.forEach((point) => {
    const value = point?.[metric.key];
    if (value == null || Number.isNaN(Number(value))) return;
    if (!minPoint || Number(value) < Number(minPoint[metric.key])) {
      minPoint = point;
    }
    if (!maxPoint || Number(value) > Number(maxPoint[metric.key])) {
      maxPoint = point;
    }
  });
  return { minPoint, maxPoint };
}

function getHistoryGapThresholdMs() {
  const bucketMinutes = Number(appState.historyMeta.bucketMinutes || 10);
  return Math.max(bucketMinutes * 60 * 1000 * 3, 2 * 60 * 1000);
}

function getTimeLabel(tsMs, startTs, endTs) {
  const totalSpan = Math.max(endTs - startTs, 1);
  const date = new Date(tsMs);
  const options = totalSpan <= 2 * 60 * 60 * 1000
    ? { hour: "2-digit", minute: "2-digit", hour12: false }
    : totalSpan <= 3 * 24 * 60 * 60 * 1000
      ? { month: "numeric", day: "numeric", hour: "2-digit", minute: "2-digit", hour12: false }
      : { month: "numeric", day: "numeric", hour12: false };
  return date.toLocaleString("zh-CN", options);
}

function getHistoryTickCount(startTs, endTs) {
  const totalSpan = Math.max(endTs - startTs, 1);
  if (totalSpan <= 2 * 60 * 60 * 1000) return 7;
  if (totalSpan <= 24 * 60 * 60 * 1000) return 8;
  if (totalSpan <= 3 * 24 * 60 * 60 * 1000) return 7;
  return 6;
}

function getTimeLabelLines(tsMs, startTs, endTs, isMobile = false) {
  const totalSpan = Math.max(endTs - startTs, 1);
  const date = new Date(tsMs);
  if (isMobile) {
    if (totalSpan <= 24 * 60 * 60 * 1000) {
      return [
        date.toLocaleDateString("zh-CN", { month: "numeric", day: "numeric" }),
        date.toLocaleTimeString("zh-CN", { hour: "2-digit", minute: "2-digit", hour12: false })
      ];
    }
    return [
      date.toLocaleDateString("zh-CN", { month: "numeric", day: "numeric" }),
      date.toLocaleTimeString("zh-CN", { hour: "2-digit", minute: "2-digit", hour12: false })
    ];
  }

  return [getTimeLabel(tsMs, startTs, endTs)];
}

function getHistoryMinVisible(totalPoints) {
  if (totalPoints <= 12) return totalPoints;
  return Math.max(6, Math.min(24, Math.floor(totalPoints * 0.08)));
}

function getHistoryZoomStep(visibleCount) {
  return Math.max(1, Math.round(visibleCount * 0.18));
}

function getCanvasSize(canvas) {
  const rect = canvas.getBoundingClientRect();
  const cssWidth = Math.max(Math.round(rect.width || 520), 320);
  const cssHeight = Math.max(Math.round(rect.height || 260), 220);
  const dpr = Math.max(1, window.devicePixelRatio || 1);
  const pixelWidth = Math.round(cssWidth * dpr);
  const pixelHeight = Math.round(cssHeight * dpr);
  if (canvas.width !== pixelWidth || canvas.height !== pixelHeight) {
    canvas.width = pixelWidth;
    canvas.height = pixelHeight;
  }
  return { width: cssWidth, height: cssHeight, dpr };
}

function getSelectionSummaryElement(tooltipEl) {
  return tooltipEl?.closest(".history-panel")?.querySelector(".history-selection") || null;
}

function showSelectionSummary(selectionEl, metric, point) {
  if (!selectionEl || !point) return;
  const valueText = metric.unit === "lux"
    ? `${Math.round(Number(point[metric.key]))} ${metric.unit}`
    : `${Number(point[metric.key]).toFixed(1)} ${metric.unit}`;
  selectionEl.innerHTML = `
    <div class="history-selection-time">${formatPointTime(point.tsMs)}</div>
    <div class="history-selection-value">${valueText}</div>
  `;
  selectionEl.classList.add("visible");
}

function hideSelectionSummary(selectionEl) {
  if (!selectionEl) return;
  selectionEl.classList.remove("visible");
}

function showTooltip(tooltipEl, metric, point, position) {
  if (!tooltipEl) return;
  const selectionEl = getSelectionSummaryElement(tooltipEl);
  const valueText = metric.unit === "lux"
    ? `${Math.round(Number(point[metric.key]))} ${metric.unit}`
    : `${Number(point[metric.key]).toFixed(1)} ${metric.unit}`;
  if (position.mobileLocked) {
    hideTooltip(tooltipEl);
    showSelectionSummary(selectionEl, metric, point);
    return;
  }
  hideSelectionSummary(selectionEl);
  tooltipEl.innerHTML = `<strong>${formatPointTime(point.tsMs)}</strong><br />${valueText}`;
  const mobileLocked = Boolean(position.mobileLocked);
  tooltipEl.classList.toggle("mobile-locked", mobileLocked);
  tooltipEl.style.left = `${position.x}px`;
  tooltipEl.style.top = `${position.y}px`;
  tooltipEl.classList.add("visible");
  const containerWidth = position.containerWidth || tooltipEl.parentElement?.clientWidth || 0;
  const tipWidth = tooltipEl.offsetWidth || 0;
  if (mobileLocked) {
    const safeHalfWidth = tipWidth > 0 ? (tipWidth / 2) + 6 : 0;
    const clampedX = containerWidth > 0
      ? Math.max(safeHalfWidth, Math.min(containerWidth - safeHalfWidth, position.x))
      : position.x;
    tooltipEl.style.left = `${clampedX}px`;
    tooltipEl.style.setProperty("--tip-shift-x", "-50%");
    return;
  }
  const shouldPlaceLeft = !position.mobileLocked && (
    position.preferLeft
    || (containerWidth > 0 && tipWidth > 0 && position.x + 14 + tipWidth > containerWidth - 6)
  );
  tooltipEl.style.setProperty("--tip-shift-x", shouldPlaceLeft ? "calc(-100% - 14px)" : "14px");
}

function hideTooltip(tooltipEl) {
  if (!tooltipEl) return;
  hideSelectionSummary(getSelectionSummaryElement(tooltipEl));
  tooltipEl.classList.remove("visible", "mobile-locked");
}

function getCanvasPlotBounds(canvas) {
  const rect = canvas.getBoundingClientRect();
  const fallbackLeft = 0;
  const fallbackRight = rect.width || 0;
  const bounds = canvas._plotBounds || {};
  return {
    left: Number.isFinite(bounds.left) ? bounds.left : fallbackLeft,
    right: Number.isFinite(bounds.right) ? bounds.right : fallbackRight
  };
}

function resolveNearestHitPoint(canvas, clientX) {
  const rect = canvas.getBoundingClientRect();
  const { left: plotLeft, right: plotRight } = getCanvasPlotBounds(canvas);
  const rawX = clientX - rect.left;
  const x = Math.max(plotLeft, Math.min(plotRight, rawX));
  const hitPoints = canvas._hitPoints || [];
  let nearest = null;
  let nearestDistance = Infinity;

  hitPoints.forEach((item) => {
    const distance = Math.abs(item.x - x);
    if (distance < nearestDistance) {
      nearest = item;
      nearestDistance = distance;
    }
  });

  if (!nearest) {
    return null;
  }
  if (!isMobileChartInteraction() && nearestDistance > 44) {
    return null;
  }
  return nearest;
}

function updateMetricHover(metric, canvas, tooltipEl, clientX) {
  const nearest = resolveNearestHitPoint(canvas, clientX);
  if (!nearest || nearest.point[metric.key] == null) {
    appState.hoverIndexByMetric[metric.key] = null;
    appState.hoverGuideXByMetric[metric.key] = null;
    hideTooltip(tooltipEl);
    drawMetricChart(metric, canvas, tooltipEl);
    return;
  }

  const rect = canvas.getBoundingClientRect();
  const { left: plotLeft, right: plotRight } = getCanvasPlotBounds(canvas);
  const rawX = clientX - rect.left;
  const clampedX = Math.max(plotLeft, Math.min(plotRight, rawX));
  const mobileLocked = isMobileChartInteraction();
  appState.hoverIndexByMetric[metric.key] = nearest.index;
  appState.hoverGuideXByMetric[metric.key] = clampedX;
  drawMetricChart(metric, canvas, tooltipEl);
  showTooltip(tooltipEl, metric, nearest.point, {
    x: clampedX,
    y: mobileLocked ? 0 : 34,
    preferLeft: clampedX > plotRight - 140,
    containerWidth: rect.width,
    mobileLocked
  });
}

function setMetricHoverByHitPoint(metric, canvas, tooltipEl, hitPoint) {
  if (!hitPoint || hitPoint.point?.[metric.key] == null) {
    clearMetricHover(metric, canvas, tooltipEl);
    return;
  }
  const rect = canvas.getBoundingClientRect();
  const { left: plotLeft, right: plotRight } = getCanvasPlotBounds(canvas);
  const clampedX = Math.max(plotLeft, Math.min(plotRight, hitPoint.x));
  const mobileLocked = isMobileChartInteraction();
  appState.hoverIndexByMetric[metric.key] = hitPoint.index;
  appState.hoverGuideXByMetric[metric.key] = clampedX;
  drawMetricChart(metric, canvas, tooltipEl);
  showTooltip(tooltipEl, metric, hitPoint.point, {
    x: clampedX,
    y: mobileLocked ? 0 : 34,
    preferLeft: clampedX > plotRight - 140,
    containerWidth: rect.width,
    mobileLocked
  });
}

function clearMetricHover(metric, canvas, tooltipEl) {
  appState.hoverIndexByMetric[metric.key] = null;
  appState.hoverGuideXByMetric[metric.key] = null;
  hideTooltip(tooltipEl);
  drawMetricChart(metric, canvas, tooltipEl);
}

function getClientXFromTouchEvent(event) {
  const touch = event.touches?.[0] || event.changedTouches?.[0];
  return touch?.clientX ?? null;
}

function isMobileChartInteraction() {
  return window.matchMedia?.("(pointer: coarse)")?.matches || window.innerWidth <= 768;
}

function isDirectChartSelectionEnabled() {
  return !isMobileChartInteraction();
}

function bindMetricCanvasInteraction(metric, canvas, tooltipEl) {
  if (!isDirectChartSelectionEnabled()) {
    return;
  }

  canvas.addEventListener("mouseleave", () => {
    clearMetricHover(metric, canvas, tooltipEl);
  });

  canvas.addEventListener("mousemove", (event) => {
    updateMetricHover(metric, canvas, tooltipEl, event.clientX);
  });

  canvas.addEventListener("pointerdown", (event) => {
    if (event.pointerType === "touch" || event.pointerType === "pen") {
      updateMetricHover(metric, canvas, tooltipEl, event.clientX);
    }
  });

  canvas.addEventListener("pointermove", (event) => {
    if (event.pointerType === "touch" || event.pointerType === "pen") {
      event.preventDefault();
      updateMetricHover(metric, canvas, tooltipEl, event.clientX);
    }
  }, { passive: false });

  canvas.addEventListener("pointerup", (event) => {
    if (event.pointerType === "touch" || event.pointerType === "pen") {
      updateMetricHover(metric, canvas, tooltipEl, event.clientX);
    }
  });

  canvas.addEventListener("pointercancel", () => {
    clearMetricHover(metric, canvas, tooltipEl);
  });

  canvas.addEventListener("touchstart", (event) => {
    const clientX = getClientXFromTouchEvent(event);
    if (clientX == null) return;
    event.preventDefault();
    updateMetricHover(metric, canvas, tooltipEl, clientX);
  }, { passive: false });

  canvas.addEventListener("touchmove", (event) => {
    const clientX = getClientXFromTouchEvent(event);
    if (clientX == null) return;
    event.preventDefault();
    updateMetricHover(metric, canvas, tooltipEl, clientX);
  }, { passive: false });

  canvas.addEventListener("touchend", (event) => {
    const clientX = getClientXFromTouchEvent(event);
    if (clientX == null) return;
    updateMetricHover(metric, canvas, tooltipEl, clientX);
  }, { passive: true });
}

function drawMetricChart(metric, canvas, tooltipEl) {
  const context = canvas.getContext("2d");
  const { width, height, dpr } = getCanvasSize(canvas);
  context.setTransform(dpr, 0, 0, dpr, 0, 0);
  const points = getVisibleHistoryPoints();
  const isMobile = isMobileChartInteraction();
  const padding = isMobile
    ? { top: 10, right: 12, bottom: 62, left: 48 }
    : { top: 18, right: 20, bottom: 44, left: 64 };
  const plotWidth = width - padding.left - padding.right;
  const plotHeight = height - padding.top - padding.bottom;
  const scale = buildMetricScale(metric, points);
  const startTs = points[0]?.tsMs ?? Date.now();
  const endTs = points[points.length - 1]?.tsMs ?? startTs;
  const timeSpan = Math.max(endTs - startTs, 1);
  const gapThresholdMs = getHistoryGapThresholdMs();
  const validPoints = points.map((point, index) => ({ point, index })).filter(({ point }) => point[metric.key] != null);

  context.clearRect(0, 0, width, height);
  context.strokeStyle = "rgba(124, 111, 98, 0.18)";
  context.lineWidth = 1;
  for (let i = 0; i <= 4; i += 1) {
    const y = padding.top + (plotHeight / 4) * i;
    context.beginPath();
    context.moveTo(padding.left, y);
    context.lineTo(width - padding.right, y);
    context.stroke();
  }

  function toX(tsMs) {
    if (points.length <= 1 || timeSpan <= 0) return padding.left + plotWidth / 2;
    return padding.left + ((tsMs - startTs) / timeSpan) * plotWidth;
  }

  function toY(value) {
    if (!scale) return padding.top + plotHeight / 2;
    const range = Math.max(scale.max - scale.min, 1);
    return padding.top + plotHeight - ((value - scale.min) / range) * plotHeight;
  }

  if (scale) {
    context.fillStyle = "#7b6f62";
    context.font = `${isMobile ? 11 : 12}px Segoe UI`;
    context.textAlign = "right";
    for (let i = 0; i <= 4; i += 1) {
      const y = padding.top + (plotHeight / 4) * i + 4;
      const value = scale.max - ((scale.max - scale.min) * i) / 4;
      const label = metric.unit === "lux"
        ? `${Math.round(value)}`
        : metric.unit === "%RH" || metric.unit === "%"
          ? `${value.toFixed(0)}`
          : `${value.toFixed(1)}`;
      context.fillText(label, padding.left - 8, y);
      // 左侧刻度短线
      context.strokeStyle = "rgba(100, 110, 130, 0.30)";
      context.lineWidth = 1;
      context.beginPath();
      context.moveTo(padding.left - 4, y - 4);
      context.lineTo(padding.left, y - 4);
      context.stroke();
    }
  }

  // 轴边框
  context.strokeStyle = "rgba(100, 110, 130, 0.30)";
  context.lineWidth = 1;
  context.setLineDash([]);
  context.beginPath();
  context.moveTo(padding.left, padding.top);
  context.lineTo(padding.left, padding.top + plotHeight);
  context.lineTo(width - padding.right, padding.top + plotHeight);
  context.stroke();

  context.strokeStyle = metric.color || "#395dff";
  context.lineWidth = 3;
  for (let lineIndex = 1; lineIndex < validPoints.length; lineIndex += 1) {
    const previous = validPoints[lineIndex - 1].point;
    const current = validPoints[lineIndex].point;
    context.setLineDash(current.tsMs - previous.tsMs > gapThresholdMs ? [8, 8] : []);
    context.beginPath();
    context.moveTo(toX(previous.tsMs), toY(previous[metric.key]));
    context.lineTo(toX(current.tsMs), toY(current[metric.key]));
    context.stroke();
  }
  context.setLineDash([]);

  if (!isMobile) {
    validPoints.forEach(({ point }) => {
      context.beginPath();
      context.arc(toX(point.tsMs), toY(point[metric.key]), 3.5, 0, Math.PI * 2);
      context.fillStyle = metric.color || "#395dff";
      context.fill();
    });
  }

  const hoverIndex = appState.hoverIndexByMetric[metric.key];
  if (hoverIndex != null && points[hoverIndex]?.[metric.key] != null) {
    const hoverPoint = points[hoverIndex];
    const hoverGuideX = appState.hoverGuideXByMetric[metric.key];
    const selectedPointX = toX(hoverPoint.tsMs);
    const selectedPointY = toY(hoverPoint[metric.key]);
    const hoverX = !isMobile && Number.isFinite(hoverGuideX)
      ? Math.max(padding.left, Math.min(width - padding.right, hoverGuideX))
      : selectedPointX;
    if (!isMobile) {
      context.save();
      context.strokeStyle = "rgba(67, 84, 111, 0.45)";
      context.lineWidth = 1.25;
      context.setLineDash([6, 6]);
      context.beginPath();
      context.moveTo(hoverX, padding.top);
      context.lineTo(hoverX, padding.top + plotHeight);
      context.stroke();
      context.restore();
    }

    context.beginPath();
    context.arc(selectedPointX, selectedPointY, isMobile ? 6.5 : 5, 0, Math.PI * 2);
    context.fillStyle = metric.color || "#395dff";
    context.fill();
    context.beginPath();
    context.arc(selectedPointX, selectedPointY, isMobile ? 9.5 : 7.5, 0, Math.PI * 2);
    context.strokeStyle = "rgba(255, 255, 255, 0.92)";
    context.lineWidth = isMobile ? 3 : 2.5;
    context.stroke();
  }

  if (isMobile) {
    context.fillStyle = "rgba(246, 248, 252, 0.98)";
    context.fillRect(padding.left - 6, padding.top + plotHeight + 8, plotWidth + 12, height - (padding.top + plotHeight + 8));
  }

  context.fillStyle = "#7b6f62";
  context.font = `${isMobile ? 11 : 12}px Segoe UI`;
  context.textAlign = "center";
  const tickCount = Math.min(
    isMobile ? 4 : getHistoryTickCount(startTs, endTs),
    Math.max(points.length, 2)
  );
  for (let i = 0; i < tickCount; i += 1) {
    const ratio = tickCount === 1 ? 0 : i / Math.max(tickCount - 1, 1);
    const tickTs = startTs + timeSpan * ratio;
    const x = toX(tickTs);
    context.textAlign = i === 0 ? "left" : i === tickCount - 1 ? "right" : "center";
    const labelLines = getTimeLabelLines(tickTs, startTs, endTs, isMobile);
    if (isMobile) {
      context.fillText(labelLines[0], x, height - 30);
      context.fillText(labelLines[1] || "", x, height - 14);
    } else {
      context.fillText(labelLines[0], x, height - 8);
    }
    // X 轴刻度短线
    context.strokeStyle = "rgba(100, 110, 130, 0.30)";
    context.lineWidth = 1;
    context.setLineDash([]);
    context.beginPath();
    context.moveTo(x, padding.top + plotHeight);
    context.lineTo(x, padding.top + plotHeight + 4);
    context.stroke();
  }

  canvas._plotBounds = {
    left: padding.left,
    right: width - padding.right
  };
  canvas._hitPoints = validPoints.map(({ point, index }) => ({ index, x: toX(point.tsMs), point }));
}

function renderHistoryPanels() {
  const historyPanelsEl = document.getElementById("historyPanels");
  if (!historyPanelsEl) return;
  historyPanelsEl.innerHTML = "";
  const visiblePoints = getVisibleHistoryPoints();
  const isMobile = isMobileChartInteraction();
  (appState.historyMeta.metrics || []).forEach((metric) => {
    const panel = document.createElement("div");
    panel.className = "history-panel";
    panel.innerHTML = `
      <div class="history-panel-head">
        <div class="history-panel-title">${metric.label}</div>
        <div class="history-panel-meta">${metric.unit}</div>
      </div>
      <div class="history-extrema"></div>
      <div class="history-selection"></div>
      <div class="chart-wrap">
        <canvas width="520" height="280"></canvas>
        <div class="chart-tip"></div>
      </div>
      <div class="chart-scrubber-wrap">
        <input class="chart-scrubber" type="range" min="0" max="0" step="1" value="0" />
      </div>
      <div class="history-panel-note"></div>
    `;

    const canvas = panel.querySelector("canvas");
    const tooltipEl = panel.querySelector(".chart-tip");
    const extremaEl = panel.querySelector(".history-extrema");
    const noteEl = panel.querySelector(".history-panel-note");
    const scrubberEl = panel.querySelector(".chart-scrubber");
    const metricVisiblePoints = visiblePoints.filter((point) => point[metric.key] != null);
    const { minPoint, maxPoint } = getMetricExtrema(visiblePoints, metric);
    extremaEl.innerHTML = metricVisiblePoints.length ? `
      <div class="history-extrema-item">
        <div class="history-extrema-label">最低</div>
        <div class="history-extrema-value">${formatMetricValue(minPoint?.[metric.key], metric.unit)}</div>
        <div class="history-extrema-time">${formatPointTime(minPoint.tsMs)}</div>
      </div>
      <div class="history-extrema-item">
        <div class="history-extrema-label">最高</div>
        <div class="history-extrema-value">${formatMetricValue(maxPoint?.[metric.key], metric.unit)}</div>
        <div class="history-extrema-time">${formatPointTime(maxPoint.tsMs)}</div>
      </div>
    ` : `
      <div class="history-extrema-item">
        <div class="history-extrema-label">历史极值</div>
        <div class="history-extrema-time">暂时还没有可展示的数据点。</div>
      </div>
    `;
    drawMetricChart(metric, canvas, tooltipEl);
    const sampleCount = appState.historyPoints.filter((point) => point[metric.key] != null).length;
    noteEl.textContent = sampleCount
      ? `${metric.label} 共 ${sampleCount} 个历史点。${isMobile ? "" : "双击可重置缩放，滚轮可缩放。"}`
      : `${metric.label} 正在等待上报。`;
    const hitPoints = canvas._hitPoints || [];
    if (scrubberEl) {
      if (hitPoints.length) {
        scrubberEl.max = String(hitPoints.length - 1);
        scrubberEl.value = String(hitPoints.length - 1);
        scrubberEl.disabled = false;
      } else {
        scrubberEl.max = "0";
        scrubberEl.value = "0";
        scrubberEl.disabled = true;
      }
      const syncScrubberHover = () => {
        if (!hitPoints.length) return;
        const hitPoint = hitPoints[Math.max(0, Math.min(hitPoints.length - 1, Number(scrubberEl.value) || 0))];
        setMetricHoverByHitPoint(metric, canvas, tooltipEl, hitPoint);
      };
      scrubberEl.addEventListener("input", syncScrubberHover);
      scrubberEl.addEventListener("change", syncScrubberHover);
      if (hitPoints.length && isMobileChartInteraction()) {
        syncScrubberHover();
      }
    }

    canvas.addEventListener("dblclick", () => resetChartZoom());
    canvas.addEventListener("wheel", (event) => {
      if (appState.historyPoints.length <= 2) return;
      event.preventDefault();
      const visibleCount = appState.historyViewEnd - appState.historyViewStart;
      const minVisible = getHistoryMinVisible(appState.historyPoints.length);
      const maxVisible = appState.historyPoints.length;
      const direction = Math.sign(event.deltaY);
      let nextVisible = visibleCount + direction * getHistoryZoomStep(visibleCount);
      nextVisible = Math.max(minVisible, Math.min(maxVisible, nextVisible));
      const rect = canvas.getBoundingClientRect();
      const ratio = Math.min(1, Math.max(0, (event.clientX - rect.left) / rect.width));
      const centerIndex = appState.historyViewStart + Math.round(visibleCount * ratio);
      let nextStart = centerIndex - Math.round(nextVisible * ratio);
      let nextEnd = nextStart + nextVisible;
      if (nextStart < 0) {
        nextStart = 0;
        nextEnd = nextVisible;
      }
      if (nextEnd > appState.historyPoints.length) {
        nextEnd = appState.historyPoints.length;
        nextStart = Math.max(0, nextEnd - nextVisible);
      }
      appState.historyViewStart = nextStart;
      appState.historyViewEnd = nextEnd;
      appState.hoverIndexByMetric = {};
      appState.hoverGuideXByMetric = {};
      renderHistoryPanels();
    }, { passive: false });

    bindMetricCanvasInteraction(metric, canvas, tooltipEl);

    historyPanelsEl.appendChild(panel);
  });
}

function drawSimpleChart(context, canvas, seriesList, options = {}) {
  const rect = canvas.getBoundingClientRect();
  const width = Math.max(Math.round(rect.width || canvas.width || 940), 320);
  const height = Math.max(Math.round(rect.height || canvas.height || 280), 220);
  const dpr = Math.max(1, window.devicePixelRatio || 1);
  const pixelWidth = Math.round(width * dpr);
  const pixelHeight = Math.round(height * dpr);
  if (canvas.width !== pixelWidth || canvas.height !== pixelHeight) {
    canvas.width = pixelWidth;
    canvas.height = pixelHeight;
  }
  context.setTransform(dpr, 0, 0, dpr, 0, 0);
  context.clearRect(0, 0, width, height);

  const padding = { top: 18, right: 20, bottom: 44, left: 58 };
  const plotWidth = width - padding.left - padding.right;
  const plotHeight = height - padding.top - padding.bottom;

  const allValues = seriesList.flatMap((series) => series.points.map((point) => point.value).filter((value) => value != null));

  if (!allValues.length) {
    context.fillStyle = "#7b6f62";
    context.font = "14px Segoe UI";
    context.textAlign = "left";
    context.fillText("还没有监控样本，等一会儿就会长出曲线。", padding.left + 8, height / 2);
    return;
  }

  const minValue = options.min ?? Math.min(...allValues);
  const maxValue = options.max ?? Math.max(...allValues);
  const extra = Math.max(1, (maxValue - minValue) * 0.12);
  const finalMin = options.lockMin ?? minValue - extra;
  const finalMax = options.lockMax ?? maxValue + extra;
  const range = Math.max(finalMax - finalMin, 1);
  const unit = options.unit || "";

  // 从数据中取时间戳范围
  const allTsMs = seriesList.flatMap((s) => s.points.map((p) => p.tsMs)).filter(Boolean);
  const startTs = allTsMs.length ? Math.min(...allTsMs) : 0;
  const endTs = allTsMs.length ? Math.max(...allTsMs) : 0;
  const timeSpan = Math.max(endTs - startTs, 1);

  function toX(tsMs) {
    if (!tsMs || timeSpan <= 0) return padding.left + plotWidth / 2;
    return padding.left + ((tsMs - startTs) / timeSpan) * plotWidth;
  }

  function toY(value) {
    return padding.top + plotHeight - ((value - finalMin) / range) * plotHeight;
  }

  // Y 轴刻度线 + 网格 + 标签（含单位）
  context.font = "12px Segoe UI";
  for (let i = 0; i <= 4; i += 1) {
    const y = padding.top + (plotHeight / 4) * i;
    const value = finalMax - ((finalMax - finalMin) * i) / 4;
    // 网格线
    context.strokeStyle = "rgba(124, 111, 98, 0.18)";
    context.lineWidth = 1;
    context.setLineDash([]);
    context.beginPath();
    context.moveTo(padding.left, y);
    context.lineTo(width - padding.right, y);
    context.stroke();
    // 刻度标签
    const label = unit === "%" ? `${value.toFixed(0)}%` : (value.toFixed(1) + (unit ? ` ${unit}` : ""));
    context.fillStyle = "#7b6f62";
    context.textAlign = "right";
    context.fillText(label, padding.left - 6, y + 4);
    // 左侧刻度短线
    context.strokeStyle = "rgba(100, 110, 130, 0.30)";
    context.beginPath();
    context.moveTo(padding.left - 4, y);
    context.lineTo(padding.left, y);
    context.stroke();
  }

  // 轴边框（左侧纵轴 + 底部横轴）
  context.strokeStyle = "rgba(100, 110, 130, 0.30)";
  context.lineWidth = 1;
  context.beginPath();
  context.moveTo(padding.left, padding.top);
  context.lineTo(padding.left, padding.top + plotHeight);
  context.lineTo(width - padding.right, padding.top + plotHeight);
  context.stroke();

  // X 轴时间标签 + 刻度短线
  if (allTsMs.length >= 2) {
    const xTickCount = width > 600 ? 6 : 4;
    context.fillStyle = "#7b6f62";
    context.font = "12px Segoe UI";
    for (let i = 0; i <= xTickCount; i += 1) {
      const ratio = i / xTickCount;
      const tickTs = startTs + timeSpan * ratio;
      const x = padding.left + ratio * plotWidth;
      const date = new Date(tickTs);
      const label = timeSpan > 12 * 3600 * 1000
        ? date.toLocaleString("zh-CN", { month: "numeric", day: "numeric", hour: "2-digit", minute: "2-digit", hour12: false })
        : date.toLocaleString("zh-CN", { hour: "2-digit", minute: "2-digit", hour12: false });
      context.textAlign = i === 0 ? "left" : i === xTickCount ? "right" : "center";
      context.fillText(label, x, height - 8);
      // 刻度短线
      context.strokeStyle = "rgba(100, 110, 130, 0.30)";
      context.lineWidth = 1;
      context.beginPath();
      context.moveTo(x, padding.top + plotHeight);
      context.lineTo(x, padding.top + plotHeight + 4);
      context.stroke();
    }
  }

  // 折线
  seriesList.forEach((series) => {
    const values = series.points.filter((point) => point.value != null);
    if (!values.length) return;
    context.beginPath();
    let started = false;
    series.points.forEach((point) => {
      if (point.value == null) return;
      const x = toX(point.tsMs);
      const y = toY(point.value);
      if (!started) {
        context.moveTo(x, y);
        started = true;
      } else {
        context.lineTo(x, y);
      }
    });
    context.strokeStyle = series.color;
    context.lineWidth = 2.2;
    context.setLineDash([]);
    context.stroke();
  });
}

async function refreshSensorHistory(deviceId, sensorKey) {
  renderHistoryLoading(sensorKey);
  const params = new URLSearchParams();
  params.set("series", sensorKey);
  const catalog = deviceCatalog[deviceId];
  (catalog?.historyDevices || [deviceId]).forEach((matchId) => params.append("device", matchId));
  if (appState.historyQuery.date) params.set("date", appState.historyQuery.date);
  else params.set("range", appState.historyQuery.range);

  const cacheKey = getHistoryCacheKey(deviceId, sensorKey, appState.historyQuery);
  let data = appState.historyCache.get(cacheKey);
  if (!data) {
    data = await fetchJson(`/api/sensor/history?${params.toString()}`);
    appState.historyCache.set(cacheKey, data);
  }
  appState.historyMeta = { metrics: data.metrics || [], bucketMinutes: data.bucketMinutes || 10, stats: data.stats || {} };
  appState.historyPoints = (data.points || []).slice().sort((a, b) => a.tsMs - b.tsMs);
  appState.historyViewStart = 0;
  appState.historyViewEnd = appState.historyPoints.length;
  appState.hoverIndexByMetric = {};
  appState.hoverGuideXByMetric = {};
  updateHistoryHeader(data);
  renderHistoryPanels();
}

function getExportQueryPayload(deviceId, sensorKey, metricKey = null, rangeOverride = null) {
  const payload = {
    deviceId,
    sensorKey
  };
  if (metricKey) {
    payload.metricKey = metricKey;
  }
  if (rangeOverride === "all") {
    payload.range = "all";
    return payload;
  }
  if (appState.historyQuery.date) {
    payload.date = appState.historyQuery.date;
  } else {
    payload.range = rangeOverride || appState.historyQuery.range || "24h";
  }
  return payload;
}

async function exportHistoryCsv(payload) {
  return postJson("/api/export/history", payload);
}

function getDevicePagePriority(catalog, page) {
  if (page.kind === "control") {
    return 999;
  }
  if (catalog?.id === "yard-01") {
    if (page.sensorKey === "shtc3") return 0;
    if (page.sensorKey === "battery") return 1;
    if (page.sensorKey === "max17043") return 2;
    if (page.sensorKey === "ina226") return 3;
  }
  if (catalog?.id === "study-01") {
    if (page.sensorKey === "dht11") return 0;
    if (page.sensorKey === "max17043") return 1;
    if (page.sensorKey === "ina226") return 2;
  }
  return 100;
}

function getDevicePages(catalog, snapshot) {
  const sensorByKey = new Map(snapshot.sensors.map((sensor) => [sensor.key, sensor]));
  const sensorPages = snapshot.sensors
    .map((sensor) => ({
      key: `sensor:${sensor.key}`,
      label: `${sensor.icon} ${sensor.title}`,
      kind: "sensor",
      sensorKey: sensor.key,
      sensorTitle: sensor.title
    }))
    .sort((a, b) => {
      const priorityDiff = getDevicePagePriority(catalog, a) - getDevicePagePriority(catalog, b);
      if (priorityDiff !== 0) {
        return priorityDiff;
      }
      const sensorA = sensorByKey.get(a.sensorKey);
      const sensorB = sensorByKey.get(b.sensorKey);
      if (sensorA?.online !== sensorB?.online) {
        return sensorA?.online ? -1 : 1;
      }
      return a.sensorTitle.localeCompare(b.sensorTitle, "zh-CN");
    });
  const controlPages = catalog.id === "yard-01"
    ? [{ key: "control:pump", label: "🧯 水泵控制", kind: "control", controlKey: "pump" }]
    : [];
  const settingsPages = [{ key: "settings:sensor-aliases", label: "⚙️ 名称映射", kind: "settings", settingsKey: "sensor-aliases" }];
  return [...sensorPages, ...controlPages, ...settingsPages];
}

function getMetricNumber(sensor, metricKey) {
  return Number(sensor?.metrics?.find((metric) => metric.key === metricKey)?.value);
}

function buildSensorReminder(sensor) {
  if (!sensor) {
    return {
      title: "AI 小提醒",
      body: "这页还没接到传感器，老板先别急，我在等它开口说话。"
    };
  }

  if (!sensor.online) {
    return {
      title: "AI 小提醒",
      body: `这位 ${sensor.title} 现在在打盹，不过历史曲线还醒着，翻翻前面的记录也能看出不少门道。`
    };
  }

  const temperature = getMetricNumber(sensor, "temperature");
  const humidity = getMetricNumber(sensor, "humidity");
  const pressure = getMetricNumber(sensor, "pressure");
  const illuminance = getMetricNumber(sensor, "illuminance");
  const percent = getMetricNumber(sensor, "percent");
  const voltage = getMetricNumber(sensor, "voltage");

  if ((sensor.key === "dht11" || sensor.key === "shtc3") && Number.isFinite(temperature) && Number.isFinite(humidity)) {
    if (humidity >= 80) {
      return {
        title: "AI 小提醒",
        body: `空气有点像刚浇完水的温室，${humidity.toFixed(0)}%RH 已经很有存在感，记得留意通风。`
      };
    }
    if (humidity <= 35) {
      return {
        title: "AI 小提醒",
        body: `这会儿空气偏干，${humidity.toFixed(0)}%RH 像在提醒你“该补点水汽啦”。`
      };
    }
    if (temperature >= 30) {
      return {
        title: "AI 小提醒",
        body: `温度冲到 ${temperature.toFixed(1)}°C 了，设备在认真上班，屋里也快有点热情过头。`
      };
    }
    if (temperature <= 10) {
      return {
        title: "AI 小提醒",
        body: `现在只有 ${temperature.toFixed(1)}°C，这股清凉劲儿已经有点“早晚添衣”的意思。`
      };
    }
    return {
      title: "AI 小提醒",
      body: `温度 ${temperature.toFixed(1)}°C、湿度 ${humidity.toFixed(0)}%RH，整体状态挺乖，像个认真值班的小管家。`
    };
  }

  if (sensor.key === "bh1750" && Number.isFinite(illuminance)) {
    if (illuminance >= 800) {
      return {
        title: "AI 小提醒",
        body: `这会儿光线很足，${Math.round(illuminance)} lux 的亮度已经是“精神抖擞模式”。`
      };
    }
    if (illuminance <= 30) {
      return {
        title: "AI 小提醒",
        body: `现在只有 ${Math.round(illuminance)} lux，环境偏暗，像是在轻声说“该开灯啦”。`
      };
    }
    return {
      title: "AI 小提醒",
      body: `光照大约 ${Math.round(illuminance)} lux，明暗刚刚好，属于不刺眼也不偷懒的亮度。`
    };
  }

  if ((sensor.key === "bmp180" || sensor.key === "bmp280") && Number.isFinite(temperature) && Number.isFinite(pressure)) {
    if (pressure <= 1000) {
      return {
        title: "AI 小提醒",
        body: `气压 ${pressure.toFixed(1)} hPa 稍微偏低，天气情绪像在酝酿一点变化。`
      };
    }
    if (pressure >= 1020) {
      return {
        title: "AI 小提醒",
        body: `气压 ${pressure.toFixed(1)} hPa 挺稳，整体状态像把“今天我很靠谱”写在了脸上。`
      };
    }
    return {
      title: "AI 小提醒",
      body: `当前 ${temperature.toFixed(1)}°C、${pressure.toFixed(1)} hPa，环境节奏平稳，没有明显的小脾气。`
    };
  }

  if ((sensor.key === "battery" || sensor.key === "max17043") && Number.isFinite(percent) && Number.isFinite(voltage)) {
    if (percent <= 15) {
      return {
        title: "AI 小提醒",
        body: `电池只剩 ${Math.round(percent)}%，它已经在认真眨眼暗示：“老板，该补能量了。”`
      };
    }
    if (percent >= 95) {
      return {
        title: "AI 小提醒",
        body: `电量 ${Math.round(percent)}%，电压 ${voltage.toFixed(2)}V，这颗小电池现在是元气满满状态。`
      };
    }
    return {
      title: "AI 小提醒",
      body: `电量还有 ${Math.round(percent)}%，电压 ${voltage.toFixed(2)}V，续航看起来还挺从容。`
    };
  }

  if (sensor.key === "ds18b20" && Number.isFinite(temperature)) {
    if (temperature >= 28) {
      return {
        title: "AI 小提醒",
        body: `探头测到 ${temperature.toFixed(1)}°C，这片区域已经开始走“偏暖”路线了。`
      };
    }
    if (temperature <= 12) {
      return {
        title: "AI 小提醒",
        body: `现在 ${temperature.toFixed(1)}°C，探头这边凉意在线，像把冷静直接写进了数据里。`
      };
    }
    return {
      title: "AI 小提醒",
      body: `温度稳定在 ${temperature.toFixed(1)}°C，探头今天表现得像个低调又可靠的哨兵。`
    };
  }

  return {
    title: "AI 小提醒",
    body: `这位 ${sensor.title} 正在稳定上报，数据节奏很顺，像是在安安静静把值班工作做好。`
  };
}

function renderSensorPageContent(sensor) {
  if (!sensor) {
    return `
      <article class="info-card" style="grid-column: span 12; margin-top: 12px;">
        <div class="detail-block-head">
          <div class="detail-block-title">传感器页面</div>
          <div class="detail-helper">当前还没有挂接传感器</div>
        </div>
      </article>
    `;
  }

  const reminder = buildSensorReminder(sensor);
  const updatedText = sensor.updatedAt ? `最新上报：${formatTime(sensor.updatedAt)}` : "最新上报：--";

  return `
    <article class="info-card" style="grid-column: span 12; margin-top: 12px;">
      <div class="detail-block-head">
        <div class="detail-block-title">${sensor.icon} ${sensor.title}</div>
        <div class="detail-helper">${updatedText}</div>
      </div>
      <div class="detail-helper" style="margin-top:6px;">${sensor.subtitle}</div>
      <div class="detail-summary-grid" style="margin-top:14px;">
        ${sensor.metrics.map((metric) => {
          const alertClass = metric.alertLevel ? ` metric-value-${metric.alertLevel}` : "";
          return `
          <article class="detail-stat">
            <div class="detail-stat-label">${metric.label}</div>
            <div class="detail-stat-value${alertClass}">${formatSensorMetricValue(sensor.key, metric.key, metric.value, metric.unit)}</div>
          </article>
          `;
        }).join("")}
      </div>
      <div class="sensor-reminder-card" style="margin-top:12px;">
        <div class="sensor-reminder-eyebrow">${reminder.title}</div>
        <div class="sensor-reminder-body">${reminder.body}</div>
      </div>
    </article>
  `;
}

function renderSensorAliasSettingsSection(deviceId, sensors) {
  const aliasMap = appState.latestSensor?.sensorAliases?.[deviceId] || {};
  const statusText = appState.sensorAliasEditor.status || "把实际上报的传感器名映射成更贴近安装位置的显示名。留空会恢复默认名称。";

  return `
    <article class="info-card" style="grid-column: span 12; margin-top: 12px;">
      <div class="detail-block-head">
        <div class="detail-block-title">传感器名称映射</div>
        <div class="detail-helper">保存后，设备分页、历史标题和 CSV 导出列名都会使用这里的名称</div>
      </div>
      <div class="info-list" style="margin-top: 16px;">
        ${sensors.map((sensor) => `
          <div class="info-row" style="align-items: center; gap: 16px;">
            <div style="flex: 1 1 240px;">
              <div class="info-label">实际上报</div>
              <strong>${sensor.icon} ${sensorCatalog[sensor.key]?.title || sensor.key}</strong>
              <div class="detail-helper" style="margin-top: 4px;">键名：${sensor.key}</div>
            </div>
            <div style="flex: 1 1 320px;">
              <div class="info-label">页面显示 / CSV 前缀</div>
              <input
                class="date-input modal-input"
                data-sensor-alias-input="${sensor.key}"
                type="text"
                maxlength="60"
                placeholder="${escapeAttribute(sensorCatalog[sensor.key]?.title || sensor.key)}"
                value="${escapeAttribute(aliasMap[sensor.key] || "")}"
              />
            </div>
          </div>
        `).join("")}
      </div>
      <div class="history-toolbar" style="margin-top: 18px;">
        <button class="ghost-btn" id="resetSensorAliasBtn">恢复默认名称</button>
        <button class="range-btn active" id="saveSensorAliasBtn">${appState.sensorAliasEditor.submitting ? "保存中..." : "保存映射"}</button>
      </div>
      <p class="footer-note" id="sensorAliasStatus">${statusText}</p>
    </article>
  `;
}

function renderDevicePageGroup(title, helper, pages, currentPage) {
  if (!pages.length) {
    return "";
  }
  return `
    <div class="device-page-group">
      <div class="device-page-group-head">
        <div class="device-page-group-title">${title}</div>
      </div>
      <div class="history-toolbar" style="margin-top:10px;">
        ${pages.map((page) => `
          <button class="range-btn ${currentPage?.key === page.key ? "active" : ""}" data-select-page="${page.key}">
            ${page.label}
          </button>
        `).join("")}
      </div>
    </div>
  `;
}

function renderDevicePages(snapshot, catalog, selectedPageKey) {
  const pages = getDevicePages(catalog, snapshot);
  const currentPage = pages.find((page) => page.key === selectedPageKey) || pages[0] || null;
  const currentSensor = currentPage?.kind === "sensor"
    ? snapshot.sensors.find((sensor) => sensor.key === currentPage.sensorKey)
    : null;
  const settingsPages = pages.filter((page) => page.kind === "settings");
  const onlinePages = pages.filter((page) => {
    if (page.kind !== "sensor") {
      return false;
    }
    const sensor = snapshot.sensors.find((item) => item.key === page.sensorKey);
    return Boolean(sensor?.online);
  });
  const offlinePages = pages.filter((page) => {
    if (page.kind === "control") {
      return true;
    }
    if (page.kind !== "sensor") {
      return false;
    }
    const sensor = snapshot.sensors.find((item) => item.key === page.sensorKey);
    return !sensor?.online;
  });

  return `
    <section style="margin-top:20px;">
      <div class="detail-block-head">
      <div class="detail-block-title">设备内分页</div>
      </div>
      ${renderDevicePageGroup("在线设备", "正在实时上报的传感器", onlinePages, currentPage)}
      ${renderDevicePageGroup("掉线设备", "离线传感器和历史/控制入口", offlinePages, currentPage)}
      ${renderDevicePageGroup("设置", "设备内显示名称等本地映射", settingsPages, currentPage)}
      ${currentPage?.kind === "control"
        ? renderPumpControlSection(catalog.id)
        : currentPage?.kind === "settings"
          ? renderSensorAliasSettingsSection(catalog.id, snapshot.sensors)
          : renderSensorPageContent(currentSensor)}
    </section>
  `;
}

function renderDeviceSensorHistorySection(selectedSensor) {
  if (!selectedSensor?.key) {
    return "";
  }

  return `
    <section style="margin-top:20px;">
      <div class="detail-block-head">
        <div class="detail-block-title">当前传感器历史数据</div>
        <div class="detail-helper">温度和湿度分行展示，时间轴可以看得更细</div>
      </div>
      <div class="detail-helper" style="margin-bottom:10px;">当前查看：${selectedSensor.title || sensorCatalog[selectedSensor.key]?.title || "--"}</div>
      <div class="history-toolbar">
        <button class="range-btn ${appState.historyQuery.range === "1h" && !appState.historyQuery.date ? "active" : ""}" data-range="1h">最近1小时</button>
        <button class="range-btn ${appState.historyQuery.range === "24h" && !appState.historyQuery.date ? "active" : ""}" data-range="24h">最近24小时</button>
        <button class="range-btn ${appState.historyQuery.range === "3d" && !appState.historyQuery.date ? "active" : ""}" data-range="3d">最近3天</button>
        <button class="range-btn ${appState.historyQuery.range === "7d" && !appState.historyQuery.date ? "active" : ""}" data-range="7d">最近7天</button>
        <input class="date-input" id="historyDate" type="date" value="${appState.historyQuery.date || ""}" />
        <button class="range-btn" id="applyDateBtn">查看指定日期</button>
      </div>
      <div class="detail-block-head" style="margin-bottom:12px;">
        <div class="detail-block-title" id="historyTitle">${selectedSensor.title || sensorCatalog[selectedSensor.key]?.title || "--"} 历史曲线</div>
        <div class="detail-helper" id="historyLegend">每个指标单独一张图</div>
      </div>
      <div class="history-panels" id="historyPanels"></div>
      <div class="chart-actions">
        <button class="ghost-btn" id="resetZoomBtn">重置缩放</button>
        <span class="detail-helper" id="historyExportStatus">点进的是设备，历史曲线按设备内部的传感器切换。</span>
      </div>
      <p class="footer-note" id="historySummary">正在整理历史统计。</p>
    </section>
  `;
}

function renderPumpControlSection(deviceId) {
  if (deviceId !== "yard-01") {
    return "";
  }

  return `
    <article class="info-card" style="grid-column: span 12; margin-top: 12px;">
      <div class="detail-block-head">
        <div class="detail-block-title">水泵控制</div>
        <div class="detail-helper">控制庭院 1 号设备上的 ESP32 IO 口</div>
      </div>
      <div class="info-list">
        <div class="info-row"><span class="info-label">控制主题</span><strong>garden/yard/pump/set</strong></div>
        <div class="info-row"><span class="info-label">作用方式</span><strong>发送一次 pulse 指令，ESP32 拉高 IO 控制水泵指定秒数</strong></div>
      </div>
      <div class="history-toolbar" style="margin-top:14px;">
        <input class="date-input" id="pumpDurationSeconds" type="number" min="1" max="600" step="1" value="5" placeholder="浇灌时间（秒）" />
        <input class="date-input" id="pumpPassword" type="password" value="1234" placeholder="口令" />
        <button class="range-btn active" id="sendPumpCommandBtn">发送水泵指令</button>
      </div>
      <p class="footer-note" id="pumpCommandStatus">默认口令是 1234。发送后，网关会转成 MQTT 消息发给 ESP32。</p>
    </article>
  `;
}

function getDeviceSceneConfig(catalog) {
  const sceneMap = {
    yard: {
      themeClass: "scene-yard",
      eyebrow: "",
      title: "",
      subtitle: "",
      badges: ["月季", "向日葵", "荷叶", "金鱼", "菊花"],
      art: ["sun", "plant", "pot", "fence", "rose", "sunflower", "lotus", "fish", "chrysanthemum"]
    },
    study: {
      themeClass: "scene-study",
      eyebrow: "书房场景",
      title: "书桌、书本与安静空气",
      subtitle: "书房设备更适合偏安静、整洁、可阅读的氛围表达。",
      badges: ["书桌", "阅读", "温湿度"],
      art: ["lamp", "books", "desk", "clock"]
    },
    office: {
      themeClass: "scene-office",
      eyebrow: "办公室场景",
      title: "工作台、文件与日常办公",
      subtitle: "让办公室节点更像一块真实的工位环境监测面板。",
      badges: ["工位", "效率", "环境"],
      art: ["monitor", "desk", "mug", "files"]
    },
    bedroom: {
      themeClass: "scene-bedroom",
      eyebrow: "卧室场景",
      title: "床头、夜灯与舒适睡眠",
      subtitle: "卧室设备适合更柔和、安静、偏休息氛围的表达。",
      badges: ["睡眠", "舒适", "夜间"],
      art: ["moon", "bed", "pillow", "lamp"]
    }
  };
  return sceneMap[catalog?.room] || {
    themeClass: "scene-generic",
    eyebrow: "设备场景",
    title: catalog?.title || "设备节点",
    subtitle: "这块区域用于补足设备所在环境的视觉语义。",
    badges: ["设备", "环境", "监测"],
    art: ["orb", "card", "spark", "line"]
  };
}

function renderSceneShapes(scene) {
  return scene.art.map((shape) => `<span class="scene-shape scene-${shape}"></span>`).join("");
}

function getRoomDeviceSnapshots(roomId) {
  return Object.keys(deviceCatalog)
    .filter((deviceId) => deviceCatalog[deviceId].type === "iot-device" && deviceCatalog[deviceId].room === roomId)
    .map((deviceId) => ({ catalog: deviceCatalog[deviceId], snapshot: getDeviceSnapshot(deviceId) }))
    .filter(({ snapshot }) => Boolean(snapshot));
}

function renderRoomDeviceQuickList(catalog) {
  const roomDevices = getRoomDeviceSnapshots(catalog?.room);
  if (!roomDevices.length) {
    return `
      <div class="scene-presence">
        <div class="scene-presence-title">同位置设备</div>
        <div class="scene-presence-empty">暂时没有可展示的设备。</div>
      </div>
    `;
  }

  const onlineCount = roomDevices.filter(({ snapshot }) => snapshot.online).length;
  return `
    <div class="scene-presence">
      <div class="scene-presence-head">
        <div class="scene-presence-title">同位置设备</div>
        <div class="scene-presence-count">在线 ${onlineCount}/${roomDevices.length}</div>
      </div>
      <div class="scene-device-list">
        ${roomDevices.map(({ catalog: itemCatalog, snapshot }) => `
          <span class="scene-device-chip ${snapshot.online ? "is-online" : "is-offline"}">
            <span class="scene-device-dot"></span>
            ${itemCatalog.title}
          </span>
        `).join("")}
      </div>
    </div>
  `;
}

function renderDeviceSceneCard(catalog) {
  const scene = getDeviceSceneConfig(catalog);
  return `
    <article class="info-card device-scene-card ${scene.themeClass}">
      <div class="scene-copy">
        ${scene.eyebrow ? `<div class="scene-eyebrow">${scene.eyebrow}</div>` : ""}
        ${scene.title ? `<div class="scene-title">${scene.title}</div>` : ""}
        ${scene.subtitle ? `<div class="scene-subtitle">${scene.subtitle}</div>` : ""}
        <div class="scene-badges">
          ${scene.badges.map((badge) => `<span>${badge}</span>`).join("")}
        </div>
        ${renderRoomDeviceQuickList(catalog)}
      </div>
      <div class="scene-art" aria-hidden="true">
        ${renderSceneShapes(scene)}
      </div>
    </article>
  `;
}

function renderIoTDeviceDetail(deviceId, catalog, snapshot) {
  const pages = getDevicePages(catalog, snapshot);
  const selectedPageKey = appState.activeDevicePageKey || pages[0]?.key || null;
  const selectedSensor = selectedPageKey?.startsWith("sensor:")
    ? snapshot.sensors.find((sensor) => sensor.key === appState.activeSensorKey) || snapshot.sensors[0] || null
    : null;
  const hideHeaderCopy = catalog.id === "yard-01";
  const offlineHint = snapshot.online
    ? ""
    : `<p class="footer-note">设备当前离线，但可以继续查看设备内分页和历史数据。</p>`;
  return `
    <div class="detail-topbar">
      <div>
        <div class="detail-header-top">
          <div class="detail-icon">${catalog.icon}</div>
          <div class="detail-status ${getStatusClass(snapshot)}"><span class="status-dot"></span>${snapshot.online ? "设备在线" : "设备超时未上报"}</div>
        </div>
        <div class="detail-title">${catalog.title}</div>
        ${hideHeaderCopy ? "" : `<div class="detail-subtitle">${catalog.subtitle}</div>`}
        ${hideHeaderCopy ? "" : `<p class="footer-note">${catalog.summary}</p>`}
        ${offlineHint}
      </div>
      <div class="detail-topbar-actions">
        <button class="ghost-btn" id="openDeviceExportDialogBtn">导出 CSV 数据</button>
        <div class="section-note">归属位置：${roomConfigs.find((room) => room.id === catalog.room)?.name || "未分组"}</div>
      </div>
    </div>

    <section class="detail-info-grid" style="margin-top:18px;">
      ${renderDeviceSceneCard(catalog)}
    </section>

    ${renderDevicePages(snapshot, catalog, selectedPageKey)}
    ${selectedPageKey?.startsWith("sensor:") ? renderDeviceSensorHistorySection(selectedSensor) : ""}
  `;
}

function renderServerDetail(catalog, snapshot) {
  const server = snapshot.raw || {};
  return `
    <div class="detail-topbar">
      <div>
        <div class="detail-header-top">
          <div class="detail-icon">${catalog.icon}</div>
          <div class="detail-status"></div>
        </div>
        <div class="detail-title">${catalog.title}</div>
        <div class="detail-subtitle">${catalog.subtitle}</div>
      </div>
      <div class="section-note">主机：${escapeHtml(server.hostname || "--")}</div>
    </div>

    <section class="detail-summary-grid">
      ${server.cpuTemperatureC != null ? `<article class="detail-stat"><div class="detail-stat-label">CPU 温度</div><div class="detail-stat-value">${formatMetricValue(server.cpuTemperatureC, "°C")}</div></article>` : ""}
      <article class="detail-stat"><div class="detail-stat-label">CPU 占用</div><div class="detail-stat-value">${formatMetricValue(server.cpuCurrentUsage, "%")}</div></article>
      <article class="detail-stat"><div class="detail-stat-label">内存占用</div><div class="detail-stat-value">${formatMetricValue(server.memory?.usedPercent, "%")}</div></article>
    </section>

    <section class="detail-info-grid" style="margin-top:16px;">
      <article class="info-card">
        <div class="detail-block-title">主机信息</div>
        <div class="info-list">
          <div class="info-row"><span class="info-label">主机名</span><strong>${escapeHtml(server.hostname || "--")}</strong></div>
          <div class="info-row"><span class="info-label">IP 地址</span><strong>${escapeHtml((server.ipAddresses || []).join(" / ") || "--")}</strong></div>
          <div class="info-row"><span class="info-label">型号</span><strong>${escapeHtml(server.deviceModel || "--")}</strong></div>
          <div class="info-row"><span class="info-label">CPU</span><strong>${escapeHtml(server.cpuModel || "--")}</strong></div>
          <div class="info-row"><span class="info-label">核心数</span><strong>${escapeHtml(server.cpuCores || "--")}</strong></div>
          <div class="info-row"><span class="info-label">运行时长</span><strong>${escapeHtml(server.uptimeText || "--")}</strong></div>
        </div>
      </article>
      <article class="info-card">
        <div class="detail-block-title">服务说明</div>
        <div class="info-list">
          <div class="info-row"><span class="info-label">MQTT</span><strong>${escapeHtml(server.mqtt?.port || 1883)}</strong></div>
          <div class="info-row"><span class="info-label">内存</span><strong>${escapeHtml(server.memory?.usedText || "--")} / ${escapeHtml(server.memory?.totalText || "--")}</strong></div>
          <div class="info-row"><span class="info-label">最近刷新</span><strong>${formatTime(server.updatedAt)}</strong></div>
          <div class="info-row"><span class="info-label">定位</span><strong>把服务器也当设备统一管理</strong></div>
        </div>
      </article>
    </section>

    <section class="chart-grid" style="margin-top:20px;">
      <article class="chart-card wide">
        <div class="detail-block-head"><div class="detail-block-title">每核 CPU 占用（最近 24 小时）</div><div class="detail-helper" id="serverCpuLegend"></div></div>
        <div class="chart-wrap"><canvas id="serverCpuChart" width="940" height="300"></canvas></div>
      </article>
      <article class="chart-card wide">
        <div class="detail-block-head"><div class="detail-block-title">内存占用（最近 24 小时）</div><div class="detail-helper">每分钟采样</div></div>
        <div class="chart-wrap"><canvas id="serverMemoryChart" width="940" height="260"></canvas></div>
      </article>
    </section>
  `;
}

function getWeatherSummary(weather) {
  const today = weather.forecast?.[0];
  if (!today) return "天气台暂时失联，稍后再试。";
  if (today.rainProbability >= 70) return "最近降雨概率偏高，庭院设备注意防水。";
  if (today.tempMax >= 30) return "最近偏热，浇水和补光时机可以提前。";
  return "天气整体平稳，适合按常规节奏巡检设备。";
}

function renderWeatherDetail(catalog, snapshot) {
  const weather = snapshot.raw || {};
  const current = weather.current || {};
  const forecast = weather.forecast || [];
  return `
    <div class="detail-topbar">
      <div>
        <div class="detail-header-top">
          <div class="detail-icon">${catalog.icon}</div>
          <div class="detail-status"></div>
        </div>
        <div class="detail-title">气象站天气预报</div>
        <div class="detail-subtitle">${escapeHtml(weather.location || catalog.subtitle)}</div>
      </div>
      <div class="section-note">更新时间：${formatTime(weather.updatedAt)}</div>
    </div>

    <section class="detail-summary-grid">
      <article class="detail-stat"><div class="detail-stat-label">当前温度</div><div class="detail-stat-value">${formatMetricValue(current.temperature, "°C")}</div></article>
      <article class="detail-stat"><div class="detail-stat-label">当前湿度</div><div class="detail-stat-value">${current.humidity == null ? "--" : `${current.humidity}%`}</div></article>
      <article class="detail-stat"><div class="detail-stat-label">当前天气</div><div class="detail-stat-value">${escapeHtml(current.weatherText || "--")}</div></article>
    </section>

    <section class="detail-info-grid" style="margin-top:16px;">
      <article class="info-card">
        <div class="detail-block-head">
          <div class="detail-block-title">天气提醒</div>
          <div class="detail-helper">点进来就是天气页，不再额外套设备页结构</div>
        </div>
        <div class="sensor-reminder-card" style="margin-top:14px;">
          <div class="sensor-reminder-eyebrow">今日建议</div>
          <div class="sensor-reminder-body">${escapeHtml(getWeatherSummary(weather))}</div>
        </div>
      </article>
      <article class="info-card">
        <div class="detail-block-title">未来 5 天</div>
        <div class="info-list">
          ${forecast.map((item) => `<div class="info-row"><span class="info-label">${formatDateLabel(item.date)}</span><strong>${item.weatherText} · ${item.tempMin}°C ~ ${item.tempMax}°C · 降雨 ${item.rainProbability}%</strong></div>`).join("") || '<div class="info-row"><span class="info-label">天气</span><strong>暂时没有预报数据</strong></div>'}
        </div>
      </article>
    </section>
  `;
}

function bindIoTDeviceEvents(deviceId) {
  document.getElementById("openDeviceExportDialogBtn")?.addEventListener("click", () => {
    openDeviceExportDialog(deviceId);
  });

  document.querySelectorAll("[data-select-page]").forEach((button) => {
    button.addEventListener("click", async () => {
      appState.activeDevicePageKey = button.dataset.selectPage;
      if (appState.activeDevicePageKey.startsWith("sensor:")) {
        appState.activeSensorKey = appState.activeDevicePageKey.split(":")[1];
      }
      renderDeviceDetail(deviceId);
      if (appState.activeDevicePageKey.startsWith("sensor:")) {
        await refreshSensorHistory(deviceId, appState.activeSensorKey);
      }
    });
  });

  document.querySelectorAll(".range-btn[data-range]").forEach((button) => {
    button.addEventListener("click", async () => {
      appState.historyQuery = { range: button.dataset.range, date: "" };
      renderDeviceDetail(deviceId);
      if (appState.activeSensorKey) {
        await refreshSensorHistory(deviceId, appState.activeSensorKey);
      }
    });
  });

  document.getElementById("applyDateBtn")?.addEventListener("click", async () => {
    const value = document.getElementById("historyDate")?.value;
    if (!value) return;
    appState.historyQuery = { ...appState.historyQuery, date: value };
    renderDeviceDetail(deviceId);
    if (appState.activeSensorKey) {
      await refreshSensorHistory(deviceId, appState.activeSensorKey);
    }
  });

  document.getElementById("resetZoomBtn")?.addEventListener("click", () => resetChartZoom());

  document.querySelectorAll("[data-export-metric]").forEach((button) => {
    button.addEventListener("click", async () => {
      const metricKey = button.dataset.exportMetric;
      const statusEl = document.getElementById("historyExportStatus");
      try {
        if (statusEl) {
          statusEl.textContent = "正在生成并保存当前曲线 CSV...";
        }
        const data = await exportVisibleMetricCsv(deviceId, appState.activeSensorKey, metricKey);
        if (statusEl) {
          statusEl.textContent = data.mode === "picker"
            ? `已保存：${data.fileName}`
            : `已触发下载：${data.fileName}`;
        }
      } catch (error) {
        if (statusEl) {
          statusEl.textContent = `导出失败：${error.message}`;
        }
      }
    });
  });

  document.getElementById("sendPumpCommandBtn")?.addEventListener("click", async () => {
    const durationInput = document.getElementById("pumpDurationSeconds");
    const passwordInput = document.getElementById("pumpPassword");
    const statusEl = document.getElementById("pumpCommandStatus");
    const durationSeconds = Number(durationInput?.value);
    const password = String(passwordInput?.value || "");

    if (!Number.isFinite(durationSeconds) || durationSeconds <= 0) {
      if (statusEl) {
        statusEl.textContent = "请输入有效的浇灌时间，单位秒。";
      }
      return;
    }

    if (statusEl) {
      statusEl.textContent = "正在发送水泵控制指令...";
    }

    try {
      const response = await fetch("/api/device/yard/pump", {
        method: "POST",
        headers: {
          "Content-Type": "application/json"
        },
        body: JSON.stringify({
          durationSeconds,
          password
        })
      });
      const data = await response.json();
      if (!response.ok) {
        throw new Error(data.message || "pump command failed");
      }
      if (statusEl) {
        statusEl.textContent = `已发送：浇灌 ${data.command.durationSeconds} 秒，主题 ${data.command.topic}`;
      }
    } catch (error) {
      if (statusEl) {
        statusEl.textContent = `发送失败：${error.message}`;
      }
    }
  });

  document.getElementById("resetSensorAliasBtn")?.addEventListener("click", () => {
    document.querySelectorAll("[data-sensor-alias-input]").forEach((input) => {
      input.value = "";
    });
    appState.sensorAliasEditor.status = "已恢复为默认名称，点保存后生效。";
    const statusEl = document.getElementById("sensorAliasStatus");
    if (statusEl) {
      statusEl.textContent = appState.sensorAliasEditor.status;
    }
  });

  document.getElementById("saveSensorAliasBtn")?.addEventListener("click", async () => {
    const statusEl = document.getElementById("sensorAliasStatus");
    const saveBtn = document.getElementById("saveSensorAliasBtn");
    const aliases = {};
    document.querySelectorAll("[data-sensor-alias-input]").forEach((input) => {
      aliases[input.dataset.sensorAliasInput] = input.value;
    });

    try {
      appState.sensorAliasEditor.submitting = true;
      appState.sensorAliasEditor.status = "正在保存名称映射...";
      if (statusEl) {
        statusEl.textContent = appState.sensorAliasEditor.status;
      }
      if (saveBtn) {
        saveBtn.textContent = "保存中...";
        saveBtn.disabled = true;
      }
      const response = await saveSensorAliases(deviceId, aliases);
      if (!appState.latestSensor) {
        appState.latestSensor = {};
      }
      if (!appState.latestSensor.sensorAliases) {
        appState.latestSensor.sensorAliases = {};
      }
      appState.latestSensor.sensorAliases[deviceId] = response.aliases || {};
      appState.sensorAliasEditor.status = "名称映射已保存，当前页面和后续 CSV 导出都会使用新名称。";
      appState.historyCache.clear();
      renderOverview();
      renderDeviceDetail(deviceId);
    } catch (error) {
      appState.sensorAliasEditor.status = `保存失败：${error.message}`;
      if (statusEl) {
        statusEl.textContent = appState.sensorAliasEditor.status;
      }
    } finally {
      appState.sensorAliasEditor.submitting = false;
      if (saveBtn) {
        saveBtn.textContent = "保存映射";
        saveBtn.disabled = false;
      }
    }
  });
}

function renderServerCharts() {
  const history = appState.serverHistory || {};
  const points = history.points || [];
  const cpuCanvas = document.getElementById("serverCpuChart");
  const memoryCanvas = document.getElementById("serverMemoryChart");
  if (!cpuCanvas || !memoryCanvas) return;

  const cpuContext = cpuCanvas.getContext("2d");
  const memoryContext = memoryCanvas.getContext("2d");

  const cpuSeries = new Array(history.cpuCoreCount || 0).fill(null).map((_, index) => ({
    label: `Core ${index + 1}`,
    color: `hsl(${(index * 47) % 360} 65% 52%)`,
    points: points.map((point) => ({ tsMs: point.tsMs, value: point.perCoreUsage?.[index] ?? null }))
  }));
  drawSimpleChart(cpuContext, cpuCanvas, cpuSeries, { lockMin: 0, lockMax: 100, unit: "%" });
  const cpuLegend = document.getElementById("serverCpuLegend");
  if (cpuLegend) cpuLegend.textContent = cpuSeries.map((series) => series.label).join(" · ");

  drawSimpleChart(memoryContext, memoryCanvas, [{
    label: "内存占用",
    color: "#4e89ff",
    points: points.map((point) => ({ tsMs: point.tsMs, value: point.memoryUsedPercent }))
  }], { lockMin: 0, lockMax: 100, unit: "%" });
}

function renderDeviceDetail(deviceId) {
  const catalog = deviceCatalog[deviceId];
  const snapshot = getDeviceSnapshot(deviceId);
  if (!catalog || !snapshot) return;

  appState.activeDeviceId = deviceId;
  if (catalog.type === "iot-device") {
    const pages = getDevicePages(catalog, snapshot);
    const pageKeys = pages.map((page) => page.key);
    const defaultPageKey = pages[0]?.key || null;
    if (!appState.activeDevicePageKey || !pageKeys.includes(appState.activeDevicePageKey)) {
      appState.activeDevicePageKey = defaultPageKey;
    }
    if (appState.activeDevicePageKey?.startsWith("sensor:")) {
      appState.activeSensorKey = appState.activeDevicePageKey.split(":")[1];
    }
  }

  els.overviewView.classList.add("hidden");
  els.detailView.classList.add("active");

  if (catalog.type === "iot-device") {
    els.detailPanel.innerHTML = renderIoTDeviceDetail(deviceId, catalog, snapshot);
    bindIoTDeviceEvents(deviceId);
    renderExportDialog();
    if (appState.activeDevicePageKey?.startsWith("sensor:") && appState.activeSensorKey) {
      refreshSensorHistory(deviceId, appState.activeSensorKey).catch(() => {
        const summary = document.getElementById("historySummary");
        if (summary) summary.textContent = "历史数据读取失败，请稍后再试。";
      });
    }
    return;
  }

  if (catalog.type === "server") {
    els.detailPanel.innerHTML = renderServerDetail(catalog, snapshot);
    renderExportDialog();
    renderServerCharts();
    return;
  }

  if (catalog.type === "weather") {
    els.detailPanel.innerHTML = renderWeatherDetail(catalog, snapshot);
    renderExportDialog();
  }
}

function openDevice(deviceId) {
  if (!deviceCatalog[deviceId]) return;
  if (appState.activeDeviceId !== deviceId) {
    appState.sensorAliasEditor = {
      submitting: false,
      status: ""
    };
  }
  renderDeviceDetail(deviceId);
  const nextHash = `#device=${encodeURIComponent(deviceId)}`;
  if (location.hash !== nextHash) {
    location.hash = nextHash.slice(1);
  }
}

function resolveDeviceId(routeId) {
  if (deviceCatalog[routeId]) {
    return routeId;
  }
  return Object.keys(deviceCatalog).find((deviceId) => getDeviceMatchIds(deviceCatalog[deviceId]).includes(routeId)) || null;
}

function closeDetail() {
  appState.activeDeviceId = null;
  appState.activeDevicePageKey = null;
  appState.activeSensorKey = null;
  appState.sensorAliasEditor = {
    submitting: false,
    status: ""
  };
  appState.exportDialog.open = false;
  els.detailView.classList.remove("active");
  els.overviewView.classList.remove("hidden");
  renderExportDialog();
  if (location.hash) history.replaceState(null, "", location.pathname + location.search);
}

function syncRoute() {
  const hash = location.hash.replace(/^#/, "");
  if (hash.startsWith("device=")) {
    let deviceId = hash.split("=")[1];
    try {
      deviceId = decodeURIComponent(deviceId);
    } catch (error) {
      // Keep the raw hash value so old/non-encoded routes still have a chance to match.
    }
    const resolvedDeviceId = resolveDeviceId(deviceId);
    if (resolvedDeviceId) {
      renderDeviceDetail(resolvedDeviceId);
      return;
    }
  }
  if (appState.activeDeviceId) closeDetail();
}

async function refreshAll() {
  try {
    const [latestSensor, serverStatus, serverHistory, devicesStatus, roomPhotoResponse] = await Promise.all([
      fetchJson("/api/sensor/latest"),
      fetchJson("/api/server/status"),
      fetchJson("/api/server/history"),
      fetchJson("/api/devices/status"),
      fetchJson("/api/room-photos")
    ]);
    appState.latestSensor = latestSensor;
    appState.serverStatus = serverStatus;
    appState.serverHistory = serverHistory;
    appState.devicesStatus = devicesStatus;
    appState.roomPhotos = roomPhotoResponse?.photos || {};
    appState.refreshAt = new Date().toISOString();
    renderOverview();
    if (appState.activeDeviceId) renderDeviceDetail(appState.activeDeviceId);
  } catch (error) {
    els.lastRefreshText.textContent = "数据读取失败，请检查网关服务。";
  }

  // 天气单独请求，不阻塞主内容渲染
  fetchJson("/api/weather/forecast")
    .then((weather) => {
      appState.weather = weather;
      renderOverview();
    })
    .catch((err) => console.warn("[weather]", err.message));
}

els.backToOverviewBtn.addEventListener("click", () => closeDetail());
window.addEventListener("hashchange", () => syncRoute());
window.addEventListener("resize", () => {
  if (appState.activeDeviceId) renderDeviceDetail(appState.activeDeviceId);
});

renderOverview();
refreshAll().then(() => syncRoute());
setInterval(refreshAll, 60 * 1000);
