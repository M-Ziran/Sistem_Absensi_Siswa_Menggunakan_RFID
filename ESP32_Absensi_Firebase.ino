/*
 * ============================================================
 * SISTEM ABSENSI NFC - ESP32 WROOM 32
 * PN532 (I2C @ pin 19,23) + OLED SSD1306 (I2C @ pin 21,22)
 *
 * Fitur:
 *  - Baca sesi aktif dari Firebase (/sesi_aktif)
 *  - Validasi kelas siswa — hanya siswa sekelas yang bisa absen
 *  - Deteksi Hadir / Terlambat berdasarkan jam mulai + toleransi
 *  - Alpha di-generate otomatis saat guru tutup sesi di web
 *  - Simpan ke /absensi_sesi/{tanggal}/{mapelKey}/{siswaKey}
 *  - Beep 2x saat kartu baru/belum terdaftar ditap (registrasi via web)
 *
 * WIRING BUZZER (buzzer aktif 3 pin: VCC, GND, I/O):
 *   I/O -> GPIO 4 ESP32 | VCC -> 3.3V/5V sesuai modul | GND -> GND ESP32
 *   Kalau buzzer bunyi terus begitu ESP32 nyala, berarti modul low-trigger
 *   — tinggal balik logika HIGH/LOW di fungsi beepDaftar().
 *
 * Library (Arduino Library Manager):
 *   - Adafruit PN532
 *   - Adafruit SSD1306
 *   - Adafruit GFX Library
 *   - ArduinoJson  (Benoit Blanchon, v6.x)
 *   - NTPClient    (Arduino NTPClient)
 *   WiFi / HTTPClient / WiFiClientSecure → built-in ESP32
 * ============================================================
 */

#include <Wire.h>
#include <Adafruit_PN532.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

// ============================================================
// KONFIGURASI
// ============================================================
#define WIFI_SSID       "STARLINK 5G"
#define WIFI_PASSWORD   "Purwakarta"
#define FIREBASE_HOST   "absen-ab028-default-rtdb.firebaseio.com"
#define FIREBASE_SECRET "erd8azFMJlaTCaGwFf2LFaoUyuWrVpZhb4b1XlGN"
#define UTC_OFFSET_SEC  25200   // WIB = UTC+7

// ============================================================
// HARDWARE
// ============================================================
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT  64
#define OLED_RESET     -1

// Buzzer — dipakai sbg penanda suara saat kartu baru/belum terdaftar
// ditap (momen ini dipakai utk registrasi siswa baru via web).
// Pin bebas asal tidak bentrok dgn OLED (21,22) & PN532 (19,23).
#define BUZZER_PIN 4

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
TwoWire I2C_PN532 = TwoWire(1);
Adafruit_PN532 nfc(0, 0, &I2C_PN532);

WiFiUDP   ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", UTC_OFFSET_SEC, 60000);

// ============================================================
// STRUCT SESI AKTIF
// ============================================================
struct SesiAktif {
  String mapelKey;
  String mapelNama;
  String mapelKode;
  String kelas;       // kelas target sesi ini (dipilih guru)
  String jamMulai;    // format "HH:MM"
  int    toleransi;   // menit
  String tanggal;
  bool   ada;
};

// ============================================================
// STATE GLOBAL
// ============================================================
bool      wifiConnected = false;
String    lastUID       = "";
unsigned long lastScanTime = 0;
const unsigned long DEBOUNCE_MS   = 3000;
const unsigned long SESI_REFRESH  = 15000;  // refresh sesi tiap 15 detik
unsigned long lastSesiCheck       = 0;

SesiAktif sesiAktif;

// ============================================================
// FIREBASE REST
// ============================================================
String fbGet(const String& path) {
  if (!wifiConnected) return "";
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  String url = "https://" + String(FIREBASE_HOST) + path
               + ".json?auth=" + String(FIREBASE_SECRET);
  http.begin(client, url);
  String result = "";
  if (http.GET() == 200) result = http.getString();
  http.end();
  return result;
}

bool fbPut(const String& path, const String& body) {
  if (!wifiConnected) return false;
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  String url = "https://" + String(FIREBASE_HOST) + path
               + ".json?auth=" + String(FIREBASE_SECRET);
  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");
  int code = http.PUT(body);
  http.end();
  return (code == 200 || code == 204);
}

// ============================================================
// OLED
// ============================================================
void oledClear() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
}

