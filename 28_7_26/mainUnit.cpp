
// [env:4d_systems_esp32s3_gen4_r8n16]
//  platform = espressif32@6.5.0      ; 🌟 Version 6.5.0 (Core 2.x) ကို အသေမှတ်ပါမည်
//  board = 4d_systems_esp32s3_gen4_r8n16
//  framework = arduino
//  monitor_speed = 115200
//  lib_ldf_mode = deep+

//  lib_deps = 
//      Wire
//      SPI
//      adafruit/RTClib@^2.1



#include <WiFi.h>
#include <esp_now.h>
#include <WebServer.h>
#include <Preferences.h>
#include <Wire.h>     
#include <RTClib.h>   

WebServer server(80);
Preferences preferences;
RTC_DS3231 rtc;       

const int configButtonPin = 0; 
const int buzzerPin = 18;      
const int SDA_PIN = 8;         
const int SCL_PIN = 9;         

String wifi_ssid;
String wifi_password;

// 🐄 & 🐔 MAC Addresses
uint8_t cowMac[] = {0x1C, 0xDB, 0xD4, 0x76, 0x67, 0xD4}; 
uint8_t chickenMac[] = {0xA4, 0xCB, 0x8F, 0xDA, 0x37, 0xE4}; 

// 🐄 Cow Data Structure
typedef struct cow_message {
  int unit_id;
  float temperature;
  int activity_state;
  int rssi_value;
  bool geofence_alert;
} cow_message;

// 🐔 Chicken Data Structure (without RSSI)
typedef struct chicken_message {
  int unit_id;
  float temperature;
  int air_quality;
  bool fan_status;
} chicken_message;

cow_message cowData;
chicken_message chickenData;

unsigned long lastCowUpdate = 0; 
unsigned long lastChickenUpdate = 0; 
String lastUpdateTimeStr = "Waiting for data..."; 

// 🌟 Data Receive Callback Function (For both units)
void OnDataRecv(const uint8_t *mac_addr, const uint8_t *incomingData, int len) {
  
  // Record current time from RTC
  DateTime now = rtc.now();
  char timeBuffer[30];
  sprintf(timeBuffer, "%04d-%02d-%02d %02d:%02d:%02d", now.year(), now.month(), now.day(), now.hour(), now.minute(), now.second());
  lastUpdateTimeStr = String(timeBuffer);

  // Check if data is from Cow Unit
  if (len == sizeof(cow_message) && memcmp(mac_addr, cowMac, 6) == 0) {
    memcpy(&cowData, incomingData, sizeof(cowData));
    lastCowUpdate = millis(); 
    Serial.println("✅ Data received from COW Unit");
  } 
  // Check if data is from Chicken Unit
  else if (len == sizeof(chicken_message) && memcmp(mac_addr, chickenMac, 6) == 0) {
    memcpy(&chickenData, incomingData, sizeof(chickenData));
    lastChickenUpdate = millis(); 
    Serial.println("✅ Data received from CHICKEN Unit");
  } else {
    Serial.println("⚠️ Unknown Data Received");
  }
}

