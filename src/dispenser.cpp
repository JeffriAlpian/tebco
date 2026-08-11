/**
 * @file dispenser.cpp
 * @brief Dual servo lid control dan sensor ultrasonik (Pin 21 & 35).
 *        Servo 1 = Kompartemen A (PIN_SERVO)
 *        Servo 2 = Kompartemen B (PIN_SERVO_2)
 */

#include "../include/dispenser.h"
#include "../include/config.h"

Dispenser::Dispenser()
    : _state(DispenserState::IDLE), _openedAt(0),
      _pendingQty1(0), _pendingQty2(0) {}

void Dispenser::begin() {
    // ESP32Servo pakai LEDC di baliknya
    _servo1.setPeriodHertz(50);
    _servo1.attach(PIN_SERVO,   500, 2400);

    _servo2.setPeriodHertz(50);
    _servo2.attach(PIN_SERVO_2, 500, 2400);

    // Konfigurasi pin Ultrasonik sesuai permintaan (Trig: 21, Echo: 35)
    pinMode(PIN_US_TRIG, OUTPUT);
    pinMode(PIN_US_ECHO, INPUT);

    // Set servo ke posisi terkunci saat alat baru menyala
    _servo1.write(SERVO1_LOCKED_DEG);
    _servo2.write(SERVO2_LOCKED_DEG);
    delay(300);
    
    _state       = DispenserState::IDLE;
    _pendingQty1 = 0;
    _pendingQty2 = 0;
    Serial.println("[Dispenser] Ready. Dual servo locked at 0°.");
}

bool Dispenser::isMedicinePresent() {
    long duration;
    int distance;

    // Bersihkan trigPin (Pin 21)
    digitalWrite(PIN_US_TRIG, LOW);
    delayMicroseconds(2);

    // Set trigPin ke HIGH selama 10 mikrodetik
    digitalWrite(PIN_US_TRIG, HIGH);
    delayMicroseconds(10);
    digitalWrite(PIN_US_TRIG, LOW);

    // Membaca echoPin (Pin 35), dengan timeout 5000us agar tidak blocking
    duration = pulseIn(PIN_US_ECHO, HIGH, 5000);

    // Jika durasi 0 (timeout / tidak ada pantulan), asumsikan jarak sangat jauh
    if (duration == 0) {
        distance = 999; 
    } else {
        // Menghitung jarak (Kecepatan suara = 0.034 cm/mikrodetik)
        distance = duration * 0.034 / 2;
    }

    // Tampilkan data pembacaan ke Serial Monitor secara real-time
    Serial.print("[Ultrasonic] Jarak terukur: ");
    Serial.print(distance);
    Serial.print(" cm -> Status: ");

    // Logika pengecekan obat: jika jarak > 5 cm maka "ada", jika <= 3 cm maka "tidak"
    if (distance > 5) {
        Serial.println("ada");
        return true;  // Obat masih ada di tempat
    } else {
        Serial.println("tidak");
        return false; // Obat sudah diambil / kosong
    }
}

void Dispenser::openDispenser(int qty1, int qty2) {
    _pendingQty1 = qty1;
    _pendingQty2 = qty2;

    if (qty1 == 0 && qty2 == 0) {
        Serial.println("[Dispenser] Tidak ada pil yang perlu dikeluarkan.");
        return;
    }

    // Servo 1 duluan: keluarkan pil satu per satu
    if (qty1 > 0) {
        Serial.printf("[Dispenser] Servo 1 (Obat A) mengeluarkan %d pil...\n", qty1);
        for (int i = 0; i < qty1; i++) {
            _servo1.write(SERVO1_OPEN_DEG);   
            delay(SERVO_OPEN_MS);              
            _servo1.write(SERVO1_LOCKED_DEG);  
            delay(200);                        
            Serial.printf("[Dispenser]   Pil A ke-%d keluar ✓\n", i + 1);
        }
    }

    // Lalu Servo 2: keluarkan pil satu per satu
    if (qty2 > 0) {
        Serial.printf("[Dispenser] Servo 2 (Obat B) mengeluarkan %d pil...\n", qty2);
        for (int i = 0; i < qty2; i++) {
            _servo2.write(SERVO2_OPEN_DEG);   
            delay(SERVO_OPEN_MS);              
            _servo2.write(SERVO2_LOCKED_DEG);  
            delay(200);                        
            Serial.printf("[Dispenser]   Pil B ke-%d keluar ✓\n", i + 1);
        }
    }

    Serial.println("[Dispenser] Semua pil sudah dikeluarkan.");
    _state    = DispenserState::OPEN;
    _openedAt = millis();
}

