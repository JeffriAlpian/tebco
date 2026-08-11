require('dotenv').config();
const express = require('express');
const axios = require('axios');
const FormData = require('form-data');
const { GoogleGenerativeAI } = require('@google/generative-ai');

const app = express();
const PORT = process.env.PORT || 3000;

// Initialize Gemini
const genAI = new GoogleGenerativeAI(process.env.GEMINI_API_KEY);
const model = genAI.getGenerativeModel({ model: 'gemini-1.5-flash' });

// Middleware to parse raw binary data for our webhook
app.use('/webhook/tebco-voice-query', express.raw({ 
    type: 'application/octet-stream', 
    limit: '10mb' 
}));

// Rute GET sederhana untuk mengecek status server via Browser
app.get('/', (req, res) => {
    res.send('<h1>TEBCO AI Backend is Running 🚀</h1><p>Status: OK. Ready to receive voice queries from ESP32.</p>');
});

// Helper: Create 44-byte WAV header for 16kHz, 16-bit, Mono PCM
function createWavHeader(dataLength, sampleRate) {
    const buffer = Buffer.alloc(44);
    buffer.write('RIFF', 0);
    buffer.writeUInt32LE(36 + dataLength, 4);
    buffer.write('WAVE', 8);
    buffer.write('fmt ', 12);
    buffer.writeUInt32LE(16, 16); 
    buffer.writeUInt16LE(1, 20);  
    buffer.writeUInt16LE(1, 22);  
    buffer.writeUInt32LE(sampleRate, 24); 
    buffer.writeUInt32LE(sampleRate * 2, 28); 
    buffer.writeUInt16LE(2, 32);  
    buffer.writeUInt16LE(16, 34); 
    buffer.write('data', 36);
    buffer.writeUInt32LE(dataLength, 40);
    return buffer;
}

// System prompt for Gemini
const SYSTEM_PROMPT = `Anda adalah TEBCO (Asisten Pengingat Obat AI). 
Tugas Anda menemani pasien TBC. Jawablah dengan singkat, sopan, empati, dan ramah. 
Jangan menggunakan pemformatan markdown (*, #) karena jawaban Anda akan dibaca oleh Text-to-Speech.`;

app.post('/webhook/tebco-voice-query', async (req, res) => {
    try {
        // 1. Authenticate Request
        const authHeader = req.headers['authorization'];
        if (authHeader !== `Bearer ${process.env.TEBCO_SECRET}`) {
            return res.status(401).send('Unauthorized');
        }

        // 2. Extract Metadata from Headers
        const patientId = req.headers['x-patient-id'] || 'Unknown';
        const userGreeting = req.headers['x-user-greeting'] || 'Halo';
        const disease = req.headers['x-disease'] || 'Umum';
        const sampleRate = parseInt(req.headers['x-sample-rate']) || 16000;

        console.log(`[+] New Request from Patient: ${patientId} (Disease: ${disease})`);

        // 3. Convert incoming raw PCM to WAV for Groq
        const pcmBuffer = req.body;
        if (!pcmBuffer || pcmBuffer.length === 0) {
            return res.status(400).send('No audio data received');
        }

        const wavHeader = createWavHeader(pcmBuffer.length, sampleRate);
        const wavBuffer = Buffer.concat([wavHeader, pcmBuffer]);

        // 4. Send to Groq Whisper for Speech-to-Text (STT)
        console.log('[-] Sending to Groq STT...');
        const formData = new FormData();
        formData.append('file', wavBuffer, { filename: 'audio.wav', contentType: 'audio/wav' });
        formData.append('model', 'whisper-large-v3');
        formData.append('language', 'id');

        const groqRes = await axios.post('https://api.groq.com/openai/v1/audio/transcriptions', formData, {
            headers: {
                ...formData.getHeaders(),
                'Authorization': `Bearer ${process.env.GROQ_API_KEY}`
            }
        });

        const transcript = groqRes.data.text;
        console.log(`[>] User said: "${transcript}"`);

        // 5. Send to Gemini for Logic (LLM)
        console.log('[-] Thinking with Gemini...');
        const prompt = `Pasien (ID: ${patientId}, Penyakit: ${disease}) berkata: "${transcript}". Berikan jawaban yang menenangkan dan sesuai konteks kesehatannya.`;
        
        const result = await model.generateContent({
            contents: [{ role: 'user', parts: [{ text: prompt }] }],
            systemInstruction: { role: 'system', parts: [{ text: SYSTEM_PROMPT }] }
        });
        
        let aiResponseText = result.response.text().trim();
        console.log(`[<] AI Answer: "${aiResponseText}"`);

        // 6. Text-to-Speech (Google TTS / VoiceRSS)
        console.log('[-] Generating TTS Audio...');
        
        // --- Contoh Menggunakan Google TTS Gratis (google-tts-api) ---
        // Karena ESP32 butuh audio mentah (PCM/WAV) dan API gratis Google TTS 
        // mengembalikan MP3, Anda bisa menggantinya dengan VoiceRSS jika ESP32
        // Anda belum mensupport MP3 decoding.
        // Di sini kita contohkan memanggil VoiceRSS yang mendukung pengembalian 16kHz WAV.
        
        /* 
        const ttsUrl = `http://api.voicerss.org/?key=${process.env.TTS_API_KEY}&hl=id-id&v=Budi&c=WAV&f=16khz_16bit_mono&src=${encodeURIComponent(aiResponseText)}`;
        const ttsRes = await axios.get(ttsUrl, { responseType: 'arraybuffer' });
        let finalAudioBuffer = ttsRes.data;
        */

        // MOCKUP RESPON AUDIO SEMENTARA (jika belum ada TTS_API_KEY)
        // Seharusnya finalAudioBuffer berisi byte array dari file WAV
        // let finalAudioBuffer = Buffer.from(...);
        
        // Simulasikan pengembalian respons (Untuk sekarang kembalikan teks sebagai fallback 
        // jika TTS belum diaktifkan, atau lempar error jika audio diwajibkan)
        
        // res.setHeader('Content-Type', 'audio/wav');
        // res.send(finalAudioBuffer);

        res.json({ text: aiResponseText }); // Fallback JSON text (sementara untuk testing backend)

    } catch (error) {
        console.error('[!] Error:', error?.response?.data || error.message);
        res.status(500).send('Internal Server Error');
    }
});

app.listen(PORT, () => {
    console.log(`🚀 TEBCO Backend running on http://localhost:${PORT}`);
});
