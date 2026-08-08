/**
 * @file rfid_auth.h
 * @brief MFRC522 RFID reader – scan and validate wristband UID.
 */

#pragma once
#include <Arduino.h>
#include <MFRC522.h>
#include <SPI.h>

enum class RFIDStatus {
    NO_CARD,
    AUTHORIZED,
    DENIED
};

class RFIDAuth {
public:
    RFIDAuth();

    /** @brief Initialise SPI bus and MFRC522. Call in setup(). */
    void begin();

    /**
     * @brief Non-blocking poll. Returns RFIDStatus.
     *        Internally debounces repeated reads of the same card.
     */
    RFIDStatus scanStatus();

    /** @brief Returns the raw UID of the last scanned card as a hex string. */
    String getLastUID();

private:
    MFRC522 _rfid;
    String  _lastUID;
    bool    _cardPresent;

    bool uidMatches(MFRC522::Uid &uid);
    String uidToString(MFRC522::Uid &uid);
};
