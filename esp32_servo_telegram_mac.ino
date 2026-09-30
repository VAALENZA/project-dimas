#include <Wire.h>
#include <RTClib.h>
#include <LiquidCrystal_I2C.h>
#include <Adafruit_PWMServoDriver.h>

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>

// =====================================================
// WIFI (dipakai untuk NTP & Telegram Bot)
// =====================================================
const char* WIFI_SSID     = "Device";
const char* WIFI_PASSWORD = "pusrijaya";

// =====================================================
// TELEGRAM BOT
// =====================================================
// Isi dengan token BARU dari @BotFather (token lama sebaiknya di-revoke)
#define BOT_TOKEN "8660227170:AAHgC5NTXvcIS1u-AqWpDq7gqm4SS2CvHfI"

// Hanya Chat ID ini yang boleh mengontrol servo.
// Boleh isi lebih dari satu, pisahkan koma tanpa spasi.
String ADMIN_CHAT_IDS = "7296970269";

WiFiClientSecure secured_client;
UniversalTelegramBot bot(BOT_TOKEN, secured_client);

// Interval cek pesan baru dari Telegram (ms)
const unsigned long BOT_CHECK_INTERVAL = 1500;
unsigned long lastBotCheck = 0;

// =====================================================
// SINKRON WAKTU OTOMATIS VIA INTERNET (NTP)
// =====================================================
#define USE_NTP true

// WIB = UTC+7, WITA = UTC+8, WIT = UTC+9
const long GMT_OFFSET_SEC      = 7 * 3600;
const int  DAYLIGHT_OFFSET_SEC = 0;

#if USE_NTP
  #include <time.h>
#endif

unsigned long lastNtpSync = 0;
const unsigned long NTP_SYNC_INTERVAL = 6UL * 60UL * 60UL * 1000UL; // 6 jam

// =====================================================
// PIN I2C
// =====================================================
#define SDA_PIN 25
#define SCL_PIN 26

// =====================================================
// ALAMAT I2C
// =====================================================
#define RTC_ADDRESS 0x68
#define LCD_ADDRESS 0x27
#define PCA_ADDRESS 0x40

// =====================================================
// OBJEK
// =====================================================
RTC_DS3231 rtc;
LiquidCrystal_I2C lcd(LCD_ADDRESS, 16, 2);
Adafruit_PWMServoDriver pwm(PCA_ADDRESS);

// =====================================================
// PENGATURAN RTC
// =====================================================
#define FORCE_SET_RTC false

// =====================================================
// PENGATURAN SERVO
// =====================================================
#define SERVO_MIN 110
#define SERVO_MAX 500

#define POSISI_OFF 80
#define POSISI_ON 0

int servoChannel[7] = {0, 1, 2, 3, 4, 5, 6};

bool servoStatus = false;

// =====================================================
// MODE KONTROL: AUTO (jadwal) vs MANUAL (via Telegram)
// =====================================================
bool modeManual = false;
bool manualTarget = false;

// =====================================================
// JADWAL
// =====================================================
int jamON = 6;
int menitON = 0;

int jamOFF = 18;
int menitOFF = 30;

// =====================================================
// MAC ADDRESS
// =====================================================
// Ambil MAC address ESP32 (mode STA).
// Format dengan titik dua, contoh: 24:6F:28:AB:CD:EF
String getMacAddress() {
  return WiFi.macAddress();
}

// Versi tanpa titik dua (12 karakter) supaya muat di LCD 16x2
String getMacAddressPendek() {
  String mac = WiFi.macAddress();
  mac.replace(":", "");
  return mac;
}

// =====================================================
// KONVERSI SUDUT KE PWM
// =====================================================
int sudutKePWM(int sudut) {
  return map(sudut, 0, 180, SERVO_MIN, SERVO_MAX);
}

