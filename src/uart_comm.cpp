/**
 * @file uart_comm.cpp
 * @brief Implementasi komunikasi UART ESP32-S3 ↔ Wemos D1 Mini
 */

#include "../include/uart_comm.h"
#include "../include/display_manager.h"
#include "../include/audio_pipeline.h"

// ── Konstruktor & Inisialisasi ────────────────────────────────────────────────

UartComm::UartComm(DisplayManager &display, AudioPipeline &audio)
    : _display(display), _audio(audio), _bufLen(0) {}

void UartComm::begin() {
    Serial1.begin(UART_BAUD_RATE, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);
    Serial.println("[UART] Serial1 siap. TX=" + String(UART_TX_PIN) +
                   " RX=" + String(UART_RX_PIN));
}

// ── Loop Utama: Baca & Parse UART ────────────────────────────────────────────

void UartComm::loop() {
    if (_revertFaceTime > 0 && millis() > _revertFaceTime) {
        _display.setExpression(FaceExpression::NEUTRAL);
        _revertFaceTime = 0;
    }

    while (Serial1.available()) {
        char c = (char)Serial1.read();

        if (c == '\n') {
            // Baris lengkap diterima → proses
            _buf[_bufLen] = '\0';
            if (_bufLen > 2) _processLine(_buf);
            _bufLen = 0; // Reset buffer
        } else if (c != '\r') {
            // Tambah karakter ke buffer (abaikan \r)
            if (_bufLen < UART_BUFFER_SIZE - 1) {
                _buf[_bufLen++] = c;
            } else {
                // Buffer overflow: reset dan log error
                Serial.println("[UART] WARNING: Buffer overflow, pesan terlalu panjang!");
                _bufLen = 0;
            }
        }
    }
}

// ── Proses Satu Baris JSON ────────────────────────────────────────────────────

void UartComm::_processLine(const char *line) {
    Serial.printf("[UART] Terima: %s\n", line);

    StaticJsonDocument<256> doc;
    DeserializationError err = deserializeJson(doc, line);

    if (err) {
        Serial.printf("[UART] ERROR Parse JSON: %s\n", err.c_str());
        return;
    }

    const char *eventStr = doc["event"] | "";
    JsonObjectConst data = doc["data"].as<JsonObjectConst>();

    UartEvent event = _parseEvent(eventStr);
    if (event == UartEvent::UNKNOWN) {
        Serial.printf("[UART] Event tidak dikenal: %s\n", eventStr);
        return;
    }

    _dispatch(event, data);
}

// ── Router Event ──────────────────────────────────────────────────────────────

void UartComm::_dispatch(UartEvent event, JsonObjectConst data) {
    switch (event) {
        case UartEvent::RFID_ACCEPT:     _onRfidAccept(data);    break;
        case UartEvent::RFID_REJECT:     _onRfidReject(data);    break;
        case UartEvent::RFID_WRONG_TIME: _onRfidWrongTime(data); break;
        case UartEvent::DOSE_TAKEN:      _onDoseTaken(data);     break;
        case UartEvent::DOSE_MISSED:     _onDoseMissed(data);    break;
        case UartEvent::WEMOS_READY:     _onWemosReady();        break;
        default: break;
    }
}

// ── Handler Event dari Wemos ──────────────────────────────────────────────────

void UartComm::_onRfidAccept(JsonObjectConst data) {
    String name = data["name"] | "Pasien";
    Serial.printf("[UART] RFID diterima untuk: %s\n", name.c_str());

    // Reaksi display: mata senang + sambut pasien
    _display.setExpression(FaceExpression::HAPPY);
    _display.showAIResponse("Selamat! Silakan ambil obat Anda, " + name + ".");

    // Reaksi audio: ucapkan sambutan
    _audio.speakText("Selamat pagi " + name + "! Laci obat sudah terbuka.", "id");
}

void UartComm::_onRfidReject(JsonObjectConst data) {
    String uid = data["uid"] | "unknown";
    Serial.printf("[UART] RFID ditolak. UID: %s\n", uid.c_str());

    // Reaksi display: mata marah + pesan penolakan
    _display.setExpression(FaceExpression::ANGRY);
    _display.showAIResponse("Akses Ditolak! Gelang tidak dikenal.");

    // Reaksi audio: peringatan tegas
    _audio.speakText("Maaf, gelang Anda tidak dikenali. Hubungi perawat.", "id");

    _revertFaceTime = millis() + 4000;
}

void UartComm::_onRfidWrongTime(JsonObjectConst data) {
    String nextDose = data["next_dose"] | "jadwal berikutnya";
    Serial.printf("[UART] RFID di luar jadwal. Dosis berikutnya: %s\n", nextDose.c_str());

    // Reaksi display: mata bingung/terkejut + info jadwal
    _display.setExpression(FaceExpression::SURPRISED);
    _display.showAIResponse("Belum waktunya! Jadwal minum obat berikutnya: " + nextDose);

    // Reaksi audio: informasi lembut
    _audio.speakText("Belum waktunya minum obat. Jadwal selanjutnya pukul " + nextDose, "id");

    _revertFaceTime = millis() + 4000;
}

