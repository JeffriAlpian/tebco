/**
 * @file TEBCO.ino
 * @brief TEBCO – AI Voice Medicine Reminder Assistant
 *        Main firmware entry point. Orchestrates all subsystems.
 *
 * ┌────────────────────────────────────────────────────────────┐
 * │  REQUIRED LIBRARIES (install via Arduino Library Manager)  │
 * │  ────────────────────────────────────────────────────────  │
 * │  • ArduinoJson          (Benoit Blanchon)                  │
 * │  • MFRC522              (GithubCommunity / miguelbalboa)   │
 * │  • ESP32Servo           (Kevin Harrington)                 │
 * │  • base64               (Densaugeo)                        │
 * │  Board: ESP32-S3 Dev Module  (or ESP32 Dev Module for U)   │
 * │  Flash: 16MB  |  PSRAM: 8MB OPI  (S3 only)                 │
 * └────────────────────────────────────────────────────────────┘
 *
 * Hospital-Scale Architecture:
 *   - Device ID = derived from MAC address (unique, permanent)
 *   - Patient assignment = managed by Hospital UI via Firebase
 *   - Device polls Firebase every ASSIGNMENT_POLL_MS for assignment
 *   - Sends heartbeat every HEARTBEAT_INTERVAL_MS
 *
 * Copyright (c) 2025 TEBCO Project. MIT License.
 */

 

// ── System Headers ────────────────────────────────────────────────────────────
#include <Arduino.h>
#include <time.h>
#include <ESP32PWM.h> // Untuk alokasi LEDC timer manual (cegah konflik dengan I2S)
#include <WebServer.h> // Tambahan untuk Standby Web Server
// #include <battery_monitor.h>

// ── Project Modules ───────────────────────────────────────────────────────────
#include "include/config.h"
#include "include/wifi_manager.h"
#include "include/firebase_manager.h"
#include "include/wa_gateway.h"
#include "include/rfid_auth.h"
#include "include/dispenser.h"
#include "include/display_manager.h"
#include "include/audio_pipeline.h"
#include "include/ai_assistant.h"
#include "include/schedule_manager.h"
#include "include/wake_word.h"

// ── Global Subsystem Instances ────────────────────────────────────────────────
WiFiManager wifiMgr;
FirebaseManager firebase;
RFIDAuth rfid;
Dispenser dispenser;
DisplayManager display;
AudioPipeline audio;
WakeWordEngine wakeWord(audio); // Edge Impulse KWS engine
AIAssistant *aiAssistant = nullptr;
ScheduleManager *schedMgr = nullptr;

// ── Application State ─────────────────────────────────────────────────────────
UserProfile userProfile;
bool profileLoaded = false;
String currentPatientId = ""; // "" = no patient assigned
String deviceAlias = "";
String deviceId = "";

// ── Web Server Standby ──
WebServer standbyServer(80);
bool manualTrigger = false;

unsigned long lastSchedCheck = 0;
unsigned long lastSchedFetch = 0;
unsigned long lastHeartbeat = 0;
unsigned long lastAssignmentPoll = 0;

// ── Forward Declarations ──────────────────────────────────────────────────────
void handleScheduleCheck();
void handleDispenserUpdate();
void handleRFIDTap();
void handleVoiceQuery();
void checkWakeWord();
void pollPatientAssignment();
void onPatientAssigned(const String &patientId);
void onPatientUnassigned();

