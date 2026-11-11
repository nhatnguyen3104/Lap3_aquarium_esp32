# 🌊 Smart Aquarium IoT – Hệ thống giám sát và điều khiển bể nước thông minh

<p align="center">
  <strong>ESP32 • FreeRTOS • MQTT • EMQX • Wokwi • Web Dashboard</strong>
</p>

## 📌 1. Giới thiệu dự án

**Smart Aquarium IoT** là dự án xây dựng mô hình bể nước thông minh sử dụng ESP32 để giám sát các thông số môi trường và điều khiển thiết bị thông qua Internet.

Hệ thống sử dụng cảm biến siêu âm HC-SR04 để đo mực nước, cảm biến DS18B20 để đo nhiệt độ. Dữ liệu được ESP32 xử lý và gửi lên MQTT Broker EMQX, sau đó hiển thị trên giao diện Web Dashboard.

Ngoài chức năng giám sát, người dùng còn có thể gửi lệnh từ website về ESP32 để điều khiển máy bơm và van xả thông qua giao thức MQTT.

Dự án được xây dựng và kiểm thử trên môi trường mô phỏng **Wokwi**, kết hợp với giao diện website chạy trên trình duyệt.

Link wokwi: https://wokwi.com/projects/480269981900002305
Link web: https://nhatnguyen0201.github.io/Lap3_aquarium_esp32/web/index.html

## 🎯 2. Mục tiêu dự án

- Xây dựng mô hình IoT giám sát mực nước và nhiệt độ.
- Đọc dữ liệu từ cảm biến HC-SR04 và DS18B20 bằng ESP32.
- Sử dụng FreeRTOS để quản lý các tác vụ độc lập.
- Truyền dữ liệu cảm biến lên MQTT Broker EMQX.
- Xây dựng giao diện Web Dashboard để theo dõi trạng thái hệ thống.
- Cho phép điều khiển máy bơm và van xả từ website.
- Hỗ trợ hai chế độ hoạt động AUTO và MANUAL.
- Thiết lập giao tiếp MQTT hai chiều giữa ESP32 và website.

## 🛠️ 3. Công nghệ và thiết bị sử dụng

### Phần cứng mô phỏng

| Thiết bị | Chức năng |
|---|---|
| ESP32 DevKit V1 | Bộ điều khiển trung tâm |
| HC-SR04 | Đo khoảng cách đến mặt nước |
| DS18B20 | Đo nhiệt độ nước |
| LED | Mô phỏng trạng thái máy bơm |
| LED | Mô phỏng trạng thái van xả |
| Điện trở 220Ω | Hạn dòng cho LED |
| Điện trở 4.7kΩ | Điện trở kéo lên cho DS18B20 |

### Công nghệ phần mềm

| Công nghệ | Vai trò |
|---|---|
| Arduino IDE / Arduino Framework | Lập trình ESP32 |
| FreeRTOS | Quản lý đa nhiệm |
| MQTT | Giao tiếp truyền nhận dữ liệu |
| EMQX | MQTT Broker |
| Wokwi | Mô phỏng phần cứng |
| HTML, CSS, JavaScript | Xây dựng giao diện website |
| MQTT.js | Kết nối MQTT từ trình duyệt |

## 🔌 4. Sơ đồ kết nối phần cứng

Các chân GPIO được cấu hình trên ESP32 như sau:

| Thiết bị | Chân thiết bị | GPIO ESP32 |
|---|---|---|
| HC-SR04 | TRIG | GPIO 5 |
| HC-SR04 | ECHO | GPIO 18 |
| DS18B20 | DATA | GPIO 4 |
| LED máy bơm | Anode | GPIO 25 |
| LED van xả | Anode | GPIO 26 |

**Lưu ý:**
- Các thiết bị sử dụng chung chân GND.
- DS18B20 cần điện trở kéo lên 4.7kΩ giữa DATA và 3.3V.
- Khi triển khai phần cứng thật, chân ECHO của HC-SR04 cần mạch chia áp để bảo vệ ESP32 khỏi tín hiệu 5V.
- LED chỉ đại diện cho trạng thái hoạt động của máy bơm và van xả trong mô phỏng.

## ⚙️ 5. Kiến trúc hoạt động của hệ thống

Hệ thống được chia thành ba thành phần chính:

1. **ESP32:** Thu thập dữ liệu cảm biến, xử lý thuật toán điều khiển và quản lý thiết bị.
2. **EMQX MQTT Broker:** Trung gian truyền dữ liệu giữa ESP32 và website.
3. **Web Dashboard:** Hiển thị thông số cảm biến, trạng thái thiết bị và gửi lệnh điều khiển.

### Sơ đồ luồng dữ liệu

