#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h> 
#include <Preferences.h>
#include <Wire.h>
#include <Adafruit_AHTX0.h>
#include <Adafruit_Sensor.h>
#include <ESP32Servo.h> 

// --- Configuration & Constants ---
// ⚠️ Main Unit ၏ MAC Address အမှန်ကို ပြင်ဆင်ပါ ⚠️
uint8_t mainUnitAddress[] = {0x30, 0xED, 0xA0, 0x2C, 0x7B, 0x44}; 

Adafruit_AHTX0 aht;
Preferences preferences;
esp_now_peer_info_t peerInfo;
Servo feederServo;

// --- Pin Definitions ---
const int CONFIG_BTN_PIN = 0;  
const int MQ135_PIN = 4;       
const int RELAY_PIN = 5;       
const int SERVO_PIN = 6;       
const int MOTOR_RELAY_PIN = 7; 
const int SDA_PIN = 8;         
const int SCL_PIN = 9;         
const int WATER_LEVEL_PIN = 10;

// FIXED: Hardcode the Channel to match Main Unit (11)
int espnow_channel = 11; 

// --- Data Structures (MUST MATCH MAIN UNIT) ---
typedef struct chicken_message {
  int unit_id;           // Assigned ID: 2
  float temperature;
  int air_quality;       
  bool fan_status;       
  bool is_hot;           
  bool is_poor_air;      
  bool is_critical;      
  bool motor_status;     
  int water_level;       
} chicken_message;

typedef struct command_message {
  int target_unit;       
  int fan_override;      
  bool trigger_feeder;   
  int motor_override;    
  bool trigger_pump;     
} command_message;

typedef struct ack_message {
  int identifier;        
  int target_unit;       
  bool ack_success;
  int confirmed_fan_mode;
  bool feeding_triggered;
  int confirmed_motor_mode; 
} ack_message;

chicken_message chickenData;
command_message incomingCmd;
ack_message chickenAck;

// --- State & Timing Variables ---
int current_fan_mode = 0; 
int current_motor_mode = 0; 

bool is_feeding = false;
unsigned long feed_start_time = 0;

bool is_pumping = false;
unsigned long pump_start_time = 0;

unsigned long last_send_time = 0; 
bool send_ack_flag = false; 

// --- 🆕 DEBUG: ESP-NOW Callbacks ---
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("[ESP-NOW TX] Status: ");
  if (status == ESP_NOW_SEND_SUCCESS) {
    Serial.println("✅ SUCCESS (Main Unit received the data)");
  } else {
    Serial.println("❌ FAIL (Main Unit didn't receive it. Wrong MAC or Channel?)");
  }
}

void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
  if (len == sizeof(command_message)) {
    memcpy(&incomingCmd, incomingData, sizeof(incomingCmd));
    
    if (incomingCmd.target_unit == 2) { 
      Serial.println("\n📥 [COMMAND RECEIVED FROM MAIN UNIT] ---");
      Serial.printf("Fan Override: %d | Motor Override: %d\n", incomingCmd.fan_override, incomingCmd.motor_override);
      
      current_fan_mode = incomingCmd.fan_override;
      current_motor_mode = incomingCmd.motor_override; 
      
      if (incomingCmd.trigger_feeder && !is_feeding) {
        Serial.println(">>> ACTION: FEEDING START! (180 Deg) <<<");
        is_feeding = true;
        feed_start_time = millis();
        feederServo.write(180); 
      }
      
      if (incomingCmd.trigger_pump && !is_pumping && current_motor_mode == 0) {
        Serial.println(">>> ACTION: AUTO PUMP TRIGGERED! (5 Seconds) <<<");
        is_pumping = true;
        pump_start_time = millis();
        digitalWrite(MOTOR_RELAY_PIN, HIGH); 
        chickenData.motor_status = true;
      }
      Serial.println("-----------------------------------------\n");
      send_ack_flag = true; 
    }
  }
}

// --- Setup ---
void setup() {
  Serial.begin(115200);
  delay(1000); // Give Serial Monitor time to open
  Serial.println("\n\n🐔 --- CHICKEN UNIT BOOTING --- 🐔");
  
  pinMode(CONFIG_BTN_PIN, INPUT_PULLUP);
  
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW); 

  pinMode(MOTOR_RELAY_PIN, OUTPUT);
  digitalWrite(MOTOR_RELAY_PIN, LOW); 

  analogReadResolution(12);
  analogSetAttenuation(ADC_11db); 

  Serial.println("[Init] Attaching Servo...");
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);
  feederServo.setPeriodHertz(50);
  feederServo.attach(SERVO_PIN, 500, 2400); 
  feederServo.write(90); 

  Serial.println("[Init] Connecting to AHT10 Sensor...");
  Wire.begin(SDA_PIN, SCL_PIN);
  if (!aht.begin(&Wire)) {
    Serial.println("❌ ERROR: AHT10 Init Failed! Check Pins 8/9.");
  } else {
    Serial.println("✅ AHT10 Found!");
  }

  preferences.begin("farm_config", false);
  espnow_channel = preferences.getInt("main_channel", 11); 

  Serial.println("\n[Init] Configuring WiFi & ESP-NOW...");
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(); 

  // ---------------------------------------------------------
  // 🆕 DEBUG: Explicitly Print MAC Information for user to verify
  // ---------------------------------------------------------
  Serial.println("\n==========================================");
  Serial.printf("🔑 MY MAC ADDRESS: %s\n", WiFi.macAddress().c_str());
  Serial.printf("🎯 TARGET MAIN MAC: %02X:%02X:%02X:%02X:%02X:%02X\n", 
                mainUnitAddress[0], mainUnitAddress[1], mainUnitAddress[2], 
                mainUnitAddress[3], mainUnitAddress[4], mainUnitAddress[5]);
  Serial.printf("📶 WiFi Channel: %d\n", espnow_channel);
  Serial.println("==========================================\n");

  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(espnow_channel, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);
  
  if (esp_now_init() != ESP_OK) {
    Serial.println("❌ ERROR: ESP-NOW Init Failed!");
    return;
  }
  
  esp_now_register_send_cb((esp_now_send_cb_t)OnDataSent);
  esp_now_register_recv_cb((esp_now_recv_cb_t)OnDataRecv); 
  
  memset(&peerInfo, 0, sizeof(peerInfo)); 
  memcpy(peerInfo.peer_addr, mainUnitAddress, 6);
  peerInfo.channel = espnow_channel;  
  peerInfo.encrypt = false;
  
  if (esp_now_add_peer(&peerInfo) != ESP_OK){
    Serial.println("❌ ERROR: Failed to add peer");
  } else {
    Serial.println("✅ ESP-NOW Peer Added. System Ready.\n");
  }
}

