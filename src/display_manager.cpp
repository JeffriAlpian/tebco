#include "../include/display_manager.h"
#include "../include/config.h"
#include <math.h>

#define COLOR_BG TFT_BLACK
#define COLOR_FACE TFT_WHITE
#define COLOR_OFF 0x4208 // Abu-abu gelap untuk WiFi mati

static const int16_t FACE_CX = 120;
static const int16_t FACE_CY = 115; // Agak ke bawah agar ada ruang untuk status bar
static const int16_t EYE_DIST = 65;

FaceState stateStart;
FaceState stateTarget;
FaceState stateCurrent;

unsigned long animStartTime = 0;
unsigned long animDuration = 400;
bool isAnimating = false;

// ── Fungsi Matematika (Easing & Interpolasi) ─────────────────────────────
float easeOutBack(float t)
{
    // c1 mengatur seberapa kuat efek "memantul" / jelly-nya
    const float c1 = 1.70158f;
    const float c3 = c1 + 1.0f;
    return 1.0f + c3 * powf(t - 1.0f, 3.0f) + c1 * powf(t - 1.0f, 2.0f);
}

float lerpF(float a, float b, float t)
{
    return a + (b - a) * t;
}

FaceState lerpFace(const FaceState &a, const FaceState &b, float t)
{
    FaceState res;
    res.leftEyeW = lerpF(a.leftEyeW, b.leftEyeW, t);
    res.leftEyeH = lerpF(a.leftEyeH, b.leftEyeH, t);
    res.leftEyeR = lerpF(a.leftEyeR, b.leftEyeR, t);
    res.leftEyeY = lerpF(a.leftEyeY, b.leftEyeY, t);
    res.rightEyeW = lerpF(a.rightEyeW, b.rightEyeW, t);
    res.rightEyeH = lerpF(a.rightEyeH, b.rightEyeH, t);
    res.rightEyeR = lerpF(a.rightEyeR, b.rightEyeR, t);
    res.rightEyeY = lerpF(a.rightEyeY, b.rightEyeY, t);
    res.mouthCurve = lerpF(a.mouthCurve, b.mouthCurve, t);
    res.mouthW = lerpF(a.mouthW, b.mouthW, t);
    res.mouthY = lerpF(a.mouthY, b.mouthY, t);
    res.isLeftArch = (t > 0.5f) ? b.isLeftArch : a.isLeftArch;
    res.isRightArch = (t > 0.5f) ? b.isRightArch : a.isRightArch;
    return res;
}

// ── Core Rendering Engine ────────────────────────────────────────────────
// MENGGUNAKAN TFT_eSPI
DisplayManager::DisplayManager()
    : _tft(),
      _canvas(nullptr)
{
}

DisplayManager::~DisplayManager()
{
    if (_canvas)
        delete _canvas;
}

void DisplayManager::begin()
{
    // ── Langkah 1: Init TFT_eSPI ─────────────────────────────────────────────
    // Library TFT_eSPI akan otomatis menggunakan konfigurasi pin yang
    // telah didefinisikan di User_Setup.h
    _tft.init();
    _tft.setRotation(0);
    _tft.fillScreen(COLOR_BG);

    // ── Langkah 2: Splash Screen ──────────────────────────────────────────────
    _showSplash();

    // ── Langkah 3: Alokasi Sprite 1-bit (hanya 4KB RAM) ─────────────────────
    _canvas = new TFT_eSprite(&_tft);
    _canvas->setColorDepth(1);
    _canvas->createSprite(200, 160);
    _canvas->setBitmapColor(COLOR_FACE, COLOR_BG); // Warna garis/mata = Putih, Latar = Hitam

    // Initialize stateCurrent BEFORE calling setExpression to prevent NaN/garbage
    // data from corrupting the lerpFace interpolation.
    stateCurrent = {45, 55, 10, 0, 45, 55, 10, 0, false, false, 0.7f, 60, -5};

    _currentExpr = FaceExpression::HAPPY;
    setExpression(FaceExpression::HAPPY);
    stateCurrent = stateTarget;

    // ── Langkah 4: Hapus splash screen dan paksa render frame pertama ──
    // Karena setup WiFi bisa memakan waktu lama, kita harus merender wajah
    // sekarang agar layar tidak dibiarkan blank atau membeku.
    _tft.fillScreen(COLOR_BG);
    renderFace(stateCurrent);
    drawStatusBar(100, false);
}

