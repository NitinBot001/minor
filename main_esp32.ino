// ================================================================
// MOTHER ESP32 — MASTER ORCHESTRATOR (100% COMPLETE)
// Features: HTTPS Cloud, Sanitizer Task, ECG Buffer, Fast MAX30102
// ================================================================

#include <Wire.h>
#include <WiFi.h>
#include <HTTPClient.h> 
#include <WiFiClientSecure.h> 
#include "MAX30105.h"
#include "spo2_algorithm.h" 
#include "heartRate.h"
#include "math.h"

// ---------- Wi-Fi & Production Server Config ----------
#define WIFI_SSID "tez"
#define WIFI_PASSWORD "87654321"
#define SERVER_URL "https://nitinbot001-minor.hf.space/api/vitals" 

// ---------- Rover Handshake Pins ----------
#define BED_REACHED_PIN 14 
#define MOVE_NEXT_PIN 27   

// ---------- AUTOMATIC SANITIZER PINS ----------
#define TRIG_PIN 12
#define ECHO_PIN 13
#define RELAY_PIN 15

// ---------- State Variables ----------
volatile bool startSync = false;
volatile int syncBed = 1, syncBpm = 0, syncSpo2 = 0;
volatile float syncTemp = 0.0;
bool lastBedState = false;
int currentBedCounter = 1; 

// --- ECG BUFFERING ---
int ecgBuffer[500]; 
volatile int ecgIndex = 0;
volatile bool isRecordingEcg = false;

// ---------- MAX30102 Variables (FAST SENSITIVITY UPGRADE) ----------
MAX30105 particleSensor;
uint32_t irBuffer[100], redBuffer[100]; 
int32_t bufferLength = 100, spo2, heartRate;         
int8_t validSPO2, validHeartRate;     
long lastBeat = 0; float beatsPerMinute; 
float beatAvg = 0, sp02Avg = 0; 
// FIX: Threshold reduced from 10000 to 6000 for quick touch response
const long FINGER_ON_THRESHOLD = 6000; 

#define DIGITAL_INPUT 4      
#define ANALOG_INPUT 35      
#define TEMP_OFFSET 2.0 
#define LO_PLUS 32
#define LO_MINUS 33
#define ECG_OUT 34 

volatile int current_bpm = 0, current_spo2 = 0;
volatile float current_temp = 0.0;

unsigned long lastTxTime = 0;
const unsigned long TX_INTERVAL = 40; 
TaskHandle_t Max30102Task, CloudTask, SanitizerTask;

double readThermistor(int RawADC) {
  if (RawADC <= 0 || RawADC >= 4095) return 0;
  double R_therm = 2048 * (4095.0 / RawADC - 1.0);
  double logR = log(R_therm);
  double Temp = 1.0 / (0.001129148 + (0.000234125 * logR) + (0.0000001753482 * logR * logR * logR));
  return Temp - 287.15;      
}

