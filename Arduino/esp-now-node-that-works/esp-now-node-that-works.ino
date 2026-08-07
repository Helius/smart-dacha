#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <ESP32Servo.h>

#include "DistanceMeter.h"

#define uS_TO_S_FACTOR 1000000ULL

#define LED_PIN 8
#define SERVO_PIN 3

#define AWAKE_BTN 2
#define ADC_PIN 4
#define SERVO_PWR_PIN 5
#define BUTTON_PIN 7
#define CLOSE_SENSOR 10
#define OPEN_SENSOR 6

uint8_t c6_mac[] = { 0x98, 0x88, 0xE0, 0x7F, 0x0A, 0x00 };  // Update this!

volatile bool replyReceived = false;
volatile int msgSend = 0;
Servo servo;

RTC_DATA_ATTR int rssi;
RTC_DATA_ATTR bool valveError = false;

float readBatteryVoltage() {
  analogSetAttenuation(ADC_11db);
  analogReadResolution(12);
  int raw = analogRead(ADC_PIN);
  return (raw / 4095.0) * 3.08 * 2.0;
}

int valveState() {
  auto open = !digitalRead(OPEN_SENSOR);
  auto close = !digitalRead(CLOSE_SENSOR);
  Serial.print("Valve sensor: ");
  Serial.print(open);
  Serial.println(close);
  if (!open && close) {
    return 0;
  } else if (open && !close) {
    return 1;
  }
  return -1;
}

// Updated callback for send status
void OnDataSent(const esp_now_send_info_t *tx_info, esp_now_send_status_t status) {

  if (status == ESP_NOW_SEND_SUCCESS) {
    msgSend = 2;
  } else {
    msgSend = 1;
  }
}

// Updated callback signature for newer API
void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
  char message[len + 1];
  memcpy(message, incomingData, len);
  message[len] = '\0';
  rssi = info->rx_ctrl->rssi;
  replyReceived = true;
}

bool sendAndReceive(byte *data, size_t len) {
  Serial.print("Sending: ");
  replyReceived = false;
  msgSend = 0;

  esp_err_t result = esp_now_send(c6_mac, data, len);

  if (result == ESP_OK) {
    Serial.print("OK, rssi: ");
    Serial.print(rssi);
  } else {
    Serial.print("F:");
    Serial.println(result);
    return false;
  }
  int attempts = 50;
  while ((msgSend == 0) && --attempts > 0) {
    if (msgSend == 1) {
      Serial.println(" can't send");
      return false;
    }
    delay(1);
  }

  if (msgSend != 2) {
    Serial.println(" send timeout");
    return false;
  }

  // just send

  // while (!replyReceived && attempts-- > 0) {
  //   delay(1);
  // }

  // if (!replyReceived) {
  //   Serial.println(" no reply");
  //   return false;
  // }

  return true;
}

void initRadio() {
  WiFi.mode(WIFI_STA);
  esp_wifi_set_max_tx_power(70);

  // 1. Force the Wi-Fi driver to start so channel changes take effect
  esp_wifi_start();

  // 2. Set WiFi channel to match your router (channel 8)
  esp_wifi_set_channel(8, WIFI_SECOND_CHAN_NONE);

  // 3. Verify the channel was actually set
  uint8_t primaryChan;
  wifi_second_chan_t secondChan;
  esp_wifi_get_channel(&primaryChan, &secondChan);
  Serial.print("Current Hardware Channel: ");
  Serial.println(primaryChan);

  Serial.print("C3 MAC: ");
  Serial.println(WiFi.macAddress());

  // Init ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return;
  }

  // Register callbacks
  esp_now_register_send_cb(OnDataSent);
  esp_now_register_recv_cb(OnDataRecv);

  // Add C6 as peer
  esp_now_peer_info_t peerInfo;
  memset(&peerInfo, 0, sizeof(peerInfo));
  memcpy(peerInfo.peer_addr, c6_mac, 6);
  peerInfo.channel = 8;
  //peerInfo.ifidx = WIFI_IF_STA;  // Changed from ESP_IF_WIFI_STA

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add C6 peer");
    return;
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  pinMode(SERVO_PWR_PIN, INPUT);
  pinMode(AWAKE_BTN, INPUT_PULLUP);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(OPEN_SENSOR, INPUT_PULLUP);
  pinMode(CLOSE_SENSOR, INPUT_PULLUP);

  esp_deep_sleep_enable_gpio_wakeup(1ULL << AWAKE_BTN, ESP_GPIO_WAKEUP_GPIO_LOW);
  Serial.println("C3 ready!");
}

void setValveOpen(bool open) {
  // TODO: check current servo valve and do nothing if it's already in state
  pinMode(SERVO_PWR_PIN, OUTPUT);
  digitalWrite(SERVO_PWR_PIN, LOW);
  Serial.print("Trun servo: ");
  Serial.println(open ? 90 : 0);
  servo.setPeriodHertz(50);
  servo.attach(SERVO_PIN, 500, 2400);
  servo.write(open ? 0 : 90);
  int attempts = 15;
  while (valveState() != (open ? 1 : 0) && attempts-- > 0) {
    Serial.println("wait servo to move");
    delay(100);
  }
  delay(100);

  if (attempts <= 0) {
    valveError = true;
  } else {
    valveError = false;
  }

  servo.detach();
  digitalWrite(SERVO_PWR_PIN, HIGH);
}

struct Packet {
  int8_t rssi;
  uint8_t bat;
  int8_t distance;
  uint8_t flags;
};

void loop() {
  static unsigned long lastSend = 0;
  digitalWrite(LED_PIN, LOW);

  for (int i = 0; i < SAMPLES_CNT; ++i) {
    measurements[i] = takeUltrasonicMeasurement();
  }

  int8_t distance = getMedian(measurements);
  uint8_t bat = readBatteryVoltage() * 10;

  // control valve
  if (distance == 0) {
    distance = -1;
  }

  if (bat < 36) {
    setValveOpen(false);
  } else {
    if (distance <= 15 && valveState() == 1) {
      setValveOpen(false);
    } else if (distance > 30 && valveState() == 0) {
      setValveOpen(true);
    }
  }

  initRadio();
  delay(10);

  Packet packet = {
    .rssi = rssi,
    .bat = bat,
    .distance = distance,
    .flags = (valveError << 1) | (valveState() == 1)
  };

  union Msg {
    Packet packet;
    byte arr[4];
  };

  Msg msg;
  msg.packet = packet;

  sendAndReceive(msg.arr, sizeof(msg.arr));

  if (valveState() == 1) {
    esp_sleep_enable_timer_wakeup(5 * uS_TO_S_FACTOR);
  } else {
    esp_sleep_enable_timer_wakeup(30 * uS_TO_S_FACTOR);
  }

  digitalWrite(LED_PIN, HIGH);
  if (!digitalRead(AWAKE_BTN)) {
    Serial.print(" Distance ");
    Serial.print(distance);

    Serial.print(", Bat ");
    Serial.print(bat);

    Serial.print(",rssi ");
    Serial.println(rssi);

    int vs = valveState();
    Serial.print("Valve state: ");
    Serial.print(vs);
    Serial.print(" valveError: ");
    Serial.print(valveError);

    delay(3000);
  } else {
    esp_deep_sleep_start();
  }

  delay(10);
}