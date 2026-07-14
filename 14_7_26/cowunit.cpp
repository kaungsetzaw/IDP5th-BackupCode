// for platformIo.ini
//   [env:4d_systems_esp32s3_gen4_r8n16]
// platform = espressif32@6.5.0
// board = 4d_systems_esp32s3_gen4_r8n16
// framework = arduino
// monitor_speed = 115200
// lib_ldf_mode = deep+

// lib_deps = 
//     Wire
//     SPI
//     adafruit/Adafruit AHTX0@^2.0.5
//     adafruit/Adafruit MPU6050@^2.2.6
//     adafruit/Adafruit Unified Sensor@^1.1.14


#include <SPI.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h> 
#include <Preferences.h>
#include <Wire.h>
#include <Adafruit_AHTX0.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

// 📡 Main Unit ၏ MAC Address 
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

typedef struct struct_message {
  int unit_id;
  float temperature;
  int activity_state;
  int rssi_value;
  bool geofence_alert;
} struct_message;

struct_message cowData;

void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("\r\nLast Packet Send Status:\t");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success ✅" : "Delivery Fail ❌");
}

void setup() {
  Serial.begin(115200);
  
  pinMode(buzzerPin, OUTPUT);
  pinMode(configButtonPin, INPUT_PULLUP);
  digitalWrite(buzzerPin, LOW);

  Wire.begin(SDA_PIN, SCL_PIN);
  if (!aht.begin(&Wire)) Serial.println("Could not find AHT10?");
  
  if (mpu.begin(0x68, &Wire)) {
    mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
    mpu.setGyroRange(MPU6050_RANGE_500_DEG);
    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
  }

  preferences.begin("farm_config", false);
  main_ssid = preferences.getString("router_ssid", "YOUR_ROUTER_SSID"); 
  espnow_channel = preferences.getInt("main_channel", 1); 

  Serial.println("\nPress BOOT Button within 3 seconds to enter Config Mode...");
  delay(3000);

  if (digitalRead(configButtonPin) == LOW) {
    Serial.println("\n>>> CONFIG MODE ACTIVATED <<<");

    while (Serial.available() > 0) Serial.read(); 
    Serial.print("Enter Router WiFi SSID (for Geofencing): ");
    while (Serial.available() == 0) { delay(10); } 
    String newSSID = Serial.readStringUntil('\n');
    newSSID.trim();
    if (newSSID.length() > 0) {
      preferences.putString("router_ssid", newSSID);
      main_ssid = newSSID;
    }

    while (Serial.available() > 0) Serial.read(); 
    Serial.print("Enter Main Unit WiFi Channel (e.g., 1 to 13): ");
    while (Serial.available() == 0) { delay(10); } 
    String channelInput = Serial.readStringUntil('\n');
    channelInput.trim();
    int newChannel = channelInput.toInt();
    if (newChannel > 0 && newChannel <= 13) {
      preferences.putInt("main_channel", newChannel);
      espnow_channel = newChannel;
    }
  }

  WiFi.mode(WIFI_STA);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(espnow_channel, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);
  
  esp_now_init();
  esp_now_register_send_cb((esp_now_send_cb_t)OnDataSent);
  
  memset(&peerInfo, 0, sizeof(peerInfo)); // 🆕 Data အဟောင်းများရှင်းလင်းခြင်း
  memcpy(peerInfo.peer_addr, mainUnitAddress, 6);
  peerInfo.channel = espnow_channel;  
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);
}

void loop() {
  cowData.unit_id = 1; 
  bool triggerBuzzer = false; 
  
  // -- 1. Temperature --
  sensors_event_t humidity, temp;
  if (aht.getEvent(&humidity, &temp)) {
     cowData.temperature = temp.temperature;
  } else {
     cowData.temperature = 0.0;
  }

  if (cowData.temperature > 38.5) {
    triggerBuzzer = true;
  }

  // -- 2. Geofencing (RSSI) 🆕 သတ်မှတ်ထားသော Channel ကိုသာ အမြန် Scan ဖတ်မည် --
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
  WiFi.scanDelete(); // 🆕 Memory မပြည့်အောင် Scan Data များကို ရှင်းလင်းမည်
  
  if (!found || cowData.rssi_value < -85) {
    cowData.geofence_alert = true;
    triggerBuzzer = true; 
  } else {
    cowData.geofence_alert = false;
  }

  // -- 3. Activity (MPU6050) --
  sensors_event_t a, g, mpu_temp;
  if (mpu.getEvent(&a, &g, &mpu_temp)) {
    if (abs(a.acceleration.x) > 15.0 || abs(a.acceleration.y) > 15.0 || abs(a.acceleration.z) > 20.0) {
      cowData.activity_state = 2; 
    } else if (abs(a.acceleration.x) > 5.0 || abs(a.acceleration.y) > 5.0) {
      cowData.activity_state = 1; 
    } else {
      cowData.activity_state = 0; 
    }
  }

  if (triggerBuzzer) {
    digitalWrite(buzzerPin, HIGH);
  } else {
    digitalWrite(buzzerPin, LOW);
  }

  // 🆕 Data မပို့မီ Channel မလွဲစေရန် ထပ်မံအတည်ပြုခြင်း
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(espnow_channel, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);

  // -- 4. Send Data --
  Serial.printf("Temp: %.2f C, Activity: %d, RSSI: %d, Alert: %d\n", 
                cowData.temperature, cowData.activity_state, cowData.rssi_value, cowData.geofence_alert);
  esp_now_send(mainUnitAddress, (uint8_t *) &cowData, sizeof(cowData));

  delay(500); 
}
