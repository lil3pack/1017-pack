#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/Clipper.h"
#include "dsp/ScopeBuffer.h"

namespace th
{
    // Parameter IDs — kept in one place so UI and DSP never drift.
    namespace PID
    {
        inline constexpr auto inputGain = "input_gain";
        inline constexpr auto ceiling   = "ceiling";
        inline constexpr auto knee      = "soft_knee";
        inline constexpr auto harmonics = "harmonics";
        inline constexpr auto character = "character";
        inline constexpr auto autoGain  = "auto_gain";
        inline constexpr auto bypass    = "bypass";
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

    const juce::String getName() const override { return "TRAP HOUSE"; }
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

    // Oscilloscope data (post-clipping)
    th::dsp::ScopeBuffer scopeBuffer;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    juce::AudioProcessorValueTreeState apvts;
    th::dsp::ClipperCore clipper;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrapHouseProcessor)
};
