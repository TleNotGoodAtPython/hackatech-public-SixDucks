#include <WiFi.h>
#include <HTTPClient.h>
#include <esp_now.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include <ArduinoJson.h>

// Configurations
const char* ssid     = "";
const char* password = "";
const char* serverUrl = "";
const char* statusUrl = ""; 
const char* bin_id   = "1";
//EC:E3:34:1B:72:C4
uint8_t CYD_MAC[]    = {0xEC, 0xE3, 0x34, 0x1B, 0x72, 0xC4};

#define MEASURE_TRIG 5   // Vertical Sensor (Top of bin)
#define MEASURE_ECHO 18  
#define DETECT_TRIG  19  // Horizontal Sensor (Chute entrance)
#define DETECT_ECHO  21
#define button 4      

const float CHUTE_WIDTH_CM = 14.5; 

bool binIsFull = false;
bool lastReportedFullStatus = false; 
bool userSessionActive = false;
int currentPointsAccumulated = 0;

// Button tracking & Debounce
bool lastButtonState = HIGH; 

// Timeout tracking configuration
unsigned long lastSessionActivityTime = 0;
const unsigned long SESSION_TIMEOUT_MS = 20000; // 20 Seconds timeout window

uint32_t globalSessionId = 0; 

typedef struct struct_packet {
    char url[96]; 
    bool isFull;   
    uint32_t session_id; 
} struct_packet;

struct_packet outboundPacket;

long readDistanceCm(int trigPin, int echoPin) {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  
  long duration = pulseIn(echoPin, HIGH, 20000); 
  if (duration == 0) return -1;                  
  
  return duration * 0.034 / 2;
}

bool Measure() {
  long distanceCm = readDistanceCm(MEASURE_TRIG, MEASURE_ECHO);
  Serial.print("Measure ");
  Serial.print(distanceCm);
  Serial.println(" cm");
  if (distanceCm > 0 && distanceCm < 5) return true; 
  return false;
}

bool Detect() {
  delay(60); 
  long distanceCm = readDistanceCm(DETECT_TRIG, DETECT_ECHO);
  Serial.print("Detect ");
  Serial.print(distanceCm);
  Serial.println(" cm");
  if (distanceCm <= 0) return false; 
  if (distanceCm < (CHUTE_WIDTH_CM - 5)) return true; 
  return false;
}

String Curl(int points) {
  WiFiClient client; 
  
  HTTPClient http;
  
  // Ensure we are using the standard client and the HTTP URL
  if (http.begin(client, serverUrl)) { 
    http.addHeader("Content-Type", "application/json");
    http.addHeader("User-Agent", "ESP32-Client");
    
    String jsonPayload = "{\"bin_id\":\"" + String(bin_id) + "\",\"points\":" + String(points) + "}";
    int httpResponseCode = http.POST(jsonPayload);
    
    String responseLink = "";
    if (httpResponseCode > 0) {
      responseLink = http.getString();
      Serial.print("HTTP Success! Response: "); Serial.println(responseLink);
    } else {
      Serial.print("HTTP Error: "); Serial.println(http.errorToString(httpResponseCode));
    }
    http.end();
    return responseLink;
  } else {
    Serial.println("Unable to connect to server");
    return "";
  }
}

void reportStatusToBackend(bool isFull) {
  if (WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  http.begin(statusUrl);
  http.addHeader("Content-Type", "application/json");
  String jsonPayload = "{\"bin_id\":\"" + String(bin_id) + "\",\"is_full\":" + (isFull ? "true" : "false") + "}";
  http.POST(jsonPayload);
  http.end();
}

void SendToCYD(const char* urlPayload, bool fullStatus) {
  strncpy(outboundPacket.url, urlPayload, sizeof(outboundPacket.url) - 1);
  outboundPacket.url[sizeof(outboundPacket.url) - 1] = '\0';
  outboundPacket.isFull = fullStatus;
  outboundPacket.session_id = globalSessionId; 
  esp_now_send(CYD_MAC, (uint8_t *) &outboundPacket, sizeof(outboundPacket));
}

void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {}

void setup() {
   WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  Serial.begin(115200);
  
  pinMode(MEASURE_TRIG, OUTPUT);
  pinMode(MEASURE_ECHO, INPUT);
  pinMode(DETECT_TRIG, OUTPUT);
  pinMode(DETECT_ECHO, INPUT);
  pinMode(button, INPUT_PULLUP); 

  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(500); }
  Serial.print("Router Channel is: ");
  Serial.println(WiFi.channel());
  if (esp_now_init() == ESP_OK) {
    esp_now_register_send_cb(OnDataSent);
    esp_now_peer_info_t peerInfo;
    memcpy(peerInfo.peer_addr, CYD_MAC, 6);
    peerInfo.channel = 13;
    peerInfo.ifidx = WIFI_IF_STA; 
    peerInfo.encrypt = false;
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
      Serial.println("Failed to add peer");
    }
  }
}

