#include <esp_now.h>
#include <WiFi.h>
#include <TinyGPSPlus.h>

// ======================= PIN DEFINITIONS =======================
#define TRIG_PIN      5     // Ultrasonic Trigger
#define ECHO_PIN      18    // Ultrasonic Echo
#define VIB_PIN       19    // SW-420 Vibration Sensor (Digital)
#define LDR_PIN       34    // LDR Sensor (Analog) - VP pin
#define LED_PIN       2     // Street Light LED (PWM Control)
#define GPS_RX        16    // GPS TX to ESP32 RX
#define GPS_TX        17    // GPS RX to ESP32 TX

// ======================= NODE CONFIGURATION =======================
#define NODE_ID       1     // Change this for each node (1, 2, 3, etc.)

// ======================= THRESHOLDS =======================
#define DIST_THRESHOLD        200   // Distance in cm to detect vehicle
#define LDR_NIGHT_THRESHOLD   2000  // LDR value below = Night (0-4095)
#define VIB_DURATION_THRESH   50    // Vibration duration in ms
#define ACCIDENT_VIB_COUNT    3     // Number of strong vibrations for accident

// ======================= PWM CONFIGURATION =======================
#define PWM_CHANNEL     0
#define PWM_FREQ        5000
#define PWM_RES         8      // 8-bit resolution (0-255)

// ======================= TIMING CONSTANTS =======================
#define LIGHT_HOLD_TIME       8000   // Keep light ON for 8 seconds after vehicle
#define TRAFFIC_BROADCAST_CD  2000   // Cooldown between traffic broadcasts (2s)
#define ACCIDENT_RESET_TIME   300000 // Reset accident after 5 minutes
#define GPS_UPDATE_INTERVAL   1000   // Update GPS every 1 second
#define SENSOR_READ_INTERVAL  200    // Read sensors every 200ms

// ======================= GLOBAL OBJECTS =======================
TinyGPSPlus gps;
HardwareSerial gpsSerial(2);

// ======================= DATA STRUCTURES =======================
typedef struct struct_message {
  int id;               // Node ID
  int command;          // 0=Idle, 1=Traffic, 2=Accident
  float lat;            // GPS Latitude
  float lng;            // GPS Longitude
  unsigned long timestamp; // Timestamp of event
} struct_message;

struct_message myData;
struct_message incomingData;

// ======================= ESP-NOW SETUP =======================
uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
esp_now_peer_info_t peerInfo;

// ======================= STATE VARIABLES =======================
bool isNightTime = false;
bool accidentDetected = false;
bool trafficDetected = false;
int currentBrightness = 0;

// Timing Variables
unsigned long lastTrafficTime = 0;
unsigned long lastTrafficBroadcast = 0;
unsigned long accidentDetectedTime = 0;
unsigned long lastGPSUpdate = 0;
unsigned long lastSensorRead = 0;

// Vibration Detection
int vibrationCount = 0;
unsigned long lastVibrationTime = 0;
unsigned long vibrationStartTime = 0;
bool vibrationActive = false;

// GPS Data
float currentLat = 0.0;
float currentLng = 0.0;
bool gpsValid = false;

// ======================= SETUP =======================
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== Smart Street Light Node ===");
  Serial.print("Node ID: ");
  Serial.println(NODE_ID);
  
  // Initialize GPS
  gpsSerial.begin(9600, SERIAL_8N1, GPS_RX, GPS_TX);
  Serial.println("GPS Initialized");

  // Pin Modes
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(VIB_PIN, INPUT);
  pinMode(LDR_PIN, INPUT);
  
  // PWM Setup for LED
  ledcSetup(PWM_CHANNEL, PWM_FREQ, PWM_RES);
  ledcAttachPin(LED_PIN, PWM_CHANNEL);
  setBrightness(0); // Start OFF
  Serial.println("LED PWM Configured");

  // WiFi in Station Mode for ESP-NOW
  WiFi.mode(WIFI_STA);
  Serial.print("MAC Address: ");
  Serial.println(WiFi.macAddress());

  // Initialize ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("ERROR: ESP-NOW Init Failed");
    return;
  }
  Serial.println("ESP-NOW Initialized");

  // Register Broadcast Peer
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = 0;  
  peerInfo.encrypt = false;
  
  if (esp_now_add_peer(&peerInfo) != ESP_OK){
    Serial.println("ERROR: Failed to add peer");
    return;
  }

  // Register Callbacks
  esp_now_register_send_cb(OnDataSent);
  esp_now_register_recv_cb(OnDataRecv);
  
  Serial.println("System Ready!\n");
}