void showStandby() {
  oledClear();
  display.setTextSize(1);

  if (sesiAktif.ada) {
    display.setCursor(0, 0);
    display.println("SESI AKTIF:");
    display.setCursor(0, 12);
    display.println(sesiAktif.mapelNama.length() > 20
      ? sesiAktif.mapelNama.substring(0, 20)
      : sesiAktif.mapelNama);
    display.setCursor(0, 24);
    display.println("Kelas: " + sesiAktif.kelas);
    display.setCursor(0, 36);
    display.println("Mulai: " + sesiAktif.jamMulai
      + (sesiAktif.toleransi > 0
        ? " (+" + String(sesiAktif.toleransi) + "mnt)"
        : ""));
    display.drawLine(0, 48, 127, 48, SSD1306_WHITE);
    display.setCursor(5, 52);
    display.println("Tempelkan kartu...");
  } else {
    display.setCursor(15, 5);  display.println("SISTEM ABSENSI");
    display.setCursor(15, 17); display.println("Berbasis NFC");
    display.drawLine(0, 27, 127, 27, SSD1306_WHITE);
    display.setCursor(0, 35);  display.println("Tidak ada sesi aktif");
    display.setCursor(0, 47);  display.println("Guru mulai sesi di web");
  }

  display.setCursor(wifiConnected ? 85 : 75, 56);
  display.println(wifiConnected ? "WiFi OK" : "No WiFi");
  display.display();
}

void showConnecting() {
  oledClear();
  display.setTextSize(1);
  display.setCursor(20, 22); display.println("Menghubungkan");
  display.setCursor(30, 35); display.println("ke WiFi...");
  display.display();
}

void showMsg(const String& l1, const String& l2, const String& l3 = "") {
  oledClear();
  display.setTextSize(1);
  display.setCursor(0, 10); display.println(l1);
  display.setCursor(0, 28); display.println(l2);
  if (l3 != "") { display.setCursor(0, 46); display.println(l3); }
  display.display();
}

void showHasil(const String& nama, const String& waktu,
               const String& status, bool unregistered = false) {
  oledClear();
  display.setTextSize(1);

  if (unregistered) {
    display.setCursor(15, 10); display.println("KARTU TERBACA!");
    display.drawLine(0, 22, 127, 22, SSD1306_WHITE);
    display.setCursor(0, 28); display.println("Belum terdaftar");
    display.setCursor(0, 44); display.println("Daftar via website!");
    display.display();
    return;
  }

  // ── BARU: tampilan untuk siswa beda kelas ──
  if (status == "beda_kelas") {
    display.setCursor(10, 0); display.println("AKSES DITOLAK!");
    display.drawLine(0, 12, 127, 12, SSD1306_WHITE);
    display.setCursor(0, 18); display.println(nama.length() > 20 ? nama.substring(0, 20) : nama);
    display.drawLine(0, 32, 127, 32, SSD1306_WHITE);
    display.setCursor(0, 38); display.println("Bukan kelas ini!");
    display.setCursor(0, 50); display.println("Sesi: " + sesiAktif.kelas);
    display.display();
    return;
  }

  if (status == "hadir") {
    display.setCursor(20, 0); display.println("ABSEN BERHASIL!");
  } else if (status == "terlambat") {
    display.setCursor(15, 0); display.println("TERLAMBAT!");
  } else {
    display.setCursor(10, 0); display.println("SUDAH ABSEN!");
  }

  display.drawLine(0, 12, 127, 12, SSD1306_WHITE);
  display.setCursor(0, 18); display.println(nama.length() > 20 ? nama.substring(0, 20) : nama);
  display.drawLine(0, 32, 127, 32, SSD1306_WHITE);
  display.setCursor(0, 38); display.println("Mapel: " +
    (sesiAktif.mapelKode.length() > 0 ? sesiAktif.mapelKode : "-"));
  display.setCursor(0, 50); display.println("Jam  : " + waktu);
  display.display();
}

void showError(const String& msg) {
  oledClear();
  display.setTextSize(1);
  display.setCursor(20, 20); display.println("ERROR!");
  display.setCursor(0, 35);  display.println(msg);
  display.display();
  delay(2000);
}

// ============================================================
// WAKTU
// ============================================================
String getTodayDate() {
  time_t raw = timeClient.getEpochTime();
  struct tm* ti = localtime(&raw);
  char buf[11];
  sprintf(buf, "%04d-%02d-%02d",
          ti->tm_year + 1900, ti->tm_mon + 1, ti->tm_mday);
  return String(buf);
}

String getTimeStr() {
  return timeClient.getFormattedTime();
}

long getEpochSec() {
  return (long)timeClient.getEpochTime();
}

