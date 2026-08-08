/**
 * @file wa_gateway.h
 * @brief WhatsApp API notification sender via api.sidobe.com gateway.
 */

#pragma once
#include <Arduino.h>

class WAGateway {
public:
    /**
     * @brief Send a WhatsApp message.
     * @param phone   Destination number in international format (e.g., "628123456789")
     * @param message Full message text (UTF-8)
     * @return HTTP response code (200 = success)
     */
    static int sendMessage(const String &phone, const String &message);

    /**
     * @brief Convenience: send medicine reminder with greeting.
     * @param greeting  e.g., "Pak Budi"
     * @param phone     Destination number
     */
    static void sendMedicineReminder(const String &greeting, const String &phone);

    /**
     * @brief Convenience: send missed dose alert.
     * @param greeting  e.g., "Pak Budi"
     * @param phone     Destination number
     */
    static void sendMissedDoseAlert(const String &greeting, const String &phone);

    /**
     * @brief Convenience: send low medicine stock alert.
     * @param greeting  e.g., "Pak Budi"
     * @param phone     Destination number
     * @param medicineName Name of the medicine (Obat A or Obat B)
     * @param remaining Remaining stock
     */
    static void sendLowStockAlert(const String &greeting, const String &phone, const String &medicineName, int remaining);
};
