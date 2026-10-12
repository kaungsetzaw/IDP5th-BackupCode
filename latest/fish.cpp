#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h> 
#include <Preferences.h>

// --- Configuration & Constants ---
uint8_t mainUnitAddress[] = {0x30, 0xED, 0xA0, 0x2C, 0x7B, 0x44}; 

const int CONFIG_BTN_PIN = 0;  // BOOT button for WiFi channel setup
const int PH_SENSOR_PIN = 4;   // Analog input for pH sensor
const int TEMP_SENSOR_PIN = 5; // Analog input for TMP36 temperature sensor

Preferences preferences;
esp_now_peer_info_t peerInfo;
int espnow_channel;

// --- Timing Control ---
unsigned long last_temp_sample_time = 0;
unsigned long last_send_time = 0;
const unsigned long TEMP_SAMPLE_INTERVAL = 1000; // Sample temp every 1s
const unsigned long SEND_INTERVAL = 5000;        // Send data every 5s

// --- Data Structures ---
typedef struct fish_message {
  int unit_id;           // Assigned ID: 3
  float temperature;
  float ph_level;
  bool is_hot;           
  bool is_bad_ph;        
  bool is_critical;      
} fish_message;

fish_message fishData;

// --- Temperature Smoothing (Trimmed Mean) Variables ---
const int NUM_SAMPLES = 50; 
const int HISTORY_SIZE = 10;
float tempHistory[HISTORY_SIZE];
int historyIndex = 0;
int historyCount = 0; 
float finalAvgTempC = 0.0; // Stores the finalized smoothed temperature

// ESP-NOW Send Callback
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  // Optional: Add serial debug for delivery status here
}

// Convert analog reading to pH value (Requires calibration)
float get_pH_Level(int analogValue) {
  float voltage = analogValue * (3.3 / 4095.0);
  return 3.5 * voltage; // Standard conversion, adjust multiplier as needed
}

void setup() {
  Serial.begin(115200);
  pinMode(CONFIG_BTN_PIN, INPUT_PULLUP);

  // Load saved WiFi channel from flash memory
  preferences.begin("farm_config", false);
  espnow_channel = preferences.getInt("main_channel", 1); 

  Serial.println("\nPress BOOT Button within 3 seconds for Config Mode...");
  delay(3000);

  // Config Mode: Update WiFi Channel via Serial Monitor
  if (digitalRead(CONFIG_BTN_PIN) == LOW) {
    Serial.println("\n>>> CONFIG MODE ACTIVATED <<<");
    while (Serial.available() > 0) Serial.read(); // Clear buffer
    
    Serial.print("Enter Main Unit WiFi Channel (1-13): ");
    while (Serial.available() == 0) { delay(10); } 
    
    String channelInput = Serial.readStringUntil('\n');
    channelInput.trim();
    int newChannel = channelInput.toInt();
    
    if (newChannel > 0 && newChannel <= 13) {
      preferences.putInt("main_channel", newChannel);
      espnow_channel = newChannel;
      Serial.printf("Channel saved: %d\n", espnow_channel);
    }
  }

  // Initialize WiFi in Station Mode
  WiFi.mode(WIFI_STA);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(espnow_channel, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);
  
  // Initialize ESP-NOW and register peer
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW Init Failed");
    return;
  }
  
  esp_now_register_send_cb((esp_now_send_cb_t)OnDataSent);
  
  memset(&peerInfo, 0, sizeof(peerInfo)); 
  memcpy(peerInfo.peer_addr, mainUnitAddress, 6);
  peerInfo.channel = espnow_channel;  
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);

  Serial.println("Fish Unit Initialization Complete.");
}

void loop() {
  unsigned long currentMillis = millis();
  
  // ==========================================
  // Task 1: Temperature Sampling (Runs every 1 second)
  // ==========================================
  if (currentMillis - last_temp_sample_time >= TEMP_SAMPLE_INTERVAL) {
    last_temp_sample_time = currentMillis;

    // 1. Rapid burst sampling for hardware noise filtering
    uint32_t total_mV = 0;
    for (int i = 0; i < NUM_SAMPLES; i++) {
      total_mV += analogReadMilliVolts(TEMP_SENSOR_PIN);
      delay(2); 
    }
    uint32_t average_mV = total_mV / NUM_SAMPLES;
    
    // Convert mV to Celsius (Equation for TMP36)
    // Note: If using LM35, change equation to: (average_mV / 10.0)
    float currentTempC = ((average_mV - 500.0) / 10.0);

    // 2. Update rolling history array
    tempHistory[historyIndex] = currentTempC;
    historyIndex = (historyIndex + 1) % HISTORY_SIZE;
    if (historyCount < HISTORY_SIZE) historyCount++;

    // 3. Create a temporary array for sorting
    float sortedHistory[HISTORY_SIZE];
    for (int i = 0; i < historyCount; i++) {
      sortedHistory[i] = tempHistory[i];
    }

    // 4. Bubble sort the temporary array (Ascending)
    for (int i = 0; i < historyCount - 1; i++) {
      for (int j = 0; j < historyCount - i - 1; j++) {
        if (sortedHistory[j] > sortedHistory[j + 1]) {
          float temp = sortedHistory[j];
          sortedHistory[j] = sortedHistory[j + 1];
          sortedHistory[j + 1] = temp;
        }
      }
    }

    // 5. Calculate Trimmed Mean
    float sumTempC = 0;
    int validSamples = 0;

    if (historyCount == HISTORY_SIZE) {
      // Discard 2 lowest and 2 highest values to eliminate outliers
      for (int i = 2; i < 8; i++) { 
        sumTempC += sortedHistory[i];
        validSamples++;
      }
    } else {
      // Direct average while buffer is still filling (first 9 seconds)
      for (int i = 0; i < historyCount; i++) {
        sumTempC += sortedHistory[i];
        validSamples++;
      }
    }
    finalAvgTempC = sumTempC / validSamples;
  }

  // ==========================================
  // Task 2: Data Transmission (Runs every 5 seconds)
  // ==========================================
  if (currentMillis - last_send_time >= SEND_INTERVAL) {
    last_send_time = currentMillis;
    
    // 1. Compile payload data
    fishData.unit_id = 3;
    fishData.temperature = finalAvgTempC; 
    
    int phAnalogValue = analogRead(PH_SENSOR_PIN);
    fishData.ph_level = get_pH_Level(phAnalogValue);

    // 2. Threshold classification
    fishData.is_hot = (fishData.temperature > 36.0);
    fishData.is_bad_ph = (fishData.ph_level <= 6.0 || fishData.ph_level >= 7.8);
    fishData.is_critical = (fishData.is_hot || fishData.is_bad_ph);

    // 3. Enforce channel lock before transmission
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(espnow_channel, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);

    // 4. Send telemetry via ESP-NOW
    esp_now_send(mainUnitAddress, (uint8_t *) &fishData, sizeof(fishData));
    
    // Output localized debug telemetry
    Serial.printf("🐟 Temp: %.1f C (Filtered), pH: %.2f | Critical: %d\n", 
                   fishData.temperature, fishData.ph_level, fishData.is_critical);
  }
}