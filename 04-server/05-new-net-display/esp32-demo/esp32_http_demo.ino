#include <WiFi.h>
#include <HTTPClient.h>

const char* WIFI_SSID = "YOUR_WIFI_NAME";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// 把这个地址改成你局域网网关机器的 IP，例如 http://192.168.1.20:3000/api/sensor/update
const char* GATEWAY_URL = "http://192.168.1.20:3000/api/sensor/update";

unsigned long lastPostMs = 0;
const unsigned long postIntervalMs = 5000;

float fakeTemperature = 26.5f;
float fakeHumidity = 60.0f;

void connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Connecting WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.print("WiFi connected, IP: ");
  Serial.println(WiFi.localIP());
}

void postSensorData(float temperature, float humidity) {
  if (WiFi.status() != WL_CONNECTED) {
    connectWifi();
  }

  HTTPClient http;
  http.begin(GATEWAY_URL);
  http.addHeader("Content-Type", "application/json");

  String payload = "{\"temperature\":";
  payload += String(temperature, 1);
  payload += ",\"humidity\":";
  payload += String(humidity, 1);
  payload += ",\"source\":\"esp32-yard-01\"}";

  int httpCode = http.POST(payload);
  String response = http.getString();

  Serial.print("HTTP code: ");
  Serial.println(httpCode);
  Serial.print("Response: ");
  Serial.println(response);

  http.end();
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  connectWifi();
}

void loop() {
  if (millis() - lastPostMs >= postIntervalMs) {
    lastPostMs = millis();

    // 这里先用假数据演示，后续替换成 DHT11/DHT22 等传感器读取值
    fakeTemperature += 0.1f;
    fakeHumidity += 0.2f;

    if (fakeTemperature > 30.0f) {
      fakeTemperature = 26.5f;
    }

    if (fakeHumidity > 70.0f) {
      fakeHumidity = 60.0f;
    }

    postSensorData(fakeTemperature, fakeHumidity);
  }
}
