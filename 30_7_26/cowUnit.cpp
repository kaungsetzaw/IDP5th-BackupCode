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

// 📡 MAC Address of the Main Unit (Receiver)
uint8_t mainUnitAddress[] = {0x30, 0xED, 0xA0, 0x2C, 0x7B, 0x44}; 

// Sensor and Preference objects
Adafruit_AHTX0 aht;
Adafruit_MPU6050 mpu;
Preferences preferences;
esp_now_peer_info_t peerInfo;

// Hardware Pin Definitions
const int buzzerPin = 18;
const int configButtonPin = 0; // Usually the BOOT button on ESP32 boards
const int SDA_PIN = 8;         // I2C SDA pin
const int SCL_PIN = 9;         // I2C SCL pin

// Configuration Variables
String main_ssid; 
int espnow_channel;

// Data structure to hold sensor readings and status
// Must match the receiver's structure exactly
typedef struct struct_message {
  int unit_id;
  float temperature;
  int activity_state;  // 0 = Resting, 1 = Normal, 2 = High Activity
  int rssi_value;      // WiFi signal strength for geofencing
  bool geofence_alert; // True if outside designated area
} struct_message;

struct_message cowData;

// Callback function executed when data is sent via ESP-NOW
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("\r\nLast Packet Send Status:\t");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success ✅" : "Delivery Fail ❌");
}

void setup() {
  Serial.begin(115200);
  
  // Initialize GPIO pins
  pinMode(buzzerPin, OUTPUT);
  pinMode(configButtonPin, INPUT_PULLUP);
  digitalWrite(buzzerPin, LOW);

  // Initialize I2C communication
  Wire.begin(SDA_PIN, SCL_PIN);
  
  // Initialize AHT10 Temperature & Humidity Sensor
  if (!aht.begin(&Wire)) Serial.println("Could not find AHT10?");
  
  // Initialize MPU6050 Accelerometer & Gyroscope
  if (mpu.begin(0x68, &Wire)) {
    mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
    mpu.setGyroRange(MPU6050_RANGE_500_DEG);
    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
  }

  // Initialize non-volatile storage (Preferences) to read saved configurations
  preferences.begin("farm_config", false);
  main_ssid = preferences.getString("router_ssid", "YOUR_ROUTER_SSID"); 
  espnow_channel = preferences.getInt("main_channel", 1); 

  // Provide a 3-second window to enter Configuration Mode via serial monitor
  Serial.println("\nPress BOOT Button within 3 seconds to enter Config Mode...");
  delay(3000);

  // Check if BOOT button was pressed
  if (digitalRead(configButtonPin) == LOW) {
    Serial.println("\n>>> CONFIG MODE ACTIVATED <<<");

    // Clear serial buffer
    while (Serial.available() > 0) Serial.read(); 
    
    // Get new Router SSID
    Serial.print("Enter Router WiFi SSID (for Geofencing): ");
    while (Serial.available() == 0) { delay(10); } 
    String newSSID = Serial.readStringUntil('\n');
    newSSID.trim();
    if (newSSID.length() > 0) {
      preferences.putString("router_ssid", newSSID);
      main_ssid = newSSID;
    }

    // Clear serial buffer again
    while (Serial.available() > 0) Serial.read(); 
    
    // Get new ESP-NOW channel
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

  // Set WiFi to Station mode to use ESP-NOW
  WiFi.mode(WIFI_STA);
  
  // Force WiFi to operate on the specified channel
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(espnow_channel, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);
  
  // Initialize ESP-NOW
  esp_now_init();
  esp_now_register_send_cb((esp_now_send_cb_t)OnDataSent);
  
  // 🆕 Clear old data in the peerInfo structure
  memset(&peerInfo, 0, sizeof(peerInfo)); 
  
  // Register the Main Unit as a peer
  memcpy(peerInfo.peer_addr, mainUnitAddress, 6);
  peerInfo.channel = espnow_channel;  
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);
}

void loop() {
  cowData.unit_id = 1; 
  bool triggerBuzzer = false; 
  
  // -- 1. Temperature Sensing --
  sensors_event_t humidity, temp;
  if (aht.getEvent(&humidity, &temp)) {
     cowData.temperature = temp.temperature;
  } else {
     cowData.temperature = 0.0;
  }

  // Trigger alarm if temperature exceeds threshold (e.g., fever detection)
  if (cowData.temperature > 38.5) {
    triggerBuzzer = true;
  }

  // -- 2. Geofencing (RSSI) --
  // 🆕 Fast scan specifically on the assigned channel to minimize loop delay
  int n = WiFi.scanNetworks(false, false, false, 100, espnow_channel);
  bool found = false;
  cowData.rssi_value = -100; // Default to lowest signal
  
  for (int i = 0; i < n; ++i) {
    if (WiFi.SSID(i) == main_ssid) { 
      cowData.rssi_value = WiFi.RSSI(i);
      found = true;
      break;
    }
  }
  
  // 🆕 Clear scan results from memory to prevent memory leaks
  WiFi.scanDelete(); 
  
  // Trigger alert if the router is out of range or the signal is too weak
  if (!found || cowData.rssi_value < -85) {
    cowData.geofence_alert = true;
    triggerBuzzer = true; 
  } else {
    cowData.geofence_alert = false;
  }

  // -- 3. Activity Tracking (MPU6050) --
  sensors_event_t a, g, mpu_temp;
  if (mpu.getEvent(&a, &g, &mpu_temp)) {
    // High acceleration detected (e.g., running, distress)
    if (abs(a.acceleration.x) > 15.0 || abs(a.acceleration.y) > 15.0 || abs(a.acceleration.z) > 20.0) {
      cowData.activity_state = 2; 
    } 
    // Moderate acceleration detected (e.g., walking, grazing)
    else if (abs(a.acceleration.x) > 5.0 || abs(a.acceleration.y) > 5.0) {
      cowData.activity_state = 1; 
    } 
    // Resting
    else {
      cowData.activity_state = 0; 
    }
  }

  // Activate buzzer if any alarm conditions are met
  if (triggerBuzzer) {
    digitalWrite(buzzerPin, HIGH);
  } else {
    digitalWrite(buzzerPin, LOW);
  }

  // 🆕 Ensure the WiFi channel is strictly set before sending ESP-NOW data. 
  // WiFi scans occasionally shift the channel, which causes ESP-NOW packets to drop.
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(espnow_channel, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);

  // -- 4. Send Data --
  Serial.printf("Temp: %.2f C, Activity: %d, RSSI: %d, Alert: %d\n", 
                cowData.temperature, cowData.activity_state, cowData.rssi_value, cowData.geofence_alert);
                
  esp_now_send(mainUnitAddress, (uint8_t *) &cowData, sizeof(cowData));

  delay(500); // Brief wait before starting the next reading cycle
}
