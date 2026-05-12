#include <WiFi.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <RadioLib.h>
#include "Adafruit_MQTT.h"
#include "Adafruit_MQTT_Client.h"
#include "esp_task_wdt.h"
#include "secrets.h"

#define WDT_TIMEOUT 10

SX1278 radio = new Module(5, 2, 14);
WiFiClient client;

QueueHandle_t packetQueue;

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

struct PacketData {
  char payload[256];
};

void connectWiFi() {

  if (WiFi.status() == WL_CONNECTED) return;

  WiFi.begin(WIFI_SSID, WIFI_PASS);

  int retries = 0;

  while (WiFi.status() != WL_CONNECTED && retries < 20) {
    Serial.print(".");
    retries++;
    vTaskDelay(pdMS_TO_TICKS(500));
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

void Task_LoRa(void *pvParameters) {

  esp_task_wdt_add(NULL);

  for (;;) {

    String packet;

    int state = radio.receive(packet);

    if (state == RADIOLIB_ERR_NONE) {

      StaticJsonDocument<256> doc;

      if (deserializeJson(doc, packet) == DeserializationError::Ok) {

        PacketData data;

        strncpy(data.payload, packet.c_str(), sizeof(data.payload));

        xQueueSend(packetQueue, &data, portMAX_DELAY);

        radio.transmit("ACK");

        Serial.println("[LoRa] Packet Queued");
      }
    }

    esp_task_wdt_reset();

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

void Task_Uploader(void *pvParameters) {

  esp_task_wdt_add(NULL);

  PacketData packet;

  for (;;) {

    connectWiFi();
    connectMQTT();

    if (xQueueReceive(packetQueue, &packet, pdMS_TO_TICKS(1000))) {

      StaticJsonDocument<256> doc;

      deserializeJson(doc, packet.payload);

      tempFeed.publish(doc["temp"]);
      humFeed.publish(doc["humidity"]);
      pressureFeed.publish(doc["pressure"]);

      Serial.println("[MQTT] Uploaded LoRa Data");
    }

    esp_task_wdt_reset();

    vTaskDelay(pdMS_TO_TICKS(300));
  }
}

void setup() {

  Serial.begin(115200);

  SPI.begin(18, 19, 23, 5);

  int state = radio.begin(433.0);

  if (state != RADIOLIB_ERR_NONE) {
    Serial.println("[LoRa] Init Failed");
    while (1);
  }

  radio.setCRC(true);
  radio.setSyncWord(0x12);

  packetQueue = xQueueCreate(10, sizeof(PacketData));

  esp_task_wdt_init(WDT_TIMEOUT, true);

  xTaskCreatePinnedToCore(Task_LoRa, "LoRa", 8192, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(Task_Uploader, "Uploader", 8192, NULL, 1, NULL, 0);
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}
