#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include "qrcode.h"
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <WiFiClientSecure.h>

TFT_eSPI tft = TFT_eSPI();

// Screen Dimensions
#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 320
#define BACKLIGHT_PIN 21

// System States
enum SystemState { STATE_SLEEP, STATE_SESSION_ACTIVE, STATE_SHOW_QR, STATE_FULL };
SystemState currentState = STATE_SLEEP; 

char currentLink[96] = ""; 
uint32_t lastSessionId = 0; 
volatile bool stateChanged = false; 

// Timers for the screen wake cycle
unsigned long qrStartTime = 0;
const unsigned long QR_TIMEOUT = 15000; 

typedef struct struct_packet {
    char url[96]; 
    bool isFull;   
    uint32_t session_id; 
} struct_packet;

struct_packet incomingData;

void wakeDisplay() {
  digitalWrite(BACKLIGHT_PIN, HIGH); 
  tft.writecommand(0x11);            
  delay(120);                        
}

void sleepDisplay() {
  tft.fillScreen(TFT_BLACK);         
  tft.writecommand(0x10);            
  digitalWrite(BACKLIGHT_PIN, LOW);  
  Serial.println("Display sleeping to save power...");
}

void drawQRCode(const char* dynamicUrl) {
  QRCode qrcode;
  const int QR_VERSION = 7; 
  
  uint8_t qrcodeData[qrcode_getBufferSize(QR_VERSION)];
  qrcode_initText(&qrcode, qrcodeData, QR_VERSION, ECC_LOW, dynamicUrl);

  int scale = 3; 
  int qrSize = qrcode.size * scale; 
  
  int xOffset = (SCREEN_WIDTH - qrSize) / 2;
  int yOffset = (SCREEN_HEIGHT - qrSize) / 2;

  tft.fillScreen(TFT_BLACK);
  tft.fillRect(xOffset - 8, yOffset - 8, qrSize + 16, qrSize + 16, TFT_WHITE);

  for (uint8_t y = 0; y < qrcode.size; y++) {
    for (uint8_t x = 0; x < qrcode.size; x++) {
      if (qrcode_getModule(&qrcode, x, y)) {
        tft.fillRect(xOffset + (x * scale), yOffset + (y * scale), scale, scale, TFT_BLACK);
      } else {
        tft.fillRect(xOffset + (x * scale), yOffset + (y * scale), scale, scale, TFT_WHITE);
      }
    }
  }
}

void drawActiveSession() {
  tft.fillScreen(TFT_DARKGREEN); 
  tft.setTextColor(TFT_WHITE);
  tft.setTextDatum(MC_DATUM); 
  
  tft.drawString("SESSION ACTIVE", SCREEN_WIDTH / 2, (SCREEN_HEIGHT / 2) - 20, 4);
  tft.drawString("Drop items into the chute...", SCREEN_WIDTH / 2, (SCREEN_HEIGHT / 2) + 20, 2);
}

void drawFullAlert() {
  tft.fillScreen(TFT_RED); 
  tft.setTextColor(TFT_WHITE);
  tft.setTextDatum(MC_DATUM); 
  
  tft.drawString("BIN FULL", SCREEN_WIDTH / 2, (SCREEN_HEIGHT / 2) - 20, 4);
  tft.drawString("Please contact staff.", SCREEN_WIDTH / 2, (SCREEN_HEIGHT / 2) + 20, 2);
}

// FIXED: Matching legacy signature for ESP32 Framework v2.0.7
void OnDataRecv(const uint8_t *mac_addr, const uint8_t *incomingDataRaw, int len) {
  Serial.println("Packet Received via ESP-NOW!"); 
  
  int bytesToCopy = min(len, (int)sizeof(struct_packet));
  memcpy(&incomingData, incomingDataRaw, bytesToCopy);
  incomingData.url[sizeof(incomingData.url) - 1] = '\0';
  
  if (strlen(incomingData.url) > 0) {
    if (incomingData.session_id != lastSessionId || currentState != STATE_SHOW_QR) {
      strncpy(currentLink, incomingData.url, sizeof(currentLink) - 1);
      currentLink[sizeof(currentLink) - 1] = '\0';
      lastSessionId = incomingData.session_id; 
      
      currentState = STATE_SHOW_QR;
      stateChanged = true;
    }
  } 
  else if (incomingData.isFull && currentState != STATE_FULL) {
    currentState = STATE_FULL;
    stateChanged = true;
  }
  else if (!incomingData.isFull && incomingData.session_id != lastSessionId) {
    lastSessionId = incomingData.session_id; 
    currentState = STATE_SESSION_ACTIVE;
    stateChanged = true;
  }
}

void setup() {
  Serial.begin(115200);
  
  pinMode(BACKLIGHT_PIN, OUTPUT);
  
  tft.init();
  tft.invertDisplay(true);
  tft.setRotation(1); 
  sleepDisplay();

  // 1. Initialize Wi-Fi Layer
  WiFi.mode(WIFI_STA);
  
  // 2. Configure Asian-Friendly Country Bounds (Fixed order execution)
  wifi_country_t countryConfig = {
    .cc = "EU",              
    .schan = 1,              
    .nchan = 13,             
    .policy = WIFI_COUNTRY_POLICY_AUTO
  };
  esp_wifi_set_country(&countryConfig);

  // 3. Register ESP-NOW Protocol Layer
  if (esp_now_init() == ESP_OK) {
    esp_now_register_recv_cb(OnDataRecv);
    Serial.println("CYD Boot Complete. Listening for wireless packets...");
  } else {
    Serial.println("Critical Error: ESP-NOW Init Failed.");
  }

  // 4. Print Local MAC Identity to Terminal
  Serial.print("CYD Real MAC Address: ");
  Serial.println(WiFi.macAddress());

  // 5. Hard Lock onto Router Frequency (Channel 12)
  uint8_t primaryChan = 13; 
  esp_wifi_set_channel(primaryChan, WIFI_SECOND_CHAN_NONE);
  Serial.print("Forced CYD Wi-Fi onto Channel: ");
  Serial.println(primaryChan);
}

void loop() {
  if (stateChanged) {
    stateChanged = false; 
    
    if (currentState == STATE_SHOW_QR) {
      wakeDisplay(); 
      Serial.print("Generating QR for user: ");
      Serial.println(currentLink);
      drawQRCode(currentLink);
      qrStartTime = millis(); 
    } 
    else if (currentState == STATE_FULL) {
      wakeDisplay(); 
      Serial.println("Displaying Bin Full Alert Shield.");
      drawFullAlert();
    }
    else if (currentState == STATE_SLEEP) {
      sleepDisplay(); 
    }
    else if (currentState == STATE_SESSION_ACTIVE) {
      wakeDisplay(); 
      Serial.println("Displaying Active Session Screen.");
      drawActiveSession();
    }
  }

  if (currentState == STATE_SHOW_QR) {
    if (millis() - qrStartTime >= QR_TIMEOUT) {
      if (incomingData.isFull) {
        currentState = STATE_FULL;
      } else {
        currentState = STATE_SLEEP; 
      }
      stateChanged = true; 
    }
  }

  delay(50); 
}