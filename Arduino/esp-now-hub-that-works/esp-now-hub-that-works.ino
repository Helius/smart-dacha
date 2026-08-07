#include "esp32-hal-gpio.h"
#include "pins_arduino.h"
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <PubSubClient.h>
#include <ESP32Servo.h>
#include <WebServer.h>
#include <HTTPUpdateServer.h>
#include <secrets.h>

const char* mqtt_server = "192.168.0.105";

const int PUMP_RELAY_PIN = 5;
const int VALVE_SERVO_PIN = 0;
const int VALVE_OPEN_PIN = 1;
const int VALVE_CLOSE_PIN = 2;
#define RGB_BUILTIN 3

// C3 MAC address
uint8_t nodeMac[] = { 0xDC, 0x06, 0x75, 0xF6, 0xE6, 0xB0 };
uint8_t newMACAddress[] = { 0x98, 0x88, 0xE0, 0x7F, 0x0A, 0x00 };

WiFiClient espClient;
PubSubClient mqttClient(espClient);
Servo valveServo;

WebServer httpServer(80);
HTTPUpdateServer httpUpdater;

unsigned long lastPublish = 0;

int rssi;
bool valveError = false;
bool tankValveError = false;

struct Packet {
  int8_t rssi;
  uint8_t bat;
  int8_t distance;
  uint8_t flags;
};

union Msg {
  Packet packet;
  byte arr[4];
};

