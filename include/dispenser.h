/**
 * @file dispenser.h
 * @brief Controls 2x SG90 servo (dual compartment) and Ultrasonic sensor.
 *
 *  Kompartemen A (Servo 1, PIN_SERVO)   → Obat jenis pertama
 *  Kompartemen B (Servo 2, PIN_SERVO_2) → Obat jenis kedua
 *
 *  Logika dispenser:
 *  - openDispenser(qty1, qty2) membuka servo sesuai qty > 0
 *  - Servo menutup setelah obat diambil (Ultrasonic sensor) atau timeout
 *  - Kedua servo bisa membuka bersamaan jika qty1 > 0 DAN qty2 > 0
 */

#pragma once
#include <Arduino.h>
#include <ESP32Servo.h>

enum class DispenserState {
    IDLE,
    OPEN,       // Salah satu atau kedua servo terbuka
    CONFIRMING, // Obat diambil, servo menutup
    LOCKED      // Access denied (outside schedule)
};

class Dispenser {
public:
    Dispenser();

    /** @brief Attach kedua servo ke pin. Panggil di setup(). */
    void begin();

    /** @return true jika ada obat di tray (Ultrasonic sensor membaca jarak < 6 cm). */
    bool isMedicinePresent();

    /**
     * @brief Condition A: Buka satu atau kedua kompartemen sesuai qty.
     *        Servo hanya terbuka jika qty-nya > 0.
     * @param qty1  Jumlah pil dari kompartemen A (Servo 1). 0 = tidak dibuka.
     * @param qty2  Jumlah pil dari kompartemen B (Servo 2). 0 = tidak dibuka.
     */
    void openDispenser(int qty1, int qty2);

    /** @brief Tutup kedua servo. */
    void closeDispenser();

    /** @brief Condition B: Tolak akses, servo tetap terkunci. */
    void denyAccess();

    /** @return Berapa pil kompartemen A yang perlu diambil pada sesi ini. */
    int getPendingQty1() const { return _pendingQty1; }

    /** @return Berapa pil kompartemen B yang perlu diambil pada sesi ini. */
    int getPendingQty2() const { return _pendingQty2; }

    DispenserState getState() const { return _state; }

    /**
     * @brief Panggil di loop(). Menangani auto-close dan deteksi pengambilan obat.
     * @return 0 = waiting/idle, 1 = obat diambil (tuntas), 2 = timeout 3 menit (bolos).
     */
    int update();

private:
    Servo          _servo1;
    Servo          _servo2;
    DispenserState _state;
    unsigned long  _openedAt;
    int            _pendingQty1;
    int            _pendingQty2;
};
