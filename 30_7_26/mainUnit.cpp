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
#include <time.h>     // Built-in library for handling time

WebServer server(80);
Preferences preferences;

// Pins
const int configButtonPin = 0; 
const int buzzerPin = 18;      

// Network Credentials
String wifi_ssid;
String wifi_password;

// 🐄, 🐔, 🐟 MAC Addresses
uint8_t cowMac[] = {0x1C, 0xDB, 0xD4, 0x76, 0x67, 0xD4}; 
uint8_t chickenMac[] = {0xA4, 0xCB, 0x8F, 0xDA, 0x37, 0xE4}; 
uint8_t fishMac[] = {0xA4, 0xCB, 0x8F, 0xD9, 0x7D, 0x98}; 

// 🐄 Cow Data Structure
typedef struct cow_message {
  int unit_id;
  float temperature;
  int activity_state;
  int rssi_value;
  bool geofence_alert;
} cow_message;

// 🐔 Chicken Data Structure
typedef struct chicken_message {
  int unit_id;
  float temperature;
  int air_quality;
  bool fan_status;
} chicken_message;

// 🐟 Fish Data Structure
typedef struct fish_message {
  int unit_id;
  float temperature;
  float ph_level;
  bool is_critical; 
} fish_message;

// Global Data Variables
cow_message cowData;
chicken_message chickenData;
fish_message fishData;

// Track the last time data was received to detect "Offline" status
unsigned long lastCowUpdate = 0; 
unsigned long lastChickenUpdate = 0; 
unsigned long lastFishUpdate = 0; 

// Function to fetch time from internet and set internal ESP32 clock
void syncInternalClockWithNTP() {
  Serial.print("\n⏳ Syncing Internal Clock with Internet Time (NTP)...");
  // Myanmar Standard Time is UTC +6:30 (23400 seconds)
  configTime(23400, 0, "pool.ntp.org", "time.nist.gov");
  
  struct tm timeinfo;
  int retryCount = 0;
  
  // Wait up to 10 seconds for NTP to sync
  while (!getLocalTime(&timeinfo, 1000) && retryCount < 10) {
    Serial.print(".");
    retryCount++;
  }
  
  if (retryCount < 10) {
    Serial.println("\n✅ Time successfully synced! Internal clock is accurate.");
  } else {
    Serial.println("\n⚠️ Failed to get Internet Time. Dashboard will show 1970.");
  }
}

// Callback function executed when data is received via ESP-NOW
void OnDataRecv(const uint8_t *mac_addr, const uint8_t *incomingData, int len) {
  // Check if it's the Cow Unit
  if (len == sizeof(cow_message) && memcmp(mac_addr, cowMac, 6) == 0) {
    memcpy(&cowData, incomingData, sizeof(cowData));
    lastCowUpdate = millis(); 
  } 
  // Check if it's the Chicken Unit
  else if (len == sizeof(chicken_message) && memcmp(mac_addr, chickenMac, 6) == 0) {
    memcpy(&chickenData, incomingData, sizeof(chickenData));
    lastChickenUpdate = millis(); 
  } 
  // Check if it's the Fish Unit
  else if (len == sizeof(fish_message) && memcmp(mac_addr, fishMac, 6) == 0) {
    memcpy(&fishData, incomingData, sizeof(fishData));
    lastFishUpdate = millis(); 
  }
}

