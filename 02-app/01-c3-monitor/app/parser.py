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
APP_LINE_RE = re.compile(r"APP_(CONFIG|STATUS|OPTIONS|EVENT|WIFI_LIST|OK|ERROR):")


def now_text() -> str:
    return datetime.now().strftime("%Y-%m-%d %H:%M:%S")


def clean_serial_line(line: str) -> str:
    text = ANSI_RE.sub("", line or "")
    return text.replace("\u0000", "").strip()


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
            "temperature": None,
            "humidity": None,
            "updated_at": "",
        },
        "publish": {
            "status": "等待上报",
            "device": "--",
            "alias": "--",
            "temperature": None,
            "humidity": None,
            "rssi": None,
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
            "sensor_types": ["dht11", "ds18b20", "bh1750", "bmp180", "bmp280", "bme280", "soil_moisture", "rain_sensor"],
        },
        "alerts": [],
        "wifi_list": [],
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
            self._state["wifi"]["reason"] = match.group(1)
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

    def _apply_status_payload(self, payload: dict[str, Any]) -> None:
        self._apply_config_payload(payload)
        wifi = payload.get("wifi") or {}
        mqtt = payload.get("mqtt") or {}
        dht11 = payload.get("dht11") or {}
        publish = payload.get("publish") or {}

        self._state["wifi"]["status"] = "已连接" if wifi.get("connected") else "未连接"
        ssid = wifi.get("ssid")
        if ssid not in (None, "", "--"):
            self._state["wifi"]["ssid"] = str(ssid).strip()
        elif not wifi.get("connected"):
            self._state["wifi"]["ssid"] = "--"
        self._state["wifi"]["ip"] = wifi.get("ip", self._state["wifi"]["ip"])
        reason = wifi.get("disconnectReason")
        self._state["wifi"]["reason"] = "--" if reason in (None, 0) else str(reason)
        self._state["wifi"]["updated_at"] = now_text()

        self._state["mqtt"]["status"] = "已连接" if mqtt.get("connected") else "未连接"
        self._state["mqtt"]["broker"] = mqtt.get("broker", self._state["mqtt"]["broker"])
        self._state["mqtt"]["topic"] = mqtt.get("topic", self._state["mqtt"]["topic"])
        self._state["mqtt"]["updated_at"] = now_text()

        self._state["sensor"]["status"] = "采样正常" if dht11.get("ready") else "等待数据"
        self._state["sensor"]["temperature"] = dht11.get("temperature")
        self._state["sensor"]["humidity"] = dht11.get("humidity")
        self._state["sensor"]["updated_at"] = now_text()

        self._state["publish"]["status"] = "上报成功" if publish.get("ready") else "等待上报"
        self._state["publish"]["temperature"] = publish.get("temperature")
        self._state["publish"]["humidity"] = publish.get("humidity")
        self._state["publish"]["rssi"] = publish.get("rssi")
        self._state["publish"]["payload_text"] = publish.get("payload", self._state["publish"]["payload_text"])
        self._state["publish"]["device"] = self._state["config"]["device_id"]
        self._state["publish"]["alias"] = self._state["config"]["device_alias"]
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
            self._state["wifi"]["reason"] = "--" if reason in (None, 0) else str(reason)
            self._state["wifi"]["updated_at"] = now_text()
        elif event_type == "mqtt":
            self._state["mqtt"]["status"] = "已连接" if data.get("connected") else "未连接"
            self._state["mqtt"]["broker"] = str(data.get("broker", self._state["mqtt"]["broker"]))
            self._state["mqtt"]["topic"] = str(data.get("topic", self._state["mqtt"]["topic"]))
            self._state["mqtt"]["updated_at"] = now_text()
        elif event_type == "sensor":
            self._state["sensor"]["status"] = "采样正常" if data.get("ready") else "读取失败"
            self._state["sensor"]["temperature"] = data.get("temperature")
            self._state["sensor"]["humidity"] = data.get("humidity")
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
                dht11 = sensors.get("dht11") if isinstance(sensors, dict) else {}
                if isinstance(dht11, dict):
                    self._state["publish"]["temperature"] = dht11.get("temperature")
                    self._state["publish"]["humidity"] = dht11.get("humidity")
            self._state["publish"]["updated_at"] = now_text()

    def _apply_options_payload(self, payload: dict[str, Any]) -> None:
        device_names = payload.get("deviceNames")
        sensor_types = payload.get("sensorTypes")
        if isinstance(device_names, list) and device_names:
            self._state["options"]["device_names"] = [str(item) for item in device_names]
        if isinstance(sensor_types, list) and sensor_types:
            self._state["options"]["sensor_types"] = [str(item) for item in sensor_types]

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
