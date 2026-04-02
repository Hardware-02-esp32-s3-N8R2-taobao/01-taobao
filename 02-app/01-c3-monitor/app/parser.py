from __future__ import annotations

import json
import re
from copy import deepcopy
from dataclasses import dataclass
from datetime import datetime
from typing import Any


ANSI_RE = re.compile(r"\x1b\[[0-9;?]*[ -/]*[@-~]")
BOOT_RE = re.compile(r"boot:\s*0x([0-9a-fA-F]+)\s*\(([^)]+)\)")
WIFI_CONNECTED_RE = re.compile(r"WiFi connected,(?: ssid=(.*?) )?ip=([0-9.]+)")
WIFI_DISCONNECTED_RE = re.compile(r"WiFi disconnected, reason=([-0-9]+)")
DHT_RE = re.compile(r"DHT11 sample: temperature=([-0-9.]+)\s*C humidity=([-0-9.]+)\s*%RH")
PUBLISH_RE = re.compile(r"publish ok:\s*(\{.*\})")
APP_LINE_RE = re.compile(r"APP_(CONFIG|STATUS|OPTIONS|EVENT|WIFI_LIST|LOW_POWER|OK|ERROR):")


def now_text() -> str:
    return datetime.now().strftime("%Y-%m-%d %H:%M:%S")


def clean_serial_line(line: str) -> str:
    text = ANSI_RE.sub("", line or "")
    return text.replace("\u0000", "").strip()


def wifi_reason_text(reason: Any) -> str:
    try:
        code = int(reason)
    except (TypeError, ValueError):
        return str(reason) if reason not in (None, "") else "--"

    reason_map = {
        0: "--",
        2: "认证过期",
        3: "AP 主动断开",
        4: "关联过期",
        15: "4 次握手超时",
        201: "未扫描到目标 AP",
        202: "认证失败，密码可能不对",
        203: "关联失败",
        204: "握手超时",
        205: "AP 与 STA 不兼容",
    }
    return reason_map.get(code, str(code))


def default_state() -> dict[str, Any]:
    return {
        "port_name": "--",
        "baudrate": 115200,
        "serial_connected": False,
        "last_line": "",
        "last_seen_at": "",
        "boot_mode": "未识别",
        "boot_detail": "",
        "wifi": {
            "status": "未连接",
            "ip": "--",
            "reason": "--",
            "ssid": "--",
            "updated_at": "",
        },
        "mqtt": {
            "status": "未连接",
            "broker": "mqtt://117.72.55.63:1884",
            "topic": "device/yard-01",
            "updated_at": "",
        },
        "sensor": {
            "status": "等待数据",
            "sensor_count": 1,
            "sensor_models": ["dht11"],
            "ready_count": 0,
            "readings": {},
            "fail_counts": {},
            "updated_at": "",
        },
        "publish": {
            "status": "等待上报",
            "device": "--",
            "alias": "--",
            "rssi": None,
            "sensors": {},
            "updated_at": "",
            "payload_text": "--",
        },
        "config": {
            "device_id": "yard-01",
            "device_name": "庭院1号",
            "device_alias": "庭院 1 号",
            "sensors": ["dht11"],
        },
        "hardware": {
            "chip_model": "--",
            "fw_version": "--",
            "target": "--",
            "cores": None,
            "revision": None,
            "mac": "--",
        },
        "options": {
            "device_names": ["庭院1号", "卧室1号", "书房1号", "办公室1号"],
            "sensor_types": ["dht11", "ds18b20", "bh1750", "bmp180", "shtc3", "soil_moisture", "rain_sensor", "battery", "max17043", "ina226"],
        },
        "alerts": [],
        "wifi_list": [],
        "wifi_scan": [],
        "low_power": {
            "enabled": False,
            "interval_sec": 300,
        },
    }


@dataclass
class ParseResult:
    changed: bool
    state: dict[str, Any]
    cleaned_line: str


