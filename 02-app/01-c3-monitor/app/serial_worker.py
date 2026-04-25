from __future__ import annotations

from dataclasses import dataclass
import queue
import threading
import time

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
    _MAX_WRITE_TIMEOUT_STREAK = 3
    _WRITE_BLOCK_COOLDOWN_SEC = 3.0
    _MIN_WRITE_GAP_SEC = 0.08
    _RX_IDLE_BEFORE_WRITE_SEC = 0.35
    _MAX_WRITE_WAIT_FOR_IDLE_SEC = 1.5

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
        self._write_timeout_streak = 0
        self._write_blocked_until = 0.0
        self._last_write_at = 0.0
        self._last_read_at = 0.0

    @property
    def port_name(self) -> str:
        return self._port_name

    @property
    def baudrate(self) -> int:
        return self._baudrate

    @property
    def write_blocked(self) -> bool:
        if self._write_blocked and time.monotonic() >= self._write_blocked_until:
            self._reset_write_state()
        return self._write_blocked

    def _clear_write_queue(self) -> None:
        while True:
            try:
                self._write_queue.get_nowait()
            except queue.Empty:
                break

    def _reset_write_state(self) -> None:
        self._write_blocked = False
        self._write_timeout_streak = 0
        self._write_blocked_until = 0.0

    def _enter_write_cooldown(self) -> None:
        self._write_blocked = True
        self._write_blocked_until = time.monotonic() + self._WRITE_BLOCK_COOLDOWN_SEC

    def _reset_write_timeout_streak(self) -> None:
        self._write_timeout_streak = 0

    def _maybe_wait_before_write(self) -> None:
        elapsed = time.monotonic() - self._last_write_at
        remaining = self._MIN_WRITE_GAP_SEC - elapsed
        if remaining > 0:
            time.sleep(remaining)

    def _next_write_payload(self) -> bytes | None:
        try:
            payload = self._write_queue.get_nowait()
        except queue.Empty:
            return None
        if not payload:
            return None
        return payload

    def _wait_for_read_quiet_period(self) -> None:
        if self._last_read_at <= 0:
            return
        deadline = time.monotonic() + self._MAX_WRITE_WAIT_FOR_IDLE_SEC
        while not self._stop_event.is_set():
            quiet_for = time.monotonic() - self._last_read_at
            if quiet_for >= self._RX_IDLE_BEFORE_WRITE_SEC:
                return
            if time.monotonic() >= deadline:
                return
            time.sleep(0.02)

    @QtCore.Slot()
    def start(self) -> None:
        if self._reader_thread is not None and self._reader_thread.is_alive():
            return
        self._stop_event.clear()
        self._reset_write_state()
        self._last_write_at = 0.0
        self._last_read_at = 0.0
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
            self._serial = serial.Serial(port=None, baudrate=self._baudrate, timeout=0.05, write_timeout=2.5)
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
                payload = self._next_write_payload()
                if payload is not None:
                    if self.write_blocked:
                        payload = None
                    else:
                        self._maybe_wait_before_write()
                        self._wait_for_read_quiet_period()
                    try:
                        if payload is not None:
                            written = ser.write(payload)
                            if written != len(payload):
                                raise serial.SerialTimeoutException(f"仅发送了 {written}/{len(payload)} 字节")
                            self._last_write_at = time.monotonic()
                            self._reset_write_state()
                    except serial.SerialTimeoutException as exc:
                        self._write_timeout_streak += 1
                        self._clear_write_queue()
                        if self._write_timeout_streak >= self._MAX_WRITE_TIMEOUT_STREAK:
                            self._enter_write_cooldown()
                            cooldown_sec = int(self._WRITE_BLOCK_COOLDOWN_SEC)
                            self.error_occurred.emit(
                                f"串口连续发送超时，已暂停发送 {cooldown_sec} 秒并将自动恢复：{exc}"
                            )
                        else:
                            self.error_occurred.emit(
                                f"串口发送超时（{self._write_timeout_streak}/{self._MAX_WRITE_TIMEOUT_STREAK}），这次命令可能未发出，请重试：{exc}"
                            )
                        continue
                    except Exception as exc:
                        self.error_occurred.emit(str(exc))
                        self.stop()
                try:
                    waiting = ser.in_waiting
                except Exception:
                    waiting = 0
                raw = ser.read(waiting or 1)
                if not raw:
                    continue
                self._last_read_at = time.monotonic()
                if self._write_timeout_streak > 0 and not self._write_blocked:
                    self._reset_write_timeout_streak()
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
    def write_line(self, text: str) -> bool:
        if self._serial is None or self.write_blocked:
            return False
        try:
            payload = (text.rstrip("\r\n") + "\n").encode("utf-8")
            self._write_queue.put_nowait(payload)
            return True
        except Exception as exc:
            self.error_occurred.emit(str(exc))
            self.stop()
            return False

    def write_bytes(self, payload: bytes) -> bool:
        if self._serial is None or self.write_blocked or not payload:
            return False
        try:
            self._write_queue.put_nowait(payload)
            return True
        except Exception as exc:
            self.error_occurred.emit(str(exc))
            self.stop()
            return False


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
