/**
 * @file wemos_firmware.ino
 * @brief Firmware Wemos D1 Mini – TEBCO Dispenser & RFID Controller
 *
 * Tanggung jawab:
 *  - Autentikasi RFID (kartu gelang pasien)
 *  - Kontrol servo (buka/tutup laci dispenser)
 *  - Deteksi obat diambil (sensor IR/Ultrasonic)
 *  - Sinkronisasi jadwal dari Firebase secara mandiri
 *  - Kirim notifikasi WhatsApp saat dosis tuntas/bolos
 *  - Komunikasi dua arah ke ESP32-S3 via UART
 *
 * Board: Wemos D1 Mini (ESP8266)
 * Library: MFRC522, ESP8266WiFi, ESP8266HTTPClient, ArduinoJson, Servo
 */

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
#include <MFRC522.h>
#include <SPI.h>
#include <ArduinoJson.h>
#include <time.h>

#include "config_wemos.h"
#include "uart_comm_wemos.h"
#include "dispenser_wemos.h"

// ── Objek Global ──────────────────────────────────────────────────────────────
UartCommWemos uartComm;
DispenserWemos dispenser;
MFRC522 rfid(PIN_RFID_SS, PIN_RFID_RST);

// ── Data Pasien Aktif (diterima dari ESP32-S3 via UART) ───────────────────────
struct PatientData {
    String patientId  = "";
    String name       = "";
    String disease    = "";
    String waNumber   = "";
    bool   loaded     = false;
} patient;

// ── Data Jadwal (diterima dari ESP32-S3 atau dari Firebase langsung) ──────────
struct Schedule {
    String time;      // "HH:MM"
    int    quantity;
    String status;    // "Pending", "Tuntas", "Bolos"
    String schedKey;  // Key Firebase
};
Schedule schedules[10];
int      scheduleCount = 0;

// ── State ─────────────────────────────────────────────────────────────────────
unsigned long lastSchedCheck    = 0;
unsigned long lastHeartbeat     = 0;
unsigned long lastAssignmentPoll= 0;
bool          dispenserWaitingIR= false;
int           activeSlot        = 1;

// ── Allowed RFID UID ──────────────────────────────────────────────────────────
const byte ALLOWED_UID[]  = RFID_ALLOWED_UID;

// ── Forward Declarations ──────────────────────────────────────────────────────
void setupWiFi();
void syncTime();
void handleRFID();
void handleDispenserCheck();
void handleScheduleCheck();
bool isUidAllowed(MFRC522::Uid &uid);
String getCurrentTimeStr();
String getNextDoseTime();
void updateFirebaseStatus(const String &schedKey, const String &status);
void sendWhatsApp(const String &message);
void fetchScheduleFromFirebase();
void fetchAssignmentFromFirebase();

// ─────────────────────────────────────────────────────────────────────────────
//  SETUP
// ─────────────────────────────────────────────────────────────────────────────
void setup() {
    // UART ke ESP32-S3 (Serial default di Wemos = UART0)
    uartComm.begin();

    // Setup SPI + RFID
    SPI.begin();
    rfid.PCD_Init();

    // Setup Dispenser
    dispenser.begin();

    // Register Callback Handlers dari UART
    UartHandlers handlers;

    handlers.onOpenDispenser = [](int slot) {
        dispenser.open();
        dispenserWaitingIR = true;
        activeSlot = slot;
    };

    handlers.onCloseDispenser = [](int slot) {
        dispenser.close();
        dispenserWaitingIR = false;
    };

    handlers.onPatientUpdate = [](String id, String name, String disease, String wa) {
        patient.patientId = id;
        patient.name      = name;
        patient.disease   = disease;
        patient.waNumber  = wa;
        patient.loaded    = true;
        // Setelah data pasien diterima, ambil jadwal dari Firebase
        fetchScheduleFromFirebase();
    };

    handlers.onScheduleUpdate = [](String schedulesJson) {
        // Parse jadwal dari JSON yang dikirim ESP32-S3
        StaticJsonDocument<512> doc;
        deserializeJson(doc, schedulesJson);
        JsonArray arr = doc.as<JsonArray>();
        scheduleCount = 0;
        for (JsonObject sched : arr) {
            if (scheduleCount >= 10) break;
            schedules[scheduleCount].time     = sched["time"].as<String>();
            schedules[scheduleCount].quantity = sched["qty"] | 1;
            schedules[scheduleCount].status   = "Pending";
            scheduleCount++;
        }
    };

    handlers.onWifiCredentials = [](String ssid, String pass) {
        // Terima kredensial dari ESP32-S3, simpan ke flash, dan konek
        WiFi.begin(ssid.c_str(), pass.c_str());
        // Biarkan loop() yang mengecek status koneksi (non-blocking)
    };

    uartComm.setHandlers(handlers);

    // Hubungkan WiFi
    setupWiFi();

    // Sync waktu NTP
    syncTime();

    // Beritahu ESP32-S3 bahwa Wemos sudah siap
    uartComm.sendWemosReady();
}

