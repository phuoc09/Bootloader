# OTA Firmware Update cho STM32F103C8 qua ESP32-C5

Project này thực hiện cập nhật firmware OTA (Over-The-Air) cho **STM32F103C8 (Blue Pill)** thông qua **ESP32-C5** làm cầu nối WiFi ↔ UART. Người dùng upload file `.bin` qua trình duyệt web, ESP32 lưu vào SPIFFS rồi truyền tuần tự xuống STM32 qua UART, bootloader trên STM32 sẽ ghi vào Flash.

---

## 1. Kiến trúc tổng thể

```
[PC/Phone] --WiFi--> [ESP32-C5] --UART--> [STM32F103C8]
   Browser            WebServer            Bootloader + App
   (upload .bin)      SPIFFS               (Flash)
```

Quy trình hoạt động:

1. ESP32-C5 kết nối WiFi và mở web server tại port 80.
2. Người dùng truy cập IP của ESP32 → upload file `firmware.bin` → ESP32 lưu vào SPIFFS.
3. Người dùng bấm **Send to STM32** → ESP32 đọc file, chia thành các gói 256 byte, gửi qua UART1.
4. Bootloader STM32 (nằm ở `0x08000000`) nhận từng gói, kiểm tra CRC, ghi vào Flash bắt đầu từ `0x0800C800`.
5. Sau khi nhận xong (gói length = 0), bootloader nhảy sang application.

---

## 2. Bản đồ Flash của STM32F103C8

STM32F103C8 có 64KB Flash (page = 1KB), tổng 64 page (page 0–63). *Lưu ý: code dùng `END_PAGE = 127` cho dòng C8 thực tế chỉ có thể tới page 63; nếu bạn dùng C**B** (128KB) thì 127 là đúng — xem mục Lưu ý ở dưới.*

| Vùng                | Địa chỉ                  | Kích thước       | Nội dung                     |
|---------------------|--------------------------|------------------|------------------------------|
| Bootloader          | `0x08000000 – 0x0800C7FF`| 50KB (page 0–49) | Bootloader + UART receive    |
| Application         | `0x0800C800 – 0x0801FBFF`| ~78KB (page 50–127) | Firmware ứng dụng         |
| Data (tuỳ chọn)     | `0x0801FC00`             | 1 page           | `writeData()` ghi mảng `mData` |

Các macro trong `bootloader.c`:
```c
#define ADDR_APP_PROGRAM 0x800C800
#define START_PAGE 50
#define END_PAGE   127
```

---

## 3. Phần cứng & Đấu nối

### Linh kiện
- 1× **STM32F103C8 Blue Pill**
- 1× **ESP32-C5 DevKit**
- 1× **ST-Link V2** (để nạp bootloader và app lần đầu)
- Dây jumper, nguồn 3.3V/5V

### Sơ đồ nối UART (ESP32-C5 ↔ STM32 USART1)

| ESP32-C5      | STM32F103C8 | Ghi chú                          |
|---------------|-------------|----------------------------------|
| GPIO5 (TX1)   | PA10 (RX1)  | ESP gửi → STM nhận               |
| GPIO4 (RX1)   | PA9  (TX1)  | STM gửi → ESP nhận               |
| GND           | GND         | Phải nối chung GND               |
| 3V3 (tuỳ chọn)| 3V3         | Nếu dùng chung nguồn             |

LED chỉ thị ở **PA0** trên STM32 (đã cấu hình trong cả bootloader và app).

---

## 4. Cấu hình STM32CubeMX

Theo 2 ảnh bạn gửi:

**USART1**:
- Mode: Asynchronous
- Baud rate: **115200**
- Word Length: 8 bits
- Parity: None
- Stop bits: 1
- Pins: PA9 (TX), PA10 (RX)

**SYS**:
- Debug: **Serial Wire** (SWD)
- Timebase Source: SysTick

**RCC**:
- HSE: Crystal/Ceramic Resonator (8MHz)
- PLL × 9 → SYSCLK = 72MHz
- APB1 = 36MHz, APB2 = 72MHz

**GPIO**:
- PA0: Output Push-Pull, No Pull, Low speed (LED)

---

## 5. Build & Nạp lần đầu

### 5.1. Bootloader (Keil/STM32CubeIDE)

Tạo project STM32F103C8, copy `bootloader.c` (đổi tên thành `main.c`), `flash.c`, `flash.h` vào project. Build và nạp **bằng ST-Link** vào địa chỉ mặc định `0x08000000`.

### 5.2. Application

Tạo project STM32F103C8 thứ hai, copy `app.c` vào (đổi tên thành `main.c`). Quan trọng:

**(a) Sửa địa chỉ Flash trong linker script** (file `.ld` hoặc `.sct`):

