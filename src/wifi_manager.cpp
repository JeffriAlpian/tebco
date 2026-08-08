/**
 * @file wifi_manager.cpp
 * @brief Wi-Fi Manager: STA connection, AP captive portal, Preferences storage.
 */

#include "../include/wifi_manager.h"
#include "../include/config.h"
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>

static WebServer  portalServer(80);
static DNSServer  dnsServer;
static Preferences prefs;

// Flag set by web handler when user submits credentials
static volatile bool _credentialsSaved = false;
static String _newSSID, _newPass, _newAlias;

// ── HTTP Handlers for Captive Portal ────────────────────────────────────────

static void handlePortalRoot() {
    // ── Scan available networks ──────────────────────────────────────────────
    int n = WiFi.scanNetworks();

    // ── Build network list HTML ──────────────────────────────────────────────
    String networkList = "";
    if (n <= 0) {
        networkList = "<p style='color:#aaa;text-align:center'>Tidak ada jaringan ditemukan.<br>Masukkan SSID manual.</p>";
    } else {
        networkList += "<div id='networks'>";
        for (int i = 0; i < n; i++) {
            int rssi = WiFi.RSSI(i);
            // Signal bars: 4=strong, 3=good, 2=fair, 1=weak
            int bars = 1;
            if      (rssi > -55) bars = 4;
            else if (rssi > -65) bars = 3;
            else if (rssi > -75) bars = 2;

            String barIcon = "";
            for (int b = 1; b <= 4; b++) {
                barIcon += "<span style='display:inline-block;width:4px;background:" +
                           String(b <= bars ? "#0f9b8e" : "#444") +
                           ";height:" + String(b * 4 + 2) + "px;margin:0 1px;border-radius:1px;vertical-align:bottom'></span>";
            }

            String lock = WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "🔓" : "🔒";
            String ssidSafe = WiFi.SSID(i);
            ssidSafe.replace("'", "\\'"); // Escape single quotes for JS

            networkList += "<div class='net-item' onclick='selectSSID(\"" + ssidSafe + "\")'>";
            networkList += "<span class='net-name'>" + lock + " " + WiFi.SSID(i) + "</span>";
            networkList += "<span class='net-bars'>" + barIcon + "</span>";
            networkList += "</div>";
        }
        networkList += "</div>";
    }

    WiFi.scanDelete(); // Free scan results from heap

    // ── Full HTML page ───────────────────────────────────────────────────────
    String html = R"rawliteral(
<!DOCTYPE html><html lang="id"><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>TEBCO Wi-Fi Setup</title>
<style>
  *{box-sizing:border-box;margin:0;padding:0}
  body{font-family:'Segoe UI',Arial,sans-serif;background:#0f0f1a;color:#eee;
       display:flex;justify-content:center;align-items:flex-start;min-height:100vh;padding:20px}
  .card{background:#16213e;border-radius:20px;padding:28px;width:100%;max-width:380px;
        box-shadow:0 12px 40px rgba(0,0,0,.6)}
  .logo{text-align:center;font-size:36px;margin-bottom:4px}
  h2{color:#0f9b8e;text-align:center;font-size:22px;margin-bottom:4px}
  .sub{text-align:center;color:#888;font-size:13px;margin-bottom:20px}
  .section-label{font-size:12px;color:#888;text-transform:uppercase;letter-spacing:1px;
                 margin:16px 0 8px;font-weight:600}
  #networks{border:1px solid #2a3a5c;border-radius:12px;overflow:hidden;max-height:220px;
            overflow-y:auto;margin-bottom:8px}
  .net-item{display:flex;justify-content:space-between;align-items:center;
            padding:10px 14px;cursor:pointer;border-bottom:1px solid #1a2a4a;
            transition:background .15s}
  .net-item:last-child{border-bottom:none}
  .net-item:hover{background:#0f3460}
  .net-item.selected{background:#0a4a3a;border-left:3px solid #0f9b8e}
  .net-name{font-size:14px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;max-width:230px}
  .net-bars{display:flex;align-items:flex-end;gap:2px;flex-shrink:0;padding-left:8px}
  input{width:100%;padding:11px 14px;margin:6px 0 14px;border:1px solid #2a3a5c;border-radius:10px;
        background:#0a1628;color:#eee;font-size:15px;outline:none;transition:border .2s}
  input:focus{border-color:#0f9b8e}
  button{width:100%;padding:13px;background:linear-gradient(135deg,#0f9b8e,#0d7a72);color:#fff;
         border:none;border-radius:10px;font-size:16px;cursor:pointer;font-weight:600;
         transition:opacity .2s;margin-top:4px}
  button:hover{opacity:.88}
  .refresh{display:block;text-align:center;color:#0f9b8e;font-size:13px;margin-top:10px;
           cursor:pointer;text-decoration:underline}
</style></head><body>
<div class="card">
  <h2>TEBCO Setup</h2>
  <p class="sub">Pilih jaringan Wi-Fi Anda</p>
  <div class="section-label">📡 Jaringan Tersedia</div>
)rawliteral";

    html += networkList;

    html += R"rawliteral(
  <form action="/save" method="POST">
    <div class="section-label">Nama Wi-Fi (SSID)</div>
    <input id="ssid" name="ssid" type="text" placeholder="Klik jaringan di atas atau ketik manual" required>
    <div class="section-label">Password</div>
    <input name="pass" type="password" placeholder="Password Wi-Fi (kosongkan jika terbuka)" autocomplete="current-password">
    <div class="section-label">Nama Ruangan / Lokasi Alat</div>
    <input name="alias" type="text" placeholder="Contoh: Kamar 301-A" maxlength="32">
    <button type="submit">Simpan &amp; Hubungkan</button>
  </form>
  <a class="refresh" onclick="window.location.reload()">🔄 Refresh Daftar</a>
</div>
<script>
  function selectSSID(ssid) {
    document.getElementById('ssid').value = ssid;
    document.querySelectorAll('.net-item').forEach(el => el.classList.remove('selected'));
    event.currentTarget.classList.add('selected');
  }
</script>
</body></html>
)rawliteral";

    portalServer.send(200, "text/html", html);
}

static void handlePortalSave() {
    if (portalServer.hasArg("ssid") && portalServer.arg("ssid").length() > 0) {
        _newSSID  = portalServer.arg("ssid");
        _newPass  = portalServer.arg("pass");
        _newAlias = portalServer.arg("alias"); // May be empty
        _credentialsSaved = true;

        String aliasDisplay = _newAlias.length() > 0 ? _newAlias : "(belum diatur)";
        portalServer.send(200, "text/html",
            "<html><body style='font-family:Arial;text-align:center;background:#0f0f1a;color:#eee;padding:60px'>"
            "<h2 style='color:#0f9b8e;margin:16px 0'>Tersimpan!</h2>"
            "<p>TEBCO akan restart dan terhubung ke</p>"
            "<b style='font-size:18px;color:#0f9b8e'>" + _newSSID + "</b>"
            "<p style='margin-top:12px;color:#aaa'>Lokasi: " + aliasDisplay + "</p>"
            "<p style='color:#888;margin-top:20px;font-size:13px'>Halaman ini akan tutup otomatis...</p>"
            "</body></html>");
    } else {
        portalServer.send(400, "text/plain", "SSID tidak boleh kosong.");
    }
}

static void handleNotFound() {
    // Redirect everything to portal root (captive portal behaviour)
    portalServer.sendHeader("Location", "http://192.168.4.1/", true);
    portalServer.send(302, "text/plain", "");
}

// ── Class Implementation ─────────────────────────────────────────────────────

WiFiManager::WiFiManager() {}

bool WiFiManager::begin() {
    String ssid, pass;
    loadCredentials(ssid, pass);

    if (ssid.length() > 0) {
        Serial.printf("[WiFi] Saved SSID found: %s\n", ssid.c_str());
        if (connectSTA(ssid, pass)) return true;
        Serial.println("[WiFi] Saved credentials failed. Starting AP...");
    } else {
        Serial.println("[WiFi] No saved credentials. Starting AP...");
    }

    runCaptivePortal();
    return false; // Will reboot after save
}

bool WiFiManager::isConnected() {
    return WiFi.status() == WL_CONNECTED;
}

void WiFiManager::loadCredentials(String &ssid, String &pass) {
    prefs.begin(PREF_NAMESPACE, true); // read-only
    ssid = prefs.getString(PREF_KEY_SSID, "");
    pass = prefs.getString(PREF_KEY_PASS, "");
    prefs.end();
}

void WiFiManager::saveCredentials(const String &ssid, const String &pass) {
    prefs.begin(PREF_NAMESPACE, false); // read-write
    prefs.putString(PREF_KEY_SSID, ssid);
    prefs.putString(PREF_KEY_PASS, pass);
    prefs.end();
    Serial.printf("[WiFi] Credentials saved: SSID=%s\n", ssid.c_str());
}

bool WiFiManager::connectSTA(const String &ssid, const String &pass) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());

    Serial.printf("[WiFi] Connecting to %s", ssid.c_str());
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - start > WIFI_CONNECT_TIMEOUT_MS) {
            Serial.println(" TIMEOUT");
            return false;
        }
        delay(500);
        Serial.print(".");
    }
    Serial.printf("\n[WiFi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
    return true;
}

void WiFiManager::startAP() {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASS);
    Serial.printf("[WiFi] AP started. SSID: %s, IP: %s\n",
                  WIFI_AP_SSID, WiFi.softAPIP().toString().c_str());

    // DNS: redirect all domains to 192.168.4.1
    dnsServer.start(53, "*", WiFi.softAPIP());
}

