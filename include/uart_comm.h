/**
 * @file uart_comm.h
 * @brief Protokol Komunikasi UART Dua Arah antara ESP32-S3 dan Wemos D1 Mini
 *
 * Format pesan: JSON satu baris diakhiri '\n'
 * Contoh: {"event":"RFID_ACCEPT","data":{"name":"Budi"}}\n
 *
 * Semua event yang mungkin:
 * ─── Dari Wemos D1 Mini ke ESP32-S3 ───────────────────────────
 *   RFID_ACCEPT     : Kartu valid + jadwal tepat waktu
 *   RFID_REJECT     : Kartu tidak dikenal
 *   RFID_WRONG_TIME : Kartu valid tapi di luar jadwal
 *   DOSE_TAKEN      : Obat terdeteksi diambil (sensor IR)
 *   DOSE_MISSED     : Jadwal minum obat terlewat
 *   WEMOS_READY     : Wemos baru selesai boot, minta data pasien
 *
 * ─── Dari ESP32-S3 ke Wemos D1 Mini ───────────────────────────
 *   OPEN_DISPENSER  : AI perintahkan buka laci dispenser
 *   CLOSE_DISPENSER : Tutup laci paksa
 *   PATIENT_UPDATE  : Data pasien baru dari Firebase
 *   SCHEDULE_UPDATE : Data jadwal baru dari Firebase
 */

#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// ── Forward Declarations ──────────────────────────────────────────────────────
class DisplayManager;
class AudioPipeline;

// ── Konstanta UART ────────────────────────────────────────────────────────────
#define UART_BAUD_RATE      115200
#define UART_TX_PIN         43      // GPIO43 = ESP32-S3 UART1 TX → Wemos RX
#define UART_RX_PIN         44      // GPIO44 = ESP32-S3 UART1 RX ← Wemos TX
#define UART_BUFFER_SIZE    512     // Buffer maks satu pesan JSON

// ── Tipe-tipe Event UART ──────────────────────────────────────────────────────
enum class UartEvent {
    UNKNOWN,
    // Dari Wemos
    RFID_ACCEPT,
    RFID_REJECT,
    RFID_WRONG_TIME,
    DOSE_TAKEN,
    DOSE_MISSED,
    WEMOS_READY,
    // Dari ESP32-S3
    OPEN_DISPENSER,
    CLOSE_DISPENSER,
    PATIENT_UPDATE,
    SCHEDULE_UPDATE,
    WIFI_CREDENTIALS,
};

class UartComm {
public:
    /**
     * @param display Referensi ke DisplayManager untuk update layar
     * @param audio   Referensi ke AudioPipeline untuk memutar suara
     */
    UartComm(DisplayManager &display, AudioPipeline &audio);

    /** Inisialisasi Serial1 pada pin yang didefinisikan di config.h */
    void begin();

    /**
     * Harus dipanggil di dalam loop() utama.
     * Membaca karakter UART satu per satu dan memproses JSON saat '\n' ditemukan.
     */
    void loop();

    // ── Fungsi Kirim Perintah ke Wemos ────────────────────────────────────────

    /** Perintahkan Wemos membuka laci dispenser */
    void sendOpenDispenser(int slot = 1);

    /** Perintahkan Wemos menutup laci dispenser */
    void sendCloseDispenser(int slot = 1);

    /**
     * Kirim data profil pasien aktif ke Wemos.
     * Dipanggil saat Firebase mendeteksi assignment pasien baru.
     */
    void sendPatientUpdate(const String &patientId, const String &name,
                           const String &disease, const String &waNumber);

    /**
     * Kirim data jadwal obat ke Wemos (array JSON).
     * Dipanggil setiap kali jadwal diperbarui dari Firebase.
     * @param schedulesJson String JSON array, contoh: [{"time":"08:00","qty":2},...]
     */
    void sendScheduleUpdate(const String &schedulesJson);

    /**
     * Kirim kredensial WiFi ke Wemos agar Wemos tidak perlu di-hardcode.
     */
    void sendWifiCredentials(const String &ssid, const String &password);

private:
    DisplayManager &_display;
    AudioPipeline  &_audio;

    char   _buf[UART_BUFFER_SIZE];
    size_t _bufLen = 0;
    unsigned long _revertFaceTime = 0;

    /** Proses satu baris JSON yang sudah lengkap */
    void _processLine(const char *line);

    /** Router event: panggil handler yang sesuai berdasarkan event string */
    void _dispatch(UartEvent event, JsonObjectConst data);

    // ── Handler per Event ─────────────────────────────────────────────────────
    void _onRfidAccept(JsonObjectConst data);
    void _onRfidReject(JsonObjectConst data);
    void _onRfidWrongTime(JsonObjectConst data);
    void _onDoseTaken(JsonObjectConst data);
    void _onDoseMissed(JsonObjectConst data);
    void _onWemosReady();

    /** Helper: kirim JSON ke Wemos via Serial1 */
    void _send(const char *eventName, const String &dataJson = "{}");

    /** Helper: konversi string event ke enum */
    static UartEvent _parseEvent(const char *name);
};
