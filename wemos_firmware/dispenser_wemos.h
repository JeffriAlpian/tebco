/**
 * @file dispenser_wemos.h
 * @brief Kontrol Servo + Sensor IR untuk Wemos D1 Mini
 *
 * Menangani buka/tutup laci dispenser dan deteksi obat diambil.
 */

#pragma once
#include <Arduino.h>
#include <Servo.h>
#include "config_wemos.h"

class DispenserWemos {
public:
    DispenserWemos() : _isOpen(false), _openTime(0) {}

    void begin() {
        _servo.attach(PIN_SERVO_1);
        _servo.write(SERVO_LOCKED_DEG);

        pinMode(PIN_SENSOR_IR, INPUT_PULLUP);
    }

    /** Buka laci dispenser */
    void open() {
        _servo.write(SERVO_OPEN_DEG);
        _isOpen   = true;
        _openTime = millis();
    }

    /** Tutup laci dispenser */
    void close() {
        _servo.write(SERVO_LOCKED_DEG);
        _isOpen = false;
    }

    bool isOpen() const { return _isOpen; }

    /**
     * Cek apakah obat sudah diambil (sensor IR).
     * IR Active LOW: GPIO LOW = ada obat, GPIO HIGH = obat tidak ada (diambil)
     * @return true jika obat terdeteksi DIAMBIL
     */
    bool isMedicineTaken() {
        // Sensor LOW = ada obat di kompartemen
        // Sensor HIGH = kompartemen kosong (obat diambil)
        return digitalRead(PIN_SENSOR_IR) == HIGH;
    }

    /**
     * Cek apakah laci sudah terbuka lebih dari batas waktu (timeout).
     * Jika iya, tutup otomatis untuk keamanan.
     * @param timeoutMs Batas waktu dalam milidetik (default 30 detik)
     * @return true jika timeout terjadi dan servo ditutup
     */
    bool checkTimeout(unsigned long timeoutMs = 30000) {
        if (_isOpen && (millis() - _openTime > timeoutMs)) {
            close();
            return true;
        }
        return false;
    }

private:
    Servo        _servo;
    bool         _isOpen;
    unsigned long _openTime;
};
