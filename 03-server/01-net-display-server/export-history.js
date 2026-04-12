"use strict";

const fs = require("fs");
const path = require("path");
const { DatabaseSync } = require("node:sqlite");
const { fetchRawSensorHistory, writeHistoryCsvFile } = require("./lib/history-export");

const SENSOR_CONFIG = {
  dht11: {
    label: "DHT11 温湿度",
    metrics: [
      { key: "temperature", label: "温度", unit: "°C" },
      { key: "humidity", label: "湿度", unit: "%RH" }
    ]
  },
  ds18b20: {
    label: "DS18B20 温度",
    metrics: [{ key: "temperature", label: "温度", unit: "°C" }]
  },
  bmp180: {
    label: "BMP180 气压温度",
    metrics: [
      { key: "temperature", label: "BMP 温度", unit: "°C" },
      { key: "pressure", label: "气压", unit: "hPa" }
    ]
  },
  bmp280: {
    label: "BMP280 环境数据",
    metrics: [
      { key: "temperature", label: "BMP280 温度", unit: "°C" },
      { key: "pressure", label: "气压", unit: "hPa" }
    ]
  },
  shtc3: {
    label: "SHTC3 温湿度",
    metrics: [
      { key: "temperature", label: "温度", unit: "°C" },
      { key: "humidity", label: "湿度", unit: "%RH" }
    ]
  },
  bh1750: {
    label: "BH1750 光照",
    metrics: [{ key: "illuminance", label: "光照", unit: "lux" }]
  },
  battery: {
    label: "电池电压",
    metrics: [
      { key: "voltage", label: "电压", unit: "V" },
      { key: "percent", label: "电量", unit: "%" }
    ]
  },
  max17043: {
    label: "MAX17043 电量计",
    metrics: [
      { key: "voltage", label: "电压", unit: "V" },
      { key: "percent", label: "电量", unit: "%" }
    ]
  },
  ina226: {
    label: "INA226 电流电压",
    metrics: [
      { key: "busVoltage", label: "母线电压", unit: "V" },
      { key: "currentMa", label: "电流", unit: "mA" },
      { key: "powerMw", label: "功率", unit: "mW" }
    ]
  }
};

const DEVICE_ALIAS_BY_ID = {
  "explorer-gateway": "探索者网关",
  "yard-01": "庭院 1 号",
  "study-01": "书房 1 号",
  "office-01": "办公室 1 号",
  "bedroom-01": "卧室 1 号"
};

const DEVICE_SENSORS = {
  "explorer-gateway": [],
  "yard-01": ["dht11", "battery"],
  "study-01": ["dht11", "max17043", "ina226"],
  "office-01": ["dht11"],
  "bedroom-01": ["dht11"]
};

function parseArgs(argv) {
  const args = {};
  for (let index = 0; index < argv.length; index += 1) {
    const token = argv[index];
    if (!token.startsWith("--")) {
      continue;
    }
    const key = token.slice(2);
    const next = argv[index + 1];
    if (!next || next.startsWith("--")) {
      args[key] = "true";
      continue;
    }
    args[key] = next;
    index += 1;
  }
  return args;
}

function printUsage() {
  console.log("用法:");
  console.log("  node export-history.js --device yard-01 --sensor all --range all");
  console.log("  node export-history.js --device yard-01 --sensor dht11 --range 24h");
  console.log("  node export-history.js --device yard-01 --sensor dht11 --date 2026-04-07");
}

function main() {
  const serverDir = __dirname;
  const repoRootDir = path.resolve(serverDir, "..", "..");
  const dbPath = path.join(serverDir, "data", "sensor-history.db");
  const exportRootDir = path.join(repoRootDir, "data");
  const args = parseArgs(process.argv.slice(2));

  if (args.help === "true") {
    printUsage();
    return;
  }

  if (!fs.existsSync(dbPath)) {
    console.error(`未找到历史数据库: ${dbPath}`);
    console.error("当前仓库没有同步服务器上的 SQLite 数据库，所以暂时无法导出真实历史数据。");
    process.exitCode = 1;
    return;
  }

  const deviceId = String(args.device || "yard-01").trim();
  const sensorArg = String(args.sensor || "all").trim();
  const range = String(args.range || "all").trim();
  const date = String(args.date || "").trim();
  const metricKey = args.metric ? String(args.metric).trim() : null;
  const deviceLabel = DEVICE_ALIAS_BY_ID[deviceId] || deviceId;
  const sensorKeys = sensorArg === "all"
    ? (DEVICE_SENSORS[deviceId] || Object.keys(SENSOR_CONFIG))
    : sensorArg.split(",").map((value) => value.trim()).filter(Boolean);

  if (!sensorKeys.length) {
    console.error(`没有可导出的传感器: ${deviceId}`);
    process.exitCode = 1;
    return;
  }

  const db = new DatabaseSync(dbPath, { readonly: true });
  try {
    sensorKeys.forEach((sensorKey) => {
      const history = fetchRawSensorHistory({
        db,
        sensorConfigMap: SENSOR_CONFIG,
        sensorKey,
        deviceNames: [deviceId],
        range,
        date,
        metricKey
      });
      const saved = writeHistoryCsvFile({
        exportRootDir,
        deviceLabel,
        history,
        metricKey
      });
      console.log(`${deviceLabel} / ${history.sensorLabel}: ${saved.filePath} (${saved.rowCount} 行)`);
    });
  } finally {
    db.close();
  }
}

main();
