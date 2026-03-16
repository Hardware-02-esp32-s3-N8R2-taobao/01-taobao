# DOIT ESP32 DEVKIT V1


## Table of Contents
* [General Info](#general-information)
* [Pinout](#pinout)
* [Setup](#setup)


## General Information
- The ESP32 is dual core, this means it has 2 processors.
- It has Wi-Fi and bluetooth built-in.
- It runs 32 bit programs.
- The clock frequency can go up to 240MHz and it has a 512 kB RAM.
- This particular board has 36 pins, 18 in each row.
- It also has wide variety of peripherals available, like: capacitive touch, ADCs, DACs, UART, SPI, I2C and much more.
- It comes with built-in hall effect sensor and built-in temperature sensor.


## Technologies Used
- [Adruino 1.8.19](https://www.arduino.cc/en/software)


## Pinout
-![ESP32-Pinout](https://user-images.githubusercontent.com/94538153/223725777-4693c948-2b97-420d-b9cb-a7c165306482.png)


## Setup
- Purchasing a Devkit: [Amazon](https://www.amazon.com/ESP32-WROOM-32-Development-ESP-32S-Bluetooth-Arduino/dp/B084KWNMM4)
- In Arduino IDE Preferences, in the Additional Boards Manager URLs textbox enter

`https://dl.espressif.com/dl/package_esp32_index.json`


- Select the board: Tools -> Boards Manager and search `ESP32`. Select the `ESP32 by Espressif Systems`.

# Libraries used:
```C++
// OLED Display
#include <Arduino.h>
#include <U8g2lib.h>
#include <SPI.h>
#include <Wire.h>
// Constructor
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

// BME280
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
// Constructor
Adafruit_BME280 bme;
// Setup
void setup() { 
  if (! bme.begin(0x76, &Wire)) {
      Serial.println("Could not find a valid BME280 sensor, check wiring!");
      while (1);
  }
}

// WiFi Weather Station
#include <WiFi.h>
#include <DNSServer.h>

// SD Card NAS
#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <SD.h> 
#include <SPI.h>
#include <WiFiAP.h>
```