```text
             ┌──────────────────────┐
             │      HC-SR04         │
             │    Đo mực nước       │
             └──────────┬───────────┘
                        │
             ┌──────────▼───────────┐
             │       DS18B20        │
             │    Đo nhiệt độ       │
             └──────────┬───────────┘
                        │
                ┌───────▼───────┐
                │     ESP32     │
                │   FreeRTOS    │
                └───────┬───────┘
                        │
                  MQTT Publish
                        │
                ┌───────▼───────┐
                │  EMQX Broker  │
                └───────┬───────┘
                        │
                 MQTT Subscribe
                        │
                ┌───────▼───────┐
                │  Web Dashboard│
                └───────┬───────┘
                        │
                  MQTT Publish
                   Lệnh điều khiển
                        │
                ┌───────▼───────┐
                │  EMQX Broker  │
                └───────┬───────┘
                        │
                  MQTT Subscribe
                        │
                ┌───────▼───────┐
                │     ESP32     │
                └───────┬───────┘
                        │
                ┌───────▼───────┐
                │ Máy bơm / Van │
                │   xả (LED)    │
                └───────────────┘
```

### Các tác vụ FreeRTOS

Chương trình ESP32 sử dụng ba task chính:

| Task | Nhiệm vụ |
|---|---|
| `Task_ReadData` | Đọc dữ liệu từ HC-SR04 và DS18B20, tính toán mực nước |
| `Task_SendData` | Duy trì kết nối MQTT, gửi dữ liệu và tiếp nhận lệnh từ website |
| `Task_Alert` | Xử lý chế độ AUTO/MANUAL và điều khiển máy bơm, van xả |

Các task trao đổi dữ liệu và lệnh thông qua Queue nhằm hạn chế việc các tác vụ can thiệp trực tiếp vào công việc của nhau.

## 📡 6. Giao tiếp MQTT với EMQX

Dự án sử dụng máy chủ MQTT công cộng của EMQX:

| Thông số | Giá trị |
|---|---|
| Broker | `broker.emqx.io` |
| MQTT TCP Port | `1883` |
| MQTT WebSocket Secure Port | `8084` |
| WebSocket Path | `/mqtt` |
| Authentication | Không yêu cầu tài khoản trong cấu hình thử nghiệm này |

ESP32 sử dụng MQTT TCP, trong khi website sử dụng MQTT over WebSocket Secure (WSS).

### MQTT Topics

**Topic 1 – Dữ liệu cảm biến**

```text
lab3/de4/aquarium_esp32/sensor
```

ESP32 gửi các thông số:
- `distance`: Khoảng cách từ cảm biến đến mặt nước (cm).
- `waterLevel`: Chiều cao mực nước (m).
- `temperature`: Nhiệt độ nước (°C).
- `pump_state`: Trạng thái máy bơm.
- `valve_state`: Trạng thái van xả.
- `mode`: Chế độ hoạt động.

Ví dụ payload:

```json
{
  "distance": 80.00,
  "waterLevel": 1.20,
  "temperature": 28.50,
  "pump_state": "OFF",
  "valve_state": "CLOSED",
  "mode": "AUTO"
}
```

**Topic 2 – Lệnh điều khiển**

```text
lab3/de4/aquarium_esp32/control
```

Website gửi lệnh điều khiển đến ESP32.

Ví dụ chuyển sang chế độ thủ công:

```json
{
  "mode": "MANUAL"
}
```

Bật máy bơm:

```json
{
  "device": "pump",
  "state": "ON"
}
```

Tắt máy bơm:

```json
{
  "device": "pump",
  "state": "OFF"
}
```

Mở van xả:

```json
{
  "device": "valve",
  "state": "OPEN"
}
```

Đóng van xả:

```json
{
  "device": "valve",
  "state": "CLOSED"
}
```

## 🎮 7. Các chế độ hoạt động

### AUTO – Chế độ tự động

ESP32 tự động đọc dữ liệu cảm biến và điều khiển thiết bị dựa trên các ngưỡng được thiết lập trong chương trình.

Các mức điều khiển hiện tại:

| Điều kiện mực nước | Hoạt động |
|---|---|
| Từ 1.0m đến dưới 1.5m | Máy bơm chạy PWM khoảng 50% trong 10 giây |
| Từ 1.5m đến 2.0m | Máy bơm chạy PWM 100% trong 20 giây |
| Dưới 0.5m | Van xả hoạt động trong 10 giây |
| Các trường hợp còn lại | Dừng thiết bị |

### MANUAL – Chế độ thủ công

Người dùng có thể trực tiếp điều khiển thiết bị từ Web Dashboard:

- Bật/tắt máy bơm.
- Mở/đóng van xả.
- Theo dõi trạng thái thiết bị trên website.

**Lưu ý:** Khi hệ thống ở chế độ AUTO, các lệnh điều khiển thiết bị thủ công sẽ không được thực thi. Khi ở MANUAL, người dùng có thể chủ động điều khiển thiết bị.

Hệ thống cũng có cơ chế liên động để máy bơm và van xả không hoạt động đồng thời.

## 🚀 8. Hướng dẫn chạy dự án

### Bước 1: Mở mô phỏng Wokwi

