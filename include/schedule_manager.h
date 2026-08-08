/**
 * @file schedule_manager.h
 * @brief Manages medication schedule state: on-time, missed, and completed tracking.
 */

#pragma once
#include <Arduino.h>
#include <vector>
#include "firebase_manager.h"

enum class ScheduleCondition {
    NONE,         // No schedule for current time window
    ON_SCHEDULE,  // Current time matches an active schedule
    MISSED        // Schedule window passed without medicine taken
};

class ScheduleManager {
public:
    ScheduleManager(FirebaseManager &fb);

    /** @brief Set the patient ID to use for Firebase paths. Call before refresh(). */
    void setPatientId(const String &patientId);

    /**
     * @brief Refresh schedules from Firebase. Call once after boot
     *        and then periodically (e.g., every hour).
     */
    bool refresh();

    /**
     * @brief Check whether the current time matches any pending schedule.
     * @param[out] matchedSchedule  Populated if ON_SCHEDULE is returned.
     * @return The condition for the current moment.
     */
    ScheduleCondition evaluate(MedSchedule &matchedSchedule);

    /**
     * @brief Mark a schedule as completed in Firebase.
     * @param scheduleId  Firebase key of the schedule.
     */
    void markTuntas(const String &scheduleId);

    /**
     * @brief Mark a schedule as missed in Firebase.
     * @param scheduleId  Firebase key of the schedule.
     */
    void markBolos(const String &scheduleId);

    /** @brief Returns HH:MM of the next pending schedule, or "--:--" if none. */
    String getNextScheduleTime();

    const std::vector<MedSchedule> &getSchedules() const { return _schedules; }

private:
    FirebaseManager          &_fb;
    std::vector<MedSchedule>  _schedules;
    String                    _patientId;

    /**
     * @brief Get current local time as "HH:MM".
     *        Requires NTP to be synced (done in main).
     */
    String getCurrentTimeHHMM();

    /** @brief Convert "HH:MM" to minutes since midnight. */
    int toMinutes(const String &hhmm);
};
