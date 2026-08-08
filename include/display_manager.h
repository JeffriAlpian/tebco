#pragma once
#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>

enum class FaceExpression {
    NEUTRAL,
    HAPPY,
    SURPRISED,
    WINK,
    SAD,
    LISTENING,
    ANGRY
};

struct FaceState {
    float leftEyeW, leftEyeH, leftEyeR, leftEyeY;
    float rightEyeW, rightEyeH, rightEyeR, rightEyeY;
    bool isLeftArch, isRightArch;
    float mouthCurve; 
    float mouthW;
    float mouthY;
};

class DisplayManager {
public:
    DisplayManager();
    ~DisplayManager(); // Destructor untuk hapus RAM
    void begin();
    void update(); 
    void setExpression(FaceExpression expr);
    void updateStatusIcons(int batteryPct, bool isConnected);
    void showAIResponse(const String& text);
    void showListeningEyes();
    void showProcessingEyes();

private:
    TFT_eSPI _tft; 
    
    // --- RAHASIA ANIMASI MULUS: SPRITE 1-BIT (Cuma 4KB RAM!) ---
    TFT_eSprite* _canvas; 
    // -----------------------------------------------------------

    unsigned long _lastDrawTime = 0;
    const unsigned long FRAME_INTERVAL = 33; // ~30 FPS sudah sangat mulus

    FaceExpression _currentExpr;        
    unsigned long _lastFaceChangeTime;  
    
    unsigned long _lastBlinkTime = 0;
    unsigned long _nextBlinkInterval = 3000; 
    bool _isBlinking = false;

    bool _showingText = false; 
    void _drawWrappedText(const String &text, int16_t x, int16_t y, int16_t maxWidth, int16_t lineHeight);
    void renderFace(const FaceState& state);
    void drawThickBezier(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, int16_t thickness);
    void drawStatusBar(int batteryPct, bool isConnected);
    void _showSplash(); // Tampilan splash saat boot
};