#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <espnow.h>
#include <ArduinoJson.h>

// WiFi credentials
const char* ssid = "vivo Y75 5G";
const char* password = "123456789";

// Your PC IPv4 address !!!
const char* serverURL = "http://10.190.109.65:3000/api/sensor-data";
const char* alertURL  = "http://10.190.109.65:3000/api/alert";

// Data structure from ESP32
typedef struct centralMessage {
  int nodeId;
  char eventType[40];

  char gpsStatus[30];
  float latitude;
  float longitude;
  float altitude;
  float speed;
  int satellites;

  char ultrasonicStatus[30];
  float distance;

  char ldrStatus[30];
  int lightLevel;

  char vibrationStatus[30];
  int vibrationCount;

  bool ledState;

} centralMessage;

centralMessage nodeData[3];
unsigned long lastReceived[3] = {0,0,0};
unsigned long lastStatusSent = 0;

// =========================================================
// ESP-NOW RECEIVE CALLBACK
// =========================================================
void onReceive(uint8_t *mac, uint8_t *data, uint8_t len) {
  centralMessage msg;
  memcpy(&msg, data, sizeof(msg));

  int index = msg.nodeId - 1;
  if (index < 0 || index > 2) return;

  nodeData[index] = msg;
  lastReceived[index] = millis();

  Serial.println("\n📡 ESP-NOW DATA RECEIVED");
  Serial.printf("Node %d | Event: %s\n", msg.nodeId, msg.eventType);
}

// =========================================================
// CONNECT WIFI RELIABLY
// =========================================================
void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  Serial.println("🔄 Connecting WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  int retry = 0;
  while (WiFi.status() != WL_CONNECTED && retry < 20) {
    delay(400);
    Serial.print(".");
    retry++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ WiFi Connected!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n❌ WiFi Failed");
  }
}

// =========================================================
// SEND STATUS UPDATE TO SERVER
// =========================================================
void sendStatusToServer() {
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClient client;
  HTTPClient http;

  if (!http.begin(client, serverURL)) {
    Serial.println("❌ http.begin() failed!");
    return;
  }

  http.addHeader("Content-Type", "application/json");

  DynamicJsonDocument doc(2048);
  JsonArray nodes = doc.createNestedArray("nodes");

  for (int i = 0; i < 3; i++) {
    JsonObject n = nodes.createNestedObject();

    n["nodeId"] = nodeData[i].nodeId;
    n["connectionStatus"] =
      (millis() - lastReceived[i] < 10000 ? "Active" : "Offline");

    // GPS
    n["latitude"]  = nodeData[i].latitude;
    n["longitude"] = nodeData[i].longitude;
    n["altitude"]  = nodeData[i].altitude;
    n["satellites"] = nodeData[i].satellites;

    // Sensors
    n["distance"] = nodeData[i].distance;
    n["vibrationCount"] = nodeData[i].vibrationCount;
    n["lightLevel"] = nodeData[i].lightLevel;

    // General
    n["eventType"] = nodeData[i].eventType;
    n["ledState"] = nodeData[i].ledState;
    n["lastSeen"] = lastReceived[i];
  }

  doc["updateType"] = "STATUS";
  doc["timestamp"] = millis();

  String json;
  serializeJson(doc, json);

  int code = http.POST(json);

  Serial.print("📤 STATUS POST: ");
  Serial.println(code);

  if (code > 0) {
    Serial.println(http.getString());
  }

  http.end();
}

// =========================================================
// SEND ACCIDENT ALERT
// =========================================================
void sendAccidentAlert(centralMessage msg) {

  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClient client;
  HTTPClient http;

  http.begin(client, alertURL);
  http.addHeader("Content-Type", "application/json");

  DynamicJsonDocument doc(1024);

  doc["alertType"] = "ACCIDENT";
  doc["severity"] = "CRITICAL";
  doc["nodeId"] = msg.nodeId;
  doc["timestamp"] = millis();

  JsonObject gps = doc.createNestedObject("location");
  gps["latitude"] = msg.latitude;
  gps["longitude"] = msg.longitude;
  gps["altitude"] = msg.altitude;
  gps["satellites"] = msg.satellites;

  JsonObject sensors = doc.createNestedObject("sensors");
  sensors["distance"] = msg.distance;
  sensors["vibrationCount"] = msg.vibrationCount;
  sensors["speed"] = msg.speed;

  String json;
  serializeJson(doc, json);

  Serial.println("\n🚨 SENDING ACCIDENT ALERT...");
  Serial.println(json);

  int code = http.POST(json);
  Serial.print("Response: ");
  Serial.println(code);

  http.end();
}

// =========================================================
// SETUP
// =========================================================
void setup() {
  Serial.begin(115200);

  connectWiFi();

  if (esp_now_init() != 0) {
    Serial.println("❌ ESP-NOW init failed");
    return;
  }

  esp_now_set_self_role(ESP_NOW_ROLE_SLAVE);
  esp_now_register_recv_cb(onReceive);

  Serial.println("✅ ESP-NOW Ready");
}

// =========================================================
// LOOP
// =========================================================
void loop() {

  connectWiFi();

  // Periodic STATUS update every 30 sec
  if (millis() - lastStatusSent > 30000) {
    lastStatusSent = millis();
    sendStatusToServer();
  }

  // Immediate accident alert
  for (int i = 0; i < 3; i++) {
    if (strcmp(nodeData[i].eventType, "Accident Occurred") == 0) {
      sendAccidentAlert(nodeData[i]);
    }
  }

  delay(100);
}
