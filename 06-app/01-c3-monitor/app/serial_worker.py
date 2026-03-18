from __future__ import annotations

from dataclasses import dataclass

import serial
from PySide6 import QtCore
from serial.tools import list_ports


@dataclass
class SerialPortInfo:
    device: str
    description: str


class SerialReader(QtCore.QObject):
    line_received = QtCore.Signal(str)
    connection_changed = QtCore.Signal(bool, str)
    error_occurred = QtCore.Signal(str)

    def __init__(self, port_name: str, baudrate: int = 115200, parent: QtCore.QObject | None = None) -> None:
        super().__init__(parent)
        self._port_name = port_name
        self._baudrate = baudrate
        self._serial: serial.Serial | None = None
        self._timer: QtCore.QTimer | None = None

    @property
    def port_name(self) -> str:
        return self._port_name

    @property
    def baudrate(self) -> int:
        return self._baudrate

    @QtCore.Slot()
    def start(self) -> None:
        try:
            self._serial = serial.Serial(self._port_name, self._baudrate, timeout=0.2)
            self.connection_changed.emit(True, f"已连接 {self._port_name}")
        except Exception as exc:
            self.error_occurred.emit(str(exc))
            self.connection_changed.emit(False, f"连接失败：{exc}")
            return

        self._timer = QtCore.QTimer(self)
        self._timer.setInterval(80)
        self._timer.timeout.connect(self._poll)
        self._timer.start()

    @QtCore.Slot()
    def stop(self) -> None:
        if self._timer is not None:
            self._timer.stop()
            self._timer.deleteLater()
            self._timer = None
        if self._serial is not None:
            try:
                self._serial.close()
            except Exception:
                pass
            self._serial = None
        self.connection_changed.emit(False, "串口已关闭")

    @QtCore.Slot()
    def _poll(self) -> None:
        if self._serial is None:
            return
        try:
            while self._serial.in_waiting:
                raw = self._serial.readline()
                if not raw:
                    break
                text = raw.decode("utf-8", errors="ignore").rstrip("\r\n")
                if text:
                    self.line_received.emit(text)
        except Exception as exc:
            self.error_occurred.emit(str(exc))
            self.stop()

    @QtCore.Slot(str)
    def write_line(self, text: str) -> None:
        if self._serial is None:
            return
        try:
            payload = (text.rstrip("\r\n") + "\n").encode("utf-8")
            self._serial.write(payload)
            self._serial.flush()
        except Exception as exc:
            self.error_occurred.emit(str(exc))
            self.stop()


def list_serial_ports() -> list[SerialPortInfo]:
    ports: list[SerialPortInfo] = []
    for port in list_ports.comports():
        ports.append(
            SerialPortInfo(
                device=port.device,
                description=port.description or "未知设备",
            )
        )
    ports.sort(key=lambda item: item.device)
    return ports
