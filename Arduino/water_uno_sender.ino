// final perfect sender




#include <OneWire.h>
#include <DallasTemperature.h>
#include <Arduino.h>
#include <stdint.h>
#include <Wire.h>

/* ================= STRUCT ================= */
struct __attribute__((packed)) SensorData {
  int32_t tds;
  float temp;
  int32_t turb;
  float do_val;
  float ph;
};

SensorData s1;

/* ================= DO SENSOR ================= */
#define DO_PIN A1
#define VREFDO 5000
#define ADC_RES 1024
#define TWO_POINT_CALIBRATION 1
const uint8_t START_BYTE = 0xAA;

#define CAL1_V (1600)  // mv
#define CAL1_T (25)    // ℃
#define CAL2_V (1300)  // mv  
#define CAL2_T (15)    // ℃

const uint16_t DO_Table[41] = {
  14460, 14220, 13820, 13440, 13090, 12740, 12420, 12110, 11810, 11530,
  11260, 11010, 10770, 10530, 10300, 10080, 9860, 9660, 9460, 9270,
  9080, 8900, 8730, 8570, 8410, 8250, 8110, 7960, 7820, 7690,
  7560, 7430, 7300, 7180, 7070, 6950, 6840, 6730, 6630, 6530, 6410
};

uint8_t Temperaturet;
uint16_t ADC_Raw;
uint16_t ADC_Voltage;

int16_t readDO(uint32_t voltage_mv, uint8_t temperature_c) {
#if TWO_POINT_CALIBRATION == 0
  uint16_t V_saturation = (uint32_t)CAL1_V + (uint32_t)35 * temperature_c - (uint32_t)CAL1_T * 35;
  return (voltage_mv * DO_Table[temperature_c] / V_saturation);
#else
  uint16_t V_saturation = (int16_t)((int8_t)temperature_c - CAL2_T) * ((uint16_t)CAL1_V - CAL2_V) / ((uint8_t)CAL1_T - CAL2_T) + CAL2_V;
  return (voltage_mv * DO_Table[temperature_c] / V_saturation);
#endif
}

/* ================= OTHER SENSORS ================= */
#define ONE_WIRE_BUS 2   //temp
#define TURBIDITY_ANALOG_PIN A2
#define TdsSensorPin A0
#define PhPin A3  
#define VREF 5.0
#define SCOUNT 30

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

int analogBuffer[SCOUNT];
int analogBufferTemp[SCOUNT];
int analogBufferIndex = 0;

float averageVoltage = 0;
float tdsValue = 0;
float temperature = 25.0;

unsigned long lastTempTime = 0;
unsigned long lastPrintTime = 0;

/* ================= MEDIAN FILTER ================= */
int getMedianNum(int bArray[], int iFilterLen) {
  int bTab[iFilterLen];
  for (byte i = 0; i < iFilterLen; i++) bTab[i] = bArray[i];

  for (int j = 0; j < iFilterLen - 1; j++) {
    for (int i = 0; i < iFilterLen - j - 1; i++) {
      if (bTab[i] > bTab[i + 1]) {
        int t = bTab[i];
        bTab[i] = bTab[i + 1];
        bTab[i + 1] = t;
      }
    }
  }
  return (iFilterLen & 1) ? bTab[iFilterLen / 2] : (bTab[iFilterLen / 2] + bTab[iFilterLen / 2 - 1]) / 2;
}

/* ================= PH SENSOR PROPERTIES ================= */
float calibration_value = 21.34 + 3.7;
int buffer_arr[10], temp;
float ph_act;
int phBufferIndex = 0;
unsigned long lastPHSampleTime = 0;

/* ================= SETUP ================= */
void setup() {
  Serial.begin(115200);
  sensors.begin();
  Wire.begin();

  pinMode(TdsSensorPin, INPUT);
  pinMode(DO_PIN, INPUT);
  pinMode(TURBIDITY_ANALOG_PIN, INPUT);
  pinMode(PhPin, INPUT);  // FIXED: Explicit pin assignment added
}

/* ================= LOOP ================= */
void loop() {
  /* ---- Temperature (Every 1s) ---- */
  if (millis() - lastTempTime >= 1000) {
    lastTempTime = millis();
    sensors.requestTemperatures();
    float t = sensors.getTempCByIndex(0);
    if (t > -10 && t < 85) {
      temperature = t;
      s1.temp = t;
    }
  }

  /* ---- TDS Sampling (Non-blocking Every 40ms) ---- */
  static unsigned long analogSampleTimepoint = millis();
  if (millis() - analogSampleTimepoint > 40) {
    analogSampleTimepoint = millis();
    analogBuffer[analogBufferIndex++] = analogRead(TdsSensorPin);
    if (analogBufferIndex == SCOUNT) analogBufferIndex = 0;
  }

  /* ---- pH Sampling (Non-blocking Every 30ms) ---- */
  if (millis() - lastPHSampleTime >= 30) {
    lastPHSampleTime = millis();
    buffer_arr[phBufferIndex++] = analogRead(PhPin);
    if (phBufferIndex >= 10) phBufferIndex = 0;
  }

  /* ---- Process and Send Packet Every 1s ---- */
  if (millis() - lastPrintTime >= 1000) {
    lastPrintTime = millis();

    // 1. Calculate TDS
    for (int i = 0; i < SCOUNT; i++) analogBufferTemp[i] = analogBuffer[i];
    averageVoltage = getMedianNum(analogBufferTemp, SCOUNT) * VREF / 1024.0;
    float compensationCoefficient = 1.0 + 0.02 * (temperature - 25.0);
    float compensationVoltage = averageVoltage / compensationCoefficient;
    tdsValue = (133.42 * compensationVoltage * compensationVoltage * compensationVoltage
                - 255.86 * compensationVoltage * compensationVoltage
                + 857.39 * compensationVoltage)
               * 0.5;
    s1.tds = (int32_t)tdsValue;

    // 2. Calculate DO
    int int_temp = (int)temperature;  // Cast to basic int for error-free logic constraint matching
    Temperaturet = (uint8_t)constrain(int_temp, 0, 40);

    ADC_Raw = analogRead(DO_PIN);
    ADC_Voltage = (uint32_t)VREFDO * ADC_Raw / ADC_RES;
    s1.do_val = (float)readDO(ADC_Voltage, Temperaturet) / 1000.0;

    // 3. Calculate Turbidity
    s1.turb = constrain(map(analogRead(TURBIDITY_ANALOG_PIN), 820, 0, 0, 100), 0, 100);

    // 4. Calculate pH
    int local_ph_buffer[10];
    for (int i = 0; i < 10; i++) local_ph_buffer[i] = buffer_arr[i];

    // Sort local copy
    for (int i = 0; i < 9; i++) {
      for (int j = i + 1; j < 10; j++) {
        if (local_ph_buffer[i] > local_ph_buffer[j]) {
          temp = local_ph_buffer[i];
          local_ph_buffer[i] = local_ph_buffer[j];
          local_ph_buffer[j] = temp;
        }
      }
    }

    unsigned long int ph_avgval = 0;
    for (int i = 2; i < 8; i++) {
      ph_avgval += local_ph_buffer[i];
    }
    float volt = (float)ph_avgval * 5.0 / 1024.0 / 6;
    ph_act = -5.70 * volt + calibration_value;
    s1.ph = ph_act;

    /* ---- SEND BINARY PACKET ---- */
    Serial.write(START_BYTE);
    Serial.write((uint8_t*)&s1, sizeof(SensorData));
  }
}