// ======================= MAIN LOOP =======================
void loop() {
  unsigned long currentMillis = millis();
  
  // 1. Update GPS Data Periodically
  if (currentMillis - lastGPSUpdate >= GPS_UPDATE_INTERVAL) {
    updateGPS();
    lastGPSUpdate = currentMillis;
  }
  
  // Process GPS data continuously
  while (gpsSerial.available() > 0) {
    gps.encode(gpsSerial.read());
  }
  
  // 2. Read Sensors Periodically
  if (currentMillis - lastSensorRead >= SENSOR_READ_INTERVAL) {
    checkDaylightCondition();
    lastSensorRead = currentMillis;
  }
  
  // Only operate at night
  if (!isNightTime) {
    setBrightness(0);
    return;
  }
  
  // 3. Check for Accident (Highest Priority)
  checkVibrationSensor();
  
  // 4. Reset Accident State After Timeout
  if (accidentDetected && (currentMillis - accidentDetectedTime > ACCIDENT_RESET_TIME)) {
    Serial.println("Accident state reset");
    accidentDetected = false;
    vibrationCount = 0;
  }
  
  // 5. Check for Traffic (if no accident)
  if (!accidentDetected) {
    checkTrafficSensors();
  }
  
  // 6. Manage Light State
  manageLightState();
}

// ======================= SENSOR FUNCTIONS =======================

void updateGPS() {
  if (gps.location.isValid()) {
    currentLat = gps.location.lat();
    currentLng = gps.location.lng();
    gpsValid = true;
  } else {
    gpsValid = false;
  }
}

void checkDaylightCondition() {
  int ldrValue = analogRead(LDR_PIN);
  isNightTime = (ldrValue < LDR_NIGHT_THRESHOLD);
  
  // Debug output every 10 seconds
  static unsigned long lastDebug = 0;
  if (millis() - lastDebug > 10000) {
    Serial.print("LDR: ");
    Serial.print(ldrValue);
    Serial.print(" | Night Mode: ");
    Serial.println(isNightTime ? "YES" : "NO");
    lastDebug = millis();
  }
}

long readUltrasonic() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  
  long duration = pulseIn(ECHO_PIN, HIGH, 30000); // 30ms timeout
  if (duration == 0) return -1; // No echo received
  
  long distance = duration * 0.034 / 2;
  return distance;
}

void checkVibrationSensor() {
  int vibState = digitalRead(VIB_PIN);
  unsigned long currentMillis = millis();
  
  if (vibState == HIGH) {
    if (!vibrationActive) {
      // Start of vibration
      vibrationActive = true;
      vibrationStartTime = currentMillis;
      vibrationCount++;
      lastVibrationTime = currentMillis;
      
      Serial.print("Vibration detected! Count: ");
      Serial.println(vibrationCount);
    }
  } else {
    if (vibrationActive) {
      // End of vibration
      unsigned long duration = currentMillis - vibrationStartTime;
      vibrationActive = false;
      
      Serial.print("Vibration duration: ");
      Serial.print(duration);
      Serial.println(" ms");
    }
  }
  
  // Check if multiple vibrations occurred in short time = ACCIDENT
  if (vibrationCount >= ACCIDENT_VIB_COUNT && 
      (currentMillis - lastVibrationTime < 2000)) {
    handleAccident();
    vibrationCount = 0; // Reset counter
  }
  
  // Reset vibration counter after 3 seconds of no activity
  if (currentMillis - lastVibrationTime > 3000) {
    vibrationCount = 0;
  }
}

void checkTrafficSensors() {
  long distance = readUltrasonic();
  
  // Vehicle detected by ultrasonic sensor
  if (distance > 0 && distance < DIST_THRESHOLD) {
    handleTraffic();
  }
}

// ======================= LOGIC HANDLERS =======================

