from __future__ import annotations

import base64
import hashlib
import json
import re
from pathlib import Path
from typing import Any
from urllib import error, parse, request


SERVER_BASE_URL = "http://117.72.55.63"
HTTP_TIMEOUT_SEC = 30
FIRMWARE_META_MARKER = b"YDOTA_META:"


def _http_json(url: str, payload: dict[str, Any] | None = None) -> dict[str, Any]:
    data = None
    headers = {"Accept": "application/json"}
    method = "GET"
    if payload is not None:
        data = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        headers["Content-Type"] = "application/json"
        method = "POST"

    req = request.Request(url, data=data, headers=headers, method=method)
    try:
        with request.urlopen(req, timeout=HTTP_TIMEOUT_SEC) as resp:
            body = resp.read().decode("utf-8", errors="replace")
    except error.HTTPError as exc:
        body = exc.read().decode("utf-8", errors="replace")
        try:
            payload_json = json.loads(body) if body else {}
            message = str(payload_json.get("message") or body or exc.reason)
        except json.JSONDecodeError:
            message = body or str(exc.reason)
        raise RuntimeError(f"HTTP {exc.code}: {message}") from exc
    except error.URLError as exc:
        raise RuntimeError(f"网络请求失败：{exc.reason}") from exc

    if not body.strip():
        return {}
    try:
        return json.loads(body)
    except json.JSONDecodeError as exc:
        raise RuntimeError("服务器返回了非 JSON 数据") from exc


def infer_firmware_version_from_file_name(file_name: str) -> str:
    raw = Path(file_name).name
    match = re.search(r"(?:_v|[-_])(\d+\.\d+\.\d+)\.bin$", raw, re.IGNORECASE) or re.search(r"(\d+\.\d+\.\d+)", raw)
    return match.group(1) if match else ""


def extract_firmware_embedded_metadata(file_path: str) -> dict[str, Any]:
    path = Path(file_path)
    head = path.read_bytes()[: 64 * 1024]
    marker_index = head.find(FIRMWARE_META_MARKER)
    if marker_index < 0:
        return {"version": "", "notes": "", "size": path.stat().st_size, "sha256": _sha256_file(path)}

    end_index = head.find(b"\x00", marker_index)
    if end_index < 0:
        end_index = min(len(head), marker_index + 512)
    raw = head[marker_index + len(FIRMWARE_META_MARKER):end_index].decode("utf-8", errors="replace").strip()
    version = ""
    notes = ""
    if raw:
        parts = raw.split("|", 1)
        version = parts[0].strip()
        notes = parts[1].strip() if len(parts) > 1 else ""
    return {
        "version": version,
        "notes": notes,
        "size": path.stat().st_size,
        "sha256": _sha256_file(path),
    }


def _sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as fp:
        while True:
            chunk = fp.read(1024 * 64)
            if not chunk:
                break
            digest.update(chunk)
    return digest.hexdigest()


def build_firmware_upload_payload(file_path: str, device_id: str, force: bool = False, notes: str = "") -> dict[str, Any]:
    path = Path(file_path)
    if not path.exists():
        raise RuntimeError(f"固件文件不存在：{path}")
    if path.suffix.lower() != ".bin":
        raise RuntimeError("请选择 .bin 固件文件")

    raw_bytes = path.read_bytes()
    metadata = extract_firmware_embedded_metadata(str(path))
    version = metadata.get("version") or infer_firmware_version_from_file_name(path.name)
    if not version:
        raise RuntimeError("无法从固件中识别版本号，请使用带版本信息的 bin 文件")

    return {
        "deviceId": str(device_id).strip(),
        "fileName": path.name,
        "version": version,
        "notes": str(notes or metadata.get("notes") or "").strip(),
        "force": bool(force),
        "dataBase64": base64.b64encode(raw_bytes).decode("ascii"),
    }


def upload_and_start_device_ota(file_path: str, device_id: str, force: bool = False, notes: str = "") -> dict[str, Any]:
    payload = build_firmware_upload_payload(file_path, device_id=device_id, force=force, notes=notes)
    return _http_json(f"{SERVER_BASE_URL}/api/device-ota/upload-and-start", payload)


def fetch_device_ota_summary(device_id: str) -> dict[str, Any]:
    query = parse.urlencode({"deviceId": str(device_id).strip()})
    return _http_json(f"{SERVER_BASE_URL}/api/device-ota?{query}")


def fetch_devices_status() -> dict[str, Any]:
    return _http_json(f"{SERVER_BASE_URL}/api/devices/status")

