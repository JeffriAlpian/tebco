/**
 * @file config.h
 * @brief TEBCO - AI Voice Medicine Reminder Assistant
 *        Central configuration: GPIO pins, constants, credentials.
 *
 *  *** HOW TO USE ***
 *  1. Set your hardware GPIO pins in the sections below.
 *  2. Fill in FIREBASE_HOST / FIREBASE_AUTH with your project details.
 *  3. Set AI_VOICE_QUERY_URL to the HTTP URL of your n8n Webhook.
 *  4. Set RFID_ALLOWED_UID to your wristband UID bytes.
 */

#pragma once

// ─────────────────────────────────────────────
//  PROJECT META
// ─────────────────────────────────────────────
#define FIRMWARE_VERSION   "1.0.0"
#define DEVICE_NAME        "TEBCO"

// ─────────────────────────────────────────────
//  GPIO PIN MAPPING
// ─────────────────────────────────────────────

// ===== ESP32-S3 WROOM-1 N16R8 =====
// ZONA TERLARANG (jangan dipakai peripheral apapun):
//   GPIO  0, 3, 45, 46 = Strapping pins
//   GPIO 19, 20         = USB D-/D+
//   GPIO 26-37          = Flash/PSRAM OCTAL internal (N16R8)
//   GPIO 43, 44         = UART0 TX/RX
//
// PETA PIN (tidak ada konflik):
//   RFID  SPI1 : SCK=12 MISO=13 MOSI=11 SS=10 RST=9
//   TFT   SPI2 : SCK=21 MOSI=47 DC=14   RST=5  CS=-1
//   MIC   I2S0 : SCK=8  WS=40   SD=41   (dipindah dari 18/17/15 -- pin lama
//                terbukti noise saat pengujian hardware. Pin 47/10/21 yang
//                terbukti bersih di test terpisah TIDAK dipakai di sini
//                karena bentrok dengan TFT_MOSI/TFT_SCK/RFID_SS)
//   SPK   I2S1 : BCLK=6 LRCK=7  DIN=16  SD=4 (shutdown/enable ampli)
//   Servo      : S1=38  S2=39
//   Ultrasonic : TRIG=2 ECHO=42  (dipindah dari 4 -- bentrok dgn SPK_SD)
//   Battery ADC: GPIO=1

// --- RFID MFRC-522 (SPI bus 1) ---
// Colok: SCK->12, MISO->13, MOSI->11, SDA/SS->10, RST->9
#define PIN_RFID_SCK       12
#define PIN_RFID_MISO      13
#define PIN_RFID_MOSI      11
#define PIN_RFID_SS        10
#define PIN_RFID_RST        9

// --- TFT Display ST7789 GMT130-V1.0 (SPI bus 2, terpisah dari RFID) ---
// Modul ini TIDAK PUNYA pin CS fisik -> wajib bus SPI sendiri.
// PERBAIKAN dari versi lama:
//   SCK  dipindah: GPIO20 (USB D+!) -> GPIO21
//   MOSI dipindah: GPIO21 (bentrok) -> GPIO47
//   DC   dipindah: GPIO9 (=RFID_RST!) -> GPIO14
// Colok: SCK->21, SDA/MOSI->47, DC->14, RES->5, BLK->3.3V
#define PIN_TFT_SCK        21
#define PIN_TFT_MOSI       47
#define PIN_TFT_DC         14
#define PIN_TFT_RST         5
#define PIN_TFT_CS         -1   // GMT130-V1.0 tidak punya CS fisik

// --- I2S Microphone INMP441 (I2S port 0) ---
// PERBAIKAN: pin lama (SCK=18, WS=17, SD=46 -- 46 adalah strapping pin,
// bukan 15 seperti niat awal) terbukti menghasilkan noise/distorsi saat
// pengujian hardware. Dipindah ke GPIO yang benar-benar bebas: 8, 40, 41.
// Colok ulang: SCK->8, WS->40, SD->41, L/R->GND
#define PIN_MIC_SCK         8
#define PIN_MIC_WS         40
#define PIN_MIC_SD         41