void WiFiManager::runCaptivePortal() {
    startAP();

    portalServer.on("/",       HTTP_GET,  handlePortalRoot);
    portalServer.on("/save",   HTTP_POST, handlePortalSave);
    portalServer.onNotFound(handleNotFound);
    portalServer.begin();
    Serial.println("[WiFi] Captive portal running...");

    // Block here until user submits credentials
    while (!_credentialsSaved) {
        dnsServer.processNextRequest();
        portalServer.handleClient();
        delay(10);
    }

    saveCredentials(_newSSID, _newPass);
    if (_newAlias.length() > 0) {
        WiFiManager::saveDeviceAlias(_newAlias);
    }
    delay(1500);
    ESP.restart(); // Reboot to connect in STA mode
}

// ── Static: Device Alias ─────────────────────────────────────────────────────

String WiFiManager::getDeviceAlias() {
    Preferences p;
    p.begin(PREF_NAMESPACE_DEV, true);
    String alias = p.getString(PREF_KEY_ALIAS, "");
    p.end();
    return alias;
}

void WiFiManager::saveDeviceAlias(const String &alias) {
    Preferences p;
    p.begin(PREF_NAMESPACE_DEV, false);
    p.putString(PREF_KEY_ALIAS, alias);
    p.end();
    Serial.printf("[WiFi] Device alias saved: %s\n", alias.c_str());
}
