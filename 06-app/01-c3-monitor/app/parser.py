from __future__ import annotations

import json
import re
from copy import deepcopy
from dataclasses import dataclass
from datetime import datetime
from typing import Any


ANSI_RE = re.compile(r"\x1b\[[0-9;?]*[ -/]*[@-~]")
BOOT_RE = re.compile(r"boot:\s*0x([0-9a-fA-F]+)\s*\(([^)]+)\)")
WIFI_CONNECTED_RE = re.compile(r"WiFi connected, ip=([0-9.]+)")
WIFI_DISCONNECTED_RE = re.compile(r"WiFi disconnected, reason=([-0-9]+)")
DHT_RE = re.compile(r"DHT11 sample: temperature=([-0-9.]+)\s*C humidity=([-0-9.]+)\s*%RH")
PUBLISH_RE = re.compile(r"publish ok:\s*(\{.*\})")


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
            "ssid": "ggg",
            "updated_at": "",
        },
        "mqtt": {
            "status": "未连接",
            "broker": "ws://117.72.55.63/mqtt",
            "topic": "garden/flower/dht11",
            "updated_at": "",
        },
        "sensor": {
            "status": "等待数据",
            "temperature": None,
            "humidity": None,
            "updated_at": "",
        },
        "publish": {
            "status": "等待上报",
            "device": "--",
            "alias": "--",
            "source": "--",
            "temperature": None,
            "humidity": None,
            "rssi": None,
            "ip": "--",
            "updated_at": "",
            "payload_text": "--",
        },
        "config": {
            "device_id": "yardHub",
            "device_name": "庭院1号",
            "device_alias": "庭院1号设备",
            "device_source": "yard-1-flower-c3",
            "sensors": ["dht11"],
        },
        "hardware": {
            "chip_model": "--",
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

        if text.startswith("APP_CONFIG:"):
            payload = self._safe_load_json(text[len("APP_CONFIG:"):])
            self._apply_config_payload(payload)
            changed = True

        elif text.startswith("APP_STATUS:"):
            payload = self._safe_load_json(text[len("APP_STATUS:"):])
            self._apply_status_payload(payload)
            changed = True

        elif text.startswith("APP_OPTIONS:"):
            payload = self._safe_load_json(text[len("APP_OPTIONS:"):])
            self._apply_options_payload(payload)
            changed = True

        elif text.startswith("APP_OK:"):
            payload = self._safe_load_json(text[len("APP_OK:"):])
            self._push_alert(payload.get("message", "操作成功"))
            changed = True

        elif text.startswith("APP_ERROR:"):
            payload = self._safe_load_json(text[len("APP_ERROR:"):])
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
            self._state["wifi"]["ip"] = match.group(1)
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
                self._state["publish"]["source"] = payload.get("source", "--")
                self._state["publish"]["temperature"] = payload.get("temperature")
                self._state["publish"]["humidity"] = payload.get("humidity")
                self._state["publish"]["rssi"] = payload.get("rssi")
                self._state["publish"]["ip"] = payload.get("ip", "--")
                if payload.get("ip"):
                    self._state["wifi"]["ip"] = payload["ip"]

        return ParseResult(changed, self.state, text)

    def _apply_config_payload(self, payload: dict[str, Any]) -> None:
        config = self._state["config"]
        hardware = self._state["hardware"]
        config["device_id"] = payload.get("deviceId", config["device_id"])
        config["device_name"] = payload.get("deviceName", config["device_name"])
        config["device_alias"] = payload.get("deviceAlias", config["device_alias"])
        config["device_source"] = payload.get("deviceSource", config["device_source"])
        sensors = payload.get("sensors")
        if isinstance(sensors, list) and sensors:
            config["sensors"] = [str(item) for item in sensors]
        hw = payload.get("hardware") or {}
        hardware["chip_model"] = hw.get("chipModel", hardware["chip_model"])
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
        self._state["wifi"]["ssid"] = wifi.get("ssid", self._state["wifi"]["ssid"])
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
        self._state["publish"]["ip"] = publish.get("ip", self._state["publish"]["ip"])
        self._state["publish"]["payload_text"] = publish.get("payload", self._state["publish"]["payload_text"])
        self._state["publish"]["device"] = self._state["config"]["device_id"]
        self._state["publish"]["alias"] = self._state["config"]["device_alias"]
        self._state["publish"]["source"] = self._state["config"]["device_source"]
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

    def _push_alert(self, text: str) -> None:
        alerts = self._state["alerts"]
        alerts.insert(0, f"{now_text()}  {text}")
        del alerts[12:]