// =====================================================
// GERAKKAN SEMUA SERVO
// =====================================================
void gerakkanSemuaServo(int sudut) {
  int pwmValue = sudutKePWM(sudut);

  Serial.print("Semua servo bergerak ke ");
  Serial.print(sudut);
  Serial.println(" derajat");

  for (int i = 0; i < 7; i++) {
    pwm.setPWM(servoChannel[i], 0, pwmValue);
    delay(100);
  }
}

// =====================================================
// SERVO ON / OFF
// =====================================================
void servoON() {
  if (!servoStatus) {
    Serial.println("7 SERVO -> ON");
    gerakkanSemuaServo(POSISI_ON);
    servoStatus = true;
  }
}

void servoOFF() {
  if (servoStatus) {
    Serial.println("7 SERVO -> OFF");
    gerakkanSemuaServo(POSISI_OFF);
    servoStatus = false;
  }
}

// =====================================================
// FORMAT WAKTU JADI STRING "HH:MM:SS"
// =====================================================
String formatWaktu(DateTime now) {
  char buf[9];
  sprintf(buf, "%02d:%02d:%02d", now.hour(), now.minute(), now.second());
  return String(buf);
}

// =====================================================
// SINKRONKAN RTC KE NTP
// =====================================================
bool sinkronRTCkeNTP() {
  #if USE_NTP
  if (WiFi.status() != WL_CONNECTED) return false;

  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, "pool.ntp.org", "time.google.com");
  struct tm timeinfo;
  if (getLocalTime(&timeinfo, 10000)) {
    rtc.adjust(DateTime(
      timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
      timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec
    ));
    Serial.println("RTC disinkronkan lewat NTP!");
    return true;
  } else {
    Serial.println("Gagal ambil waktu dari NTP.");
    return false;
  }
  #else
  return false;
  #endif
}

// =====================================================
// CEK JADWAL (HANYA BERJALAN KALAU MODE AUTO)
// =====================================================
void cekJadwal(DateTime now) {

  if (modeManual) {
    if (manualTarget) servoON();
    else servoOFF();
    return;
  }

  int sekarang = now.hour() * 60 + now.minute();
  int waktuON  = jamON * 60 + menitON;
  int waktuOFF = jamOFF * 60 + menitOFF;

  if (waktuON < waktuOFF) {
    if (sekarang >= waktuON && sekarang < waktuOFF) servoON();
    else servoOFF();
  } else {
    if (sekarang >= waktuON || sekarang < waktuOFF) servoON();
    else servoOFF();
  }
}

// =====================================================
// TAMPILKAN LCD
// =====================================================
void tampilkanLCD(DateTime now) {
  lcd.setCursor(0, 0);

  if (now.hour() < 10) lcd.print("0");
  lcd.print(now.hour());
  lcd.print(":");
  if (now.minute() < 10) lcd.print("0");
  lcd.print(now.minute());
  lcd.print(":");
  if (now.second() < 10) lcd.print("0");
  lcd.print(now.second());

  lcd.print(modeManual ? " M" : " A");
  lcd.print("   ");

  lcd.setCursor(0, 1);
  lcd.print(servoStatus ? "SERVO: ON " : "SERVO: OFF");
  lcd.print("       ");
}

// =====================================================
// CEK APAKAH CHAT ID BOLEH KIRIM PERINTAH
// =====================================================
bool isAuthorized(String chatId) {
  String list = "," + ADMIN_CHAT_IDS + ",";
  String target = "," + chatId + ",";
  return list.indexOf(target) >= 0;
}

