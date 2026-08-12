/**
 * @file schedule_manager.cpp
 * @brief Schedule evaluation: on-time window detection and missed dose logic.
 */

#include "../include/schedule_manager.h"
#include "../include/config.h"
#include <time.h>

ScheduleManager::ScheduleManager(FirebaseManager &fb) : _fb(fb), _patientId("") {}

void ScheduleManager::setPatientId(const String &patientId) {
    _patientId = patientId;
    Serial.printf("[Schedule] Patient ID set to: %s\n", patientId.c_str());
}

bool ScheduleManager::refresh() {
    if (_patientId.isEmpty()) {
        Serial.println("[Schedule] Cannot refresh: no patient ID set.");
        return false;
    }
    bool ok = _fb.fetchSchedules(_patientId, _schedules);
    Serial.printf("[Schedule] Fetched %zu schedule(s) for %s.\n",
                  _schedules.size(), _patientId.c_str());
    return ok;
}

// ── Time Helpers ──────────────────────────────────────────────────────────────

String ScheduleManager::getCurrentTimeHHMM() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        Serial.println("[Schedule] Time not synced yet.");
        return "00:00";
    }
    char buf[6];
    snprintf(buf, sizeof(buf), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
    return String(buf);
}

int ScheduleManager::toMinutes(const String &hhmm) {
    int h = hhmm.substring(0, 2).toInt();
    int m = hhmm.substring(3, 5).toInt();
    return h * 60 + m;
}

// ── Core Evaluation ───────────────────────────────────────────────────────────

ScheduleCondition ScheduleManager::evaluate(MedSchedule &matchedSchedule) {
    String now       = getCurrentTimeHHMM();
    int    nowMinutes = toMinutes(now);

    for (auto &s : _schedules) {
        if (s.status != "Pending") continue; // Already done or bolos

        int schedMinutes  = toMinutes(s.timeHHMM);
        int diffMinutes   = nowMinutes - schedMinutes;

        if (diffMinutes >= 0 && diffMinutes < MISSED_DOSE_WINDOW_MIN) {
            // Within the dispensing window
            matchedSchedule = s;
            Serial.printf("[Schedule] ON_SCHEDULE: %s (now=%s)\n",
                          s.timeHHMM.c_str(), now.c_str());
            return ScheduleCondition::ON_SCHEDULE;
        }

        if (diffMinutes >= MISSED_DOSE_WINDOW_MIN) {
            // Window passed, medicine not taken → missed dose
            matchedSchedule = s;
            Serial.printf("[Schedule] MISSED: %s (now=%s)\n",
                          s.timeHHMM.c_str(), now.c_str());
            return ScheduleCondition::MISSED;
        }
    }

    return ScheduleCondition::NONE;
}

void ScheduleManager::markTuntas(const String &scheduleId) {
    _fb.updateScheduleStatus(_patientId, scheduleId, "Tuntas");
    for (auto &s : _schedules) {
        if (s.id == scheduleId) { s.status = "Tuntas"; break; }
    }
}

void ScheduleManager::markBolos(const String &scheduleId) {
    _fb.updateScheduleStatus(_patientId, scheduleId, "Bolos");
    for (auto &s : _schedules) {
        if (s.id == scheduleId) { s.status = "Bolos"; break; }
    }
}

String ScheduleManager::getNextScheduleTime() {
    String now        = getCurrentTimeHHMM();
    int    nowMinutes = toMinutes(now);
    String next       = "--:--";
    int    minDiff    = INT_MAX;

    for (const auto &s : _schedules) {
        if (s.status != "Pending") continue;
        int diff = toMinutes(s.timeHHMM) - nowMinutes;
        if (diff > 0 && diff < minDiff) {
            minDiff = diff;
            next    = s.timeHHMM;
        }
    }
    return next;
}

String ScheduleManager::getSchedulesAsJson() const {
    String json = "[";
    bool first = true;
    for (const auto &s : _schedules) {
        if (!first) json += ",";
        json += "{\"time\":\"" + s.timeHHMM + "\",\"qty\":" + String(s.qty_servo1 + s.qty_servo2) + "}";
        first = false;
    }
    json += "]";
    return json;
}
