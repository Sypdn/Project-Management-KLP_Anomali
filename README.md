# 💧 Kontrol Tandon Air Otomatis — ESP32 + MQTT + ThingsBoard

Sistem otomasi tandon air berbasis ESP32 yang mengukur ketinggian air dengan sensor ultrasonik, mengontrol pompa lewat relay secara otomatis (hysteresis) maupun manual, dan mengirim data real-time ke dashboard cloud ThingsBoard melalui broker MQTT.

## ✨ Fitur

- **Pengukuran level air non-blocking** menggunakan sensor ultrasonik HC-SR04, dengan median filter dari 3 sampel untuk meredam noise pembacaan.
- **Kontrol pompa otomatis (mode AUTO)** dengan hysteresis: pompa menyala saat level ≤10%, mati saat level ≥95%, dan mempertahankan kondisi di antara ambang batas agar relay tidak "kedip-kedip".
- **Pengaman overflow mutlak** — pompa langsung dimatikan begitu level mencapai 98%, berlaku di semua mode termasuk manual.
- **Mode manual override** (`ON` / `OFF` / `AUTO`) via publish MQTT ke topic perintah, dikendalikan langsung dari ESP32 tanpa delay siklus telemetry.
- **Fail-safe sensor** — jika pembacaan sensor gagal/error, pompa otomatis dimatikan demi keamanan.
- **Edge app (Bun/TypeScript)** yang subscribe ke broker MQTT lokal, menganalisis status tandon (`RENDAH` / `NORMAL` / `PENUH`), lalu meneruskan data ke ThingsBoard Cloud lewat REST API.
- **Siklus baca-vs-kirim terpisah** — pembacaan sensor & evaluasi pompa berjalan cepat (tiap 500ms) agar respons relay instan, sementara telemetry ke cloud dikirim lebih jarang (tiap 2 detik) agar broker tidak dibanjiri data.

## 🧰 Arsitektur & Teknologi

| Bagian | Teknologi |
|---|---|
| Firmware | ESP32 (Arduino, C++), PubSubClient (MQTT), ArduinoJson |
| Sensor | HC-SR04 (ultrasonik) |
| Aktuator | Relay 1 channel → pompa air |
| Broker | MQTT (Mosquitto lokal, autentikasi username/password) |
| Edge app | Bun + TypeScript, library `mqtt` & `axios` |
| Cloud dashboard | ThingsBoard Cloud (REST API telemetry) |

**Alur data:** ESP32 membaca jarak air via HC-SR04 → menghitung level (%) → mengevaluasi & mengontrol relay pompa secara lokal → publish telemetry ke topic MQTT `tandon/level` → edge app (Bun) subscribe topic tersebut, menganalisis status tandon, lalu mengirim ke ThingsBoard via HTTP POST. Override manual pompa dilakukan dengan publish `ON`/`OFF`/`AUTO` ke topic `tandon/pompa/cmd`.

## 📁 Struktur Project

```
├── edge.ino          # Firmware ESP32 (sensor, relay, WiFi, MQTT)
├── index.ts           # Edge app: subscribe MQTT → kirim ke ThingsBoard
├── edge/
│   ├── config.ts       # Konfigurasi broker MQTT & ThingsBoard
│   └── rules.ts         # Logika penentuan status tandon (RENDAH/NORMAL/PENUH)
├── package.json
└── tsconfig.json
```

## 🚀 Instalasi & Setup

### 1. Broker MQTT (Mosquitto)
Jalankan Mosquitto di PC/laptop/Raspberry Pi di jaringan lokal yang sama dengan ESP32. Karena `allow_anonymous false`, buat user/password dengan `mosquitto_passwd`.

### 2. Firmware ESP32 (`edge.ino`)
1. Buka di Arduino IDE, install library: `PubSubClient`, `ArduinoJson`.
2. Sesuaikan `WIFI_SSID`, `WIFI_PASSWORD`, `MQTT_BROKER` (IP broker Mosquitto), `MQTT_USERNAME`, `MQTT_PASSWORD`.
3. Sesuaikan pin: `PIN_TRIG` (5), `PIN_ECHO` (18, **wajib pakai voltage divider** karena ESP32 GPIO 3.3V sedangkan echo HC-SR04 5V), `PIN_RELAY` (19).
4. Kalibrasi `FULL_DISTANCE_CM` dan `EMPTY_DISTANCE_CM` sesuai tinggi tandon kamu.
5. Upload ke board.

### 3. Edge app (Bun)
```bash
bun install
```
Sesuaikan `edge/config.ts` (alamat broker MQTT, kredensial, token ThingsBoard), lalu jalankan:
```bash
bun run index.ts
```

## ⚙️ Kontrol Manual

Publish salah satu payload berikut ke topic `tandon/pompa/cmd`:
| Payload | Efek |
|---|---|
| `ON` | Paksa pompa menyala |
| `OFF` | Paksa pompa mati |
| `AUTO` | Kembali ke mode otomatis (hysteresis) |

## ⚠️ Catatan Keamanan

`edge/config.ts` dan `edge.ino` saat ini berisi kredensial asli (password WiFi, password MQTT, token ThingsBoard) tertulis langsung di kode. **Sebelum push ke repo publik**, sebaiknya:
- Pindahkan nilai sensitif ke file `.env` / secrets terpisah yang di-*gitignore*, atau
- Reset ulang password & token setelah repo publik, lalu ganti dengan placeholder di kode.

## 👤 Author

Dikembangkan oleh **Syaepuddin** bersama tim (Kelompok Anomali).

## 📷 Hak Cipta & Lisensi

© 2026 Sypdn. Hak Cipta Dilindungi Undang-Undang.