// =====================================================
// TANGANI PESAN BARU DARI TELEGRAM
// =====================================================
void handleNewMessages(int numNewMessages) {

  for (int i = 0; i < numNewMessages; i++) {

    String chatId = bot.messages[i].chat_id;
    String text   = bot.messages[i].text;
    String fromName = bot.messages[i].from_name;

    Serial.println("Pesan masuk dari " + fromName + " (" + chatId + "): " + text);

    if (!isAuthorized(chatId)) {
      bot.sendMessage(chatId, "Maaf, kamu tidak punya akses untuk mengontrol alat ini.", "");
      continue;
    }

    text.trim();

    if (text == "/on") {

      modeManual = true;
      manualTarget = true;
      servoON();
      bot.sendMessage(chatId, "Servo dinyalakan (mode manual aktif).", "");

    } else if (text == "/off") {

      modeManual = true;
      manualTarget = false;
      servoOFF();
      bot.sendMessage(chatId, "Servo dimatikan (mode manual aktif).", "");

    } else if (text == "/auto") {

      modeManual = false;
      bot.sendMessage(chatId, "Kembali ke mode jadwal otomatis.", "");

    } else if (text == "/status") {

      DateTime now = rtc.now();
      String pesan = "Waktu RTC: " + formatWaktu(now) + "\n";
      pesan += "Mode: " + String(modeManual ? "MANUAL" : "AUTO") + "\n";
      pesan += "Servo: " + String(servoStatus ? "ON" : "OFF") + "\n";
      pesan += "Jadwal: " + String(jamON) + ":" + (menitON < 10 ? "0" : "") + String(menitON);
      pesan += " - " + String(jamOFF) + ":" + (menitOFF < 10 ? "0" : "") + String(menitOFF) + "\n";
      pesan += "MAC: " + getMacAddress() + "\n";
      pesan += "IP: " + WiFi.localIP().toString();
      bot.sendMessage(chatId, pesan, "");

    } else if (text == "/mac") {

      String pesan = "MAC Address: " + getMacAddress() + "\n";
      pesan += "IP Address: " + WiFi.localIP().toString();
      bot.sendMessage(chatId, pesan, "");

    } else if (text == "/sync") {

      if (sinkronRTCkeNTP()) {
        DateTime now = rtc.now();
        bot.sendMessage(chatId, "RTC berhasil disinkronkan. Waktu sekarang: " + formatWaktu(now), "");
      } else {
        bot.sendMessage(chatId, "Gagal sinkron ke NTP. Cek koneksi internet ESP32.", "");
      }

    } else if (text == "/start" || text == "/help") {

      String pesan = "Perintah yang tersedia:\n";
      pesan += "/on - nyalakan servo (mode manual)\n";
      pesan += "/off - matikan servo (mode manual)\n";
      pesan += "/auto - kembali ke jadwal otomatis\n";
      pesan += "/status - lihat status alat\n";
      pesan += "/mac - lihat MAC address & IP\n";
      pesan += "/sync - paksa sinkron ulang waktu RTC ke internet";
      bot.sendMessage(chatId, pesan, "");

    } else {
      bot.sendMessage(chatId, "Perintah tidak dikenali. Ketik /help untuk daftar perintah.", "");
    }
  }
}