// ============================================================
// CEK STATUS: hadir atau terlambat
// ============================================================
String hitungStatus(long epochSec) {
  if (!sesiAktif.ada || sesiAktif.jamMulai == "") return "hadir";

  int colonIdx = sesiAktif.jamMulai.indexOf(':');
  if (colonIdx < 0) return "hadir";
  int jamMulai   = sesiAktif.jamMulai.substring(0, colonIdx).toInt();
  int menitMulai = sesiAktif.jamMulai.substring(colonIdx + 1).toInt();

  time_t raw = (time_t)epochSec;
  struct tm* ti = localtime(&raw);
  int jamSekarang   = ti->tm_hour;
  int menitSekarang = ti->tm_min;

  int totalMulai    = jamMulai   * 60 + menitMulai + sesiAktif.toleransi;
  int totalSekarang = jamSekarang * 60 + menitSekarang;

  return (totalSekarang > totalMulai) ? "terlambat" : "hadir";
}

// ============================================================
// BACA SESI AKTIF DARI FIREBASE
// ============================================================
void refreshSesi() {
  String payload = fbGet("/sesi_aktif");
  if (payload == "" || payload == "null") {
    sesiAktif.ada = false;
    return;
  }

  StaticJsonDocument<512> doc;
  if (deserializeJson(doc, payload)) {
    sesiAktif.ada = false;
    return;
  }

  sesiAktif.mapelKey  = doc["mapelKey"]  | "";
  sesiAktif.mapelNama = doc["mapelNama"] | "";
  sesiAktif.mapelKode = doc["mapelKode"] | "";
  sesiAktif.kelas     = doc["kelas"]     | "";
  sesiAktif.jamMulai  = doc["jamMulai"]  | "";
  sesiAktif.toleransi = doc["toleransi"] | 0;
  sesiAktif.tanggal   = doc["tanggal"]   | getTodayDate();
  sesiAktif.ada       = (sesiAktif.mapelKey != "" && sesiAktif.kelas != "");

  Serial.println("Sesi : " + sesiAktif.mapelNama);
  Serial.println("Kelas: " + sesiAktif.kelas);
  Serial.println("Mulai: " + sesiAktif.jamMulai);
}

// ============================================================
// CARI SISWA BERDASARKAN UID
// ============================================================
bool cariSiswa(const String& uid, String& outKey, String& outNama,
               String& outNIS, String& outKelas) {
  String payload = fbGet("/siswa");
  if (payload == "" || payload == "null") return false;

  DynamicJsonDocument doc(8192);
  if (deserializeJson(doc, payload)) return false;

  for (JsonPair kv : doc.as<JsonObject>()) {
    String cardUID = kv.value()["uid"] | "";
    cardUID.toUpperCase();
    if (cardUID == uid) {
      outKey   = kv.key().c_str();
      outNama  = kv.value()["nama"]  | "Siswa";
      outNIS   = kv.value()["nis"]   | "-";
      outKelas = kv.value()["kelas"] | "";
      return true;
    }
  }
  return false;
}

// ============================================================
// CEK SUDAH ABSEN DI SESI INI
// ============================================================
bool sudahAbsen(const String& tanggal, const String& mapelKey,
                const String& siswaKey) {
  String payload = fbGet(
    "/absensi_sesi/" + tanggal + "/" + mapelKey + "/" + siswaKey + "/waktu"
  );
  return (payload != "" && payload != "null");
}

// ============================================================
// SIMPAN ABSENSI
// ============================================================
bool simpanAbsensi(const String& tanggal, const String& mapelKey,
                   const String& siswaKey, const String& nama,
                   const String& nis, const String& kelas,
                   const String& uid, long epochSec,
                   const String& status) {
  String path = "/absensi_sesi/" + tanggal + "/" + mapelKey + "/" + siswaKey;
  String json = "{\"nama\":\""     + nama     + "\","
                "\"nis\":\""       + nis      + "\","
                "\"kelas\":\""     + kelas    + "\","
                "\"uid\":\""       + uid      + "\","
                "\"mapelNama\":\"" + sesiAktif.mapelNama + "\","
                "\"waktu\":"       + String(epochSec) + ","
                "\"status\":\""    + status   + "\","
                "\"auto\":false}";
  return fbPut(path, json);
}

// ============================================================
// BUZZER — beep saat kartu baru/belum terdaftar ditap (registrasi)
// ============================================================
void beepDaftar() {
  // 2x beep pendek: penanda "kartu baru terbaca, siap didaftarkan"
  digitalWrite(BUZZER_PIN, HIGH); delay(120); digitalWrite(BUZZER_PIN, LOW);
  delay(100);
  digitalWrite(BUZZER_PIN, HIGH); delay(120); digitalWrite(BUZZER_PIN, LOW);
}