class StatusParser:
    def __init__(self) -> None:
        self._state = default_state()

    @property
    def state(self) -> dict[str, Any]:
        return deepcopy(self._state)

    def set_connection(self, port_name: str, baudrate: int, connected: bool) -> dict[str, Any]:
        self._state["port_name"] = port_name or "--"
        self._state["baudrate"] = baudrate
        self._state["serial_connected"] = connected
        if connected:
            self._push_alert(f"已连接串口 {port_name} @ {baudrate}")
        else:
            self._push_alert("串口已断开")
        return self.state

    def parse_line(self, line: str) -> ParseResult:
        text = clean_serial_line(line)
        if not text:
            return ParseResult(False, self.state, "")

        changed = False
        self._state["last_line"] = text
        self._state["last_seen_at"] = now_text()
        if self._state["boot_mode"] == "未识别":
            self._state["boot_mode"] = "运行中"

        for app_type, raw_payload in self._extract_app_segments(text):
            if app_type == "CONFIG":
                payload = self._safe_load_json(raw_payload)
                self._apply_config_payload(payload)
                changed = True
            elif app_type == "STATUS":
                payload = self._safe_load_json(raw_payload)
                self._apply_status_payload(payload)
                changed = True
            elif app_type == "OPTIONS":
                payload = self._safe_load_json(raw_payload)
                self._apply_options_payload(payload)
                changed = True
            elif app_type == "EVENT":
                payload = self._safe_load_json(raw_payload)
                self._apply_event_payload(payload)
                changed = True
            elif app_type == "WIFI_LIST":
                try:
                    data = json.loads(raw_payload)
                    if isinstance(data, list):
                        self._state["wifi_list"] = [
                            {"ssid": str(e.get("ssid", "")), "password": str(e.get("password", ""))}
                            for e in data if isinstance(e, dict) and e.get("ssid")
                        ]
                        changed = True
                except json.JSONDecodeError:
                    pass
            elif app_type == "LOW_POWER":
                payload = self._safe_load_json(raw_payload)
                self._apply_low_power_payload(payload)
                changed = True
            elif app_type == "OK":
                payload = self._safe_load_json(raw_payload)
                self._push_alert(payload.get("message", "操作成功"))
                changed = True
            elif app_type == "ERROR":
                payload = self._safe_load_json(raw_payload)
                self._push_alert(f"错误：{payload.get('message', '未知错误')}")
                changed = True

        if match := BOOT_RE.search(text):
            changed = True
            self._state["boot_mode"] = match.group(2)
            self._state["boot_detail"] = f"boot:0x{match.group(1)}"
            self._push_alert(f"检测到启动模式：{match.group(2)}")

        if match := WIFI_CONNECTED_RE.search(text):
            changed = True
            self._state["wifi"]["status"] = "已连接"
            ssid = match.group(1)
            if ssid:
                self._state["wifi"]["ssid"] = ssid
            self._state["wifi"]["ip"] = match.group(2)
            self._state["wifi"]["reason"] = "--"
            self._state["wifi"]["updated_at"] = now_text()

        if match := WIFI_DISCONNECTED_RE.search(text):
            changed = True
            self._state["wifi"]["status"] = "已断开"
            self._state["wifi"]["reason"] = wifi_reason_text(match.group(1))
            self._state["wifi"]["updated_at"] = now_text()
            self._state["mqtt"]["status"] = "未连接"
            self._state["mqtt"]["updated_at"] = now_text()

        if "MQTT connected" in text:
            changed = True
            self._state["mqtt"]["status"] = "已连接"
            self._state["mqtt"]["updated_at"] = now_text()

        if "MQTT disconnected" in text:
            changed = True
            self._state["mqtt"]["status"] = "已断开"
            self._state["mqtt"]["updated_at"] = now_text()

        if match := DHT_RE.search(text):
            changed = True
            self._state["sensor"]["status"] = "采样正常"
            self._state["sensor"]["temperature"] = float(match.group(1))
            self._state["sensor"]["humidity"] = float(match.group(2))
            self._state["sensor"]["updated_at"] = now_text()

        if "waiting dht11/wifi/mqtt" in text:
            changed = True
            self._state["sensor"]["status"] = "等待链路就绪"

        if match := PUBLISH_RE.search(text):
            changed = True
            payload_text = match.group(1)
            payload = self._safe_load_json(payload_text)
            self._state["publish"]["status"] = "上报成功"
            self._state["publish"]["payload_text"] = payload_text
            self._state["publish"]["updated_at"] = now_text()
            self._state["mqtt"]["status"] = "已连接"
            self._state["mqtt"]["updated_at"] = now_text()
            self._state["wifi"]["status"] = "已连接"
            self._state["wifi"]["updated_at"] = now_text()
            if payload:
                self._state["publish"]["device"] = payload.get("device", "--")
                self._state["publish"]["alias"] = payload.get("alias", "--")
                sensors = payload.get("sensors") or {}
                dht11 = sensors.get("dht11") if isinstance(sensors, dict) else {}
                self._state["publish"]["temperature"] = (
                    dht11.get("temperature") if isinstance(dht11, dict) else payload.get("temperature")
                )
                self._state["publish"]["humidity"] = (
                    dht11.get("humidity") if isinstance(dht11, dict) else payload.get("humidity")
                )
                self._state["publish"]["rssi"] = payload.get("rssi")

        return ParseResult(changed, self.state, text)

    def _apply_config_payload(self, payload: dict[str, Any]) -> None:
        config = self._state["config"]
        hardware = self._state["hardware"]
        config["device_id"] = payload.get("deviceId", config["device_id"])
        config["device_name"] = payload.get("deviceName", config["device_name"])
        config["device_alias"] = payload.get("deviceAlias", config["device_alias"])
        sensors = payload.get("sensors")
        if isinstance(sensors, list) and sensors:
            config["sensors"] = [str(item) for item in sensors]
        self._state["sensor"]["sensor_models"] = list(config["sensors"])
        self._state["sensor"]["sensor_count"] = len(config["sensors"])
        hw = payload.get("hardware") or {}
        hardware["chip_model"] = hw.get("chipModel", hardware["chip_model"])
        hardware["fw_version"] = hw.get("fwVersion", hardware["fw_version"])
        hardware["target"] = hw.get("target", hardware["target"])
        hardware["cores"] = hw.get("cores", hardware["cores"])
        hardware["revision"] = hw.get("revision", hardware["revision"])
        hardware["mac"] = hw.get("mac", hardware["mac"])
        self._apply_low_power_payload(payload.get("lowPower") or {})

    def _apply_status_payload(self, payload: dict[str, Any]) -> None:
        self._apply_config_payload(payload)
        wifi = payload.get("wifi") or {}
        mqtt = payload.get("mqtt") or {}
        sensors_data = payload.get("sensorsData") or {}
        publish = payload.get("publish") or {}

        self._state["wifi"]["status"] = "已连接" if wifi.get("connected") else "未连接"
        ssid = wifi.get("ssid")
        if ssid not in (None, "", "--"):
            self._state["wifi"]["ssid"] = str(ssid).strip()
        elif not wifi.get("connected"):
            self._state["wifi"]["ssid"] = "--"
        self._state["wifi"]["ip"] = wifi.get("ip", self._state["wifi"]["ip"])
        reason = wifi.get("disconnectReason")
        self._state["wifi"]["reason"] = wifi_reason_text(reason)
        self._state["wifi"]["updated_at"] = now_text()

        self._state["mqtt"]["status"] = "已连接" if mqtt.get("connected") else "未连接"
        self._state["mqtt"]["broker"] = mqtt.get("broker", self._state["mqtt"]["broker"])
        self._state["mqtt"]["topic"] = mqtt.get("topic", self._state["mqtt"]["topic"])
        self._state["mqtt"]["updated_at"] = now_text()

        self._state["sensor"]["ready_count"] = int(payload.get("sensorReadyCount", 0) or 0)
        self._state["sensor"]["sensor_count"] = int(payload.get("sensorTotalCount", len(self._state["config"]["sensors"])) or 0)
        self._state["sensor"]["status"] = "采样正常" if self._state["sensor"]["ready_count"] > 0 else "等待数据"
        self._merge_sensor_readings(sensors_data if isinstance(sensors_data, dict) else {})
        self._state["sensor"]["updated_at"] = now_text()

        self._state["publish"]["status"] = "上报成功" if publish.get("ready") else "等待上报"
        self._state["publish"]["rssi"] = publish.get("rssi")
        self._state["publish"]["payload_text"] = publish.get("payload", self._state["publish"]["payload_text"])
        self._state["publish"]["device"] = self._state["config"]["device_id"]
        self._state["publish"]["alias"] = self._state["config"]["device_alias"]
        publish_payload = self._safe_load_json(self._state["publish"]["payload_text"])
        if publish_payload:
            sensors = publish_payload.get("sensors")
            if isinstance(sensors, dict):
                self._state["publish"]["sensors"] = sensors
        self._state["publish"]["updated_at"] = now_text()

    def _apply_event_payload(self, payload: dict[str, Any]) -> None:
        event_type = str(payload.get("type", "")).strip()
        data = payload.get("data")
        if not event_type or not isinstance(data, dict):
            return

        if event_type == "wifi":
            self._state["wifi"]["status"] = "已连接" if data.get("connected") else "未连接"
            ssid = data.get("ssid")
            if ssid not in (None, "", "--"):
                self._state["wifi"]["ssid"] = str(ssid)
            elif not data.get("connected"):
                self._state["wifi"]["ssid"] = "--"
            self._state["wifi"]["ip"] = str(data.get("ip", "--")) or "--"
            reason = data.get("reason")
            self._state["wifi"]["reason"] = wifi_reason_text(reason)
            self._state["wifi"]["updated_at"] = now_text()
        elif event_type == "mqtt":
            self._state["mqtt"]["status"] = "已连接" if data.get("connected") else "未连接"
            self._state["mqtt"]["broker"] = str(data.get("broker", self._state["mqtt"]["broker"]))
            self._state["mqtt"]["topic"] = str(data.get("topic", self._state["mqtt"]["topic"]))
            self._state["mqtt"]["updated_at"] = now_text()
        elif event_type == "sensor":
            sensor_type = data.get("sensorType")
            if sensor_type:
                self._merge_sensor_readings({str(sensor_type): {k: v for k, v in data.items() if k != "sensorType"}})
            self._state["sensor"]["status"] = "采样正常" if data.get("ready") else "读取失败"
            self._state["sensor"]["updated_at"] = now_text()
        elif event_type == "publish":
            self._state["publish"]["status"] = "上报成功" if data.get("ready") else "等待上报"
            payload_data = data.get("payload")
            if isinstance(payload_data, dict):
                self._state["publish"]["payload_text"] = json.dumps(payload_data, ensure_ascii=False)
                self._state["publish"]["device"] = payload_data.get("device", self._state["publish"]["device"])
                self._state["publish"]["alias"] = payload_data.get("alias", self._state["publish"]["alias"])
                self._state["publish"]["rssi"] = payload_data.get("rssi")
                sensors = payload_data.get("sensors") or {}
                if isinstance(sensors, dict):
                    self._state["publish"]["sensors"] = sensors
            self._state["publish"]["updated_at"] = now_text()
        elif event_type == "wifi_scan":
            access_points = data.get("accessPoints")
            if isinstance(access_points, list):
                self._state["wifi_scan"] = access_points
                self._push_alert(f"扫描到 {len(access_points)} 个 WiFi")

    def _apply_options_payload(self, payload: dict[str, Any]) -> None:
        device_names = payload.get("deviceNames")
        sensor_types = payload.get("sensorTypes")
        if isinstance(device_names, list) and device_names:
            self._state["options"]["device_names"] = [str(item) for item in device_names]
        if isinstance(sensor_types, list) and sensor_types:
            self._state["options"]["sensor_types"] = [str(item) for item in sensor_types]

    def _apply_low_power_payload(self, payload: dict[str, Any]) -> None:
        if not isinstance(payload, dict):
            return
        low_power = self._state["low_power"]
        if "lowPower" in payload and isinstance(payload.get("lowPower"), dict):
            payload = payload["lowPower"]
        low_power["enabled"] = bool(payload.get("enabled", low_power["enabled"]))
        interval = payload.get("intervalSec", low_power["interval_sec"])
        if isinstance(interval, (int, float)) and int(interval) > 0:
            low_power["interval_sec"] = int(interval)

    def _merge_sensor_readings(self, incoming: dict[str, Any]) -> None:
        readings = self._state["sensor"].setdefault("readings", {})
        fail_counts = self._state["sensor"].setdefault("fail_counts", {})
        for sensor_key, reading in incoming.items():
            if not isinstance(reading, dict):
                continue
            if reading.get("ready"):
                readings[sensor_key] = dict(reading)
                fail_counts[sensor_key] = 0
                continue

            fail_counts[sensor_key] = int(fail_counts.get(sensor_key, 0)) + 1
            if sensor_key not in readings or fail_counts[sensor_key] >= 3:
                readings[sensor_key] = dict(reading)

    def _safe_load_json(self, payload_text: str) -> dict[str, Any]:
        try:
            data = json.loads(payload_text)
        except json.JSONDecodeError:
            return {}
        return data if isinstance(data, dict) else {}

    def _extract_app_segments(self, text: str) -> list[tuple[str, str]]:
        matches = list(APP_LINE_RE.finditer(text))
        if not matches:
            return []

        segments: list[tuple[str, str]] = []
        for index, match in enumerate(matches):
            start = match.end()
            end = matches[index + 1].start() if index + 1 < len(matches) else len(text)
            payload = text[start:end].strip()
            if payload:
                segments.append((match.group(1), payload))
        return segments

    def _push_alert(self, text: str) -> None:
        alerts = self._state["alerts"]
        alerts.insert(0, f"{now_text()}  {text}")
        del alerts[12:]
