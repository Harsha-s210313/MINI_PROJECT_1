#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

// ============ RECEIVER MAC (ESP8266) ============
uint8_t receiverMAC[] = {0xD8, 0xBF, 0xC0, 0x05, 0x96, 0x97};

// Same SSID your ESP8266 connects to
const char* wifiSSID = "vivo Y75 5G";

// Node ID
#define NODE_ID 1

// ============ DATA STRUCTURE (same as receiver) ============
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

centralMessage sensorData;
int cycleCounter = 0;

// =============================================================
// NEW CALLBACK FORMAT FOR ESP32 IDF 5.x
// =============================================================
void onSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
  Serial.print("📡 Send Status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "✅ Success" : "❌ Fail");
}

// =============================================================
// AUTO-DETECT ROUTER CHANNEL (so ESP-NOW matches ESP8266)
// =============================================================
int getWiFiChannel(const char* ssid) {
  Serial.println("\n🔍 Scanning to determine WiFi channel...");
  int networks = WiFi.scanNetworks();

  for (int i = 0; i < networks; i++) {
    if (WiFi.SSID(i) == ssid) {
      int channel = WiFi.channel(i);
      Serial.printf("📡 Router FOUND on Channel %d\n", channel);
      return channel;
    }
  }

  Serial.println("⚠️ Router NOT found! Using fallback channel 1.");
  return 1;
}

// =============================================================
// MOCK SENSOR DATA GENERATOR
// =============================================================
void generateMockData() {
  cycleCounter++;

  sensorData.nodeId = NODE_ID;

  int scenario = (cycleCounter / 5) % 4;  // Every 15 sec change mode

  switch (scenario) {
    case 0:
      strcpy(sensorData.eventType, "Normal");
      strcpy(sensorData.gpsStatus, "GPS Valid");
      sensorData.latitude = 17.385044 + (random(-100, 100) / 1000000.0);
      sensorData.longitude = 78.486671 + (random(-100, 100) / 1000000.0);
      sensorData.altitude = 542.0 + random(-5, 5);
      sensorData.speed = random(0, 3);
      sensorData.satellites = 8;

      strcpy(sensorData.ultrasonicStatus, "Sensor Working");
      sensorData.distance = 100 + random(-20, 20);

      strcpy(sensorData.ldrStatus, "Sensor Working");
      sensorData.lightLevel = 2500 + random(-200, 200);

      strcpy(sensorData.vibrationStatus, "Sensor Working");
      sensorData.vibrationCount = random(0, 2);

      sensorData.ledState = false;
      break;

    case 1:
      strcpy(sensorData.eventType, "Object Detected");
      strcpy(sensorData.gpsStatus, "GPS Valid");
      sensorData.latitude = 17.385100;
      sensorData.longitude = 78.486700;
      sensorData.altitude = 543.0;
      sensorData.speed = 5.0;
      sensorData.satellites = 9;

      strcpy(sensorData.ultrasonicStatus, "Sensor Working");
      sensorData.distance = 25.0;

      strcpy(sensorData.ldrStatus, "Sensor Working");
      sensorData.lightLevel = 2300;

      strcpy(sensorData.vibrationStatus, "Sensor Working");
      sensorData.vibrationCount = random(1, 3);

      sensorData.ledState = true;
      break;

    case 2:
      strcpy(sensorData.eventType, "Low Light");
      strcpy(sensorData.gpsStatus, "GPS Valid");
      sensorData.latitude = 17.385200;
      sensorData.longitude = 78.486800;
      sensorData.altitude = 544;
      sensorData.speed = 0.5;
      sensorData.satellites = 7;

      strcpy(sensorData.ultrasonicStatus, "Sensor Working");
      sensorData.distance = 150;

      strcpy(sensorData.ldrStatus, "Sensor Working");
      sensorData.lightLevel = 1500 + random(-200, 200);

      strcpy(sensorData.vibrationStatus, "Sensor Working");
      sensorData.vibrationCount = 0;

      sensorData.ledState = true;
      break;

    case 3:
      strcpy(sensorData.eventType, "Accident Occurred");
      strcpy(sensorData.gpsStatus, "GPS Valid");
      sensorData.latitude = 17.385150;
      sensorData.longitude = 78.486750;
      sensorData.altitude = 543.5;
      sensorData.speed = 15.0;
      sensorData.satellites = 8;

      strcpy(sensorData.ultrasonicStatus, "Sensor Working");
      sensorData.distance = 10.0;

      strcpy(sensorData.ldrStatus, "Sensor Working");
      sensorData.lightLevel = 2200;

      strcpy(sensorData.vibrationStatus, "Sensor Working");
      sensorData.vibrationCount = 7 + random(0, 3);

      sensorData.ledState = true;
      break;
  }
}

// =============================================================
// SEND DATA
// =============================================================
void sendDataToReceiver() {
  esp_err_t result = esp_now_send(receiverMAC, (uint8_t*)&sensorData, sizeof(sensorData));

  Serial.println("════════════════════════════════════");
  Serial.printf("📤 Node %d Sending...\n", NODE_ID);
  Serial.println(sensorData.eventType);

  if (result == ESP_OK)
    Serial.println("ESP-NOW Result: ✅ OK");
  else
    Serial.println("ESP-NOW Result: ❌ ERROR");

  Serial.println("════════════════════════════════════\n");
}

// =============================================================
// SETUP
// =============================================================
void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("======================================");
  Serial.println("    ESP32 MOCK SENDER (AUTO CHANNEL)  ");
  Serial.println("======================================");

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  // Step 1 → Detect WiFi channel
  int channel = getWiFiChannel(wifiSSID);

  // Step 2 → Set ESP32 channel
  esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
  Serial.printf("📡 ESP32 Now Operating on Channel %d\n", channel);

  // Step 3 → Init ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("❌ ESP-NOW INIT FAILED!");
    return;
  }

  // Step 4 → Register callback
  esp_now_register_send_cb(onSent);

  // Step 5 → Add ESP8266 as peer
  esp_now_peer_info_t peer{};
  memcpy(peer.peer_addr, receiverMAC, 6);
  peer.channel = channel;
  peer.encrypt = false;

  if (esp_now_add_peer(&peer) != ESP_OK) {
    Serial.println("❌ Failed to add peer!");
    return;
  }

  Serial.println("✅ ESP-NOW Ready!\n");
}

// =============================================================
// LOOP
// =============================================================
void loop() {
  generateMockData();
  sendDataToReceiver();
  delay(3000);
}
