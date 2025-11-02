#include <Wire.h>

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

// Địa chỉ BMS và các lệnh (chuẩn SBS)
#define BMS_ADDR 0x0B
#define CMD_TEMPERATURE       0x08  // °K /10
#define CMD_VOLTAGE           0x09  // mV
#define CMD_CURRENT           0x0A  // mA
#define CMD_RELATIVE_SOC      0x0D  // %
#define CMD_ABSOLUTE_SOC      0x0E  // %
#define CMD_REMAINING_CAPACITY 0x0F // mAh
#define CMD_FULL_CAPACITY     0x10  // mAh
#define CMD_BATTERY_STATUS    0x16  // Flags
#define CMD_CYCLE_COUNT       0x17  // Counts
#define CMD_VCELL1            0x3F  // Cell 1 (thấp nhất) - mV
#define CMD_VCELL2            0x3E
#define CMD_VCELL3            0x3D
#define CMD_VCELL4            0x3C  // Cell 4 (cao nhất) - mV

bool bmsDetected = false;
const int MAX_RETRIES = 3;  // Số lần thử lại khi đọc register

// Hàm đọc SMBus Read Word (LSB first, với retry)
uint16_t readRegister(uint8_t cmd) {
  for (int retry = 0; retry < MAX_RETRIES; retry++) {
    Wire.beginTransmission(BMS_ADDR);
    Wire.write(cmd);
    if (Wire.endTransmission(false) == 0) {  // Repeated start OK
      if (Wire.requestFrom(BMS_ADDR, 2) == 2) {
        uint16_t low = Wire.read();
        uint16_t high = Wire.read();
        return (high << 8) | low;  // LSB first
      } else {
        Serial.printf("Retry %d: No data for cmd 0x%02X\n", retry + 1, cmd);
      }
    } else {
      Serial.printf("Retry %d: No ACK for cmd 0x%02X\n", retry + 1, cmd);
    }
    delay(10);  // Delay nhỏ giữa các lần thử
  }
  Serial.printf("Error: Failed read cmd 0x%02X after %d retries\n", cmd, MAX_RETRIES);
  return 0xFFFF;  // Giá trị lỗi
}

int16_t readSignedRegister(uint8_t cmd) {
  return (int16_t)readRegister(cmd);
}

// Hàm decode Status sang tiếng Anh (dựa trên SBS spec)
String decodeStatus(uint16_t status) {
  if (status == 0xFFFF) return "Error reading Status";
  
  String desc = "";
  bool hasFlag = false;

  // Bit 15-0 theo SBS
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

  // Error code bit 3-0
  uint8_t error = status & 0x000F;
  if (error != 0) {
    desc += "Error code " + String(error) + ", ";
    hasFlag = true;
  }

  if (!hasFlag) desc = "Normal, No alarms";
  else if (desc.endsWith(", ")) desc.remove(desc.length() - 2);  // Xóa dấu phẩy cuối

  return desc;
}

// === HÀM KIỂM TRA I2C SCANNER ===
int scanI2C() {
  byte error, address;
  int nDevices = 0;
  Serial.println("Scanning I2C bus...");

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
    } else if (error == 4) {
      Serial.printf("Unknown error at 0x%02X\n", address);
    }
  }
  if (nDevices == 0) {
    Serial.println("No I2C devices found. Check wiring/pull-up/power!");
    bmsDetected = false;
  } else {
    Serial.printf("Scan complete: %d device(s) found.\n", nDevices);
  }
  Serial.println("-------------------");
  return nDevices;
}

void setup() {
  Serial.begin(115200);
  Wire.begin(8, 9);  // SDA=GPIO8, SCL=GPIO9 (thay đổi nếu board khác)
  Wire.setClock(100000);  // 100kHz cho SMBus
  delay(1000);
  Serial.println("BMS Reader Ready (Status in English)");

  // Chạy scanner một lần
  scanI2C();
}

void loop() {
  if (bmsDetected) {
    readBatteryData();

    // In dữ liệu (chỉ in nếu đọc thành công)
    if (batteryData.voltage != 0.0f || batteryData.current != 0.0f) {
      Serial.printf("Voltage: %.2f V\n", batteryData.voltage);
      Serial.printf("Current: %.2f A\n", batteryData.current);
      Serial.printf("Temp: %.2f °C\n", batteryData.temperature);
      Serial.printf("SOC Rel: %d %% | Abs: %d %%\n", batteryData.relativeSOC, batteryData.absoluteSOC);
      Serial.printf("Remain: %d mAh | Full: %d mAh\n", batteryData.remainingCapacity, batteryData.fullCapacity);
      Serial.printf("Cycles: %d\n", batteryData.cycleCount);
      
      // STATUS tiếng Anh
      String engStatus = decodeStatus(batteryData.batteryStatus);
      Serial.printf("Status: 0x%04X: %s\n", batteryData.batteryStatus, engStatus.c_str());
      
      Serial.printf("VCell: %.3f | %.3f | %.3f | %.3f V\n",
                    batteryData.vcell1, batteryData.vcell2, batteryData.vcell3, batteryData.vcell4);
      Serial.println("-------------------");
    } else {
      Serial.println("Read failed: All zero/error values.");
    }
  } else {
    Serial.println("BMS not detected. Retrying scan in 10s...");
    delay(10000);
    scanI2C();
  }
  
  delay(5000);  // Đọc mỗi 5 giây
}

void readBatteryData() {
  uint16_t raw;

  // Reset giá trị trước khi đọc
  batteryData = {0};

  raw = readRegister(CMD_VOLTAGE);
  batteryData.voltage = (raw != 0xFFFF) ? raw / 1000.0f : 0.0f;

  raw = readSignedRegister(CMD_CURRENT);
  batteryData.current = (raw != 0xFFFF) ? raw / 1000.0f : 0.0f;

  raw = readRegister(CMD_TEMPERATURE);
  batteryData.temperature = (raw != 0xFFFF) ? raw / 10.0f - 273.15f : 0.0f;

  raw = readRegister(CMD_REMAINING_CAPACITY);
  batteryData.remainingCapacity = (raw != 0xFFFF) ? raw : 0;

  raw = readRegister(CMD_FULL_CAPACITY);
  batteryData.fullCapacity = (raw != 0xFFFF) ? raw : 0;

  raw = readRegister(CMD_BATTERY_STATUS);
  batteryData.batteryStatus = (raw != 0xFFFF) ? raw : 0;

  raw = readRegister(CMD_CYCLE_COUNT);
  batteryData.cycleCount = (raw != 0xFFFF) ? raw : 0;

  raw = readRegister(CMD_RELATIVE_SOC);
  batteryData.relativeSOC = (raw != 0xFFFF) ? raw : 0;

  raw = readRegister(CMD_ABSOLUTE_SOC);
  batteryData.absoluteSOC = (raw != 0xFFFF) ? raw : 0;

  // Vcell: Thứ tự từ cell thấp đến cao
  raw = readRegister(CMD_VCELL1);  // Thấp nhất
  batteryData.vcell1 = (raw != 0xFFFF) ? raw / 1000.0f : 0.0f;

  raw = readRegister(CMD_VCELL2);
  batteryData.vcell2 = (raw != 0xFFFF) ? raw / 1000.0f : 0.0f;

  raw = readRegister(CMD_VCELL3);
  batteryData.vcell3 = (raw != 0xFFFF) ? raw / 1000.0f : 0.0f;

  raw = readRegister(CMD_VCELL4);  // Cao nhất
  batteryData.vcell4 = (raw != 0xFFFF) ? raw / 1000.0f : 0.0f;
}