- Truy cập [Wokwi](https://wokwi.com/).
- Mở project ESP32 của hệ thống.
- Kiểm tra kết nối cảm biến và các chân GPIO theo bảng cấu hình.

### Bước 2: Chuẩn bị chương trình ESP32

Cài đặt các thư viện cần thiết:

```text
OneWire
DallasTemperature
PubSubClient
ArduinoJson
```

Cấu hình Wi-Fi mô phỏng:

```cpp
const char* ssid = "Wokwi-GUEST";
```

Sau đó nạp chương trình vào ESP32 và chạy mô phỏng.

### Bước 3: Kiểm tra kết nối MQTT

Mở Serial Monitor và kiểm tra các thông báo kết nối Wi-Fi, EMQX, đăng ký topic điều khiển.

ESP32 cần kết nối thành công và subscribe topic:

```text
lab3/de4/aquarium_esp32/control
```

### Bước 4: Khởi chạy Web Dashboard

- Mở file `index.html` bằng trình duyệt hoặc triển khai website trên hosting phù hợp.
- Cấu hình MQTT:
  - Host: `broker.emqx.io`
  - Port: `8084`
  - Path: `/mqtt`
  - Protocol: `WSS`
- Nhấn Connect để kết nối broker.

### Bước 5: Kiểm tra hệ thống

- Quan sát dữ liệu nhiệt độ và mực nước.
- Thay đổi khoảng cách HC-SR04 trên Wokwi.
- Chuyển chế độ AUTO/MANUAL.
- Gửi lệnh bật/tắt máy bơm.
- Gửi lệnh mở/đóng van xả.
- Quan sát trạng thái LED và dữ liệu cập nhật trên dashboard.

## 📊 9. Kết quả mô phỏng

Các chức năng được thiết kế để kiểm thử gồm:

| STT | Chức năng | Nội dung kiểm thử |
|---|---|---|
| 1 | Kết nối Wi-Fi | ESP32 kết nối mạng mô phỏng |
| 2 | Kết nối MQTT | ESP32 và website kết nối EMQX |
| 3 | Đọc cảm biến | Hiển thị khoảng cách, mực nước và nhiệt độ |
| 4 | Gửi dữ liệu | ESP32 publish telemetry lên broker |
| 5 | Nhận lệnh | ESP32 subscribe và xử lý lệnh từ website |
| 6 | Điều khiển máy bơm | Bật/tắt qua dashboard |
| 7 | Điều khiển van xả | Mở/đóng qua dashboard |
| 8 | AUTO/MANUAL | Chuyển đổi chế độ hoạt động |

### Hình ảnh minh họa

Có thể bổ sung ảnh chụp màn hình mô phỏng tại các vị trí sau:

- Sơ đồ mạch Wokwi.
- Serial Monitor khi ESP32 kết nối EMQX.
- Giao diện Web Dashboard.
- Dashboard hiển thị dữ liệu cảm biến.
- Máy bơm hoạt động trong chế độ AUTO.
- Điều khiển máy bơm và van xả trong chế độ MANUAL.

## 📁 10. Cấu trúc thư mục dự án

```text
Smart-Aquarium-IoT/
│
├── ESP32/
│   ├── Aquarium_ESP32.ino
│   └── diagram.json
│
├── Web/
│   └── index.html
│
├── README.md
│
└── docs/
    ├── images/
    │   ├── wokwi-circuit.png
    │   ├── serial-monitor.png
    │   └── dashboard.png
    │
    └── report.pdf
```

*Cấu trúc trên là cấu trúc đề xuất. Có thể điều chỉnh tên thư mục và file cho phù hợp với repository thực tế.*

## 🔮 11. Hướng phát triển

- Kết nối máy bơm và van điện từ thực tế thông qua relay hoặc driver phù hợp.
- Bổ sung lưu trữ dữ liệu và biểu đồ lịch sử.
- Xây dựng hệ thống cảnh báo khi mực nước vượt ngưỡng.
- Bổ sung xác thực MQTT và phân quyền điều khiển.
- Tích hợp thêm cảm biến chất lượng nước như pH, độ đục.
- Tối ưu giao diện dashboard cho thiết bị di động.

## ⚠️ 12. Lưu ý

- Dự án hiện được xây dựng chủ yếu phục vụ học tập và mô phỏng trên Wokwi.
- EMQX public broker là môi trường dùng chung, không nên sử dụng để truyền dữ liệu nhạy cảm.
- Khi triển khai phần cứng thật cần bổ sung mạch bảo vệ, nguồn cấp phù hợp và thiết bị đóng cắt an toàn.
- Các ngưỡng điều khiển cần được kiểm tra và hiệu chỉnh theo yêu cầu sử dụng thực tế.

## 👨‍💻 13. Tác giả
Nguyễn Long Nhật
Nate3104/nhatnguyen3104

**Đề tài:** Lab 3 – Đề 4: Hệ thống giám sát và điều khiển bể nước thông minh.

---