// Render langsung ke RAM (1 = Putih, 0 = Hitam)
void DisplayManager::drawThickBezier(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, int16_t thickness)
{
    if (!_canvas)
        return;
    float step = 1.0f / 30.0f;
    for (float u = 0; u <= 1.0f; u += step)
    {
        float u1 = 1.0f - u;
        int16_t px = (int16_t)(u1 * u1 * x0 + 2.0f * u1 * u * x1 + u * u * x2);
        int16_t py = (int16_t)(u1 * u1 * y0 + 2.0f * u1 * u * y1 + u * u * y2);

        // Offset -20 X dan -40 Y karena kotak canvas 200x160 berada di tengah layar
        _canvas->fillCircle(px - 20, py - 40, thickness / 2, 1);
    }
}

void DisplayManager::renderFace(const FaceState &state)
{
    if (!_canvas)
        return;
    _canvas->fillSprite(0);

    // --- ANIMASI PROSEDURAL (LIVE BREATHING) ---
    // Membuat efek "bernapas" yang berjalan terus menerus di latar belakang
    float timeSec = millis() / 1000.0f;
    float floatY = sin(timeSec * 3.0f) * 3.0f;     // Mata mengambang naik-turun pelan (3px)
    float mouthPulse = sin(timeSec * 6.0f) * 4.0f; // Mulut kembang-kempis elastis (4px)

    int16_t lW = max(2, (int)state.leftEyeW);
    int16_t lH = max(2, (int)state.leftEyeH);
    int16_t lR = max(0, min((int)state.leftEyeR, min(lW, lH) / 2));

    int16_t rW = max(2, (int)state.rightEyeW);
    int16_t rH = max(2, (int)state.rightEyeH);
    int16_t rR = max(0, min((int)state.rightEyeR, min(rW, rH) / 2));

    // Gambar Mata Kiri (Terapkan floatY)
    int16_t lx = FACE_CX - EYE_DIST;
    int16_t ly = FACE_CY + (int16_t)state.leftEyeY + floatY;
    if (state.isLeftArch)
    {
        drawThickBezier(lx - 25, ly + 15, lx, ly - 20, lx + 25, ly + 15, 12);
    }
    else
    {
        _canvas->fillRoundRect((lx - lW / 2) - 20, (ly - lH / 2) - 40, lW, lH, lR, 1);
    }

    // Gambar Mata Kanan (Terapkan floatY)
    int16_t rx = FACE_CX + EYE_DIST;
    int16_t ry = FACE_CY + (int16_t)state.rightEyeY + floatY;
    if (state.isRightArch)
    {
        drawThickBezier(rx - 25, ry + 15, rx, ry - 20, rx + 25, ry + 15, 12);
    }
    else
    {
        _canvas->fillRoundRect((rx - rW / 2) - 20, (ry - rH / 2) - 40, rW, rH, rR, 1);
    }

    // Gambar Mulut (Terapkan mouthPulse dan sebagian floatY)
    int16_t my = FACE_CY + 50 + (int16_t)state.mouthY + (floatY * 0.5f);
    int16_t mw = max(2, (int)state.mouthW + (int)mouthPulse); // Mulut bergerak-gerak!
    int16_t mControlY = my + (int16_t)(state.mouthCurve * 30.0f);
    drawThickBezier(FACE_CX - mw / 2, my, FACE_CX, mControlY, FACE_CX + mw / 2, my, 10);

    _canvas->pushSprite(20, 40);
}

