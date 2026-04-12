from __future__ import annotations

import hashlib
import json
import re
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Iterable


PACKAGE_MAGIC = b"YDFWPKG1"
PACKAGE_HEADER_SIZE = len(PACKAGE_MAGIC) + 4
PACKAGE_FORMAT = "yd-esp32-firmware-package"
PACKAGE_SCHEMA = 1
FIRMWARE_META_MARKER = b"YDOTA_META:"
DEFAULT_APP_OFFSET = 0x10000


class FirmwarePackageError(RuntimeError):
    pass


@dataclass(frozen=True)
class FirmwareSegment:
    name: str
    role: str
    flash_offset: int
    payload_offset: int
    size: int
    sha256: str


@dataclass(frozen=True)
class FirmwarePackage:
    source_name: str
    package_format: str
    schema: int
    package_version: str
    release_notes: str
    chip: str
    flash_settings: dict[str, Any]
    ota_segment_role: str
    supports_full_flash: bool
    segments: tuple[FirmwareSegment, ...]
    raw_bytes: bytes

    def get_segment(self, role_or_name: str) -> FirmwareSegment | None:
        normalized = str(role_or_name or "").strip().lower()
        if not normalized:
            return None
        for segment in self.segments:
            if segment.role.lower() == normalized or segment.name.lower() == normalized:
                return segment
        return None

    def require_segment(self, role_or_name: str) -> FirmwareSegment:
        segment = self.get_segment(role_or_name)
        if segment is None:
            raise FirmwarePackageError(f"missing segment: {role_or_name}")
        return segment

    def extract_segment_bytes(self, role_or_name: str) -> bytes:
        segment = self.require_segment(role_or_name)
        start = segment.payload_offset
        end = start + segment.size
        if start < 0 or end > len(self.raw_bytes):
            raise FirmwarePackageError(f"segment out of range: {segment.name}")
        return self.raw_bytes[start:end]

    def ota_segment(self) -> FirmwareSegment:
        return self.require_segment(self.ota_segment_role)

    def ota_bytes(self) -> bytes:
        return self.extract_segment_bytes(self.ota_segment_role)

    def ota_sha256(self) -> str:
        return self.ota_segment().sha256

    def ota_size(self) -> int:
        return self.ota_segment().size

    def to_summary(self) -> dict[str, Any]:
        return {
            "format": self.package_format,
            "schema": self.schema,
            "version": self.package_version,
            "notes": self.release_notes,
            "chip": self.chip,
            "flashSettings": dict(self.flash_settings),
            "otaSegmentRole": self.ota_segment_role,
            "supportsFullFlash": self.supports_full_flash,
            "segments": [
                {
                    "name": segment.name,
                    "role": segment.role,
                    "flashOffset": segment.flash_offset,
                    "size": segment.size,
                    "sha256": segment.sha256,
                }
                for segment in self.segments
            ],
        }


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def infer_firmware_version_from_file_name(file_name: str) -> str:
    raw = Path(file_name).name
    match = re.search(r"(?:_v|[-_])(\d+\.\d+\.\d+)\.bin$", raw, re.IGNORECASE) or re.search(r"(\d+\.\d+\.\d+)", raw)
    return match.group(1) if match else ""


def extract_firmware_embedded_metadata_bytes(data: bytes) -> dict[str, str]:
    head = data[: 64 * 1024]
    marker_index = head.find(FIRMWARE_META_MARKER)
    if marker_index < 0:
        return {"version": "", "notes": ""}

    end_index = head.find(b"\x00", marker_index)
    if end_index < 0:
        end_index = min(len(head), marker_index + 512)
    raw = head[marker_index + len(FIRMWARE_META_MARKER):end_index].decode("utf-8", errors="replace").strip()
    if not raw:
        return {"version": "", "notes": ""}

    parts = raw.split("|", 1)
    return {
        "version": parts[0].strip(),
        "notes": parts[1].strip() if len(parts) > 1 else "",
    }


def _read_segment_payload(entry: dict[str, Any]) -> bytes:
    payload = entry.get("data")
    if isinstance(payload, bytes):
        return payload

    source_path = entry.get("path")
    if source_path is None:
        raise FirmwarePackageError(f"segment missing path/data: {entry.get('name') or entry.get('role')}")
    return Path(source_path).read_bytes()


