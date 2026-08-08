// wake_word.cpp
#include "../include/wake_word.h"
#include "../include/config.h"
#include <jeffrialpian-project-1_inferencing.h>

static int16_t *audio_buffer = nullptr;

static int microphone_audio_signal_get_data(size_t offset, size_t length, float *out_ptr)
{
    numpy::int16_to_float(&audio_buffer[offset], out_ptr, length);
    return 0;
}

WakeWordEngine::WakeWordEngine(AudioPipeline &audio)
    : _audio(audio), _lastConfidence(0.0f), _ready(false),
      _consecutiveHits(0), _mutedUntil(0), _speechActive(false) {}

bool WakeWordEngine::begin()
{
    Serial.println("[KWS] Memulai Edge Impulse Continuous Inference...");

    if (EI_CLASSIFIER_FREQUENCY != 16000)
    {
        Serial.println("[ERROR] Frekuensi model EI harus 16kHz!");
        return false;
    }

    if (audio_buffer == nullptr)
    {
        audio_buffer = (int16_t *)malloc(EI_CLASSIFIER_SLICE_SIZE * sizeof(int16_t));
    }

    run_classifier_init();
    _ready = true;
    return true;
}

bool WakeWordEngine::check()
{
    if (!_ready)
        return false;

    // 0. Mute saat speaker aktif atau dalam cooldown
    if (_audio.isPlaying())
    {
        _consecutiveHits = 0;
        _speechActive = false;
        _mutedUntil = millis() + KWS_POST_TTS_MUTE_MS;
        return false;
    }

    if (millis() < _mutedUntil)
        return false;

    // 1. Baca frame audio
    size_t samples_read = _audio.readFrame(audio_buffer, EI_CLASSIFIER_SLICE_SIZE);
    if (samples_read < EI_CLASSIFIER_SLICE_SIZE)
        return false;

    // 2. Hitung RMS dengan hysteresis
    int64_t sum_squares = 0;
    for (size_t i = 0; i < samples_read; i++)
    {
        sum_squares += ((int32_t)audio_buffer[i] * (int32_t)audio_buffer[i]);
    }
    float rms = sqrtf((float)sum_squares / samples_read);

    // Hysteresis: threshold turun 20% jika sudah dalam mode bicara
    float effectiveGate = _speechActive ? KWS_RMS_SILENCE_GATE * 0.8f : KWS_RMS_SILENCE_GATE;

    if (rms < effectiveGate)
    {
        _consecutiveHits = 0;
        _speechActive = false;
        return false;
    }
    _speechActive = true;

    // 3. Siapkan sinyal untuk Edge Impulse
    signal_t sig;  // ganti nama dari 'signal' untuk menghindari konflik dengan fungsi C signal
    sig.total_length = EI_CLASSIFIER_SLICE_SIZE;
    sig.get_data = &microphone_audio_signal_get_data;

    // 4. Jalankan klasifikasi dengan Moving Average Filter (MAF) diaktifkan
    ei_impulse_result_t result = {0};
    EI_IMPULSE_ERROR r = run_classifier_continuous(&sig, &result, false, true); // enable_maf = true
    if (r != EI_IMPULSE_OK)
        return false;

    // 5. Ambil confidence label target
    float target_confidence = 0.0f;
    float best_other_confidence = 0.0f;
    for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++)
    {
        const char *label = result.classification[ix].label;
        const float value = result.classification[ix].value;

        if (strcmp(label, KWS_TARGET_LABEL_CFG) == 0)
        {
            target_confidence = value;
            _lastConfidence = target_confidence;
        }
        else if (value > best_other_confidence)
        {
            best_other_confidence = value;
        }
    }

    const float confidence_margin = target_confidence - best_other_confidence;
    const bool strong_target_match = (target_confidence >= KWS_THRESHOLD_CFG) &&
                                     (confidence_margin >= KWS_CONFIDENCE_MARGIN_CFG);

    // Debug (opsional)
    if (target_confidence >= 0.35f)
    {
        Serial.printf("[KWS] Detected '%s': %.2f | Margin: %.2f | RMS: %.1f\n",
                      KWS_TARGET_LABEL_CFG, target_confidence, confidence_margin, rms);
    }

    // 6. Evaluasi deteksi
    // Syarat A: Trigger instan (skor sangat tinggi)
    if ((target_confidence >= KWS_INSTANT_THRESHOLD_CFG) && strong_target_match)
    {
        Serial.printf("[KWS] 🟢 WAKE WORD TERDETEKSI (Instan! Skor: %.2f, Margin: %.2f)\n",
                      target_confidence, confidence_margin);
        _consecutiveHits = 0;
        _speechActive = false;
        _mutedUntil = millis() + KWS_POST_DETECT_COOLDOWN_MS;
        return true;
    }

    // Syarat B: Akumulasi consecutive hits
    if (strong_target_match)
    {
        _consecutiveHits++;
        Serial.printf("[KWS] Hit ucapan (%d/%d) - Confidence: %.2f, Margin: %.2f\n",
                      _consecutiveHits, KWS_CONSECUTIVE_HITS_CFG, target_confidence, confidence_margin);

        if (_consecutiveHits >= KWS_CONSECUTIVE_HITS_CFG)
        {
            Serial.printf("[KWS] 🟢 WAKE WORD VALID TERDETEKSI! Skor: %.2f, Margin: %.2f\n",
                          target_confidence, confidence_margin);
            _consecutiveHits = 0;
            _speechActive = false;
            _mutedUntil = millis() + KWS_POST_DETECT_COOLDOWN_MS;
            return true;
        }
    }
    else
    {
        // Penalti: turunkan perlahan jika satu slice meleset
        if (_consecutiveHits > 0)
            _consecutiveHits--;
    }

    return false;
}