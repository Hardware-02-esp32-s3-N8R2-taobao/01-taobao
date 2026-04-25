from __future__ import annotations

import os
import tempfile
import unittest
from pathlib import Path

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

from PySide6 import QtWidgets

from firmware_package import build_firmware_package, load_firmware_package
from app.window import C3MonitorWindow


class UpgradeFallbackTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls._app = QtWidgets.QApplication.instance() or QtWidgets.QApplication([])

    def setUp(self) -> None:
        self.window = C3MonitorWindow()

    def tearDown(self) -> None:
        self.window.close()

    def _make_temp_packages(self) -> tuple[str, str]:
        temp_dir = tempfile.TemporaryDirectory()
        self.addCleanup(temp_dir.cleanup)
        temp_path = Path(temp_dir.name)

        raw_app_path = temp_path / "demo_v1.1.32.bin"
        raw_app_path.write_bytes(b"demo-app-bytes")

        package_path = temp_path / "demo_package_v1.1.32.bin"
        build_firmware_package(
            package_path,
            package_version="1.1.32",
            release_notes="test package",
            chip="esp32c3",
            flash_settings={"flash_mode": "dio", "flash_freq": "80m", "flash_size": "4MB"},
            segments=[
                {"name": "bootloader.bin", "role": "bootloader", "flash_offset": 0x0, "data": b"boot"},
                {"name": "partition-table.bin", "role": "partition-table", "flash_offset": 0x8000, "data": b"part"},
                {"name": "app.bin", "role": "app", "flash_offset": 0x10000, "data": b"app"},
            ],
        )
        return str(raw_app_path), str(package_path)

    def _prepare_one_way_device_state(self, raw_app_path: str) -> None:
        class FakeReader:
            def __init__(self) -> None:
                self.port_name = "COM11"
                self.write_blocked = False

            def stop(self) -> None:
                return

            def deleteLater(self) -> None:
                return

        self.window._ota_selected_file = raw_app_path
        self.window.ota_file_edit.setText(raw_app_path)
        self.window._selected_firmware_package = load_firmware_package(raw_app_path)
        self.window._reader = FakeReader()
        self.window._serial_command_timeout_seen = True
        self.window._parser._state["hardware"]["fw_version"] = "1.1.29"
        self.window._parser._state["config"]["device_id"] = "office-01"
        self.window._parser._state["last_line"] = 'APP_STATUS:{"deviceId":"office-01"}'

    def test_strategy_prefers_full_flash_when_companion_package_exists(self) -> None:
        raw_app_path, package_path = self._make_temp_packages()
        self._prepare_one_way_device_state(raw_app_path)

        strategy, message = self.window._describe_auto_upgrade_strategy()

        self.assertEqual(strategy, "full-flash")
        self.assertIn(Path(package_path).name, message)

    def test_promote_switches_selected_file_to_full_flash_package(self) -> None:
        raw_app_path, package_path = self._make_temp_packages()
        self._prepare_one_way_device_state(raw_app_path)

        promoted = self.window._promote_to_full_flash_package_if_needed("test")

        self.assertTrue(promoted)
        self.assertEqual(Path(self.window._ota_selected_file), Path(package_path))
        self.assertIsNotNone(self.window._selected_firmware_package)
        assert self.window._selected_firmware_package is not None
        self.assertTrue(self.window._selected_firmware_package.supports_full_flash)
        self.assertEqual(self.window.ota_file_edit.text(), package_path)


if __name__ == "__main__":
    unittest.main()