def build_firmware_package(
    output_path: str | Path,
    *,
    package_version: str,
    release_notes: str,
    chip: str,
    flash_settings: dict[str, Any],
    segments: Iterable[dict[str, Any]],
    ota_segment_role: str = "app",
    source_name: str = "",
) -> Path:
    output = Path(output_path)

    manifest_segments: list[dict[str, Any]] = []
    payload_blobs: list[bytes] = []
    payload_offset = 0

    for index, entry in enumerate(segments):
        role = str(entry.get("role") or entry.get("name") or f"segment-{index}").strip()
        name = str(entry.get("name") or role).strip()
        flash_offset = int(entry.get("flash_offset") or 0)
        data = _read_segment_payload(entry)
        manifest_segments.append(
            {
                "name": name,
                "role": role,
                "flashOffset": flash_offset,
                "payloadOffset": payload_offset,
                "size": len(data),
                "sha256": sha256_bytes(data),
            }
        )
        payload_blobs.append(data)
        payload_offset += len(data)

    manifest = {
        "format": PACKAGE_FORMAT,
        "schema": PACKAGE_SCHEMA,
        "createdAt": datetime.now(timezone.utc).isoformat(),
        "sourceName": source_name or output.name,
        "packageVersion": str(package_version or "").strip(),
        "releaseNotes": str(release_notes or "").strip(),
        "chip": str(chip or "").strip(),
        "flashSettings": dict(flash_settings or {}),
        "otaSegmentRole": str(ota_segment_role or "app").strip(),
        "segments": manifest_segments,
    }
    manifest_bytes = json.dumps(manifest, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
    header = PACKAGE_MAGIC + len(manifest_bytes).to_bytes(4, "little")
    output.write_bytes(header + manifest_bytes + b"".join(payload_blobs))
    return output


def load_firmware_package(file_path: str | Path) -> FirmwarePackage:
    path = Path(file_path)
    data = path.read_bytes()
    source_name = path.name

    if data.startswith(PACKAGE_MAGIC):
        return _load_packaged_firmware(data, source_name)
    return _load_legacy_app_firmware(data, source_name)


def load_firmware_package_bytes(data: bytes, source_name: str = "firmware.bin") -> FirmwarePackage:
    if data.startswith(PACKAGE_MAGIC):
        return _load_packaged_firmware(data, source_name)
    return _load_legacy_app_firmware(data, source_name)


def _load_packaged_firmware(data: bytes, source_name: str) -> FirmwarePackage:
    if len(data) < PACKAGE_HEADER_SIZE:
        raise FirmwarePackageError("firmware package header is incomplete")

    manifest_size = int.from_bytes(data[len(PACKAGE_MAGIC):PACKAGE_HEADER_SIZE], "little")
    manifest_start = PACKAGE_HEADER_SIZE
    manifest_end = manifest_start + manifest_size
    if manifest_end > len(data):
        raise FirmwarePackageError("firmware package manifest is truncated")

    try:
        manifest = json.loads(data[manifest_start:manifest_end].decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise FirmwarePackageError("firmware package manifest is invalid") from exc

    if manifest.get("format") != PACKAGE_FORMAT:
        raise FirmwarePackageError("unknown firmware package format")

    payload_base = manifest_end
    segments: list[FirmwareSegment] = []
    for entry in manifest.get("segments") or []:
        payload_offset = int(entry.get("payloadOffset") or 0)
        size = int(entry.get("size") or 0)
        file_offset = payload_base + payload_offset
        end = file_offset + size
        if file_offset < payload_base or end > len(data):
            raise FirmwarePackageError(f"segment is out of package range: {entry.get('name') or entry.get('role')}")
        segments.append(
            FirmwareSegment(
                name=str(entry.get("name") or entry.get("role") or "segment"),
                role=str(entry.get("role") or entry.get("name") or "segment"),
                flash_offset=int(entry.get("flashOffset") or 0),
                payload_offset=file_offset,
                size=size,
                sha256=str(entry.get("sha256") or sha256_bytes(data[file_offset:end])),
            )
        )

    segment_roles = {segment.role.lower() for segment in segments}
    supports_full_flash = {"bootloader", "partition-table", "app"}.issubset(segment_roles)
    return FirmwarePackage(
        source_name=source_name,
        package_format=PACKAGE_FORMAT,
        schema=int(manifest.get("schema") or PACKAGE_SCHEMA),
        package_version=str(manifest.get("packageVersion") or "").strip(),
        release_notes=str(manifest.get("releaseNotes") or "").strip(),
        chip=str(manifest.get("chip") or "").strip(),
        flash_settings=dict(manifest.get("flashSettings") or {}),
        ota_segment_role=str(manifest.get("otaSegmentRole") or "app").strip(),
        supports_full_flash=supports_full_flash,
        segments=tuple(segments),
        raw_bytes=data,
    )


def _load_legacy_app_firmware(data: bytes, source_name: str) -> FirmwarePackage:
    meta = extract_firmware_embedded_metadata_bytes(data)
    version = meta.get("version") or infer_firmware_version_from_file_name(source_name)
    return FirmwarePackage(
        source_name=source_name,
        package_format="raw-app-bin",
        schema=0,
        package_version=str(version or "").strip(),
        release_notes=str(meta.get("notes") or "").strip(),
        chip="esp32c3",
        flash_settings={},
        ota_segment_role="app",
        supports_full_flash=False,
        segments=(
            FirmwareSegment(
                name="app",
                role="app",
                flash_offset=DEFAULT_APP_OFFSET,
                payload_offset=0,
                size=len(data),
                sha256=sha256_bytes(data),
            ),
        ),
        raw_bytes=data,
    )
