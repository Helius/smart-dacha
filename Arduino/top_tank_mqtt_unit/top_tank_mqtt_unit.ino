#include <WiFi.h>
#include <PubSubClient.h>
#include <WebServer.h>
#include <HTTPUpdateServer.h>
#include <secrets.h>

const char* mqtt_server = "192.168.0.105";
const char* mqtt_topic = "top-tank-unit/state";
const char* hostname = "esp-top-tank-unit";
#define uS_TO_S_FACTOR 1000000ULL  // Conversion factor for micro seconds to
#define TRIG_PIN 1
#define ECHO_PIN 2
#define FULL_SWITCH_PIN 3
#define LED_PIN 8
#define BAT_PIN 4

WiFiClient espClient;
PubSubClient client(espClient);

WebServer httpServer(80);
HTTPUpdateServer httpUpdater;

#define SAMPLES_CNT 7
float measurements[SAMPLES_CNT];
size_t measurementCount = 0;

float takeUltrasonicMeasurement() {
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  // pulseIn имеет таймаут. 30000 мкс = ~5 метров макс.
  // Это предотвратит зависание, если датчик не увидит эхо.
  float duration = pulseIn(ECHO_PIN, HIGH, 30000);
  return duration / (34.0 * 2);
}

float readBatteryVoltage() {
  analogSetAttenuation(ADC_11db);
  analogReadResolution(12);
  int raw = analogRead(BAT_PIN);
  return (raw / 4095.0) * 3.08 * 2.0;
}

void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(hostname);
  WiFi.setTxPower(WIFI_POWER_18_5dBm);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(100);
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void reconnect() {

  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (client.connect("ESP32C3Client")) {
      Serial.println("connected");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 1 seconds");
      delay(1000);
    }
  }
}

float getMedian(float arr[]) {
  // Простая сортировка пузырьком (для 5 элементов это самый быстрый и легкочитаемый способ)
  for (int i = 0; i < SAMPLES_CNT - 1; i++) {
    for (int j = i + 1; j < SAMPLES_CNT; j++) {
      if (arr[i] > arr[j]) {
        // Меняем элементы местами
        float temp = arr[i];
        arr[i] = arr[j];
        arr[j] = temp;
      }
    }
  }

  // Возвращаем центральный элемент отсортированного массива
  return arr[SAMPLES_CNT / 2];
}

void setup() {
  setCpuFrequencyMhz(120);
  Serial.begin(115200);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  pinMode(FULL_SWITCH_PIN, INPUT_PULLUP);

  measurementCount = 0;

  setup_wifi();
  client.setServer(mqtt_server, 1883);

  httpUpdater.setup(&httpServer);
  httpServer.begin();
}

time_t lastIteration = 0;

void loop() {

  if (millis() - lastIteration > 20000) {
    lastIteration = millis();
    for (int i = 0; i < SAMPLES_CNT; ++i) {
      delay(100);
      measurements[i] = takeUltrasonicMeasurement();
    }

    digitalWrite(LED_PIN, LOW);

    if (!WiFi.isConnected()) {
      setup_wifi();
    }

    if (!client.connected()) {
      reconnect();
    }
    client.loop();

    long rssi = WiFi.RSSI();
    float distance = getMedian(measurements);

    char msg[80];
    snprintf(msg, 80, "{\"rssi\": %ld, \"level\": %.2f, \"full\": %s}", 
      rssi, distance, digitalRead(FULL_SWITCH_PIN) ? "true" : "false");
    Serial.print("Publishing: ");
    Serial.println(msg);

    int attempts = 3;
    while (!client.publish(mqtt_topic, msg) && attempts-- > 0) {
      Serial.print("Publish failed, rc=");
      Serial.println(client.state());
    }

    client.loop();
    digitalWrite(LED_PIN, HIGH);
    Serial.print("full ");
    Serial.println(digitalRead(FULL_SWITCH_PIN));
  }

  httpServer.handleClient();
  delay(10);
}
