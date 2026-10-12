#include <SPI.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h> 
#include <Preferences.h>
#include <Wire.h>
#include <Adafruit_AHTX0.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

// 📡 MAC Address of the Main Unit (Receiver)
// ⚠️ Update this with your actual Main Unit MAC ⚠️
uint8_t mainUnitAddress[] = {0x30, 0xED, 0xA0, 0x2C, 0x7B, 0x44}; 

Adafruit_AHTX0 aht;
Adafruit_MPU6050 mpu;
Preferences preferences;
esp_now_peer_info_t peerInfo;

const int buzzerPin = 18;
const int configButtonPin = 0; 
const int SDA_PIN = 8;         
const int SCL_PIN = 9;         

String main_ssid; 
int espnow_channel;
unsigned long last_send_time = 0; 

// --- Data Structures ---
typedef struct cow_message {
  int unit_id;
  float temperature;
  int activity_state;  
  int rssi_value;      
  bool geofence_alert; 
  bool is_sick;        
  bool is_critical;    
} cow_message;

// 🆕 NEW: Command structure coming FROM Main Unit
typedef struct cow_command_message {
  int target_unit;       
  bool trigger_alarm;    
} cow_command_message;

cow_message cowData; 
cow_command_message incomingCmd; // 🆕 Added

bool dashboard_alarm_triggered = false; // 🆕 State variable to hold Main Unit command

// --- ESP-NOW Callbacks ---
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  // Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");
}

// 🆕 NEW: Receive Callback to catch Main Unit's Alarm Command
void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
  if (len == sizeof(cow_command_message)) {
    memcpy(&incomingCmd, incomingData, sizeof(incomingCmd));
    if (incomingCmd.target_unit == 1) { 
      dashboard_alarm_triggered = incomingCmd.trigger_alarm;
    }
  }
}

void setup() {
  delay(100); 
  
  pinMode(buzzerPin, OUTPUT);
  digitalWrite(buzzerPin, LOW);
  pinMode(configButtonPin, INPUT_PULLUP);
  Serial.begin(115200);
  Serial.println("\n[SYSTEM] Cow Unit Booting...");

  // Startup Beep Sequence
  digitalWrite(buzzerPin, HIGH); delay(100); digitalWrite(buzzerPin, LOW);
  delay(1000); 
  digitalWrite(buzzerPin, HIGH); delay(100); digitalWrite(buzzerPin, LOW);
  delay(100);
  digitalWrite(buzzerPin, HIGH); delay(100); digitalWrite(buzzerPin, LOW);
  
  preferences.begin("farm_config", false);
  main_ssid = preferences.getString("router_ssid", "YOUR_ROUTER_SSID"); 
  espnow_channel = preferences.getInt("main_channel", 1); 

  Serial.println("[CONFIG] Press BOOT Button within 3 seconds to enter Config Mode...");
  delay(3000); 

  if (digitalRead(configButtonPin) == LOW) {
    Serial.println("\n--- CONFIG MODE ACTIVATED ---");
    bool timeoutOccurred = false;

    while (Serial.available() > 0) Serial.read(); 
    Serial.print("Enter Router WiFi SSID (15s timeout): ");
    unsigned long waitStart = millis();
    
    while (Serial.available() == 0) { 
      if (millis() - waitStart > 15000) {
        Serial.println("\n[WARN] Timeout waiting for SSID. Skipping config.");
        timeoutOccurred = true;
        break;
      }
      delay(10); 
    } 

    if (!timeoutOccurred) {
      String newSSID = Serial.readStringUntil('\n');
      newSSID.trim();
      if (newSSID.length() > 0) {
        preferences.putString("router_ssid", newSSID);
        main_ssid = newSSID;
        Serial.printf("Saved SSID: %s\n", main_ssid.c_str());
      }

      while (Serial.available() > 0) Serial.read(); 
      Serial.print("Enter Main Unit WiFi Channel (1-13): ");
      waitStart = millis();
      while (Serial.available() == 0) { 
        if (millis() - waitStart > 15000) break;
        delay(10); 
      } 
      
      if (Serial.available() > 0) {
        String channelInput = Serial.readStringUntil('\n');
        channelInput.trim();
        int newChannel = channelInput.toInt();
        if (newChannel > 0 && newChannel <= 13) {
          preferences.putInt("main_channel", newChannel);
          espnow_channel = newChannel;
          Serial.printf("Saved Channel: %d\n", espnow_channel);
        }
      }
    }
  }

  // ---------------------------------------------------------
  // SENSOR & WIFI INITIALIZATION
  // ---------------------------------------------------------
  Wire.begin(SDA_PIN, SCL_PIN);
  if (!aht.begin(&Wire)) Serial.println("[ERROR] AHT10 Init Failed");
  if (mpu.begin(0x68, &Wire)) {
    mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
    mpu.setGyroRange(MPU6050_RANGE_500_DEG);
    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
  } else {
    Serial.println("[ERROR] MPU6050 Init Failed");
  }

  WiFi.mode(WIFI_STA);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(espnow_channel, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);
  
  if (esp_now_init() != ESP_OK) Serial.println("[ERROR] ESP-NOW Init Failed");
  
  esp_now_register_send_cb((esp_now_send_cb_t)OnDataSent);
  esp_now_register_recv_cb((esp_now_recv_cb_t)OnDataRecv); // 🆕 Catch incoming commands
  
  memset(&peerInfo, 0, sizeof(peerInfo)); 
  memcpy(peerInfo.peer_addr, mainUnitAddress, 6);
  peerInfo.channel = espnow_channel;  
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);

  // Ready Beep
  digitalWrite(buzzerPin, HIGH); delay(1000); digitalWrite(buzzerPin, LOW);
}

