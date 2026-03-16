from machine import Pin
from neopixel import NeoPixel
import time


led = NeoPixel(Pin(48, Pin.OUT), 1)
boot_key = Pin(0, Pin.IN, Pin.PULL_UP)


def set_rgb(r, g, b):
    led[0] = (r, g, b)
    led.write()


def flash_white():
    set_rgb(20, 20, 20)
    time.sleep_ms(120)


colors = [
    ("red", (20, 0, 0)),
    ("green", (0, 20, 0)),
    ("blue", (0, 0, 20)),
]


print("YD-ESP32-S3 MicroPython demo start")
print("RGB LED: GPIO48, BOOT key: GPIO0")

last_key_state = 1

while True:
    for name, color in colors:
        set_rgb(*color)
        print("color =", name)

        start = time.ticks_ms()
        while time.ticks_diff(time.ticks_ms(), start) < 500:
            current_key_state = boot_key.value()

            if last_key_state == 1 and current_key_state == 0:
                print("BOOT pressed")
                flash_white()
                set_rgb(*color)

            last_key_state = current_key_state
            time.sleep_ms(20)
