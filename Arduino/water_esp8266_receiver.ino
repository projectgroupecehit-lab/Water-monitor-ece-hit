// receiver main code

// Description: 
// On initial power-on, the device attempts to reconnect to the previously configured Wi-Fi network (SSID and password) 
// for 1 minute while displaying the animated reconnection page. If this 1-minute attempt fails, the device switches 
// to Access Point (AP) mode, broadcasting the hotspot name "EceRocks" with the password "12345678" to allow new configuration. 
// 
// If the device has already established a successful connection during its current power cycle but later loses internet, 
// it will strictly remain in reconnection mode (indefinitely looping the loader page) without dropping back into AP mode.
// 
// Note: To force-change the Wi-Fi configuration layout, power the device off and back on, then wait 1 minute for the 
// reconnection screen to time out and display the AP credentials.

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Arduino.h>
#include <stdint.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>
#include <WiFiManager.h> 

/* ================= STRUCT FOR SERIAL RECEPTION ================= */
struct __attribute__((packed)) SensorData {
  int32_t tds;
  float   temp;
  int32_t turb;   
  float   do_val;
  float   ph;
};

SensorData rxData;
bool dataReady = false;

/* ================= SERIAL FRAME CONFIG ================= */
const uint8_t START_BYTE = 0xAA;
uint8_t serialBuffer[sizeof(SensorData)];
uint8_t indexPos = 0;
bool receiving = false;

/* ================= WIFI CONFIGURATION ================= */
const char* serverUrl = "https://water-monitor-ece-hit-backend.onrender.com/api/data"; // Backend url running on render

WiFiManager wm;

/* ================= TFT SPI PIN DEFINITIONS (NodeMCU) ================= */
#define TFT_CS   15  // D8 (GPIO15) - Hardware CS
#define TFT_RST  5   // D1 (GPIO5)  - Reset
#define TFT_DC   4   // D2 (GPIO4)  - Data/Command
#define TFT_MOSI 13  // D7 (GPIO13) - Hardware SPI MOSI (SDA)
#define TFT_CLK  14  // D5 (GPIO14) - Hardware SPI SCK (SCL)

Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_MOSI, TFT_CLK, TFT_RST);

/* ================= PREMIUM INDUSTRIAL COLOR PALETTE (RGB565) ================= */
#define COLOR_BG        0x0000  
#define COLOR_CARD_BG   0x10A2  
#define COLOR_HEADER_BG 0x08A3  
#define COLOR_TEXT_MUT  0x7BEF  
#define COLOR_TEXT_WHT  0xFFFF  

#define COLOR_TDS       0xFDA0  
#define COLOR_TURB      0x07FF  
#define COLOR_DO        0xF81F  
#define COLOR_PH        0x07E0  
#define COLOR_EC        0xFFE0  
#define COLOR_TEMP      0xFC00  

#define STATUS_OK       0x07E0  

/* ================= SYSTEM VARIABLES ================= */
float currentTDS  = 0.0;
float currentTurb = 0.0;
float currentDO   = 0.0;
float currentPH   = 0.0;  
float currentEC   = 0.0; 
float currentTemp = 0.0;

bool wasConnected = false;
bool initialMessageShown = false;
bool heartbeatState = false;
int spinnerFrame = 0;

const int cardW = 73;
const int cardH = 33;
const int gapX = 6;
const int gapY = 4;
const int yStart = 17;

/* ================= NON-BLOCKING TIMERS ================= */
unsigned long lastSendTime = 0;
const unsigned long SEND_INTERVAL = 30000; 

unsigned long lastTftUpdateTime = 0;
const unsigned long TFT_UPDATE_INTERVAL = 300; 

unsigned long lastSpinnerTime = 0;
const unsigned long SPINNER_INTERVAL = 150; 

/* ================= FUNCTION PROTOTYPES ================= */
void drawPremiumDashboard();
void drawOfflineScreenStatic();
void drawOfflineSpinner();
void updateDashboardValues();
void sendToBackend(const SensorData& data);
void drawWiFiPortalMessage();
String getLabel(int index);
String getUnit(int index);
uint16_t getParameterColor(int index);
float getLiveValue(int index);
String getValueString(int index, float val);

