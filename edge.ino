/*
  ===========================================================
  KONTROL TANDON AIR OTOMATIS - ESP32 (VERSI RESPONSIF)
  Sensor  : HC-SR04 (Ultrasonik)
  Aktuator: Relay 1 channel -> Pompa air
  Koneksi : WiFi + MQTT (broker Mosquitto lokal)

  ===========================================================
*/

#include <WiFi.h>
#include <WiFiClient.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// ===================== KONFIGURASI WIFI =====================
const char* WIFI_SSID     = "Xiaomi 11T";
const char* WIFI_PASSWORD = "11111111";

// ===================== KONFIGURASI MQTT ======================
// Ganti dengan IP broker Mosquitto Anda (harus sama dgn yang
// dipakai di config.ts proyek edge / YTTA)
const char* MQTT_BROKER = "10.90.179.237";
const int   MQTT_PORT   = 1883;
const char* MQTT_CLIENT_ID = "esp32-tandon-air";

// Karena mosquitto.conf pakai allow_anonymous false + password_file,
// isi username & password yang sama dengan yang dibuat via mosquitto_passwd
const char* MQTT_USERNAME = "sypdn";
const char* MQTT_PASSWORD = "180905";

const char* TOPIC_LEVEL     = "tandon/level";        // publish telemetry
const char* TOPIC_POMPA_CMD = "tandon/pompa/cmd";     // subscribe override manual

// ===================== KONFIGURASI PIN ========================
const int PIN_TRIG  = 5;   // HC-SR04 TRIG
const int PIN_ECHO  = 18;  // HC-SR04 ECHO (WAJIB pakai voltage divider, lihat catatan di bawah)
const int PIN_RELAY = 19;  // Relay IN

// Sebagian besar modul relay 1 channel aktif LOW (LOW = ON).
// Jika pompa malah menyala terbalik, ubah dua baris ini.
const int RELAY_ON_LEVEL  = LOW;
const int RELAY_OFF_LEVEL = HIGH;

// ===================== KONFIGURASI TANDON =====================
const float FULL_DISTANCE_CM  = 8.0;   // jarak saat PENUH  → pompa OFF
const float EMPTY_DISTANCE_CM = 16.0;  // jarak saat KOSONG → beri margin +1 dari 15.62

// Hysteresis
const float LOW_THRESHOLD_PERCENT  = 10.0;  // <=10% → pompa ON  (sekitar jarak ~15cm)
const float HIGH_THRESHOLD_PERCENT = 95.0;  // >=95% → pompa OFF (sekitar jarak ~8.7cm)

const float HARD_MAX_PERCENT = 98.0;         // ~8.1cm → paksa OFF absolut

// ===================== INTERVAL (DIPISAH) =====================
// Interval baca sensor + cek kontrol pompa -> dibuat CEPAT
// supaya relay langsung merespons begitu batas tercapai.
const unsigned long SENSOR_READ_INTERVAL_MS = 500;   // 5x per detik

// Interval kirim telemetry ke MQTT -> tetap lebih jarang
// agar tidak membanjiri broker/ThingsBoard.
const unsigned long TELEMETRY_INTERVAL_MS = 2000;

// Interval retry WiFi & MQTT (non-blocking)
const unsigned long WIFI_RETRY_INTERVAL_MS = 5000;
const unsigned long MQTT_RETRY_INTERVAL_MS = 3000;

// ===================== STATE =====================
enum Mode { MODE_AUTO, MODE_MANUAL_ON, MODE_MANUAL_OFF };
Mode currentMode = MODE_AUTO;
bool pumpIsOn = false;

unsigned long lastSensorReadMs = 0;
unsigned long lastTelemetryMs  = 0;
unsigned long lastWifiAttemptMs = 0;
unsigned long lastMqttAttemptMs = 0;

// Nilai sensor terakhir, dipakai bersama oleh logika kontrol & telemetry
float lastDistance     = -1;
float lastLevelCm      = 0;
float lastLevelPercent = 0;
bool  lastSensorError   = true;

WiFiClient espClient;
PubSubClient mqttClient(espClient);

