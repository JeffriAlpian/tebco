/**
 * @file ai_assistant.cpp
 * @brief Implementation of AI Assistant with Continuous Listening & Auto VAD
 */

#include "../include/ai_assistant.h"
#include "../include/config.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <base64.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

AIAssistant::AIAssistant(AudioPipeline &audio, DisplayManager &display)
    : _audio(audio), _display(display) {}

static String jsonEscape(const String &s)
{
    String out;
    out.reserve(s.length() + 8);
    for (size_t i = 0; i < s.length(); i++)
    {
        char c = s[i];
        if (c == '"')
            out += "\\\"";
        else if (c == '\\')
            out += "\\\\";
        else if (c == '\n')
            out += "\\n";
        else if (c == '\r')
            out += "\\r";
        else if (c == '\t')
            out += "\\t";
        else
            out += c;
    }
    return out;
}

void AIAssistant::handleVoiceQuery(const String &patientId, const String &userGreeting, const String &disease)
{

    bool continuousMode = true;

    while (continuousMode)
    {
        // ── 1. Cek Wi-Fi ────────────────────────────────────────────────────
        if (WiFi.status() != WL_CONNECTED)
        {
            Serial.println("[AI] Wi-Fi terputus. Menghentikan percakapan.");
            _display.showAIResponse("Wi-Fi terputus.");
            break;
        }

        // ── 2. Perekaman VAD ────────────────────────────────────────────────
        _display.setListeningMode(true);   // Cegah auto-revert ekspresi
        _display.showListeningEyes();
        Serial.println("\n[AI] Mode Mendengarkan Aktif (VAD)...");

        // Alokasikan buffer maksimal untuk 5 detik
        size_t maxBufSize = _audio.calcBufferSize(AUDIO_RECORD_SEC);
        int16_t *pcmBuf = (int16_t *)malloc(maxBufSize);
        if (!pcmBuf)
        {
            Serial.println("[AI] ERROR: PSRAM/RAM tidak cukup!");
            _display.showAIResponse("Memori penuh.");
            break;
        }

        // Rekam dengan VAD:
        // - Silence Timeout: 1000ms (Auto-stop 1 detik setelah selesai bicara)
        // - Initial Timeout: 3000ms (Standby jika tidak ada suara dalam 3 detik)
        // - tickCallback: update animasi display setiap chunk agar tidak blank
        DisplayManager &disp = _display;
        size_t recorded = _audio.recordVAD(pcmBuf, maxBufSize, 1000, 3000,
                                           [&disp]() { disp.update(); });

        // JIKA TIDAK ADA SUARA DALAM 3 DETIK PERTAMA -> KELUAR LOOP (STANDBY)
        if (recorded == 0)
        {
            free(pcmBuf);
            _display.setListeningMode(false);  // Kembalikan mode normal
            Serial.println("[AI] Tidak ada respon dalam 3 detik. Kembali ke Mode Standby.");
            _display.setExpression(FaceExpression::NEUTRAL);
            continuousMode = false;
            break;
        }

        _display.setListeningMode(false);  // VAD selesai, kembalikan mode normal

        Serial.printf("[AI] Terekam %zu byte PCM.\n", recorded);

        // ── 3. HTTP POST Request ke n8n (Binary) ────────────────────────────
        _display.showProcessingEyes();
        Serial.println("[AI] Mengirim data audio binary ke n8n Webhook...");
        
        WiFiClientSecure client;
        client.setInsecure(); // Abaikan verifikasi sertifikat SSL
        
        HTTPClient http;
        http.begin(client, AI_VOICE_QUERY_URL);
        
        // Header untuk file binary
        http.addHeader("Content-Type", "application/octet-stream");
        http.addHeader("Authorization", String("Bearer ") + AI_WEBHOOK_SECRET);
        
        // Header metadata custom (akan ditangkap oleh webhook n8n sebagai header)
        http.addHeader("X-Patient-Id", patientId);
        // Clean newlines just in case
        String cleanGreeting = userGreeting; cleanGreeting.replace("\n", " "); cleanGreeting.replace("\r", "");
        http.addHeader("X-User-Greeting", cleanGreeting);
        String cleanDisease = disease; cleanDisease.replace("\n", " "); cleanDisease.replace("\r", "");
        http.addHeader("X-Disease", cleanDisease);
        http.addHeader("X-Sample-Rate", String(I2S_SAMPLE_RATE));
        
        http.addHeader("Connection", "close");
        http.setTimeout(60000); // 60s timeout untuk LLM

        int code = http.POST((uint8_t *)pcmBuf, recorded);
        free(pcmBuf); // Bebaskan buffer PCM setelah terkirim

        if (code != HTTP_CODE_OK)
        {
            Serial.printf("[AI] HTTP Error: %d\n", code);
            _display.showAIResponse("Server Error " + String(code));
            http.end();
            break;
        }

        // ── 6. Parsing Respons Teks ─────────────────────────────────────────
        String responseBody = http.getString();
        http.end();

        if (responseBody.length() == 0)
        {
            _display.showAIResponse("Respons kosong.");
            break;
        }

        StaticJsonDocument<1024> doc;
        DeserializationError err = deserializeJson(doc, responseBody);

        String aiText;
        if (err)
        {
            if (responseBody.startsWith("RIFF") || responseBody.startsWith("ID3") || responseBody.length() > 500)
            {
                _display.showAIResponse("Error Format Audio");
                break;
            }
            else
            {
                aiText = responseBody;
            }
        }
        else
        {
            JsonObject obj = doc.is<JsonArray>() ? doc[0].as<JsonObject>() : doc.as<JsonObject>();
            if (obj.containsKey("output"))
                aiText = obj["output"].as<String>();
            else if (obj.containsKey("text"))
                aiText = obj["text"].as<String>();
            else if (obj.containsKey("response"))
                aiText = obj["response"].as<String>();
            else if (obj.containsKey("answer"))
                aiText = obj["answer"].as<String>();
        }

        responseBody = String();

        if (aiText.length() == 0)
        {
            _display.showAIResponse("Tidak ada jawaban.");
            break;
        }

        Serial.printf("[AI] Jawaban AI: %s\n", aiText.c_str());

        // ── 7. Tampilkan Teks & Ucapkan via Google TTS ─────────────────────
        _display.showAIResponse(aiText);
        Serial.println("[AI] Memutar Google TTS...");

        // Google TTS chunking (max 200 karakter per request)
        if (aiText.length() <= 200)
        {
            _audio.speakText(aiText, "id");
            while (_audio.isPlaying())
            {
                _audio.loop();
                delay(10);
            }
        }
        else
        {
            int start = 0;
            while (start < (int)aiText.length())
            {
                int end = -1;
                for (int i = start; i < min((int)aiText.length(), start + 200); i++)
                {
                    if (aiText[i] == '.' || aiText[i] == '!' || aiText[i] == '?')
                        end = i + 1;
                }
                if (end == -1)
                {
                    end = min((int)aiText.length(), start + 200);
                    for (int i = end - 1; i > start; i--)
                    {
                        if (aiText[i] == ' ')
                        {
                            end = i;
                            break;
                        }
                    }
                }

                String chunk = aiText.substring(start, end);
                chunk.trim();
                if (chunk.length() > 0)
                {
                    _audio.speakText(chunk, "id");
                    while (_audio.isPlaying())
                    {
                        _audio.loop();
                        delay(10);
                    }
                    delay(100);
                }
                start = end;
            }
        }

        // ── 8. Jeda Singkat & Loop Kembali ke Listening Mode ─────────────────
        delay(500); // naikkan dari 300ms
        // Frame flush sekarang ditangani oleh wakeWord.reset() setelah voice query selesai
            
        Serial.println("[AI] Selesai berbicara. Otomatis mendengarkan kembali...");
        // Perulangan 'while (continuousMode)' akan berulang ke atas
    }

    Serial.println("[AI] Percakapan selesai. Kembali ke Standby.");
}