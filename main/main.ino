// Compiler optimizations
#pragma GCC optimize ("O2")

#include <Wire.h>

// ==========================================================
// CONFIG
// ==========================================================
#define SDA_PIN 6
#define SCL_PIN 7

unsigned long lastI2CReadTime = 0;
const unsigned long i2cReadInterval = 5000;

// Button & LED
const int buttonPin = 4;
const int ledPin = 8;
const int sw3517sEnablePin = 5;

int buttonState = 0;
int lastButtonState = HIGH;
bool ledState = false;  // MẶC ĐỊNH TẮT
bool sw3517sState = false;  // MẶC ĐỊNH TẮT
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50;

// SW3517S
#define SW3517S_ADDR 0x3C
#define REG_IC_VER      0x01
#define REG_FCX_STAT    0x06
#define REG_PWR_STAT    0x07
#define REG_ADC_TYPE    0x3A
#define REG_ADC_H       0x3B

#define CHAN_VIN        1
#define CHAN_VOUT       2
#define CHAN_IOUT_C     3
#define CHAN_IOUT_A     4

#define VIN_STEP_MV     10.0f
#define VOUT_STEP_MV    6.0f
#define IOUT_STEP_MA    2.5f

bool sw3517sDetected = false;

// BMS
#define BMS_ADDR 0x0B

struct BatteryData {
  float voltage, current, temperature;
  uint16_t remainingCapacity, fullCapacity;
  uint16_t batteryStatus, cycleCount;
  uint16_t relativeSOC, absoluteSOC;
  float vcell1, vcell2, vcell3, vcell4;
} batteryData;

#define CMD_TEMPERATURE       0x08
#define CMD_VOLTAGE           0x09
#define CMD_CURRENT           0x0A
#define CMD_RELATIVE_SOC      0x0D
#define CMD_ABSOLUTE_SOC      0x0E
#define CMD_REMAINING_CAPACITY 0x0F
#define CMD_FULL_CAPACITY     0x10
#define CMD_BATTERY_STATUS    0x16
#define CMD_CYCLE_COUNT       0x17
#define CMD_VCELL1            0x3F
#define CMD_VCELL2            0x3E
#define CMD_VCELL3            0x3D
#define CMD_VCELL4            0x3C

bool bmsDetected = false;
const int MAX_RETRIES = 3;

