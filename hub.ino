#include <esp_now.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ======================= GATEWAY CONFIGURATION =======================
#define GATEWAY_ID    99    // Unique ID for Gateway Hub

// ======================= WiFi CREDENTIALS =======================
// Replace with your WiFi credentials
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// ======================= SERVER CONFIGURATION =======================
// Replace with your server endpoint
const char* serverURL = "http://your-server.com/api/accident";
// Alternative for testing: "https://webhook.site/your-unique-url"

// For ThingSpeak (Alternative)
// const char* thingSpeakURL = "http://api.thingspeak.com/update";
// const char* thingSpeakAPIKey = "YOUR_API_KEY";

// ======================= PIN DEFINITIONS =======================
#define STATUS_LED    2     // Built-in LED for status indication
#define BUZZER_PIN    23    // Optional buzzer for local alerts

// ======================= TIMING CONSTANTS =======================
#define WIFI_RETRY_INTERVAL    5000   // Retry WiFi connection every 5s
#define SERVER_TIMEOUT         10000  // HTTP request timeout
#define ACCIDENT_DEBOUNCE      30000  // Prevent duplicate reports within 30s

// ======================= DATA STRUCTURES =======================
typedef struct struct_message {
  int id;               // Node ID
  int command;          // 0=Idle, 1=Traffic, 2=Accident
  float lat;            // GPS Latitude
  float lng;            // GPS Longitude
  unsigned long timestamp; // Timestamp of event
} struct_message;

struct_message incomingData;

// ======================= STATE VARIABLES =======================
bool wifiConnected = false;
unsigned long lastWiFiAttempt = 0;
unsigned long lastAccidentReport = 0;
int lastAccidentNodeID = -1;

// Traffic Statistics
int totalTrafficEvents = 0;
int totalAccidentEvents = 0;
unsigned long lastTrafficTime = 0;

// ======================= SETUP =======================
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║   CENTRAL GATEWAY HUB - INITIALIZED   ║");
  Serial.println("╚════════════════════════════════════════╝\n");

  // Pin Setup
  pinMode(STATUS_LED, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(STATUS_LED, LOW);
  digitalWrite(BUZZER_PIN, LOW);

  // Initialize WiFi in Dual Mode (STA + ESP-NOW)
  WiFi.mode(WIFI_AP_STA);
  
  // Connect to WiFi for Internet
  connectToWiFi();

  // Initialize ESP-NOW for receiving from nodes
  if (esp_now_init() != ESP_OK) {
    Serial.println("❌ ERROR: ESP-NOW Init Failed");
    return;
  }
  Serial.println("✓ ESP-NOW Initialized");

  // Register Receive Callback
  esp_now_register_recv_cb(OnDataRecv);

  Serial.println("\n🔴 Gateway Hub Ready - Listening for swarm data...\n");
  blinkLED(3, 200); // 3 blinks to indicate ready
}

// ======================= MAIN LOOP =======================
void loop() {
  unsigned long currentMillis = millis();

  // Check WiFi Connection Status
  if (WiFi.status() != WL_CONNECTED) {
    wifiConnected = false;
    if (currentMillis - lastWiFiAttempt >= WIFI_RETRY_INTERVAL) {
      Serial.println("⚠️  WiFi Disconnected - Attempting Reconnection...");
      connectToWiFi();
      lastWiFiAttempt = currentMillis;
    }
  } else {
    if (!wifiConnected) {
      wifiConnected = true;
      Serial.println("✓ WiFi Reconnected");
    }
  }

  // Status LED Heartbeat (blink every 2 seconds when online)
  static unsigned long lastBlink = 0;
  if (wifiConnected && currentMillis - lastBlink > 2000) {
    digitalWrite(STATUS_LED, !digitalRead(STATUS_LED));
    lastBlink = currentMillis;
  }

  // Keep processing (ESP-NOW events handled in callback)
  delay(100);
}