// --- I2S DAC/Amp MAX98357A (I2S port 1) ---
// PERBAIKAN: BCLK dipindah GPIO3 (strapping pin!) -> GPIO6
// Colok: BCLK->6, LRC->7, DIN->16, SD(shutdown/enable)->4
#define PIN_SPK_BCLK        7
#define PIN_SPK_LRCK        6
#define PIN_SPK_DIN        16
#define PIN_SPK_SD          4

// --- Servo SG90 ---
// Colok: Servo1->38, Servo2->39
#define PIN_SERVO          38
#define PIN_SERVO_2        39

// --- Ultrasonic HC-SR04 ---
// PERBAIKAN: ECHO dipindah GPIO4 (bentrok dgn PIN_SPK_SD) -> GPIO42
// Colok: TRIG->2, ECHO->42
#define PIN_US_TRIG         2
#define PIN_US_ECHO        42


// Jarak deteksi obat (cm)
#define MAX_MEDICINE_DISTANCE_CM 3

// ─────────────────────────────────────────────
//  SERVO ANGLES
// ─────────────────────────────────────────────
#define SERVO1_LOCKED_DEG    45
#define SERVO1_OPEN_DEG       0
#define SERVO2_LOCKED_DEG     0
#define SERVO2_OPEN_DEG      47
#define SERVO_OPEN_MS       100

// ─────────────────────────────────────────────
//  I2S AUDIO CONFIG
// ─────────────────────────────────────────────
#define I2S_MIC_PORT       I2S_NUM_0
#define I2S_SPK_PORT       I2S_NUM_1
#define I2S_SAMPLE_RATE    16000
#define I2S_BITS           16
#define I2S_DMA_BUF_COUNT   4
#define I2S_DMA_BUF_LEN   256
#define AUDIO_RECORD_SEC    5

// ─────────────────────────────────────────────
//  Wi-Fi MANAGER
// ─────────────────────────────────────────────
#define WIFI_AP_SSID       "TEBCO_Setup"
#define WIFI_AP_PASS       "12345678"
#define WIFI_CONNECT_TIMEOUT_MS  15000
#define PREF_NAMESPACE     "tebco_wifi"
#define PREF_KEY_SSID      "ssid"
#define PREF_KEY_PASS      "pass"
#define PREF_NAMESPACE_DEV "tebco_dev"
#define PREF_KEY_ALIAS     "alias"

// ─────────────────────────────────────────────
//  FIREBASE CONFIG
// ─────────────────────────────────────────────
#define FIREBASE_HOST      "tebco-9e7a6-default-rtdb.asia-southeast1.firebasedatabase.app"
#define FIREBASE_AUTH      "FKic265uaJqjTc19MjrL430nU6O9vsIZt4XIEr62"

// ─────────────────────────────────────────────
//  WHATSAPP GATEWAY
// ─────────────────────────────────────────────
#define WA_API_URL         "https://api.sidobe.com/wa/v1/send-message"
#define WA_SECRET_KEY      "rnwPvomARUXEnfIXfYswZSlvtwaIIIQLYOEoLttOBcXZTjaEsG"

// ─────────────────────────────────────────────
//  AI VOICE QUERY (n8n Webhook)
// ─────────────────────────────────────────────
#define AI_VOICE_QUERY_URL  "https://n8n.jeffrialpian.my.id/webhook/tebco-voice-query"
#define AI_WEBHOOK_SECRET   "rahasia-tebco-123"

// ─────────────────────────────────────────────
//  WAKE WORD CONFIGURATION (Tuned)
// ─────────────────────────────────────────────
#define KWS_TARGET_LABEL_CFG      "alexa"

// Tetap sensitif saat kata target benar-benar dipanggil, tetapi tidak mudah terpicu oleh echo speaker
#define KWS_THRESHOLD_CFG         0.60f

// Trigger instan hanya saat score sangat kuat
#define KWS_INSTANT_THRESHOLD_CFG 0.72f

// Butuh beberapa slice konsisten agar tidak memicu dari noise acak
#define KWS_CONSECUTIVE_HITS_CFG  2

