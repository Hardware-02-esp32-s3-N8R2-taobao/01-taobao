from __future__ import annotations

import json
import sys
from typing import Any

from PySide6 import QtCore, QtGui, QtWidgets

from .parser import StatusParser, clean_serial_line
from .serial_worker import SerialReader, list_serial_ports


def fmt_value(value: Any, unit: str = "") -> str:
    if value is None or value == "":
        return "--"
    if isinstance(value, float):
        return f"{value:.1f}{unit}"
    return f"{value}{unit}"


def sensor_display_name(sensor_key: str) -> str:
    names = {
        "dht11": "DHT11 温湿度",
        "ds18b20": "DS18B20 温度",
        "bh1750": "BH1750 光照",
        "bmp180": "BMP180 气压温度",
        "shtc3": "SHTC3 温湿度",
        "soil_moisture": "土壤湿度",
        "rain_sensor": "雨滴传感器",
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
    return [f"{sensor_key}：{json.dumps(reading, ensure_ascii=False)}"]


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
        config_layout.addStretch(1)

        config_tab_layout.addWidget(config_frame, 1)
        self.tab_widget.addTab(config_tab, "设备配置")

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
        self._reader.line_received.connect(self._handle_serial_line)
        self._reader.connection_changed.connect(self._handle_connection_change)
        self._reader.error_occurred.connect(self._handle_error)
        self.command_requested.connect(self._reader.write_line)
        self._reader.start()
        self.connect_btn.setEnabled(False)
        self.disconnect_btn.setEnabled(True)
        self.statusBar().showMessage(f"正在连接 {port_name}")

    @QtCore.Slot()
    def disconnect_serial(self) -> None:
        self._status_poll_timer.stop()
        if self._reader is not None:
            try:
                self.command_requested.disconnect(self._reader.write_line)
            except Exception:
                pass
            self._reader.stop()
        if self._reader is not None:
            self._reader.deleteLater()
            self._reader = None
        self.connect_btn.setEnabled(True)
        self.disconnect_btn.setEnabled(False)

    @QtCore.Slot(bool, str)
    def _handle_connection_change(self, connected: bool, message: str) -> None:
        port_name = self.port_combo.currentData() or "--"
        baudrate = int(self.baud_combo.currentText())
        state = self._parser.set_connection(port_name, baudrate, connected)
        self._render_state(state)
        self.statusBar().showMessage(message)
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

    @QtCore.Slot(str)
    def _handle_serial_line(self, line: str) -> None:
        cleaned = clean_serial_line(line)
        if cleaned:
            self.log_edit.appendPlainText(cleaned)
        result = self._parser.parse_line(line)
        if cleaned.startswith("APP_OK:"):
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
        elif cleaned.startswith("APP_CONFIG:"):
            config = result.state["config"]
            self.statusBar().showMessage(f"当前设备已更新为：{config['device_name']} ({config['device_id']})")
        if result.changed:
            self._render_state(result.state)

    def query_device_state(self) -> None:
        if self._reader is None:
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
        QtCore.QTimer.singleShot(540, self.query_device_state)

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
        if not self._initial_load_done:
            return
        if self.tab_widget.currentIndex() not in (0, 1):
            return
        self._mark_auto_refresh()
        self.query_device_state()

    def _handle_tab_changed(self, index: int) -> None:
        if index == 2 and self._reader is not None:
            self._config_form_dirty = False
            self._wifi_form_dirty = False
            self.read_device_config()

    def save_device_config(self) -> None:
        sensors = [name for name, checkbox in self._sensor_checks.items() if checkbox.isChecked()]
        if not sensors:
            sensors = ["dht11"]
        device_name = self.device_name_combo.currentText().strip() or "庭院1号"
        payload = {
            "deviceName": device_name,
            "deviceAlias": device_name + "设备",
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
        self.log_edit.appendPlainText(f">>> {text}")
        self.command_requested.emit(text)

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
        for sensor_key in sensor_models:
            reading = sensor_readings.get(sensor_key)
            if isinstance(reading, dict):
                sensor_lines.extend(sensor_display_lines(sensor_key, reading))
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
        for sensor_key, reading in publish_sensors.items():
            if isinstance(reading, dict):
                publish_lines.extend(sensor_display_lines(sensor_key, reading))
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