/* ================= SETUP ================= */
void setup() {
  Serial.begin(115200);
  
  // Explicitly initialize rxData structure fields to avoid trash values/NaN at boot up
  rxData.tds = 0;
  rxData.temp = 0.0;
  rxData.turb = 0;
  rxData.do_val = 0.0;
  rxData.ph = 0.0;

  tft.initR(INITR_BLACKTAB);
  tft.setRotation(1);        
  tft.fillScreen(COLOR_BG);

  // 1. Draw your offline loader interface background
  drawOfflineScreenStatic();

  // 2. Begin silent Wi-Fi auto-reconnect sequence using cached credentials
  Serial.println("\n[SYSTEM] Attempting auto-reconnect for 60 seconds...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(); 

  unsigned long startAttemptTime = millis();
  
  // Force background connection checking with spinner animation for exactly 60 seconds
  while (WiFi.status() != WL_CONNECTED && (millis() - startAttemptTime < 60000)) {
    if (millis() - lastSpinnerTime >= SPINNER_INTERVAL) {
      drawOfflineSpinner();
      lastSpinnerTime = millis();
    }
    yield(); // Keeps the ESP8266 watchdog timer happy
  }

  // 3. Fallback evaluation
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[SYSTEM] Reconnection failed. Launching captive portal AP...");
    
    // Switch UI to display config information right before stepping into the portal loop
    drawWiFiPortalMessage();

    // Configure the configuration portal timeout (3 minutes)
    wm.setConfigPortalTimeout(180); 

    // Directly open the Access Point portal since autoConnect background timing expired
    if (!wm.startConfigPortal("EceRocks", "12345678")) {
      Serial.println("Configuration timed out or failed.");
      tft.fillScreen(COLOR_BG);
      tft.setCursor(10, 50);
      tft.setTextColor(COLOR_TEMP);
      tft.print("Boot Timeout...");
      delay(3000);
    }
  }

  // If connected successfully via background auto-reconnect OR after portal completion
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi successfully connected!");
    tft.fillScreen(COLOR_BG); 
  }
}

/* ================= MAIN LOOP ================= */
void loop() {
  bool isConnected = (WiFi.status() == WL_CONNECTED);

  /* ---- STATE 1: ONLINE DASHBOARD ---- */
  if (isConnected) {
    if (!wasConnected) {
      tft.fillScreen(COLOR_BG);
      drawPremiumDashboard();
      wasConnected = true;
      initialMessageShown = false; 
    }

    while (Serial.available()) {
      uint8_t b = Serial.read();

      if (!receiving) {
        if (b == START_BYTE) {
          receiving = true;
          indexPos = 0;
        }
      } else {
        serialBuffer[indexPos++] = b;

        if (indexPos == sizeof(SensorData)) {
          memcpy(&rxData, serialBuffer, sizeof(rxData));
          receiving = false;
          dataReady = true;

          currentTDS  = (float)rxData.tds;
          currentTemp = rxData.temp;
          currentTurb = (float)rxData.turb;
          currentDO   = rxData.do_val;
          currentPH   = rxData.ph;
          currentEC   = (float)rxData.tds * 1.56;

          Serial.println("\n--- Received Telemetry via Serial ---");
          Serial.print("TDS: ");  Serial.print(rxData.tds); Serial.println(" ppm");
          Serial.print("Temp: "); Serial.print(rxData.temp, 2); Serial.println(" C");
          Serial.print("Turb: "); Serial.print(rxData.turb); Serial.println(" %");
          Serial.print("DO: ");   Serial.print(rxData.do_val, 3); Serial.println(" mg/L");
          Serial.print("pH: ");   Serial.print(rxData.ph, 2);
          Serial.print("  | Calculated EC: "); Serial.print(currentEC, 1); Serial.println(" uS/cm");
          Serial.println("-------------------------------------");
        }
      }
    }

    if (millis() - lastTftUpdateTime >= TFT_UPDATE_INTERVAL) {
      updateDashboardValues();
      lastTftUpdateTime = millis();
    }

    if (millis() - lastSendTime >= SEND_INTERVAL) {
      sendToBackend(rxData);
      lastSendTime = millis();
      dataReady = false; 
    }
  } 
  
  /* ---- STATE 2: OFFLINE LOADER ---- */
  else {
    if (wasConnected || !initialMessageShown) {
      tft.fillScreen(COLOR_BG);
      drawOfflineScreenStatic();
      wasConnected = false;
      initialMessageShown = true;
    }
    
    if (millis() - lastSpinnerTime >= SPINNER_INTERVAL) {
      drawOfflineSpinner();
      lastSpinnerTime = millis();
    }
  }
}

