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


class C3MonitorWindow(QtWidgets.QMainWindow):
    command_requested = QtCore.Signal(str)

    def __init__(self) -> None:
        super().__init__()
        self.setWindowTitle("ESP32-C3 状态上位机")
        self.resize(1280, 900)

        self._parser = StatusParser()
        self._thread: QtCore.QThread | None = None
        self._reader: SerialReader | None = None
        self._sensor_checks: dict[str, QtWidgets.QCheckBox] = {}

        self._build_ui()
        self.refresh_ports()
        self._render_state(self._parser.state)

    def _build_ui(self) -> None:
        root = QtWidgets.QWidget()
        self.setCentralWidget(root)

        layout = QtWidgets.QVBoxLayout(root)
        layout.setContentsMargins(18, 18, 18, 18)
        layout.setSpacing(14)

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

        controls = QtWidgets.QHBoxLayout()
        controls.setSpacing(8)
        controls.addWidget(self.port_combo)
        controls.addWidget(self.baud_combo)
        controls.addWidget(self.refresh_btn)
        controls.addWidget(self.connect_btn)
        controls.addWidget(self.disconnect_btn)
        header_layout.addLayout(controls)
        layout.addWidget(header)

        status_grid = QtWidgets.QGridLayout()
        status_grid.setHorizontalSpacing(12)
        status_grid.setVerticalSpacing(12)
        self.serial_card = self._create_status_card("串口状态")
        self.boot_card = self._create_status_card("启动状态")
        self.wifi_card = self._create_status_card("WiFi 状态")
        self.mqtt_card = self._create_status_card("服务器 / MQTT")
        self.sensor_card = self._create_status_card("传感器状态")
        self.publish_card = self._create_status_card("最近一次上报")
        cards = [self.serial_card, self.boot_card, self.wifi_card, self.mqtt_card, self.sensor_card, self.publish_card]
        for index, card in enumerate(cards):
            status_grid.addWidget(card["frame"], index // 3, index % 3)
        layout.addLayout(status_grid)

        config_frame = QtWidgets.QFrame()
        config_frame.setObjectName("panelCard")
        config_layout = QtWidgets.QVBoxLayout(config_frame)
        config_layout.setContentsMargins(16, 14, 16, 14)
        config_layout.setSpacing(12)

        config_head = QtWidgets.QHBoxLayout()
        config_title = QtWidgets.QLabel("设备配置与硬件信息")
        config_title.setObjectName("panelTitle")
        self.query_btn = QtWidgets.QPushButton("读取设备配置")
        self.save_btn = QtWidgets.QPushButton("保存配置")
        config_head.addWidget(config_title)
        config_head.addStretch(1)
        config_head.addWidget(self.query_btn)
        config_head.addWidget(self.save_btn)
        config_layout.addLayout(config_head)

        info_grid = QtWidgets.QGridLayout()
        info_grid.setHorizontalSpacing(16)
        info_grid.setVerticalSpacing(10)

        self.device_name_combo = QtWidgets.QComboBox()
        self.device_name_combo.setEditable(True)
        self.device_alias_edit = QtWidgets.QLineEdit()
        self.device_source_edit = QtWidgets.QLineEdit()
        self.device_id_label = QtWidgets.QLabel("--")
        self.hardware_label = QtWidgets.QLabel("--")
        self.hardware_meta_label = QtWidgets.QLabel("--")
        self.hardware_meta_label.setWordWrap(True)

        info_grid.addWidget(QtWidgets.QLabel("设备名称"), 0, 0)
        info_grid.addWidget(self.device_name_combo, 0, 1)
        info_grid.addWidget(QtWidgets.QLabel("显示别名"), 0, 2)
        info_grid.addWidget(self.device_alias_edit, 0, 3)
        info_grid.addWidget(QtWidgets.QLabel("设备来源"), 1, 0)
        info_grid.addWidget(self.device_source_edit, 1, 1)
        info_grid.addWidget(QtWidgets.QLabel("设备 ID"), 1, 2)
        info_grid.addWidget(self.device_id_label, 1, 3)
        info_grid.addWidget(QtWidgets.QLabel("芯片型号"), 2, 0)
        info_grid.addWidget(self.hardware_label, 2, 1)
        info_grid.addWidget(QtWidgets.QLabel("硬件细节"), 2, 2)
        info_grid.addWidget(self.hardware_meta_label, 2, 3)
        config_layout.addLayout(info_grid)

        sensor_title = QtWidgets.QLabel("挂载传感器")
        sensor_title.setObjectName("subTitle")
        config_layout.addWidget(sensor_title)
        self.sensor_checks_wrap = QtWidgets.QFrame()
        self.sensor_checks_layout = QtWidgets.QHBoxLayout(self.sensor_checks_wrap)
        self.sensor_checks_layout.setContentsMargins(0, 0, 0, 0)
        self.sensor_checks_layout.setSpacing(12)
        config_layout.addWidget(self.sensor_checks_wrap)

        layout.addWidget(config_frame)

        lower_split = QtWidgets.QSplitter(QtCore.Qt.Orientation.Horizontal)
        lower_split.setChildrenCollapsible(False)

        alert_panel = QtWidgets.QFrame()
        alert_panel.setObjectName("panelCard")
        alert_layout = QtWidgets.QVBoxLayout(alert_panel)
        alert_layout.setContentsMargins(16, 14, 16, 14)
        alert_title = QtWidgets.QLabel("关键事件")
        alert_title.setObjectName("panelTitle")
        self.alert_list = QtWidgets.QListWidget()
        alert_layout.addWidget(alert_title)
        alert_layout.addWidget(self.alert_list, 1)

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
        log_layout.addLayout(log_head)
        log_layout.addWidget(self.log_edit, 1)

        lower_split.addWidget(alert_panel)
        lower_split.addWidget(log_panel)
        lower_split.setSizes([320, 820])
        layout.addWidget(lower_split, 1)

        self.statusBar().showMessage("准备就绪")

        self.refresh_btn.clicked.connect(self.refresh_ports)
        self.connect_btn.clicked.connect(self.connect_serial)
        self.disconnect_btn.clicked.connect(self.disconnect_serial)
        self.clear_log_btn.clicked.connect(self.log_edit.clear)
        self.query_btn.clicked.connect(self.query_device_state)
        self.save_btn.clicked.connect(self.save_device_config)

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
            QPushButton, QComboBox, QLineEdit {
                min-height: 36px;
                border-radius: 12px;
                border: 1px solid #cfdceb;
                background: #f7faff;
                padding: 0 12px;
            }
            QPushButton:hover { background: #ebf3ff; }
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
        self._thread = QtCore.QThread(self)
        self._reader = SerialReader(port_name, baudrate)
        self._reader.moveToThread(self._thread)
        self._thread.started.connect(self._reader.start)
        self._reader.line_received.connect(self._handle_serial_line)
        self._reader.connection_changed.connect(self._handle_connection_change)
        self._reader.error_occurred.connect(self._handle_error)
        self.command_requested.connect(self._reader.write_line)
        self._thread.start()
        self.connect_btn.setEnabled(False)
        self.disconnect_btn.setEnabled(True)
        self.statusBar().showMessage(f"正在连接 {port_name}")

    @QtCore.Slot()
    def disconnect_serial(self) -> None:
        if self._reader is not None:
            try:
                self.command_requested.disconnect(self._reader.write_line)
            except Exception:
                pass
            self._reader.stop()
        if self._thread is not None:
            self._thread.quit()
            self._thread.wait(1000)
            self._thread.deleteLater()
            self._thread = None
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
            QtCore.QTimer.singleShot(1200, self.query_device_state)
            QtCore.QTimer.singleShot(3000, self.query_device_state)

    @QtCore.Slot(str)
    def _handle_error(self, message: str) -> None:
        self.statusBar().showMessage(f"串口错误：{message}")

    @QtCore.Slot(str)
    def _handle_serial_line(self, line: str) -> None:
        cleaned = clean_serial_line(line)
        if cleaned:
            self.log_edit.appendPlainText(cleaned)
        result = self._parser.parse_line(line)
        if result.changed:
            self._render_state(result.state)

    def query_device_state(self) -> None:
        self._send_command("GET_OPTIONS")
        self._send_command("GET_CONFIG")
        self._send_command("GET_STATUS")

    def save_device_config(self) -> None:
        sensors = [name for name, checkbox in self._sensor_checks.items() if checkbox.isChecked()]
        if not sensors:
            sensors = ["dht11"]
        payload = {
            "deviceName": self.device_name_combo.currentText().strip() or "庭院1号",
            "deviceAlias": self.device_alias_edit.text().strip() or "庭院1号设备",
            "deviceSource": self.device_source_edit.text().strip() or "yard-1-flower-c3",
            "sensors": sensors,
        }
        self._send_command(f"SET_CONFIG {json.dumps(payload, ensure_ascii=False)}")
        QtCore.QTimer.singleShot(800, self.query_device_state)

    def _send_command(self, text: str) -> None:
        if self._reader is None:
            self.statusBar().showMessage("串口尚未连接")
            return
        self.command_requested.emit(text)

    def _render_state(self, state: dict[str, Any]) -> None:
        self._set_card(
            self.serial_card,
            "已连接" if state["serial_connected"] else "未连接",
            f"串口：{state['port_name']}\n波特率：{state['baudrate']}\n最近日志：{state['last_seen_at'] or '--'}",
        )
        self._set_card(
            self.boot_card,
            state["boot_mode"],
            f"启动细节：{state['boot_detail'] or '--'}\n最后一行：{state['last_line'] or '--'}",
        )
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
        self._set_card(
            self.sensor_card,
            sensor["status"],
            f"温度：{fmt_value(sensor['temperature'], ' °C')}\n湿度：{fmt_value(sensor['humidity'], ' %RH')}\n更新时间：{sensor['updated_at'] or '--'}",
        )
        publish = state["publish"]
        self._set_card(
            self.publish_card,
            publish["status"],
            (
                f"设备：{publish['alias']} ({publish['device']})\n"
                f"来源：{publish['source']}\n"
                f"温度：{fmt_value(publish['temperature'], ' °C')}  湿度：{fmt_value(publish['humidity'], ' %RH')}\n"
                f"RSSI：{fmt_value(publish['rssi'])}  IP：{publish['ip']}\n"
                f"更新时间：{publish['updated_at'] or '--'}"
            ),
        )

        self.alert_list.clear()
        for item in state["alerts"]:
            self.alert_list.addItem(item)

        config = state["config"]
        options = state["options"]
        self._sync_combo(self.device_name_combo, options["device_names"], config["device_name"])
        self.device_alias_edit.setText(config["device_alias"])
        self.device_source_edit.setText(config["device_source"])
        self.device_id_label.setText(config["device_id"])

        hardware = state["hardware"]
        self.hardware_label.setText(hardware["chip_model"])
        self.hardware_meta_label.setText(
            f"target={hardware['target']}  cores={hardware['cores'] or '--'}  rev={hardware['revision'] or '--'}  mac={hardware['mac']}"
        )

        self._sync_sensor_checks(options["sensor_types"], config["sensors"])

    def _sync_combo(self, combo: QtWidgets.QComboBox, items: list[str], current_text: str) -> None:
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

    def _sync_sensor_checks(self, sensor_types: list[str], selected: list[str]) -> None:
        while self.sensor_checks_layout.count():
            item = self.sensor_checks_layout.takeAt(0)
            widget = item.widget()
            if widget is not None:
                widget.deleteLater()

        self._sensor_checks.clear()
        for name in sensor_types:
            checkbox = QtWidgets.QCheckBox(name)
            checkbox.setChecked(name in selected)
            self._sensor_checks[name] = checkbox
            self.sensor_checks_layout.addWidget(checkbox)

        self.sensor_checks_layout.addStretch(1)

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
