#include <Wire.h>

// --- Cấu hình chân và địa chỉ ---
#define SDA_PIN 8
#define SCL_PIN 9
#define I2C_ADDR 0x3C

#define REG_IC_VER      0x01 // [cite: 985]
#define REG_FCX_STAT    0x06 // 
#define REG_PWR_STAT    0x07 // 

// Thanh ghi ADC (Buffered read)
#define REG_ADC_TYPE    0x3A // Chọn kênh ADC để đọc 
#define REG_ADC_H       0x3B // 8 bit cao của ADC 
#define REG_ADC_L       0x3C // 4 bit thấp của ADC 

// --- Kênh ADC (cho REG_ADC_TYPE)  ---
#define CHAN_VIN        1    // Đọc điện áp đầu vào
#define CHAN_VOUT       2    // Đọc điện áp đầu ra
#define CHAN_IOUT_C     3    // Đọc dòng Cổng 1 (Type-C)
#define CHAN_IOUT_A     4    // Đọc dòng Cổng 2 (Type-A)
#define CHAN_TEMP       6    // Đọc nhiệt độ (ADC thô)

// --- Giá trị LSB (Step) ---
#define VIN_STEP_MV     10.0f  // 
#define VOUT_STEP_MV    6.0f   // 
#define IOUT_STEP_MA    2.5f   // 

// Hàm đọc 1 byte 
uint8_t readReg(uint8_t reg) {
  Wire.beginTransmission(I2C_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return 0xFF;
  if (Wire.requestFrom(I2C_ADDR, 1) != 1) return 0xFF;
  return Wire.read();
}

// Hàm ghi 1 byte
void writeReg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(I2C_ADDR);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

/**
 * @brief Đọc giá trị ADC 12-bit từ một kênh cụ thể
 * Dựa trên phương thức đọc "buffered" (REG 0x3A, 0x3B, 0x3C)
 */
uint16_t readSW_ADC(uint8_t channel) {
  // 1. Ghi kênh muốn đọc vào REG 0x3A 
  writeReg(REG_ADC_TYPE, channel);

  // 2. Đặt con trỏ đọc tại REG 0x3B (byte cao) 
  Wire.beginTransmission(I2C_ADDR);
  Wire.write(REG_ADC_H);
  if (Wire.endTransmission(false) != 0) {
    Serial.println("Loi: Khong the set con tro doc!");
    return 0;
  }

  // 3. Đọc 2 byte (0x3B và 0x3C)
  if (Wire.requestFrom(I2C_ADDR, 2) != 2) {
    Serial.println("Loi: Khong the doc du lieu ADC!");
    return 0;
  }

  uint8_t high_byte = Wire.read(); // Đọc REG 0x3B 
  uint8_t low_byte = Wire.read();  // Đọc REG 0x3C 

  // 4. Ghép 12 bit: (high_byte[7:0] << 4) | (low_byte[3:0])
  uint16_t raw_adc = (high_byte << 4) | (low_byte & 0x0F);
  return raw_adc;
}

/**
 * @brief Đọc và in giao thức sạc nhanh từ REG 0x06
 */
void printProtocol(uint8_t fcx_stat) {
  uint8_t pd_ver = (fcx_stat >> 4) & 0x03; // Bit 5-4 
  uint8_t fc_proto = fcx_stat & 0x0F;      // Bit 3-0 

  Serial.print("Protocol: ");
  switch (fc_proto) { // 
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

  if (pd_ver == 1) Serial.println(" (PD 2.0)"); // 
  else if (pd_ver == 2) Serial.println(" (PD 3.0)"); // 
  else Serial.println();
}


void setup() {
  Serial.begin(115200);
  while (!Serial); delay(100);
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(100000); // 100kHz la tot

  Serial.println(F("\nDoc thong tin IC SW3517S"));
  Serial.println(F("========================================="));
  
  uint8_t ic_ver = readReg(REG_IC_VER) & 0x07; // [cite: 985]
  Serial.print("Tim thay IC, phien ban: "); Serial.println(ic_ver);
  
  delay(500);
}

void loop() {
  Serial.println(F("\n=== DU LIEU REALTIME ==="));

  // --- 1. Đọc Trạng Thái ---
  uint8_t pwr_stat = readReg(REG_PWR_STAT); // 
  uint8_t fcx_stat = readReg(REG_FCX_STAT); // 

  // Giải mã trạng thái nguồn 
  bool buck_on = (pwr_stat >> 2) & 0x01; // Bit 2
  bool port_a_on = (pwr_stat >> 1) & 0x01; // Bit 1 (Port 2 là Type-A)
  bool port_c_on = pwr_stat & 0x01;       // Bit 0 (Port 1 là Type-C)

  Serial.print("Trang thai: ");
  Serial.print(buck_on ? "BUCK (On) | " : "BUCK (Off) | ");
  Serial.print(port_c_on ? "PORT-C (On) | " : "PORT-C (Off) | ");
  Serial.print(port_a_on ? "PORT-A (On)" : "PORT-A (Off)");
  Serial.println();

  // In giao thức sạc
  printProtocol(fcx_stat);

  // --- 2. Đọc ADC (Sử dụng hàm đọc 12-bit) ---
  uint16_t vin_raw    = readSW_ADC(CHAN_VIN);
  uint16_t vout_raw   = readSW_ADC(CHAN_VOUT);
  uint16_t iout_c_raw = readSW_ADC(CHAN_IOUT_C);
  uint16_t iout_a_raw = readSW_ADC(CHAN_IOUT_A);

  // --- 3. Tính toán ---
  float vin    = vin_raw * VIN_STEP_MV / 1000.0f;     // 
  float vout   = vout_raw * VOUT_STEP_MV / 1000.0f;    // 
  float iout_c = iout_c_raw * IOUT_STEP_MA / 1000.0f;  // [cite: 1039]
  float iout_a = iout_a_raw * IOUT_STEP_MA / 1000.0f;  // [cite: 1042]
  
  // --- 4. In kết quả ---
  Serial.printf("VIN : %.2f V  (raw: %u)\n", vin, vin_raw);
  Serial.printf("VOUT: %.2f V  (raw: %u)\n", vout, vout_raw);
  
  Serial.println("--- Cong ra ---");
  Serial.printf("  Port-C: %.2f A  (raw: %u)\n", iout_c, iout_c_raw);
  Serial.printf("  Port-A: %.2f A  (raw: %u)\n", iout_a, iout_a_raw);

  float p_in  = vin * (iout_c + iout_a); // Iin không được cung cấp trực tiếp, 
                                         // chúng ta ước tính P_out
  float p_out = vout * (iout_c + iout_a);
  Serial.printf("POWER (OUT): %.2f W\n", p_out);

  Serial.println(F("========================================="));
  delay(2000);
}