void DisplayManager::update()
{
    unsigned long now = millis();

    // ── Auto-Revert: Kembali ke HAPPY setelah 5 detik ekspresi non-HAPPY ──
    // SKIP jika sedang dalam mode listening (VAD aktif) agar wajah tidak hilang
    if (!_showingText && !_listeningMode && _currentExpr != FaceExpression::HAPPY)
    {
        if (now - _lastFaceChangeTime >= 5000)
        {
            setExpression(FaceExpression::HAPPY);
        }
    }

    // ── Auto-Revert: Kembali ke wajah setelah 5 detik menampilkan teks ──
    if (_showingText)
    {
        if (now - _lastFaceChangeTime >= 5000)
        {
            setExpression(FaceExpression::HAPPY);
            // setExpression sudah set _showingText = false
        }
        return; // Jangan render wajah saat menampilkan teks
    }

    // ── Logika Kedip (Blink) dengan Ritme Biologis ──
    if (!isAnimating)
    {
        if (now - _lastBlinkTime >= _nextBlinkInterval)
        {
            if (!_isBlinking)
            {
                _isBlinking = true;
                stateStart = stateCurrent;
                stateTarget = stateCurrent;

                // --- RANDOMIZER KEDIPAN ---
                int blinkType = random(0, 100);

                if (blinkType < 60)
                {
                    // 60% Peluang: Kedip Normal (Dua mata tertutup rapat)
                    stateTarget.leftEyeH = 4;
                    stateTarget.rightEyeH = 4;
                }
                else if (blinkType < 80)
                {
                    // 20% Peluang: Wink (Hanya mata kiri yang berkedip)
                    stateTarget.leftEyeH = 4;
                    // Mata kanan dibiarkan terbuka (mengambil dari stateCurrent)
                }
                else
                {
                    // 20% Peluang: Kedip "Gede Sebelah" / Kebingungan Lucu
                    // Kiri merem separuh, Kanan merem rapat dan agak naik
                    stateTarget.leftEyeH = 20;
                    stateTarget.rightEyeH = 4;
                    stateTarget.rightEyeY -= 5;
                }

                stateTarget.isLeftArch = false;
                stateTarget.isRightArch = false;

                animStartTime = now;
                animDuration = 70; // Durasi kedip sedikit diperlambat agar lebih terasa
                isAnimating = true;
            }
            else
            {
                // Membuka mata (LAMBAT DAN MEMANTUL - 300ms)
                // ... (biarkan bagian ini tetap sama seperti kode lamamu)
                _isBlinking = false;
                _lastBlinkTime = now;
                _nextBlinkInterval = random(2000, 5000);

                unsigned long savedTimer = _lastFaceChangeTime;
                setExpression(_currentExpr);
                _lastFaceChangeTime = savedTimer;

                animDuration = 300;
            }
        }
    }

    // ── Frame Rate Limiter (~30 FPS) ──
    if (now - _lastDrawTime < FRAME_INTERVAL)
        return;
    _lastDrawTime = now;

    // ── Render: Animasi ATAU Idle ──
    if (isAnimating)
    {
        float t = (float)(now - animStartTime) / animDuration;

        if (t >= 1.0f)
        {
            t = 1.0f;
            isAnimating = false;
            if (_isBlinking)
                _lastBlinkTime = now - _nextBlinkInterval;
        }

        float easedT = easeOutBack(t);
        stateCurrent = lerpFace(stateStart, stateTarget, easedT);
    }

    // SELALU render wajah setiap frame — baik saat animasi maupun idle.
    // Ini menjamin wajah TIDAK PERNAH hilang meskipun ada gangguan SPI
    // dari modul lain (RFID, WiFi HTTP, Firebase).
    renderFace(stateCurrent);
}

void DisplayManager::setExpression(FaceExpression expr)
{
    if (_showingText)
    {
        // Jika sebelumnya sedang menampilkan teks, bersihkan layar (kecuali status bar)
        // agar sisa teks AI hilang sebelum wajah digambar
        _tft.fillRect(0, 30, 240, 210, COLOR_BG);
    }

    _showingText = false;
    stateStart = stateCurrent;
    _currentExpr = expr;
    _lastFaceChangeTime = millis();
    animStartTime = millis();
    isAnimating = true;

    // Kembalikan durasi ke kecepatan normal (karena animasi kedip mengubahnya ke 100ms)
    animDuration = 400;

    switch (expr)
    {
    case FaceExpression::NEUTRAL:
    case FaceExpression::HAPPY:
        // Wajah Standby (Mata Persegi Panjang Rounded 10%)
        // W=45, H=55, Radius=5 (~10%). isArch = false (agar menjadi kotak, bukan kurva)
        // Mulut melengkung senyum (0.7f), lebar mulut 60.
        stateTarget = {45, 55, 10, 0, 45, 55, 10, 0, false, false, 0.7f, 60, -5};
        break;

    case FaceExpression::SURPRISED:
        // Mata membulat sempurna (O_O)
        stateTarget = {60, 60, 30, 0, 60, 60, 30, 0, false, false, 0.0f, 25, 25};
        break;

    case FaceExpression::WINK:
        // Kiri mengedip (garis), kanan membulat
        stateTarget = {45, 8, 4, 10, 60, 60, 30, -5, false, false, 0.3f, 45, 0};
        break;

    case FaceExpression::SAD:
        // Mata pipih/turun, senyum terbalik
        stateTarget = {45, 20, 10, 15, 45, 20, 10, 15, false, false, -0.8f, 50, 15};
        break;

    case FaceExpression::LISTENING:
        stateTarget = {65, 65, 32, 0, 30, 30, 15, -10, false, false, 0.0f, 15, 20};
        break;

    case FaceExpression::ANGRY:
        // Mata menyipit kotak (Radius = 0) dan turun. Mulut cemberut ekstrem (-1.2f)
        // Format: {lW, lH, lR, lY, rW, rH, rR, rY, isLeftArch, isRightArch, mouthCurve, mouthW, mouthY}
        stateTarget = {50, 12, 0, 15, 50, 12, 0, 15, false, false, -1.2f, 45, 15};
        break;
    }
}

