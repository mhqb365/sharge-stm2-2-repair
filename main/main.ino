#include <Wire.h>

// ==========================================================
// --- CẤU HÌNH CHUNG GPIO I2C ---
// ==========================================================
#define SDA_PIN 6
#define SCL_PIN 7

// ==========================================================
// --- CẤU HÌNH SW3517S ---
// ==========================================================
#define SW3517S_ADDR 0x3C

#define REG_IC_VER      0x01
#define REG_FCX_STAT    0x06
#define REG_PWR_STAT    0x07

// Thanh ghi ADC (Buffered read)
#define REG_ADC_TYPE    0x3A
#define REG_ADC_H       0x3B
#define REG_ADC_L       0x3C

// --- Kênh ADC (cho REG_ADC_TYPE)  ---
#define CHAN_VIN        1
#define CHAN_VOUT       2
#define CHAN_IOUT_C     3
#define CHAN_IOUT_A     4
#define CHAN_TEMP       6

// --- Giá trị LSB (Step) ---
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
  float voltage;          // V
  float current;          // A
  float temperature;      // °C
  uint16_t remainingCapacity;  // mAh
  uint16_t fullCapacity;       // mAh
  uint16_t batteryStatus;      // bit flags
  uint16_t cycleCount;
  uint16_t relativeSOC;    // %
  uint16_t absoluteSOC;    // %
  float vcell1;           // V (cell thấp nhất)
  float vcell2;
  float vcell3;
  float vcell4;           // V (cell cao nhất)
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
    Serial.println("Error: Not found SW3517S ADC pointer");
    return 0;
  }

  if (Wire.requestFrom(SW3517S_ADDR, 2) != 2) {
    Serial.println("Error: Not found SW3517S ADC data");
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
  // Serial.printf("VIN : %.2f V  (raw: %u)\n", vin, vin_raw);
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

// Hàm đọc SMBus
uint16_t readRegister_BMS(uint8_t cmd) {
  for (int retry = 0; retry < MAX_RETRIES; retry++) {
    Wire.beginTransmission(BMS_ADDR);
    Wire.write(cmd);
    if (Wire.endTransmission(false) == 0) {
      if (Wire.requestFrom(BMS_ADDR, 2) == 2) {
        uint16_t low = Wire.read();
        uint16_t high = Wire.read();
        return (high << 8) | low;
      } else {
        Serial.printf("Retry %d: No data for cmd 0x%02X\n", retry + 1, cmd);
      }
    } else {
      Serial.printf("Retry %d: No ACK for cmd 0x%02X\n", retry + 1, cmd);
    }
    delay(10);
  }
  Serial.printf("Error: Failed read cmd 0x%02X after %d retries\n", cmd, MAX_RETRIES);
  return 0xFFFF;
}

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

  // Reset cờ phát hiện
  bmsDetected = false;
  // (Chúng ta sẽ kiểm tra sw3517s riêng)

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
        // cờ sw3517sDetected sẽ được đặt trong setup()
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

  raw = readRegister_BMS(CMD_REMAINING_CAPACITY);
  batteryData.remainingCapacity = (raw != 0xFFFF) ? raw : 0;

  raw = readRegister_BMS(CMD_FULL_CAPACITY);
  batteryData.fullCapacity = (raw != 0xFFFF) ? raw : 0;

  raw = readRegister_BMS(CMD_BATTERY_STATUS);
  batteryData.batteryStatus = (raw != 0xFFFF) ? raw : 0;

  raw = readRegister_BMS(CMD_CYCLE_COUNT);
  batteryData.cycleCount = (raw != 0xFFFF) ? raw : 0;

  raw = readRegister_BMS(CMD_RELATIVE_SOC);
  batteryData.relativeSOC = (raw != 0xFFFF) ? raw : 0;

  raw = readRegister_BMS(CMD_ABSOLUTE_SOC);
  batteryData.absoluteSOC = (raw != 0xFFFF) ? raw : 0;

  raw = readRegister_BMS(CMD_VCELL1);
  batteryData.vcell1 = (raw != 0xFFFF) ? raw / 1000.0f : 0.0f;

  raw = readRegister_BMS(CMD_VCELL2);
  batteryData.vcell2 = (raw != 0xFFFF) ? raw / 1000.0f : 0.0f;

  raw = readRegister_BMS(CMD_VCELL3);
  batteryData.vcell3 = (raw != 0xFFFF) ? raw / 1000.0f : 0.0f;

  raw = readRegister_BMS(CMD_VCELL4);
  batteryData.vcell4 = (raw != 0xFFFF) ? raw / 1000.0f : 0.0f;
}

// ==========================================================
// --- HÀM SETUP VÀ LOOP CHÍNH ---
// ==========================================================

void setup() {
  Serial.begin(115200);
  while (!Serial); delay(100);

  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(100000);
  delay(1000); // Chờ khởi tạo

  Serial.println(F("\n=== SW3517S & BMS Monitor ==="));
  
  // Chạy I2C scanner để phát hiện BMS (và các thiết bị khác)
  scanI2C();

  // Kiểm tra cụ thể SW3517S
  Serial.println(F("--- SW3517S (0x3C) ---"));
  uint8_t ic_ver = readReg_SW(REG_IC_VER);
  if (ic_ver == 0xFF) {
    Serial.println("Error: Not found SW3517S 0x3C");
    sw3517sDetected = false;
  } else {
    ic_ver &= 0x07; //
    Serial.print("Found SW3517S IC, version: "); Serial.println(ic_ver);
    sw3517sDetected = true;
  }
  
  Serial.println(F("========================================="));
  delay(500);
}

void loop() {
  
  // --- 1. XỬ LÝ BMS ---
  if (bmsDetected) {
    Serial.println(F("\n--- BMS (0x0B) ---"));
    readBatteryData();
    
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
  } else {
    Serial.println(F("\n--- Not found BMS. Trying to scan again... ---"));
    scanI2C(); // Sẽ cố gắng đặt bmsDetected = true nếu tìm thấy
  }

  // --- 2. XỬ LÝ SW3517S ---
  if (sw3517sDetected) {
    Serial.println(F("\n--- SW3517S (0x3C) ---"));
    readAndPrintSWData(); // Gọi hàm đã tách ra
  } else {
    Serial.println(F("\n--- Not found SW3517S. Trying to scan again... ---"));
    uint8_t ic_ver = readReg_SW(REG_IC_VER);
    if (ic_ver != 0xFF) {
      sw3517sDetected = true;
      Serial.println("Found SW3517S");
    }
  }

  Serial.println(F("========================================="));
  Serial.println(F("Delay 5s..."));
  delay(5000); // Sử dụng delay của BMS
}