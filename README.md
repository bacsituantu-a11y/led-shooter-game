# LED Shooter Game — Game bắn màu trên dây LED WS2812

Game bắn màu chạy trên **ESP32 WROOM**, điều khiển dây **70 LED WS2812**, 3 nút nhấn và loa qua module **MAX98357** (I2S).

Một chuỗi màu ngẫu nhiên chảy từ cuối dây về phía gốc. Người chơi bấm nút đúng màu để bắn viên đạn cùng màu:

- **Đúng màu đầu chuỗi** → đầu chuỗi nổ, chuỗi ngắn lại, **+10 điểm**.
- **Sai màu** → viên đạn dính vào đầu chuỗi, chuỗi dài thêm 1 led.
- **Chuỗi chạm gốc dây** → thua, dây **chớp đỏ 3 lần**, về menu.
- **Đủ 100 điểm** → qua bài, dây **chớp xanh lá 3 lần**, chuỗi chảy nhanh hơn.
- Tổng **10 bài**. Qua hết bài 10 thì dây chạy cầu vồng rồi về bài 1.

## Cách chơi

Bật nguồn, mạch phát một đoạn nhạc ngắn rồi vào menu. Số led **xanh lá** sáng liền nhau ở **cuối dây** cho biết bài hiện tại: 1 led = bài 1, 10 led = bài 10.

| Nút | Trong menu | Trong game |
|---|---|---|
| **Đỏ** | Tăng bài (tối đa 10, không quay vòng) | Bắn đạn màu đỏ |
| **Xanh lá** | Giảm bài (tối thiểu 1, không quay vòng) | Bắn đạn màu xanh lá |
| **Xanh dương** | Vào game ngay | Bắn đạn màu xanh dương |

## BOM — Danh sách vật tư

| # | Thành phần | SL |
|---|---|---|
| 1 | ESP32 WROOM DevKit | 1 |
| 2 | Dây LED WS2812 5V, 70 led | 1 |
| 3 | Module khuếch đại MAX98357 | 1 |
| 4 | Loa 4Ω 3W | 1 |
| 5 | Nút nhấn (xanh lá, xanh dương, đỏ) | 3 |
| 6 | Nguồn 5V ≥ 3A | 1 |
| 7 | Tụ điện 1000µF / 10V | 1 |
| 8 | Điện trở 330Ω | 1 |
| 9 | Dây nối | — |

## Bảng chân nối

### Dây LED WS2812

| Chân LED | Nối tới |
|---|---|
| DIN | **GPIO16** qua điện trở **330Ω** |
| 5V | Nguồn 5V |
| GND | GND nguồn |

Gắn tụ **1000µF/10V** ngay ở đầu dây LED (giữa 5V và GND, đúng chiều cực).

### Module MAX98357

| Chân MAX98357 | Nối tới |
|---|---|
| BCLK | **GPIO26** |
| LRC | **GPIO25** |
| DIN | **GPIO22** |
| VIN | 5V |
| GND | GND |
| GAIN | GND (khuếch đại 12dB) |
| SD | để trống |

Loa 4Ω 3W nối vào hai chân output của module.

### Nút nhấn

| Nút | Nối tới |
|---|---|
| Xanh lá | **GPIO32** |
| Xanh dương | **GPIO33** |
| Đỏ | **GPIO27** |

Chân còn lại của cả 3 nút nối **GND** — code dùng `INPUT_PULLUP` nên không cần điện trở ngoài.

### Nguồn

| Chân ESP32 | Nối tới |
|---|---|
| VIN | 5V |
| GND | GND chung với nguồn LED |

## Lưu ý khi lắp

- **Chung GND toàn mạch**: GND của ESP32, dây LED, MAX98357 và nguồn 5V phải nối chung. Thiếu điểm này thì LED nhấp nháy sai hoặc loa rít.
- **Không cấp nguồn cho dây LED từ cổng USB** của ESP32 — 70 led kéo dòng vượt xa khả năng của USB. Dùng nguồn 5V ≥ 3A riêng.
- Nếu **màu hiển thị sai** (đỏ ra xanh, xanh ra đỏ), đổi thứ tự màu trong `FastLED.addLeds`:
  ```cpp
  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);   // đổi GRB -> RGB
  ```

## Build và nạp

Cần **arduino-cli**, core **esp32:esp32 3.x** và thư viện **FastLED**.

```bash
# Cai core ESP32 (mot lan)
arduino-cli config add board_manager.additional_urls \
  https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
arduino-cli core update-index
arduino-cli core install esp32:esp32

# Cai thu vien
arduino-cli lib install FastLED

# Bien dich
arduino-cli compile --fqbn esp32:esp32:esp32 led_shooter_game

# Nap (sua COM8 thanh cong cua ban)
arduino-cli upload -p COM8 --fqbn esp32:esp32:esp32 led_shooter_game

# Xem log
arduino-cli monitor -p COM8 -c baudrate=115200 --raw
```

Thư mục sketch phải trùng tên file `.ino` — đó là yêu cầu của Arduino. Trên Linux/macOS cổng sẽ là dạng `/dev/ttyUSB0` hoặc `/dev/cu.usbserial-*` thay cho `COM8`.

Sketch dùng driver I2S mới (`driver/i2s_std.h`). API cũ `driver/i2s.h` **không dùng được** với core esp32 3.x — sẽ abort ngay khi boot với lỗi `CONFLICT! The new i2s driver can't work along with the legacy i2s driver`.

## Các tham số chỉnh nhanh

| Tham số | Mặc định | Ý nghĩa |
|---|---|---|
| `NUM_LEDS` | 70 | Số led trên dây |
| `BRIGHTNESS` | 70 | Độ sáng 0–255, giảm để tiết kiệm dòng |
| `CHAIN_START` | 10 | Độ dài chuỗi màu lúc bắt đầu |
| `BULLET_MS` | 15 | Tốc độ đạn, ms cho mỗi led (nhỏ = nhanh) |
| `stepMs()` | `max(80, 450 - (level-1)*35)` | Tốc độ chuỗi chảy theo bài, ms mỗi bước |

Ngoài ra `MAX_LEVEL` (10 bài) và `WIN_SCORE` (100 điểm qua bài) cũng nằm ngay đầu file.

## License

[MIT](LICENSE)