// Separate function to cleanly close out sessions and dispatch data to server
void terminateUserSession() {
  binIsFull = Measure();
  
  if (currentPointsAccumulated > 0) {
    String jsonResponse = Curl(currentPointsAccumulated);
    
    // Parse the JSON to get only the URL
    StaticJsonDocument<200> doc;
    DeserializationError error = deserializeJson(doc, jsonResponse);
    
    if (!error) {
      String actualUrl = doc["claim_url"]; // Extract just the URL
      Serial.print("Sending clean URL to CYD: "); Serial.println(actualUrl);
      SendToCYD(actualUrl.c_str(), binIsFull);
    } else {
      Serial.println("JSON Parsing failed!");
      SendToCYD("", binIsFull);
    }
  } else {
    SendToCYD("", binIsFull);
  }
}

void loop() {
  bool currentButtonState = digitalRead(button);
  Serial.println(currentButtonState);
  // 1. BUTTON PRESS LOGIC (MANUAL TOGGLE)
  if (currentButtonState == LOW && lastButtonState == HIGH) { 
    delay(500); // Debounce
    
    if (!userSessionActive) {
      if (!binIsFull) {
        userSessionActive = true;
        globalSessionId++; 
        currentPointsAccumulated = 0; 
        lastSessionActivityTime = millis(); // Reset timeout tracker right at start

        SendToCYD("", false);

        Serial.println("\n=== Session Started Manual Control ===");
      } else {
        Serial.println("Cannot execute: Bin full lockdown active.");
      }
    } else {
      Serial.println("\n=== Session Finalized by User Button ===");
      terminateUserSession();
    }
  }
  lastButtonState = currentButtonState;

  // 2. TIMEOUT LOGIC (AUTO-EXPIRY)
  if (userSessionActive) {
    if (millis() - lastSessionActivityTime >= SESSION_TIMEOUT_MS) {
      Serial.println("\n=== Session Timeout Warning: Inactivity Auto-Close! ===");
      terminateUserSession();
    }
  }

  // 3. BACKGROUND SYSTEM LOGS
  if (!userSessionActive) {
    binIsFull = Measure();
    if (binIsFull != lastReportedFullStatus) {
      lastReportedFullStatus = binIsFull;
      reportStatusToBackend(binIsFull);
    }
    if (binIsFull) {
      SendToCYD("", true); 
      delay(3000); 
      return;      
    }
  }

  // 4. ACTIVE DISPOSAL DETECTION
  if (userSessionActive && !binIsFull) {
    if (Detect()) {
      currentPointsAccumulated += 5;
      Serial.print("Item Tracked! Points: ");
      Serial.println(currentPointsAccumulated);
      
      lastSessionActivityTime = millis(); // <-- Reset the safety timer when an item is dropped!
      delay(600); 
      
      binIsFull = Measure();
      if (binIsFull != lastReportedFullStatus) {
        lastReportedFullStatus = binIsFull;
        reportStatusToBackend(binIsFull);
      }
      
      if (binIsFull) {
        Serial.println("Auto-kill session: Bin max capacity reached.");
        String dynamicUrl = Curl(currentPointsAccumulated);
        SendToCYD(dynamicUrl.c_str(), true);
        currentPointsAccumulated = 0;
        userSessionActive = false;
      }
    }
  }

  delay(2000); 
}