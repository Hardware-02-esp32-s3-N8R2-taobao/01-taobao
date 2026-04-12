from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any

from PySide6 import QtCore, QtGui, QtWidgets

from firmware_package import FirmwarePackage, FirmwarePackageError, load_firmware_package
from .flash_worker import FullFlashWorker
from .parser import StatusParser, clean_serial_line
from .serial_worker import SerialReader, list_serial_ports


def fmt_value(value: Any, unit: str = "") -> str:
    if value is None or value == "":
        return "--"
    if isinstance(value, float):
        return f"{value:.1f}{unit}"
    return f"{value}{unit}"


def fmt_number(value: Any, decimals: int = 1, unit: str = "") -> str:
    if value is None or value == "":
        return "--"
    if isinstance(value, (int, float)) and not isinstance(value, bool):
        text = f"{float(value):.{decimals}f}"
        if decimals > 0:
            text = text.rstrip("0").rstrip(".")
        return f"{text}{unit}"
    return f"{value}{unit}"


def sensor_display_name(sensor_key: str) -> str:
    names = {
        "dht11": "DHT11 温湿度",
        "ds18b20": "DS18B20 温度",
        "bh1750": "BH1750 光照",
        "wifi_signal": "WiFi 信号强度",
        "bmp180": "BMP180 气压温度",
        "shtc3": "SHTC3 温湿度",
        "soil_moisture": "土壤湿度",
        "rain_sensor": "雨滴传感器",
        "battery": "电池",
        "max17043": "MAX17043 电量计",
        "ina226": "INA226 电流电压",
    }
    return names.get(sensor_key, sensor_key)


def sensor_display_lines(sensor_key: str, reading: dict[str, Any]) -> list[str]:
    if sensor_key == "dht11":
        return [
            f"DHT11 温度：{fmt_value(reading.get('temperature'), ' °C')}",
            f"DHT11 湿度：{fmt_value(reading.get('humidity'), ' %RH')}",
        ]
    if sensor_key == "ds18b20":
        return [f"DS18B20 温度：{fmt_value(reading.get('temperature'), ' °C')}"]
    if sensor_key == "bh1750":
        return [f"BH1750 光照：{fmt_value(reading.get('illuminance'), ' lux')}"]
    if sensor_key == "wifi_signal":
        return [f"WiFi 信号强度：{fmt_value(reading.get('rssi'), ' dBm')}"]
    if sensor_key == "bmp180":
        model = str(reading.get("model") or "bmp180").upper()
        lines = [
            f"{model} 温度：{fmt_value(reading.get('temperature'), ' °C')}",
            f"{model} 气压：{fmt_value(reading.get('pressure'), ' hPa')}",
        ]
        return lines
    if sensor_key == "shtc3":
        return [
            f"SHTC3 温度：{fmt_value(reading.get('temperature'), ' °C')}",
            f"SHTC3 湿度：{fmt_value(reading.get('humidity'), ' %RH')}",
        ]
    if sensor_key == "soil_moisture":
        return [
            f"土壤湿度原始值：{fmt_value(reading.get('raw'))}",
            f"土壤湿度百分比：{fmt_value(reading.get('percent'), ' %')}",
        ]
    if sensor_key == "rain_sensor":
        return [
            f"雨水原始值：{fmt_value(reading.get('raw'))}",
            f"雨水百分比：{fmt_value(reading.get('percent'), ' %')}",
        ]
    if sensor_key == "battery":
        return [
            f"当前电压：{fmt_number(reading.get('voltage'), 2, ' V')}",
            f"当前电量：{fmt_number(reading.get('percent'), 0, ' %')}",
        ]
    if sensor_key == "max17043":
        return [
            f"MAX17043 电压：{fmt_number(reading.get('voltage'), 3, ' V')}",
            f"MAX17043 电量：{fmt_number(reading.get('percent'), 1, ' %')}",
        ]
    if sensor_key == "ina226":
        return [
            f"INA226 母线电压：{fmt_number(reading.get('busVoltage'), 4, ' V')}",
            f"INA226 电流：{fmt_number(reading.get('currentMa'), 3, ' mA')}",
            f"INA226 功率：{fmt_number(reading.get('powerMw'), 3, ' mW')}",
        ]
    return [f"{sensor_key}：{json.dumps(reading, ensure_ascii=False)}"]


def device_alias_from_name(device_name: str) -> str:
    alias_map = {
        "探索者1号": "探索者 1 号",
        "探索者网关": "探索者网关",
        "庭院1号": "庭院 1 号",
        "卧室1号": "卧室 1 号",
        "书房1号": "书房 1 号",
        "办公室1号": "办公室 1 号",
    }
    return alias_map.get(device_name, f"{device_name}设备")


