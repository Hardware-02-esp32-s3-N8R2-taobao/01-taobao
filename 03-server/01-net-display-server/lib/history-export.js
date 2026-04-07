"use strict";

const fs = require("fs");
const path = require("path");

const RANGE_MAP = {
  "1h": { label: "最近1小时", durationMs: 60 * 60 * 1000, bucketMinutes: 2, fileLabel: "1h" },
  "24h": { label: "最近24小时", durationMs: 24 * 60 * 60 * 1000, bucketMinutes: 10, fileLabel: "24h" },
  "3d": { label: "最近3天", durationMs: 3 * 24 * 60 * 60 * 1000, bucketMinutes: 30, fileLabel: "3d" },
  "7d": { label: "最近7天", durationMs: 7 * 24 * 60 * 60 * 1000, bucketMinutes: 60, fileLabel: "7d" },
  all: { label: "全部历史", durationMs: null, bucketMinutes: 120, fileLabel: "all" }
};

function resolveHistoryWindow({ range = "24h", date = "" }, now = Date.now()) {
  const dateText = String(date || "").trim();
  if (dateText) {
    const startTs = new Date(`${dateText}T00:00:00`).getTime();
    if (!Number.isFinite(startTs)) {
      throw new Error("invalid date");
    }
    return {
      mode: "date",
      key: "date",
      label: `指定日期 ${dateText}`,
      fileLabel: dateText,
      startTs,
      endTs: startTs + 24 * 60 * 60 * 1000,
      bucketMinutes: 10
    };
  }

  const rangeKey = String(range || "24h").trim().toLowerCase();
  const config = RANGE_MAP[rangeKey];
  if (!config) {
    throw new Error("invalid range");
  }

  return {
    mode: "range",
    key: rangeKey,
    label: config.label,
    fileLabel: config.fileLabel,
    startTs: config.durationMs == null ? 0 : now - config.durationMs,
    endTs: now,
    bucketMinutes: config.bucketMinutes
  };
}

function resolveAbsoluteWindow({ startAt = "", endAt = "" }) {
  const startTs = new Date(String(startAt || "").trim()).getTime();
  const endTs = new Date(String(endAt || "").trim()).getTime();
  if (!Number.isFinite(startTs) || !Number.isFinite(endTs)) {
    throw new Error("invalid datetime range");
  }
  if (endTs <= startTs) {
    throw new Error("endAt must be later than startAt");
  }
  return {
    mode: "absolute",
    key: "absolute",
    label: `${startAt} ~ ${endAt}`,
    fileLabel: `${startAt}_${endAt}`,
    startTs,
    endTs,
    bucketMinutes: null
  };
}