// ===================== WIFI (NON-BLOCKING) =====================
void connectWiFiNonBlocking() {
  if (WiFi.status() == WL_CONNECTED) return;

  unsigned long now = millis();
  if (now - lastWifiAttemptMs < WIFI_RETRY_INTERVAL_MS) return;
  lastWifiAttemptMs = now;

  Serial.println("Mencoba konek WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  // Tidak ada while/delay di sini -> tidak blocking.
  // Status akan dicek lagi di pemanggilan berikutnya.
}

// ===================== MQTT CALLBACK =====================
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  message.trim();
  message.toUpperCase();

  Serial.print("Pesan MQTT diterima [");
  Serial.print(topic);
  Serial.print("]: ");
  Serial.println(message);

  if (String(topic) == TOPIC_POMPA_CMD) {
    if (message == "ON") {
      currentMode = MODE_MANUAL_ON;
      Serial.println("-> Mode: MANUAL ON");
    } else if (message == "OFF") {
      currentMode = MODE_MANUAL_OFF;
      Serial.println("-> Mode: MANUAL OFF");
    } else if (message == "AUTO") {
      currentMode = MODE_AUTO;
      Serial.println("-> Mode: AUTO (hysteresis ESP32)");
    } else {
      Serial.println("-> Perintah tidak dikenal (gunakan ON/OFF/AUTO)");
    }
    // Setelah mode berubah, langsung evaluasi ulang pompa
    // berdasarkan pembacaan sensor TERAKHIR (tidak perlu
    // menunggu siklus baca sensor berikutnya).
    evaluatePump();
  }
}

// ===================== MQTT CONNECT (NON-BLOCKING) =====================
void connectMQTTNonBlocking() {
  if (mqttClient.connected()) return;

  unsigned long now = millis();
  if (now - lastMqttAttemptMs < MQTT_RETRY_INTERVAL_MS) return;
  lastMqttAttemptMs = now;

  Serial.print("Mencoba konek MQTT...");
  if (mqttClient.connect(MQTT_CLIENT_ID, MQTT_USERNAME, MQTT_PASSWORD)) {
    Serial.println(" berhasil");
    mqttClient.subscribe(TOPIC_POMPA_CMD);
    Serial.print("Subscribe ke: ");
    Serial.println(TOPIC_POMPA_CMD);
  } else {
    Serial.print(" gagal, rc=");
    Serial.println(mqttClient.state());
  }
}

// ===================== BACA SENSOR ULTRASONIK (SEKALI) =====================
// Mengembalikan jarak dalam cm, atau -1 jika gagal/di luar rentang wajar
float readDistanceOnceCM() {
  digitalWrite(PIN_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);

  // Timeout 25ms (~4m) -> cukup untuk jarak tandon, sekaligus
  // membuat tiap sampel lebih cepat dibanding timeout 30ms.
  long duration = pulseIn(PIN_ECHO, HIGH, 25000UL);

  if (duration == 0) {
    return -1; // tidak ada pantulan / timeout
  }

  float distance = duration * 0.0343 / 2.0; // cm

  if (distance < 2 || distance > 400) {
    return -1; // di luar rentang wajar sensor HC-SR04
  }

  return distance;
}

// ===================== BACA SENSOR DENGAN MEDIAN FILTER =====================
// Mengambil 3 sampel cepat lalu ambil median, untuk meredam
// noise sesaat tanpa memperlambat respons secara signifikan
// (total durasi tetap sub-50ms, jauh di bawah interval 200ms).
float readDistanceMedianCM() {
  float samples[3];
  int validCount = 0;

  for (int i = 0; i < 3; i++) {
    float d = readDistanceOnceCM();
    if (d > 0) {
      samples[validCount++] = d;
    }
    delayMicroseconds(500); // jeda singkat antar pulse, jauh dari delay() detik
  }

  if (validCount == 0) return -1;       // semua gagal -> error
  if (validCount == 1) return samples[0];
  if (validCount == 2) return (samples[0] + samples[1]) / 2.0;

  // validCount == 3 -> sort kecil lalu ambil tengah (median asli)
  if (samples[0] > samples[1]) { float t = samples[0]; samples[0] = samples[1]; samples[1] = t; }
  if (samples[1] > samples[2]) { float t = samples[1]; samples[1] = samples[2]; samples[2] = t; }
  if (samples[0] > samples[1]) { float t = samples[0]; samples[0] = samples[1]; samples[1] = t; }
  return samples[1];
}

// ===================== KONTROL RELAY =====================
void setPump(bool on) {
  if (pumpIsOn == on) return; // hindari toggle pin yang tidak perlu
  pumpIsOn = on;
  digitalWrite(PIN_RELAY, on ? RELAY_ON_LEVEL : RELAY_OFF_LEVEL);
  Serial.print(">> RELAY ");
  Serial.println(on ? "ON" : "OFF");
}