/* ================= UI HELPER FUNCTIONS ================= */
String getLabel(int index) {
  switch (index) {
    case 0: return "TDS";
    case 1: return "TURB";
    case 2: return "D.O.";
    case 3: return "pH";
    case 4: return "E.C.";
    case 5: return "TEMP";
    default: return "";
  }
}

String getUnit(int index) {
  switch (index) {
    case 0: return "ppm";
    case 1: return "%";
    case 2: return "mg/L";
    case 3: return "pH";
    case 4: return "uS";
    case 5: return "C";
    default: return "";
  }
}

uint16_t getParameterColor(int index) {
  switch (index) {
    case 0: return COLOR_TDS;
    case 1: return COLOR_TURB;
    case 2: return COLOR_DO;
    case 3: return COLOR_PH;
    case 4: return COLOR_EC;
    case 5: return COLOR_TEMP;
    default: return COLOR_TEXT_WHT;
  }
}

float getLiveValue(int index) {
  switch (index) {
    case 0: return currentTDS;
    case 1: return currentTurb;
    case 2: return currentDO;
    case 3: return currentPH;
    case 4: return currentEC;
    case 5: return currentTemp;
    default: return 0.0;
  }
}

String getValueString(int index, float val) {
  // Catch NaN values on layout parsing as a visual fallback
  if (isnan(val)) return "0";

  switch (index) {
    case 0: return String(val, 0); 
    case 1: return String(val, 0); 
    case 2: return String(val, 1); 
    case 3: return String(val, 2); 
    case 4: return String(val, 0); 
    case 5: return String(val, 1); 
    default: return "0";
  }
}

/* ================= DRAWING ENGINE ================== */
void drawWiFiPortalMessage() {
  tft.fillScreen(COLOR_BG);
  tft.drawRoundRect(8, 12, 144, 104, 6, COLOR_CARD_BG);
  tft.setTextSize(1);
  tft.setTextColor(COLOR_TEXT_WHT);
  tft.setCursor(20, 25);
  tft.print("WIFI CONFIG REQUIRED");
  
  tft.setTextColor(COLOR_TEXT_MUT);
  tft.setCursor(18, 45);
  tft.print("Connect to WiFi AP:");
  
  tft.setTextColor(COLOR_TDS);
  tft.setCursor(18, 60);
  tft.print(">> EceRocks");
  
  tft.setTextColor(COLOR_TEXT_MUT);
  tft.setCursor(18, 80);
  tft.print("Open 192.168.4.1 to");
  tft.setCursor(18, 92);
  tft.print("configure settings.");
}

void drawPremiumDashboard() {
  tft.fillRect(0, 0, 160, 14, COLOR_HEADER_BG);
  tft.setTextColor(COLOR_TEXT_WHT);
  tft.setTextSize(1);
  tft.setCursor(6, 3);
  tft.print("AQUA ANALYTICA");
  
  tft.setTextColor(COLOR_TURB);
  tft.setCursor(120, 3);
  tft.print("[LIVE]");

  tft.drawFastHLine(0, 14, 160, 0x1A2F);

  tft.setTextSize(1);
  for (int row = 0; row < 3; row++) {
    for (int col = 0; col < 2; col++) {
      int x = 4 + col * (cardW + gapX);
      int y = yStart + row * (cardH + gapY);
      int index = row * 2 + col;

      tft.fillRoundRect(x, y, cardW, cardH, 3, COLOR_CARD_BG);

      uint16_t accentColor = getParameterColor(index);
      tft.fillRect(x, y, 3, cardH, accentColor);

      tft.setTextColor(COLOR_TEXT_MUT);
      tft.setCursor(x + 7, y + 4);
      tft.print(getLabel(index));

      String unit = getUnit(index);
      int unitWidth = unit.length() * 6;
      tft.setCursor(x + cardW - 5 - unitWidth, y + 4);
      tft.print(unit);
    }
  }
}

