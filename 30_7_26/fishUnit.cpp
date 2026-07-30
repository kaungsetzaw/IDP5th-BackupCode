// [env:esp32-s3-devkitc-1-n16r8v]
// platform = espressif32@6.5.0 
// board = esp32-s3-devkitc-1-n16r8v
// framework = arduino


// lib_deps = 
//     Wire
//     SPI
//     adafruit/Adafruit AHTX0@^2.0.5
//     adafruit/Adafruit Unified Sensor@^1.1.14




#include <Arduino.h>

#include <SPI.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h> 
#include <Preferences.h>
#include <Wire.h>
#include <Adafruit_AHTX0.h>
#include <Adafruit_Sensor.h>

// 📡 Main Unit MAC Address
uint8_t mainUnitAddress[] = {0x30, 0xED, 0xA0, 0x2C, 0x7B, 0x44}; 

Adafruit_AHTX0 aht;
Preferences preferences;
esp_now_peer_info_t peerInfo;

// Pins
const int configButtonPin = 0; 
const int SDA_PIN = 8;         
const int SCL_PIN = 9;         
const int PH_SENSOR_PIN = 4;   // Analog Pin for pH Sensor

int espnow_channel;

// 🐟 Fish Data Structure
typedef struct fish_message {
  int unit_id;           // Fish Unit ID = 3
  float temperature;
  float ph_level;
  bool is_critical;      // Pre-classified alert flag
} fish_message;

fish_message fishData;

// Callback function to check if data was sent successfully
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("\r\nLast Packet Send Status:\t");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success ✅" : "Delivery Fail ❌");
}

// Function to convert analog reading to pH value (Needs calibration based on your specific sensor module)
float get_pH_Level(int analogValue) {
  float voltage = analogValue * (3.3 / 4095.0);
  // Basic standard conversion. You may need to adjust the multiplier/offset for your exact sensor.
  float phValue = 3.5 * voltage; 
  return phValue;
}

void setup() {
  Serial.begin(115200);
  
  pinMode(configButtonPin, INPUT_PULLUP);

  Wire.begin(SDA_PIN, SCL_PIN);
  if (!aht.begin(&Wire)) {
    Serial.println("Could not find AHT10? Check wiring");
  }

  // Load configuration from non-volatile memory
  preferences.begin("farm_config", false);
  espnow_channel = preferences.getInt("main_channel", 1); 

  Serial.println("\nPress BOOT Button within 3 seconds to enter Config Mode...");
  delay(3000);

  // Config mode to change Main Unit's WiFi channel via Serial Monitor
  if (digitalRead(configButtonPin) == LOW) {
    Serial.println("\n>>> CONFIG MODE ACTIVATED <<<");

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

  Serial.println("\n=================================");
  Serial.print("🐟 FISH UNIT MAC ADDRESS: ");
  Serial.println(WiFi.macAddress()); 
  Serial.println("=================================\n");

  // Set ESP32 to Station Mode and lock the channel
  WiFi.mode(WIFI_STA);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(espnow_channel, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);
  
  // Initialize ESP-NOW
  if (esp_now_init() != ESP_OK) return;
  esp_now_register_send_cb((esp_now_send_cb_t)OnDataSent);
  
  // Register peer (Main Unit)
  memset(&peerInfo, 0, sizeof(peerInfo)); 
  memcpy(peerInfo.peer_addr, mainUnitAddress, 6);
  peerInfo.channel = espnow_channel;  
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);
}

void loop() {
  fishData.unit_id = 3; // Assign ID 3 for Fish Unit
  
  // -- 1. Read Temperature (AHT10) --
  sensors_event_t humidity, temp;
  if (aht.getEvent(&humidity, &temp)) {
     fishData.temperature = temp.temperature;
  } else {
     fishData.temperature = 0.0;
  }

  // -- 2. Read pH Level --
  int phAnalogValue = analogRead(PH_SENSOR_PIN);
  fishData.ph_level = get_pH_Level(phAnalogValue);

  // -- 3. Classification (Data processed on Node to save Main Unit power) --
  // Critical if Temp > 36°C OR pH <= 6.0 OR pH >= 7.8
  if (fishData.temperature > 36.0 || fishData.ph_level <= 6.0 || fishData.ph_level >= 7.8) {
    fishData.is_critical = true;
  } else {
    fishData.is_critical = false;
  }

  // -- 4. Channel Lock & Transmit Data --
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(espnow_channel, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);

  Serial.printf("🐟 Temp: %.2f C, pH Level: %.2f, Critical Alert: %d\n", 
                fishData.temperature, fishData.ph_level, fishData.is_critical);
                
  esp_now_send(mainUnitAddress, (uint8_t *) &fishData, sizeof(fishData));
  
  delay(5000); // Wait 5 seconds before sending again
}
