#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/Clipper.h"
#include "dsp/ScopeBuffer.h"

namespace th
{
    // Parameter IDs — kept in one place so UI and DSP never drift.
    namespace PID
    {
        inline constexpr auto drive       = "drive";         // macro 0-1
        inline constexpr auto subGuard    = "sub_guard";     // dual-band amount 0-1
        inline constexpr auto character   = "character";     // HARD / TAPE / TUBE / ICE
        inline constexpr auto autoGain    = "auto_gain";
        inline constexpr auto bypass      = "bypass";
        // v4.4 secret panel params (exposed for automation but hidden in main UI)
        inline constexpr auto stereoWidth = "stereo_width";  // 0=mono, 1=unchanged, 2=wide  (deprecated in v5.2)
        inline constexpr auto outputTrim  = "output_trim";   // ±12 dB post-gain            (deprecated in v5.2)
        // v5.2: MIX = parallel dry/wet blend. 0 = pure dry, 1 = pure wet (clipper).
        inline constexpr auto mix         = "mix";
    }
}

class TrapHouseProcessor : public juce::AudioProcessor
{
public:
    TrapHouseProcessor();
    ~TrapHouseProcessor() override = default;

    // --- AudioProcessor ---
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "3PACK CLIP"; }
    bool acceptsMidi()  const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // Public access so editor can build attachments
    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts; }

    // For UI meters — atomic snapshot of recent input / output peak
    std::atomic<float> inputRms  { 0.0f };
    std::atomic<float> outputRms { 0.0f };

    // v4.4: per-channel RMS for VU meters + clip-indicator
    std::atomic<float> inputRmsL  { 0.0f };
    std::atomic<float> inputRmsR  { 0.0f };
    std::atomic<float> outputRmsL { 0.0f };
    std::atomic<float> outputRmsR { 0.0f };
    std::atomic<float> gainReductionDb { 0.0f };   // 0 = no GR, negative = reducing
    std::atomic<bool>  clipEventFlag  { false };   // set true when peak hit ceiling
    // v5.2: short-term loudness estimate (LUFS-ish, K-weighted approximation)
    std::atomic<float> loudnessLufs   { -60.0f };
    // v5.2: snapshot of current knee blend for transfer-curve display
    std::atomic<float> currentKnee01  { 0.0f };

    // Oscilloscope data (post-clipping)
    th::dsp::ScopeBuffer scopeBuffer;

    // 🎮 1017 TYCOON persistent game state (survives plugin close/reopen)
    juce::ValueTree tycoonState { "TycoonState" };

    // 🎮 Game-unlocked plugin features (read by processBlock + editor)
    // ATL EMPIRE v2 building indices:
    //   0=TRAP  1=STUDIO  2=RADIO  3=LABEL_HQ  4=AIRPORT  5=MANSION
    bool isIceUnlocked() const noexcept
    {
        if (! tycoonState.isValid()) return false;
        return (int) tycoonState.getProperty ("count_3", 0) > 0; // LABEL HQ
    }
    bool isPrestigeActive() const noexcept
    {
        if (! tycoonState.isValid()) return false;
        return (int) tycoonState.getProperty ("prestige", 0) > 0;
    }
    // New: signed artists unlocks advanced auto-gain behaviour
    int getSignedArtistCount() const noexcept
    {
        if (! tycoonState.isValid()) return 0;
        const int mask = (int) tycoonState.getProperty ("signedArtistMask", 0);
        int n = 0;
        for (int i = 0; i < 8; ++i) if (mask & (1 << i)) ++n;
        return n;
    }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    juce::AudioProcessorValueTreeState apvts;
    th::dsp::ClipperCore clipper;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrapHouseProcessor)
};
