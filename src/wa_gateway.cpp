/**
 * @file wa_gateway.cpp
 * @brief HTTP POST to api.sidobe.com WhatsApp gateway.
 *        PHP equivalent converted to ESP32 HTTPClient + ArduinoJson.
 */

#include "../include/wa_gateway.h"
#include "../include/config.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

int WAGateway::sendMessage(const String &phone, const String &message) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[WA] No Wi-Fi, cannot send message.");
        return -1;
    }

    // Format phone number to E.164
    String cleanPhone = "";
    for (size_t i = 0; i < phone.length(); i++) {
        if (isDigit(phone[i])) {
            cleanPhone += phone[i];
        }
    }
    
    if (cleanPhone.startsWith("0")) {
        cleanPhone = "+62" + cleanPhone.substring(1);
    } else if (cleanPhone.startsWith("62")) {
        cleanPhone = "+" + cleanPhone;
    } else if (cleanPhone.length() > 0) {
        cleanPhone = "+" + cleanPhone; 
    }

    if (cleanPhone.isEmpty()) {
        Serial.println("[WA] Phone number is empty or invalid.");
        return -1;
    }

    // Build JSON payload
    StaticJsonDocument<512> doc;
    doc["phone"]   = cleanPhone;
    doc["message"] = message;
    String payload;
    serializeJson(doc, payload);

    HTTPClient http;
    http.begin(WA_API_URL);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("X-Secret-Key",  WA_SECRET_KEY);
    http.setTimeout(10000);

    int code = http.POST(payload);

    if (code > 0) {
        Serial.printf("[WA] Sent to %s | HTTP %d\n", phone.c_str(), code);
        if (code != 200) {
            Serial.printf("[WA] Response: %s\n", http.getString().c_str());
        }
    } else {
        Serial.printf("[WA] Error: %s\n", http.errorToString(code).c_str());
    }
    http.end();
    return code;
}

void WAGateway::sendMedicineReminder(const String &greeting, const String &phone) {
    String msg = "Halo " + greeting + ", saatnya minum obat Anda. "
                 "Segera ambil obat dari dispenser TEBCO Anda. 💊";
    sendMessage(phone, msg);
}

void WAGateway::sendMissedDoseAlert(const String &greeting, const String &phone) {
    String msg = "⚠️ PERHATIAN: " + greeting + " melewatkan jadwal minum obat. "
                 "Harap segera hubungi yang bersangkutan.";
    sendMessage(phone, msg);
}

void WAGateway::sendLowStockAlert(const String &greeting, const String &phone, const String &medicineName, int remaining) {
    String msg = "⚠️ INFO STOK: Halo " + greeting + ", stok " + medicineName + 
                 " Anda hampir habis (tersisa " + String(remaining) + " pil). "
                 "Harap segera isi ulang alat TEBCO Anda.";
    sendMessage(phone, msg);
}