// ── Standby Web Handlers ──
void handleStandbyRoot() {
    String html = R"rawliteral(
<!DOCTYPE html><html lang="id"><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>TEBCO Dashboard</title>
<style>
  body{font-family:'Segoe UI',Arial,sans-serif;background:#0f0f1a;color:#eee;text-align:center;padding:40px;}
  button{padding:20px 40px;background:linear-gradient(135deg,#0f9b8e,#0d7a72);color:#fff;
         border:none;border-radius:12px;font-size:24px;cursor:pointer;font-weight:600;
         box-shadow:0 8px 24px rgba(15,155,142,0.4);transition:transform 0.2s;}
  button:active{transform:scale(0.95);}
</style></head><body>
  <h2>TEBCO Dashboard</h2>
  <p style="color:#aaa;margin-bottom:40px">Tekan tombol di bawah untuk membangunkan TEBCO</p>
  <button onclick="fetch('/listen').then(r=>console.log('OK'))">🎙️ Dengarkan</button>
</body></html>
)rawliteral";
    standbyServer.send(200, "text/html", html);
}

void handleStandbyListen() {
    manualTrigger = true;
    standbyServer.send(200, "text/plain", "Listening");
}

// ── Helpers ───────────────────────────────────────────────────────────────────

// Cache nilai terakhir supaya bisa dibaca fungsi lain (mis. untuk log tegangan)
// tanpa perlu sampling ulang.
static float g_lastBatVoltage = 0.0f;

int getBatteryPercentage()
{
    // Mode tanpa baterai (colok listrik/USB langsung)
    return 100;
}

// ─────────────────────────────────────────────────────────────────────────────
//  SETUP
// ─────────────────────────────────────────────────────────────────────────────
void setup()
{
    Serial.begin(115200);
    Serial.println("\n\n[BOOT] --- MULAI SETUP ---");
    delay(100);
    

    Serial.println("[BOOT] Inisialisasi SPI...");
    SPI.begin(PIN_RFID_SCK, PIN_RFID_MISO, PIN_RFID_MOSI, PIN_RFID_SS);

    // Serial.println("[BOOT] Inisialisasi ADC Baterai...");
    // // analogReadResolution(12);       // 0-4095
    // // analogSetAttenuation(ADC_11db); // Range ~0-3.3V, dibutuhkan karena Vout pembagi bisa ~2.0-2.6V
    // // pinMode(PIN_BATTERY_ADC, INPUT);

    Serial.println("[BOOT] Mendapatkan Device ID...");
    deviceId = FirebaseManager::getDeviceId();
    deviceAlias = WiFiManager::getDeviceAlias();
    if (deviceAlias.isEmpty())
        deviceAlias = deviceId;

    Serial.println("\n╔══════════════════════════════════════╗");
    Serial.println("║   TEBCO Firmware v" FIRMWARE_VERSION " ║");
    Serial.printf("║   ID    : %-26s║\n", deviceId.c_str());
    Serial.printf("║   Lokasi: %-26s║\n", deviceAlias.c_str());
    Serial.println("╚══════════════════════════════════════╝");

    // ── Display first ──
    display.begin();

    // ── LEDC Timer Allocation ──
    ESP32PWM::allocateTimer(0); // Servo A
    ESP32PWM::allocateTimer(1); // Servo B
    Serial.println("[BOOT] LEDC timer allocated: 0, 1 untuk servo.");

    // ── Audio Pipeline ──
    if (audio.begin())
    {
        audio.playDroneStartup();
        wakeWord.begin();
    }
    else
    {
        Serial.println("[SYSTEM] ERROR: Gagal memulai Audio Pipeline!");
        display.setExpression(FaceExpression::SAD);
    }

    // ── RFID ──
    rfid.begin();

    // ── Dispenser ──
    dispenser.begin();

    // ── Wi-Fi ──
    if (!wifiMgr.begin())
    {
        Serial.println("[MAIN] Wi-Fi failed.");
        return;
    }

    // Mulai Standby Server jika terkoneksi Wi-Fi
    if (WiFi.status() == WL_CONNECTED) {
        standbyServer.on("/", HTTP_GET, handleStandbyRoot);
        standbyServer.on("/listen", HTTP_GET, handleStandbyListen);
        standbyServer.begin();
        Serial.println("[MAIN] Standby Web Server started on port 80");

        // Tampilkan IP di pojok kanan bawah layar
        String localIP = WiFi.localIP().toString();
        display.setIPAddress(localIP);
        Serial.printf("[MAIN] IP ditampilkan di layar: %s\n", localIP.c_str());
    }

    // ── NTP Time Sync ──
    configTime(GMT_OFFSET_SEC, DST_OFFSET_SEC, NTP_SERVER);
    Serial.print("[MAIN] Syncing NTP time");
    struct tm timeinfo;
    for (int i = 0; i < 20; i++)
    {
        if (getLocalTime(&timeinfo))
        {
            Serial.println(" OK");
            break;
        }
        Serial.print(".");
        delay(500);
    }

    // ── Firebase ──
    firebase.begin();
    firebase.registerDevice(deviceAlias, getBatteryPercentage(), "online");

    // ── AI Assistant ──
    aiAssistant = new AIAssistant(audio, display);

    // ── Initial assignment poll ──
    pollPatientAssignment();

    Serial.println("[MAIN] Boot complete. Waiting for patient assignment...");
}

// ─────────────────────────────────────────────────────────────────────────────
//  MAIN LOOP
// ─────────────────────────────────────────────────────────────────────────────
void loop()
{
    unsigned long now = millis();

    // 1. UPDATE DISPLAY & AUDIO LOOP (Prioritas Utama Non-Blocking)
    static unsigned long lastStatusCheck = 0;
    if (now - lastStatusCheck > 10000)
    {
        lastStatusCheck = now;
        display.updateStatusIcons(getBatteryPercentage(), WiFi.status() == WL_CONNECTED);
        // Serial.printf("[STATUS] Battery: %d%% | Wi-Fi: %s\n", getBatteryPercentage(),
        //               (WiFi.status() == WL_CONNECTED) ? "Connected" : "Disconnected");
    }

    display.update(); // Render animasi mata Mochi
    audio.loop();     // Streaming TTS Audio

    // 2. PENGECEKAN WAKE WORD (Harus secepat dan semulus mungkin)
    checkWakeWord();

    // 3. POLL PATIENT ASSIGNMENT (Default: 15 Detik sekali)
    if (now - lastAssignmentPoll >= ASSIGNMENT_POLL_MS || lastAssignmentPoll == 0)
    {
        lastAssignmentPoll = now;
        pollPatientAssignment();
    }

    // 4. HEARTBEAT TO FIREBASE (Setiap 30 Detik sekali)
    if (now - lastHeartbeat >= HEARTBEAT_INTERVAL_MS || lastHeartbeat == 0)
    {
        lastHeartbeat = now;
        int currentBattery = getBatteryPercentage();
        String status = profileLoaded ? "online" : "idle";
        firebase.updateDeviceStatus(currentBattery, status);
    }

    // 5. LOGIKA KHUSUS PASIEN (Jika ada pasien terikat)
    if (profileLoaded)
    {
        // ── HOURLY Schedule Refresh (Diubah dari 15s menjadi 1 Jam = 3600000ms) ──
        if (now - lastSchedFetch >= 3600000UL || lastSchedFetch == 0)
        {
            lastSchedFetch = now;
            if (schedMgr)
                schedMgr->refresh();
        }

        // ── Schedule Evaluation (Tiap 30 Detik sekali) ──
        if (now - lastSchedCheck >= SCHEDULE_CHECK_INTERVAL_MS || lastSchedCheck == 0)
        {
            lastSchedCheck = now;
            handleScheduleCheck();
        }

        // ── State Machine Dispenser & RFID ──
        handleDispenserUpdate();
        handleRFIDTap();
    }

    // 6. WEB SERVER HANDLE
    if (WiFi.status() == WL_CONNECTED) {
        standbyServer.handleClient();
    }

    // 7. DEBUGGING SERIAL MONITOR & MANUAL TRIGGER
    if (Serial.available())
    {
        char c = Serial.read();
        if (c == 'r' || c == 'R')
        {
            manualTrigger = true;
        }
    }

    if (manualTrigger)
    {
        manualTrigger = false;
        Serial.println("\n[DEBUG] Manual / Virtual button triggered! Triggering voice query...");
        if (profileLoaded)
        {
            display.setExpression(FaceExpression::SURPRISED);
            audio.playTone(1000, 300);

            handleVoiceQuery();
            wakeWord.reset();  // ← Reset classifier setelah voice query
            display.setExpression(FaceExpression::NEUTRAL);
        }
        else
        {
            Serial.println("[DEBUG] Patient belum di-load, tidak bisa trigger AI.");
        }
    }

    // Cukup beri jeda minimal 2ms agar FreeRTOS dapat bernapas tanpa memutus sampel audio I2S
    delay(2);
}

// ─────────────────────────────────────────────────────────────────────────────
//  PATIENT ASSIGNMENT LOGIC
// ─────────────────────────────────────────────────────────────────────────────

void pollPatientAssignment()
{
    String newPatientId = firebase.checkAssignment();

    if (newPatientId == currentPatientId)
        return;

    if (newPatientId.isEmpty())
    {
        if (!currentPatientId.isEmpty())
        {
            Serial.println("[MAIN] Patient unassigned. Returning to standby.");
            onPatientUnassigned();
        }
    }
    else
    {
        Serial.printf("[MAIN] New patient assigned: %s\n", newPatientId.c_str());
        onPatientAssigned(newPatientId);
    }
    currentPatientId = newPatientId;
}

void onPatientAssigned(const String &patientId)
{
    profileLoaded = firebase.fetchUserProfile(patientId, userProfile);
    if (!profileLoaded)
    {
        Serial.println("[MAIN] Failed to load patient profile.");
        return;
    }

    if (schedMgr)
        delete schedMgr;
    schedMgr = new ScheduleManager(firebase);
    schedMgr->setPatientId(patientId);
    schedMgr->refresh();

    lastSchedCheck = 0;
    lastSchedFetch = millis();

    display.setExpression(FaceExpression::HAPPY);

    firebase.updateDeviceStatus(getBatteryPercentage(), "online");
    Serial.printf("[MAIN] Patient '%s' loaded. System active.\n", patientId.c_str());
}

void onPatientUnassigned()
{
    profileLoaded = false;
    currentPatientId = "";
    userProfile = UserProfile();

    if (schedMgr)
    {
        delete schedMgr;
        schedMgr = nullptr;
    }

    lastSchedCheck = 0;
    lastSchedFetch = 0;

    display.setExpression(FaceExpression::SAD);
    delay(3000);

    firebase.updateDeviceStatus(getBatteryPercentage(), "idle");
    Serial.println("[MAIN] Device is now idle. No patient assigned.");
}

// ─────────────────────────────────────────────────────────────────────────────
//  HANDLER: Schedule Check
// ─────────────────────────────────────────────────────────────────────────────
void handleScheduleCheck()
{
    if (!schedMgr || !profileLoaded)
        return;

    MedSchedule matched;
    ScheduleCondition cond = schedMgr->evaluate(matched);

    if (cond == ScheduleCondition::ON_SCHEDULE)
    {
        Serial.printf("[MAIN] Schedule triggered: %s\n", matched.timeHHMM.c_str());
        WAGateway::sendMedicineReminder(userProfile.greeting, userProfile.waNumber);
    }
    else if (cond == ScheduleCondition::MISSED)
    {
        Serial.printf("[MAIN] MISSED dose: %s\n", matched.id.c_str());
        if (dispenser.isMedicinePresent())
        {
            schedMgr->markBolos(matched.id);
            WAGateway::sendMissedDoseAlert(userProfile.greeting, userProfile.waNumber);
            display.setExpression(FaceExpression::SAD);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  HANDLER: RFID Tap
// ─────────────────────────────────────────────────────────────────────────────
void handleRFIDTap()
{
    RFIDStatus status = rfid.scanStatus();

    if (status == RFIDStatus::NO_CARD)
        return;

    if (status == RFIDStatus::DENIED)
    {
        Serial.println("[MAIN] Unauthorized RFID scan detected.");
        display.setExpression(FaceExpression::ANGRY);
        audio.playTone(300, 500);

        unsigned long startWait = millis();
        while (millis() - startWait < 2000)
        {
            display.update();
            delay(10);
        }

        display.setExpression(FaceExpression::NEUTRAL);
        return;
    }

    if (!schedMgr || !profileLoaded)
        return;

    Serial.println("[MAIN] Authorised RFID scan detected.");

    MedSchedule matched;
    ScheduleCondition cond = schedMgr->evaluate(matched);

    if (cond == ScheduleCondition::ON_SCHEDULE)
    {
        Serial.println("[MAIN] CONDITION A: Opening dispenser.");
        dispenser.openDispenser(matched.qty_servo1, matched.qty_servo2);
        display.setExpression(FaceExpression::HAPPY);
        firebase.updateDeviceStatus(getBatteryPercentage(), "dispensing");
        Serial.printf("[MAIN] Membuka: Obat A=%d pil, Obat B=%d pil\n",
                      matched.qty_servo1, matched.qty_servo2);
    }
    else
    {
        Serial.println("[MAIN] CONDITION B: Access denied – outside schedule.");
        dispenser.denyAccess();
        display.setExpression(FaceExpression::SAD);

        String ttsText = "Maaf " + userProfile.greeting + ", belum waktunya minum obat.";
        Serial.println("[MAIN] Memutar TTS: " + ttsText);
        audio.speakText(ttsText, "id");
    }

    delay(200);
}

// ─────────────────────────────────────────────────────────────────────────────
//  HANDLER: Dispenser Update
// ─────────────────────────────────────────────────────────────────────────────
void handleDispenserUpdate()
{
    int dispStatus = dispenser.update();

    if (dispStatus == 1 && schedMgr)
    {
        MedSchedule matched;
        ScheduleCondition cond = schedMgr->evaluate(matched);

        if (cond == ScheduleCondition::ON_SCHEDULE || cond == ScheduleCondition::MISSED)
        {
            Serial.printf("[MAIN] Marking %s as Tuntas.\n", matched.id.c_str());
            schedMgr->markTuntas(matched.id);

            userProfile.stock_servo1 = max(0, userProfile.stock_servo1 - matched.qty_servo1);
            userProfile.stock_servo2 = max(0, userProfile.stock_servo2 - matched.qty_servo2);

            firebase.updatePatientStock(currentPatientId, userProfile.stock_servo1, userProfile.stock_servo2);

            if (userProfile.stock_servo1 <= userProfile.stock_threshold && matched.qty_servo1 > 0)
            {
                WAGateway::sendLowStockAlert(userProfile.greeting, userProfile.waNumber, "Obat A", userProfile.stock_servo1);
            }
            if (userProfile.stock_servo2 <= userProfile.stock_threshold && matched.qty_servo2 > 0)
            {
                WAGateway::sendLowStockAlert(userProfile.greeting, userProfile.waNumber, "Obat B", userProfile.stock_servo2);
            }

            display.setExpression(FaceExpression::WINK);
            firebase.updateDeviceStatus(getBatteryPercentage(), "online");
            delay(3000);
            display.setExpression(FaceExpression::NEUTRAL);
        }
    }
    else if (dispStatus == 2 && schedMgr)
    {
        MedSchedule matched;
        ScheduleCondition cond = schedMgr->evaluate(matched);

        if (cond == ScheduleCondition::ON_SCHEDULE || cond == ScheduleCondition::MISSED)
        {
            Serial.printf("[MAIN] 3 menit berlalu. Marking %s as Bolos.\n", matched.id.c_str());
            schedMgr->markBolos(matched.id);
            WAGateway::sendMissedDoseAlert(userProfile.greeting, userProfile.waNumber);

            display.setExpression(FaceExpression::SAD);
            firebase.updateDeviceStatus(getBatteryPercentage(), "online");
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  HANDLER: Voice Query
// ─────────────────────────────────────────────────────────────────────────────
void handleVoiceQuery()
{
    if (!aiAssistant || !profileLoaded)
        return;
    Serial.println("[MAIN] Voice query initiated.");
    firebase.updateDeviceStatus(getBatteryPercentage(), "listening");
    aiAssistant->handleVoiceQuery(userProfile.patientId, userProfile.greeting, userProfile.disease);
    firebase.updateDeviceStatus(getBatteryPercentage(), "online");
}

// ─────────────────────────────────────────────────────────────────────────────
//  HANDLER: Wake Word (Edge Impulse KWS)
// ─────────────────────────────────────────────────────────────────────────────
void checkWakeWord()
{
    static unsigned long lastWakeTime = 0;
    const unsigned long cooldownTime = 3000; // Cooldown 3 detik

    if (wakeWord.check())
    {
        if (millis() - lastWakeTime < cooldownTime)
        {
            Serial.println("[KWS] Deteksi diabaikan (Cooldown aktif)");
            return;
        }
        lastWakeTime = millis();

        Serial.println("[MAIN] 🔊 Wake Word 'Hai TEBCO' Terdeteksi!");
        display.setExpression(FaceExpression::SURPRISED);
        audio.playTone(1000, 300); // Beep konfirmasi
        handleVoiceQuery();
        wakeWord.reset();  // ← KRITIS: Reset classifier setelah voice query
        display.setExpression(FaceExpression::NEUTRAL);
    }
}
