# SHARGE STM2-2 Repair Project

## 📋 Giới thiệu

Dự án này là sửa chữa sạc dự phòng **SHARGE STM2-2** bị chết MCU. Do không thể thay thế MCU gốc, tôi đã quyết định sử dụng **ESP32-C3 SuperMini** để mô phỏng lại các chức năng của MCU ban đầu

## ⚖️ Miễn trừ trách nhiệm

Tác giả KHÔNG chịu trách nhiệm về:
- 🔥 Mọi thiệt hại về tài sản, người, hoặc thiết bị xảy ra trong quá trình thực hiện dự án này
- ⚡ Sự cố cháy nổ, chập điện, hoặc tai nạn liên quan đến pin lithium
- 💸 Hư hỏng thiết bị, linh kiện do thao tác không đúng kỹ thuật
- 🚫 Vi phạm bảo hành của nhà sản xuất gốc (SHARGE)
- ⚠️ Bất kỳ hậu quả pháp lý nào phát sinh từ việc sử dụng thông tin trong dự án này

**Bằng cách sử dụng thông tin từ dự án này, bạn đồng ý:**
- Tự chịu hoàn toàn trách nhiệm về mọi rủi ro
- Có đầy đủ kiến thức và kỹ năng cần thiết
- Tuân thủ các quy định an toàn khi làm việc với thiết bị điện tử và pin lithium
- Không yêu cầu bồi thường bất kỳ thiệt hại nào từ tác giả

**Khuyến cáo:**
- Nếu không có kinh nghiệm, hãy tìm đến dịch vụ sửa chữa chuyên nghiệp
- Luôn làm việc trong môi trường an toàn, có bình chữa cháy
- Ngắt kết nối pin trước khi hàn/tháo gỡ linh kiện


## ⚠️ Tình trạng hiện tại

**Đang trong quá trình phát triển**

### ✅ Tính năng đã hoàn thành
- ✅ Theo dõi thông số BMS (Battery Management System)
  - Điện áp, dòng điện, nhiệt độ pin
  - Dung lượng pin còn lại/tối đa
  - Tình trạng SOC (State of Charge)
  - Điện áp từng cell (4 cell)
  - Chu kỳ sạc và trạng thái pin
- ✅ Đọc thông tin từ SW3517S (USB Controller)
  - Điện áp đầu vào/ra
  - Dòng điện cổng USB-C và USB-A
  - Giao thức sạc nhanh (QC, PD, FCP, SCP, v.v.)
  - Công suất thời gian thực
- ✅ Điều khiển bật/tắt bằng nút nhấn
- ✅ LED báo trạng thái

## 🔧 Phần cứng

### Kết nối I2C
- **SDA**: GPIO 6
- **SCL**: GPIO 7
- **Nút nhấn**: GPIO 4 (INPUT_PULLUP)
- **LED**: GPIO 8 (Active LOW)
- **SW3517S Enable**: GPIO 5

### Linh kiện chính
- **MCU thay thế**: [ESP32-C3 SuperMini](https://s.shopee.vn/4VUL0G2kbb)
- **MCU gốc**: HC32F005C6UA (tháo bỏ)
- **Hàn mạch**: Câu dây như hình bên dưới


## 📊 Thông số đọc được

### BMS (Battery Management System)
- Điện áp pin: V
- Dòng điện: A (dương = sạc, âm = xả)
- Nhiệt độ: °C
- SOC (State of Charge): % (tương đối và tuyệt đối)
- Dung lượng: mAh (còn lại/tối đa)
- Số chu kỳ sạc
- Trạng thái pin (sạc, xả, đầy, quá nhiệt, v.v.)
- Điện áp từng cell (Cell 1-4)

### SW3517S (USB Controller)
- Điện áp đầu vào: V
- Điện áp đầu ra: V
- Dòng điện USB-C: A
- Dòng điện USB-A: A
- Công suất tổng: W
- Giao thức sạc nhanh: QC2.0, QC3.0, PD, FCP, SCP, PE, v.v.
- Trạng thái: BUCK, USB-C, USB-A

## 🚀 Cách sử dụng

### 1. Cài đặt Arduino IDE
- Cài đặt board ESP32 (ESP32-C3)
- Thư viện cần thiết: `Wire.h` (có sẵn)

### 2. Nạp code
```bash
# Mở file main/main.ino trong Arduino IDE
# Chọn board: ESP32C3 Dev Module
# Chọn cổng COM tương ứng
# Nhấn Upload
```

### 3. Vận hành
1. Khi khởi động, hệ thống ở trạng thái TẮT (LED tắt, SW3517S tắt)
2. Nhấn nút để BẬT - LED sáng, SW3517S bật, bắt đầu đọc dữ liệu
3. Nhấn nút lần nữa để TẮT
4. Dữ liệu được đọc và hiển thị qua Serial Monitor mỗi 5 giây

### 4. Xem dữ liệu
- Mở Serial Monitor với baud rate: **115200**
- Dữ liệu sẽ hiển thị định kỳ 5 giây một lần

## 📖 Tài liệu tham khảo

Tất cả datasheet được lưu trong thư mục `Datasheets/`:
- ESP32-C3 SuperMini: MCU thay thế
- HC32F005C6UA: MCU gốc (để tham khảo)
- SW3517S: USB Controller và Fast Charging Protocol
- SW35xx Register Map: Bản đồ thanh ghi để điều khiển

## 🔍 I2C Scan

Khi bật hệ thống, sẽ tự động quét I2C và phát hiện:
```
=== I2C Scan ===
Found: 0x0B --> BMS
Found: 0x3C --> SW3517S
Total: 2 device(s)
```

## 📝 Ghi chú kỹ thuật

### Công thức chuyển đổi ADC
- **VIN**: ADC × 10.0 mV
- **VOUT**: ADC × 6.0 mV
- **IOUT**: ADC × 2.5 mA

### Trạng thái pin
Code tự động giải mã các bit trạng thái:
- OverCharge (0x8000)
- TermCharge (0x4000)
- OverTemp (0x1000)
- TermDischarge (0x0800)
- Discharging (0x0040)
- FullyCharged (0x0020)
- FullyDischarged (0x0010)
- Error codes (0x000F)

## 🐛 Debug

Nếu gặp vấn đề:
1. Kiểm tra kết nối I2C (SDA, SCL, GND)
2. Kiểm tra điện áp cấp nguồn cho ESP32-C3
3. Kiểm tra Serial Monitor có nhận được log không
4. Xem kết quả I2C Scan để đảm bảo phát hiện đúng thiết bị

## 📄 License

Dự án cá nhân - Tự do sử dụng và chỉnh sửa cho mục đích phi thương mại. Vui lòng ghi nguồn khi chia sẻ

## 👤 Tác giả

- Creator: [mhqb365.com](https://mhqb365.com)
- Hỗ trợ ý tưởng & BMS: [Thái Thuận An](https://facebook.com/thai.thuan.an)

---

**Cập nhật lần cuối**: 16/11/2025  
**Phiên bản**: v3.1 (BMS Monitor)