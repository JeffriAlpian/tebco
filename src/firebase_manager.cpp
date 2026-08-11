/**
 * @file firebase_manager.cpp
 * @brief Firebase REST API client.
 *        Hospital-scale multi-device architecture:
 *        Device ID is derived from MAC address.
 *        Patient assignment is read from /devices/{deviceId}/assigned_patient.
 *        All patient data paths are built dynamically from the patient ID.
 */

#include "../include/firebase_manager.h"
#include "../include/config.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <vector>

FirebaseManager::FirebaseManager() {}

bool FirebaseManager::begin() {
    Serial.printf("[Firebase] Device ID: %s\n", getDeviceId().c_str());
    Serial.println("[Firebase] Initialized (REST mode, multi-device).");
    return true;
}

// ── Static: Device ID from MAC ────────────────────────────────────────────────

String FirebaseManager::getDeviceId() {
    // Use last 3 bytes of MAC address for unique but short ID
    // e.g. MAC "AA:BB:CC:DD:EE:FF" → "TEBCO-DDEEFF"
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char id[16];
    snprintf(id, sizeof(id), "TEBCO-%02X%02X%02X", mac[3], mac[4], mac[5]);
    return String(id);
}

// ── URL Builder ───────────────────────────────────────────────────────────────

String FirebaseManager::buildAuthURL(const String &path) {
    return String("https://") + FIREBASE_HOST + path + ".json?auth=" + FIREBASE_AUTH;
}

// ── HTTP Helpers ──────────────────────────────────────────────────────────────

String FirebaseManager::httpGET(const String &url) {
    HTTPClient http;
    http.begin(url);
    http.setTimeout(3000); // Harus di bawah 5000ms agar ESP32 Task Watchdog tidak reset (Panic)
    int code = http.GET();
    String body = "";
    if (code == HTTP_CODE_OK) {
        body = http.getString();
    } else {
        Serial.printf("[Firebase] GET failed, HTTP %d\n", code);
    }
    http.end();
    return body;
}

bool FirebaseManager::httpPATCH(const String &url, const String &payload) {
    HTTPClient http;
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(3000); // Harus di bawah 5000ms
    int code = http.sendRequest("PATCH", payload);
    bool ok = (code == HTTP_CODE_OK);
    if (!ok) Serial.printf("[Firebase] PATCH failed, HTTP %d\n", code);
    http.end();
    return ok;
}

bool FirebaseManager::httpPUT(const String &url, const String &payload) {
    HTTPClient http;
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(3000); // Harus di bawah 5000ms
    int code = http.PUT(payload);
    bool ok = (code == HTTP_CODE_OK);
    if (!ok) Serial.printf("[Firebase] PUT failed, HTTP %d\n", code);
    http.end();
    return ok;
}

// ── Greeting Logic ────────────────────────────────────────────────────────────

void FirebaseManager::deriveGreeting(UserProfile &profile) {
    if (profile.age < 20) {
        profile.greeting = "Sobat";
    } else if (profile.gender == "male") {
        profile.greeting = "Pak " + profile.name;
    } else {
        profile.greeting = "Bu " + profile.name;
    }
}

// ── Device Management ─────────────────────────────────────────────────────────

bool FirebaseManager::registerDevice(const String &alias, int batteryPct, const String &status) {
    String deviceId = getDeviceId();
    String path     = "/devices/" + deviceId;
    String url      = buildAuthURL(path);

    // Get current time string
    struct tm t;
    char timeBuf[25] = "unknown";
    if (getLocalTime(&t)) {
        strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%dT%H:%M:%S", &t);
    }

    // PATCH (not PUT) so we don't overwrite assigned_patient set by Hospital UI
    String payload = "{";
    payload += "\"alias\":\"" + alias + "\",";
    payload += "\"status\":\"" + status + "\",";
    payload += "\"battery\":" + String(batteryPct) + ",";
    payload += "\"firmware\":\"" FIRMWARE_VERSION "\",";
    payload += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
    payload += "\"last_seen\":\"" + String(timeBuf) + "\"";
    payload += "}";

    bool ok = httpPATCH(url, payload);
    Serial.printf("[Firebase] Device registered: %s (%s) → %s\n",
                  deviceId.c_str(), alias.c_str(), ok ? "OK" : "FAIL");
    return ok;
}

