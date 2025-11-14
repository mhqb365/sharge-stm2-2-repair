#include <Wire.h>

// ==========================================================
// --- CẤU HÌNH CHUNG ---
// ==========================================================
#define SDA_PIN 6
#define SCL_PIN 7

// Biến cho hoạt động non-blocking (I2C)
unsigned long lastI2CReadTime = 0;
// Đặt lại khoảng thời gian đọc I2C là 5000ms (5 giây)
const unsigned long i2cReadInterval = 5000; 

// ==========================================================
// --- CẤU HÌNH BUTTON VÀ LED ---
// ==========================================================
const int buttonPin = 4;  // SMD button connected to GPIO4
const int ledPin = 8;     // Onboard LED on ESP32-C3 Super Mini connected to GPIO8 (active LOW)

// Variables for button/LED
int buttonState = 0;      // Current state of the button
int lastButtonState = HIGH; // Previous state of the button (pull-up, so HIGH is default)
bool ledState = true;     // Current state of the LED (true = on, ĐẢO NGƯỢC ĐỂ SÁNG MẶC ĐỊNH)
unsigned long lastDebounceTime = 0;  // Last time the button was toggled
const unsigned long debounceDelay = 50; // Debounce delay in milliseconds

// ==========================================================
// --- CẤU HÌNH SW3517S ---
// ==========================================================
#define SW3517S_ADDR 0x3C
#define REG_IC_VER      0x01
#define REG_FCX_STAT    0x06
#define REG_PWR_STAT    0x07
#define REG_ADC_TYPE    0x3A
#define REG_ADC_H       0x3B
#define REG_ADC_L       0x3C
#define CHAN_VIN        1
#define CHAN_VOUT       2
#define CHAN_IOUT_C     3
#define CHAN_IOUT_A     4
#define CHAN_TEMP       6
#define VIN_STEP_MV     10.0f
#define VOUT_STEP_MV    6.0f 
#define IOUT_STEP_MA    2.5f 

bool sw3517sDetected = false;

// ==========================================================
// --- CẤU HÌNH BMS ---
// ==========================================================
#define BMS_ADDR 0x0B

// Cấu trúc batteryData
struct BatteryData {
  float voltage; float current; float temperature;
  uint16_t remainingCapacity; uint16_t fullCapacity; 
  uint16_t batteryStatus; uint16_t cycleCount;
  uint16_t relativeSOC; uint16_t absoluteSOC;
  float vcell1; float vcell2; float vcell3; float vcell4;
} batteryData;

// Địa chỉ lệnh (chuẩn SBS)
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
// --- CÁC HÀM CỦA SW3517S ---
// ==========================================================