// ============================================================
// CORE 1: Independent Sanitizer Task
// ============================================================
void runSanitizer(void * parameter) {
  for(;;) {
    digitalWrite(TRIG_PIN, LOW); delayMicroseconds(2);
    digitalWrite(TRIG_PIN, HIGH); delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);

    long duration = pulseIn(ECHO_PIN, HIGH, 30000); 
    if (duration > 0) {
      int distance = duration * 0.034 / 2;
      if (distance > 2 && distance < 12) {
        Serial.println("Hand detected! Dispensing Sanitizer...");
        digitalWrite(RELAY_PIN, HIGH); 
        vTaskDelay(2000 / portTICK_PERIOD_MS); 
        digitalWrite(RELAY_PIN, LOW); 
        Serial.println("Dispensing complete. Cooldown...");
        vTaskDelay(2000 / portTICK_PERIOD_MS); 
      }
    }
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

// ============================================================
// CORE 0: HTTPS Cloud Sync Task (Hugging Face)
// ============================================================
void runCloudSync(void * parameter) {
  for(;;) {
    if (startSync) {
      if (WiFi.status() == WL_CONNECTED) {
        Serial.println("Uploading Full Report + ECG to Hugging Face Cloud...");
        HTTPClient http;
        WiFiClientSecure secureClient;
        secureClient.setInsecure(); 

        http.begin(secureClient, SERVER_URL); 
        http.addHeader("Content-Type", "application/json");

        String payload;
        payload.reserve(4500); 
        
        payload = "{";
        payload += "\"bed_id\":" + String(syncBed) + ",";
        payload += "\"bpm\":" + String(syncBpm) + ",";
        payload += "\"spo2\":" + String(syncSpo2) + ",";
        payload += "\"temp\":" + String(syncTemp) + ",";
        payload += "\"ecg\":[";
        for(int i=0; i<500; i++) {
            payload += String(ecgBuffer[i]);
            if(i < 499) payload += ",";
        }
        payload += "]}";

        int httpResponseCode = http.POST(payload);
        if (httpResponseCode > 0) Serial.println("Upload Success! Code: " + String(httpResponseCode));
        else Serial.println("Upload Failed! Error: " + http.errorToString(httpResponseCode));
        http.end(); 
      } else {
        Serial.println("Wi-Fi not connected!");
      }
      startSync = false; 
    }
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

// ============================================================
// CORE 0: Fast MAX30102 Algorithm Task
// ============================================================
void runMax30102(void * parameter) {
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) { while (1) vTaskDelay(10 / portTICK_PERIOD_MS); }
  particleSensor.setup(50, 1, 2, 100, 69, 4096);
  for (byte i=0; i<bufferLength; i++) { while(!particleSensor.available()) particleSensor.check(); redBuffer[i] = particleSensor.getIR(); irBuffer[i] = particleSensor.getRed(); particleSensor.nextSample(); }
  maxim_heart_rate_and_oxygen_saturation(irBuffer, bufferLength, redBuffer, &spo2, &validSPO2, &heartRate, &validHeartRate);
  for (;;) {
    for (byte i=25; i<100; i++) { redBuffer[i-25] = redBuffer[i]; irBuffer[i-25] = irBuffer[i]; }
    for (byte i=75; i<100; i++) {
      while(!particleSensor.available()) particleSensor.check(); 
      redBuffer[i] = particleSensor.getRed(); irBuffer[i] = particleSensor.getIR(); particleSensor.nextSample(); 
      long irValue = irBuffer[i];
      if (irValue < FINGER_ON_THRESHOLD) { beatAvg = 0; sp02Avg = 0; beatsPerMinute = 0; current_bpm = 0; current_spo2 = 0; }
      else {
        if (checkForBeat(irValue)) {
          long delta = millis() - lastBeat; lastBeat = millis(); beatsPerMinute = 60 / (delta / 1000.0);
          if (beatsPerMinute < 220 && beatsPerMinute > 40) { 
             beatAvg = (beatAvg==0) ? beatsPerMinute : (beatAvg*0.6 + beatsPerMinute*0.4); 
             current_bpm = beatAvg; 
          }
        }
      }
    }
    if (irBuffer[99] > FINGER_ON_THRESHOLD) {
      maxim_heart_rate_and_oxygen_saturation(irBuffer, bufferLength, redBuffer, &spo2, &validSPO2, &heartRate, &validHeartRate);
      if (spo2 <= 100 && spo2 > 60) { 
        sp02Avg = (sp02Avg==0) ? spo2 : (sp02Avg*0.7 + spo2*0.3); 
        current_spo2 = sp02Avg; 
      }
    }
    vTaskDelay(1 / portTICK_PERIOD_MS); 
  }
}

void setup() {
  Serial.begin(115200); 
  Serial2.begin(9600, SERIAL_8N1, 16, 17); 
  
  pinMode(DIGITAL_INPUT, INPUT); pinMode(ANALOG_INPUT, INPUT); pinMode(LO_PLUS, INPUT); pinMode(LO_MINUS, INPUT); pinMode(ECG_OUT, INPUT);
  pinMode(BED_REACHED_PIN, INPUT_PULLDOWN); pinMode(MOVE_NEXT_PIN, OUTPUT); digitalWrite(MOVE_NEXT_PIN, LOW);

  pinMode(TRIG_PIN, OUTPUT); pinMode(ECHO_PIN, INPUT); pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW); 

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) { delay(500); }
  
  for(int i=0; i<500; i++) ecgBuffer[i] = 2048; 

  xTaskCreatePinnedToCore(runMax30102, "MaxTask", 10000, NULL, 0, &Max30102Task, 0);
  xTaskCreatePinnedToCore(runCloudSync, "CloudTask", 9000, NULL, 0, &CloudTask, 0);
  xTaskCreatePinnedToCore(runSanitizer, "SanitizerTask", 4000, NULL, 1, &SanitizerTask, 1);
}

void loop() {
  bool currentBedState = digitalRead(BED_REACHED_PIN);
  if (currentBedState == HIGH && lastBedState == LOW) { Serial2.printf("CMD:ARRIVED,%d\n", currentBedCounter); }
  lastBedState = currentBedState;

  if (Serial.available()) {
    String pcCmd = Serial.readStringUntil('\n'); pcCmd.trim();
    if (pcCmd == "ARRIVE") Serial2.printf("CMD:ARRIVED,%d\n", currentBedCounter);
  }

  if (Serial2.available()) {
    String incoming = Serial2.readStringUntil('\n'); incoming.trim();
    if (incoming.startsWith("CMD:START_ECG")) {
      isRecordingEcg = true; ecgIndex = 0;
      Serial.println("Started 20s ECG Buffer Stream...");
    }
    else if (incoming.startsWith("SYNC:")) {
      incoming.remove(0, 5); 
      int comma1 = incoming.indexOf(','); int comma2 = incoming.indexOf(',', comma1 + 1); int comma3 = incoming.indexOf(',', comma2 + 1);
      syncBed = incoming.substring(0, comma1).toInt(); syncBpm = incoming.substring(comma1 + 1, comma2).toInt();
      syncSpo2 = incoming.substring(comma2 + 1, comma3).toInt(); syncTemp = incoming.substring(comma3 + 1).toFloat();
      startSync = true; 
    }
    else if (incoming.startsWith("CMD:NEXT")) {
      currentBedCounter++; if(currentBedCounter > 3) currentBedCounter = 1; 
      digitalWrite(MOVE_NEXT_PIN, HIGH); delay(300); digitalWrite(MOVE_NEXT_PIN, LOW);
    }
  }

  unsigned long currentMillis = millis();
  if (currentMillis - lastTxTime >= TX_INTERVAL) {
    lastTxTime = currentMillis;
    int analog_temp = analogRead(ANALOG_INPUT);         
    int revised_temp = map(analog_temp, 0, 4095, 4095, 0);
    current_temp = readThermistor(revised_temp) + TEMP_OFFSET;
    int ecgRawVal = (digitalRead(LO_PLUS) == 1 || digitalRead(LO_MINUS) == 1) ? 2048 : analogRead(ECG_OUT);

    if(isRecordingEcg && ecgIndex < 500) {
        ecgBuffer[ecgIndex] = ecgRawVal;
        ecgIndex++;
        if(ecgIndex >= 500) isRecordingEcg = false;
    }

    Serial2.printf("B:%d S:%d T:%.1f E:%d\n", current_bpm, current_spo2, current_temp, ecgRawVal);
  }
}