// Function to generate and serve the HTML Dashboard
void handleRoot() {
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta http-equiv='refresh' content='5'>"; 
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<style>body{font-family: sans-serif; margin: 20px; background-color: #f4f4f9;} ";
  html += ".card{border: 1px solid #ddd; padding: 15px; border-radius: 10px; margin-bottom: 15px; background: white; box-shadow: 2px 2px 5px rgba(0,0,0,0.1);} ";
  html += ".alert{color: white; background-color: #e53935; border-color: #b71c1c;} ";
  html += ".offline{color: #555; background-color: #e0e0e0;} ";
  html += "h3{margin-top:0; color:#2c3e50;}</style></head><body>";
  
  html += "<h2>🌾 Smart Animal Farm Dashboard</h2>";
  html += "<p style='color: #666;'>🕒 Last Updated: <b>" + lastUpdateTimeStr + "</b></p><hr>";
  
  bool mainBuzzerTrigger = false;

  // ================= 🐄 COW CARD =================
  bool isCowOffline = (millis() - lastCowUpdate > 15000); 
  if (isCowOffline) {
     html += "<div class='card offline'><h3>🐄 Cow Unit - <span style='color:red;'>OFFLINE</span></h3><p>No data recently.</p></div>";
  } else {
     bool isCowSick = (cowData.temperature > 38.5); 
     bool isCowCritical = (cowData.activity_state == 2 || cowData.geofence_alert || isCowSick);
     
     if (isCowCritical) mainBuzzerTrigger = true;
     
     String alertClass = isCowCritical ? "alert" : "";
     html += "<div class='card " + alertClass + "'><h3>🐄 Cow Unit - <span>ONLINE</span></h3>";
     
     if (isCowSick) html += "<p>🌡️ Temp: <b>" + String(cowData.temperature) + " &deg;C 🚨(Sick!)</b></p>";
     else html += "<p>🌡️ Temp: <b>" + String(cowData.temperature) + " &deg;C</b></p>";
     
     String activityText = "Idle";
     if (cowData.activity_state == 1) activityText = "Active";
     else if (cowData.activity_state == 2) activityText = "⚠️ CRITICAL (Fighting)";
     html += "<p>🏃 Activity: <b>" + activityText + "</b></p>";
     
     if (cowData.geofence_alert) html += "<p><strong>🚨 OUT OF BOUNDS 🚨</strong></p>";
     html += "<p style='font-size:12px;'>📶 Signal: " + String(cowData.rssi_value) + " dBm</p></div>";
  }

  // ================= 🐔 CHICKEN CARD =================
  bool isChickenOffline = (millis() - lastChickenUpdate > 15000); 
  if (isChickenOffline) {
     html += "<div class='card offline'><h3>🐔 Chicken Unit - <span style='color:red;'>OFFLINE</span></h3><p>No data recently.</p></div>";
  } else {
     // Mark as critical if temp > 32 OR air quality > 1500
     bool isChickenCritical = (chickenData.temperature > 32.0 || chickenData.air_quality > 1500); 
     if (isChickenCritical) mainBuzzerTrigger = true;

     String alertClass = isChickenCritical ? "alert" : "";
     html += "<div class='card " + alertClass + "'><h3>🐔 Chicken Unit - <span>ONLINE</span></h3>";
     
     html += "<p>🌡️ Temp: <b>" + String(chickenData.temperature) + " &deg;C</b></p>";
     
     if (chickenData.air_quality > 1500) html += "<p>💨 Air Quality: <b>" + String(chickenData.air_quality) + " 🚨(Poor!)</b></p>";
     else html += "<p>💨 Air Quality: <b>" + String(chickenData.air_quality) + " (Good)</b></p>";
     
     if (chickenData.fan_status) html += "<p>🌀 Exhaust Fan: <b>ON 🟢</b></p>";
     else html += "<p>🌀 Exhaust Fan: <b>OFF 🔴</b></p>";
     html += "</div>";
  }

  // Trigger main buzzer if any critical alert is active
  if (mainBuzzerTrigger) digitalWrite(buzzerPin, HIGH);
  else digitalWrite(buzzerPin, LOW);

  html += "</body></html>";
  server.send(200, "text/html", html);
}

void setup() {
  Serial.begin(115200);
  pinMode(configButtonPin, INPUT_PULLUP);
  pinMode(buzzerPin, OUTPUT);
  digitalWrite(buzzerPin, LOW); // Keep buzzer OFF initially

  // Initialize RTC
  Wire.begin(SDA_PIN, SCL_PIN);
  if (!rtc.begin(&Wire)) {
    Serial.println("Couldn't find RTC");
  } else if (rtc.lostPower()) {
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__))); // Set time to compile time
  }
  
  // Load router credentials from memory
  preferences.begin("farm_config", false);
  wifi_ssid = preferences.getString("wifi_ssid", "YOUR_ROUTER_SSID"); 
  wifi_password = preferences.getString("wifi_pass", "YOUR_ROUTER_PASS"); 

  Serial.println("\nPress BOOT Button within 3 seconds to change Router SSID & Password...");
  delay(3000);

  // Config Mode to change WiFi credentials via Serial Monitor
  if (digitalRead(configButtonPin) == LOW) {
    while (Serial.available() > 0) Serial.read(); 
    Serial.print("Enter Router WiFi SSID: ");
    while (Serial.available() == 0) { delay(10); } 
    String newSSID = Serial.readStringUntil('\n');
    newSSID.trim();
    if (newSSID.length() > 0) {
      preferences.putString("wifi_ssid", newSSID);
      wifi_ssid = newSSID;
    } 

    while (Serial.available() > 0) Serial.read(); 
    Serial.print("Enter Router WiFi Password: ");
    while (Serial.available() == 0) { delay(10); } 
    String newPassword = Serial.readStringUntil('\n');
    newPassword.trim();
    if (newPassword.length() >= 8) {
      preferences.putString("wifi_pass", newPassword);
      wifi_password = newPassword;
    }
  }

  // Setup WiFi connection
  WiFi.mode(WIFI_STA); 
  WiFi.setSleep(false); // Disable sleep mode to prevent packet loss
  WiFi.begin(wifi_ssid.c_str(), wifi_password.c_str());
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) { 
    delay(500);
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("\n🌐 Dashboard IP: ");
    Serial.println(WiFi.localIP()); 
  } else {
    Serial.println("\nFailed to connect. Starting Backup AP...");
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP("FARM_BACKUP", "12345678");
  }

  Serial.print("📶 Channel: "); Serial.println(WiFi.channel());
  Serial.print("🔑 MAIN MAC: "); Serial.println(WiFi.macAddress()); 

  // Initialize ESP-NOW
  if (esp_now_init() != ESP_OK) return;
  esp_now_register_recv_cb((esp_now_recv_cb_t)OnDataRecv);

  // Start HTTP Server
  server.on("/", handleRoot);
  server.begin();
}

void loop() {
  server.handleClient(); // Listen for incoming web clients
}