void DisplayManager::drawStatusBar(int batteryPct, bool isConnected)
{
    _tft.fillRect(0, 0, 240, 30, COLOR_BG);
    _tft.drawRect(200, 5, 30, 15, COLOR_FACE);
    int barWidth = (int)(26 * (batteryPct / 100.0));
    _tft.fillRect(202, 7, barWidth, 11, COLOR_FACE);

    int16_t x = 20, y = 20;
    _tft.fillCircle(x, y, 2, isConnected ? COLOR_FACE : COLOR_OFF);

    if (isConnected)
    {
        _tft.drawCircleHelper(x, y, 6, 3, COLOR_FACE);
        _tft.drawCircleHelper(x, y, 10, 3, COLOR_FACE);
        _tft.drawCircleHelper(x, y, 14, 3, COLOR_FACE);
    }
    else
    {
        _tft.drawCircleHelper(x, y, 6, 3, COLOR_OFF);
    }

    // Tampilkan IP address di pojok kanan bawah (kecil)
    if (_ipAddress.length() > 0)
    {
        _tft.setTextFont(1);        // Font bawaan terkecil TFT_eSPI (~6x8px)
        _tft.setTextSize(1);
        _tft.setTextColor(COLOR_OFF, COLOR_BG); // Abu-abu gelap, tidak mencolok
        int16_t ipW = _tft.textWidth(_ipAddress);
        _tft.setCursor(240 - ipW - 4, 240 - 12); // Pojok kanan bawah, margin 4px
        _tft.print(_ipAddress);
    }
}

void DisplayManager::setIPAddress(const String &ip)
{
    _ipAddress = ip;
    // Langsung render ulang area bawah
    if (_ipAddress.length() > 0)
    {
        _tft.fillRect(0, 228, 240, 12, COLOR_BG); // Bersihkan baris bawah (240 - 12)
        _tft.setTextFont(1);
        _tft.setTextSize(1);
        _tft.setTextColor(COLOR_OFF, COLOR_BG);
        int16_t ipW = _tft.textWidth(_ipAddress);
        _tft.setCursor(240 - ipW - 4, 240 - 12);
        _tft.print(_ipAddress);
    }
}

// Gunakan fungsi ini dengan Stateful Timer dari TEBCO.ino (Setiap 10 detik)
void DisplayManager::updateStatusIcons(int batteryPct, bool isConnected)
{
    static int lastBattery = -1;
    static bool lastWifi = false;

    // Selalu gambar ulang jika ada IP baru, atau jika status berubah
    if (batteryPct != lastBattery || isConnected != lastWifi)
    {
        drawStatusBar(batteryPct, isConnected);
        lastBattery = batteryPct;
        lastWifi = isConnected;
    }
    else if (_ipAddress.length() > 0)
    {
        // Pastikan IP tetap terlihat meskipun status baterai/WiFi tidak berubah
        _tft.setTextFont(1);
        _tft.setTextSize(1);
        _tft.setTextColor(COLOR_OFF, COLOR_BG);
        int16_t ipW = _tft.textWidth(_ipAddress);
        _tft.setCursor(240 - ipW - 4, 240 - 12);
        _tft.print(_ipAddress);
    }
}

void DisplayManager::setListeningMode(bool active)
{
    _listeningMode = active;
    if (active)
    {
        // Reset timer agar auto-revert tidak langsung terpicu
        _lastFaceChangeTime = millis();
    }
}

