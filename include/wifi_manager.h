/**
 * @file wifi_manager.h
 * @brief Handles Wi-Fi connection, AP captive portal, and credential persistence.
 *        Also manages device alias (room name) stored in Preferences.
 */

#pragma once
#include <Arduino.h>

class WiFiManager {
public:
    WiFiManager();

    /**
     * @brief Call on boot. Loads saved credentials and attempts STA connection.
     *        Falls back to AP captive portal if connection fails.
     * @return true if connected to internet in STA mode.
     */
    bool begin();

    /** @return true if currently connected to Wi-Fi in STA mode. */
    bool isConnected();

    /** @brief Blocking: runs the captive portal web server until credentials are saved. */
    void runCaptivePortal();

    /**
     * @brief Load saved device alias (room name) from Preferences.
     * @return Alias string, e.g. "Kamar 301-A". Empty string if not set.
     */
    static String getDeviceAlias();

    /**
     * @brief Save device alias to Preferences.
     * @param alias Room name, e.g. "Kamar 301-A"
     */
    static void saveDeviceAlias(const String &alias);

private:
    void loadCredentials(String &ssid, String &pass);
    void saveCredentials(const String &ssid, const String &pass);
    bool connectSTA(const String &ssid, const String &pass);
    void startAP();
};