// Hàm đọc 1 byte (SW3517S)
uint8_t readReg_SW(uint8_t reg) {
  Wire.beginTransmission(SW3517S_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return 0xFF;
  if (Wire.requestFrom(SW3517S_ADDR, 1) != 1) return 0xFF;
  return Wire.read();
}

// Hàm ghi 1 byte (SW3517S)
void writeReg_SW(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(SW3517S_ADDR);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

// Đọc giá trị ADC 12-bit (SW3517S)
uint16_t readSW_ADC(uint8_t channel) {
  writeReg_SW(REG_ADC_TYPE, channel);
  
  Wire.beginTransmission(SW3517S_ADDR);
  Wire.write(REG_ADC_H);
  if (Wire.endTransmission(false) != 0) {
    return 0;
  }

  if (Wire.requestFrom(SW3517S_ADDR, 2) != 2) {
    return 0;
  }

  uint8_t high_byte = Wire.read();
  uint8_t low_byte = Wire.read();

  uint16_t raw_adc = (high_byte << 4) | (low_byte & 0x0F);
  return raw_adc;
}

// Đọc và in giao thức sạc nhanh (SW3517S)
void printProtocol(uint8_t fcx_stat) {
  uint8_t pd_ver = (fcx_stat >> 4) & 0x03;
  uint8_t fc_proto = fcx_stat & 0x0F;

  Serial.print("Protocol: ");
  switch (fc_proto) {
    case 1: Serial.print("QC2.0"); break;
    case 2: Serial.print("QC3.0"); break;
    case 3: Serial.print("FCP"); break;
    case 4: Serial.print("SCP"); break;
    case 5: Serial.print("PD FIX"); break;
    case 6: Serial.print("PD PPS"); break;
    case 7: Serial.print("PE1.1"); break;
    case 8: Serial.print("PE2.0"); break;
    case 0x0A: Serial.print("SFCP"); break;
    default: Serial.print("5V (hoac khong co)"); break;
  }

  if (pd_ver == 1) Serial.println(" (PD 2.0)");
  else if (pd_ver == 2) Serial.println(" (PD 3.0)");
  else Serial.println();
}

// Hàm đọc dữ liệu SW3517S
void readAndPrintSWData() {
  // 1. Đọc Trạng Thái
  uint8_t pwr_stat = readReg_SW(REG_PWR_STAT);
  uint8_t fcx_stat = readReg_SW(REG_FCX_STAT);

  // Giải mã trạng thái nguồn
  bool buck_on = (pwr_stat >> 2) & 0x01;
  bool port_a_on = (pwr_stat >> 1) & 0x01;
  bool port_c_on = pwr_stat & 0x01;

  Serial.print("Status: ");
  Serial.print(buck_on ? "BUCK (On) | " : "BUCK (Off) | ");
  Serial.print(port_c_on ? "PORT-C (On) | " : "PORT-C (Off) | ");
  Serial.print(port_a_on ? "PORT-A (On)" : "PORT-A (Off)");
  Serial.println();
  printProtocol(fcx_stat);

  // 2. Đọc ADC
  uint16_t vin_raw    = readSW_ADC(CHAN_VIN);
  uint16_t vout_raw   = readSW_ADC(CHAN_VOUT);
  uint16_t iout_c_raw = readSW_ADC(CHAN_IOUT_C);
  uint16_t iout_a_raw = readSW_ADC(CHAN_IOUT_A);
  
  // 3. Tính toán
  float vin    = vin_raw * VIN_STEP_MV / 1000.0f;
  float vout   = vout_raw * VOUT_STEP_MV / 1000.0f;
  float iout_c = iout_c_raw * IOUT_STEP_MA / 1000.0f;
  float iout_a = iout_a_raw * IOUT_STEP_MA / 1000.0f;
  
  // 4. In kết quả
  Serial.printf("VIN : %.2f V  (raw: %u)\n", vin, vin_raw);
  Serial.printf("VOUT: %.2f V  (raw: %u)\n", vout, vout_raw);
  Serial.println("--- Output 2---");
  Serial.printf("  Port-C: %.2f A  (raw: %u)\n", iout_c, iout_c_raw);
  Serial.printf("  Port-A: %.2f A  (raw: %u)\n", iout_a, iout_a_raw);
  float p_out = vout * (iout_c + iout_a);
  Serial.printf("POWER (OUT): %.2f W\n", p_out);
}

// ==========================================================
// --- CÁC HÀM CỦA BMS ---
// ==========================================================

// Hàm đọc SMBus (2 bytes)
uint16_t readRegister_BMS(uint8_t cmd) {
  for (int retry = 0; retry < MAX_RETRIES; retry++) {
    Wire.beginTransmission(BMS_ADDR);
    Wire.write(cmd);
    if (Wire.endTransmission(false) == 0) {
      if (Wire.requestFrom(BMS_ADDR, 2) == 2) {
        uint16_t low = Wire.read();
        uint16_t high = Wire.read();
        return (high << 8) | low;
      }
    }
    delay(10);
  }
  return 0xFFFF; // Trả về giá trị lỗi
}

// Hàm đọc SMBus (Signed)
int16_t readSignedRegister_BMS(uint8_t cmd) {
  return (int16_t)readRegister_BMS(cmd);
}

// Hàm decode Status
String decodeStatus(uint16_t status) {
  if (status == 0xFFFF) return "Error reading Status";
  String desc = "";
  bool hasFlag = false;

  if (status & 0x8000) { desc += "Overcharge, "; hasFlag = true; }
  if (status & 0x4000) { desc += "Terminate charge, "; hasFlag = true; }
  if (status & 0x1000) { desc += "Over-temperature, "; hasFlag = true; }
  if (status & 0x0800) { desc += "Terminate discharge, "; hasFlag = true; }
  if (status & 0x0200) { desc += "Remaining capacity, "; hasFlag = true; }
  if (status & 0x0100) { desc += "Remaining time, "; hasFlag = true; }
  if (status & 0x0080) { desc += "Initialized, "; hasFlag = true; }
  if (status & 0x0040) { desc += "Discharging, "; hasFlag = true; }
  if (status & 0x0020) { desc += "Fully charged, "; hasFlag = true; }
  if (status & 0x0010) { desc += "Fully discharged, "; hasFlag = true; }

  uint8_t error = status & 0x000F;
  if (error != 0) {
    desc += "Error code " + String(error) + ", ";
    hasFlag = true;
  }

  if (!hasFlag) desc = "Normal, No alarms";
  else if (desc.endsWith(", ")) desc.remove(desc.length() - 2);

  return desc;
}

// Hàm quét I2C (BMS)
void scanI2C() {
  byte error, address;
  int nDevices = 0;
  Serial.println("Scanning I2C bus...");

  bmsDetected = false;
  sw3517sDetected = false;

  for (address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    error = Wire.endTransmission();

    if (error == 0) {
      Serial.printf("I2C device found at 0x%02X\n", address);
      nDevices++;
      if (address == BMS_ADDR) {
        Serial.println(">>> BMS detected at 0x0B! <<<");
        bmsDetected = true;
      }
      if (address == SW3517S_ADDR) {
        Serial.println(">>> SW3517S detected at 0x3C! <<<");
        sw3517sDetected = true;
      }
    } else if (error == 4) {
      Serial.printf("Unknown error at 0x%02X\n", address);
    }
  }
  if (nDevices == 0) {
    Serial.println("No I2C devices found. Check wiring/pull-up/power!");
  } else {
    Serial.printf("Scan complete: %d device(s) found\n", nDevices);
  }
  Serial.println("-------------------");
}

// Hàm đọc dữ liệu BMS
void readBatteryData() {
  uint16_t raw;
  batteryData = {0};

  raw = readRegister_BMS(CMD_VOLTAGE);
  batteryData.voltage = (raw != 0xFFFF) ? raw / 1000.0f : 0.0f;

  raw = readSignedRegister_BMS(CMD_CURRENT);
  batteryData.current = (raw != 0xFFFF) ? raw / 1000.0f : 0.0f;

  raw = readRegister_BMS(CMD_TEMPERATURE);
  batteryData.temperature = (raw != 0xFFFF) ? raw / 10.0f - 273.15f : 0.0f;

  batteryData.remainingCapacity = readRegister_BMS(CMD_REMAINING_CAPACITY);
  batteryData.fullCapacity = readRegister_BMS(CMD_FULL_CAPACITY);
  batteryData.batteryStatus = readRegister_BMS(CMD_BATTERY_STATUS);
  batteryData.cycleCount = readRegister_BMS(CMD_CYCLE_COUNT);
  batteryData.relativeSOC = readRegister_BMS(CMD_RELATIVE_SOC);
  batteryData.absoluteSOC = readRegister_BMS(CMD_ABSOLUTE_SOC);

  // Cell Voltages
  batteryData.vcell1 = (readRegister_BMS(CMD_VCELL1) != 0xFFFF) ? readRegister_BMS(CMD_VCELL1) / 1000.0f : 0.0f;
  batteryData.vcell2 = (readRegister_BMS(CMD_VCELL2) != 0xFFFF) ? readRegister_BMS(CMD_VCELL2) / 1000.0f : 0.0f;
  batteryData.vcell3 = (readRegister_BMS(CMD_VCELL3) != 0xFFFF) ? readRegister_BMS(CMD_VCELL3) / 1000.0f : 0.0f;
  batteryData.vcell4 = (readRegister_BMS(CMD_VCELL4) != 0xFFFF) ? readRegister_BMS(CMD_VCELL4) / 1000.0f : 0.0f;
}

// Hàm in dữ liệu BMS
void printBMSData() {
    Serial.println(F("\n--- BMS (0x0B) ---"));
    if (batteryData.voltage != 0.0f || batteryData.current != 0.0f) {
      Serial.printf("Voltage: %.2f V\n", batteryData.voltage);
      Serial.printf("Current: %.2f A\n", batteryData.current);
      Serial.printf("Temp: %.2f °C\n", batteryData.temperature);
      Serial.printf("SOC Rel: %d %% | Abs: %d %%\n", batteryData.relativeSOC, batteryData.absoluteSOC);
      Serial.printf("Remain: %d mAh | Full: %d mAh\n", batteryData.remainingCapacity, batteryData.fullCapacity);
      Serial.printf("Cycles: %d\n", batteryData.cycleCount);
      String engStatus = decodeStatus(batteryData.batteryStatus);
      Serial.printf("Status: 0x%04X: %s\n", batteryData.batteryStatus, engStatus.c_str());
      Serial.printf("VCell: %.3f | %.3f | %.3f | %.3f V\n",
                    batteryData.vcell1, batteryData.vcell2, batteryData.vcell3, batteryData.vcell4);
    } else {
      Serial.println("Error: BMS read returned 0/error.");
    }
}

// ==========================================================
// --- HÀM SETUP VÀ LOOP CHÍNH ---
// ==========================================================

void setup() {
  Serial.begin(115200);
  while (!Serial); delay(100);

  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(100000);

  // Cấu hình button và LED
  pinMode(buttonPin, INPUT_PULLUP); // Button with internal pull-up resistor
  pinMode(ledPin, OUTPUT);          // LED as output
  digitalWrite(ledPin, ledState ? LOW : HIGH); // Set initial LED state (LOW = on, active LOW)
  
  delay(1000); // Chờ khởi tạo

  Serial.println(F("\n=== SW3517S & BMS Monitor ==="));
  
  // 1. CHỈ CHẠY SCAN I2C MỘT LẦN TRONG SETUP
  scanI2C();

  // 2. Kiểm tra xác nhận SW3517S
  Serial.println(F("--- SW3517S (0x3C) ---"));
  if (sw3517sDetected) {
      uint8_t ic_ver = readReg_SW(REG_IC_VER);
      if (ic_ver == 0xFF) {
        Serial.println("Error: Failed to confirm SW3517S at 0x3C.");
        sw3517sDetected = false;
      } else {
        ic_ver &= 0x07;
        Serial.print("Found SW3517S IC, version: "); Serial.println(ic_ver);
      }
  } else {
      Serial.println("SW3517S not found during initial scan.");
  }
  
  // 3. Đọc dữ liệu ban đầu cho BMS/SW3517S
  if (bmsDetected) readBatteryData();
  if (sw3517sDetected) readAndPrintSWData();
  
  lastI2CReadTime = millis(); // Thiết lập thời gian đọc I2C ban đầu
  
  Serial.println(F("========================================="));
}

void loop() {
  // --- 1. XỬ LÝ BUTTON VÀ LED (NON-BLOCKING) ---
  int reading = digitalRead(buttonPin);

  // Check if the button state has changed
  if (reading != lastButtonState) {
    lastDebounceTime = millis(); // Reset the debounce timer
  }

  // If the debounce period has passed, process the button state
  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (reading != buttonState) {
      buttonState = reading;

      // If the button is pressed (LOW due to pull-up)
      if (buttonState == LOW) {
        ledState = !ledState; // Toggle LED state
        digitalWrite(ledPin, ledState ? LOW : HIGH); // Update LED (LOW = on, HIGH = off)
        Serial.println(ledState ? "LED ON" : "LED OFF");
      }
    }
  }

  lastButtonState = reading; // Save the current button state for the next loop

  // --- 2. XỬ LÝ I2C (CHẠY ĐỊNH KỲ 5S, NON-BLOCKING) ---
  unsigned long currentMillis = millis();
  
  // KHÔNG DÙNG DELAY. Chỉ chạy khi đã đủ 5 giây
  if (currentMillis - lastI2CReadTime >= i2cReadInterval) {
    lastI2CReadTime = currentMillis; // Reset timer

    Serial.println(F("\n========================================="));
    Serial.println(F("Reading I2C data (Interval 5s)..."));

    // --- 2a. XỬ LÝ BMS ---
    if (bmsDetected) {
      readBatteryData();
      printBMSData();
    } else {
      Serial.println(F("\n--- BMS (0x0B) Not Found. Scan only runs in setup(). ---"));
    }

    // --- 2b. XỬ LÝ SW3517S ---
    if (sw3517sDetected) {
      Serial.println(F("\n--- SW3517S (0x3C) ---"));
      readAndPrintSWData();
    } else {
      Serial.println(F("\n--- SW3517S (0x3C) Not Found. Scan only runs in setup(). ---"));
    }
    
    Serial.println(F("========================================="));
  }
}