// void DisplayManager::showListeningEyes() { setExpression(FaceExpression::SURPRISED); }
void DisplayManager::showProcessingEyes() { setExpression(FaceExpression::WINK); }
void DisplayManager::showListeningEyes()
{
    setExpression(FaceExpression::LISTENING);
}
// ── Word-wrap manual dengan jarak antar baris yang lega ─────────────────
// TFT_eSPI's built-in print()+setTextWrap() tidak memberi jarak antar baris
// (line height = tinggi font persis, 0 spasi tambahan) -> teks terlihat padat.
// Fungsi ini membungkus per kata dan menggambar baris demi baris dengan
// lineHeight yang sudah ditambah spasi ekstra.
void DisplayManager::_drawWrappedText(const String &text, int16_t x, int16_t y,
                                      int16_t maxWidth, int16_t lineHeight)
{
    String line = "";
    int16_t cy = y;
    int start = 0;
    int len = text.length();

    while (start < len)
    {
        int spaceIdx = text.indexOf(' ', start);
        String word = (spaceIdx == -1) ? text.substring(start) : text.substring(start, spaceIdx);
        String testLine = line.length() ? (line + " " + word) : word;

        if (_tft.textWidth(testLine) > maxWidth && line.length() > 0)
        {
            _tft.setCursor(x, cy);
            _tft.print(line);
            cy += lineHeight;
            line = word;
        }
        else
        {
            line = testLine;
        }
        start = (spaceIdx == -1) ? len : spaceIdx + 1;
    }

    if (line.length())
    {
        _tft.setCursor(x, cy);
        _tft.print(line);
    }
}

void DisplayManager::showAIResponse(const String &text)
{
    isAnimating = false;
    _showingText = true;
    // FIX: timer auto-revert HARUS di-reset di sini. Sebelumnya ini tidak
    // di-set, jadi update() bisa membaca _lastFaceChangeTime yang basi
    // (dari perubahan ekspresi sebelumnya) dan langsung memanggil
    // setExpression() -> _showingText jadi false lagi -> wajah menimpa teks
    // padahal baru sepersekian detik teks tampil.
    _lastFaceChangeTime = millis();

    _tft.fillRect(0, 30, 240, 210, COLOR_BG); // Hanya hapus area bawah status bar
    _tft.setTextColor(COLOR_FACE, COLOR_BG);
    _tft.setTextSize(2);
    _tft.setTextWrap(false); // wrapping sekarang ditangani manual oleh _drawWrappedText

    // x=10, y=60, maxWidth=220 (lebar layar 240 dikurangi margin kiri/kanan),
    // lineHeight=28 (tinggi font size2 ~16px + spasi ekstra ~12px antar baris)
    _drawWrappedText(text, 10, 60, 220, 28);

    // FIX: delay(5000) dihapus. Class ini SUDAH punya mekanisme non-blocking
    // untuk auto-revert (lihat update(): blok "Auto-Revert teks" di atas,
    // yang membaca _showingText + _lastFaceChangeTime). Memakai delay() di
    // sini membekukan seluruh loop() (audio, WiFi, tombol) selama 5 detik
    // DAN duplikat/bentrok dengan timer yang sudah ada di update().
    // Cukup keluar dari fungsi ini; update() akan otomatis kembali ke wajah
    // HAPPY setelah 5 detik berjalan.
}

// ── Splash Screen saat Boot ───────────────────────────────────────────────
void DisplayManager::_showSplash()
{
    _tft.fillScreen(COLOR_BG); // Latar hitam

    // ── Garis dekoratif atas ─────────────────────────────────────────────
    _tft.fillRect(30, 75, 180, 2, COLOR_FACE);

    // ── Nama Perangkat (TEBCO) — Besar, tengah ───────────────────────────
    // Ukuran font 4 = 24px tinggi, setiap karakter 6*4=24px lebar
    const char *name = DEVICE_NAME; // "TEBCO" dari config.h
    _tft.setTextSize(5);
    _tft.setTextColor(COLOR_FACE, COLOR_BG);

    // Hitung lebar teks untuk tengah horizontal (font5: 6px * scale * jumlah char)
    int16_t nameW = strlen(name) * 6 * 5;
    int16_t nameX = (240 - nameW) / 2;
    _tft.setCursor(nameX, 90);
    _tft.print(name);

    // ── Garis dekoratif bawah ─────────────────────────────────────────────
    _tft.fillRect(30, 145, 180, 2, COLOR_FACE);

    // ── Versi firmware — Kecil, tengah ────────────────────────────────────
    // Ukuran font 2 = 12px, setiap char = 6*2=12px lebar
    const char *ver = "v" FIRMWARE_VERSION; // "v1.0.0" dari config.h
    _tft.setTextSize(2);
    int16_t verW = strlen(ver) * 6 * 2;
    int16_t verX = (240 - verW) / 2;
    _tft.setCursor(verX, 155);
    _tft.print(ver);

    // Tahan 2 detik agar sempat terbaca
    delay(2000);
}