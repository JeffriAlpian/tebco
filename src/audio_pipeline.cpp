/**
 * @file audio_pipeline.cpp
 * @brief Implementation of Audio Pipeline with Real-time VAD Engine
 */

#include "../include/audio_pipeline.h"
#include "../include/config.h"
#include <math.h>

AudioPipeline::AudioPipeline()
    : _audioLib(new Audio(I2S_NUM_1)), _speakerReady(false), _mic_rx_chan(nullptr) {}

bool AudioPipeline::initMic() {
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    esp_err_t err = i2s_new_channel(&chan_cfg, NULL, &_mic_rx_chan);
    if (err != ESP_OK) return false;

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(I2S_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = (gpio_num_t)PIN_MIC_SCK,
            .ws   = (gpio_num_t)PIN_MIC_WS,
            .dout = I2S_GPIO_UNUSED,
            .din  = (gpio_num_t)PIN_MIC_SD,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
        },
    };
    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;

    err = i2s_channel_init_std_mode(_mic_rx_chan, &std_cfg);
    if (err != ESP_OK) return false;

    err = i2s_channel_enable(_mic_rx_chan);
    if (err != ESP_OK) return false;

    Serial.println("[Audio] INMP441 mic initialised (I2S STD).");
    return true;
}

bool AudioPipeline::initSpeaker() {
    _audioLib->setPinout(PIN_SPK_BCLK, PIN_SPK_LRCK, PIN_SPK_DIN);
    _audioLib->setVolume(21); 
    _speakerReady = true;
    Serial.println("[Audio] MAX98357A speaker initialised.");
    return true;
}

bool AudioPipeline::begin() {
    return initMic() && initSpeaker();
}

void AudioPipeline::stop() {
    if (_mic_rx_chan) {
        i2s_channel_disable(_mic_rx_chan);
        i2s_del_channel(_mic_rx_chan);
        _mic_rx_chan = nullptr;
    }
}

void AudioPipeline::loop() {
    if (_audioLib) _audioLib->loop();
}

bool AudioPipeline::isPlaying() {
    return _audioLib ? _audioLib->isRunning() : false;
}

void AudioPipeline::setVolume(uint8_t vol) {
    if (_audioLib) _audioLib->setVolume(vol);
}

size_t AudioPipeline::calcBufferSize(uint8_t durationSec) const {
    return (size_t)I2S_SAMPLE_RATE * sizeof(int16_t) * durationSec;
}

size_t AudioPipeline::readFrame(int16_t *frameBuffer, size_t num_samples) {
    if (!_mic_rx_chan) return 0;
    
    size_t bytesRead = 0;
    size_t toReadBytes = num_samples * sizeof(int32_t);

    static int32_t *rawBuf = nullptr;
    static size_t rawBufCapacity = 0;

    if (rawBuf == nullptr || rawBufCapacity < toReadBytes) {
        free(rawBuf);
        rawBuf = (int32_t *)malloc(toReadBytes);
        rawBufCapacity = rawBuf ? toReadBytes : 0;
    }
    if (!rawBuf) return 0;

    esp_err_t err = i2s_channel_read(_mic_rx_chan, rawBuf, toReadBytes, &bytesRead, portMAX_DELAY);
    if (err != ESP_OK) return 0;

    size_t samplesRead = bytesRead / sizeof(int32_t);
    for (size_t i = 0; i < samplesRead; i++) {
        frameBuffer[i] = (int16_t)(rawBuf[i] >> 14);
    }

    return samplesRead;
}