void handleTraffic() {
  trafficDetected = true;
  lastTrafficTime = millis();
  
  setBrightness(255); // Full brightness
  
  // Broadcast with cooldown to prevent spam
  if (millis() - lastTrafficBroadcast >= TRAFFIC_BROADCAST_CD) {
    myData.id = NODE_ID;
    myData.command = 1; // Traffic command
    myData.lat = currentLat;
    myData.lng = currentLng;
    myData.timestamp = millis();
    
    esp_err_t result = esp_now_send(broadcastAddress, (uint8_t *) &myData, sizeof(myData));
    
    if (result == ESP_OK) {
      Serial.println(">> Traffic Alert Broadcasted");
    }
    
    lastTrafficBroadcast = millis();
  }
}

void handleAccident() {
  if (accidentDetected) return; // Already in accident mode
  
  accidentDetected = true;
  accidentDetectedTime = millis();
  
  Serial.println("\n!!! ACCIDENT DETECTED !!!");
  
  // Visual Alert - Strobe Effect
  for(int i = 0; i < 6; i++) {
    setBrightness(255);
    delay(150);
    setBrightness(0);
    delay(150);
  }
  setBrightness(255); // Stay ON
  
  // Broadcast Emergency
  myData.id = NODE_ID;
  myData.command = 2; // Accident command
  myData.lat = currentLat;
  myData.lng = currentLng;
  myData.timestamp = millis();
  
  esp_err_t result = esp_now_send(broadcastAddress, (uint8_t *) &myData, sizeof(myData));
  
  Serial.println(">> EMERGENCY BROADCAST SENT");
  if (gpsValid) {
    Serial.print("Location: ");
    Serial.print(currentLat, 6);
    Serial.print(", ");
    Serial.println(currentLng, 6);
  } else {
    Serial.println("Warning: GPS location not valid");
  }
}

void manageLightState() {
  unsigned long currentMillis = millis();
  
  if (accidentDetected) {
    // Stay bright during accident
    setBrightness(255);
    return;
  }
  
  if (trafficDetected) {
    // Check if hold time expired
    if (currentMillis - lastTrafficTime > LIGHT_HOLD_TIME) {
      trafficDetected = false;
      setBrightness(50); // Dim to 20% (idle)
    }
  } else {
    // Idle state - dim light
    setBrightness(50);
  }
}

void setBrightness(int duty) {
  if (duty != currentBrightness) {
    ledcWrite(PWM_CHANNEL, duty);
    currentBrightness = duty;
  }
}

// ======================= ESP-NOW CALLBACKS =======================

void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  // Optional: Log send status
  if (status != ESP_NOW_SEND_SUCCESS) {
    Serial.println("Send Failed!");
  }
}

void OnDataRecv(const uint8_t *mac, const uint8_t *incomingDataBytes, int len) {
  memcpy(&incomingData, incomingDataBytes, sizeof(incomingData));
  
  Serial.print("\n<< Received from Node ");
  Serial.print(incomingData.id);
  Serial.print(" | Command: ");
  Serial.println(incomingData.command);
  
  // Ignore messages from self
  if (incomingData.id == NODE_ID) return;
  
  // RESPONSE TO TRAFFIC (Command 1)
  if (incomingData.command == 1) {
    Serial.println("-> Neighbor detected traffic, pre-lighting");
    setBrightness(255);
    lastTrafficTime = millis(); // Reset hold timer
  }
  
  // RESPONSE TO ACCIDENT (Command 2)
  if (incomingData.command == 2) {
    Serial.println("!!! SWARM EMERGENCY ALERT !!!");
    Serial.print("Accident at Node ");
    Serial.print(incomingData.id);
    
    if (incomingData.lat != 0.0 && incomingData.lng != 0.0) {
      Serial.print(" | Location: ");
      Serial.print(incomingData.lat, 6);
      Serial.print(", ");
      Serial.println(incomingData.lng, 6);
    } else {
      Serial.println(" | Location: Unknown");
    }
    
    // Warning Strobe
    for(int i = 0; i < 8; i++) {
      setBrightness(255);
      delay(200);
      setBrightness(0);
      delay(200);
    }
    setBrightness(255); // Stay ON
    
    // Enter supporting accident mode
    accidentDetected = true;
    accidentDetectedTime = millis();
  }
}