void loop() {
  unsigned long currentMillis = millis();

  // ၅ စက္ကန့်ပြည့်တိုင်း Data ပို့မည်
  if (currentMillis - last_send_time >= 5000) {
    last_send_time = currentMillis;

    cowData.unit_id = 1; 
    
    // -- 1. Temperature Sensing --
    sensors_event_t humidity, temp;
    if (aht.getEvent(&humidity, &temp)) {
       cowData.temperature = temp.temperature;
    } else {
       cowData.temperature = 0.0;
    }
    cowData.is_sick = (cowData.temperature > 38.5);

    // -- 2. Geofencing (RSSI) --
    int n = WiFi.scanNetworks(false, false, false, 100, espnow_channel);
    bool found = false;
    cowData.rssi_value = -100; 
    
    for (int i = 0; i < n; ++i) {
      if (WiFi.SSID(i) == main_ssid) { 
        cowData.rssi_value = WiFi.RSSI(i);
        found = true;
        break;
      }
    }
    WiFi.scanDelete(); 
    
    // -- 3. Activity Tracking (MPU6050) --
    sensors_event_t a, g, mpu_temp;
    if (mpu.getEvent(&a, &g, &mpu_temp)) {
      if (abs(a.acceleration.x) > 15.0 || abs(a.acceleration.y) > 15.0 || abs(a.acceleration.z) > 20.0) {
        cowData.activity_state = 2; // Fighting/Running
      } else if (abs(a.acceleration.x) > 5.0 || abs(a.acceleration.y) > 5.0) {
        cowData.activity_state = 1; // Active/Walking
      } else {
        cowData.activity_state = 0; // Idle/Resting
      }
    }

    // UPDATE: Cow Unit no longer decides the alarm condition. It relies on the Main Unit.
    cowData.geofence_alert = dashboard_alarm_triggered;
    cowData.is_critical = (cowData.is_sick || cowData.activity_state == 2 || cowData.geofence_alert);

    // -- 4. Send Data --
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(espnow_channel, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);

    Serial.printf("[TX] Temp: %.1fC | Act: %d | RSSI: %d dBm | GeoAlert: %d | Fever: %d\n", 
                  cowData.temperature, cowData.activity_state, cowData.rssi_value, cowData.geofence_alert, cowData.is_sick);
                  
    esp_now_send(mainUnitAddress, (uint8_t *) &cowData, sizeof(cowData));
    delay(100); 

    // ---------------------------------------------------------
    // 5. TWO-WAY BUZZER LOGIC 
    // ---------------------------------------------------------
    if (!found) {
      // 🚨 Failsafe: If network is completely lost, buzz immediately
      Serial.println("[ALARM] Connection Lost! Buzzing...");
      for(int i = 0; i < 3; i++) {
        tone(buzzerPin, 2000); delay(300); noTone(buzzerPin); delay(300); 
      }
    } 
    else if (dashboard_alarm_triggered) {
      // 🚨 Main Unit sent the command to trigger alarm based on Control Panel Zones
      Serial.println("[ALARM] Dashboard Zone Breach! Buzzing...");
      for(int i = 0; i < 3; i++) {
        tone(buzzerPin, 2000); delay(300); noTone(buzzerPin); delay(300); 
      }
    } 
    else {
      Serial.println("[STATUS] OK. Waiting for next reading...");
    }
  }
}