size_t AudioPipeline::record(int16_t *outBuffer, size_t bufSize, uint8_t durationSec) {
    if (!_mic_rx_chan) return 0;
    size_t totalRead = 0;
    size_t bytesRead = 0;
    const size_t CHUNK = 256;
    int32_t rawBuf[CHUNK];

    while (totalRead < bufSize) {
        size_t remaining16 = bufSize - totalRead;
        size_t remaining32 = remaining16 * 2;
        size_t toRead = min((size_t)(CHUNK * sizeof(int32_t)), remaining32);
        
        esp_err_t err = i2s_channel_read(_mic_rx_chan, rawBuf, toRead, &bytesRead, portMAX_DELAY);
        if (err != ESP_OK) break;

        size_t samples = bytesRead / sizeof(int32_t);
        for (size_t i = 0; i < samples && totalRead < bufSize; i++) {
            outBuffer[totalRead / sizeof(int16_t)] = (int16_t)(rawBuf[i] >> 14);
            totalRead += sizeof(int16_t);
        }
    }
    return totalRead;
}

size_t AudioPipeline::recordVAD(int16_t *outBuffer, size_t maxBufSize,
                                 uint16_t silenceTimeoutMs, uint16_t initialTimeoutMs) {
    if (!_mic_rx_chan) return 0;

    const size_t CHUNK_SAMPLES = 512;
    int16_t frameBuf[CHUNK_SAMPLES];

    size_t totalRecordedBytes = 0;
    size_t speechBytes = 0;              // hitung durasi bicara aktual
    bool   speechStarted = false;
    uint8_t consecutiveVoiced = 0;       // debounce, seperti di wake_word.cpp
    const uint8_t VOICED_FRAMES_NEEDED = 3; // ~96ms suara konsisten baru dianggap mulai bicara

    unsigned long startTime = millis();
    unsigned long lastSpeechTime = millis();

    while ((totalRecordedBytes + (CHUNK_SAMPLES * sizeof(int16_t))) <= maxBufSize) {
        size_t samplesRead = readFrame(frameBuf, CHUNK_SAMPLES);
        if (samplesRead == 0) { vTaskDelay(pdMS_TO_TICKS(1)); continue; }

        int64_t sumSquare = 0;
        for (size_t i = 0; i < samplesRead; i++) sumSquare += (int64_t)frameBuf[i] * frameBuf[i];
        float rms = sqrtf((float)sumSquare / samplesRead);

        bool voiced = rms > VAD_SILENCE_THRESHOLD;

        if (!speechStarted) {
            // Fase konfirmasi: butuh beberapa frame berturut-turut, bukan cuma 1
            if (voiced) {
                consecutiveVoiced++;
                if (consecutiveVoiced >= VOICED_FRAMES_NEEDED) {
                    speechStarted = true;
                    lastSpeechTime = millis();
                }
            } else {
                consecutiveVoiced = 0;
            }

            if (millis() - startTime > initialTimeoutMs) return 0;
            continue; // jangan simpan apapun sebelum speechStarted valid
        }

        // Sudah dalam mode rekam
        memcpy((uint8_t*)outBuffer + totalRecordedBytes, frameBuf, samplesRead * sizeof(int16_t));
        totalRecordedBytes += samplesRead * sizeof(int16_t);

        if (voiced) {
            speechBytes += samplesRead * sizeof(int16_t);
            lastSpeechTime = millis();
        }

        if (millis() - lastSpeechTime > silenceTimeoutMs) break;
    }

    // Validasi akhir: buang kalau total durasi suara terlalu pendek (ketukan/noise sesaat)
    size_t minSpeechBytes = (size_t)I2S_SAMPLE_RATE * sizeof(int16_t) * VAD_MIN_SPEECH_MS / 1000;
    if (speechBytes < minSpeechBytes) {
        Serial.printf("[VAD] Ditolak: durasi suara %zums < minimum %dms (kemungkinan noise)\n",
                      speechBytes * 1000 / (I2S_SAMPLE_RATE * sizeof(int16_t)), VAD_MIN_SPEECH_MS);
        return 0;
    }

    return totalRecordedBytes;
}

void AudioPipeline::speakText(const String &text, const char *lang) {
    if (!_speakerReady || !_audioLib) return;
    _audioLib->connecttospeech(text.c_str(), lang);
}

