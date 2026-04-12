from __future__ import annotations

from dataclasses import dataclass
import queue
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
        self._write_queue: queue.Queue[bytes] = queue.Queue()
        self._rx_buffer = bytearray()
        self._write_blocked = False

    @property
    def port_name(self) -> str:
        return self._port_name

    @property
    def baudrate(self) -> int:
        return self._baudrate

    @property
    def write_blocked(self) -> bool:
        return self._write_blocked

    def _clear_write_queue(self) -> None:
        while True:
            try:
                self._write_queue.get_nowait()
            except queue.Empty:
                break

    @QtCore.Slot()
    def start(self) -> None:
        if self._reader_thread is not None and self._reader_thread.is_alive():
            return
        self._stop_event.clear()
        self._write_blocked = False
        self._reader_thread = threading.Thread(
            target=self._read_loop,
            name=f"SerialReader-{self._port_name}",
            daemon=True,
        )
        self._reader_thread.start()

    @QtCore.Slot()
    def stop(self) -> None:
        self._stop_event.set()
        try:
            self._write_queue.put_nowait(b"")
        except Exception:
            pass
        if self._serial is not None:
            try:
                self._serial.close()
            except Exception:
                pass
            self._serial = None

    def _read_loop(self) -> None:
        try:
            self._serial = serial.Serial(port=None, baudrate=self._baudrate, timeout=0.05, write_timeout=0.5)
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
                while True:
                    try:
                        payload = self._write_queue.get_nowait()
                    except queue.Empty:
                        break
                    if not payload:
                        continue
                    if self._write_blocked:
                        continue
                    try:
                        ser.write(payload)
                    except serial.SerialTimeoutException as exc:
                        self._write_blocked = True
                        self._clear_write_queue()
                        self.error_occurred.emit(f"串口发送超时，已切换为只读监听模式：{exc}")
                        break
                    except Exception as exc:
                        self.error_occurred.emit(str(exc))
                        self.stop()
                        break
                try:
                    waiting = ser.in_waiting
                except Exception:
                    waiting = 0
                raw = ser.read(waiting or 1)
                if not raw:
                    continue
                self._rx_buffer.extend(raw)
                while True:
                    newline_index = self._rx_buffer.find(b"\n")
                    if newline_index < 0:
                        break
                    line = self._rx_buffer[:newline_index]
                    del self._rx_buffer[: newline_index + 1]
                    text = line.decode("utf-8", errors="ignore").rstrip("\r")
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
        if self._serial is None or self._write_blocked:
            return
        try:
            payload = (text.rstrip("\r\n") + "\n").encode("utf-8")
            self._write_queue.put_nowait(payload)
        except Exception as exc:
            self.error_occurred.emit(str(exc))
            self.stop()

    def write_bytes(self, payload: bytes) -> None:
        if self._serial is None or self._write_blocked or not payload:
            return
        try:
            self._write_queue.put_nowait(payload)
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
