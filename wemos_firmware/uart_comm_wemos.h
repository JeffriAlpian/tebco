/**
 * @file uart_comm_wemos.h
 * @brief Handler UART untuk sisi Wemos D1 Mini
 *
 * Menerima perintah dari ESP32-S3 dan mengirimkan event fisik balik.
 * Mirror dari uart_comm.h di sisi ESP32-S3.
 */

#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "config_wemos.h"

// ── Tipe Event (sama persis dengan sisi ESP32-S3) ────────────────────────────
enum class UartEvent {
    UNKNOWN,
    RFID_ACCEPT,
    RFID_REJECT,
    RFID_WRONG_TIME,
    DOSE_TAKEN,
    DOSE_MISSED,
    WEMOS_READY,
    OPEN_DISPENSER,
    CLOSE_DISPENSER,
    PATIENT_UPDATE,
    SCHEDULE_UPDATE,
    WIFI_CREDENTIALS,
};

// ── Callback Handler ──────────────────────────────────────────────────────────
// Struct berisi pointer ke fungsi yang akan dipanggil saat event tiba
struct UartHandlers {
    void (*onOpenDispenser)(int slot)   = nullptr;
    void (*onCloseDispenser)(int slot)  = nullptr;
    void (*onPatientUpdate)(String patientId, String name,
                            String disease, String waNumber) = nullptr;
    void (*onScheduleUpdate)(String schedulesJson) = nullptr;
    void (*onWifiCredentials)(String ssid, String pass) = nullptr;
};

class UartCommWemos {
public:
    UartCommWemos() : _bufLen(0) {}

    void begin() {
        Serial.begin(UART_BAUD_RATE);
        // Catatan: Serial pada Wemos = UART0 yang terhubung ke ESP32-S3
    }

    /** Daftarkan callback handlers dari sketch utama */
    void setHandlers(const UartHandlers &handlers) {
        _handlers = handlers;
    }

    /** Harus dipanggil di loop() utama wemos_firmware.ino */
    void loop() {
        while (Serial.available()) {
            char c = (char)Serial.read();
            if (c == '\n') {
                _buf[_bufLen] = '\0';
                if (_bufLen > 2) _processLine(_buf);
                _bufLen = 0;
            } else if (c != '\r') {
                if (_bufLen < 511) _buf[_bufLen++] = c;
                else _bufLen = 0; // overflow guard
            }
        }
    }

    // ── Fungsi Kirim Event ke ESP32-S3 ───────────────────────────────────────

    void sendWemosReady() {
        _send("WEMOS_READY", "{}");
    }

    void sendRfidAccept(const String &name, const String &patientId) {
        String data = "{\"name\":\"" + name + "\",\"patient_id\":\"" + patientId + "\"}";
        _send("RFID_ACCEPT", data);
    }

    void sendRfidReject(const String &uid) {
        String data = "{\"uid\":\"" + uid + "\"}";
        _send("RFID_REJECT", data);
    }

    void sendRfidWrongTime(const String &nextDose) {
        String data = "{\"next_dose\":\"" + nextDose + "\"}";
        _send("RFID_WRONG_TIME", data);
    }

    void sendDoseTaken(int slot, const String &time) {
        String data = "{\"slot\":" + String(slot) + ",\"time\":\"" + time + "\"}";
        _send("DOSE_TAKEN", data);
    }

    void sendDoseMissed(const String &scheduledTime) {
        String data = "{\"scheduled_time\":\"" + scheduledTime + "\"}";
        _send("DOSE_MISSED", data);
    }

private:
    char         _buf[512];
    size_t       _bufLen;
    UartHandlers _handlers;

    void _processLine(const char *line) {
        StaticJsonDocument<256> doc;
        if (deserializeJson(doc, line)) return;

        const char    *eventStr = doc["event"] | "";
        JsonObjectConst data    = doc["data"].as<JsonObjectConst>();

        UartEvent event = _parseEvent(eventStr);

        switch (event) {
            case UartEvent::OPEN_DISPENSER:
                if (_handlers.onOpenDispenser)
                    _handlers.onOpenDispenser(data["slot"] | 1);
                break;

            case UartEvent::CLOSE_DISPENSER:
                if (_handlers.onCloseDispenser)
                    _handlers.onCloseDispenser(data["slot"] | 1);
                break;

            case UartEvent::PATIENT_UPDATE:
                if (_handlers.onPatientUpdate)
                    _handlers.onPatientUpdate(
                        data["patient_id"] | "",
                        data["name"]       | "",
                        data["disease"]    | "",
                        data["wa_number"]  | ""
                    );
                break;

            case UartEvent::SCHEDULE_UPDATE: {
                // Serialisasi kembali array jadwal ke string JSON
                String sched;
                serializeJson(data["schedules"], sched);
                if (_handlers.onScheduleUpdate) _handlers.onScheduleUpdate(sched);
                break;
            }

            case UartEvent::WIFI_CREDENTIALS:
                if (_handlers.onWifiCredentials)
                    _handlers.onWifiCredentials(data["ssid"] | "", data["pass"] | "");
                break;

            default: break;
        }
    }

    void _send(const char *eventName, const String &dataJson) {
        String msg = "{\"event\":\"";
        msg += eventName;
        msg += "\",\"data\":";
        msg += dataJson;
        msg += "}\n";
        Serial.print(msg);
    }

    static UartEvent _parseEvent(const char *name) {
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
};
