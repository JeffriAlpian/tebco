/**
 * @file firebase_manager.h
 * @brief Handles all Firebase Realtime Database reads and writes.
 *        Supports hospital-scale multi-device architecture:
 *        - Each device has a unique ID derived from MAC address
 *        - Patient assignment is managed by Hospital UI via Firebase
 *        - Device registers itself and sends heartbeat automatically
 */

#pragma once
#include <Arduino.h>
#include <vector>

// ── Data Structures ───────────────────────────────────────────────────────────

struct UserProfile {
    String patientId;  // Firebase patient key, e.g. "P-003"
    String name;
    int    age;
    String gender;    // "male" | "female"
    String disease;
    String greeting;  // Derived: "Sobat" | "Pak X" | "Bu X"
    String waNumber;  // WhatsApp number
    int    stock_servo1;
    int    stock_servo2;
    int    stock_threshold;
};

struct MedSchedule {
    String id;          // Unique key in Firebase
    String timeHHMM;    // "08:00"
    int    qty_servo1;  // Jumlah pil dari Kompartemen A (Servo 1)
    int    qty_servo2;  // Jumlah pil dari Kompartemen B (Servo 2)
    String status;      // "Pending" | "Tuntas" | "Bolos"
};

// ── Device Info ───────────────────────────────────────────────────────────────

struct DeviceInfo {
    String deviceId;   // e.g. "TEBCO-A1B2C3"
    String alias;      // e.g. "Kamar 301-A" (stored in Preferences)
    String assignedPatientId; // null/"" if no patient assigned
};

// ── Class Declaration ─────────────────────────────────────────────────────────

class FirebaseManager {
public:
    FirebaseManager();

    /** @brief Init Firebase. Call after Wi-Fi is up. */
    bool begin();

    // ── Device Management ────────────────────────────────────────────────────

    /**
     * @brief Generate and return this device's unique ID from MAC address.
     *        Format: "TEBCO-AABBCC" (last 3 bytes of MAC)
     */
    static String getDeviceId();

    /**
     * @brief Register/update this device's entry in Firebase /devices/{id}.
     *        Called on boot and every HEARTBEAT_INTERVAL_MS.
     * @param alias  Room alias from Preferences (e.g. "Kamar 301-A")
     * @param batteryPct Battery percentage 0-100
     * @param status "online" | "idle" | "dispensing"
     */
    bool registerDevice(const String &alias, int batteryPct, const String &status = "online");

    /**
     * @brief Check Firebase for patient assignment.
     *        Returns patient ID string, or "" if no patient assigned.
     */
    String checkAssignment();

    /**
     * @brief Update device status field only (lightweight heartbeat).
     * @param batteryPct Battery percentage 0-100
     * @param status "online" | "idle" | "dispensing" | "listening"
     */
    bool updateDeviceStatus(int batteryPct, const String &status);

    // ── Patient Data ─────────────────────────────────────────────────────────

    /**
     * @brief Fetch patient profile from /patients/{patientId}.
     * @param patientId  Patient key from Firebase (e.g. "P-003")
     */
    bool fetchUserProfile(const String &patientId, UserProfile &profile);

    /**
     * @brief Update the medicine stock for a patient in Firebase.
     */
    bool updatePatientStock(const String &patientId, int stock1, int stock2);

    /**
     * @brief Fetch all schedules for a patient from /schedules/{patientId}.
     * @param patientId  Patient key from Firebase
     */
    bool fetchSchedules(const String &patientId, std::vector<MedSchedule> &schedules);

    /**
     * @brief Update a schedule status field in Firebase.
     * @param patientId   Patient key
     * @param scheduleId  Schedule entry key (e.g. "sched_1")
     * @param status      "Tuntas" | "Bolos"
     */
    bool updateScheduleStatus(const String &patientId,
                              const String &scheduleId,
                              const String &status);

private:
    String buildAuthURL(const String &path);
    String httpGET(const String &url);
    bool   httpPATCH(const String &url, const String &payload);
    bool   httpPUT(const String &url, const String &payload);
    void   deriveGreeting(UserProfile &profile);
};
