// for platformio.ini
//   [env:4d_systems_esp32s3_gen4_r8n16]
// platform = espressif32@6.5.0      ; 🌟 Version 6.5.0 (Core 2.x) ကို အသေမှတ်ပါမည်
// board = 4d_systems_esp32s3_gen4_r8n16
// framework = arduino
// monitor_speed = 115200
// lib_ldf_mode = deep+

// lib_deps = 
//     Wire
//     SPI
//     adafruit/RTClib@^2.1.4


#include <SPI.h>
#include <WiFi.h>
#include <esp_now.h>
#include <WebServer.h>
#include <Preferences.h>
#include <Wire.h>     
#include <RTClib.h>   

RTC_DS3231 rtc;
WebServer server(80);
Preferences preferences;

const int configButtonPin = 0; 
const int buzzerPin = 18;      
const int SDA_PIN = 8;
const int SCL_PIN = 9;

String wifi_ssid;
String wifi_password;

// 🐄 Cow Unit ၏ MAC Address အမှန်
uint8_t cowMac[] = {0x1C, 0xDB, 0xD4, 0x76, 0x67, 0xD4}; 

typedef struct struct_message {
  int unit_id;
  float temperature;
  int activity_state;
  int rssi_value;
  bool geofence_alert;
} struct_message;

struct_message cowData;
unsigned long lastCowUpdate = 0; 
String lastUpdateTimeStr = "Waiting for data..."; 

void OnDataRecv(const uint8_t *mac_addr, const uint8_t *incomingData, int len) {
  if (len != sizeof(struct_message)) return; 

  if (memcmp(mac_addr, cowMac, 6) == 0) {
    memcpy(&cowData, incomingData, sizeof(cowData));
    lastCowUpdate = millis(); 
    
    DateTime now = rtc.now();
    char timeBuffer[30];
    sprintf(timeBuffer, "%04d-%02d-%02d %02d:%02d:%02d", now.year(), now.month(), now.day(), now.hour(), now.minute(), now.second());
    lastUpdateTimeStr = String(timeBuffer);
  } 
}

void handleRoot() {
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta http-equiv='refresh' content='5'>"; 
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<style>body{font-family: sans-serif; margin: 20px; background-color: #f4f4f9;} ";
  html += ".card{border: 1px solid #ddd; padding: 15px; border-radius: 10px; margin-bottom: 15px; background: white; box-shadow: 2px 2px 5px rgba(0,0,0,0.1);} ";
  html += ".alert{color: white; background-color: #e53935; border-color: #b71c1c;} ";
  html += ".offline{color: #555; background-color: #e0e0e0;}</style></head><body>";
  
  html += "<h2 style='color: #2c3e50;'>🌾 Smart Animal Farm Dashboard</h2>";
  
  bool isCowOffline = (millis() - lastCowUpdate > 15000); 
  
  if (isCowOffline) {
     html += "<div class='card offline'><h3>🐄 Cow Unit - <span style='color:red;'>OFFLINE</span></h3>";
     html += "<p>No data received recently. Please check the unit.</p>";
     html += "<p>🕒 Last Seen: <b>" + lastUpdateTimeStr + "</b></p></div>"; 
     digitalWrite(buzzerPin, LOW);
  } else {
     bool isSick = (cowData.temperature > 38.5);
     bool isCritical = (cowData.activity_state == 2 || cowData.geofence_alert || isSick);
     String alertClass = isCritical ? "alert" : "";
     
     html += "<div class='card " + alertClass + "'><h3>🐄 Cow Unit - <span>ONLINE</span></h3>";
     html += "<p style='color: #666;'>🕒 Time: <b>" + lastUpdateTimeStr + "</b></p>";
     html += "<hr style='border: 0; border-top: 1px solid #eee;'>";
     
     if (isSick) {
       html += "<p style='font-size: 18px;'>🌡️ Temperature: <b>" + String(cowData.temperature) + " &deg;C <span style='color: yellow;'> (🚨 Cow is sick!)</span></b></p>";
     } else {
       html += "<p>🌡️ Temperature: <b>" + String(cowData.temperature) + " &deg;C</b></p>";
     }

     html += "<p>📶 RSSI (Signal): <b>" + String(cowData.rssi_value) + " dBm</b></p>";
     
     String activityText = "Idle (Sleeping/Resting)";
     if (cowData.activity_state == 1) activityText = "Active (Walking/Eating)";
     else if (cowData.activity_state == 2) activityText = "⚠️ CRITICAL (Running/Fighting)";
     
     html += "<p>🏃 Activity: <b>" + activityText + "</b></p>";
     
     if (cowData.geofence_alert) {
       html += "<p style='font-size: 18px;'><strong>🚨 OUT OF BOUNDS (Geofence Alert) 🚨</strong></p>";
     }
     html += "</div>";
     
     if (isCritical) {
         digitalWrite(buzzerPin, HIGH);
     } else {
         digitalWrite(buzzerPin, LOW);
     }
  }

  html += "</body></html>";
  server.send(200, "text/html", html);
}

void setup() {
  Serial.begin(115200);
  pinMode(configButtonPin, INPUT_PULLUP);
  pinMode(buzzerPin, OUTPUT);
  digitalWrite(buzzerPin, LOW);
  
  Wire.begin(SDA_PIN, SCL_PIN);
  if (!rtc.begin(&Wire)) {
    Serial.println("Couldn't find RTC");
  } else {
    if (rtc.lostPower()) {
      rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }
  }
  
  preferences.begin("farm_config", false);
  wifi_ssid = preferences.getString("wifi_ssid", "YOUR_ROUTER_SSID"); 
  wifi_password = preferences.getString("wifi_pass", "YOUR_ROUTER_PASS"); 

  Serial.println("\nPress BOOT Button within 3 seconds to change Router SSID & Password...");
  delay(3000);

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

  WiFi.mode(WIFI_STA);
  // 🆕 အရေးကြီးဆုံး ပြင်ဆင်ချက် - Main Unit ကို Power Save Mode ပိတ်ပြီး အမြဲနားစွင့်ခိုင်းထားမည်
  WiFi.setSleep(false); 

  WiFi.begin(wifi_ssid.c_str(), wifi_password.c_str());
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) { 
    delay(500);
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("🌐 Dashboard IP Address: ");
    Serial.println(WiFi.localIP()); 
  } else {
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP("FARM_BACKUP", "12345678");
  }

  Serial.println("\n=================================");
  Serial.print("📶 Main Unit WiFi Channel: ");
  Serial.println(WiFi.channel());
  Serial.print("🔑 MAIN UNIT MAC ADDRESS: ");
  Serial.println(WiFi.macAddress()); 
  Serial.println("=================================\n");

  esp_now_init();
  esp_now_register_recv_cb((esp_now_recv_cb_t)OnDataRecv);

  server.on("/", handleRoot);
  server.begin();
}

void loop() {
  server.handleClient();
}