void drawOfflineScreenStatic() {
  tft.drawRoundRect(8, 12, 144, 104, 6, COLOR_CARD_BG);
  tft.drawRoundRect(9, 13, 142, 102, 5, 0x0821); 

  tft.setTextSize(1);
  tft.setTextColor(COLOR_TEXT_MUT);
  tft.setCursor(38, 22);
  tft.print("AQUA ANALYTICA");
  
  tft.drawFastHLine(25, 33, 110, COLOR_CARD_BG);

  tft.setTextColor(COLOR_TEXT_MUT);
  tft.setCursor(24, 82);
  tft.print("NETWORK DISCONNECTED");
  
  tft.setTextColor(COLOR_TDS);
  tft.setCursor(30, 96);
  tft.print("Reconnecting...");
}

void drawOfflineSpinner() {
  const int centerX = 80;
  const int centerY = 56;
  const int numDots = 8;
  
  const int dx[] = { 11,  8,  0, -8, -11, -8,  0,  8 };
  const int dy[] = {  0,  8, 11,  8,   0, -8, -11, -8 };
  
  for (int i = 0; i < numDots; i++) {
    int x = centerX + dx[i];
    int y = centerY + dy[i];
    
    if (i == spinnerFrame) {
      tft.fillCircle(x, y, 3, COLOR_TURB); 
    } 
    else if (i == (spinnerFrame - 1 + numDots) % numDots) {
      tft.fillCircle(x, y, 2, 0x03EF);     
    } 
    else if (i == (spinnerFrame - 2 + numDots) % numDots) {
      tft.fillCircle(x, y, 1, 0x01EF);     
    }
    else {
      tft.fillCircle(x, y, 1, 0x18C3);     
    }
  }
  spinnerFrame = (spinnerFrame + 1) % numDots;
}

void updateDashboardValues() {
  heartbeatState = !heartbeatState;
  tft.fillCircle(112, 6, 2, heartbeatState ? STATUS_OK : 0x02E0);

  for (int row = 0; row < 3; row++) {
    for (int col = 0; col < 2; col++) {
      int x = 4 + col * (cardW + gapX);
      int y = yStart + row * (cardH + gapY);
      int index = row * 2 + col;

      float val = getLiveValue(index);
      uint16_t valColor = getParameterColor(index);
      String valStr = getValueString(index, val) + " "; 

      tft.setTextSize(2);
      tft.setCursor(x + 7, y + 14);
      tft.setTextColor(valColor, COLOR_CARD_BG); 
      tft.print(valStr);
    }
  }
}

/* ================= CLOUD TRANSMISSION ENGINE ================= */
void sendToBackend(const SensorData& data) {
  if (WiFi.status() != WL_CONNECTED) return;

  // Protect backend from NaN crashes if data transmission has not started yet
  float safeEC = currentEC;
  if (isnan(safeEC)) {
    safeEC = 0.0;
  }
  
  float safeTemp = data.temp;
  if (isnan(safeTemp)) {
    safeTemp = 0.0;
  }

  float safeDO = data.do_val;
  if (isnan(safeDO)) {
    safeDO = 0.0;
  }

  float safePH = data.ph;
  if (isnan(safePH)) {
    safePH = 0.0;
  }

  HTTPClient http;
  WiFiClientSecure client; 

  client.setInsecure();

  http.begin(client, serverUrl);
  http.addHeader("Content-Type", "application/json");

  // Re-structured payload with unique JSON fields and safe values
  String payload = "{";
  payload += "\"deviceId\":\"NODE8266_01\",";
  payload += "\"tds\":" + String(data.tds) + ",";
  payload += "\"temp\":" + String(safeTemp, 2) + ",";
  payload += "\"turb\":" + String(data.turb) + ",";
  payload += "\"do_val\":" + String(safeDO, 3) + ",";
  payload += "\"ec\":" + String(safeEC, 1) + ","; 
  payload += "\"ph\":" + String(safePH, 2);
  payload += "}";

  Serial.println("\n>>> Uploading Telemetry to API...");
  Serial.print("Payload: ");
  Serial.println(payload);

  int httpCode = http.POST(payload);
  
  if (httpCode > 0) {
    String response = http.getString();
    Serial.print("HTTP Code: ");
    Serial.println(httpCode);
    Serial.print("Server Response: ");
    Serial.println(response);
  } else {
    Serial.print("HTTP Error: ");
    Serial.println(http.errorToString(httpCode).c_str());
  }
  Serial.println("=====================================\n");

  http.end();
}