function fetchSensorHistory({ db, sensorConfigMap, sensorKey, deviceNames = [], range = "24h", date = "" }) {
  const config = sensorConfigMap[sensorKey];
  if (!config) {
    throw new Error("invalid series");
  }

  const window = resolveHistoryWindow({ range, date });
  const metricKeys = config.metrics.map((metric) => metric.key);
  const bucketMs = window.bucketMinutes * 60 * 1000;
  const placeholders = metricKeys.map(() => "?").join(", ");
  const normalizedDevices = deviceNames
    .map((value) => String(value || "").trim())
    .filter(Boolean);
  const deviceClause = normalizedDevices.length
    ? `AND source IN (${normalizedDevices.map(() => "?").join(", ")})`
    : "";
  const historyParams = normalizedDevices.length
    ? [sensorKey, ...metricKeys, ...normalizedDevices, window.startTs, window.endTs, bucketMs]
    : [sensorKey, ...metricKeys, window.startTs, window.endTs, bucketMs];

  const rows = db.prepare(`
    SELECT
      MIN(ts_ms) AS tsMs,
      metric_key AS metricKey,
      ROUND(AVG(value), 3) AS value,
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
    const current = pointsByTs.get(row.tsMs) || { tsMs: row.tsMs, sampleCount: 0 };
    current[row.metricKey] = row.value;
    current.sampleCount = Math.max(current.sampleCount, Number(row.sampleCount || 0));
    pointsByTs.set(row.tsMs, current);
  });

  return {
    mode: window.mode,
    windowLabel: window.label,
    windowFileLabel: window.fileLabel,
    startTs: window.startTs,
    endTs: window.endTs,
    bucketMinutes: window.bucketMinutes,
    sensorKey,
    sensorLabel: config.label,
    metrics: config.metrics,
    devices: normalizedDevices,
    points: Array.from(pointsByTs.values()).sort((a, b) => a.tsMs - b.tsMs)
  };
}

function fetchRawSensorHistory({ db, sensorConfigMap, sensorKey, deviceNames = [], range = "24h", date = "", metricKey = null }) {
  const config = sensorConfigMap[sensorKey];
  if (!config) {
    throw new Error("invalid series");
  }

  const window = resolveHistoryWindow({ range, date });
  const metricKeys = metricKey ? [metricKey] : config.metrics.map((metric) => metric.key);
  const placeholders = metricKeys.map(() => "?").join(", ");
  const normalizedDevices = deviceNames
    .map((value) => String(value || "").trim())
    .filter(Boolean);
  const deviceClause = normalizedDevices.length
    ? `AND source IN (${normalizedDevices.map(() => "?").join(", ")})`
    : "";
  const queryParams = normalizedDevices.length
    ? [sensorKey, ...metricKeys, ...normalizedDevices, window.startTs, window.endTs]
    : [sensorKey, ...metricKeys, window.startTs, window.endTs];

  const rows = db.prepare(`
    SELECT
      ts_ms AS tsMs,
      metric_key AS metricKey,
      value,
      recorded_at AS recordedAt
    FROM metric_readings
    WHERE sensor_key = ?
      AND metric_key IN (${placeholders})
      ${deviceClause}
      AND ts_ms >= ?
      AND ts_ms < ?
    ORDER BY ts_ms ASC, metric_key ASC
  `).all(...queryParams);

  if (metricKey) {
    return {
      mode: window.mode,
      windowLabel: window.label,
      windowFileLabel: window.fileLabel,
      startTs: window.startTs,
      endTs: window.endTs,
      bucketMinutes: null,
      sensorKey,
      sensorLabel: config.label,
      metrics: config.metrics.filter((metric) => metric.key === metricKey),
      devices: normalizedDevices,
      points: rows.map((row) => ({
        tsMs: row.tsMs,
        recordedAt: row.recordedAt || new Date(row.tsMs).toISOString(),
        [row.metricKey]: row.value,
        sampleCount: 1
      }))
    };
  }

  const pointsByTs = new Map();
  rows.forEach((row) => {
    const current = pointsByTs.get(row.tsMs) || {
      tsMs: row.tsMs,
      recordedAt: row.recordedAt || new Date(row.tsMs).toISOString(),
      sampleCount: 0
    };
    current[row.metricKey] = row.value;
    current.sampleCount += 1;
    pointsByTs.set(row.tsMs, current);
  });

  return {
    mode: window.mode,
    windowLabel: window.label,
    windowFileLabel: window.fileLabel,
    startTs: window.startTs,
    endTs: window.endTs,
    bucketMinutes: null,
    sensorKey,
    sensorLabel: config.label,
    metrics: config.metrics,
    devices: normalizedDevices,
    points: Array.from(pointsByTs.values()).sort((a, b) => a.tsMs - b.tsMs)
  };
}

function sanitizeFileName(value) {
  return String(value || "")
    .replace(/[<>:"/\\|?*\x00-\x1f]/g, "-")
    .replace(/\s+/g, " ")
    .trim() || "unnamed";
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

function buildHistoryCsvText(history, metricKey = null) {
  const metrics = metricKey
    ? history.metrics.filter((metric) => metric.key === metricKey)
    : history.metrics.slice();

  if (!metrics.length) {
    throw new Error("invalid metric");
  }

  const header = [
    "recorded_at",
    ...metrics.map((metric) => `${metric.key}_${metric.label}_${metric.unit}`),
    "sample_count"
  ];

  const lines = [
    header.map(csvEscape).join(","),
    ...history.points.map((point) => {
      const row = [
        new Date(point.tsMs).toISOString(),
        ...metrics.map((metric) => point[metric.key] ?? ""),
        point.sampleCount ?? 0
      ];
      return row.map(csvEscape).join(",");
    })
  ];

  return `\ufeff${lines.join("\n")}\n`;
}

function writeHistoryCsvFile({ exportRootDir, deviceLabel, history, metricKey = null }) {
  const selectedMetric = metricKey
    ? history.metrics.find((metric) => metric.key === metricKey) || null
    : null;
  if (metricKey && !selectedMetric) {
    throw new Error("invalid metric");
  }

  const deviceDirName = sanitizeFileName(deviceLabel);
  const fileNameParts = [history.sensorLabel];
  if (selectedMetric) {
    fileNameParts.push(selectedMetric.label);
  }
  fileNameParts.push(history.windowFileLabel || "export");
  const fileName = `${sanitizeFileName(fileNameParts.join("_"))}.csv`;
  const targetDir = path.join(exportRootDir, deviceDirName);
  const filePath = path.join(targetDir, fileName);

  fs.mkdirSync(targetDir, { recursive: true });
  fs.writeFileSync(filePath, buildHistoryCsvText(history, metricKey), "utf8");

  return {
    filePath,
    fileName,
    deviceDir: targetDir,
    rowCount: history.points.length,
    metricKey: selectedMetric?.key || null,
    metricLabel: selectedMetric?.label || null
  };
}

module.exports = {
  buildHistoryCsvText,
  fetchRawSensorHistory,
  fetchSensorHistory,
  resolveAbsoluteWindow,
  resolveHistoryWindow,
  sanitizeFileName,
  writeHistoryCsvFile
};