class C3MonitorWindow(QtWidgets.QMainWindow):
    command_requested = QtCore.Signal(str)

    def __init__(self) -> None:
        super().__init__()
        self.setWindowTitle("ESP32-C3 状态上位机")
        self.resize(1280, 900)

        self._parser = StatusParser()
        self._reader: SerialReader | None = None
        self._sensor_checks: dict[str, QtWidgets.QCheckBox] = {}
        self._sensor_detail_cards: dict[str, dict[str, Any]] = {}
        self._last_wifi_list: list[dict] = []
        self._last_auto_refresh_at = "--"
        self._initial_load_done = False
        self._pending_config_save = False
        self._pending_config_name = ""
        self._config_form_dirty = False
        self._wifi_form_dirty = False
        self._updating_config_form = False
        self._updating_wifi_form = False
        self._ota_selected_file = ""
        self._selected_firmware_package: FirmwarePackage | None = None
        self._serial_ota_active = False
        self._serial_ota_waiting_ack = False
        self._serial_ota_finish_sent = False
        self._serial_ota_protocol = "hex"
        self._serial_ota_bytes = b""
        self._serial_ota_offset = 0
        self._serial_ota_sha256 = ""
        self._serial_ota_version = ""
        self._serial_ota_chunk_size = 2048
        self._serial_ota_legacy_chunk_size = 240
        self._device_serial_ota_ready = False
        self._device_supports_raw_ota: bool | None = None
        self._flash_thread: QtCore.QThread | None = None
        self._flash_worker: FullFlashWorker | None = None
        self._reconnect_after_flash = False
        self._flash_port_name = ""
        self._status_poll_timer = QtCore.QTimer(self)
        self._status_poll_timer.setInterval(2500)
        self._status_poll_timer.timeout.connect(self._handle_auto_status_poll)

        self._build_ui()
        self.refresh_ports()
        self._render_state(self._parser.state)

    def _build_ui(self) -> None:
        root = QtWidgets.QWidget()
        self.setCentralWidget(root)

        layout = QtWidgets.QVBoxLayout(root)
        layout.setContentsMargins(18, 18, 18, 18)
        layout.setSpacing(14)

        # ── Header ───────────────────────────────────────────────────────
        header = QtWidgets.QFrame()
        header.setObjectName("headerCard")
        header_layout = QtWidgets.QHBoxLayout(header)
        header_layout.setContentsMargins(18, 16, 18, 16)

        title_box = QtWidgets.QVBoxLayout()
        title = QtWidgets.QLabel("ESP32-C3 状态上位机")
        title.setObjectName("pageTitle")
        subtitle = QtWidgets.QLabel("连接串口后，实时查看 WiFi、MQTT、传感器与设备配置")
        subtitle.setObjectName("pageSubtitle")
        title_box.addWidget(title)
        title_box.addWidget(subtitle)
        header_layout.addLayout(title_box, 1)

        self.port_combo = QtWidgets.QComboBox()
        self.port_combo.setMinimumWidth(320)
        self.baud_combo = QtWidgets.QComboBox()
        self.baud_combo.addItems(["115200", "921600"])
        self.baud_combo.setCurrentText("115200")
        self.refresh_btn = QtWidgets.QPushButton("刷新串口")
        self.connect_btn = QtWidgets.QPushButton("连接设备")
        self.disconnect_btn = QtWidgets.QPushButton("断开连接")
        self.disconnect_btn.setEnabled(False)
        self.auto_refresh_check = QtWidgets.QCheckBox("自动刷新")
        self.auto_refresh_check.setChecked(True)
        self.auto_refresh_label = QtWidgets.QLabel("自动刷新：--")
        self.auto_refresh_label.setObjectName("pageSubtitle")

        controls = QtWidgets.QHBoxLayout()
        controls.setSpacing(8)
        controls.addWidget(self.port_combo)
        controls.addWidget(self.baud_combo)
        controls.addWidget(self.refresh_btn)
        controls.addWidget(self.auto_refresh_check)
        controls.addWidget(self.auto_refresh_label)
        controls.addWidget(self.connect_btn)
        controls.addWidget(self.disconnect_btn)
        header_layout.addLayout(controls)
        layout.addWidget(header)

        self.tab_widget = QtWidgets.QTabWidget()
        self.tab_widget.setObjectName("mainTabs")
        layout.addWidget(self.tab_widget, 1)

        # ── Tab 1: 监控 ──────────────────────────────────────────────────
        monitor_tab = QtWidgets.QWidget()
        monitor_layout = QtWidgets.QVBoxLayout(monitor_tab)
        monitor_layout.setContentsMargins(0, 12, 0, 0)
        monitor_layout.setSpacing(12)

        status_grid = QtWidgets.QGridLayout()
        status_grid.setHorizontalSpacing(12)
        status_grid.setVerticalSpacing(12)
        self.wifi_card = self._create_status_card("WiFi 状态")
        self.mqtt_card = self._create_status_card("服务器 / MQTT")
        self.sensor_card = self._create_status_card("传感器状态")
        self.publish_card = self._create_status_card("最近一次上报")
        cards = [self.wifi_card, self.mqtt_card, self.sensor_card, self.publish_card]
        for index, card in enumerate(cards):
            status_grid.addWidget(card["frame"], index // 2, index % 2)
        monitor_layout.addLayout(status_grid)

        # Hardware info panel (moved from 设备配置)
        hw_panel = QtWidgets.QFrame()
        hw_panel.setObjectName("panelCard")
        hw_layout = QtWidgets.QVBoxLayout(hw_panel)
        hw_layout.setContentsMargins(16, 14, 16, 14)
        hw_layout.setSpacing(8)
        hw_title = QtWidgets.QLabel("硬件信息")
        hw_title.setObjectName("panelTitle")
        hw_layout.addWidget(hw_title)
        hw_info_row = QtWidgets.QHBoxLayout()
        hw_info_row.setSpacing(24)
        self.hw_chip_label = QtWidgets.QLabel("芯片：--")
        self.hw_chip_label.setObjectName("pageSubtitle")
        self.hw_device_id_label = QtWidgets.QLabel("设备ID：--")
        self.hw_device_id_label.setObjectName("pageSubtitle")
        self.hw_meta_label = QtWidgets.QLabel("--")
        self.hw_meta_label.setObjectName("pageSubtitle")
        self.hw_meta_label.setWordWrap(True)
        hw_info_row.addWidget(self.hw_chip_label)
        hw_info_row.addWidget(self.hw_device_id_label)
        hw_info_row.addWidget(self.hw_meta_label, 1)
        hw_layout.addLayout(hw_info_row)
        monitor_layout.addWidget(hw_panel)

        log_panel = QtWidgets.QFrame()
        log_panel.setObjectName("panelCard")
        log_layout = QtWidgets.QVBoxLayout(log_panel)
        log_layout.setContentsMargins(16, 14, 16, 14)
        log_head = QtWidgets.QHBoxLayout()
        log_title = QtWidgets.QLabel("原始串口日志")
        log_title.setObjectName("panelTitle")
        self.clear_log_btn = QtWidgets.QPushButton("清空日志")
        log_head.addWidget(log_title)
        log_head.addStretch(1)
        log_head.addWidget(self.clear_log_btn)
        self.log_edit = QtWidgets.QPlainTextEdit()
        self.log_edit.setReadOnly(True)
        self.log_edit.document().setMaximumBlockCount(800)
        log_layout.addLayout(log_head)
        log_layout.addWidget(self.log_edit, 1)
        monitor_layout.addWidget(log_panel, 1)

        self.tab_widget.addTab(monitor_tab, "监控")

        # ── Tab 2: 传感器数据 ─────────────────────────────────────────────
        sensor_tab = QtWidgets.QWidget()
        sensor_tab_layout = QtWidgets.QVBoxLayout(sensor_tab)
        sensor_tab_layout.setContentsMargins(0, 12, 0, 0)
        sensor_tab_layout.setSpacing(12)

        sensor_panel = QtWidgets.QFrame()
        sensor_panel.setObjectName("panelCard")
        sensor_panel_layout = QtWidgets.QVBoxLayout(sensor_panel)
        sensor_panel_layout.setContentsMargins(16, 14, 16, 14)
        sensor_panel_layout.setSpacing(12)

        sensor_head = QtWidgets.QHBoxLayout()
        sensor_title = QtWidgets.QLabel("传感器数据页")
        sensor_title.setObjectName("panelTitle")
        sensor_hint = QtWidgets.QLabel("每个传感器独立显示：未使能、通信异常或正常数据")
        sensor_hint.setObjectName("pageSubtitle")
        sensor_head.addWidget(sensor_title)
        sensor_head.addStretch(1)
        sensor_head.addWidget(sensor_hint)
        sensor_panel_layout.addLayout(sensor_head)

        sensor_scroll = QtWidgets.QScrollArea()
        sensor_scroll.setWidgetResizable(True)
        sensor_scroll.setFrameShape(QtWidgets.QFrame.Shape.NoFrame)
        sensor_scroll_wrap = QtWidgets.QWidget()
        self.sensor_detail_grid = QtWidgets.QGridLayout(sensor_scroll_wrap)
        self.sensor_detail_grid.setHorizontalSpacing(12)
        self.sensor_detail_grid.setVerticalSpacing(12)
        sensor_scroll.setWidget(sensor_scroll_wrap)
        sensor_panel_layout.addWidget(sensor_scroll, 1)
        sensor_tab_layout.addWidget(sensor_panel, 1)
        self.tab_widget.addTab(sensor_tab, "传感器数据")

        # ── Tab 3: 设备配置 ───────────────────────────────────────────────
        config_tab = QtWidgets.QWidget()
        config_tab_layout = QtWidgets.QVBoxLayout(config_tab)
        config_tab_layout.setContentsMargins(0, 12, 0, 0)
        config_tab_layout.setSpacing(12)

        config_frame = QtWidgets.QFrame()
        config_frame.setObjectName("panelCard")
        config_layout = QtWidgets.QVBoxLayout(config_frame)
        config_layout.setContentsMargins(16, 14, 16, 14)
        config_layout.setSpacing(12)

        config_head = QtWidgets.QHBoxLayout()
        config_title = QtWidgets.QLabel("设备配置")
        config_title.setObjectName("panelTitle")
        self.query_btn = QtWidgets.QPushButton("读取设备配置")
        self.save_btn = QtWidgets.QPushButton("保存配置")
        config_head.addWidget(config_title)
        config_head.addStretch(1)
        config_head.addWidget(self.query_btn)
        config_head.addWidget(self.save_btn)
        config_layout.addLayout(config_head)

        name_row = QtWidgets.QHBoxLayout()
        name_row.setSpacing(12)
        name_label = QtWidgets.QLabel("设备名称")
        name_label.setFixedWidth(80)
        self.device_name_combo = QtWidgets.QComboBox()
        self.device_name_combo.setEditable(True)
        name_row.addWidget(name_label)
        name_row.addWidget(self.device_name_combo, 1)
        config_layout.addLayout(name_row)

        self.config_summary_label = QtWidgets.QLabel("当前配置：--")
        self.config_summary_label.setObjectName("pageSubtitle")
        config_layout.addWidget(self.config_summary_label)

        self.config_result_label = QtWidgets.QLabel("最近操作：--")
        self.config_result_label.setObjectName("pageSubtitle")
        config_layout.addWidget(self.config_result_label)

        sensor_title = QtWidgets.QLabel("挂载传感器")
        sensor_title.setObjectName("subTitle")
        config_layout.addWidget(sensor_title)
        self.sensor_checks_wrap = QtWidgets.QFrame()
        self.sensor_checks_layout = QtWidgets.QHBoxLayout(self.sensor_checks_wrap)
        self.sensor_checks_layout.setContentsMargins(0, 0, 0, 0)
        self.sensor_checks_layout.setSpacing(12)
        config_layout.addWidget(self.sensor_checks_wrap)

        # WiFi config section (merged from former WiFi tab)
        wifi_divider = QtWidgets.QFrame()
        wifi_divider.setFrameShape(QtWidgets.QFrame.Shape.HLine)
        wifi_divider.setStyleSheet("color: #dbe4f0;")
        config_layout.addWidget(wifi_divider)

        wifi_head = QtWidgets.QHBoxLayout()
        wifi_panel_title = QtWidgets.QLabel("WiFi 配置")
        wifi_panel_title.setObjectName("subTitle")
        self.wifi_read_btn = QtWidgets.QPushButton("读取 WiFi")
        self.wifi_scan_btn = QtWidgets.QPushButton("扫描附近 WiFi")
        self.wifi_send_btn = QtWidgets.QPushButton("发送到设备")
        wifi_head.addWidget(wifi_panel_title)
        wifi_head.addStretch(1)
        wifi_head.addWidget(self.wifi_read_btn)
        wifi_head.addWidget(self.wifi_scan_btn)
        wifi_head.addWidget(self.wifi_send_btn)
        config_layout.addLayout(wifi_head)

        wifi_hint = QtWidgets.QLabel("设备启动后将按顺序尝试连接，直到成功为止。断开后自动轮询下一条记录。")
        wifi_hint.setObjectName("pageSubtitle")
        config_layout.addWidget(wifi_hint)

        self.wifi_table = QtWidgets.QTableWidget(0, 2)
        self.wifi_table.setHorizontalHeaderLabels(["WiFi 名称 (SSID)", "密码"])
        self.wifi_table.horizontalHeader().setSectionResizeMode(0, QtWidgets.QHeaderView.ResizeMode.Stretch)
        self.wifi_table.horizontalHeader().setSectionResizeMode(1, QtWidgets.QHeaderView.ResizeMode.Stretch)
        self.wifi_table.setSelectionBehavior(QtWidgets.QAbstractItemView.SelectionBehavior.SelectRows)
        self.wifi_table.verticalHeader().setDefaultSectionSize(38)
        self.wifi_table.setMaximumHeight(200)
        config_layout.addWidget(self.wifi_table)

        wifi_row_btns = QtWidgets.QHBoxLayout()
        self.wifi_add_btn = QtWidgets.QPushButton("＋ 添加")
        self.wifi_del_btn = QtWidgets.QPushButton("－ 删除选中行")
        wifi_row_btns.addWidget(self.wifi_add_btn)
        wifi_row_btns.addWidget(self.wifi_del_btn)
        wifi_row_btns.addStretch(1)
        config_layout.addLayout(wifi_row_btns)

        low_power_divider = QtWidgets.QFrame()
        low_power_divider.setFrameShape(QtWidgets.QFrame.Shape.HLine)
        low_power_divider.setStyleSheet("color: #dbe4f0;")
        config_layout.addWidget(low_power_divider)

        low_power_head = QtWidgets.QHBoxLayout()
        low_power_title = QtWidgets.QLabel("低功耗模式")
        low_power_title.setObjectName("subTitle")
        self.low_power_read_btn = QtWidgets.QPushButton("读取低功耗")
        self.low_power_enable_btn = QtWidgets.QPushButton("进入低功耗")
        self.low_power_disable_btn = QtWidgets.QPushButton("退出低功耗")
        low_power_head.addWidget(low_power_title)
        low_power_head.addStretch(1)
        low_power_head.addWidget(self.low_power_read_btn)
        low_power_head.addWidget(self.low_power_enable_btn)
        low_power_head.addWidget(self.low_power_disable_btn)
        config_layout.addLayout(low_power_head)

        low_power_hint = QtWidgets.QLabel("建议用于电池供电：设备定时唤醒，连接 WiFi/MQTT，上报一次后立即进入 Deep Sleep。")
        low_power_hint.setObjectName("pageSubtitle")
        config_layout.addWidget(low_power_hint)

        low_power_row = QtWidgets.QHBoxLayout()
        low_power_label = QtWidgets.QLabel("唤醒周期")
        low_power_label.setFixedWidth(80)
        self.low_power_interval_spin = QtWidgets.QSpinBox()
        self.low_power_interval_spin.setRange(10, 86400)
        self.low_power_interval_spin.setSuffix(" 秒")
        self.low_power_interval_spin.setValue(300)
        self.low_power_status_label = QtWidgets.QLabel("状态：未开启")
        self.low_power_status_label.setObjectName("pageSubtitle")
        low_power_row.addWidget(low_power_label)
        low_power_row.addWidget(self.low_power_interval_spin)
        low_power_row.addWidget(self.low_power_status_label, 1)
        config_layout.addLayout(low_power_row)

        config_layout.addStretch(1)

        config_tab_layout.addWidget(config_frame, 1)
        self.tab_widget.addTab(config_tab, "设备配置")

        # ── Tab 4: 固件升级 ───────────────────────────────────────────────
        upgrade_tab = QtWidgets.QWidget()
        upgrade_tab_layout = QtWidgets.QVBoxLayout(upgrade_tab)
        upgrade_tab_layout.setContentsMargins(0, 12, 0, 0)
        upgrade_tab_layout.setSpacing(12)

        upgrade_frame = QtWidgets.QFrame()
        upgrade_frame.setObjectName("panelCard")
        upgrade_layout = QtWidgets.QVBoxLayout(upgrade_frame)
        upgrade_layout.setContentsMargins(16, 14, 16, 14)
        upgrade_layout.setSpacing(12)

        upgrade_head = QtWidgets.QHBoxLayout()
        upgrade_title = QtWidgets.QLabel("固件升级")
        upgrade_title.setObjectName("panelTitle")
        upgrade_hint = QtWidgets.QLabel("统一升级包 .bin：上位机会自动判断，运行中的设备走串口 OTA，空板或非项目固件走串口全烧录。")
        upgrade_hint.setObjectName("pageSubtitle")
        upgrade_hint.setWordWrap(True)
        upgrade_head.addWidget(upgrade_title)
        upgrade_head.addStretch(1)
        upgrade_layout.addLayout(upgrade_head)
        upgrade_layout.addWidget(upgrade_hint)

        self.ota_target_label = QtWidgets.QLabel("升级目标：自动判断串口 OTA / 全烧录  |  设备：--")
        self.ota_target_label.setObjectName("pageSubtitle")
        self.ota_target_label.setWordWrap(True)
        upgrade_layout.addWidget(self.ota_target_label)

        self.upgrade_device_label = QtWidgets.QLabel("设备识别：--")
        self.upgrade_device_label.setObjectName("pageSubtitle")
        self.upgrade_device_label.setWordWrap(True)
        upgrade_layout.addWidget(self.upgrade_device_label)

        self.upgrade_plan_label = QtWidgets.QLabel("自动策略：--")
        self.upgrade_plan_label.setObjectName("pageSubtitle")
        self.upgrade_plan_label.setWordWrap(True)
        upgrade_layout.addWidget(self.upgrade_plan_label)

        ota_file_row = QtWidgets.QHBoxLayout()
        ota_file_label = QtWidgets.QLabel("升级包")
        ota_file_label.setFixedWidth(80)
        self.ota_file_edit = QtWidgets.QLineEdit()
        self.ota_file_edit.setReadOnly(True)
        self.ota_file_edit.setPlaceholderText("请选择统一升级包 .bin")
        self.ota_pick_btn = QtWidgets.QPushButton("选择 bin 文件")
        ota_file_row.addWidget(ota_file_label)
        ota_file_row.addWidget(self.ota_file_edit, 1)
        ota_file_row.addWidget(self.ota_pick_btn)
        upgrade_layout.addLayout(ota_file_row)

        self.upgrade_package_label = QtWidgets.QLabel("升级包信息：--")
        self.upgrade_package_label.setObjectName("pageSubtitle")
        self.upgrade_package_label.setWordWrap(True)
        upgrade_layout.addWidget(self.upgrade_package_label)

        self.upgrade_package_segments_label = QtWidgets.QLabel("固件分段：--")
        self.upgrade_package_segments_label.setObjectName("pageSubtitle")
        self.upgrade_package_segments_label.setWordWrap(True)
        upgrade_layout.addWidget(self.upgrade_package_segments_label)

        upgrade_btn_row = QtWidgets.QHBoxLayout()
        upgrade_btn_row.setSpacing(8)
        upgrade_btn_row.addStretch(1)
        self.ota_upgrade_btn = QtWidgets.QPushButton("升级")
        self.ota_upgrade_btn.setMinimumWidth(180)
        upgrade_btn_row.addWidget(self.ota_upgrade_btn)
        upgrade_layout.addLayout(upgrade_btn_row)

        self.ota_status_label = QtWidgets.QLabel("升级状态：--")
        self.ota_status_label.setObjectName("pageSubtitle")
        self.ota_status_label.setWordWrap(True)
        upgrade_layout.addWidget(self.ota_status_label)
        self.ota_progress_bar = QtWidgets.QProgressBar()
        self.ota_progress_bar.setRange(0, 100)
        self.ota_progress_bar.setValue(0)
        upgrade_layout.addWidget(self.ota_progress_bar)

        self.upgrade_log_edit = QtWidgets.QPlainTextEdit()
        self.upgrade_log_edit.setReadOnly(True)
        self.upgrade_log_edit.setPlaceholderText("全烧录输出和升级决策日志会显示在这里。")
        self.upgrade_log_edit.document().setMaximumBlockCount(800)
        upgrade_layout.addWidget(self.upgrade_log_edit, 1)

        upgrade_tab_layout.addWidget(upgrade_frame, 1)
        self.tab_widget.addTab(upgrade_tab, "固件升级")

        # ── Signal connections ────────────────────────────────────────────
        self.statusBar().showMessage("准备就绪")

        self.refresh_btn.clicked.connect(self.refresh_ports)
        self.connect_btn.clicked.connect(self.connect_serial)
        self.disconnect_btn.clicked.connect(self.disconnect_serial)
        self.auto_refresh_check.toggled.connect(self._toggle_auto_refresh)
        self.tab_widget.currentChanged.connect(self._handle_tab_changed)
        self.clear_log_btn.clicked.connect(self.log_edit.clear)
        self.query_btn.clicked.connect(self.read_device_config)
        self.save_btn.clicked.connect(self.save_device_config)
        self.wifi_read_btn.clicked.connect(self._request_wifi_list)
        self.wifi_scan_btn.clicked.connect(self._scan_wifi)
        self.wifi_send_btn.clicked.connect(self.send_wifi_list)
        self.wifi_add_btn.clicked.connect(self._add_wifi_row)
        self.wifi_del_btn.clicked.connect(self._del_wifi_row)
        self.wifi_table.itemChanged.connect(self._mark_wifi_form_dirty)
        self.low_power_read_btn.clicked.connect(self._request_low_power)
        self.low_power_enable_btn.clicked.connect(self.enable_low_power)
        self.low_power_disable_btn.clicked.connect(self.disable_low_power)
        self.ota_pick_btn.clicked.connect(self._pick_ota_file)
        self.ota_upgrade_btn.clicked.connect(self._start_auto_upgrade)

        combo_line_edit = self.device_name_combo.lineEdit()
        if combo_line_edit is not None:
            combo_line_edit.textEdited.connect(self._mark_config_form_dirty)
        self.device_name_combo.activated.connect(lambda _index: self._mark_config_form_dirty())

        self.setStyleSheet(
            """
            QMainWindow { background: #eef3fb; }
            QFrame#headerCard, QFrame#panelCard, QFrame[card="true"] {
                background: white;
                border: 1px solid #dbe4f0;
                border-radius: 18px;
            }
            QLabel#pageTitle { font-size: 28px; font-weight: 700; color: #183153; }
            QLabel#pageSubtitle { font-size: 14px; color: #55708d; }
            QLabel#panelTitle { font-size: 18px; font-weight: 700; color: #183153; }
            QLabel#subTitle { font-size: 15px; font-weight: 700; color: #244a7c; }
            QLabel[role="cardTitle"] { font-size: 16px; font-weight: 700; color: #183153; }
            QLabel[role="cardValue"] { font-size: 28px; font-weight: 700; color: #244a7c; }
            QLabel[role="cardMeta"] { font-size: 13px; color: #55708d; }
            QPlainTextEdit, QListWidget {
                background: #f8fbff;
                border: 1px solid #dbe4f0;
                border-radius: 12px;
                font-family: Consolas, 'Courier New', monospace;
                font-size: 13px;
            }
            QTableWidget {
                background: #f8fbff;
                border: 1px solid #dbe4f0;
                border-radius: 12px;
                font-size: 14px;
                gridline-color: #dbe4f0;
            }
            QHeaderView::section {
                background: #eef3fb;
                border: none;
                border-bottom: 1px solid #dbe4f0;
                padding: 6px 12px;
                font-size: 13px;
                font-weight: 600;
                color: #183153;
            }
            QPushButton, QComboBox, QLineEdit {
                min-height: 36px;
                border-radius: 12px;
                border: 1px solid #cfdceb;
                background: #f7faff;
                padding: 0 12px;
            }
            QPushButton:hover { background: #ebf3ff; }
            QTabWidget#mainTabs::pane {
                border: none;
                background: transparent;
            }
            QTabWidget#mainTabs > QTabBar::tab {
                min-width: 120px;
                min-height: 38px;
                padding: 0 24px;
                font-size: 15px;
                font-weight: 600;
                color: #55708d;
                background: #dbe4f0;
                border-radius: 10px 10px 0 0;
                margin-right: 4px;
            }
            QTabWidget#mainTabs > QTabBar::tab:selected {
                background: white;
                color: #183153;
            }
            QTabWidget#mainTabs > QTabBar::tab:hover:!selected {
                background: #c8d8ec;
            }
            """
        )

    def _create_status_card(self, title: str) -> dict[str, Any]:
        frame = QtWidgets.QFrame()
        frame.setProperty("card", True)
        layout = QtWidgets.QVBoxLayout(frame)
        layout.setContentsMargins(16, 14, 16, 14)
        layout.setSpacing(6)
        title_label = QtWidgets.QLabel(title)
        title_label.setProperty("role", "cardTitle")
        value_label = QtWidgets.QLabel("--")
        value_label.setProperty("role", "cardValue")
        meta_label = QtWidgets.QLabel("--")
        meta_label.setWordWrap(True)
        meta_label.setProperty("role", "cardMeta")
        layout.addWidget(title_label)
        layout.addWidget(value_label)
        layout.addWidget(meta_label)
        return {"frame": frame, "value": value_label, "meta": meta_label}

    def _ensure_sensor_detail_cards(self, sensor_types: list[str]) -> None:
        current_types = list(self._sensor_detail_cards.keys())
        if current_types == sensor_types:
            return

        while self.sensor_detail_grid.count():
            item = self.sensor_detail_grid.takeAt(0)
            widget = item.widget()
            if widget is not None:
                widget.deleteLater()

        self._sensor_detail_cards.clear()
        for index, sensor_key in enumerate(sensor_types):
            card = self._create_status_card(sensor_display_name(sensor_key))
            self._sensor_detail_cards[sensor_key] = card
            self.sensor_detail_grid.addWidget(card["frame"], index // 2, index % 2)
        self.sensor_detail_grid.setRowStretch((len(sensor_types) + 1) // 2, 1)

    def _append_upgrade_log(self, text: str) -> None:
        if not text:
            return
        self.upgrade_log_edit.appendPlainText(text)

    def _current_serial_port_name(self) -> str:
        if self._reader is not None:
            return self._reader.port_name
        return str(self.port_combo.currentData() or "").strip()

    def _select_serial_port(self, port_name: str) -> None:
        target = str(port_name or "").strip()
        if not target:
            return
        index = self.port_combo.findData(target)
        if index < 0:
            self.refresh_ports()
            index = self.port_combo.findData(target)
        if index >= 0:
            self.port_combo.setCurrentIndex(index)

    def _device_running_project_firmware(self) -> bool:
        if self._reader is None:
            return False
        state = self._parser.state
        hardware = state.get("hardware") or {}
        fw_version = str(hardware.get("fw_version") or "").strip()
        last_line = str(state.get("last_line") or "").strip()
        return fw_version not in ("", "--") or last_line.startswith("APP_")

    def _device_supports_serial_ota(self) -> bool:
        return self._reader is not None and self._device_serial_ota_ready

    @staticmethod
    def _parse_version_tuple(version_text: str) -> tuple[int, ...] | None:
        text = str(version_text or "").strip()
        if not text:
            return None
        parts = text.split(".")
        values: list[int] = []
        for part in parts:
            if not part.isdigit():
                return None
            values.append(int(part))
        return tuple(values) if values else None

    def _device_supports_raw_serial_ota(self) -> bool:
        if self._device_supports_raw_ota is not None:
            return self._device_supports_raw_ota
        state = self._parser.state
        hardware = state.get("hardware") or {}
        fw_version = self._parse_version_tuple(str(hardware.get("fw_version") or "").strip())
        return fw_version is not None and fw_version >= (1, 1, 25)

    def _describe_auto_upgrade_strategy(self) -> tuple[str, str]:
        package = self._selected_firmware_package
        if package is None:
            return ("none", "请先选择统一升级包 .bin")

        state = self._parser.state
        if self._device_supports_serial_ota():
            hardware = state.get("hardware") or {}
            fw_version = str(hardware.get("fw_version") or "--")
            if not self._device_supports_raw_serial_ota() and package.supports_full_flash:
                return (
                    "full-flash",
                    f"检测到设备正在运行旧版项目固件（当前版本 {fw_version}），仅支持兼容 HEX OTA。为避免旧协议升级异常，自动策略将改走全烧录。"
                )
            protocol_text = "高速 RAW OTA" if self._device_supports_raw_serial_ota() else "兼容 HEX OTA"
            return ("serial-ota", f"检测到设备正在运行本项目固件（当前版本 {fw_version}），自动策略将走串口 OTA，模式 {protocol_text}。")

        if self._device_running_project_firmware():
            hardware = state.get("hardware") or {}
            fw_version = str(hardware.get("fw_version") or "--")
            port_name = self._current_serial_port_name() or "--"
            if package.supports_full_flash:
                return (
                    "full-flash",
                    f"检测到串口 {port_name} 正在运行本项目固件（当前版本 {fw_version}），但尚未确认串口命令通道可写。为避免 COM18 这类单向日志链路导致 OTA 失败，自动策略将走全烧录。"
                )
            return ("pending-detect", f"检测到设备正在运行本项目固件（当前版本 {fw_version}），但尚未确认串口 OTA 命令通道，请稍候重试。")

        if self._reader is not None:
            port_name = self._current_serial_port_name() or "--"
            last_line = str(state.get("last_line") or "").strip()
            if not last_line:
                return ("pending-detect", f"串口 {port_name} 已连接，但还没有收到设备响应。为避免误判，自动策略会先等待识别结果。")
            if package.supports_full_flash:
                return ("full-flash", f"串口 {port_name} 已连接，但未识别到本项目 OTA 协议，自动策略将按空板/非项目固件执行全烧录。")
            return ("unsupported", "当前文件只包含 app OTA 镜像，而串口侧又未识别到本项目 OTA 协议，无法自动升级。")

        if package.supports_full_flash:
            port_name = self._current_serial_port_name()
            if port_name:
                return ("full-flash", f"当前未识别到可用 OTA 固件会话，自动策略将通过 {port_name} 执行全烧录。")
            return ("full-flash", "当前未识别到可用 OTA 固件会话，自动策略将执行全烧录。")

        return ("unsupported", "当前文件只包含 app OTA 镜像，不包含 bootloader/partition，无法用于空板全烧录。")

    def _update_upgrade_page(self) -> None:
        package = self._selected_firmware_package
        state = self._parser.state
        hardware = state.get("hardware") or {}
        device_id = str((state.get("config") or {}).get("device_id") or "--")
        fw_version = str(hardware.get("fw_version") or "--")
        port_name = self._current_serial_port_name() or "--"

        if self._device_supports_serial_ota():
            self.upgrade_device_label.setText(
                f"设备识别：串口 {port_name} 已连接到运行中设备，deviceId={device_id}，当前固件={fw_version}，支持串口 OTA。"
            )
        elif self._device_running_project_firmware():
            self.upgrade_device_label.setText(
                f"设备识别：串口 {port_name} 已连接到运行中设备，deviceId={device_id}，当前固件={fw_version}，但尚未确认串口命令通道可写。"
            )
        elif self._reader is not None:
            self.upgrade_device_label.setText(
                f"设备识别：串口 {port_name} 已连接，但暂未识别到本项目运行态协议，可能是空板、ROM 下载态或其他固件。"
            )
        else:
            self.upgrade_device_label.setText(
                f"设备识别：当前未连接串口，若直接自动升级，将优先尝试按空板全烧录处理。"
            )

        if package is None:
            self.upgrade_package_label.setText("升级包信息：--")
            self.upgrade_package_segments_label.setText("固件分段：--")
        else:
            mode_text = "支持空板全烧录" if package.supports_full_flash else "仅支持 OTA"
            self.upgrade_package_label.setText(
                f"升级包信息：格式 {package.package_format}，版本 {package.package_version or '--'}，备注 {package.release_notes or '未填写'}，芯片 {package.chip or '--'}，{mode_text}。"
            )
            segment_text = "；".join(
                f"{segment.role}@0x{segment.flash_offset:X} ({segment.size}B)"
                for segment in package.segments
            )
            self.upgrade_package_segments_label.setText(
                f"固件分段：{segment_text or '--'}"
            )

        _strategy, strategy_text = self._describe_auto_upgrade_strategy()
        self.upgrade_plan_label.setText(f"自动策略：{strategy_text}")

    def _set_upgrade_controls_enabled(self, enabled: bool) -> None:
        for widget in (
            self.ota_pick_btn,
            self.ota_upgrade_btn,
        ):
            widget.setEnabled(enabled)

    @QtCore.Slot()
    def refresh_ports(self) -> None:
        current = self.port_combo.currentData()
        self.port_combo.clear()
        for port in list_serial_ports():
            self.port_combo.addItem(f"{port.device}  |  {port.description}", port.device)
        if self.port_combo.count() == 0:
            self.port_combo.addItem("未找到串口设备", "")
        if current:
            index = self.port_combo.findData(current)
            if index >= 0:
                self.port_combo.setCurrentIndex(index)

    @QtCore.Slot()
    def connect_serial(self) -> None:
        port_name = self.port_combo.currentData()
        if not port_name:
            self.statusBar().showMessage("当前没有可连接的串口")
            return

        self.disconnect_serial()
        baudrate = int(self.baud_combo.currentText())
        self._initial_load_done = False
        self._reader = SerialReader(port_name, baudrate)
        self._device_serial_ota_ready = False
        self._device_supports_raw_ota = None
        self._reader.line_received.connect(self._handle_serial_line)
        self._reader.connection_changed.connect(self._handle_connection_change)
        self._reader.error_occurred.connect(self._handle_error)
        self.command_requested.connect(self._reader.write_line)
        self._reader.start()
        self.connect_btn.setEnabled(False)
        self.disconnect_btn.setEnabled(True)
        self.statusBar().showMessage(f"正在连接 {port_name}")
        self._update_upgrade_page()

    @QtCore.Slot()
    def disconnect_serial(self) -> None:
        self._status_poll_timer.stop()
        if self._serial_ota_active:
            self._fail_serial_ota("串口已断开，串口 OTA 中止")
        if self._reader is not None:
            try:
                self.command_requested.disconnect(self._reader.write_line)
            except Exception:
                pass
            self._reader.stop()
        if self._reader is not None:
            self._reader.deleteLater()
            self._reader = None
        self._device_serial_ota_ready = False
        self._device_supports_raw_ota = None
        self.connect_btn.setEnabled(True)
        self.disconnect_btn.setEnabled(False)
        self._update_upgrade_page()

    @QtCore.Slot(bool, str)
    def _handle_connection_change(self, connected: bool, message: str) -> None:
        port_name = self.port_combo.currentData() or "--"
        baudrate = int(self.baud_combo.currentText())
        state = self._parser.set_connection(port_name, baudrate, connected)
        self._render_state(state)
        self.statusBar().showMessage(message)
        self._update_upgrade_page()
        if connected:
            self.connect_btn.setEnabled(False)
            self.disconnect_btn.setEnabled(True)
            QtCore.QTimer.singleShot(2500, self._initial_device_load)
            self._update_auto_refresh_timers()
        else:
            self.connect_btn.setEnabled(True)
            self.disconnect_btn.setEnabled(False)
            self._status_poll_timer.stop()
            self.auto_refresh_label.setText("自动刷新：--")
            self._initial_load_done = False
            if "连接失败" in message or "拒绝访问" in message or "could not open port" in message:
                self.log_edit.appendPlainText(f"!!! {message}")
                self.config_result_label.setText(f"最近操作：{message}")

    @QtCore.Slot(str)
    def _handle_error(self, message: str) -> None:
        self.statusBar().showMessage(f"串口错误：{message}")
        self.log_edit.appendPlainText(f"!!! 串口错误：{message}")
        self.config_result_label.setText(f"最近操作：串口错误：{message}")
        self._append_upgrade_log(f"[serial-error] {message}")

    @QtCore.Slot(str)
    def _handle_serial_line(self, line: str) -> None:
        cleaned = clean_serial_line(line)
        if cleaned:
            self.log_edit.appendPlainText(cleaned)
        result = self._parser.parse_line(line)
        if cleaned.startswith("APP_OK:"):
            if "\"commands\"" in cleaned:
                self._device_serial_ota_ready = "OTA_WRITE <hex>" in cleaned
                self._device_supports_raw_ota = "OTA_WRITE_RAW <size> + raw-bytes" in cleaned
            self.statusBar().showMessage("设备已确认操作成功")
            if self._pending_config_save:
                config = result.state["config"]
                message = f"设备配置已保存：{config['device_name']} ({config['device_id']})"
                self.config_result_label.setText(f"最近操作：{message}")
                self._pending_config_save = False
                self._pending_config_name = ""
        elif cleaned.startswith("APP_ERROR:"):
            self.statusBar().showMessage("设备返回错误，请查看日志")
            if self._pending_config_save:
                self.config_result_label.setText("最近操作：保存失败，请查看串口日志")
                self._pending_config_save = False
                self._pending_config_name = ""
            if self._serial_ota_active:
                self._fail_serial_ota("设备返回 OTA 错误，请查看日志")
        elif cleaned.startswith("APP_OTA:"):
            self._handle_ota_status(result.state.get("ota") or {})
        elif cleaned.startswith("APP_CONFIG:"):
            config = result.state["config"]
            self.statusBar().showMessage(f"当前设备已更新为：{config['device_name']} ({config['device_id']})")
        if result.changed:
            self._render_state(result.state)

    def query_device_state(self) -> None:
        if self._reader is None or self._serial_ota_active:
            return
        self._send_command("GET_STATUS")

    def _initial_device_load(self) -> None:
        if self._reader is None:
            return
        self._initial_load_done = True
        self._config_form_dirty = False
        self._wifi_form_dirty = False
        QtCore.QTimer.singleShot(0, lambda: self._send_command("GET_CONFIG"))
        QtCore.QTimer.singleShot(180, lambda: self._send_command("GET_OPTIONS"))
        QtCore.QTimer.singleShot(360, lambda: self._send_command("GET_WIFI_LIST"))
        QtCore.QTimer.singleShot(540, lambda: self._send_command("GET_LOW_POWER"))
        QtCore.QTimer.singleShot(720, self.query_device_state)
        QtCore.QTimer.singleShot(900, lambda: self._send_command("HELP"))

    def read_device_config(self) -> None:
        self._config_form_dirty = False
        self._wifi_form_dirty = False
        self.config_result_label.setText("最近操作：已请求读取设备配置")
        self._refresh_device_config_views()
        QtCore.QTimer.singleShot(600, self.query_device_state)

    def _refresh_device_config_views(self) -> None:
        if self._reader is None:
            return
        QtCore.QTimer.singleShot(0, lambda: self._send_command("GET_CONFIG"))
        QtCore.QTimer.singleShot(180, lambda: self._send_command("GET_OPTIONS"))
        QtCore.QTimer.singleShot(360, lambda: self._send_command("GET_WIFI_LIST"))
        QtCore.QTimer.singleShot(540, lambda: self._send_command("GET_LOW_POWER"))

    def _toggle_auto_refresh(self, checked: bool) -> None:
        self._update_auto_refresh_timers()
        if checked:
            self.statusBar().showMessage("已开启自动刷新")
        else:
            self.statusBar().showMessage("已关闭自动刷新")
            self.auto_refresh_label.setText(f"自动刷新：{self._last_auto_refresh_at}")

    def _update_auto_refresh_timers(self) -> None:
        if self._reader is None or not self.auto_refresh_check.isChecked():
            self._status_poll_timer.stop()
            return
        self._status_poll_timer.start()

    def _mark_auto_refresh(self) -> None:
        self._last_auto_refresh_at = QtCore.QDateTime.currentDateTime().toString("yyyy-MM-dd HH:mm:ss")
        self.auto_refresh_label.setText(f"自动刷新：{self._last_auto_refresh_at}")

    def _handle_auto_status_poll(self) -> None:
        if not self._initial_load_done or self._serial_ota_active:
            return
        if self.tab_widget.currentIndex() not in (0, 1, 3):
            return
        self._mark_auto_refresh()
        self.query_device_state()

    def _handle_tab_changed(self, index: int) -> None:
        if index == 2 and self._reader is not None:
            self._config_form_dirty = False
            self._wifi_form_dirty = False
            self.read_device_config()
            return
        if index == 3:
            self._update_upgrade_page()
            if self._reader is not None and not self._serial_ota_active:
                QtCore.QTimer.singleShot(0, lambda: self._send_command("GET_STATUS"))
                QtCore.QTimer.singleShot(180, lambda: self._send_command("GET_CONFIG"))
                QtCore.QTimer.singleShot(360, lambda: self._send_command("HELP"))

    def save_device_config(self) -> None:
        sensors = [name for name, checkbox in self._sensor_checks.items() if checkbox.isChecked()]
        device_name = self.device_name_combo.currentText().strip() or "探索者1号"
        payload = {
            "deviceName": device_name,
            "deviceAlias": device_alias_from_name(device_name),
            "sensors": sensors,
        }
        self._pending_config_save = True
        self._pending_config_name = device_name
        self._config_form_dirty = False
        self.config_result_label.setText(f"最近操作：正在保存 {device_name}")
        self._send_command(f"SET_CONFIG {json.dumps(payload, ensure_ascii=False)}")
        self.statusBar().showMessage(f"正在发送设备配置：{device_name}")

    def _request_wifi_list(self) -> None:
        self._send_command("GET_WIFI_LIST")

    def send_wifi_list(self) -> None:
        entries = []
        for row in range(self.wifi_table.rowCount()):
            ssid_item = self.wifi_table.item(row, 0)
            pw_item = self.wifi_table.item(row, 1)
            ssid = ssid_item.text().strip() if ssid_item else ""
            pw = pw_item.text().strip() if pw_item else ""
            if ssid:
                entries.append({"ssid": ssid, "password": pw})
        if not entries:
            self.statusBar().showMessage("WiFi列表为空，请至少添加一条记录")
            return
        self._send_command(f"SET_WIFI_LIST {json.dumps(entries, ensure_ascii=False)}")
        self._wifi_form_dirty = False
        self.config_result_label.setText(f"最近操作：已发送 {len(entries)} 条 WiFi 配置")
        self.statusBar().showMessage(f"已发送 {len(entries)} 条 WiFi 配置")

    def _scan_wifi(self) -> None:
        self._send_command("SCAN_WIFI")
        self.statusBar().showMessage("正在让设备扫描附近 WiFi...")

    def _request_low_power(self) -> None:
        self._send_command("GET_LOW_POWER")
        self.statusBar().showMessage("正在读取低功耗配置...")

    def _pick_ota_file(self) -> None:
        file_path, _ = QtWidgets.QFileDialog.getOpenFileName(
            self,
            "选择统一升级包",
            str(QtCore.QDir.currentPath()),
            "Firmware Binary (*.bin)",
        )
        if not file_path:
            return
        self._ota_selected_file = file_path
        self.ota_file_edit.setText(file_path)
        try:
            self._selected_firmware_package = load_firmware_package(file_path)
        except FirmwarePackageError as exc:
            self._selected_firmware_package = None
            self.ota_status_label.setText(f"升级状态：文件检查失败，{exc}")
            self.statusBar().showMessage(str(exc))
            self._update_upgrade_page()
            return

        package = self._selected_firmware_package
        if package is None:
            self.ota_status_label.setText("升级状态：升级包加载失败，请重新选择")
            return

        version = package.package_version or "--"
        self._serial_ota_version = str(version)
        notes = package.release_notes or "未填写备注"
        mode_text = "支持自动全烧录" if package.supports_full_flash else "仅支持 OTA"
        self.ota_status_label.setText(
            f"升级状态：已选择 {Path(file_path).name}，识别版本 {version}，{mode_text}，备注：{notes}"
        )
        self.statusBar().showMessage(f"已选择升级包：{Path(file_path).name}")
        self.ota_progress_bar.setValue(0)
        self._append_upgrade_log(f"[package] selected {Path(file_path).name}")
        self._update_upgrade_page()

    def _start_auto_upgrade(self) -> None:
        strategy, message = self._describe_auto_upgrade_strategy()
        self._append_upgrade_log(f"[auto] {message}")
        if strategy == "serial-ota":
            self._start_serial_ota_upgrade()
            return
        if strategy == "full-flash":
            self._start_full_flash_upgrade()
            return
        self.ota_status_label.setText(f"升级状态：{message}")
        self.statusBar().showMessage(message)

    def _start_serial_ota_upgrade(self) -> None:
        if self._reader is None:
            self.ota_status_label.setText("升级状态：请先连接设备串口")
            self.statusBar().showMessage("请先连接设备串口")
            return
        if self._serial_ota_active:
            self.statusBar().showMessage("串口 OTA 已在进行中")
            return
        if not self._ota_selected_file:
            self.ota_status_label.setText("升级状态：请先选择 .bin 固件文件")
            self.statusBar().showMessage("请先选择 .bin 固件文件")
            return
        if not self._device_supports_serial_ota():
            message = "当前尚未确认串口 OTA 命令通道可用，请直接点击“升级”，由上位机自动判断并优先切换到全烧录。"
            self.ota_status_label.setText(f"升级状态：{message}")
            self.statusBar().showMessage(message)
            return

        package = self._selected_firmware_package
        if package is None:
            self.ota_status_label.setText("升级状态：请先重新选择升级包")
            self.statusBar().showMessage("请先重新选择升级包")
            return

        try:
            payload = package.ota_bytes()
        except (FirmwarePackageError, RuntimeError) as exc:
            self.ota_status_label.setText(f"升级状态：提取 OTA 镜像失败，{exc}")
            self.statusBar().showMessage(str(exc))
            return

        self._serial_ota_bytes = payload
        self._serial_ota_offset = 0
        self._serial_ota_sha256 = package.ota_sha256()
        if not self._serial_ota_version:
            self._serial_ota_version = package.package_version or Path(self._ota_selected_file).stem
        self._serial_ota_active = True
        self._serial_ota_waiting_ack = True
        self._serial_ota_finish_sent = False
        self._serial_ota_protocol = "raw" if self._device_supports_raw_serial_ota() else "hex"
        self.ota_progress_bar.setValue(0)
        chunk_size = self._serial_ota_chunk_size if self._serial_ota_protocol == "raw" else self._serial_ota_legacy_chunk_size
        protocol_text = "raw-binary" if self._serial_ota_protocol == "raw" else "legacy-hex"
        self.ota_status_label.setText(
            f"升级状态：正在启动串口 OTA（{protocol_text}），app 段大小 {len(payload)} 字节，版本 {self._serial_ota_version}"
        )
        self.statusBar().showMessage("正在启动串口 OTA...")
        self._append_upgrade_log(
            f"[serial-ota] protocol={self._serial_ota_protocol}, chunk={chunk_size}, size={len(payload)}, sha256={self._serial_ota_sha256}"
        )
        meta = {
            "size": len(payload),
            "sha256": self._serial_ota_sha256,
            "version": self._serial_ota_version,
        }
        self._send_command(f"OTA_BEGIN {json.dumps(meta, ensure_ascii=False)}")

    def _start_full_flash_upgrade(self) -> None:
        if self._flash_thread is not None:
            self.statusBar().showMessage("当前已有全烧录任务在进行中")
            return
        if not self._ota_selected_file or self._selected_firmware_package is None:
            self.ota_status_label.setText("升级状态：请先选择统一升级包 .bin")
            self.statusBar().showMessage("请先选择统一升级包 .bin")
            return
        if not self._selected_firmware_package.supports_full_flash:
            message = "当前固件文件仅包含 OTA app，不包含 bootloader/partition，无法用于空板全烧录。"
            self.ota_status_label.setText(f"升级状态：{message}")
            self.statusBar().showMessage(message)
            return

        port_name = self._current_serial_port_name()
        if not port_name:
            self.ota_status_label.setText("升级状态：请先选择一个串口")
            self.statusBar().showMessage("请先选择一个串口")
            return

        baudrate = int(self.baud_combo.currentText())
        self._flash_port_name = port_name
        self._reconnect_after_flash = self._reader is not None and self._reader.port_name == port_name
        self._append_upgrade_log(
            f"[full-flash] plan start on {port_name}, baud={baudrate}, package={Path(self._ota_selected_file).name}"
        )

        if self._reader is not None:
            self.disconnect_serial()

        QtCore.QTimer.singleShot(250, lambda: self._launch_full_flash_worker(port_name, baudrate))

    def _launch_full_flash_worker(self, port_name: str, baudrate: int) -> None:
        self._set_upgrade_controls_enabled(False)
        self.ota_progress_bar.setRange(0, 0)
        self.ota_status_label.setText(
            f"升级状态：正在通过 {port_name} 执行全烧录，将写入 bootloader / partition / app。"
        )
        self.statusBar().showMessage("正在执行全烧录...")

        thread = QtCore.QThread(self)
        worker = FullFlashWorker(self._ota_selected_file, port_name, baudrate)
        worker.moveToThread(thread)
        worker.log_line.connect(self._append_upgrade_log)
        worker.finished.connect(self._handle_full_flash_finished)
        worker.finished.connect(thread.quit)
        worker.finished.connect(worker.deleteLater)
        thread.finished.connect(thread.deleteLater)
        thread.started.connect(worker.run)
        self._flash_thread = thread
        self._flash_worker = worker
        thread.start()

    @QtCore.Slot(bool, str)
    def _handle_full_flash_finished(self, success: bool, message: str) -> None:
        self.ota_progress_bar.setRange(0, 100)
        self.ota_progress_bar.setValue(100 if success else 0)
        self.ota_status_label.setText(f"升级状态：{message}")
        self.statusBar().showMessage(message)
        self._append_upgrade_log(f"[full-flash] {'done' if success else 'failed'}: {message}")
        self._set_upgrade_controls_enabled(True)
        self._flash_thread = None
        self._flash_worker = None
        reconnect_after_flash = self._reconnect_after_flash
        flash_port_name = self._flash_port_name
        self._reconnect_after_flash = False
        self._flash_port_name = ""
        self._update_upgrade_page()
        if success and reconnect_after_flash and flash_port_name:
            self._select_serial_port(flash_port_name)
            QtCore.QTimer.singleShot(1500, self.connect_serial)

    def enable_low_power(self) -> None:
        interval_sec = int(self.low_power_interval_spin.value())
        payload = {
            "enabled": True,
            "intervalSec": interval_sec,
        }
        self._send_command(f"SET_LOW_POWER {json.dumps(payload, ensure_ascii=False)}")
        self.config_result_label.setText(f"最近操作：已请求进入低功耗，周期 {interval_sec} 秒")
        self.statusBar().showMessage(f"已下发低功耗模式，设备将按 {interval_sec} 秒周期唤醒上报")

    def disable_low_power(self) -> None:
        interval_sec = int(self.low_power_interval_spin.value())
        payload = {
            "enabled": False,
            "intervalSec": interval_sec,
        }
        self._send_command(f"SET_LOW_POWER {json.dumps(payload, ensure_ascii=False)}")
        self.config_result_label.setText("最近操作：已请求退出低功耗")
        self.statusBar().showMessage("已下发退出低功耗模式")

    def _add_wifi_row(self) -> None:
        row = self.wifi_table.rowCount()
        self.wifi_table.insertRow(row)
        self.wifi_table.setItem(row, 0, QtWidgets.QTableWidgetItem(""))
        self.wifi_table.setItem(row, 1, QtWidgets.QTableWidgetItem(""))
        self._wifi_form_dirty = True
        self.wifi_table.editItem(self.wifi_table.item(row, 0))

    def _del_wifi_row(self) -> None:
        rows = sorted({idx.row() for idx in self.wifi_table.selectedIndexes()}, reverse=True)
        for row in rows:
            self.wifi_table.removeRow(row)
        if rows:
            self._wifi_form_dirty = True

    def _populate_wifi_table(self, entries: list[dict]) -> None:
        self._updating_wifi_form = True
        self.wifi_table.setRowCount(0)
        for entry in entries:
            row = self.wifi_table.rowCount()
            self.wifi_table.insertRow(row)
            self.wifi_table.setItem(row, 0, QtWidgets.QTableWidgetItem(entry.get("ssid", "")))
            self.wifi_table.setItem(row, 1, QtWidgets.QTableWidgetItem(entry.get("password", "")))
        self._updating_wifi_form = False

    def _send_command(self, text: str) -> None:
        if self._reader is None:
            self.statusBar().showMessage("串口尚未连接")
            return
        if self._reader.write_blocked:
            self.statusBar().showMessage("当前串口仅支持接收日志，发送命令已跳过")
            return
        self.log_edit.appendPlainText(f">>> {text}")
        self.command_requested.emit(text)

    def _handle_ota_status(self, ota_state: dict[str, Any]) -> None:
        state = str(ota_state.get("state") or "--")
        received = int(ota_state.get("received") or 0)
        total = int(ota_state.get("total") or 0)
        progress = int(ota_state.get("progress") or 0)
        target_version = str(ota_state.get("target_version") or ota_state.get("targetVersion") or "--")
        message = str(ota_state.get("message") or "--")
        self.ota_progress_bar.setValue(max(0, min(progress, 100)))
        self.ota_status_label.setText(
            f"升级状态：串口 OTA {state}，进度 {progress}%（{received}/{total}），目标版本 {target_version}，消息：{message}"
        )
        if not self._serial_ota_active:
            return
        self._serial_ota_waiting_ack = False
        if state == "done":
            self._finish_serial_ota("串口 OTA 已写入完成，设备即将重启")
            return
        if state not in ("ready", "receiving"):
            return
        if self._serial_ota_offset < len(self._serial_ota_bytes):
            QtCore.QTimer.singleShot(0, self._send_next_serial_ota_chunk)
            return
        if not self._serial_ota_finish_sent and received >= len(self._serial_ota_bytes):
            self._serial_ota_finish_sent = True
            self._serial_ota_waiting_ack = True
            self._send_command("OTA_FINISH")

    def _send_next_serial_ota_chunk(self) -> None:
        if not self._serial_ota_active or self._serial_ota_waiting_ack or self._reader is None:
            return
        if self._serial_ota_offset >= len(self._serial_ota_bytes):
            return
        chunk_size = self._serial_ota_chunk_size if self._serial_ota_protocol == "raw" else self._serial_ota_legacy_chunk_size
        chunk = self._serial_ota_bytes[self._serial_ota_offset:self._serial_ota_offset + chunk_size]
        self._serial_ota_offset += len(chunk)
        self._serial_ota_waiting_ack = True
        if self._serial_ota_protocol == "raw":
            self._send_command(f"OTA_WRITE_RAW {len(chunk)}")
            self._reader.write_bytes(chunk)
            return
        self._send_command(f"OTA_WRITE {chunk.hex()}")

    def _finish_serial_ota(self, message: str) -> None:
        self._serial_ota_active = False
        self._serial_ota_waiting_ack = False
        self._serial_ota_finish_sent = False
        self._serial_ota_protocol = "hex"
        self._serial_ota_bytes = b""
        self._serial_ota_offset = 0
        self.ota_progress_bar.setValue(100)
        self.ota_status_label.setText(f"升级状态：{message}")
        self._append_upgrade_log(f"[serial-ota] done: {message}")
        self.statusBar().showMessage(message)

    def _fail_serial_ota(self, message: str) -> None:
        if self._reader is not None:
            self.command_requested.emit("OTA_ABORT")
        self._serial_ota_active = False
        self._serial_ota_waiting_ack = False
        self._serial_ota_finish_sent = False
        self._serial_ota_protocol = "hex"
        self._serial_ota_bytes = b""
        self._serial_ota_offset = 0
        self.ota_status_label.setText(f"升级状态：{message}")
        self._append_upgrade_log(f"[serial-ota] failed: {message}")
        self.statusBar().showMessage(message)

    def _render_state(self, state: dict[str, Any]) -> None:
        wifi = state["wifi"]
        self._set_card(
            self.wifi_card,
            wifi["status"],
            f"SSID：{wifi['ssid']}\nIP：{wifi['ip']}\n原因：{wifi['reason']}\n更新时间：{wifi['updated_at'] or '--'}",
        )
        mqtt = state["mqtt"]
        self._set_card(
            self.mqtt_card,
            mqtt["status"],
            f"Broker：{mqtt['broker']}\n主题：{mqtt['topic']}\n更新时间：{mqtt['updated_at'] or '--'}",
        )
        sensor = state["sensor"]
        sensor_models = sensor.get("sensor_models") or []
        sensor_count = sensor.get("sensor_count", len(sensor_models))
        sensor_ready_count = sensor.get("ready_count", 0)
        sensor_readings = sensor.get("readings") or {}
        sensor_lines = [
            f"当前上报：{sensor_count} 个",
            f"采样成功：{sensor_ready_count} 个",
            f"型号：{', '.join(sensor_models) if sensor_models else '--'}",
        ]
        online_sensor_names = [
            sensor_display_name(sensor_key)
            for sensor_key in sensor_models
            if isinstance(sensor_readings.get(sensor_key), dict) and sensor_readings[sensor_key].get("ready")
        ]
        if online_sensor_names:
            sensor_lines.append(f"在线传感器：{', '.join(online_sensor_names)}")
        sensor_lines.append(f"更新时间：{sensor['updated_at'] or '--'}")
        self._set_card(
            self.sensor_card,
            sensor["status"],
            "\n".join(sensor_lines),
        )
        publish = state["publish"]
        publish_sensors = publish.get("sensors") or {}
        publish_lines = [
            f"设备：{publish['alias']} ({publish['device']})",
            f"RSSI：{fmt_value(publish['rssi'])}",
        ]
        publish_sensor_names = [
            sensor_display_name(sensor_key)
            for sensor_key, reading in publish_sensors.items()
            if isinstance(reading, dict)
        ]
        if publish_sensor_names:
            publish_lines.append(f"本次上报传感器：{', '.join(publish_sensor_names)}")
        publish_lines.append(f"更新时间：{publish['updated_at'] or '--'}")
        self._set_card(
            self.publish_card,
            publish["status"],
            "\n".join(publish_lines),
        )

        # Hardware info in monitor tab
        config = state["config"]
        hardware = state["hardware"]
        self.hw_chip_label.setText(f"芯片：{hardware['chip_model']}")
        self.hw_device_id_label.setText(f"设备ID：{config['device_id']}")
        self.hw_meta_label.setText(
            f"fw={hardware['fw_version']}  target={hardware['target']}  cores={hardware['cores'] or '--'}  "
            f"rev={hardware['revision'] or '--'}  mac={hardware['mac']}"
        )
        self.ota_target_label.setText(
            f"升级目标：自动判断串口 OTA / 全烧录  |  设备：{config['device_id']}  |  当前固件：{hardware['fw_version']}"
        )
        ota = state.get("ota") or {}
        if not self._serial_ota_active and ota.get("total"):
            self.ota_progress_bar.setValue(int(ota.get("progress") or 0))

        # Device config tab
        options = state["options"]
        self._ensure_sensor_detail_cards(options["sensor_types"])
        if not self._config_form_dirty:
            self._sync_combo(self.device_name_combo, options["device_names"], config["device_name"])
            self._sync_sensor_checks(options["sensor_types"], config["sensors"])
        self.config_summary_label.setText(
            f"当前配置：{config['device_name']}  |  设备ID：{config['device_id']}  |  传感器：{', '.join(config['sensors']) if config['sensors'] else '--'}"
        )
        self._render_sensor_detail_cards(state)

        wifi_list = state.get("wifi_list", [])
        if wifi_list and wifi_list != self._last_wifi_list:
            self._last_wifi_list = wifi_list
            if not self._wifi_form_dirty:
                self._populate_wifi_table(wifi_list)
                self.statusBar().showMessage(f"已读取 {len(wifi_list)} 条 WiFi 配置")

        wifi_scan = state.get("wifi_scan", [])
        if wifi_scan:
            preview = "，".join(
                f"{item.get('ssid', '--')}({item.get('rssi', '--')}dBm)"
                for item in wifi_scan[:5]
                if item.get("ssid")
            )
            if preview:
                self.statusBar().showMessage(f"设备扫描结果：{preview}")

        low_power = state.get("low_power", {})
        interval_sec = int(low_power.get("interval_sec", 300) or 300)
        self.low_power_interval_spin.blockSignals(True)
        self.low_power_interval_spin.setValue(max(10, min(interval_sec, 86400)))
        self.low_power_interval_spin.blockSignals(False)
        if low_power.get("enabled"):
            self.low_power_status_label.setText(f"状态：已开启，每 {interval_sec} 秒唤醒一次")
        else:
            self.low_power_status_label.setText(f"状态：未开启，当前配置周期 {interval_sec} 秒")
        self._update_upgrade_page()

    def _sync_combo(self, combo: QtWidgets.QComboBox, items: list[str], current_text: str) -> None:
        self._updating_config_form = True
        current_items = [combo.itemText(i) for i in range(combo.count())]
        if current_items != items:
            combo.blockSignals(True)
            combo.clear()
            combo.addItems(items)
            combo.blockSignals(False)
        index = combo.findText(current_text)
        if index >= 0:
            combo.setCurrentIndex(index)
        else:
            combo.setEditText(current_text)
        self._updating_config_form = False

    def _sync_sensor_checks(self, sensor_types: list[str], selected: list[str]) -> None:
        self._updating_config_form = True
        current_types = list(self._sensor_checks.keys())
        if current_types != sensor_types:
            while self.sensor_checks_layout.count():
                item = self.sensor_checks_layout.takeAt(0)
                widget = item.widget()
                if widget is not None:
                    widget.deleteLater()

            self._sensor_checks.clear()
            for name in sensor_types:
                checkbox = QtWidgets.QCheckBox(sensor_display_name(name))
                checkbox.stateChanged.connect(self._mark_config_form_dirty)
                self._sensor_checks[name] = checkbox
                self.sensor_checks_layout.addWidget(checkbox)

            self.sensor_checks_layout.addStretch(1)

        selected_set = set(selected)
        for name, checkbox in self._sensor_checks.items():
            should_check = name in selected_set
            if checkbox.isChecked() != should_check:
                checkbox.blockSignals(True)
                checkbox.setChecked(should_check)
                checkbox.blockSignals(False)
        self._updating_config_form = False

    def _mark_config_form_dirty(self, *_args: object) -> None:
        if self._updating_config_form:
            return
        self._config_form_dirty = True
        self.config_result_label.setText("最近操作：本地配置已修改，等待保存")

    def _mark_wifi_form_dirty(self, *_args: object) -> None:
        if self._updating_wifi_form:
            return
        self._wifi_form_dirty = True
        self.config_result_label.setText("最近操作：本地 WiFi 配置已修改，等待发送")

    def _render_sensor_detail_cards(self, state: dict[str, Any]) -> None:
        configured = set(state["config"].get("sensors") or [])
        readings = state["sensor"].get("readings") or {}
        updated_at = state["sensor"].get("updated_at") or "--"
        for sensor_key, card in self._sensor_detail_cards.items():
            if sensor_key not in configured:
                self._set_card(card, "未使能", "请在设备配置页勾选该传感器后保存到设备。")
                continue

            reading = readings.get(sensor_key)
            if not isinstance(reading, dict) or not reading.get("ready"):
                reason = "--"
                if isinstance(reading, dict):
                    reason = str(reading.get("reason") or "--")
                self._set_card(card, "通信异常", f"原因：{reason}\n更新时间：{updated_at}")
                continue

            lines = sensor_display_lines(sensor_key, reading)
            address = reading.get("address")
            if address is not None:
                lines.append(f"I2C 地址：0x{int(address):02X}")
            lines.append(f"更新时间：{updated_at}")
            self._set_card(card, "正常", "\n".join(lines))

    def _set_card(self, card: dict[str, Any], value: str, meta: str) -> None:
        card["value"].setText(value)
        card["meta"].setText(meta)

    def closeEvent(self, event: QtGui.QCloseEvent) -> None:
        self.disconnect_serial()
        super().closeEvent(event)


def main() -> int:
    app = QtWidgets.QApplication(sys.argv)
    app.setApplicationName("ESP32-C3 Serial Monitor")
    window = C3MonitorWindow()
    window.show()
    return app.exec()