void AudioPipeline::playFromURL(const String &url) {
    if (!_speakerReady || !_audioLib) return;
    _audioLib->connecttohost(url.c_str());
}

void AudioPipeline::play(const int16_t *audioBuffer, size_t bufSize) {
    if (!_speakerReady) return;

    if (_audioLib) {
        _audioLib->stopSong();
        delete _audioLib; 
        _audioLib = nullptr;
    }
    delay(50);

    i2s_chan_handle_t tx_chan = nullptr;
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    if (i2s_new_channel(&chan_cfg, &tx_chan, NULL) != ESP_OK) {
        _audioLib = new Audio(I2S_NUM_1);
        _audioLib->setPinout(PIN_SPK_BCLK, PIN_SPK_LRCK, PIN_SPK_DIN);
        _audioLib->setVolume(21);
        return;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(I2S_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = (gpio_num_t)PIN_SPK_BCLK,
            .ws   = (gpio_num_t)PIN_SPK_LRCK,
            .dout = (gpio_num_t)PIN_SPK_DIN,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false }
        },
    };

    i2s_channel_init_std_mode(tx_chan, &std_cfg);
    i2s_channel_enable(tx_chan);

    size_t numSamples = bufSize / sizeof(int16_t);
    size_t written = 0;
    const size_t CHUNK_SAMPLES = 512;
    int16_t stereoChunk[CHUNK_SAMPLES * 2];

    for (size_t i = 0; i < numSamples; i += CHUNK_SAMPLES) {
        size_t samplesToProcess = min(CHUNK_SAMPLES, numSamples - i);
        for (size_t j = 0; j < samplesToProcess; j++) {
            int16_t sample = audioBuffer[i + j];
            stereoChunk[j * 2]     = sample;
            stereoChunk[j * 2 + 1] = sample;
        }
        size_t bytesToWrite = samplesToProcess * 2 * sizeof(int16_t);
        i2s_channel_write(tx_chan, stereoChunk, bytesToWrite, &written, portMAX_DELAY);
    }

    i2s_channel_disable(tx_chan);
    i2s_del_channel(tx_chan);
    delay(50);

    _audioLib = new Audio(I2S_NUM_1);
    _audioLib->setPinout(PIN_SPK_BCLK, PIN_SPK_LRCK, PIN_SPK_DIN);
    _audioLib->setVolume(19);
}

void AudioPipeline::playTone(uint16_t freqHz, uint16_t durationMs, int16_t amplitude) {
    if (freqHz == 0) return;
    size_t totalSamples = (size_t)I2S_SAMPLE_RATE * durationMs / 1000;
    int16_t *toneBuf = (int16_t *)malloc(totalSamples * sizeof(int16_t));
    if (!toneBuf) return;

    const size_t fadeSamples = I2S_SAMPLE_RATE * 10 / 1000;
    for (size_t i = 0; i < totalSamples; i++) {
        float t = (float)i / I2S_SAMPLE_RATE;
        float currentAmp = amplitude;
        if (i < fadeSamples) currentAmp = amplitude * ((float)i / fadeSamples);
        else if (i > totalSamples - fadeSamples) currentAmp = amplitude * ((float)(totalSamples - i) / fadeSamples);
        toneBuf[i] = (int16_t)(currentAmp * sinf(2.0f * PI * freqHz * t));
    }
    play(toneBuf, totalSamples * sizeof(int16_t));
    free(toneBuf);
}

void AudioPipeline::playDroneStartup() {
    playTone(1046, 120, 25000);
    delay(30);
    playTone(1318, 120, 25000);
    delay(30);
    playTone(1568, 120, 25000);
    delay(30);
    playTone(2093, 400, 32000); 
}

void audio_info(const char *info) { Serial.printf("[Audio-Lib] %s\n", info); }
void audio_eof_speech(const char *info) { Serial.printf("[Audio] TTS Selesai: %s\n", info); }