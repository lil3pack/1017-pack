#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <vector>
#include <array>
#include "PluginProcessor.h"
#include "LookAndFeel1017.h"
#include "ui/ScopeDisplay.h"
#include "ui/TycoonGame.h"

// Particle struct for the knob/clip spark system
struct Particle
{
    float x { 0 }, y { 0 };
    float vx { 0 }, vy { 0 };
    float life { 1.0f };
    float size { 1.0f };
    juce::Colour colour { juce::Colours::gold };
};

class TrapHouseEditor : public juce::AudioProcessorEditor,
                        private juce::Timer
{
public:
    explicit TrapHouseEditor (TrapHouseProcessor&);
    ~TrapHouseEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void setupKnob (juce::Slider& k);

    TrapHouseProcessor& processorRef;
    LookAndFeel1017 lookAndFeel;

    // Controls — macro DRIVE + SUB GUARD
    juce::Slider driveKnob, subGuardKnob;
    juce::ComboBox characterBox, presetBox;
    juce::ToggleButton autoGainBtn { "AUTO GAIN" };
    juce::ToggleButton bypassBtn   { "BYPASS" };

    // Oscilloscope
    th::ui::ScopeDisplay scope;

    // 🎮 1017 TYCOON — pixel art trap tycoon mini-game
    th::game::TycoonGame tycoon;

public:
    th::game::TycoonGame& getTycoon() { return tycoon; }
private:

    // Meters
    float outMeter { 0.0f };

    // Animation phase (drives COMBO pulse + frame glow + anything tempo-synced)
    float wavePhase { 0.0f };

    // --- 🔥 Transformation animation state ---
    std::vector<Particle> particles;           // sparks / clip bursts
    float screenShakeX   { 0.0f };
    float screenShakeY   { 0.0f };
    float lightningFlash { 0.0f };             // 0..1, decays each frame
    int   stageTransitionTimer { 0 };          // frames remaining for level-up banner
    juce::String stageTransitionText;
    int   lastStage { 0 };                     // for detecting stage crossings
    std::array<float, 32> freqSeed;            // pseudo-rand seeds for FFT bars
    juce::Random rng;

    void updateAnimation();                    // advance state (called from timer)
    void spawnSparks (float kcxA, float kcxB, float kcy);

    // 🎮 Tycoon state sync counter (every 30 ticks = ~1 s)
    int tycoonSaveCounter { 0 };

    using APVTS = juce::AudioProcessorValueTreeState;
    std::unique_ptr<APVTS::SliderAttachment>   driveAtt, subGuardAtt;
    std::unique_ptr<APVTS::ComboBoxAttachment> characterAtt;
    std::unique_ptr<APVTS::ButtonAttachment>   autoGainAtt, bypassAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrapHouseEditor)
};
