// TRAP HOUSE — standalone DSP validation
// Reproduces the exact algorithm from 1017Pack/TrapHouse/Source/dsp/Clipper.h
// but without any JUCE dependency. Processes a test signal and writes WAVs
// so the sound can be auditioned before the full plugin build.
//
// Build:  g++ -O3 -std=c++20 clipper_standalone.cpp -o clipper_test -lm
// Run:    ./clipper_test

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include <fstream>

// ----------------------------------------------------------------------------
// Clipper core (same logic as Clipper.h in the plugin, simplified: no 4x
// oversampling. Adding oversampling doesn't change the audible signature on
// low-frequency content — it only tames aliasing in the 10-20kHz range.)
// ----------------------------------------------------------------------------

enum class Character { Hard = 0, Tape, Tube };

struct ClipperCore
{
    float inputGainDb = 0.0f;
    float ceilingDb   = -0.3f;
    float knee01      = 0.25f;
    float harmonics01 = 0.40f;
    Character character = Character::Hard;
    bool autoGain = false;

    static float dbToGain(float db) { return std::pow(10.0f, db / 20.0f); }

    void processSample(float& x) const
    {
        const float inGain  = dbToGain(inputGainDb);
        const float ceiling = std::max(dbToGain(ceilingDb), 1.0e-6f);

        x *= inGain;

        switch (character) {
            case Character::Hard: break;
            case Character::Tape: x = std::tanh(x * 1.2f) * 0.9f; break;
            case Character::Tube: x = std::tanh(x * 1.1f + 0.08f) - std::tanh(0.08f); break;
        }

        const float xn   = x / ceiling;
        const float hard = std::clamp(xn, -1.0f, 1.0f);
        const float soft = std::tanh(xn * 1.5f);
        float y = (1.0f - knee01) * hard + knee01 * soft;

        if (harmonics01 > 0.0f) {
            const float h2 = 2.0f * y * y;            // DC-free
            const float h3 = 4.0f * y * y * y - 3.0f * y;
            const float h  = 0.5f * (h2 * y) + 0.5f * h3;
            y += harmonics01 * 0.35f * h;
        }

        y *= ceiling;

        if (autoGain) {
            const float makeup = 1.0f / std::sqrt(std::max(inGain, 1.0f));
            y *= makeup;
        }

        x = y;
    }

    void processBuffer(std::vector<float>& buf) const
    {
        for (auto& s : buf) processSample(s);
    }
};

// ----------------------------------------------------------------------------
// WAV writer (16-bit mono PCM)
// ----------------------------------------------------------------------------
static void writeWav16(const std::string& path, const std::vector<float>& samples, int sr)
{
    std::ofstream f(path, std::ios::binary);
    if (!f) { std::fprintf(stderr, "cannot open %s\n", path.c_str()); return; }

    const uint32_t dataBytes = static_cast<uint32_t>(samples.size() * sizeof(int16_t));
    const uint32_t riffSize  = 36 + dataBytes;

    auto w32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto w16 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };

    f.write("RIFF", 4);                w32(riffSize);
    f.write("WAVE", 4);
    f.write("fmt ", 4);                w32(16);
    w16(1); w16(1);                    // PCM, mono
    w32(sr); w32(sr * 2);              // byte rate
    w16(2); w16(16);                   // block align, bits
    f.write("data", 4);                w32(dataBytes);

    for (float s : samples) {
        const float c = std::clamp(s, -1.0f, 1.0f);
        const int16_t i = static_cast<int16_t>(c * 32767.0f);
        f.write(reinterpret_cast<const char*>(&i), 2);
    }
}

// ----------------------------------------------------------------------------
// Signal generators
// ----------------------------------------------------------------------------
static std::vector<float> sineWave(int sr, float freq, float seconds, float amp)
{
    const int N = static_cast<int>(sr * seconds);
    std::vector<float> out(N);
    const float w = 2.0f * 3.14159265358979323846f * freq / sr;
    for (int n = 0; n < N; ++n) out[n] = amp * std::sin(w * n);
    return out;
}