// --- Main Loop ---
void loop() {
  unsigned long currentMillis = millis();

  // Task 1: Sensor Reading & ESP-NOW Tx (Every 5s)
  if (currentMillis - last_send_time >= 5000) {
    last_send_time = currentMillis;
    chickenData.unit_id = 2; 

    Serial.println("\n--- [READING SENSORS] ---");

    sensors_event_t humidity, temp;
    if (aht.getEvent(&humidity, &temp)) {
      chickenData.temperature = temp.temperature;
      Serial.printf("Temp: %.1f C\n", temp.temperature);
    } else {
      chickenData.temperature = 0.0;
    }

    chickenData.air_quality = analogRead(MQ135_PIN);
    Serial.printf("Air Quality: %d\n", chickenData.air_quality);
    
    int rawWater = analogRead(WATER_LEVEL_PIN); 
    chickenData.water_level = map(rawWater, 0, 4095, 0, 100); 
    if(chickenData.water_level < 0) chickenData.water_level = 0;
    if(chickenData.water_level > 100) chickenData.water_level = 100;
    
    Serial.printf("Water Level: %d%% (Raw: %d)\n", chickenData.water_level, rawWater);

    chickenData.is_hot = (chickenData.temperature > 35.0); 
    chickenData.is_poor_air = (chickenData.air_quality > 1500);
    chickenData.is_critical = (chickenData.is_hot || chickenData.is_poor_air);

    // ⚙️ Fan Control Logic
    if (current_fan_mode == 1) { 
      digitalWrite(RELAY_PIN, HIGH);
      chickenData.fan_status = true;
    } else if (current_fan_mode == 2) { 
      digitalWrite(RELAY_PIN, LOW);
      chickenData.fan_status = false;
    } else { 
      if (chickenData.is_critical) {
        digitalWrite(RELAY_PIN, HIGH);
        chickenData.fan_status = true;
      } else {
        digitalWrite(RELAY_PIN, LOW);
        chickenData.fan_status = false;
      }
    }

    // ⚙️ Motor Control Logic
    if (current_motor_mode == 1) { 
      digitalWrite(MOTOR_RELAY_PIN, HIGH);
      chickenData.motor_status = true;
      is_pumping = false; 
    } else if (current_motor_mode == 2) { 
      digitalWrite(MOTOR_RELAY_PIN, LOW);
      chickenData.motor_status = false;
      is_pumping = false; 
    } else { 
      if (!is_pumping) {
         digitalWrite(MOTOR_RELAY_PIN, LOW);
         chickenData.motor_status = false;
      }
    }

    // Transmit Telemetry
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(espnow_channel, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);
    
    Serial.println("🚀 Sending Data to Main Unit...");
    esp_now_send(mainUnitAddress, (uint8_t *) &chickenData, sizeof(chickenData));
  }

  // Task 2: Transmit Acknowledgment (ACK)
  if (send_ack_flag) {
    send_ack_flag = false; 
    
    chickenAck.identifier = 999;
    chickenAck.target_unit = 2;
    chickenAck.ack_success = true;
    chickenAck.confirmed_fan_mode = current_fan_mode;
    chickenAck.feeding_triggered = incomingCmd.trigger_feeder;
    chickenAck.confirmed_motor_mode = current_motor_mode; 

    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(espnow_channel, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);
    esp_now_send(mainUnitAddress, (uint8_t *) &chickenAck, sizeof(chickenAck));
  }

  // Task 3: Non-blocking Feeder Control (5s)
  if (is_feeding) {
    if (currentMillis - feed_start_time >= 5000) {
      feederServo.write(90); 
      is_feeding = false;
      Serial.println("<<< ACTION: FEEDING COMPLETE! Returned to 90 Deg >>>\n");
    }
  }

  // Task 4: Non-blocking Pump Control (5s)
  if (is_pumping) {
    if (currentMillis - pump_start_time >= 5000) {
      if (current_motor_mode == 0) {
         digitalWrite(MOTOR_RELAY_PIN, LOW); 
         chickenData.motor_status = false;
      }
      is_pumping = false;
      Serial.println("<<< ACTION: AUTO PUMPING COMPLETE! >>>\n");
    }
  }
}