// =====================================================
// SETUP
// =====================================================
void setup() {

  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("==============================");
  Serial.println("SISTEM ESP32 + TELEGRAM BOT");
  Serial.println("==============================");

  // ===================================================
  // WIFI
  // ===================================================
  WiFi.mode(WIFI_STA);
  delay(100);

  // Tampilkan MAC address SEBELUM konek WiFi
  // (jadi tetap muncul walau WiFi gagal tersambung)
  Serial.println("MAC Address: " + getMacAddress());

  Serial.println("Menghubungkan ke WiFi...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int percobaan = 0;
  while (WiFi.status() != WL_CONNECTED && percobaan < 30) {
    delay(500);
    Serial.print(".");
    percobaan++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.println("WiFi terhubung! IP: " + WiFi.localIP().toString());
    Serial.println("MAC Address: " + getMacAddress());
    secured_client.setInsecure();
  } else {
    Serial.println();
    Serial.println("GAGAL konek WiFi. Bot Telegram tidak akan berfungsi, tapi jadwal servo tetap jalan.");
  }

  // ===================================================
  // I2C, LCD
  // ===================================================
  Wire.begin(SDA_PIN, SCL_PIN);

  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("SISTEM OTOMATIS");
  lcd.setCursor(0, 1);
  lcd.print("MEMULAI...");
  delay(1500);

  // Tampilkan MAC address di LCD (tanpa titik dua supaya muat 16 karakter)
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("MAC ADDRESS:");
  lcd.setCursor(0, 1);
  lcd.print(getMacAddressPendek());
  delay(3000);

  // ===================================================
  // RTC
  // ===================================================
  Serial.println();
  Serial.println("Mengecek RTC DS3231...");

  if (!rtc.begin()) {
    Serial.println("RTC TIDAK TERDETEKSI!");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("RTC ERROR!");
    lcd.setCursor(0, 1);
    lcd.print("CEK KABEL");
    while (1) delay(1000);
  }

  Serial.println("RTC DS3231 TERDETEKSI!");

  bool waktuBerhasilDiset = sinkronRTCkeNTP();

  if (!waktuBerhasilDiset) {
    if (rtc.lostPower() || FORCE_SET_RTC) {
      Serial.println("RTC disamakan dengan waktu compile (cadangan).");
      rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    } else {
      Serial.println("NTP tidak tersedia, RTC tetap pakai waktu lama yang tersimpan.");
    }
  }

  lastNtpSync = millis();

  // ===================================================
  // PCA9685
  // ===================================================
  Serial.println();
  Serial.println("Mengecek PCA9685...");

  if (!pwm.begin()) {
    Serial.println("PCA9685 TIDAK TERDETEKSI!");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("PCA ERROR!");
    lcd.setCursor(0, 1);
    lcd.print("CEK KABEL");
    while (1) delay(1000);
  }

  Serial.println("PCA9685 TERDETEKSI!");

  pwm.setOscillatorFrequency(27000000);
  pwm.setPWMFreq(50);
  delay(500);

  // ===================================================
  // CEK JADWAL AWAL
  // ===================================================
  DateTime now = rtc.now();
  Serial.println("Waktu RTC: " + formatWaktu(now));
  cekJadwal(now);

  // ===================================================
  // KIRIM NOTIFIKASI STARTUP KE TELEGRAM
  // ===================================================
  if (WiFi.status() == WL_CONNECTED) {
    String pesan = "Sistem ESP32 menyala. Waktu: " + formatWaktu(now) + "\n";
    pesan += "MAC: " + getMacAddress() + "\n";
    pesan += "IP: " + WiFi.localIP().toString() + "\n";
    pesan += "Ketik /help untuk daftar perintah.";
    bot.sendMessage(ADMIN_CHAT_IDS, pesan, "");
  }

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("SISTEM SIAP");
  lcd.setCursor(0, 1);
  lcd.print(servoStatus ? "SERVO: ON" : "SERVO: OFF");
  delay(2000);

  Serial.println();
  Serial.println("SISTEM SIAP");
}

// =====================================================
// LOOP
// =====================================================
void loop() {

  DateTime now = rtc.now();

  tampilkanLCD(now);
  cekJadwal(now);

  Serial.print("RTC: " + formatWaktu(now));
  Serial.print(" | Mode: ");
  Serial.print(modeManual ? "MANUAL" : "AUTO");
  Serial.print(" | SERVO: ");
  Serial.println(servoStatus ? "ON" : "OFF");

  // ===================================================
  // CEK PESAN TELEGRAM BARU
  // ===================================================
  if (WiFi.status() == WL_CONNECTED &&
      millis() - lastBotCheck > BOT_CHECK_INTERVAL) {

    int numNewMessages = bot.getUpdates(bot.last_message_received + 1);

    while (numNewMessages) {
      handleNewMessages(numNewMessages);
      numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    }

    lastBotCheck = millis();
  }

  // ===================================================
  // RESYNC RTC KE NTP SECARA BERKALA
  // ===================================================
  if (WiFi.status() == WL_CONNECTED &&
      millis() - lastNtpSync > NTP_SYNC_INTERVAL) {

    sinkronRTCkeNTP();
    lastNtpSync = millis();
  }

  delay(1000);
}
