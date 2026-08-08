/**
 * @file ai_assistant.h
 * @brief Continuous Conversation AI Voice Assistant Manager
 */

#pragma once
#include <Arduino.h>
#include "audio_pipeline.h"
#include "display_manager.h"

class AIAssistant {
public:
    AIAssistant(AudioPipeline &audio, DisplayManager &display);

    /**
     * @brief Menjalankan siklus percakapan interaktif berulang (Continuous Conversation Loop).
     *        1. Rekam VAD (Auto-stop jika diam 1 detik, Standby jika diam 3 detik)
     *        2. Base64 & Send ke n8n Webhook
     *        3. Terima teks respons & putar via Google TTS
     *        4. Otomatis masuk mode Listening lagi tanpa memanggil wake word.
     */
    void handleVoiceQuery(const String &patientId, const String &userGreeting, const String &disease);

private:
    AudioPipeline  &_audio;
    DisplayManager &_display;
};