// SIMPAN LAST_SCAN (untuk registrasi di web)
void simpanLastScan(const String& uid, long epochSec) {
  String json = "{\"uid\":\"" + uid + "\",\"ts\":" + String(epochSec) + "}";
  fbPut("/last_scan", json);
}

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);   // pastikan diam saat boot

  Wire.begin(21, 22);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED tidak ditemukan!"); while (true);
  }
  showConnecting();

  I2C_PN532.begin(19, 23);
  delay(300);
  nfc.begin();
  if (!nfc.getFirmwareVersion()) {
    showError("PN532 tidak\nditemukan!"); while (true);
  }
  nfc.SAMConfig();
  Serial.println("PN532 OK");

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting WiFi");
  int attempt = 0;
  while (WiFi.status() != WL_CONNECTED && attempt < 40) {
    delay(500); Serial.print("."); attempt++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    Serial.println("\nWiFi OK: " + WiFi.localIP().toString());
    timeClient.begin();
    timeClient.update();
    Serial.println("NTP OK: " + getTodayDate() + " " + getTimeStr());
    refreshSesi();
  } else {
    Serial.println("\nWiFi GAGAL!");
  }

  sesiAktif.ada = false;
  showStandby();
}

// ============================================================
// LOOP
// ============================================================
void loop() {
  if (wifiConnected) {
    timeClient.update();
    if (millis() - lastSesiCheck >= SESI_REFRESH) {
      lastSesiCheck = millis();
      bool wasAda = sesiAktif.ada;
      refreshSesi();
      if (wasAda != sesiAktif.ada) showStandby();
    }
  }

  uint8_t uid[7];
  uint8_t uidLength;
  boolean kartuAda = nfc.readPassiveTargetID(
    PN532_MIFARE_ISO14443A, uid, &uidLength, 500
  );
  if (!kartuAda) return;

  // Build UID string
  String uidStr = "";
  for (uint8_t i = 0; i < uidLength; i++) {
    if (uid[i] < 0x10) uidStr += "0";
    uidStr += String(uid[i], HEX);
    if (i < uidLength - 1) uidStr += ":";
  }
  uidStr.toUpperCase();
  Serial.println("Kartu: " + uidStr);

  // Debounce
  unsigned long now = millis();
  if (uidStr == lastUID && (now - lastScanTime) < DEBOUNCE_MS) return;
  lastUID      = uidStr;
  lastScanTime = now;

  showMsg("Kartu terbaca!", uidStr, "Memproses...");

  long   epochSec = getEpochSec();
  String tanggal  = getTodayDate();
  String waktuStr = getTimeStr();

  // Simpan last_scan untuk registrasi web
  simpanLastScan(uidStr, epochSec);

  if (!wifiConnected) {
    showMsg("OFFLINE!", uidStr, "No WiFi!"); delay(2500); showStandby(); return;
  }

  // ── 1. Cek sesi aktif ──
  if (!sesiAktif.ada) {
    showMsg("Tidak ada sesi!", "Guru belum mulai", "sesi absensi.");
    delay(2500); showStandby(); return;
  }

  // ── 2. Cari siswa berdasarkan UID ──
  String siswaKey = "", namaSiswa = "", nisSiswa = "", kelasSiswa = "";
  bool ditemukan = cariSiswa(uidStr, siswaKey, namaSiswa, nisSiswa, kelasSiswa);

  if (!ditemukan) {
    showHasil("", waktuStr, "", true);
    beepDaftar();
    Serial.println("Kartu belum terdaftar: " + uidStr);
    delay(2500); showStandby(); return;
  }

  Serial.println("Siswa: " + namaSiswa + " | Kelas: " + kelasSiswa);

  // ── 3. VALIDASI KELAS ──
  // Siswa hanya boleh absen jika kelasnya sama dengan kelas sesi ini
  if (kelasSiswa != sesiAktif.kelas) {
    showHasil(namaSiswa, waktuStr, "beda_kelas");
    Serial.println("TOLAK — beda kelas. Siswa: " + kelasSiswa + " | Sesi: " + sesiAktif.kelas);
    delay(2500); showStandby(); return;
  }

  // ── 4. Cek duplikat absen di sesi ini ──
  if (sudahAbsen(tanggal, sesiAktif.mapelKey, siswaKey)) {
    showHasil(namaSiswa, waktuStr, "duplikat");
    Serial.println("Sudah absen: " + namaSiswa);
    delay(2500); showStandby(); return;
  }

  // ── 5. Hitung status: hadir / terlambat ──
  String status = hitungStatus(epochSec);
  Serial.println("Status: " + status);

  // ── 6. Simpan ke Firebase ──
  bool ok = simpanAbsensi(
    tanggal, sesiAktif.mapelKey, siswaKey,
    namaSiswa, nisSiswa, kelasSiswa,
    uidStr, epochSec, status
  );

  if (ok) {
    showHasil(namaSiswa, waktuStr, status);
    Serial.println("Absen OK: " + namaSiswa + " [" + status + "] @ " + waktuStr);
  } else {
    showError("Simpan gagal!\nCek koneksi");
  }

  delay(2500);
  showStandby();
}
