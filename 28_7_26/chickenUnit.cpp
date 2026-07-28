[env:esp32-s3-devkitc-1-n16r8v]
platform = espressif32
board = esp32-s3-devkitc-1-n16r8v
framework = arduino
lib_deps = adafruit/Adafruit AHTX0@^2.0.6





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
const int MQ135_PIN = 4;       // MQ135 Analog Pin for Air Quality
const int RELAY_PIN = 5;       // Relay pin to control the Exhaust Fan

int espnow_channel;

// 🐔 Chicken Data Structure (without RSSI)
typedef struct chicken_message {
  int unit_id;           // Chicken Unit ID = 2
  float temperature;
  int air_quality;       // MQ135 Analog Value
  bool fan_status;       // Relay ON/OFF status
} chicken_message;

chicken_message chickenData;

// Callback function to check if data was sent successfully
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("\r\nLast Packet Send Status:\t");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success ✅" : "Delivery Fail ❌");
}

void setup() {
  Serial.begin(115200);
  
  pinMode(configButtonPin, INPUT_PULLUP);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW); // Turn off the fan initially

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
  Serial.print("🐔 CHICKEN UNIT MAC ADDRESS: ");
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
  chickenData.unit_id = 2; // Assign ID 2 for Chicken Unit
  
  // -- 1. Read Temperature (AHT10) --
  sensors_event_t humidity, temp;
  if (aht.getEvent(&humidity, &temp)) {
     chickenData.temperature = temp.temperature;
  } else {
     chickenData.temperature = 0.0;
  }

  // -- 2. Read Air Quality (MQ135) --
  chickenData.air_quality = analogRead(MQ135_PIN);

  // -- 3. Check Critical Conditions & Control Relay --
  // If temp > 32°C OR air quality is poor (ADC > 1500)
  if (chickenData.temperature > 32.0 || chickenData.air_quality > 1500) {
    digitalWrite(RELAY_PIN, HIGH); // Turn ON the fan
    chickenData.fan_status = true;
  } else {
    digitalWrite(RELAY_PIN, LOW);  // Turn OFF the fan
    chickenData.fan_status = false;
  }

  // -- 4. Channel Lock & Transmit Data --
  // Ensure the channel has not drifted before sending data
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(espnow_channel, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);

  Serial.printf("🐔 Temp: %.2f C, Air Quality: %d, Fan ON: %d\n", 
                chickenData.temperature, chickenData.air_quality, chickenData.fan_status);
                
  esp_now_send(mainUnitAddress, (uint8_t *) &chickenData, sizeof(chickenData));
  
  delay(5000); // Wait 5 seconds before sending again
}