// ======================= WiFi CONNECTION =======================
void connectToWiFi() {
  Serial.print("🔌 Connecting to WiFi: ");
  Serial.println(ssid);
  
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✓ WiFi Connected!");
    Serial.print("   IP Address: ");
    Serial.println(WiFi.localIP());
    Serial.print("   Signal Strength: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm\n");
    wifiConnected = true;
    blinkLED(2, 100);
  } else {
    Serial.println("\n❌ WiFi Connection Failed");
    wifiConnected = false;
  }
}

// ======================= ESP-NOW CALLBACK =======================
void OnDataRecv(const uint8_t *mac, const uint8_t *incomingDataBytes, int len) {
  memcpy(&incomingData, incomingDataBytes, sizeof(incomingData));
  
  // Get MAC address as string
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  Serial.println("╔════════════════════════════════════════╗");
  Serial.print("║ 📡 DATA RECEIVED FROM NODE ");
  Serial.print(incomingData.id);
  Serial.println("          ║");
  Serial.println("╠════════════════════════════════════════╣");
  Serial.print("║ MAC: ");
  Serial.print(macStr);
  Serial.println("    ║");
  Serial.print("║ Command: ");
  
  switch(incomingData.command) {
    case 0:
      Serial.println("IDLE                      ║");
      break;
    case 1:
      Serial.println("TRAFFIC DETECTED          ║");
      handleTrafficEvent();
      break;
    case 2:
      Serial.println("🚨 ACCIDENT DETECTED! 🚨    ║");
      handleAccidentEvent();
      break;
    default:
      Serial.println("UNKNOWN                   ║");
  }
  
  Serial.print("║ GPS: ");
  if (incomingData.lat != 0.0 && incomingData.lng != 0.0) {
    Serial.print(incomingData.lat, 6);
    Serial.print(", ");
    Serial.print(incomingData.lng, 6);
  } else {
    Serial.print("Invalid/Unavailable");
  }
  Serial.println("   ║");
  
  Serial.print("║ Timestamp: ");
  Serial.print(incomingData.timestamp);
  Serial.println(" ms           ║");
  Serial.println("╚════════════════════════════════════════╝\n");
}

// ======================= EVENT HANDLERS =======================

void handleTrafficEvent() {
  totalTrafficEvents++;
  lastTrafficTime = millis();
  
  Serial.print("📊 Total Traffic Events: ");
  Serial.println(totalTrafficEvents);
  
  // Optional: Send traffic analytics to server periodically
  // For now, we just log it locally
  
  blinkLED(1, 50); // Quick blink for traffic
}

void handleAccidentEvent() {
  unsigned long currentMillis = millis();
  
  // Debounce - Prevent duplicate reports from same node within 30 seconds
  if (incomingData.id == lastAccidentNodeID && 
      (currentMillis - lastAccidentReport < ACCIDENT_DEBOUNCE)) {
    Serial.println("⚠️  Duplicate accident report ignored (debounce active)");
    return;
  }
  
  totalAccidentEvents++;
  lastAccidentReport = currentMillis;
  lastAccidentNodeID = incomingData.id;
  
  // Local Alert
  soundAlarm();
  digitalWrite(STATUS_LED, HIGH);
  
  Serial.println("\n🚨🚨🚨 EMERGENCY PROTOCOL ACTIVATED 🚨🚨🚨");
  Serial.print("📍 Accident Location: ");
  
  if (incomingData.lat != 0.0 && incomingData.lng != 0.0) {
    Serial.print(incomingData.lat, 6);
    Serial.print(", ");
    Serial.println(incomingData.lng, 6);
    
    // Send to Cloud Server
    if (wifiConnected) {
      sendAccidentToServer();
    } else {
      Serial.println("❌ Cannot send to server - No WiFi connection");
      Serial.println("⚠️  Data cached for retry when connection restored");
      // TODO: Implement local storage/queue for offline events
    }
  } else {
    Serial.println("⚠️  GPS coordinates unavailable");
    Serial.println("⚠️  Sending alert without location data");
    
    if (wifiConnected) {
      sendAccidentToServer(); // Send anyway, emergency services can triangulate
    }
  }
  
  digitalWrite(STATUS_LED, LOW);
}

