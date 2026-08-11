// wake_word.h
#pragma once
#include <Arduino.h>
#include "audio_pipeline.h"

class WakeWordEngine {
public:
    WakeWordEngine(AudioPipeline &audio);
    bool begin();
    bool check();
    void reset();  // Reset classifier state setelah interupsi (voice query, TTS, dll)
    float lastConfidence() const { return _lastConfidence; }
    bool isMuted() const { return _mutedUntil > millis(); }

private:
    AudioPipeline &_audio;
    float          _lastConfidence;
    bool           _ready;
    uint8_t        _consecutiveHits;
    unsigned long  _mutedUntil;
    bool           _speechActive;   // untuk hysteresis RMS
};