bool FirebaseManager::updateDeviceStatus(int batteryPct, const String &status) {
    String deviceId = getDeviceId();
    String path     = "/devices/" + deviceId;
    String url      = buildAuthURL(path);

    struct tm t;
    char timeBuf[25] = "unknown";
    if (getLocalTime(&t)) {
        strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%dT%H:%M:%S", &t);
    }

    String payload = "{";
    payload += "\"status\":\"" + status + "\",";
    payload += "\"battery\":" + String(batteryPct) + ",";
    payload += "\"last_seen\":\"" + String(timeBuf) + "\"";
    payload += "}";

    return httpPATCH(url, payload);
}

String FirebaseManager::checkAssignment() {
    String deviceId = getDeviceId();
    String url = buildAuthURL("/devices/" + deviceId + "/assigned_patient");
    String body = httpGET(url);

    // Firebase returns "null" (string) if field is null
    if (body.isEmpty() || body == "null") return "";

    // Remove surrounding quotes from JSON string value
    body.trim();
    body.replace("\"", "");
    Serial.printf("[Firebase] Assignment check: %s\n", body.c_str());
    return body; // e.g. "P-003"
}

// ── Patient Data ──────────────────────────────────────────────────────────────

bool FirebaseManager::fetchUserProfile(const String &patientId, UserProfile &profile) {
    String url  = buildAuthURL("/patients/" + patientId);
    String body = httpGET(url);

    if (body.isEmpty() || body == "null") {
        Serial.printf("[Firebase] Patient '%s' not found.\n", patientId.c_str());
        return false;
    }

    StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
        Serial.printf("[Firebase] Profile JSON error: %s\n", err.c_str());
        return false;
    }

    profile.patientId = patientId;
    profile.name      = doc["name"]     | "Pengguna";
    profile.age       = doc["age"]      | 30;
    profile.gender    = doc["gender"]   | "male";
    profile.disease   = doc["disease"]  | "Umum";
    profile.waNumber  = doc["wa_number"].as<String>();
    profile.stock_servo1 = doc["stock_servo1"] | 0;
    profile.stock_servo2 = doc["stock_servo2"] | 0;
    profile.stock_threshold = doc["stock_threshold"] | 5;

    deriveGreeting(profile);
    Serial.printf("[Firebase] Patient: %s (%s) | Greeting: %s\n",
                  profile.name.c_str(), patientId.c_str(), profile.greeting.c_str());
    return true;
}

bool FirebaseManager::updatePatientStock(const String &patientId, int stock1, int stock2) {
    String url = buildAuthURL("/patients/" + patientId);
    String payload = "{";
    payload += "\"stock_servo1\":" + String(stock1) + ",";
    payload += "\"stock_servo2\":" + String(stock2);
    payload += "}";

    bool ok = httpPATCH(url, payload);
    Serial.printf("[Firebase] Stock updated for %s | A:%d B:%d → %s\n",
                  patientId.c_str(), stock1, stock2, ok ? "OK" : "FAIL");
    return ok;
}

bool FirebaseManager::fetchSchedules(const String &patientId,
                                      std::vector<MedSchedule> &schedules) {
    String url  = buildAuthURL("/schedules/" + patientId);
    String body = httpGET(url);

    if (body.isEmpty() || body == "null") {
        Serial.printf("[Firebase] No schedules for '%s'.\n", patientId.c_str());
        return false;
    }

    DynamicJsonDocument doc(2048);
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
        Serial.printf("[Firebase] Schedule JSON error: %s\n", err.c_str());
        return false;
    }

    schedules.clear();
    for (JsonPair kv : doc.as<JsonObject>()) {
        MedSchedule s;
        s.id        = String(kv.key().c_str());
        s.timeHHMM  = kv.value()["time"]      | "00:00";
        s.qty_servo1 = kv.value()["qty_servo1"] | 0;
        s.qty_servo2 = kv.value()["qty_servo2"] | 0;
        s.status    = kv.value()["status"]     | "Pending";
        schedules.push_back(s);
        Serial.printf("[Firebase] Schedule: %s at %s | S1:%d S2:%d (%s)\n",
                      s.id.c_str(), s.timeHHMM.c_str(),
                      s.qty_servo1, s.qty_servo2, s.status.c_str());
    }
    return true;
}

bool FirebaseManager::updateScheduleStatus(const String &patientId,
                                            const String &scheduleId,
                                            const String &status) {
    String url     = buildAuthURL("/schedules/" + patientId + "/" + scheduleId);
    String payload = "{\"status\":\"" + status + "\"}";
    bool   ok      = httpPATCH(url, payload);
    Serial.printf("[Firebase] Schedule %s/%s → %s : %s\n",
                  patientId.c_str(), scheduleId.c_str(), status.c_str(),
                  ok ? "OK" : "FAIL");
    return ok;
}