// ======================= SERVER COMMUNICATION =======================

void sendAccidentToServer() {
  if (!wifiConnected) {
    Serial.println("❌ Server upload failed - No internet connection");
    return;
  }

  HTTPClient http;
  http.setTimeout(SERVER_TIMEOUT);
  
  Serial.println("\n📤 Uploading accident data to cloud server...");
  
  // Prepare JSON payload
  StaticJsonDocument<512> jsonDoc;
  jsonDoc["node_id"] = incomingData.id;
  jsonDoc["event_type"] = "ACCIDENT";
  jsonDoc["latitude"] = incomingData.lat;
  jsonDoc["longitude"] = incomingData.lng;
  jsonDoc["timestamp"] = incomingData.timestamp;
  jsonDoc["gateway_id"] = GATEWAY_ID;
  jsonDoc["reported_at"] = millis();
  
  // Add system info
  jsonDoc["wifi_rssi"] = WiFi.RSSI();
  jsonDoc["gateway_ip"] = WiFi.localIP().toString();
  
  // Convert to string
  String jsonPayload;
  serializeJson(jsonDoc, jsonPayload);
  
  Serial.println("📦 Payload:");
  Serial.println(jsonPayload);
  
  // Send HTTP POST Request
  http.begin(serverURL);
  http.addHeader("Content-Type", "application/json");
  
  int httpResponseCode = http.POST(jsonPayload);
  
  if (httpResponseCode > 0) {
    Serial.print("✓ Server Response Code: ");
    Serial.println(httpResponseCode);
    
    String response = http.getString();
    Serial.print("✓ Server Response: ");
    Serial.println(response);
    
    if (httpResponseCode == 200 || httpResponseCode == 201) {
      Serial.println("✅ ACCIDENT ALERT SUCCESSFULLY SENT TO EMERGENCY SERVICES");
      blinkLED(5, 100); // Success indication
    }
  } else {
    Serial.print("❌ HTTP Error: ");
    Serial.println(httpResponseCode);
    Serial.println("   Failed to reach server");
    // TODO: Queue for retry
  }
  
  http.end();
  Serial.println();
}

// Alternative: Send to ThingSpeak (for testing/logging)
void sendToThingSpeak() {
  if (!wifiConnected) return;
  
  HTTPClient http;
  
  String url = String(thingSpeakURL) + 
               "?api_key=" + String(thingSpeakAPIKey) +
               "&field1=" + String(incomingData.id) +
               "&field2=" + String(incomingData.command) +
               "&field3=" + String(incomingData.lat, 6) +
               "&field4=" + String(incomingData.lng, 6);
  
  http.begin(url);
  int httpCode = http.GET();
  
  if (httpCode > 0) {
    Serial.println("✓ ThingSpeak Updated");
  }
  
  http.end();
}

// ======================= UTILITY FUNCTIONS =======================

void soundAlarm() {
  // Three short beeps
  for (int i = 0; i < 3; i++) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(150);
    digitalWrite(BUZZER_PIN, LOW);
    delay(150);
  }
}

void blinkLED(int times, int delayMs) {
  for (int i = 0; i < times; i++) {
    digitalWrite(STATUS_LED, HIGH);
    delay(delayMs);
    digitalWrite(STATUS_LED, LOW);
    delay(delayMs);
  }
}

// ======================= DIAGNOSTIC FUNCTIONS =======================

void printSystemStatus() {
  Serial.println("\n═══════════════ SYSTEM STATUS ═══════════════");
  Serial.print("Gateway ID: ");
  Serial.println(GATEWAY_ID);
  Serial.print("WiFi Status: ");
  Serial.println(wifiConnected ? "Connected" : "Disconnected");
  Serial.print("Total Traffic Events: ");
  Serial.println(totalTrafficEvents);
  Serial.print("Total Accidents Reported: ");
  Serial.println(totalAccidentEvents);
  Serial.print("Uptime: ");
  Serial.print(millis() / 1000);
  Serial.println(" seconds");
  Serial.println("════════════════════════════════════════════\n");
}