/**
  1 - open
  0 - closed
  -1 - error
*/
int valveState() {
  auto open = !digitalRead(VALVE_OPEN_PIN);
  auto close = !digitalRead(VALVE_CLOSE_PIN);
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


bool openValve(bool open) {
  valveServo.attach(VALVE_SERVO_PIN, 500, 2400);

  valveServo.write(open ? 0 : 90);
  int attempts = 15;
  while ((valveState() != (open ? 1 : 0)) && attempts-- > 0) {
    Serial.print("wait servo to move ");
    Serial.println(attempts);
    delay(100);
  }
  delay(100);

  if (attempts <= 0) {
    valveError = true;
    Serial.println("cant understand the valve sensors");
  } else {
    valveError = false;
    Serial.println("valve is ok");
  }
  return valveError;
}


void publishStatus() {
  char payload[128];
  snprintf(payload, sizeof(payload), "{\"pump\":\"%s\",\"valve\":\"%s\",\"rssi\":%d, \"valveError\":\"%s\"}",
           digitalRead(PUMP_RELAY_PIN) == HIGH ? "ON" : "OFF",
           valveState() == 1 ? "ON" : "OFF",
           WiFi.RSSI(),
           valveError || valveState() == -1 ? "true" : "false");

  mqttClient.publish("stat/pump-valve-unit/STATUS", payload);
  Serial.printf("[PUB] Status: %s\n", payload);
}

void publishTankStatus(int distance, int bat, uint8_t flags) {
  char payload[128];
  snprintf(payload, sizeof(payload), "{\"distance\":\"%d\",\"bat\":\"%d\", \"valve\":\"%s\", \"valveError\":\"%s\"}",
           distance, bat, (flags & 1)  ? "ON" : "OFF", (flags & (1<<1))  ? "true" : "false");
  
  mqttClient.publish("stat/low-tank-unit/STATUS", payload);
  Serial.printf("[PUB] Status: %s\n", payload);
}


// Updated callback signature for newer API
void OnDataRecv(const esp_now_recv_info_t* info, const uint8_t* incomingData, int len) {
  Msg message;
  memcpy(message.arr, incomingData, sizeof(Msg::arr));

  rssi = info->rx_ctrl->rssi;
  Serial.print("Received, rssi: ");
  Serial.println(rssi);

  // Send back the same message to the sender
  //esp_now_send(info->src_addr, incomingData, len);
  //Serial.println("Reply sent");
  Serial.printf("Receive: bat %d, level %d, flag %d, rssi %d\n",
                message.packet.bat,
                message.packet.distance,
                message.packet.flags,
                message.packet.rssi);

  tankValveError = message.packet.flags & (1 << 1);
  publishTankStatus(message.packet.distance, message.packet.bat, message.packet.flags);
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  char message[length + 1];
  memcpy(message, payload, length);
  message[length] = '\0';

  String msg(message);
  String topicStr(topic);

  Serial.printf("[CMD] Received: topic=%s message=%s\n", topic, message);

  if (topicStr == "cmnd/pump-valve-unit/pump") {
    if (msg == "ON") {
      digitalWrite(PUMP_RELAY_PIN, HIGH);
      // on pump turn off inlet servo
      openValve(false);
    } else if (msg == "OFF") {
      digitalWrite(PUMP_RELAY_PIN, LOW);
    }
  } else if (topicStr == "cmnd/pump-valve-unit/valve") {
    if (msg == "ON") {
      openValve(true);
    } else if (msg == "OFF") {
      openValve(false);
    }
  }

  publishStatus();
}

bool reconnect() {
  int attempts = 6;
  rgbLedWrite(RGB_BUILTIN, 0, 0, 100);
  while (!mqttClient.connected() && attempts > 0) {
    --attempts;
    Serial.printf("[MQTT] Connecting to %s...\n", mqtt_server);
    if (mqttClient.connect("pumpAndValveUnit")) {
      Serial.println("[MQTT] Connected!");
      mqttClient.subscribe("cmnd/pump-valve-unit/pump");
      mqttClient.subscribe("cmnd/pump-valve-unit/valve");
      Serial.println("[MQTT] Subscribed to cmnd/pump-valve-unit/pump and cmnd/pump-valve-unit/valve");
    } else {
      Serial.printf("[MQTT] Connection failed (rc=%d), retrying in 5s...\n", mqttClient.state());
      delay(3000);
    }
  }
  if (attempts == 0) {
    return false;
  }
  return true;
}

void setupWiFiAndMQTT() {
  Serial.print("Connecting to WiFi");
  WiFi.mode(WIFI_STA);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("WiFi connected, IP: ");
  Serial.println(WiFi.localIP());

  //Setup MQTT
  mqttClient.setServer(mqtt_server, 1883);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setBufferSize(512);  // Increase buffer for larger messages

  reconnect();

  httpUpdater.setup(&httpServer);
  httpServer.begin();
}

void setup() {
  Serial.begin(115200);
  
  pinMode(PUMP_RELAY_PIN, OUTPUT);
  // turn all off after init
  digitalWrite(PUMP_RELAY_PIN, LOW);
  
  pinMode(VALVE_OPEN_PIN, INPUT_PULLUP);
  pinMode(VALVE_CLOSE_PIN, INPUT_PULLUP);

  pinMode(BUILTIN_LED, OUTPUT);
  digitalWrite(BUILTIN_LED, HIGH);
  delay(100);
  digitalWrite(BUILTIN_LED, LOW);
  delay(100);
  digitalWrite(BUILTIN_LED, HIGH);
  delay(100);
  digitalWrite(BUILTIN_LED, LOW);
  rgbLedWrite(RGB_BUILTIN, 100, 0, 0);
  delay(300);
  rgbLedWrite(RGB_BUILTIN, 0, 100, 0);
  delay(300);
  rgbLedWrite(RGB_BUILTIN, 0, 0, 100);
  delay(300);

  Serial.println("check valve");
  // check valve
  if (valveState() == 1) {
    openValve(false);
    valveError = valveState() == -1;
  } else {
    openValve(true);
    valveError = valveState() == -1;
    openValve(false);
    valveError |= valveState() == -1;
  }

  Serial.println("setup radio");

  WiFi.mode(WIFI_STA);
  // 3. Change the MAC address
  esp_err_t err = esp_wifi_set_mac(WIFI_IF_STA, newMACAddress);
  
  if (err == ESP_OK) {
    Serial.println("mac successfully updated!");
  } else {
    Serial.println("Failed to set mac");
  }
  
  esp_err_t my_error = esp_wifi_set_max_tx_power(50);

  if (my_error == ESP_OK) {
    Serial.println("TX Power successfully reduced!");
  } else {
    Serial.println("Failed to set TX Power");
  }

  setupWiFiAndMQTT();

  // Print C6 MAC address
  Serial.print("HUB MAC: ");
  Serial.println(WiFi.macAddress());
  // Set WiFi channel to match your router (channel 8)
  esp_wifi_set_channel(8, WIFI_SECOND_CHAN_NONE);
  Serial.println("WiFi channel set to 8");

  // Init ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return;
  }

  // Register receive callback
  esp_now_register_recv_cb(OnDataRecv);

  // Add NODE as peer
  esp_now_peer_info_t peerInfo;
  memset(&peerInfo, 0, sizeof(peerInfo));
  memcpy(peerInfo.peer_addr, nodeMac, 6);
  peerInfo.channel = 8;
  //peerInfo.ifidx = WIFI_IF_STA;  // Changed from ESP_IF_WIFI_STA

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add C3 peer");
    return;
  }

  Serial.println("C6 ready!");
  rgbLedWrite(RGB_BUILTIN, 0, 100, 0);
  delay(300);
}

const long publishInterval = 60000;

void loop() {

  if (!mqttClient.connected()) {
    Serial.println("mqtt is not connected, try reconnect");
    if (!reconnect()) {
      // turn all off if failed to connect to the mqtt
      Serial.println("failed to connect mqtt");
      digitalWrite(PUMP_RELAY_PIN, LOW);
      openValve(false);
      rgbLedWrite(RGB_BUILTIN, 100, 0, 0);
      delay(1000);
      return;
    }
  }

  rgbLedWrite(RGB_BUILTIN, 0, 100, 0);
  mqttClient.loop();

  unsigned long now = millis();
  if (now - lastPublish >= publishInterval) {
    lastPublish = now;
    rgbLedWrite(RGB_BUILTIN, 0, 0, 100);
    publishStatus();
    rgbLedWrite(RGB_BUILTIN, 0, 100, 0);
  }

  if (valveError || tankValveError) {
    rgbLedWrite(RGB_BUILTIN, 100, 0, 0);
    delay(1000);
  }

  httpServer.handleClient();
}