// ==========================================================
// SW3517S Functions
// ==========================================================
uint8_t readReg_SW(uint8_t reg) {
  Wire.beginTransmission(SW3517S_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return 0xFF;
  return (Wire.requestFrom(SW3517S_ADDR, 1) == 1) ? Wire.read() : 0xFF;
}

void writeReg_SW(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(SW3517S_ADDR);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

uint16_t readSW_ADC(uint8_t channel) {
  writeReg_SW(REG_ADC_TYPE, channel);
  
  Wire.beginTransmission(SW3517S_ADDR);
  Wire.write(REG_ADC_H);
  if (Wire.endTransmission(false) != 0) return 0;
  if (Wire.requestFrom(SW3517S_ADDR, 2) != 2) return 0;

  return (Wire.read() << 4) | (Wire.read() & 0x0F);
}

void printProtocol(uint8_t fcx_stat) {
  const char* protocols[] = {"5V", "QC2.0", "QC3.0", "FCP", "SCP", 
                             "PD_FIX", "PD_PPS", "PE1.1", "PE2.0", 
                             "?", "SFCP"};
  uint8_t idx = fcx_stat & 0x0F;
  Serial.print("Proto: ");
  Serial.print(idx <= 10 ? protocols[idx] : "?");
  
  uint8_t pd = (fcx_stat >> 4) & 0x03;
  if (pd) Serial.printf(" (PD%d.0)", pd + 1);
  Serial.println();
}

void readAndPrintSWData() {
  uint8_t pwr = readReg_SW(REG_PWR_STAT);
  uint8_t fcx = readReg_SW(REG_FCX_STAT);

  Serial.printf("SW: BUCK(%s) C(%s) A(%s)\n",
                (pwr >> 2) & 1 ? "ON" : "OFF",
                pwr & 1 ? "ON" : "OFF",
                (pwr >> 1) & 1 ? "ON" : "OFF");
  printProtocol(fcx);

  float vin = readSW_ADC(CHAN_VIN) * VIN_STEP_MV / 1000.0f;
  float vout = readSW_ADC(CHAN_VOUT) * VOUT_STEP_MV / 1000.0f;
  float ic = readSW_ADC(CHAN_IOUT_C) * IOUT_STEP_MA / 1000.0f;
  float ia = readSW_ADC(CHAN_IOUT_A) * IOUT_STEP_MA / 1000.0f;
  
  Serial.printf("IN:%.2fV OUT:%.2fV | C:%.2fA A:%.2fA | Power:%.2fW\n", 
                vin, vout, ic, ia, vout * (ic + ia));
}

// ==========================================================
// BMS Functions
// ==========================================================
uint16_t readRegister_BMS(uint8_t cmd) {
  for (int retry = 0; retry < MAX_RETRIES; retry++) {
    Wire.beginTransmission(BMS_ADDR);
    Wire.write(cmd);
    if (Wire.endTransmission(false) == 0 && 
        Wire.requestFrom(BMS_ADDR, 2) == 2) {
      return Wire.read() | (Wire.read() << 8);
    }
    delay(10);
  }
  return 0xFFFF;
}

int16_t readSignedRegister_BMS(uint8_t cmd) {
  return (int16_t)readRegister_BMS(cmd);
}

void decodeStatus(uint16_t status, char* buffer) {
  if (status == 0xFFFF) {
    strcpy(buffer, "READ_ERROR");
    return;
  }
  
  buffer[0] = '\0';
  
  if (status & 0x8000) strcat(buffer, "OverCharge ");
  if (status & 0x4000) strcat(buffer, "TermCharge ");
  if (status & 0x1000) strcat(buffer, "OverTemp ");
  if (status & 0x0800) strcat(buffer, "TermDischarge ");
  if (status & 0x0040) strcat(buffer, "Discharging ");
  if (status & 0x0020) strcat(buffer, "FullyCharged ");
  if (status & 0x0010) strcat(buffer, "FullyDischarged ");
  
  uint8_t err = status & 0x0F;
  if (err) {
    char tmp[15];
    sprintf(tmp, "Error:%d", err);
    strcat(buffer, tmp);
  }
  
  if (buffer[0] == '\0') strcpy(buffer, "Charging"); // Normal
}

void scanI2C() {
  Serial.println("\n=== I2C Scan ===");
  bmsDetected = sw3517sDetected = false;
  int count = 0;

  for (byte addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("Found: 0x%02X", addr);
      if (addr == BMS_ADDR) {
        Serial.print(" --> BMS");
        bmsDetected = true;
      } else if (addr == SW3517S_ADDR) {
        Serial.print(" --> SW3517S");
        sw3517sDetected = true;
      }
      Serial.println();
      count++;
    }
  }
  Serial.printf("Total: %d device(s)\n", count);
}

void readBatteryData() {
  batteryData.voltage = readRegister_BMS(CMD_VOLTAGE) / 1000.0f;
  batteryData.current = readSignedRegister_BMS(CMD_CURRENT) / 1000.0f;
  
  uint16_t temp = readRegister_BMS(CMD_TEMPERATURE);
  batteryData.temperature = (temp != 0xFFFF) ? temp / 10.0f - 273.15f : 0.0f;
  
  batteryData.remainingCapacity = readRegister_BMS(CMD_REMAINING_CAPACITY);
  batteryData.fullCapacity = readRegister_BMS(CMD_FULL_CAPACITY);
  batteryData.batteryStatus = readRegister_BMS(CMD_BATTERY_STATUS);
  batteryData.cycleCount = readRegister_BMS(CMD_CYCLE_COUNT);
  batteryData.relativeSOC = readRegister_BMS(CMD_RELATIVE_SOC);
  batteryData.absoluteSOC = readRegister_BMS(CMD_ABSOLUTE_SOC);

  batteryData.vcell1 = readRegister_BMS(CMD_VCELL1) / 1000.0f;
  batteryData.vcell2 = readRegister_BMS(CMD_VCELL2) / 1000.0f;
  batteryData.vcell3 = readRegister_BMS(CMD_VCELL3) / 1000.0f;
  batteryData.vcell4 = readRegister_BMS(CMD_VCELL4) / 1000.0f;
}

void printBMSData() {
  char statusBuf[100];
  decodeStatus(batteryData.batteryStatus, statusBuf);
  
  Serial.println("\n--- BMS Data ---");
  Serial.printf("Voltage    : %.2f V\n", batteryData.voltage);
  Serial.printf("Current    : %.2f A\n", batteryData.current);
  Serial.printf("Temperature: %.1f C\n", batteryData.temperature);
  Serial.printf("SOC        : %d%% (Absolute: %d%%)\n",
                batteryData.relativeSOC, batteryData.absoluteSOC);
  Serial.printf("Capacity   : %d / %d mAh\n",
                batteryData.remainingCapacity, batteryData.fullCapacity);
  Serial.printf("Cycles     : %d\n", batteryData.cycleCount);
  Serial.printf("Status     : %s\n", statusBuf);
  Serial.printf("Cell 1-4   : %.3fV | %.3fV | %.3fV | %.3fV\n",
                batteryData.vcell1, batteryData.vcell2, 
                batteryData.vcell3, batteryData.vcell4);
}

// ==========================================================
// MAIN
// ==========================================================
void setup() {
  Serial.begin(115200);
  delay(100);

  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(100000);

  pinMode(buttonPin, INPUT_PULLUP);
  pinMode(ledPin, OUTPUT);
  pinMode(sw3517sEnablePin, OUTPUT);
  
  // MẶC ĐỊNH TẤT CẢ TẮT
  digitalWrite(ledPin, HIGH);  // LED OFF (active LOW, HIGH = OFF)
  digitalWrite(sw3517sEnablePin, LOW);  // SW3517S OFF
  
  delay(500);

  Serial.println("\n");
  Serial.println("================================");
  Serial.println("  ESP32-C3 BMS Monitor v3.1");
  Serial.println("================================");
  Serial.println("\nDefault State: LED OFF | SW3517S OFF");
  Serial.println("Press button to turn ON and start monitoring");
  Serial.println("================================\n");
  
  lastI2CReadTime = millis();
}

void loop() {
  // Button handling
  int reading = digitalRead(buttonPin);
  
  if (reading != lastButtonState) {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (reading != buttonState) {
      buttonState = reading;

      // Button pressed (LOW because of pull-up)
      if (buttonState == LOW) {
        ledState = !ledState;
        sw3517sState = !sw3517sState;
        
        digitalWrite(ledPin, ledState ? LOW : HIGH);
        digitalWrite(sw3517sEnablePin, sw3517sState ? HIGH : LOW);
        
        Serial.println("\n========================================");
        Serial.printf(">>> Button Pressed! <<<\n");
        Serial.printf("LED      : %s\n", ledState ? "ON" : "OFF");
        Serial.printf("SW3517S  : %s\n", sw3517sState ? "ON" : "OFF");
        Serial.printf("I2C Read : %s\n", sw3517sState ? "ENABLED" : "DISABLED");
        Serial.println("========================================\n");
        
        // Nếu vừa bật ON, thực hiện scan I2C ngay lập tức
        if (sw3517sState) {
          delay(100);  // Chờ SW3517S khởi động
          scanI2C();
          
          if (sw3517sDetected) {
            uint8_t ver = readReg_SW(REG_IC_VER) & 0x07;
            Serial.printf("\nSW3517S Version: %d\n", ver);
          }
          
          // Reset timer để đọc ngay
          lastI2CReadTime = millis();
        }
      }
    }
  }

  lastButtonState = reading;

  // CHỈ ĐỌC I2C KHI SW3517S ĐANG BẬT
  if (sw3517sState && (millis() - lastI2CReadTime >= i2cReadInterval)) {
    lastI2CReadTime = millis();
    
    Serial.println("\n========================================");
    Serial.println("        Reading I2C Data...");
    Serial.println("========================================");
    
    if (bmsDetected) {
      readBatteryData();
      printBMSData();
    } else {
      Serial.println("\n[!] BMS not detected");
    }
    
    if (sw3517sDetected) {
      Serial.println("\n--- SW3517S Data ---");
      readAndPrintSWData();
    } else {
      Serial.println("\n[!] SW3517S not detected");
    }
    
    Serial.println("========================================\n");
  }
}