Với GCC/CubeIDE (`STM32F103C8TX_FLASH.ld`):
```
FLASH (rx) : ORIGIN = 0x0800C800, LENGTH = 78K
```

Với Keil (sửa trong Options → Target):
```
IROM1 Start: 0x0800C800
IROM1 Size:  0x13800   (78KB)
```

**(b)** Trong `app.c` đã có dòng remap vector table:
```c
SCB->VTOR = 0x0800C800;
```
→ giữ nguyên, không xoá.

**(c)** Build → ra file `application.bin` (raw binary, không phải `.hex`). Trong CubeIDE: Project Properties → C/C++ Build → Settings → MCU Post Build → bật "Convert to binary file". Trong Keil: dùng `fromelf --bin --output app.bin app.axf`.

> Lần đầu để test có thể nạp `application.bin` trực tiếp bằng ST-Link vào `0x0800C800` để chắc chắn app chạy được trước khi test OTA.

---

## 6. ESP32-C5 (file `esp32c5.ino`)

### 6.1. Cài đặt
- Arduino IDE 2.x với **ESP32 board package ≥ 3.1** (yêu cầu cho ESP32-C5).
- Board: chọn **ESP32C5 Dev Module**.
- Partition scheme: **Default 4MB with spiffs**.
- Cài thư viện đi kèm board: `WiFi`, `WebServer`, `SPIFFS`, `FS` (đều đã có sẵn).

### 6.2. Sửa thông tin WiFi

Mở `esp32c5.ino`, sửa SSID/password ở đầu file:
```cpp
const char *ssid = "YOUR_WIFI_SSID";
const char *password = "YOUR_WIFI_PASSWORD";
```

### 6.3. Compile & nạp
Cắm USB → chọn cổng COM → Upload.

Sau khi nạp xong, mở **Serial Monitor (115200 baud)**, sẽ thấy:
```
WiFi connecting....
WiFi connected. IP:
192.168.x.x
```
→ Ghi nhớ IP này.

---

## 7. Quy trình OTA — Cập nhật firmware

### Bước 1: Reset STM32
Bootloader chạy ngay sau reset. Trong `bootloader.c`:
```c
send_byte(ACK);          // Báo cho ESP biết bootloader sẵn sàng
bootloader_loop();       // Vào vòng lặp nhận gói
```
Bootloader có timeout 5000ms ở mỗi vòng chờ START_BYTE — nếu không có dữ liệu nó vẫn tiếp tục chờ.

### Bước 2: Truy cập web server
Mở trình duyệt (PC hoặc điện thoại cùng WiFi) → vào `http://<IP_của_ESP32>`.

Giao diện:
```
┌──────────────────────────┐
│   OTA - STM32 - ESP32    │
│   [ Choose File ]        │
│   [  Upload BIN  ]       │
│   [ Send to STM32 ]      │
└──────────────────────────┘
```

### Bước 3: Upload file `.bin`
Bấm **Choose File** → chọn `application.bin` → bấm **Upload BIN**.
ESP32 sẽ lưu vào `/firmware.bin` trong SPIFFS. Serial Monitor in:
```
Upload...
Upload done.
```

### Bước 4: Gửi xuống STM32
Reset STM32 (để bootloader vào trạng thái `bootloader_loop`), rồi bấm **Send to STM32** trên web.

ESP32 sẽ:
1. Đọc `firmware.bin` từ SPIFFS, chia thành các gói 256 byte.
2. Mỗi gói có cấu trúc:
   ```
   [0xAA] [LEN_HI] [LEN_LO] [DATA...] [CRC_HI] [CRC_LO]
   ```
3. Gửi qua UART1, chờ ACK (`0x79`) hoặc NACK (`0x1F`). Retry tối đa 3 lần.
4. Cuối cùng gửi gói kết thúc `[0xAA] [0x00] [0x00]` báo hết file.

Serial Monitor ESP32 in:
```
packet[0] = AA packet[1] = 01 packet[2] = 00 crc = 12345
Sent block 0, waiting for ACK...
Block 0: OK
...
Finished sending firmware.bin to STM32.
```

### Bước 5: Bootloader nhảy sang App
Sau khi nhận gói length = 0, bootloader thoát vòng lặp:
```c
HAL_GPIO_WritePin(GPIOA, GPIO_PIN_0, 1);  // LED sáng 2s
HAL_Delay(2000);
enter_to_application();                    // Jump sang 0x0800C800
```

App mới chạy → LED PA0 nháy chu kỳ ~400ms (199ms ON, 199ms OFF).

---

## 8. Giao thức UART chi tiết

### Cấu trúc gói (ESP → STM32)
| Offset | Trường     | Kích thước | Giá trị                    |
|--------|------------|------------|----------------------------|
| 0      | START_BYTE | 1 byte     | `0xAA`                     |
| 1–2    | Length     | 2 byte BE  | Số byte data (≤ 256)       |
| 3..    | Data       | N byte     | Payload firmware           |
| 3+N    | CRC        | 2 byte BE  | Tổng cộng các byte data    |