// ─────────────────────────────────────────────────────────────────────────────
//  LOOP UTAMA
// ─────────────────────────────────────────────────────────────────────────────
void loop() {
    // 1. Proses perintah dari ESP32-S3
    uartComm.loop();

    // 2. Cek RFID
    handleRFID();

    // 3. Cek status dispenser (apakah obat sudah diambil? timeout?)
    handleDispenserCheck();

    // 4. Cek jadwal obat setiap 30 detik
    if (millis() - lastSchedCheck > SCHEDULE_CHECK_INTERVAL_MS) {
        lastSchedCheck = millis();
        handleScheduleCheck();
    }

    // 5. Poll assignment pasien dari Firebase tiap 15 detik
    if (millis() - lastAssignmentPoll > ASSIGNMENT_POLL_MS) {
        lastAssignmentPoll = millis();
        if (!patient.loaded) fetchAssignmentFromFirebase();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  HANDLER RFID
// ─────────────────────────────────────────────────────────────────────────────
void handleRFID() {
    if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) return;

    // Bangun string UID untuk log
    String uid = "";
    for (byte i = 0; i < rfid.uid.size; i++) {
        if (rfid.uid.uidByte[i] < 0x10) uid += "0";
        uid += String(rfid.uid.uidByte[i], HEX);
    }
    uid.toUpperCase();

    bool uidValid = isUidAllowed(rfid.uid);

    if (!uidValid) {
        // Kartu tidak dikenal
        uartComm.sendRfidReject(uid);
        rfid.PICC_HaltA();
        return;
    }

    if (!patient.loaded) {
        // Kartu valid tapi belum ada pasien di-assign
        uartComm.sendRfidWrongTime("belum ada jadwal");
        rfid.PICC_HaltA();
        return;
    }

    // Cek apakah sekarang waktunya minum obat
    String now = getCurrentTimeStr();
    for (int i = 0; i < scheduleCount; i++) {
        if (schedules[i].status == "Pending" && schedules[i].time == now) {
            // TEPAT WAKTU → Buka dispenser
            dispenser.open();
            dispenserWaitingIR = true;
            activeSlot = i + 1;
            uartComm.sendRfidAccept(patient.name, patient.patientId);
            rfid.PICC_HaltA();
            return;
        }
    }

    // Kartu valid tapi di luar jadwal
    String nextDose = getNextDoseTime();
    uartComm.sendRfidWrongTime(nextDose);
    rfid.PICC_HaltA();
}

// ─────────────────────────────────────────────────────────────────────────────
//  HANDLER DISPENSER (Cek IR + Timeout)
// ─────────────────────────────────────────────────────────────────────────────
void handleDispenserCheck() {
    if (!dispenser.isOpen()) return;

    // Cek apakah obat sudah diambil
    if (dispenserWaitingIR && dispenser.isMedicineTaken()) {
        dispenser.close();
        dispenserWaitingIR = false;

        String now = getCurrentTimeStr();
        uartComm.sendDoseTaken(activeSlot, now);

        // Update status ke Firebase
        int idx = activeSlot - 1;
        if (idx >= 0 && idx < scheduleCount) {
            schedules[idx].status = "Tuntas";
            updateFirebaseStatus(schedules[idx].schedKey, "Tuntas");

            // Kirim WhatsApp ke keluarga
            String msg = "✅ *TEBCO Notifikasi*\n";
            msg += "Pasien *" + patient.name + "* telah mengambil obat.\n";
            msg += "Waktu: " + now + "\n";
            msg += "Jadwal: " + schedules[idx].time;
            sendWhatsApp(msg);
        }
        return;
    }

    // Timeout 30 detik → tutup paksa
    if (dispenser.checkTimeout(30000)) {
        dispenserWaitingIR = false;
        // Obat tidak diambil dalam 30 detik, tapi tidak dianggap "Bolos"
        // (bisa jadi pasien lambat mengambil). Biarkan schedule check menangani.
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  HANDLER JADWAL (Cek Dosis Terlewat)
// ─────────────────────────────────────────────────────────────────────────────
void handleScheduleCheck() {
    if (!patient.loaded || scheduleCount == 0) return;

    String now = getCurrentTimeStr(); // "HH:MM"
    struct tm timeinfo;
    getLocalTime(&timeinfo);

    for (int i = 0; i < scheduleCount; i++) {
        if (schedules[i].status != "Pending") continue;

        // Hitung selisih waktu
        int schedHour = schedules[i].time.substring(0, 2).toInt();
        int schedMin  = schedules[i].time.substring(3, 5).toInt();
        int nowMin    = timeinfo.tm_hour * 60 + timeinfo.tm_min;
        int schedMinTotal = schedHour * 60 + schedMin;

        int delta = nowMin - schedMinTotal;

        if (delta >= MISSED_DOSE_WINDOW_MIN) {
            // TERLEWAT → Tandai Bolos
            schedules[i].status = "Bolos";
            updateFirebaseStatus(schedules[i].schedKey, "Bolos");

            // Kirim notifikasi ke ESP32-S3
            uartComm.sendDoseMissed(schedules[i].time);

            // Kirim WhatsApp ke keluarga
            String msg = "⚠️ *TEBCO Peringatan*\n";
            msg += "Pasien *" + patient.name + "* BELUM mengambil obat!\n";
            msg += "Jadwal: " + schedules[i].time + "\n";
            msg += "Mohon segera diperiksa.";
            sendWhatsApp(msg);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  HELPER FUNGSI
// ─────────────────────────────────────────────────────────────────────────────

bool isUidAllowed(MFRC522::Uid &uid) {
    if (uid.size != RFID_UID_LEN) return false;
    for (byte i = 0; i < RFID_UID_LEN; i++) {
        if (uid.uidByte[i] != ALLOWED_UID[i]) return false;
    }
    return true;
}

String getCurrentTimeStr() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) return "00:00";
    char buf[6];
    snprintf(buf, sizeof(buf), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
    return String(buf);
}

String getNextDoseTime() {
    for (int i = 0; i < scheduleCount; i++) {
        if (schedules[i].status == "Pending") return schedules[i].time;
    }
    return "tidak ada jadwal tersisa";
}

void setupWiFi() {
    // Coba konek pakai kredensial yang tersimpan di flash ROM (dari koneksi sebelumnya)
    WiFi.begin();
    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries < 20) {
        uartComm.loop(); // Tetap baca UART selagi menunggu!
        delay(500);
        tries++;
    }
    // Jika gagal, jangan block selamanya! Wemos akan menunggu kredensial
    // baru dari ESP32-S3 yang dikirim via UART (event WIFI_CREDENTIALS)
}

void syncTime() {
    if (WiFi.status() != WL_CONNECTED) return; // Jangan block jika tidak ada internet
    
    configTime(GMT_OFFSET_SEC, 0, NTP_SERVER);
    struct tm timeinfo;
    int tries = 0;
    while (!getLocalTime(&timeinfo) && tries < 20) {
        uartComm.loop(); // Tetap baca UART
        delay(500);
        tries++;
    }
}

void updateFirebaseStatus(const String &schedKey, const String &status) {
    if (WiFi.status() != WL_CONNECTED || schedKey.isEmpty()) return;

    BearSSL::WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;

    String url = "https://" + String(FIREBASE_HOST) + "/schedules/"
                 + patient.patientId + "/" + schedKey + "/status.json?auth="
                 + FIREBASE_AUTH;

    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");
    http.PUT("\"" + status + "\"");
    http.end();
}

void sendWhatsApp(const String &message) {
    if (WiFi.status() != WL_CONNECTED || patient.waNumber.isEmpty()) return;

    BearSSL::WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;

    http.begin(client, WA_API_URL);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", WA_SECRET_KEY);

    StaticJsonDocument<256> doc;
    doc["phone"]   = patient.waNumber;
    doc["message"] = message;
    String body;
    serializeJson(doc, body);
    http.POST(body);
    http.end();
}

void fetchScheduleFromFirebase() {
    if (!patient.loaded || WiFi.status() != WL_CONNECTED) return;

    BearSSL::WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;

    String url = "https://" + String(FIREBASE_HOST) + "/schedules/"
                 + patient.patientId + ".json?auth=" + FIREBASE_AUTH;

    http.begin(client, url);
    int code = http.GET();
    if (code != 200) { http.end(); return; }

    DynamicJsonDocument doc(1024);
    deserializeJson(doc, http.getString());
    http.end();

    scheduleCount = 0;
    for (JsonPair kv : doc.as<JsonObject>()) {
        if (scheduleCount >= 10) break;
        schedules[scheduleCount].schedKey = kv.key().c_str();
        schedules[scheduleCount].time     = kv.value()["time"] | "00:00";
        schedules[scheduleCount].quantity = kv.value()["quantity"] | 1;
        schedules[scheduleCount].status   = kv.value()["status"] | "Pending";
        scheduleCount++;
    }
}

void fetchAssignmentFromFirebase() {
    // Dapatkan MAC Address sebagai Device ID
    String deviceId = "TEBCO-" + WiFi.macAddress();
    deviceId.replace(":", "");

    BearSSL::WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;

    String url = "https://" + String(FIREBASE_HOST) + "/devices/"
                 + deviceId + "/assigned_patient.json?auth=" + FIREBASE_AUTH;

    http.begin(client, url);
    int code = http.GET();
    if (code != 200) { http.end(); return; }

    String body = http.getString();
    http.end();
    body.replace("\"", "");
    body.trim();
    if (body.isEmpty() || body == "null") return;

    // Ada pasien baru → ambil data profil
    patient.patientId = body;
    // Ambil detail pasien
    url = "https://" + String(FIREBASE_HOST) + "/patients/"
          + patient.patientId + ".json?auth=" + FIREBASE_AUTH;

    http.begin(client, url);
    code = http.GET();
    if (code != 200) { http.end(); return; }

    StaticJsonDocument<256> doc;
    deserializeJson(doc, http.getString());
    http.end();

    patient.name      = doc["name"]      | "Pasien";
    patient.disease   = doc["disease"]   | "Umum";
    patient.waNumber  = doc["wa_number"] | "";
    patient.loaded    = true;

    fetchScheduleFromFirebase();
}
