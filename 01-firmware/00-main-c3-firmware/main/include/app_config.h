#pragma once

#define APP_WIFI_SSID            "ggg"
#define APP_WIFI_PASSWORD        "gf666666"

#define APP_MQTT_URI             "mqtt://117.72.55.63:1884"
#define APP_MQTT_USERNAME        ""
#define APP_MQTT_PASSWORD        ""
#define APP_MQTT_TOPIC_PREFIX    "device"

#define APP_DEVICE_ID            "explorer-01"
#define APP_DEVICE_ALIAS         "探索者 1 号"
#define APP_FIRMWARE_VERSION_MAJOR 1
#define APP_FIRMWARE_VERSION_MINOR 2
#define APP_FIRMWARE_VERSION     "1.1.29"
#define APP_FIRMWARE_RELEASE_NOTES "修复部分 C3 板子重启后 WiFi 认证过期问题并增强开机重连；WiFi 未连接时不自动休眠；手机配网成功后会持久化保存 WiFi 列表"
#define APP_SERIAL_PUSH_EVENTS   0

#define APP_OTA_SERVER_BASE_URL  "http://117.72.55.63"
#define APP_OTA_CHECK_PATH       "/api/device/ota/check"
#define APP_OTA_REPORT_PATH      "/api/device/ota/report"
#define APP_REMOTE_CONFIG_CHECK_PATH  "/api/device/config/check"
#define APP_REMOTE_CONFIG_REPORT_PATH "/api/device/config/report"

#define APP_PUBLISH_INTERVAL_MS  3000
#define APP_WIFI_PROVISIONING_TIMEOUT_MS 30000
#define APP_WIFI_PROVISIONING_AP_PREFIX "YD-PROV-"
#define APP_WIFI_PROVISIONING_AP_PASSWORD "gf666666"
#define APP_WIFI_PROVISIONING_AP_CHANNEL 6
#define APP_STATUS_LED_GPIO      GPIO_NUM_8
#define APP_STATUS_LED_ACTIVE_LEVEL 0
#define APP_STATUS_LED_BLINK_MS  80
#define APP_SCREEN_PAGE_BUTTON_GPIO GPIO_NUM_9
#define APP_SCREEN_PAGE_BUTTON_ACTIVE_LEVEL 0

#define APP_DHT11_GPIO           GPIO_NUM_3
#define APP_DS18B20_GPIO         GPIO_NUM_2

#define APP_I2C_PORT             I2C_NUM_0
#define APP_I2C_SDA_SUPERMINI    GPIO_NUM_4
#define APP_I2C_SCL_SUPERMINI    GPIO_NUM_5
#define APP_I2C_SDA_OLED_SCREEN  GPIO_NUM_5
#define APP_I2C_SCL_OLED_SCREEN  GPIO_NUM_6
#define APP_I2C_CLOCK_HZ         50000

#define APP_BH1750_ADDR_PRIMARY  0x23
#define APP_BH1750_ADDR_SECONDARY 0x5C

#define APP_BMP180_ADDR_PRIMARY  0x77
#define APP_BMP180_ADDR_SECONDARY 0x76

#define APP_SHTC3_ADDR           0x70
#define APP_MAX17043_ADDR        0x36
#define APP_INA226_ADDR          0x40

#define APP_SOIL_MOISTURE_GPIO   GPIO_NUM_1

// 电池电压采样：GPIO0 (ADC1_CH0)
// 外部分压：电池+ → 30kΩ → GPIO0 → 7.5kΩ → GND
// 单节锂电池电压范围 3.0V ~ 4.2V，分压后 0.60V ~ 0.84V
#define APP_BATTERY_GPIO         GPIO_NUM_0
