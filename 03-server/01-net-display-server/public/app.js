const roomConfigs = [
  { id: "all", name: "全屋", description: "查看全部设备" },
  { id: "yard", name: "庭院", description: "庭院节点和植物相关设备" },
  { id: "study", name: "书房", description: "书房温湿度监测" },
  { id: "office", name: "办公室", description: "办公室温湿度监测" },
  { id: "bedroom", name: "卧室", description: "卧室温湿度监测" },
  { id: "server", name: "机柜", description: "网关和服务运行状态" },
  { id: "outdoor", name: "户外", description: "外部天气和室外数据" }
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
  }
};

const deviceCatalog = {
  "yard-01": {
    id: "yard-01",
    title: "庭院 1 号",
    subtitle: "庭院温湿度节点",
    room: "yard",
    type: "iot-device",
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
    icon: "📚",
    accentClass: "accent-flower",
    matchIds: ["study-01"],
    historyDevices: ["study-01"],
    sensors: [{ key: "dht11", title: "DHT11 温湿度", subtitle: "空气温度与相对湿度", icon: "🌡️", metricLabels: { temperature: "温度", humidity: "湿度" } }],
    summary: "书房 ESP32-C3 节点，挂载 DHT11 温湿度传感器。"
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
    title: "户外天气台",
    subtitle: "Open-Meteo 预报汇总",
    room: "outdoor",
    type: "weather",
    icon: "🌦️",
    accentClass: "accent-weather",
    summary: "天气服务单独作为一个设备入口，后续替换成真实户外站也不需要换交互。"
  }
};

// 在线判定窗口（与服务端 DEVICE_ONLINE_WINDOW_MS 保持一致）
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
  refreshAt: null
};

const els = {
  overviewView: document.getElementById("overviewView"),
  detailView: document.getElementById("detailView"),
  detailPanel: document.getElementById("detailPanel"),
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
  if (unit === "V") return `${Number(value).toFixed(2)} ${unit}`;
  return `${Number(value).toFixed(1)} ${unit}`;
}

function formatSensorMetricValue(sensorKey, metricKey, value, unit) {
  if (value == null || Number.isNaN(Number(value))) return "--";
  if (sensorKey === "battery" && metricKey === "percent") {
    return `${Math.round(Number(value))} ${unit}`;
  }
  return formatMetricValue(value, unit);
}

async function fetchJson(url, options = {}) {
  const response = await fetch(url, { cache: "no-store", ...options });
  if (!response.ok) throw new Error(url);
  return response.json();
}

