require('dotenv').config();
const path = require('path');
const express = require('express');
const cors = require('cors');
const OpenAI = require('openai');
const { GoogleGenerativeAI } = require('@google/generative-ai');

const app = express();
const PORT = process.env.PORT || 3000;

// Izinkan akses dari browser (untuk file test.html)
app.use(cors());

// Sajikan file test.html langsung dari server ini
app.use(express.static(path.join(__dirname)));

// ── Inisialisasi Client AI ────────────────────────────────────────────────────

// Groq Client (menggunakan OpenAI SDK, cukup ganti baseURL ke Groq)
const groq = new OpenAI({
    apiKey: process.env.GROQ_API_KEY,
    baseURL: 'https://api.groq.com/openai/v1',
});

// Gemini Client
const genAI = new GoogleGenerativeAI(process.env.GEMINI_API_KEY);
const gemini = genAI.getGenerativeModel({ model: 'gemini-2.5-flash-lite' });

// ── Middleware ────────────────────────────────────────────────────────────────

// Tangkap kiriman biner (audio PCM) dari ESP32 / test.html
app.use('/webhook/tebco-voice-query', express.raw({
    type: 'application/octet-stream',
    limit: '10mb'
}));

// ── Rute Status (Cek via Browser) ────────────────────────────────────────────
app.get('/', (req, res) => {
    res.send('<h1>TEBCO AI Backend is Running 🚀</h1><p>Status: OK</p>');
});

// ── Helper: Buat 44-byte WAV Header ──────────────────────────────────────────
function createWavHeader(dataLength, sampleRate) {
    const buf = Buffer.alloc(44);
    buf.write('RIFF', 0);
    buf.writeUInt32LE(36 + dataLength, 4);
    buf.write('WAVE', 8);
    buf.write('fmt ', 12);
    buf.writeUInt32LE(16, 16);
    buf.writeUInt16LE(1, 20);   // PCM format
    buf.writeUInt16LE(1, 22);   // Mono
    buf.writeUInt32LE(sampleRate, 24);
    buf.writeUInt32LE(sampleRate * 2, 28);
    buf.writeUInt16LE(2, 32);
    buf.writeUInt16LE(16, 34);  // 16-bit
    buf.write('data', 36);
    buf.writeUInt32LE(dataLength, 40);
    return buf;
}

// ── System Prompt Gemini ──────────────────────────────────────────────────────
const SYSTEM_PROMPT = `Anda adalah TEBCO, Asisten Pengingat Obat berbasis AI. 
Tugas Anda adalah menemani dan membantu pasien. Jawablah dengan singkat, hangat, dan empati.
Jangan gunakan simbol markdown seperti * atau # karena jawaban Anda akan dibacakan oleh Text-to-Speech.`;

// ── Endpoint Utama: Menerima Audio dari ESP32 / Browser ──────────────────────
app.post('/webhook/tebco-voice-query', async (req, res) => {
    try {
        // 1. Autentikasi
        const authHeader = req.headers['authorization'];
        if (authHeader !== `Bearer ${process.env.TEBCO_SECRET}`) {
            return res.status(401).json({ error: 'Unauthorized' });
        }

        // 2. Ambil Metadata dari Headers
        const patientId   = req.headers['x-patient-id']     || 'Unknown';
        const userGreeting= req.headers['x-user-greeting']  || 'Halo';
        const disease     = req.headers['x-disease']        || 'Umum';
        const sampleRate  = parseInt(req.headers['x-sample-rate']) || 16000;

        console.log(`\n[+] Request masuk — Pasien: ${patientId} | Penyakit: ${disease}`);

        // 3. Validasi data audio
        const pcmBuffer = req.body;
        if (!pcmBuffer || pcmBuffer.length === 0) {
            return res.status(400).json({ error: 'Tidak ada data audio.' });
        }

        // 4. Sisipkan WAV Header ke data PCM mentah dari ESP32
        const wavBuffer = Buffer.concat([createWavHeader(pcmBuffer.length, sampleRate), pcmBuffer]);

        // ── LANGKAH 1: STT (Groq Whisper via OpenAI SDK) ─────────────────────
        console.log('[-] Mengirim ke Groq Whisper STT...');

        // OpenAI SDK memerlukan objek File. Kita buat dari Buffer menggunakan Blob.
        const audioFile = new File([wavBuffer], 'audio.wav', { type: 'audio/wav' });

        const transcription = await groq.audio.transcriptions.create({
            file: audioFile,
            model: 'whisper-large-v3',
            language: 'id',
        });

        const transcript = transcription.text.trim();
        console.log(`[>] Pengguna berkata: "${transcript}"`);

        if (!transcript) {
            return res.json({ text: 'Maaf, saya tidak mendengar dengan jelas. Bisa diulangi?' });
        }

        // ── LANGKAH 2: LLM (Google Gemini) ───────────────────────────────────
        console.log('[-] Berpikir dengan Gemini...');

        const prompt = `Konteks — Pasien ID: ${patientId}, Sapaan: "${userGreeting}", Penyakit: ${disease}.
Pertanyaan/Ucapan Pasien: "${transcript}"`;

        const result = await gemini.generateContent({
            contents: [{ role: 'user', parts: [{ text: prompt }] }],
            systemInstruction: { role: 'system', parts: [{ text: SYSTEM_PROMPT }] }
        });

        const aiText = result.response.text().trim();
        console.log(`[<] Jawaban AI: "${aiText}"`);

        // ── LANGKAH 3: Kembalikan Respons ─────────────────────────────────────
        // Sementara: kembalikan teks (untuk testing)
        // Nanti: ganti dengan audio WAV dari TTS
        res.json({ text: aiText });

    } catch (error) {
        const errMsg = error?.error?.message || error?.message || 'Unknown error';
        console.error(`[!] ERROR: ${errMsg}`);
        res.status(500).json({ error: errMsg });
    }
});

// ── Jalankan Server ───────────────────────────────────────────────────────────
app.listen(PORT, () => {
    console.log(`🚀 TEBCO Backend berjalan di http://localhost:${PORT}`);
    console.log(`🧪 Halaman tes tersedia di http://localhost:${PORT}/test.html`);
});
