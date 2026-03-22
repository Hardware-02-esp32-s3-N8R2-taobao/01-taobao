#pragma once

#ifndef PROJECT_VER
#define PROJECT_VER "dev"
#endif

#define APP_WIFI_SSID            "ggg"
#define APP_WIFI_PASSWORD        "gf666666"

#define APP_MQTT_URI             "mqtt://117.72.55.63:1884"
#define APP_MQTT_USERNAME        ""
#define APP_MQTT_PASSWORD        ""
#define APP_MQTT_TOPIC_PREFIX    "device"

#define APP_DEVICE_ID            "yard-01"
#define APP_DEVICE_ALIAS         "庭院 1 号"
#define APP_FIRMWARE_VERSION_MAJOR 1
#define APP_FIRMWARE_VERSION_MINOR 0
#define APP_FIRMWARE_VERSION     "1.0"
#define APP_SERIAL_PUSH_EVENTS   0

#define APP_PUBLISH_INTERVAL_MS  3000

#define APP_DHT11_GPIO           GPIO_NUM_3
#define APP_DS18B20_GPIO         GPIO_NUM_2

#define APP_I2C_PORT             I2C_NUM_0
#define APP_I2C_SDA_SUPERMINI    GPIO_NUM_4
#define APP_I2C_SCL_SUPERMINI    GPIO_NUM_5
#define APP_I2C_SDA_OLED_SCREEN  GPIO_NUM_5
#define APP_I2C_SCL_OLED_SCREEN  GPIO_NUM_6
#define APP_I2C_CLOCK_HZ         100000

#define APP_BH1750_ADDR_PRIMARY  0x23
#define APP_BH1750_ADDR_SECONDARY 0x5C

#define APP_BMP280_ADDR_PRIMARY  0x76
#define APP_BMP280_ADDR_SECONDARY 0x77

#define APP_SOIL_MOISTURE_GPIO   GPIO_NUM_1
#define APP_RAIN_SENSOR_GPIO     GPIO_NUM_0