void Dispenser::closeDispenser() {
    if (_state == DispenserState::OPEN || _state == DispenserState::IDLE) {
        if (_pendingQty1 > 0 || (_pendingQty1 == 0 && _pendingQty2 == 0)) {
            _servo1.write(SERVO1_LOCKED_DEG);
        }
        if (_pendingQty2 > 0 || (_pendingQty1 == 0 && _pendingQty2 == 0)) {
            _servo2.write(SERVO2_LOCKED_DEG);
        }
    } else {
        _servo1.write(SERVO1_LOCKED_DEG);
        _servo2.write(SERVO2_LOCKED_DEG);
    }

    _state       = DispenserState::IDLE;
    _pendingQty1 = 0;
    _pendingQty2 = 0;
    Serial.println("[Dispenser] Kedua servo ditutup perlahan.");
}

void Dispenser::denyAccess() {
    _servo1.write(SERVO1_LOCKED_DEG);
    _servo2.write(SERVO2_LOCKED_DEG);
    _state = DispenserState::LOCKED;
    Serial.println("[Dispenser] Akses ditolak – di luar jadwal.");
}

int Dispenser::update() {
    if (_state != DispenserState::OPEN) return 0;

    // 1. Cooldown setelah servo terbuka (Tunggu 5 detik agar obat jatuh & sensor stabil)
    if (millis() - _openedAt < 5000) {
        return 0; // Belum waktunya membaca sensor
    }

    // Memanggil isMedicinePresent() yang kini sudah mencetak status ke Serial Monitor
    bool isPresent = isMedicinePresent(); 
    
    static unsigned long notPresentStartTime = 0;
    static bool isCurrentlyNotPresent = false;

    // Jika sensor mendeteksi kosong ("tidak")
    if (!isPresent) {
        if (!isCurrentlyNotPresent) {
            notPresentStartTime = millis(); 
            isCurrentlyNotPresent = true;
        }
        
        // Tampilkan sisa waktu tunggu konfirmasi di Serial Monitor (opsional, tiap detik)
        unsigned long elapsed = millis() - notPresentStartTime;
        if (elapsed % 1000 < 50) { // Print kira-kira tiap detik
            Serial.printf("[Dispenser] Menunggu konfirmasi stabil... %lu/10 detik\n", elapsed / 1000);
        }
    } else {
        // Jika obat terdeteksi kembali ("ada"), reset hitungan agar tidak salah membaca
        if (isCurrentlyNotPresent) {
            Serial.println("[Dispenser] Deteksi tidak stabil (angka naik turun), hitungan 10s direset.");
        }
        isCurrentlyNotPresent = false; 
    }

    // 2. Obat dianggap benar-benar diambil jika konsisten "tidak" (kosong) selama 10 detik (10000 ms)
    bool medicineTaken = (isCurrentlyNotPresent && (millis() - notPresentStartTime >= 10000));

    // Konfirmasi bolos jika waktu sudah lewat 3 menit (180.000 ms) sejak dispenser terbuka
    bool timedOut = (millis() - _openedAt) >= 180000;

    if (medicineTaken) {
        Serial.println("[Dispenser] Obat berhasil diambil ✅ (Terkonfirmasi setelah 10 detik)");
        closeDispenser();
        isCurrentlyNotPresent = false;
        return 1; // 1 = Status Tuntas
    }

    if (timedOut) {
        Serial.println("[Dispenser] 3 Menit berlalu tanpa pengambilan - Status: Bolos ❌");
        closeDispenser();
        isCurrentlyNotPresent = false;
        return 2; // 2 = Status Bolos
    }

    return 0; // 0 = Masih menunggu
}