// Selisih confidence minimal terhadap kelas lain agar wake word dianggap valid
#define KWS_CONFIDENCE_MARGIN_CFG 0.10f

// DEBUG SEMENTARA: aktifkan untuk lihat persis di titik mana check() berhenti
// (isPlaying/mute/RMS gate/dsb). Comment/hapus baris ini kalau sudah selesai
// debugging supaya Serial Monitor tidak banjir log.
// #define KWS_DEBUG_VERBOSE

// FILTER RMS: Jika RMS dibawah nilai ini, murni dianggap noise ruangan -> Abaikan AI
// PENTING: nilai ini WAJIB dikalibrasi ulang pakai TEBCO_WakeWordCalibration.ino
// setelah amplifikasi 2x dihapus dari wake_word.cpp (skala RMS sekarang beda).
// Sedikit lebih ketat agar echo / noise dasar tidak memicu deteksi palsu
#define KWS_RMS_SILENCE_GATE      650.0f

// Berapa lama (ms) engine tetap dibisukan SETELAH speaker berhenti memutar
// audio (TTS/tone), untuk memberi waktu gema/dengungan sisa meredam sebelum
// mic dipercaya lagi. Naikkan kalau masih ada ghost detection tepat setelah
// device selesai bicara; turunkan kalau device jadi terasa lambat merespons.
#define KWS_POST_TTS_MUTE_MS      1500

// Cooldown tambahan sesaat setelah wake word berhasil terdeteksi untuk
// mencegah echo dari respons TTS langsung memicu deteksi berikutnya.
#define KWS_POST_DETECT_COOLDOWN_MS 2500


// ── VAD (Voice Activity Detection) CONFIGURATION ─────────────────────

// Batas RMS keheningan (Di atas noise floor ~800, di bawah suara orang ~2500)
#define VAD_SILENCE_THRESHOLD     1300.0f  

// Durasi diam sebelum rekaman OTOMATIS STOP (dalam milidetik)
// Set ke 1500 ms (1.5 detik) agar jeda napas/berpikir tidak memotong rekaman
#define VAD_SILENCE_DURATION_MS   1500     

// Durasi minimal ucapan agar dianggap percakapan valid (mencegah ketukan/batuk)
#define VAD_MIN_SPEECH_MS         1000      

// Batas durasi rekaman maksimal (misal: 10 detik) agar tidak menggantung selamanya
#define VAD_MAX_RECORDING_MS      10000

// ─────────────────────────────────────────────
//  RFID - Allowed UID
// ─────────────────────────────────────────────
#define RFID_ALLOWED_UID   {0x61, 0x47, 0x17, 0x17}
#define RFID_UID_LEN        4

// ─────────────────────────────────────────────
//  TIMING CONSTANTS
// ─────────────────────────────────────────────
#define SCHEDULE_CHECK_INTERVAL_MS  30000
#define HEARTBEAT_INTERVAL_MS       30000
#define ASSIGNMENT_POLL_MS          15000
#define MISSED_DOSE_WINDOW_MIN      30
#define NTP_SERVER         "pool.ntp.org"
#define GMT_OFFSET_SEC     25200
#define DST_OFFSET_SEC     0

// ─────────────────────────────────────────────
//  BATTERY CONFIG
// ─────────────────────────────────────────────
#define PIN_BATTERY_ADC        1
// ── Battery Monitoring (Voltage Divider) ─────────────────────────────────────
// Pembagi tegangan: BAT(+) --[22K]-- ADC_PIN --[10K]-- GND
// Rasio = 10K / (22K + 10K) = 0.3125  ->  faktor kali balik = 3.2
#define BATT_DIVIDER_RATIO  5.40f     // (R_ATAS + R_BAWAH) / R_BAWAH = (22+10)/10
#define BATT_ADC_VREF       3.3f     // Referensi ADC ESP32-S3
#define BATT_ADC_RES        4095.0f  // Resolusi 12-bit
#define BATT_VOLT_EMPTY     6.40f     
#define BATT_VOLT_FULL      7.85f     