void UartComm::_onDoseTaken(JsonObjectConst data) {
    int slot = data["slot"] | 1;
    Serial.printf("[UART] Obat diambil dari slot %d\n", slot);

    // Reaksi display: mata sangat senang (wink/kedip ganda)
    _display.setExpression(FaceExpression::WINK);
    _display.showAIResponse("Hebat! Obat berhasil diambil. Tetap jaga kesehatan!");

    // Reaksi audio: apresiasi positif
    _audio.speakText("Terima kasih! Jangan lupa minum obatnya ya. Semoga lekas sembuh!", "id");

    _revertFaceTime = millis() + 5000;
}

void UartComm::_onDoseMissed(JsonObjectConst data) {
    String scheduledTime = data["scheduled_time"] | "jadwal";
    Serial.printf("[UART] Dosis terlewat! Jadwal: %s\n", scheduledTime.c_str());

    // Reaksi display: mata sedih + peringatan
    _display.setExpression(FaceExpression::SAD);
    _display.showAIResponse("Obat pukul " + scheduledTime + " belum diminum. Segera hubungi perawat!");

    // Reaksi audio: pengingat mendesak tapi tetap empati
    _audio.speakText("Perhatian! Obat jadwal " + scheduledTime + " belum diminum. Tolong segera minum obatnya.", "id");

    _revertFaceTime = millis() + 5000;
}

void UartComm::_onWemosReady() {
    Serial.println("[UART] Wemos siap. Mengirim data pasien aktif...");
    // Catatan: Data pasien aktif akan dikirim oleh TEBCO2.ino
    // yang memanggil sendPatientUpdate() setelah menerima event ini.
    // Di sini kita hanya log dan tampilkan status.
    _display.showAIResponse("Sistem dispenser terhubung.");
}

// ── Fungsi Kirim Perintah ke Wemos ────────────────────────────────────────────

void UartComm::sendOpenDispenser(int slot) {
    String data = "{\"slot\":" + String(slot) + "}";
    _send("OPEN_DISPENSER", data);
    Serial.printf("[UART] Kirim: OPEN_DISPENSER slot=%d\n", slot);
}

void UartComm::sendCloseDispenser(int slot) {
    String data = "{\"slot\":" + String(slot) + "}";
    _send("CLOSE_DISPENSER", data);
    Serial.printf("[UART] Kirim: CLOSE_DISPENSER slot=%d\n", slot);
}

void UartComm::sendPatientUpdate(const String &patientId, const String &name,
                                  const String &disease, const String &waNumber) {
    String data = "{\"patient_id\":\"" + patientId + "\","
                  "\"name\":\"" + name + "\","
                  "\"disease\":\"" + disease + "\","
                  "\"wa_number\":\"" + waNumber + "\"}";
    _send("PATIENT_UPDATE", data);
    Serial.println("[UART] Kirim: PATIENT_UPDATE → " + patientId);
}

void UartComm::sendScheduleUpdate(const String &schedulesJson) {
    // schedulesJson sudah berupa JSON array, langsung masukkan ke data
    String data = "{\"schedules\":" + schedulesJson + "}";
    _send("SCHEDULE_UPDATE", data);
    Serial.println("[UART] Kirim: SCHEDULE_UPDATE");
}

void UartComm::sendWifiCredentials(const String &ssid, const String &password) {
    String data = "{\"ssid\":\"" + ssid + "\",\"pass\":\"" + password + "\"}";
    _send("WIFI_CREDENTIALS", data);
    Serial.println("[UART] Kirim: WIFI_CREDENTIALS (" + ssid + ")");
}

// ── Helper Privat ─────────────────────────────────────────────────────────────

void UartComm::_send(const char *eventName, const String &dataJson) {
    // Format: {"event":"NAMA","data":{...}}\n
    String msg = "{\"event\":\"";
    msg += eventName;
    msg += "\",\"data\":";
    msg += dataJson;
    msg += "}\n";
    Serial1.print(msg);
}

UartEvent UartComm::_parseEvent(const char *name) {
    if (strcmp(name, "RFID_ACCEPT")     == 0) return UartEvent::RFID_ACCEPT;
    if (strcmp(name, "RFID_REJECT")     == 0) return UartEvent::RFID_REJECT;
    if (strcmp(name, "RFID_WRONG_TIME") == 0) return UartEvent::RFID_WRONG_TIME;
    if (strcmp(name, "DOSE_TAKEN")      == 0) return UartEvent::DOSE_TAKEN;
    if (strcmp(name, "DOSE_MISSED")     == 0) return UartEvent::DOSE_MISSED;
    if (strcmp(name, "WEMOS_READY")     == 0) return UartEvent::WEMOS_READY;
    if (strcmp(name, "OPEN_DISPENSER")  == 0) return UartEvent::OPEN_DISPENSER;
    if (strcmp(name, "CLOSE_DISPENSER") == 0) return UartEvent::CLOSE_DISPENSER;
    if (strcmp(name, "PATIENT_UPDATE")  == 0) return UartEvent::PATIENT_UPDATE;
    if (strcmp(name, "SCHEDULE_UPDATE") == 0) return UartEvent::SCHEDULE_UPDATE;
    if (strcmp(name, "WIFI_CREDENTIALS")== 0) return UartEvent::WIFI_CREDENTIALS;
    return UartEvent::UNKNOWN;
}