### CRC
Đơn giản là tổng (sum) các byte:
```c
uint16_t crc = 0;
for (i = 0; i < len; i++) crc += data[i];
```
*Đây không phải CRC chuẩn (CRC-16/CCITT...) mà chỉ là checksum cộng dồn — đủ cho prototype, nhưng nên thay bằng CRC-16 thực sự cho production.*

### Phản hồi (STM32 → ESP)
- `0x79` = ACK (gói OK, ghi flash thành công)
- `0x1F` = NACK (CRC sai / timeout / length lỗi)

### Gói kết thúc
Length = 0 → bootloader gửi ACK rồi `break` khỏi vòng lặp → jump app.

---

## 9. Lưu ý quan trọng & Pitfall

### 9.1. `END_PAGE = 127` trên F103**C8**
F103C8 chính thức chỉ có 64KB (page 0–63). Tuy nhiên hầu hết chip C8 thực tế có 128KB (đặc tính "hidden" của dòng này), nên code đặt `END_PAGE = 127` vẫn chạy được. Nếu chip của bạn thực sự chỉ 64KB, sửa lại:
```c
#define END_PAGE 63
```
Và trong app linker, giảm LENGTH tương ứng.

### 9.2. CRC đặt sai trong `simpleCRC`
Trong `bootloader.c` hàm tính checksum trả về `uint16_t`, nhưng nếu data dài 256 byte với giá trị lớn thì có thể tràn — đây không phải lỗi chức năng (hai bên đều tràn giống nhau) nhưng dễ gây hiểu nhầm. Nên đổi sang CRC-16 chuẩn về sau.

### 9.3. Vector table của App
**Bắt buộc** giữ dòng `SCB->VTOR = 0x0800C800;` trong app, nếu không khi có interrupt sẽ nhảy về vector của bootloader → crash.

### 9.4. Linker của App
Nếu quên đổi `ORIGIN = 0x0800C800`, file `.bin` sinh ra vẫn có vector table tham chiếu địa chỉ `0x08000000` → app nạp xong sẽ HardFault ngay khi jump.

### 9.5. Nguồn cấp & GND
ESP32-C5 và STM32 **bắt buộc nối chung GND**. Mức logic UART đều là 3.3V nên không cần level shifter.

### 9.6. Thứ tự reset
Thứ tự đúng để OTA hoạt động:
1. Upload `.bin` lên ESP32 (qua web).
2. **Reset STM32** → bootloader bắt đầu nghe UART.
3. Trong vòng vài giây, bấm **Send to STM32** trên web.

Nếu bấm Send mà STM32 chưa ở chế độ bootloader_loop, sẽ mất gói.

### 9.7. Cache instruction
Cortex-M3 không có cache, nên không cần `__DSB()`/`__ISB()` sau khi flash. Tuy nhiên `enter_to_application()` đã DeInit RCC + HAL — nên giữ nguyên thứ tự đó để tránh interrupt còn pending.

---

## 10. Cấu trúc file project

```
.
├── README.md                ← file này
├── stm32_bootloader/
│   ├── Core/Src/main.c      ← từ bootloader.c
│   ├── Core/Src/flash.c
│   ├── Core/Inc/flash.h
│   └── STM32F103C8TX_FLASH.ld   (mặc định, ORIGIN = 0x08000000)
├── stm32_application/
│   ├── Core/Src/main.c      ← từ app.c
│   └── STM32F103C8TX_FLASH.ld   (sửa ORIGIN = 0x0800C800)
└── esp32c5/
    └── esp32c5.ino
```

---

## 11. Test nhanh

Sau khi nạp bootloader + nạp app lần đầu bằng ST-Link:
- LED PA0 phải nháy → app đang chạy.

Test OTA:
1. Sửa `app.c` đổi `HAL_Delay(199)` thành `HAL_Delay(500)` → build ra `application.bin` mới.
2. Upload qua web ESP32.
3. Reset STM32, bấm Send.
4. Sau 2 giây LED chuyển sang nhịp chậm hơn → OTA thành công.

---

## 12. Hướng phát triển tiếp

- Thay checksum cộng dồn → **CRC-16/CCITT** hoặc **CRC-32**.
- Thêm **magic number + version** ở đầu firmware để bootloader xác thực trước khi flash.
- Thêm **dual-bank**: giữ bản firmware cũ ở slot dự phòng, rollback khi app mới crash.
- Thêm chữ ký số (RSA/ECDSA) để chống firmware giả.
- Cho phép STM32 chủ động vào bootloader bằng nút bấm hoặc magic value trong RAM (thay vì luôn vào bootloader sau reset).
- Đổi web upload sang **HTTPS** + xác thực user.