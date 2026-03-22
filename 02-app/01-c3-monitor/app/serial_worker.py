from __future__ import annotations

from dataclasses import dataclass
import threading

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
        self._stop_event = threading.Event()
        self._reader_thread: threading.Thread | None = None

    @property
    def port_name(self) -> str:
        return self._port_name

    @property
    def baudrate(self) -> int:
        return self._baudrate

    @QtCore.Slot()
    def start(self) -> None:
        if self._reader_thread is not None and self._reader_thread.is_alive():
            return
        self._stop_event.clear()
        self._reader_thread = threading.Thread(
            target=self._read_loop,
            name=f"SerialReader-{self._port_name}",
            daemon=True,
        )
        self._reader_thread.start()

    @QtCore.Slot()
    def stop(self) -> None:
        self._stop_event.set()
        if self._serial is not None:
            try:
                self._serial.close()
            except Exception:
                pass
            self._serial = None

    def _read_loop(self) -> None:
        try:
            self._serial = serial.Serial(port=None, baudrate=self._baudrate, timeout=0.3)
            self._serial.dtr = False
            self._serial.rts = False
            self._serial.port = self._port_name
            self._serial.open()
            self.connection_changed.emit(True, f"已连接 {self._port_name}")
        except Exception as exc:
            self._serial = None
            self.error_occurred.emit(str(exc))
            self.connection_changed.emit(False, f"连接失败：{exc}")
            return

        try:
            while not self._stop_event.is_set():
                ser = self._serial
                if ser is None:
                    break
                raw = ser.readline()
                if not raw:
                    continue
                text = raw.decode("utf-8", errors="ignore").rstrip("\r\n")
                if text:
                    self.line_received.emit(text)
        except Exception as exc:
            self.error_occurred.emit(str(exc))
        finally:
            if self._serial is not None:
                try:
                    self._serial.close()
                except Exception:
                    pass
                self._serial = None
            self.connection_changed.emit(False, "串口已关闭")

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