// ===================== EVALUASI POMPA (DIPANGGIL SESERING MUNGKIN) =====================
// Fungsi ini HANYA mengambil keputusan ON/OFF berdasarkan data
// sensor yang sudah ada di variabel global (lastLevelPercent dst).
// Dipanggil setiap siklus baca sensor (200ms) DAN setiap kali
// mode berubah lewat MQTT, sehingga tidak ada delay tambahan
// saat batas maksimal tercapai.
void evaluatePump() {
  if (lastSensorError) {
    // Proteksi: sensor error -> pompa dimatikan demi keamanan
    setPump(false);
    return;
  }

  if (lastLevelPercent >= HARD_MAX_PERCENT) {
    // Pengaman mutlak, berlaku di SEMUA mode (cegah overflow)
    // Ini yang memastikan relay LANGSUNG mati saat air penuh,
    // tanpa menunggu siklus telemetry 2 detik.
    setPump(false);
    return;
  }

  switch (currentMode) {
    case MODE_AUTO:
      if (lastLevelPercent <= LOW_THRESHOLD_PERCENT) {
        setPump(true);
      } else if (lastLevelPercent >= HIGH_THRESHOLD_PERCENT) {
        setPump(false);
      }
      // di antara ambang batas -> pertahankan kondisi terakhir
      break;

    case MODE_MANUAL_ON:
      setPump(true);
      break;

    case MODE_MANUAL_OFF:
      setPump(false);
      break;
  }
}

// ===================== PUBLISH TELEMETRY =====================
void publishTelemetry() {
  StaticJsonDocument<256> doc;
  doc["distance_cm"]   = lastSensorError ? -1 : lastDistance;
  doc["level_cm"]      = lastSensorError ? 0 : lastLevelCm;
  doc["level_percent"] = lastSensorError ? 0 : lastLevelPercent;
  doc["pump_status"]   = pumpIsOn ? "ON" : "OFF";
  doc["mode"]          = currentMode == MODE_AUTO ? "AUTO" :
                          (currentMode == MODE_MANUAL_ON ? "MANUAL_ON" : "MANUAL_OFF");
  doc["sensor_error"]  = lastSensorError;

  char buffer[256];
  size_t len = serializeJson(doc, buffer);

  if (mqttClient.connected()) {
    mqttClient.publish(TOPIC_LEVEL, buffer, len);
  }

  Serial.print("Level: ");
  Serial.print(lastSensorError ? -1 : lastLevelPercent);
  Serial.print("% | Pompa: ");
  Serial.print(pumpIsOn ? "ON" : "OFF");
  Serial.print(" | Mode: ");
  Serial.println(currentMode == MODE_AUTO ? "AUTO" :
                  (currentMode == MODE_MANUAL_ON ? "MANUAL_ON" : "MANUAL_OFF"));
}

// ===================== SETUP =====================
void setup() {
  Serial.begin(115200);

  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  pinMode(PIN_RELAY, OUTPUT);

  // Pastikan pompa mati saat boot (set langsung, lewati cache pumpIsOn)
  pumpIsOn = true;       // paksa beda dulu
  setPump(false);        // baru benar-benar di-set OFF

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
  // Buffer keepalive default PubSubClient (15s) sudah cukup;
  // tidak perlu diubah untuk kasus ini.
}

// ===================== LOOP =====================
void loop() {
  // 1) Jaga koneksi WiFi & MQTT, TANPA blocking sama sekali.
  connectWiFiNonBlocking();

  if (WiFi.status() == WL_CONNECTED) {
    connectMQTTNonBlocking();
    mqttClient.loop(); // proses pesan masuk secepat mungkin
  }

  unsigned long now = millis();

  // 2) SIKLUS CEPAT: baca sensor + evaluasi pompa setiap 200ms.
  //    Inilah yang membuat relay langsung mati/nyala begitu
  //    batas tercapai, tanpa delay beberapa detik.
  if (now - lastSensorReadMs >= SENSOR_READ_INTERVAL_MS) {
    lastSensorReadMs = now;

    float distance = readDistanceMedianCM();
    lastSensorError = (distance < 0);
    lastDistance = distance;

    if (!lastSensorError) {
      float levelPercent = (EMPTY_DISTANCE_CM - distance) /
                            (EMPTY_DISTANCE_CM - FULL_DISTANCE_CM) * 100.0;
      levelPercent = constrain(levelPercent, 0, 100);
      float levelCm = EMPTY_DISTANCE_CM - distance;
      if (levelCm < 0) levelCm = 0;

      lastLevelPercent = levelPercent;
      lastLevelCm = levelCm;
    }

    // Evaluasi & eksekusi relay SEKARANG, di siklus cepat ini.
    evaluatePump();
  }

  // 3) SIKLUS LAMBAT: publish telemetry tiap 2 detik saja,
  //    supaya broker/ThingsBoard tidak dibanjiri data.
  if (now - lastTelemetryMs >= TELEMETRY_INTERVAL_MS) {
    lastTelemetryMs = now;
    publishTelemetry();
  }
}