static std::vector<float> kick808ish(int sr, float seconds, float amp)
{
    // Fake 808: 50Hz sine with fast pitch drop from 200Hz + amplitude decay.
    const int N = static_cast<int>(sr * seconds);
    std::vector<float> out(N);
    float phase = 0.0f;
    for (int n = 0; n < N; ++n) {
        const float t = n / (float)sr;
        const float pitch = 50.0f + 150.0f * std::exp(-t * 40.0f);
        phase += 2.0f * 3.14159265358979323846f * pitch / sr;
        const float env = std::exp(-t * 3.0f);
        out[n] = amp * env * std::sin(phase);
    }
    return out;
}

static float rms(const std::vector<float>& x)
{
    double s = 0; for (float v : x) s += v * v;
    return (float) std::sqrt(s / std::max<size_t>(1, x.size()));
}
static float peak(const std::vector<float>& x)
{
    float p = 0; for (float v : x) p = std::max(p, std::abs(v));
    return p;
}
static float peakToDb(float p)
{
    return p > 0 ? 20.0f * std::log10(p) : -120.0f;
}

// ----------------------------------------------------------------------------
int main()
{
    constexpr int sr = 48000;

    // --- Preset A: master bus glue (moderate push, HARD character) ---
    ClipperCore presetMaster;
    presetMaster.inputGainDb = 6.0f;
    presetMaster.ceilingDb   = -0.3f;
    presetMaster.knee01      = 0.25f;
    presetMaster.harmonics01 = 0.40f;
    presetMaster.character   = Character::Hard;
    presetMaster.autoGain    = false;

    // --- Preset B: 808 warmth (TUBE + big harmonics) ---
    ClipperCore preset808;
    preset808.inputGainDb = 9.0f;
    preset808.ceilingDb   = -1.0f;
    preset808.knee01      = 0.55f;
    preset808.harmonics01 = 0.70f;
    preset808.character   = Character::Tube;
    preset808.autoGain    = true;

    // --- Preset C: transparent tape lift ---
    ClipperCore presetTape;
    presetTape.inputGainDb = 3.0f;
    presetTape.ceilingDb   = -0.5f;
    presetTape.knee01      = 0.80f;
    presetTape.harmonics01 = 0.20f;
    presetTape.character   = Character::Tape;
    presetTape.autoGain    = false;

    auto runAndReport = [&](const std::string& name,
                            std::vector<float> dry,
                            const ClipperCore& preset)
    {
        auto wet = dry;
        preset.processBuffer(wet);

        std::printf("%-30s | IN  peak %6.2f dB  rms %6.2f dB\n",
                    ("[" + name + "] dry").c_str(), peakToDb(peak(dry)), peakToDb(rms(dry)));
        std::printf("%-30s | OUT peak %6.2f dB  rms %6.2f dB\n",
                    ("[" + name + "] wet").c_str(), peakToDb(peak(wet)), peakToDb(rms(wet)));

        const std::string outDir = "/sessions/funny-zen-volta/mnt/outputs/1017Pack/dsp_validation/";
        writeWav16(outDir + name + "_dry.wav", dry, sr);
        writeWav16(outDir + name + "_wet.wav", wet, sr);
    };

    // Test 1: 100Hz sine at -12dBFS (check pure clipping / harmonics on a tone)
    runAndReport("sine100_master", sineWave(sr, 100.0f, 2.0f, 0.25f), presetMaster);

    // Test 2: 808 kick (the main use-case of the "warmth" preset)
    runAndReport("kick808_warmth", kick808ish(sr, 1.0f, 0.9f), preset808);

    // Test 3: loud sine for visible clipping shape (1kHz)
    runAndReport("sine1k_hot_tape", sineWave(sr, 1000.0f, 1.5f, 0.9f), presetTape);

    // Test 4: already-hot signal (headroom test, ceiling should really catch it)
    ClipperCore preset808Copy = preset808;
    preset808Copy.autoGain = false;
    runAndReport("sine200_brickwall", sineWave(sr, 200.0f, 1.5f, 1.2f), preset808Copy);

    std::puts("\nWAVs written to: 1017Pack/dsp_validation/");
    std::puts("Drop the *_dry.wav / *_wet.wav pairs into FL Studio to A/B.");
    return 0;
}
