/**
 * @file rfid_auth.cpp
 * @brief MFRC522 RFID reader implementation with UID whitelist check.
 */

#include "../include/rfid_auth.h"
#include "../include/config.h"

// Allowed UID bytes from config.h
static const byte ALLOWED_UID[RFID_UID_LEN] = RFID_ALLOWED_UID;

RFIDAuth::RFIDAuth()
    : _rfid(PIN_RFID_SS, PIN_RFID_RST), _cardPresent(false) {}

void RFIDAuth::begin()
{
    _rfid.PCD_Init();
    delay(4); // Tunggu MFRC522 siap
    _rfid.PCD_DumpVersionToSerial();
    Serial.println("[RFID] Initialised.");
}

RFIDStatus RFIDAuth::scanStatus()
{
    // No new card in RF field
    if (!_rfid.PICC_IsNewCardPresent())
    {
        _cardPresent = false;
        return RFIDStatus::NO_CARD;
    }
    // Card present but could not read serial
    if (!_rfid.PICC_ReadCardSerial())
    {
        return RFIDStatus::NO_CARD;
    }

    String uid = uidToString(_rfid.uid);

    // Debounce: ignore repeated read of same card without removal
    if (_cardPresent && uid == _lastUID)
    {
        _rfid.PICC_HaltA();
        return RFIDStatus::NO_CARD;
    }

    _lastUID = uid;
    _cardPresent = true;

    Serial.printf("[RFID] Card detected UID: %s\n", uid.c_str());
    _rfid.PICC_HaltA();      // Halt card
    _rfid.PCD_StopCrypto1(); // Stop encryption

    if (uidMatches(_rfid.uid)) {
        return RFIDStatus::AUTHORIZED;
    } else {
        Serial.println("[RFID] ❌ Access Denied: Unknown wristband.");
        return RFIDStatus::DENIED;
    }
}

String RFIDAuth::getLastUID() { return _lastUID; }

// ── Private ───────────────────────────────────────────────────────────────────

bool RFIDAuth::uidMatches(MFRC522::Uid &uid)
{
    if (uid.size != RFID_UID_LEN)
        return false;
    for (uint8_t i = 0; i < RFID_UID_LEN; i++)
    {
        if (uid.uidByte[i] != ALLOWED_UID[i])
            return false;
    }
    Serial.println("[RFID] ✅ Authorised wristband.");
    return true;
}

String RFIDAuth::uidToString(MFRC522::Uid &uid)
{
    String s = "";
    for (uint8_t i = 0; i < uid.size; i++)
    {
        if (uid.uidByte[i] < 0x10)
            s += "0";
        s += String(uid.uidByte[i], HEX);
    }
    s.toUpperCase();
    return s;
}
