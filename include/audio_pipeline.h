/**
 * @file audio_pipeline.h
 * @brief I2S Microphone (INMP441) & Speaker (MAX98357A) Audio Pipeline with VAD
 */

#pragma once
#include <Arduino.h>
#include <driver/i2s_std.h>
#include "Audio.h"

class AudioPipeline {
public:
    AudioPipeline();

    bool begin();
    void stop();
    void loop();

    // Controls
    void setVolume(uint8_t vol);
    bool isPlaying();

    // Buffer Calculation
    size_t calcBufferSize(uint8_t durationSec) const;

    // Recording Methods
    size_t record(int16_t *outBuffer, size_t bufSize, uint8_t durationSec);
    size_t readFrame(int16_t *frameBuffer, size_t num_samples);

    /**
     * @brief Perekaman berbasis Voice Activity Detection (VAD)
     * @param outBuffer Target buffer PCM
     * @param maxBufSize Batas maksimal ukuran buffer
     * @param silenceTimeoutMs Durasi diam (ms) untuk memicu Auto-Stop setelah mulai bicara (default 1000ms)
     * @param initialTimeoutMs Durasi tunggu suara (ms) sebelum kembali ke Standby (default 3000ms)
     * @return size_t Ukuran byte yang berhasil direkam (0 jika timeout tanpa suara)
     */
    size_t recordVAD(int16_t *outBuffer, size_t maxBufSize, uint16_t silenceTimeoutMs = 1000, uint16_t initialTimeoutMs = 3000);

    // Playback
    void speakText(const String &text, const char *lang = "id");
    void playFromURL(const String &url);
    void play(const int16_t *audioBuffer, size_t bufSize);
    void playTone(uint16_t freqHz, uint16_t durationMs, int16_t amplitude = 10000);
    void playDroneStartup();

private:
    Audio *_audioLib;
    bool _speakerReady;
    i2s_chan_handle_t _mic_rx_chan;

    bool initMic();
    bool initSpeaker();
};