// Function to serve the Professional Web Dashboard
void handleRoot() {
  // Read the current time from ESP32's internal clock
  struct tm timeinfo;
  bool timeValid = getLocalTime(&timeinfo);

  // --- HTML & CSS DESIGN START ---
  String html = "<!DOCTYPE html><html lang='en'><head><meta charset='UTF-8'>"; 
  html += "<meta http-equiv='refresh' content='5'>"; // Auto-refresh every 5 seconds
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>Smart Farm Dashboard</title>";
  
  // Advanced CSS Styling for modern UI/UX
  html += "<style>";
  html += ":root { --bg: #f8fafc; --card: #ffffff; --text: #0f172a; --muted: #64748b; ";
  html += "--green: #10b981; --green-bg: #d1fae5; --red: #ef4444; --red-bg: #fee2e2; ";
  html += "--gray: #94a3b8; --gray-bg: #f1f5f9; --border: #e2e8f0; }";
  
  html += "body { font-family: system-ui, -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; ";
  html += "background-color: var(--bg); color: var(--text); margin: 0; padding: 20px; display: flex; flex-direction: column; align-items: center; }";
  
  html += ".header { text-align: center; margin-bottom: 30px; width: 100%; max-width: 1000px;}";
  html += ".header h2 { margin: 0 0 10px 0; font-size: 28px; font-weight: 800; letter-spacing: -0.5px; color: #1e293b; }";
  html += ".time-badge { background: var(--card); padding: 8px 20px; border-radius: 30px; box-shadow: 0 2px 4px rgba(0,0,0,0.05); ";
  html += "font-size: 14px; font-weight: 500; color: var(--muted); border: 1px solid var(--border); display: inline-block; }";
  
  html += ".grid { display: grid; gap: 24px; grid-template-columns: repeat(auto-fit, minmax(300px, 1fr)); width: 100%; max-width: 1000px; }";
  
  html += ".card { background: var(--card); border-radius: 16px; padding: 24px; box-shadow: 0 4px 6px -1px rgba(0,0,0,0.05), 0 2px 4px -1px rgba(0,0,0,0.03); ";
  html += "border: 1px solid var(--border); transition: transform 0.2s ease; }";
  html += ".card:hover { transform: translateY(-3px); }";
  html += ".card.alert-card { border: 2px solid var(--red); box-shadow: 0 4px 15px rgba(239, 68, 68, 0.15); }";
  
  html += ".card-header { display: flex; justify-content: space-between; align-items: center; border-bottom: 1px solid var(--border); padding-bottom: 15px; margin-bottom: 18px; }";
  html += ".card-header h3 { margin: 0; font-size: 18px; display: flex; align-items: center; gap: 10px; font-weight: 700;}";
  
  html += ".status { padding: 6px 12px; border-radius: 20px; font-size: 11px; font-weight: 800; text-transform: uppercase; letter-spacing: 0.5px; }";
  html += ".status.online { background: var(--green-bg); color: #065f46; }";
  html += ".status.critical { background: var(--red-bg); color: #991b1b; }";
  html += ".status.offline { background: var(--gray-bg); color: #475569; }";
  
  html += ".data-row { display: flex; justify-content: space-between; align-items: center; margin-bottom: 14px; font-size: 15px; }";
  html += ".data-row:last-child { margin-bottom: 0; }";
  html += ".data-label { color: var(--muted); display: flex; align-items: center; gap: 8px; font-weight: 500;}";
  html += ".data-value { font-weight: 700; color: var(--text); }";
  html += ".text-red { color: var(--red); }";
  html += ".text-green { color: var(--green); }";
  html += ".signal { font-size: 12px; color: #cbd5e1; font-weight: normal; margin-left: 5px; }";
  
  html += ".offline-msg { text-align: center; color: var(--muted); font-style: italic; padding: 20px 0; }";
  
  // Smooth fade-in animation to mask the 5-second reload
  html += "@keyframes fadeIn { from { opacity: 0.7; } to { opacity: 1; } }";
  html += "body { animation: fadeIn 0.4s ease-in-out; }";
  html += "</style></head><body>";
  
  // --- HEADER & LIVE TIME ---
  html += "<div class='header'><h2>🌾 Smart Animal Farm</h2>";
  if (timeValid) {
    html += "<div class='time-badge'>🕒 <span id='liveClock'>Loading Date & Time...</span></div></div>";
    
    // Improved Professional JS Clock
    html += "<script>";
    html += "var rtcTime = new Date(" + String(timeinfo.tm_year + 1900) + ", " + String(timeinfo.tm_mon) + ", " + String(timeinfo.tm_mday) + ", " + String(timeinfo.tm_hour) + ", " + String(timeinfo.tm_min) + ", " + String(timeinfo.tm_sec) + ");";
    html += "const months = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];";
    html += "function updateClock() {";
    html += "  var y = rtcTime.getFullYear();";
    html += "  var mo = months[rtcTime.getMonth()];";
    html += "  var d = rtcTime.getDate().toString().padStart(2, '0');";
    html += "  var h = rtcTime.getHours().toString().padStart(2, '0');";
    html += "  var m = rtcTime.getMinutes().toString().padStart(2, '0');";
    html += "  var s = rtcTime.getSeconds().toString().padStart(2, '0');";
    html += "  document.getElementById('liveClock').innerHTML = mo + ' ' + d + ', ' + y + ' &nbsp;|&nbsp; ' + h + ':' + m + ':' + s;";
    html += "  rtcTime.setSeconds(rtcTime.getSeconds() + 1);"; 
    html += "}";
    html += "updateClock(); setInterval(updateClock, 1000);"; 
    html += "</script>";
  } else {
    html += "<div class='time-badge' style='color: var(--red);'>🕒 Waiting for Time Sync...</div></div>";
  }

  // --- GRID CONTAINER ---
  html += "<div class='grid'>";
  bool mainBuzzerTrigger = false;

  // ================= 🐄 COW CARD =================
  bool isCowOffline = (millis() - lastCowUpdate > 15000); 
  if (isCowOffline) {
     html += "<div class='card'><div class='card-header'><h3>🐄 Cow Unit</h3><span class='status offline'>Offline</span></div>";
     html += "<div class='offline-msg'>No connection established.</div></div>";
  } else {
     bool isCowSick = (cowData.temperature > 38.5); 
     bool isCowCritical = (cowData.activity_state == 2 || cowData.geofence_alert || isCowSick);
     if (isCowCritical) mainBuzzerTrigger = true;
     
     String cardClass = isCowCritical ? "card alert-card" : "card";
     String badgeClass = isCowCritical ? "status critical" : "status online";
     String badgeText = isCowCritical ? "Critical" : "Online";
     
     html += "<div class='" + cardClass + "'><div class='card-header'><h3>🐄 Cow Unit</h3><span class='" + badgeClass + "'>" + badgeText + "</span></div>";
     
     // Temperature Row
     html += "<div class='data-row'><span class='data-label'>🌡️ Temperature</span>";
     if (isCowSick) html += "<span class='data-value text-red'>" + String(cowData.temperature, 1) + " &deg;C (Fever)</span></div>";
     else html += "<span class='data-value'>" + String(cowData.temperature, 1) + " &deg;C</span></div>";
     
     // Activity Row
     String actText = "Idle";
     String actStyle = "data-value";
     if (cowData.activity_state == 1) actText = "Active";
     else if (cowData.activity_state == 2) { actText = "Fighting!"; actStyle = "data-value text-red"; }
     html += "<div class='data-row'><span class='data-label'>🏃 Activity</span><span class='" + actStyle + "'>" + actText + "</span></div>";
     
     // Geofence / Signal Row
     html += "<div class='data-row'><span class='data-label'>📍 Location <span class='signal'>(" + String(cowData.rssi_value) + " dBm)</span></span>";
     if (cowData.geofence_alert) html += "<span class='data-value text-red'>Out of Bounds!</span></div>";
     else html += "<span class='data-value text-green'>Inside Farm</span></div>";
     
     html += "</div>"; // End Card
  }

  // ================= 🐔 CHICKEN CARD =================
  bool isChickenOffline = (millis() - lastChickenUpdate > 15000); 
  if (isChickenOffline) {
     html += "<div class='card'><div class='card-header'><h3>🐔 Chicken Unit</h3><span class='status offline'>Offline</span></div>";
     html += "<div class='offline-msg'>No connection established.</div></div>";
  } else {
     bool isChickenCritical = (chickenData.temperature > 32.0 || chickenData.air_quality > 1500); 
     if (isChickenCritical) mainBuzzerTrigger = true;

     String cardClass = isChickenCritical ? "card alert-card" : "card";
     String badgeClass = isChickenCritical ? "status critical" : "status online";
     String badgeText = isChickenCritical ? "Critical" : "Online";

     html += "<div class='" + cardClass + "'><div class='card-header'><h3>🐔 Chicken Unit</h3><span class='" + badgeClass + "'>" + badgeText + "</span></div>";
     
     // Temperature Row
     html += "<div class='data-row'><span class='data-label'>🌡️ Temperature</span>";
     if (chickenData.temperature > 32.0) html += "<span class='data-value text-red'>" + String(chickenData.temperature, 1) + " &deg;C (Hot)</span></div>";
     else html += "<span class='data-value'>" + String(chickenData.temperature, 1) + " &deg;C</span></div>";
     
     // Air Quality Row
     html += "<div class='data-row'><span class='data-label'>💨 Air Quality</span>";
     if (chickenData.air_quality > 1500) html += "<span class='data-value text-red'>" + String(chickenData.air_quality) + " AQI (Poor)</span></div>";
     else html += "<span class='data-value'>" + String(chickenData.air_quality) + " AQI (Good)</span></div>";
     
     // Fan Status Row
     html += "<div class='data-row'><span class='data-label'>🌀 Exhaust Fan</span>";
     if (chickenData.fan_status) html += "<span class='data-value text-green'>Running</span></div>";
     else html += "<span class='data-value'>Standby</span></div>";

     html += "</div>"; // End Card
  }

  // ================= 🐟 FISH CARD =================
  bool isFishOffline = (millis() - lastFishUpdate > 15000); 
  if (isFishOffline) {
     html += "<div class='card'><div class='card-header'><h3>🐟 Fish Unit</h3><span class='status offline'>Offline</span></div>";
     html += "<div class='offline-msg'>No connection established.</div></div>";
  } else {
     if (fishData.is_critical) mainBuzzerTrigger = true;

     String cardClass = fishData.is_critical ? "card alert-card" : "card";
     String badgeClass = fishData.is_critical ? "status critical" : "status online";
     String badgeText = fishData.is_critical ? "Critical" : "Online";

     html += "<div class='" + cardClass + "'><div class='card-header'><h3>🐟 Fish Pond</h3><span class='" + badgeClass + "'>" + badgeText + "</span></div>";
     
     // Temperature Row
     html += "<div class='data-row'><span class='data-label'>🌡️ Water Temp</span>";
     if (fishData.temperature > 36.0) html += "<span class='data-value text-red'>" + String(fishData.temperature, 1) + " &deg;C (Danger)</span></div>";
     else html += "<span class='data-value'>" + String(fishData.temperature, 1) + " &deg;C</span></div>";
     
     // pH Level Row
     html += "<div class='data-row'><span class='data-label'>💧 pH Level</span>";
     if (fishData.ph_level <= 6.0 || fishData.ph_level >= 7.8) {
         html += "<span class='data-value text-red'>" + String(fishData.ph_level, 2) + " (Imbalanced)</span></div>";
     } else {
         html += "<span class='data-value'>" + String(fishData.ph_level, 2) + " (Optimal)</span></div>";
     }

     html += "</div>"; // End Card
  }

  html += "</div>"; // End Grid Container

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
  digitalWrite(buzzerPin, LOW); 

  // Initialize Preferences (Non-Volatile Memory)
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

  // Set WiFi to Station mode, disable sleep for stability, and connect
  WiFi.mode(WIFI_STA); 
  WiFi.setSleep(false); 
  WiFi.begin(wifi_ssid.c_str(), wifi_password.c_str());
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) { 
    delay(500);
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("\n🌐 Dashboard IP: ");
    Serial.println(WiFi.localIP()); 
    
    // 🌟 Sync the internal clock immediately after Wi-Fi connects
    syncInternalClockWithNTP();
    
  } else {
    Serial.println("\nFailed to connect. Starting Backup AP...");
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP("FARM_BACKUP", "12345678");
  }

  // Print Channel and MAC for debugging
  Serial.print("📶 Channel: "); Serial.println(WiFi.channel());
  Serial.print("🔑 MAIN MAC: "); Serial.println(WiFi.macAddress()); 

  // Initialize ESP-NOW
  if (esp_now_init() != ESP_OK) return;
  esp_now_register_recv_cb((esp_now_recv_cb_t)OnDataRecv);

  // Start HTTP Web Server
  server.on("/", handleRoot);
  server.begin();
}

void loop() {
  server.handleClient(); // Listen for incoming web requests
}
