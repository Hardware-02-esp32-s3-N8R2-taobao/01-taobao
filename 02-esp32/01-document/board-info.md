# ESP32 board notes

This folder stores reference material for the ESP32 board currently connected on `COM84`.

## Locally detected facts

- USB UART bridge: `Silicon Labs CP210x`
- USB product string: `CP2102 USB to UART Bridge Controller`
- Serial port: `COM84`
- Target chip: `ESP32-D0WDQ6`
- Flash size: `4MB`
- Crystal: `40MHz`
- MAC: `84:0d:8e:2b:95:9c`

## Identification note

The exact board vendor/model is not exposed by the USB descriptor. Based on the detected
combination (`ESP32` + `CP2102` + `4MB flash`), the closest public reference design is the
official Espressif `ESP32-DevKitC V4` family / common CP2102-based ESP32 dev boards.

The downloaded schematic in this folder should therefore be treated as the best matching
reference schematic, not as a guaranteed exact vendor board schematic.

## Source links

- Espressif schematic: https://dl.espressif.com/dl/schematics/esp32_devkitc_v4-sch.pdf
- Espressif getting started page: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/hw-reference/esp32/get-started-devkitc.html
