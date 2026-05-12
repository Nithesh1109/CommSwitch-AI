#include <WiFi.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <RadioLib.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "Adafruit_MQTT.h"
#include "Adafruit_MQTT_Client.h"
#include "esp_task_wdt.h"
#include "secrets.h"

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define WDT_TIMEOUT 10

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
SX1278 radio = new Module(5, 2, 14);
WiFiClient client;

Adafruit_MQTT_Client mqtt(
  &client,
  "io.adafruit.com",
  1883,
  AIO_USERNAME,
  AIO_KEY
);

Adafruit_MQTT_Publish tempFeed =
  Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/commswitch.temperature");

Adafruit_MQTT_Publish humFeed =
  Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/commswitch.humidity");

Adafruit_MQTT_Publish pressureFeed =
  Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/commswitch.pressure");

struct SensorData {
  float temp;
  float humidity;
  float pressure;
  int rssi;
  char mode[10];
  unsigned long timestamp;
};

SensorData sharedData;
SemaphoreHandle_t dataMutex;

bool useLoRa = false;
int wifiRecoveryCounter = 0;
String lastStatus = "BOOT";

void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  Serial.println("[WiFi] Connecting...");

  WiFi.begin(WIFI_SSID, WIFI_PASS);

  int retries = 0;

  while (WiFi.status() != WL_CONNECTED && retries < 20) {
    vTaskDelay(pdMS_TO_TICKS(500));
    retries++;
    Serial.print(".");
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("[WiFi] Connected");
  }
}

void connectMQTT() {
  if (mqtt.connected()) return;

  int8_t ret;

  while ((ret = mqtt.connect()) != 0) {
    Serial.println(mqtt.connectErrorString(ret));
    mqtt.disconnect();
    vTaskDelay(pdMS_TO_TICKS(3000));
  }

  Serial.println("[MQTT] Connected");
}

String buildJSON() {
  StaticJsonDocument<256> doc;

  xSemaphoreTake(dataMutex, portMAX_DELAY);

  doc["device"] = "UnitA";
  doc["temp"] = sharedData.temp;
  doc["humidity"] = sharedData.humidity;
  doc["pressure"] = sharedData.pressure;
  doc["rssi"] = sharedData.rssi;
  doc["mode"] = sharedData.mode;
  doc["timestamp"] = sharedData.timestamp;

  xSemaphoreGive(dataMutex);

  String output;
  serializeJson(doc, output);

  return output;
}

void Task_Logic(void *pvParameters) {

  esp_task_wdt_add(NULL);

  for (;;) {

    xSemaphoreTake(dataMutex, portMAX_DELAY);

    sharedData.temp = random(240, 360) / 10.0;
    sharedData.humidity = random(400, 850) / 10.0;
    sharedData.pressure = random(9900, 10300) / 10.0;
    sharedData.timestamp = millis();

    xSemaphoreGive(dataMutex);

    Serial.println("[Logic] Sensor updated");

    esp_task_wdt_reset();

    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}

void Task_OLED(void *pvParameters) {

  esp_task_wdt_add(NULL);

  for (;;) {

    display.clearDisplay();

    xSemaphoreTake(dataMutex, portMAX_DELAY);

    display.setTextSize(1);
    display.setTextColor(WHITE);

    display.setCursor(0, 0);
    display.print("Mode: ");
    display.println(sharedData.mode);

    display.print("RSSI: ");
    display.println(sharedData.rssi);

    display.print("Temp: ");
    display.println(sharedData.temp);

    display.print("Hum: ");
    display.println(sharedData.humidity);

    display.println(lastStatus);

    xSemaphoreGive(dataMutex);

    display.display();

    esp_task_wdt_reset();

    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void Task_Comm(void *pvParameters) {

  esp_task_wdt_add(NULL);

  for (;;) {

    connectWiFi();

    if (WiFi.status() == WL_CONNECTED) {

      int rssi = WiFi.RSSI();

      xSemaphoreTake(dataMutex, portMAX_DELAY);
      sharedData.rssi = rssi;
      xSemaphoreGive(dataMutex);

      if (rssi < -85) {
        useLoRa = true;
      }

      if (rssi > -70) {
        wifiRecoveryCounter++;

        if (wifiRecoveryCounter >= 5) {
          useLoRa = false;
        }
      } else {
        wifiRecoveryCounter = 0;
      }
    }

    String payload = buildJSON();

    if (!useLoRa && WiFi.status() == WL_CONNECTED) {

      strcpy(sharedData.mode, "WIFI");

      connectMQTT();

      bool t = tempFeed.publish(sharedData.temp);
      bool h = humFeed.publish(sharedData.humidity);
      bool p = pressureFeed.publish(sharedData.pressure);

      if (t && h && p) {
        lastStatus = "MQTT OK";
        Serial.println("[MQTT] Published");
      } else {
        lastStatus = "MQTT FAIL";
      }

    } else {

      strcpy(sharedData.mode, "LORA");

      bool success = false;

      for (int retry = 0; retry < 3; retry++) {

        int state = radio.transmit(payload);

        if (state == RADIOLIB_ERR_NONE) {

          String ack;

          int ackState = radio.receive(ack);

          if (ackState == RADIOLIB_ERR_NONE && ack == "ACK") {
            success = true;
            break;
          }
        }

        vTaskDelay(pdMS_TO_TICKS(300));
      }

      if (success) {
        lastStatus = "LORA OK";
      } else {
        lastStatus = "LORA FAIL";
      }
    }

    esp_task_wdt_reset();

    vTaskDelay(pdMS_TO_TICKS(3000));
  }
}

void setup() {

  Serial.begin(115200);

  Wire.begin(21, 22);

  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);

  SPI.begin(18, 19, 23, 5);

  int state = radio.begin(433.0);

  if (state != RADIOLIB_ERR_NONE) {
    Serial.println("[LoRa] Init Failed");
    while (1);
  }

  radio.setCRC(true);
  radio.setSyncWord(0x12);

  dataMutex = xSemaphoreCreateMutex();

  esp_task_wdt_init(WDT_TIMEOUT, true);

  xTaskCreatePinnedToCore(Task_Logic, "Logic", 4096, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(Task_OLED, "OLED", 4096, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(Task_Comm, "COMM", 8192, NULL, 1, NULL, 0);
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}