function isRecentSensorUpdate(updatedAt) {
  if (!updatedAt) return false;
  const tsMs = new Date(updatedAt).getTime();
  return Number.isFinite(tsMs) && Date.now() - tsMs <= SENSOR_ONLINE_WINDOW_MS;
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

function getDeviceMatchIds(catalog) {
  return catalog?.matchIds?.length ? catalog.matchIds : [catalog?.id].filter(Boolean);
}

function buildDynamicSensorRefs(deviceId, catalog) {
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

  const deviceStatus = getDevicesStatusMap().get(deviceId);
  (deviceStatus?.sensors || []).forEach((sensorKey) => {
    if (sensorCatalog[sensorKey]) {
      sensorKeys.add(sensorKey);
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
    title: ref.title || sensorDef.title,
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
    online: isRecentSensorUpdate(sensor.updatedAt),
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

function buildDeviceSummaryMetrics(sensors) {
  const metrics = [];

  const pushMetric = (sensorKey, metricKey) => {
    const sensor = sensors.find((item) => item.key === sensorKey);
    const metric = sensor?.metrics?.find((item) => item.key === metricKey && item.value != null);
    if (!metric) return;
    metrics.push({
      ...metric,
      sensorKey
    });
  };

  // 电池设备卡片优先展示电量/电压，避免被温湿度等指标挤掉。
  pushMetric("battery", "percent");
  pushMetric("battery", "voltage");

  sensors.forEach((sensor) => {
    const metric = sensor.metrics.find((item) => item.value != null);
    if (!metric) return;
    const exists = metrics.some((item) => item.sensorKey === sensor.key && item.key === metric.key);
    if (!exists) {
      metrics.push({
        ...metric,
        sensorKey: sensor.key
      });
    }
  });

  return metrics.slice(0, 2);
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

function renderRoomOverview() {
  const visibleDevices = getVisibleDevices();
  const allSensorMetrics = visibleDevices
    .filter(({ catalog }) => catalog.type === "iot-device")
    .flatMap(({ snapshot }) => snapshot.sensors)
    .flatMap((sensor) => sensor.metrics)
    .filter((metric) => metric.value != null);

  const avg = (items) => {
    if (!items.length) return "--";
    return (items.reduce((sum, item) => sum + Number(item.value), 0) / items.length).toFixed(items[0].unit === "lux" ? 0 : 1);
  };

  const tempItems = allSensorMetrics.filter((item) => item.unit === "°C");
  const humidityItems = allSensorMetrics.filter((item) => item.unit === "%RH" || item.unit === "%");
  const lightItems = allSensorMetrics.filter((item) => item.unit === "lux");

  els.roomOverview.innerHTML = ``;
}

function getStatusClass(snapshot) {
  return snapshot?.online ? "is-online" : "is-offline";
}

function getStatusText(snapshot) {
  return snapshot?.statusText || (snapshot?.online ? "在线" : "离线");
}

function getDeviceAlertBadge(snapshot) {
  if (!snapshot.sensors) return "";
  const highest = snapshot.sensors
    .flatMap((sensor) => sensor.metrics)
    .reduce((acc, metric) => {
      if (metric.alertLevel === "alert") return "alert";
      if (metric.alertLevel === "warn" && acc !== "alert") return "warn";
      return acc;
    }, null);
  if (!highest) return "";
  return `<span class="alert-badge alert-badge-${highest}">${highest === "alert" ? "异常" : "警告"}</span>`;
}

function renderDeviceGrid() {
  const visibleDevices = getVisibleDevices();
  els.deviceGrid.innerHTML = visibleDevices
    .map(({ catalog, snapshot }) => {
      const metrics = (snapshot.summaryMetrics || []).slice(0, 2);
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
        ? `<span>${snapshot.sensors.length} 个传感器</span>`
        : `<span>${catalog.type === "server" ? "系统设备" : "服务设备"}</span>`;
      const alertBadge = getDeviceAlertBadge(snapshot);
      return `
        <article class="device-card ${catalog.accentClass}" data-open-device="${catalog.id}">
          <div class="device-card-header">
            <div class="device-icon">${catalog.icon}</div>
            <button class="device-open" data-open-device="${catalog.id}" aria-label="打开${catalog.title}">›</button>
          </div>
          <div class="device-title">${catalog.title}${alertBadge}</div>
          <div class="device-subtitle">${catalog.subtitle}</div>
          <div class="device-metrics">${metricHtml || '<div class="metric-pill"><div class="metric-label">当前数据</div><div class="metric-value">--</div></div>'}</div>
          <div class="device-meta">
            ${catalog.type === "iot-device" ? `<span class="device-status ${getStatusClass(snapshot)}"><span class="status-dot"></span>${getStatusText(snapshot)}</span>` : ""}
            ${deviceMeta}
            <span>${snapshot.updatedAt ? `更新 ${formatTime(snapshot.updatedAt)}` : "等待数据"}</span>
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

function showTooltip(tooltipEl, metric, point, position) {
  if (!tooltipEl) return;
  const valueText = metric.unit === "lux"
    ? `${Math.round(Number(point[metric.key]))} ${metric.unit}`
    : `${Number(point[metric.key]).toFixed(1)} ${metric.unit}`;
  tooltipEl.innerHTML = `<strong>${formatPointTime(point.tsMs)}</strong><br />${valueText}`;
  tooltipEl.style.left = `${position.x}px`;
  tooltipEl.style.top = `${position.y}px`;
  tooltipEl.classList.add("visible");
}

function hideTooltip(tooltipEl) {
  if (tooltipEl) tooltipEl.classList.remove("visible");
}

function drawMetricChart(metric, canvas, tooltipEl) {
  const context = canvas.getContext("2d");
  const { width, height, dpr } = getCanvasSize(canvas);
  context.setTransform(dpr, 0, 0, dpr, 0, 0);
  const points = getVisibleHistoryPoints();
  const padding = { top: 18, right: 20, bottom: 44, left: 64 };
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
    context.font = "12px Segoe UI";
    context.textAlign = "right";
    for (let i = 0; i <= 4; i += 1) {
      const y = padding.top + (plotHeight / 4) * i + 4;
      const value = scale.max - ((scale.max - scale.min) * i) / 4;
      // 含单位的纵轴标签
      const label = metric.unit === "lux"
        ? `${Math.round(value)} lux`
        : metric.unit === "%RH" || metric.unit === "%"
          ? `${value.toFixed(0)}${metric.unit}`
          : `${value.toFixed(1)} ${metric.unit}`;
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

  validPoints.forEach(({ point, index }) => {
    context.beginPath();
    context.arc(toX(point.tsMs), toY(point[metric.key]), appState.hoverIndexByMetric[metric.key] === index ? 5 : 3.5, 0, Math.PI * 2);
    context.fillStyle = metric.color || "#395dff";
    context.fill();
  });

  const hoverIndex = appState.hoverIndexByMetric[metric.key];
  if (hoverIndex != null && points[hoverIndex]?.[metric.key] != null) {
    const hoverPoint = points[hoverIndex];
    const hoverX = toX(hoverPoint.tsMs);
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

  context.fillStyle = "#7b6f62";
  context.font = "12px Segoe UI";
  context.textAlign = "center";
  const tickCount = Math.min(getHistoryTickCount(startTs, endTs), Math.max(points.length, 2));
  for (let i = 0; i < tickCount; i += 1) {
    const ratio = tickCount === 1 ? 0 : i / Math.max(tickCount - 1, 1);
    const tickTs = startTs + timeSpan * ratio;
    const x = toX(tickTs);
    context.textAlign = i === 0 ? "left" : i === tickCount - 1 ? "right" : "center";
    context.fillText(getTimeLabel(tickTs, startTs, endTs), x, height - 8);
    // X 轴刻度短线
    context.strokeStyle = "rgba(100, 110, 130, 0.30)";
    context.lineWidth = 1;
    context.setLineDash([]);
    context.beginPath();
    context.moveTo(x, padding.top + plotHeight);
    context.lineTo(x, padding.top + plotHeight + 4);
    context.stroke();
  }

  canvas._hitPoints = points.map((point, index) => ({ index, x: toX(point.tsMs), point }));
}

function renderHistoryPanels() {
  const historyPanelsEl = document.getElementById("historyPanels");
  if (!historyPanelsEl) return;
  historyPanelsEl.innerHTML = "";
  (appState.historyMeta.metrics || []).forEach((metric) => {
    const panel = document.createElement("div");
    panel.className = "history-panel";
    panel.innerHTML = `
      <div class="history-panel-head">
        <div class="history-panel-title">${metric.label}</div>
        <div class="history-panel-meta">${metric.unit}</div>
      </div>
      <div class="chart-wrap">
        <canvas width="520" height="280"></canvas>
        <div class="chart-tip"></div>
      </div>
      <div class="history-panel-note"></div>
    `;

    const canvas = panel.querySelector("canvas");
    const tooltipEl = panel.querySelector(".chart-tip");
    const noteEl = panel.querySelector(".history-panel-note");
    drawMetricChart(metric, canvas, tooltipEl);
    const sampleCount = appState.historyPoints.filter((point) => point[metric.key] != null).length;
    noteEl.textContent = sampleCount ? `${metric.label} 共 ${sampleCount} 个历史点。双击可重置缩放，滚轮可缩放。` : `${metric.label} 正在等待上报。`;

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
      renderHistoryPanels();
    }, { passive: false });

    canvas.addEventListener("mouseleave", () => {
      appState.hoverIndexByMetric[metric.key] = null;
      hideTooltip(tooltipEl);
      drawMetricChart(metric, canvas, tooltipEl);
    });

    canvas.addEventListener("mousemove", (event) => {
      const rect = canvas.getBoundingClientRect();
      // _hitPoints.x 已是 CSS 像素坐标（与 toX() 同空间），直接用鼠标偏移量比较
      const x = event.clientX - rect.left;
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
      if (!nearest || nearestDistance > 36 || nearest.point[metric.key] == null) {
        appState.hoverIndexByMetric[metric.key] = null;
        hideTooltip(tooltipEl);
        drawMetricChart(metric, canvas, tooltipEl);
        return;
      }
      appState.hoverIndexByMetric[metric.key] = nearest.index;
      drawMetricChart(metric, canvas, tooltipEl);
      showTooltip(tooltipEl, metric, nearest.point, { x: nearest.x, y: 34 });
    });

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
  updateHistoryHeader(data);
  renderHistoryPanels();
}

function getDevicePages(catalog, snapshot) {
  const sensorPages = snapshot.sensors.map((sensor) => ({
    key: `sensor:${sensor.key}`,
    label: `${sensor.icon} ${sensor.title}`,
    kind: "sensor",
    sensorKey: sensor.key,
    sensorTitle: sensor.title
  }));
  const controlPages = catalog.id === "yard-01"
    ? [{ key: "control:pump", label: "🧯 水泵控制", kind: "control", controlKey: "pump" }]
    : [];
  return [...sensorPages, ...controlPages];
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

  return `
    <article class="info-card" style="grid-column: span 12; margin-top: 12px;">
      <div class="detail-block-head">
        <div class="detail-block-title">${sensor.icon} ${sensor.title}</div>
        <div class="detail-helper">${sensor.subtitle}</div>
      </div>
      <div class="detail-summary-grid" style="margin-top:14px;">
        ${sensor.metrics.map((metric) => {
          const alertClass = metric.alertLevel ? ` metric-value-${metric.alertLevel}` : "";
          const alertTag = metric.alertLevel === "alert"
            ? ` <span class="alert-badge alert-badge-alert">异常</span>`
            : metric.alertLevel === "warn"
              ? ` <span class="alert-badge alert-badge-warn">警告</span>`
              : "";
          return `
          <article class="detail-stat">
            <div class="detail-stat-label">${metric.label}${alertTag}</div>
            <div class="detail-stat-value${alertClass}">${formatSensorMetricValue(sensor.key, metric.key, metric.value, metric.unit)}</div>
          </article>
          `;
        }).join("")}
      </div>
      <div class="info-list" style="margin-top:12px;">
        <div class="info-row"><span class="info-label">状态</span><strong>${sensor.online ? "在线" : "离线"}</strong></div>
        <div class="info-row"><span class="info-label">上报设备</span><strong>${escapeHtml(sensor.source)}</strong></div>
        <div class="info-row"><span class="info-label">最后更新</span><strong>${formatTime(sensor.updatedAt)}</strong></div>
        <div class="info-row"><span class="info-label">MQTT 主题</span><strong>${escapeHtml(sensor.topic)}</strong></div>
        ${sensor.pressureState ? `<div class="info-row"><span class="info-label">补充状态</span><strong>${escapeHtml(sensor.pressureState)}</strong></div>` : ""}
      </div>
    </article>
  `;
}

function renderDevicePages(snapshot, catalog, selectedPageKey) {
  const pages = getDevicePages(catalog, snapshot);
  const currentPage = pages.find((page) => page.key === selectedPageKey) || pages[0] || null;
  const currentSensor = currentPage?.kind === "sensor"
    ? snapshot.sensors.find((sensor) => sensor.key === currentPage.sensorKey)
    : null;

  return `
    <section style="margin-top:20px;">
      <div class="detail-block-head">
        <div class="detail-block-title">设备内分页</div>
        <div class="detail-helper">传感器页和控制页统一作为这个设备的子页面</div>
      </div>
      <div class="history-toolbar">
        ${pages.map((page) => `
          <button class="range-btn ${currentPage?.key === page.key ? "active" : ""}" data-select-page="${page.key}">
            ${page.label}
          </button>
        `).join("")}
      </div>
      ${currentPage?.kind === "control" ? renderPumpControlSection(catalog.id) : renderSensorPageContent(currentSensor)}
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
        <span class="detail-helper">点进的是设备，历史曲线按设备内部的传感器切换。</span>
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

function renderIoTDeviceDetail(deviceId, catalog, snapshot) {
  const pages = getDevicePages(catalog, snapshot);
  const selectedPageKey = appState.activeDevicePageKey || pages[0]?.key || null;
  const selectedSensor = selectedPageKey?.startsWith("sensor:")
    ? snapshot.sensors.find((sensor) => sensor.key === appState.activeSensorKey) || snapshot.sensors[0] || null
    : null;
  return `
    <div class="detail-topbar">
      <div>
        <div class="detail-header-top">
          <div class="detail-icon">${catalog.icon}</div>
          <div class="detail-status ${getStatusClass(snapshot)}"><span class="status-dot"></span>${snapshot.online ? "设备在线" : "设备超时未上报"}</div>
        </div>
        <div class="detail-title">${catalog.title}</div>
        <div class="detail-subtitle">${catalog.subtitle}</div>
        <p class="footer-note">${catalog.summary}</p>
      </div>
      <div class="section-note">归属位置：${roomConfigs.find((room) => room.id === catalog.room)?.name || "未分组"}</div>
    </div>

    <section class="detail-info-grid" style="margin-top:18px;">
      <article class="info-card">
        <div class="detail-block-title">设备总览</div>
        <div class="info-list">
          <div class="info-row"><span class="info-label">设备 ID</span><strong>${catalog.id}</strong></div>
          <div class="info-row"><span class="info-label">最近更新时间</span><strong>${formatTime(snapshot.updatedAt)}</strong></div>
        </div>
      </article>
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
        <div class="detail-title">${catalog.title}</div>
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
        <div class="detail-block-title">天气建议</div>
        <div class="info-list">
          <div class="info-row"><span class="info-label">地点</span><strong>${escapeHtml(weather.location || "--")}</strong></div>
          <div class="info-row"><span class="info-label">建议</span><strong>${escapeHtml(getWeatherSummary(weather))}</strong></div>
          <div class="info-row"><span class="info-label">定位</span><strong>天气也是独立设备入口</strong></div>
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
    if (!appState.activeDevicePageKey || !pageKeys.includes(appState.activeDevicePageKey)) {
      appState.activeDevicePageKey = pages[0]?.key || null;
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
    renderServerCharts();
    return;
  }

  if (catalog.type === "weather") {
    els.detailPanel.innerHTML = renderWeatherDetail(catalog, snapshot);
  }
}

function openDevice(deviceId) {
  if (!deviceCatalog[deviceId]) return;
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
  els.detailView.classList.remove("active");
  els.overviewView.classList.remove("hidden");
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
    const [latestSensor, serverStatus, serverHistory, devicesStatus] = await Promise.all([
      fetchJson("/api/sensor/latest"),
      fetchJson("/api/server/status"),
      fetchJson("/api/server/history"),
      fetchJson("/api/devices/status")
    ]);
    appState.latestSensor = latestSensor;
    appState.serverStatus = serverStatus;
    appState.serverHistory = serverHistory;
    appState.devicesStatus = devicesStatus;
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
