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

// v4.4: expanding glow ring (replaces the old screen-shake visual)
struct GlowRing
{
    float cx, cy;
    float radius   { 10.0f };
    float maxRadius { 200.0f };
    float life      { 1.0f };
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
    void mouseDown (const juce::MouseEvent&) override; // v4.4 — secret panel trigger
    void mouseDrag (const juce::MouseEvent&) override; // v5.2 — reposition tycoon
    void mouseUp   (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void setupKnob (juce::Slider& k);

    TrapHouseProcessor& processorRef;
    LookAndFeel1017 lookAndFeel;

    // v5: macro DRIVE knob only. SUB GUARD still in APVTS for backward compat
    // but no longer shown in the UI (user can still automate it if they want).
    juce::Slider driveKnob, subGuardKnob;
    juce::ComboBox characterBox, presetBox;

    // v5 MASTER CLASS: draggable positions for DRIVE knob + TYCOON
    float driveKnobX { 240.0f };
    float driveKnobY { 380.0f };
    int   driveKnobSize { 180 };
    float tycoonX { 470.0f };
    float tycoonY { 300.0f };
    int   tycoonW { 470 };
    int   tycoonH { 200 };

    // v5.2: drag state — Tycoon window is now repositionable by grabbing its
    // top-right handle. The tycoon component fires drag callbacks that the
    // editor wires in the constructor to update tycoonX/Y + resized().
    enum class DragTarget { None, Knob, Tycoon };
    DragTarget dragTarget { DragTarget::None };
    juce::Point<float> dragOffset;
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
    std::vector<GlowRing> glowRings;           // v4.4: replaces screen shake
    float screenShakeX { 0.0f };               // retained for compat but unused in v4.4
    float screenShakeY { 0.0f };
    float lightningFlash { 0.0f };             // 0..1, decays each frame
    int   stageTransitionTimer { 0 };          // frames remaining for level-up banner
    juce::String stageTransitionText;
    int   lastStage { 0 };                     // for detecting stage crossings
    std::array<float, 32> freqSeed;            // pseudo-rand seeds for FFT bars
    juce::Random rng;

    // v4.4: VU meter peak-hold + clip LED state
    float inPeakL  { 0.0f }, inPeakR  { 0.0f };
    float outPeakL { 0.0f }, outPeakR { 0.0f };
    float clipLedFade { 0.0f }; // flashes on clip event, decays

    // v4.4: secret panel state (click title 3× to toggle)
    bool    secretPanelVisible { false };
    int     logoClickCount     { 0 };
    int64_t lastLogoClickMs    { 0 };
    float   secretPanelAlpha   { 0.0f }; // animates open/close

    // v5.2 SECRET PANEL — refactored:
    //   - MIX knob (dry/wet parallel blend)
    //   - live TRANSFER CURVE display (input -> output clipping function)
    //   - LUFS readout (short-term loudness)
    // The old stereoWidth / outputTrim knobs were removed (buggy/unmusical);
    // the corresponding APVTS params remain for backward-compatible save/load.
    juce::Slider mixKnob;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mixAtt;
    juce::Rectangle<int> transferCurveBounds;   // where to draw the live curve

    void updateAnimation();                    // advance state (called from timer)
    void spawnSparks (float kcxA, float kcxB, float kcy);

    // v5.2 — live clipper transfer curve in the MASTER LAB secret panel.
    void drawTransferCurve (juce::Graphics& g,
                             juce::Rectangle<int> bounds,
                             const LookAndFeel1017::Palette& P,
                             float alpha);

    // v5 MASTER CLASS animation state
    struct OrbitParticle { float angle; float speed; float elliptical; float offset; };
    std::array<OrbitParticle, 8> orbitParticles;
    std::vector<Particle> emberStream;         // ember particles rising from knob
    float frameFlowPhase { 0.0f };              // 0-1 traveling around the frame
    // drag support deferred — position state defaults match v5 preview layout

    // 🎮 Tycoon state sync counter (every 30 ticks = ~1 s)
    int tycoonSaveCounter { 0 };

    using APVTS = juce::AudioProcessorValueTreeState;
    std::unique_ptr<APVTS::SliderAttachment>   driveAtt, subGuardAtt;
    std::unique_ptr<APVTS::ComboBoxAttachment> characterAtt;
    std::unique_ptr<APVTS::ButtonAttachment>   autoGainAtt, bypassAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrapHouseEditor)
};
