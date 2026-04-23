#include "PluginEditor.h"
#include <array>
#include <algorithm>
#include <cmath>

//==============================================================================
static const LookAndFeel1017::Palette& pal (LookAndFeel1017& lnf) { return lnf.getPalette(); }

//==============================================================================
// 🎮 Pixel sprites — 5×5 and 12×12 patterns
namespace sprites
{
    using P5 = std::array<std::array<int, 5>, 5>;
    using P6 = std::array<std::array<int, 6>, 6>;
    using P12 = std::array<std::array<int, 12>, 12>;

    static const P5 STAR  = {{ {0,0,1,0,0},{0,1,1,1,0},{1,1,1,1,1},{0,1,1,1,0},{0,0,1,0,0} }};
    static const P5 HEART = {{ {0,1,0,1,0},{1,1,1,1,1},{1,1,1,1,1},{0,1,1,1,0},{0,0,1,0,0} }};
    static const P5 COIN  = {{ {0,1,1,1,0},{1,1,0,1,1},{1,0,1,0,1},{1,1,0,1,1},{0,1,1,1,0} }};
    static const P6 EYE   = {{ {0,1,1,1,1,0},{1,1,1,1,1,1},{1,1,0,0,1,1},{1,1,0,0,1,1},{1,1,1,1,1,1},{0,1,1,1,1,0} }};
    static const P12 MASCOT = {{
        {0,0,0,0,1,1,1,1,0,0,0,0},{0,0,0,1,1,0,0,1,1,0,0,0},
        {0,0,0,1,0,0,0,0,1,0,0,0},{0,0,1,1,1,1,1,1,1,1,0,0},
        {0,1,1,1,1,1,1,1,1,1,1,0},{0,1,1,0,1,1,1,1,0,1,1,0},
        {0,1,1,0,1,1,1,1,0,1,1,0},{0,1,1,1,1,0,0,1,1,1,1,0},
        {0,0,1,1,1,1,1,1,1,1,0,0},{0,0,1,0,1,1,1,1,0,1,0,0},
        {0,0,0,0,0,1,1,0,0,0,0,0},{0,0,0,0,0,1,1,0,0,0,0,0},
    }};

    template <typename Pat>
    static void draw (juce::Graphics& g, const Pat& p, float x, float y, float ps, juce::Colour c)
    {
        g.setColour (c);
        for (int r = 0; r < (int) p.size(); ++r)
            for (int c2 = 0; c2 < (int) p[r].size(); ++c2)
                if (p[r][c2])
                    g.fillRect (x + c2 * ps, y + r * ps, ps + 0.3f, ps + 0.3f);
    }
}

//==============================================================================
// v4.4: Vertical VU meter with peak-hold line + gradient fill
static void drawVuMeter (juce::Graphics& g, juce::Rectangle<float> bounds,
                          float rms, float peakHold,
                          const LookAndFeel1017::Palette& P,
                          const juce::String& label)
{
    // Background slot
    g.setColour (P.bgDeep);
    g.fillRoundedRectangle (bounds, 2.0f);
    g.setColour (P.gold.withAlpha (0.35f));
    g.drawRoundedRectangle (bounds, 2.0f, 0.8f);

    // Convert RMS (linear) → dB [-60, 0] → normalized [0, 1]
    auto dbNorm = [] (float lin)
    {
        if (lin <= 1.0e-5f) return 0.0f;
        const float db = 20.0f * std::log10 (lin);
        return juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
    };

    const float levelNorm = dbNorm (rms);
    const float peakNorm  = dbNorm (peakHold);

    if (levelNorm > 0.02f)
    {
        const float fillH = bounds.getHeight() * levelNorm;
        const auto fill = juce::Rectangle<float> (bounds.getX() + 1.0f,
                                                    bounds.getBottom() - fillH,
                                                    bounds.getWidth() - 2.0f,
                                                    fillH);
        juce::ColourGradient grad (juce::Colour (0xFF2DCC70), bounds.getCentreX(), bounds.getBottom(),
                                    juce::Colour (0xFFE74C3C), bounds.getCentreX(), bounds.getY(), false);
        grad.addColour (0.55, juce::Colour (0xFFF4D03F));
        grad.addColour (0.80, juce::Colour (0xFFFFA726));
        g.setGradientFill (grad);
        g.fillRoundedRectangle (fill, 1.5f);
    }

    // Peak-hold line
    if (peakNorm > 0.02f)
    {
        const float peakY = bounds.getBottom() - bounds.getHeight() * peakNorm;
        g.setColour (juce::Colours::white.withAlpha (0.9f));
        g.drawHorizontalLine ((int) peakY,
                              bounds.getX() + 1.0f,
                              bounds.getRight() - 1.0f);
    }

    // Label below (small)
    if (label.isNotEmpty())
    {
        g.setColour (P.cream.withAlpha (0.6f));
        g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 7.5f, juce::Font::bold));
        g.drawText (label,
                    bounds.translated (0.0f, bounds.getHeight() + 2.0f).withHeight (9.0f).toNearestInt(),
                    juce::Justification::centred);
    }
}

//==============================================================================
// v4.4: Clip LED — flashes red when audio hits ceiling
static void drawClipLed (juce::Graphics& g, juce::Rectangle<float> b,
                          float fade, const LookAndFeel1017::Palette& P)
{
    // Idle: dim dark red. Triggered: bright red glow.
    const juce::Colour ledCol = juce::Colour (0xFFFF2020).withAlpha (0.25f + fade * 0.75f);
    g.setColour (P.bgDeep);
    g.fillEllipse (b.expanded (1.0f));
    g.setColour (ledCol);
    g.fillEllipse (b);
    if (fade > 0.1f)
    {
        g.setColour (juce::Colour (0xFFFF2020).withAlpha (fade * 0.3f));
        g.fillEllipse (b.expanded (fade * 4.0f));
    }
}

//==============================================================================
// v4.4: GR meter — horizontal bar, fills left→right based on gain reduction (dB)
static void drawGrMeter (juce::Graphics& g, juce::Rectangle<float> bounds,
                          float grDb, const LookAndFeel1017::Palette& P)
{
    g.setColour (P.bgDeep);
    g.fillRoundedRectangle (bounds, 2.0f);
    g.setColour (P.gold.withAlpha (0.3f));
    g.drawRoundedRectangle (bounds, 2.0f, 0.8f);

    // GR: 0 = no reduction, -12 dB = max shown. Map to 0..1
    const float grAbs = juce::jlimit (0.0f, 12.0f, -grDb);
    const float norm  = grAbs / 12.0f;
    if (norm > 0.01f)
    {
        const float fillW = bounds.getWidth() * norm;
        const auto fill = juce::Rectangle<float> (bounds.getX() + 1.0f, bounds.getY() + 1.0f,
                                                    fillW - 2.0f, bounds.getHeight() - 2.0f);
        juce::ColourGradient grad (juce::Colour (0xFFF4D03F), bounds.getX(), bounds.getCentreY(),
                                    juce::Colour (0xFFE74C3C), bounds.getRight(), bounds.getCentreY(), false);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (fill, 1.5f);
    }
    g.setColour (P.cream.withAlpha (0.7f));
    g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 7.5f, juce::Font::bold));
    g.drawText ("GR " + juce::String (grDb, 1) + " dB",
                bounds.toNearestInt(),
                juce::Justification::centred);
}

//==============================================================================
// LED power bar under knobs — 10 segments gold → orange → red
static void drawLedPowerBar (juce::Graphics& g, juce::Rectangle<float> bounds,
                             float value01, const LookAndFeel1017::Palette& P)
{
    constexpr int N = 10; constexpr float gap = 2.0f;
    const float segW = (bounds.getWidth() - gap * (N - 1)) / N;
    const int active = (int) std::ceil (juce::jlimit (0.0f, 1.0f, value01) * N);
    for (int i = 0; i < N; ++i)
    {
        const auto r = juce::Rectangle<float> (bounds.getX() + i * (segW + gap),
                                                bounds.getY(), segW, bounds.getHeight());
        const bool on = i < active;
        const juce::Colour fill = on ? (i < 6 ? P.gold
                                       : i < 8 ? juce::Colour (0xFFFFA726)
                                               : juce::Colour (0xFFE53935))
                                     : P.bgDeep;
        g.setColour (fill.withAlpha (on ? 1.0f : 0.3f));
        g.fillRoundedRectangle (r, 1.5f);
        if (on)
        {
            g.setColour (fill.brighter (0.35f).withAlpha (0.55f));
            g.fillRoundedRectangle (r.withTrimmedLeft (1).withTrimmedRight (1)
                                      .withHeight (std::max (1.0f, bounds.getHeight() / 3.0f))
                                      .translated (0.0f, 1.0f), 1.0f);
        }
        g.setColour (P.purpleLean.withAlpha (on ? 0.4f : 0.55f));
        g.drawRoundedRectangle (r, 1.5f, 0.8f);
    }
}

//==============================================================================
// COMBO indicator
static void drawCombo (juce::Graphics& g, int cx, int cy, float drive01,
                       const LookAndFeel1017::Palette& P, float phase)
{
    if (drive01 < 0.5f) return;
    juce::String text; juce::Colour c;
    if      (drive01 >= 0.9f)  { text = "MAX DRIVE !"; c = P.purpleHi; }
    else if (drive01 >= 0.75f) { text = "x3 COMBO";    c = juce::Colour (0xFFFFA726); }
    else                       { text = "x2 COMBO";    c = P.goldHi; }
    const float pulse = 1.0f + 0.08f * std::sin (phase * 3.0f);
    juce::Graphics::ScopedSaveState s (g);
    g.addTransform (juce::AffineTransform::scale (pulse).translated ((float) cx, (float) cy));
    const juce::Rectangle<float> box (-52.0f, -10.0f, 104.0f, 18.0f);
    g.setColour (P.bgDeep.withAlpha (0.85f)); g.fillRoundedRectangle (box, 2.0f);
    g.setColour (c); g.drawRoundedRectangle (box, 2.0f, 1.5f);
    g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::bold));
    g.drawText (text, box, juce::Justification::centred, false);
}

//==============================================================================
// 🌈 Chromatic aberration title — draws 3 offset copies at high DRIVE
static void drawChromaticTitle (juce::Graphics& g, const juce::String& text,
                                juce::Rectangle<int> area, float fontSize,
                                const LookAndFeel1017::Palette& P, float drive01)
{
    const float chroma = drive01 > 0.7f ? (drive01 - 0.7f) * 6.0f : 0.0f;
    g.setFont (juce::Font (fontSize, juce::Font::bold));

    if (chroma > 0.0f)
    {
        // Cyan/red split drawn behind the main title
        g.setColour (P.purpleHi.withAlpha (0.55f));
        g.drawFittedText (text, area.translated ((int) chroma, 0),
                          juce::Justification::centred, 1);
        g.setColour (P.goldHi.withAlpha (0.55f));
        g.drawFittedText (text, area.translated ((int) -chroma, 0),
                          juce::Justification::centred, 1);
    }
    // Main title
    g.setColour (P.gold);
    g.drawFittedText (text, area, juce::Justification::centred, 1);
}

//==============================================================================
// 🔥 Procedural flames at the bottom edge (above DRIVE 40%)
static void drawFlames (juce::Graphics& g, int width, int height, float drive01,
                        const LookAndFeel1017::Palette& P, float phase)
{
    if (drive01 < 0.4f) return;
    const float I = std::min (1.0f, (drive01 - 0.4f) / 0.55f);
    constexpr int N = 20;
    for (int i = 0; i < N; ++i)
    {
        const float baseX = (width / (float) N) * (i + 0.5f);
        const float fx = baseX + std::sin (phase * 1.5f + i * 0.8f) * 12.0f;
        const float flameH = 25.0f + I * (60.0f + std::sin (phase * 3.0f + i) * 25.0f);
        const float W = 18.0f + std::sin (phase + i) * 4.0f;
        const float yBase = (float) height;

        // Outer wide red flame
        juce::Path outer;
        outer.startNewSubPath (fx - W/2, yBase);
        outer.quadraticTo (fx - W * 0.4f, yBase - flameH * 0.3f, fx - W * 0.2f, yBase - flameH * 0.6f);
        outer.quadraticTo (fx,            yBase - flameH,         fx + W * 0.2f, yBase - flameH * 0.6f);
        outer.quadraticTo (fx + W * 0.4f, yBase - flameH * 0.3f,  fx + W/2,       yBase);
        outer.closeSubPath();
        g.setColour (P.purpleLean.withAlpha (0.55f * I));
        g.fillPath (outer);

        // Inner yellow/gold flame
        juce::Path inner;
        const float h2 = flameH * 0.7f;
        inner.startNewSubPath (fx - W * 0.3f, yBase);
        inner.quadraticTo (fx - W * 0.15f, yBase - h2 * 0.5f, fx, yBase - h2);
        inner.quadraticTo (fx + W * 0.15f, yBase - h2 * 0.5f, fx + W * 0.3f, yBase);
        inner.closeSubPath();
        g.setColour (P.gold.withAlpha (0.65f * I));
        g.fillPath (inner);

        // Core bright
        const float coreH = flameH * 0.4f;
        g.setColour (P.goldHi.withAlpha (0.9f * I));
        g.fillEllipse (fx - W * 0.15f, yBase - coreH, W * 0.3f, coreH);
    }
}

//==============================================================================
// 🌟 Knob aura — halo that pulses and shifts color with value
static void drawKnobAura (juce::Graphics& g, float cx, float cy, float size,
                          float value, const LookAndFeel1017::Palette& P, float phase)
{
    if (value < 0.3f) return;
    const float I = std::min (1.0f, (value - 0.3f) / 0.7f);
    const float pulse = 1.0f + 0.15f * std::sin (phase * 3.0f);
    const float auraR = (size / 2.0f) * (1.15f + I * 0.4f * pulse);
    const juce::Colour c = value > 0.8f ? P.purpleHi
                         : value > 0.5f ? juce::Colour (0xFFFFA726)
                                        : P.goldHi;
    juce::ColourGradient grad (c.withAlpha (0.4f * I * pulse), cx, cy,
                                c.withAlpha (0.0f),
                                cx + auraR, cy, true);
    grad.addColour (0.5, c.withAlpha (0.2f * I));
    g.setGradientFill (grad);
    g.fillEllipse (cx - auraR, cy - auraR, auraR * 2.0f, auraR * 2.0f);
}

//==============================================================================
// 🎵 FFT-style frequency bars behind the scope (pseudo-animated)
static void drawFreqBars (juce::Graphics& g, juce::Rectangle<int> scopeBounds,
                           float drive01, const LookAndFeel1017::Palette& P,
                           const std::array<float, 32>& seed, float phase)
{
    if (drive01 < 0.35f) return;
    const float I = (drive01 - 0.35f) / 0.65f;
    constexpr int N = 32;
    const float barW = (scopeBounds.getWidth() - 20) / (float) N;
    const float midY = scopeBounds.getCentreY();
    const float maxH = scopeBounds.getHeight() * 0.42f;

    for (int i = 0; i < N; ++i)
    {
        const float bx = scopeBounds.getX() + 10 + i * barW;
        const float v = std::abs (std::sin (phase * 1.8f * (0.5f + seed[(size_t) i]) + i * 0.9f)
                                 * std::cos (phase * 1.2f + i * 0.4f));
        const float h = v * maxH * I * (0.5f + drive01 * 0.5f);
        const float t = i / (float) N;
        const juce::Colour c = t < 0.5f
            ? P.gold.interpolatedWith (P.purpleLean, t * 2.0f)
            : P.purpleLean.interpolatedWith (P.purpleHi, (t - 0.5f) * 2.0f);

        g.setColour (c.withAlpha (0.4f * I));
        g.fillRect (bx + 1, midY - h, barW - 2, h);
        g.fillRect (bx + 1, midY,     barW - 2, h);
        // Gold cap
        g.setColour (P.goldHi.withAlpha (0.6f * I));
        g.fillRect (bx + 1, midY - h - 1, barW - 2, 2.0f);
        g.fillRect (bx + 1, midY + h - 1, barW - 2, 2.0f);
    }
}

//==============================================================================
// ⚡ Demonic eyes at MAX DRIVE
static void drawDemonEyes (juce::Graphics& g, int width, float drive01,
                           const LookAndFeel1017::Palette& P, float phase)
{
    if (drive01 < 0.9f) return;
    const float I = (drive01 - 0.9f) / 0.1f;
    const float blink = std::abs (std::sin (phase * 0.3f));
    const float alpha = juce::jlimit (0.0f, 1.0f, I * blink);
    const juce::Colour red (0xFFFF3DA5);
    g.setColour (red.withAlpha (alpha));
    sprites::draw (g, sprites::EYE, 40.0f,                   30.0f, 2.0f + I, red.withAlpha (alpha));
    sprites::draw (g, sprites::EYE, (float) width - 60.0f,   30.0f, 2.0f + I, red.withAlpha (alpha));
}

//==============================================================================
// 📢 Level-up banner (stage transitions)
static void drawLevelUp (juce::Graphics& g, int width, int height,
                         int timer, const juce::String& text,
                         const LookAndFeel1017::Palette& P)
{
    if (timer <= 0) return;
    const float alpha = std::min (1.0f, timer / 30.0f);
    const float scale = 1.0f + (1.0f - alpha) * 0.5f;
    juce::Graphics::ScopedSaveState s (g);
    g.addTransform (juce::AffineTransform::scale (scale)
                        .translated (width / 2.0f, height / 2.0f - 40.0f));
    const juce::Rectangle<float> box (-120.0f, -20.0f, 240.0f, 40.0f);
    g.setColour (P.bgDeep.withAlpha (0.9f * alpha)); g.fillRoundedRectangle (box, 6.0f);
    g.setColour (P.goldHi.withAlpha (alpha));        g.drawRoundedRectangle (box, 6.0f, 2.0f);
    g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 18.0f, juce::Font::bold));
    g.drawText (text, box, juce::Justification::centred, false);
}

//==============================================================================
static int getStage (float drive) {
    return drive < 0.20f ? 0 : drive < 0.40f ? 1 : drive < 0.65f ? 2 : drive < 0.85f ? 3 : 4;
}

//==============================================================================
void TrapHouseEditor::setupKnob (juce::Slider& k)
{
    k.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    k.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                           juce::MathConstants<float>::pi * 2.75f, true);
    k.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    k.setVelocityBasedMode (true);
    k.setVelocityModeParameters (0.7, 1, 0.08, false);
    k.setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    k.setPopupDisplayEnabled (true, true, nullptr, 1000);
    addAndMakeVisible (k);
}

//==============================================================================
TrapHouseEditor::TrapHouseEditor (TrapHouseProcessor& p)
    : AudioProcessorEditor (&p),
      processorRef (p),
      scope (p.scopeBuffer, lookAndFeel)
{
    setLookAndFeel (&lookAndFeel);
    // v4.3: wider layout (960×560) — properly-aligned knobs on the left,
    // TYCOON game on the right, top-right hosts preset+character+toggles.
    setSize (960, 560);

    auto& apvts = processorRef.getAPVTS();

    addAndMakeVisible (scope);

    // 🎮 Tycoon game — load saved state from processor's tycoonState ValueTree
    tycoon.loadSaveState (p.tycoonState);
    addAndMakeVisible (tycoon);

    setupKnob (driveKnob);
    setupKnob (subGuardKnob);
    subGuardKnob.setVisible (false); // v5: hidden — SUB GUARD no longer in main UI
    driveKnob   .setDoubleClickReturnValue (true, 0.35);
    subGuardKnob.setDoubleClickReturnValue (true, 0.00);

    // v5 MASTER CLASS: init 8 orbital particles around the knob
    for (int i = 0; i < 8; ++i)
        orbitParticles[(size_t) i] = {
            (float) i / 8.0f * juce::MathConstants<float>::twoPi,
            0.5f + rng.nextFloat() * 0.3f,
            0.7f + rng.nextFloat() * 0.3f,
            rng.nextFloat() * juce::MathConstants<float>::pi
        };

    characterBox.addItemList ({ "HARD", "TAPE", "TUBE", "ICE" }, 1);
    addAndMakeVisible (characterBox);

    presetBox.addItem ("PRESET", 1);
    presetBox.setSelectedId (1, juce::dontSendNotification);
    addAndMakeVisible (presetBox);

    addAndMakeVisible (autoGainBtn);
    addAndMakeVisible (bypassBtn);

    driveAtt     = std::make_unique<APVTS::SliderAttachment>   (apvts, th::PID::drive,     driveKnob);
    subGuardAtt  = std::make_unique<APVTS::SliderAttachment>   (apvts, th::PID::subGuard,  subGuardKnob);
    characterAtt = std::make_unique<APVTS::ComboBoxAttachment> (apvts, th::PID::character, characterBox);
    autoGainAtt  = std::make_unique<APVTS::ButtonAttachment>   (apvts, th::PID::autoGain,  autoGainBtn);
    bypassAtt    = std::make_unique<APVTS::ButtonAttachment>   (apvts, th::PID::bypass,    bypassBtn);

    // v4.4 Secret panel sliders (hidden by default, toggle via 3-click on title)
    setupKnob (stereoWidthKnob);
    setupKnob (outputTrimKnob);
    stereoWidthKnob.setDoubleClickReturnValue (true, 1.0);
    outputTrimKnob .setDoubleClickReturnValue (true, 0.0);
    stereoWidthAtt = std::make_unique<APVTS::SliderAttachment> (apvts, th::PID::stereoWidth, stereoWidthKnob);
    outputTrimAtt  = std::make_unique<APVTS::SliderAttachment> (apvts, th::PID::outputTrim,  outputTrimKnob);
    stereoWidthKnob.setVisible (false);
    outputTrimKnob .setVisible (false);

    // Init animation state
    for (auto& s : freqSeed) s = rng.nextFloat();
    particles.reserve (256);
    glowRings.reserve (16);

    startTimerHz (30);
}

TrapHouseEditor::~TrapHouseEditor()
{
    stopTimer();
    // 🎮 Save tycoon game state back to processor (persists across editor reopens)
    processorRef.tycoonState = tycoon.getSaveState();
    setLookAndFeel (nullptr);
}

//==============================================================================
void TrapHouseEditor::spawnSparks (float kcxA, float kcxB, float kcy)
{
    const float drive = processorRef.getAPVTS().getRawParameterValue (th::PID::drive)->load();
    if (drive < 0.05f) return;
    const int rate = (int) std::floor (drive * 3.0f);
    for (int i = 0; i < rate; ++i)
    {
        if (rng.nextFloat() > drive * 0.9f) continue;
        const auto& P = pal (lookAndFeel);
        const bool fromA = rng.nextBool();
        const float cx = fromA ? kcxA : kcxB;
        const juce::Colour c = drive > 0.8f ? P.purpleHi
                             : drive > 0.5f ? P.goldHi
                                            : P.gold;
        particles.push_back ({
            cx + (rng.nextFloat() - 0.5f) * 40.0f,
            kcy + (rng.nextFloat() - 0.5f) * 20.0f,
            (rng.nextFloat() - 0.5f) * 2.0f,
            -rng.nextFloat() * (2.0f + drive * 4.0f) - 1.0f,
            1.0f,
            1.0f + rng.nextFloat() * 3.0f,
            c
        });
    }
    // Cap
    if (particles.size() > 220)
        particles.erase (particles.begin(),
                         particles.begin() + (particles.size() - 220));
}

void TrapHouseEditor::updateAnimation()
{
    wavePhase += 0.08f;
    const float drive = processorRef.getAPVTS().getRawParameterValue (th::PID::drive)->load();

    // Stage transition detection
    const int stage = getStage (drive);
    if (stage > lastStage)
    {
        stageTransitionTimer = 45;
        stageTransitionText = stage == 1 ? "WARMING UP"
                            : stage == 2 ? "COMBO START"
                            : stage == 3 ? "ON FIRE"
                                         : "MAX DRIVE !!";
    }
    lastStage = stage;
    if (stageTransitionTimer > 0) --stageTransitionTimer;

    // Lightning flashes at MAX
    if (drive > 0.85f && rng.nextFloat() < 0.05f) lightningFlash = 1.0f;
    lightningFlash *= 0.85f;

    // v4.4: glow rings replace screen shake. Spawn rings from knob centers at
    // high DRIVE — they expand outward and fade. Visually "explosive" but not
    // jerky (no element moves out of its grid position).
    if (drive > 0.80f && rng.nextFloat() < 0.08f)
    {
        const int knobSize = 160;
        const int knobY    = 300;
        const float cx = (rng.nextBool() ? 60.0f : 260.0f) + knobSize * 0.5f;
        const float cy = (float) knobY + knobSize * 0.5f;
        juce::Colour col = drive > 0.95f ? juce::Colour (0xFFFF3DA5)
                         : drive > 0.88f ? juce::Colour (0xFFFF5252)
                                          : juce::Colour (0xFFFFEE58);
        glowRings.push_back ({ cx, cy, 20.0f, 180.0f, 1.0f, col });
    }
    // Update / prune
    for (auto& r : glowRings)
    {
        r.radius += (r.maxRadius - r.radius) * 0.08f;
        r.life   *= 0.94f;
    }
    glowRings.erase (std::remove_if (glowRings.begin(), glowRings.end(),
                        [] (const GlowRing& r) { return r.life < 0.02f; }),
                      glowRings.end());

    // v5 MASTER CLASS: advance orbital particles (elliptical orbits around knob)
    for (auto& op : orbitParticles)
        op.angle += op.speed * 0.03f * (1.0f + drive);

    // v5: spawn ember stream from knob at DRIVE > 50%
    if (drive > 0.5f && rng.nextFloat() < 0.3f)
    {
        const auto& P = pal (lookAndFeel);
        emberStream.push_back ({
            driveKnobX + (rng.nextFloat() - 0.5f) * driveKnobSize * 0.4f,
            driveKnobY - driveKnobSize * 0.3f,
            (rng.nextFloat() - 0.5f) * 0.8f,
            -1.0f - rng.nextFloat() * 2.0f * drive,
            1.0f,
            2.0f + rng.nextFloat() * 3.0f,
            drive > 0.85f ? P.purpleHi : P.goldHi
        });
    }
    // Update + prune emberStream
    for (auto& e : emberStream)
    {
        e.x += e.vx;
        e.y += e.vy;
        e.vy += 0.01f; // light gravity
        e.life -= 0.015f;
    }
    emberStream.erase (std::remove_if (emberStream.begin(), emberStream.end(),
                        [] (const Particle& e) { return e.life <= 0.02f || e.y < -20.0f; }),
                      emberStream.end());
    if (emberStream.size() > 100)
        emberStream.erase (emberStream.begin(), emberStream.begin() + (emberStream.size() - 100));

    // v5: frame flow phase advance
    frameFlowPhase += 0.005f + drive * 0.01f;
    if (frameFlowPhase > 1.0f) frameFlowPhase -= 1.0f;

    // v4.4: VU peak-hold decay + clip LED fade
    inPeakL  = juce::jmax (inPeakL  * 0.95f, processorRef.inputRmsL.load());
    inPeakR  = juce::jmax (inPeakR  * 0.95f, processorRef.inputRmsR.load());
    outPeakL = juce::jmax (outPeakL * 0.95f, processorRef.outputRmsL.load());
    outPeakR = juce::jmax (outPeakR * 0.95f, processorRef.outputRmsR.load());
    if (processorRef.clipEventFlag.exchange (false)) clipLedFade = 1.0f;
    clipLedFade *= 0.88f;

    // v4.4: secret panel alpha smoothing
    const float targetAlpha = secretPanelVisible ? 1.0f : 0.0f;
    secretPanelAlpha += (targetAlpha - secretPanelAlpha) * 0.18f;

    // Knob positions (must match resized())
    const int knobSize = 150;
    const int knobY    = 295;
    const int avail    = getWidth() - 120;
    const int gap      = (avail - 2 * knobSize) / 3;
    const float kcxA = gap + knobSize * 0.5f;
    const float kcxB = gap * 2 + knobSize + knobSize * 0.5f;
    const float kcy  = knobY + knobSize * 0.5f;

    spawnSparks (kcxA, kcxB, kcy);

    // Update particles (physics + life decay)
    for (auto& p : particles)
    {
        p.x += p.vx; p.y += p.vy;
        p.vy += 0.08f; p.vx *= 0.99f;
        p.life -= 0.015f + drive * 0.01f;
    }
    // Remove dead
    particles.erase (std::remove_if (particles.begin(), particles.end(),
                        [] (const Particle& p) { return p.life <= 0.0f || p.y > 560 || p.y < -10; }),
                     particles.end());
}

//==============================================================================
void TrapHouseEditor::paint (juce::Graphics& g)
{
    const auto& P = pal (lookAndFeel);
    const float drive01 = processorRef.getAPVTS().getRawParameterValue (th::PID::drive)->load();
    const float phase = wavePhase;
    // v5: master-class intensity curve — 0 below 50% DRIVE, 1 at 100%.
    const float driveHot = juce::jmax (0.0f, (drive01 - 0.5f) / 0.5f);

    // v4.4: NO MORE screen shake (too chaotic). Glow rings + intensified
    // frame pulse + chromatic aberration carry the "MAX DRIVE" wow effect instead.

    // ---- Background: red glass gradient (gets hotter at high drive) ----
    const juce::Colour bgD = drive01 > 0.7f ? P.bgDeep.interpolatedWith (P.purpleLean, (drive01 - 0.7f) * 0.4f) : P.bgDeep;
    const juce::Colour bgM = drive01 > 0.7f ? P.bgMid.interpolatedWith  (P.purpleLean, (drive01 - 0.7f) * 0.25f) : P.bgMid;
    juce::ColourGradient bg (bgD, 0.0f, 0.0f, bgM, (float) getWidth(), (float) getHeight(), false);
    g.setGradientFill (bg);
    g.fillAll();

    // v5: REMOVED procedural flames (too chaotic for master class vibe).
    // Replaced by the cleaner nebula background below.
    //
    // Layer A: AMBIENT NEBULA — two rotating radial gradients blending on bg.
    // Subtle at low drive, becomes visible at 50%+.
    if (driveHot > 0.15f)
    {
        const float a = driveHot * 0.30f;
        const float r1 = 0.35f + 0.15f * std::sin (phase * 0.15f);
        const float r2 = 0.65f + 0.20f * std::cos (-phase * 0.10f);
        juce::ColourGradient neb1 (
            P.purpleHi.withAlpha (a), getWidth() * r1,          getHeight() * 0.4f,
            P.bgDeep.withAlpha (0.0f), getWidth() * r1 + 380.0f, getHeight() * 0.4f, true);
        neb1.addColour (0.6, P.purpleLean.withAlpha (a * 0.4f));
        g.setGradientFill (neb1);
        g.fillRect (getLocalBounds());
        juce::ColourGradient neb2 (
            P.gold.withAlpha (a * 0.6f), getWidth() * r2,          getHeight() * 0.6f,
            P.bgDeep.withAlpha (0.0f),   getWidth() * r2 + 320.0f, getHeight() * 0.6f, true);
        neb2.addColour (0.7, P.purpleLean.withAlpha (a * 0.2f));
        g.setGradientFill (neb2);
        g.fillRect (getLocalBounds());
    }

    // ---- Thin gold frame (pulses with DRIVE) ----
    const float frameAlpha = juce::jlimit (0.0f, 1.0f,
        0.35f + drive01 * 0.35f + std::sin (phase * 2.0f) * 0.07f * drive01);
    g.setColour (P.gold.withAlpha (frameAlpha));
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (8.0f), 14.0f,
                            1.2f + drive01 * 1.8f);

    // ---- Pixel decorations (corners) ----
    sprites::draw (g, sprites::STAR,  22.0f,  22.0f,                       3.0f, P.goldHi);
    sprites::draw (g, sprites::STAR,  (float) getWidth() - 36.0f, 22.0f,   3.0f, P.goldHi);

    // 3 LIVES hearts top-left (under title + subtitle — y=96)
    const juce::Colour heartRed (0xFFFF5252);
    sprites::draw (g, sprites::HEART, 22.0f, 96.0f, 2.0f, heartRed);
    sprites::draw (g, sprites::HEART, 37.0f, 96.0f, 2.0f, heartRed);
    sprites::draw (g, sprites::HEART, 52.0f, 96.0f, 2.0f, heartRed);

    // Pixel mascot (skull) tucked bottom-left above footer
    sprites::draw (g, sprites::MASCOT, 29.0f, (float) getHeight() - 54.0f, 2.0f, P.bgDeep);
    sprites::draw (g, sprites::MASCOT, 28.0f, (float) getHeight() - 55.0f, 2.0f, P.goldHi);

    // v5: Clean gold title (demon eyes / chromatic removed — kept master-class clean).
    // Title descended a bit — more air at the top.
    g.setColour (P.gold);
    g.setFont (juce::Font (42.0f, juce::Font::bold));
    g.drawFittedText ("3PACK CLIP", 0, 22, getWidth(), 46, juce::Justification::centred, 1);

    g.setColour (P.cream.withAlpha (0.85f));
    g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::plain));
    g.drawFittedText ("1017 DSP  .  INSERT COIN TO CLIP", 0, 72, getWidth(), 14,
                      juce::Justification::centred, 1);

    // ---- COMBO indicator ----
    drawCombo (g, getWidth() / 2, 85, drive01, P, phase);

    // v5: FFT freq bars removed (too busy). Scope stays clean.

    // ---- 📊 v4.4: VU METERS flanking the scope ----
    // Left side: IN L / IN R (between scope and left margin)
    const float vuTop = 110.0f;
    const float vuH   = 140.0f;
    drawVuMeter (g, juce::Rectangle<float> (74.0f, vuTop, 9.0f, vuH),
                 processorRef.inputRmsL.load(),  inPeakL,  P, "IL");
    drawVuMeter (g, juce::Rectangle<float> (88.0f, vuTop, 9.0f, vuH),
                 processorRef.inputRmsR.load(),  inPeakR,  P, "IR");
    // Right side: OUT L / OUT R
    const float rx = (float) getWidth() - 100.0f;
    drawVuMeter (g, juce::Rectangle<float> (rx,        vuTop, 9.0f, vuH),
                 processorRef.outputRmsL.load(), outPeakL, P, "OL");
    drawVuMeter (g, juce::Rectangle<float> (rx + 14.0f, vuTop, 9.0f, vuH),
                 processorRef.outputRmsR.load(), outPeakR, P, "OR");

    // Clip LEDs: one above each VU column
    drawClipLed (g, juce::Rectangle<float> (80.0f, 90.0f, 10.0f, 10.0f), clipLedFade, P);
    drawClipLed (g, juce::Rectangle<float> (rx + 7.0f, 90.0f, 10.0f, 10.0f), clipLedFade, P);

    // ---- Output meter bar under scope ----
    auto meterBar = juce::Rectangle<int> (20, 266, getWidth() - 40, 18);
    g.setColour (P.bgDeep);
    g.fillRoundedRectangle (meterBar.toFloat(), 3.0f);
    const int outW = (int) ((float) meterBar.getWidth() * juce::jlimit (0.0f, 1.0f, outMeter));
    juce::ColourGradient meter (P.gold, (float) meterBar.getX(), 0.0f,
                                juce::Colours::red, (float) meterBar.getRight(), 0.0f, false);
    meter.addColour (0.7, juce::Colours::orange);
    g.setGradientFill (meter);
    g.fillRoundedRectangle (meterBar.withWidth (outW).toFloat(), 2.0f);

    // v5 MASTER CLASS: knob at draggable position, no labels, with corona +
    // orbital particles + ember stream visuals at high drive.
    const float kcx = driveKnobX;
    const float kcy = driveKnobY;
    const float kSize = (float) driveKnobSize;
    // driveHot already defined at the top of paint()

    // Layer D: CORONA halo around knob (soft radial gradient)
    if (driveHot > 0.15f)
    {
        const float pulse = 1.0f + 0.1f * std::sin (phase * 2.0f);
        const float coronaR = kSize * (1.1f + driveHot * 0.5f) * pulse;
        juce::ColourGradient corona (
            P.gold.withAlpha (driveHot * 0.35f), kcx, kcy,
            P.purpleHi.withAlpha (0.0f), kcx + coronaR, kcy, true);
        corona.addColour (0.5, P.purpleHi.withAlpha (driveHot * 0.18f));
        corona.addColour (0.8, P.purpleLean.withAlpha (driveHot * 0.10f));
        g.setGradientFill (corona);
        g.fillEllipse (kcx - coronaR, kcy - coronaR, coronaR * 2.0f, coronaR * 2.0f);
    }

    // Layer E: 8 ORBITAL PARTICLES around the knob (elliptical orbits, trails)
    if (driveHot > 0.15f)
    {
        for (const auto& op : orbitParticles)
        {
            const float rx = kSize * (0.65f + driveHot * 0.3f);
            const float ry = rx * op.elliptical;
            const float size = 2.5f + driveHot * 2.0f + std::sin (phase * 3.0f + op.offset) * 1.0f;
            // Main dot
            const float x = kcx + std::cos (op.angle) * rx;
            const float y = kcy + std::sin (op.angle) * ry * 0.7f;
            g.setColour (P.goldHi.withAlpha (0.4f + driveHot * 0.4f));
            g.fillEllipse (x - size, y - size, size * 2.0f, size * 2.0f);
            // Trail dot 1
            const float ta = op.angle - 0.2f;
            g.setColour (P.purpleHi.withAlpha (0.25f * driveHot));
            g.fillEllipse (kcx + std::cos (ta) * rx - size * 0.4f,
                            kcy + std::sin (ta) * ry * 0.7f - size * 0.4f,
                            size * 0.8f, size * 0.8f);
            // Trail dot 2
            const float tb = op.angle - 0.4f;
            g.setColour (P.purpleLean.withAlpha (0.15f * driveHot));
            g.fillEllipse (kcx + std::cos (tb) * rx - size * 0.25f,
                            kcy + std::sin (tb) * ry * 0.7f - size * 0.25f,
                            size * 0.5f, size * 0.5f);
        }
    }

    // Layer F: EMBER STREAM rising from the knob (particles that float up)
    for (const auto& e : emberStream)
    {
        const float sz = e.size * e.life;
        g.setColour (e.colour.withAlpha (e.life * 0.7f));
        g.fillEllipse (e.x - sz, e.y - sz, sz * 2.0f, sz * 2.0f);
    }

    // ---- LED power bar centered under knob + GR meter ----
    const float pbY = kcy + kSize * 0.5f + 6.0f;
    const float pbW = kSize - 30.0f;
    drawLedPowerBar (g, juce::Rectangle<float> (kcx - pbW * 0.5f, pbY, pbW, 8.0f), drive01, P);
    drawGrMeter (g, juce::Rectangle<float> (kcx - (kSize - 20.0f) * 0.5f, pbY + 14.0f,
                                              kSize - 20.0f, 10.0f),
                 processorRef.gainReductionDb.load(), P);

    // ---- 💫 v4.4: expanding GLOW RINGS (replaces screen shake at MAX DRIVE) ----
    for (const auto& r : glowRings)
    {
        const float alpha = r.life * 0.45f;
        const float strokeW = 2.0f + r.life * 5.0f;
        g.setColour (r.colour.withAlpha (alpha));
        g.drawEllipse (r.cx - r.radius, r.cy - r.radius,
                       r.radius * 2.0f, r.radius * 2.0f, strokeW);
        // Inner glow
        g.setColour (r.colour.withAlpha (alpha * 0.4f));
        g.drawEllipse (r.cx - r.radius * 0.88f, r.cy - r.radius * 0.88f,
                       r.radius * 1.76f, r.radius * 1.76f, strokeW * 0.5f);
    }

    // ---- ✨ Particles ----
    for (const auto& part : particles)
    {
        g.setColour (part.colour.withAlpha (std::max (0.0f, part.life)));
        const float sz = part.size * part.life;
        g.fillEllipse (part.x - sz, part.y - sz, sz * 2.0f, sz * 2.0f);
    }

    // ---- Footer ----
    g.setColour (P.cream.withAlpha (0.45f));
    g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 9.5f, juce::Font::plain));
    g.drawFittedText ("1017 DSP  .  MADE IN ZONE 6",
                      0, getHeight() - 22, getWidth(), 14, juce::Justification::centred, 1);

    // ---- 📢 Level-up banner (stage transition) ----
    drawLevelUp (g, getWidth(), getHeight(), stageTransitionTimer, stageTransitionText, P);

    // v5: Lightning flash removed (chaotic). Corona + orbital particles
    // carry the MAX DRIVE energy instead.

    // ---- 🔐 SECRET PANEL overlay (triggered by 3× click on title) ----
    if (secretPanelAlpha > 0.01f)
    {
        // Dim background
        g.setColour (juce::Colours::black.withAlpha (secretPanelAlpha * 0.55f));
        g.fillAll();

        // Centered panel card
        auto panel = juce::Rectangle<int> (getWidth() / 2 - 200, getHeight() / 2 - 110,
                                            400, 220);
        g.setColour (P.bgDeep.withAlpha (secretPanelAlpha * 0.95f));
        g.fillRoundedRectangle (panel.toFloat(), 8.0f);
        g.setColour (P.goldHi.withAlpha (secretPanelAlpha * 0.9f));
        g.drawRoundedRectangle (panel.toFloat(), 8.0f, 2.0f);

        // Title
        g.setColour (P.goldHi.withAlpha (secretPanelAlpha));
        g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 14.0f, juce::Font::bold));
        g.drawText ("SECRET LAB",
                    panel.withHeight (28).translated (0, 8),
                    juce::Justification::centred, false);

        g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 9.0f, juce::Font::plain));
        g.setColour (P.cream.withAlpha (secretPanelAlpha * 0.7f));
        g.drawText ("STEREO WIDTH       OUTPUT TRIM",
                    panel.withY (panel.getY() + 150).withHeight (12),
                    juce::Justification::centred, false);

        // Hint text
        g.setColour (P.cream.withAlpha (secretPanelAlpha * 0.5f));
        g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 8.0f, juce::Font::plain));
        g.drawText ("click title 3x to close",
                    panel.withY (panel.getBottom() - 20).withHeight (14),
                    juce::Justification::centred, false);

        // Tip about ICE + easter eggs
        g.setColour (P.goldHi.withAlpha (secretPanelAlpha * 0.8f));
        g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 8.5f, juce::Font::bold));
        g.drawText ("TIP: own a LABEL in TYCOON to unlock ICE character",
                    panel.withY (panel.getY() + 36).withHeight (14),
                    juce::Justification::centred, false);
    }
}

void TrapHouseEditor::resized()
{
    // v4.3 layout — 960×560 window, cadré & aligned.
    // Top-right compact panel hosts PRESET + CHARACTER + AUTO GAIN + BYPASS.
    const int W = getWidth();

    // Top-right 2×2 grid (starts at x=720, 4 controls in 2 rows of 2)
    presetBox   .setBounds (W - 240, 20, 110, 26);
    characterBox.setBounds (W - 120, 20, 110, 26);
    autoGainBtn .setBounds (W - 240, 52, 110, 22);
    bypassBtn   .setBounds (W - 120, 52, 110, 22);

    // Scope leaves room on left/right for VU meters
    scope.setBounds (110, 100, W - 220, 160);

    // v5 MASTER CLASS: knob + tycoon use draggable positions from state
    const int ks = driveKnobSize;
    driveKnob   .setBounds ((int) driveKnobX - ks/2, (int) driveKnobY - ks/2, ks, ks);
    subGuardKnob.setBounds (-1000, -1000, 10, 10); // hidden offscreen

    // 🎮 TYCOON v2 — draggable, default 470×200
    tycoon.setBounds ((int) tycoonX, (int) tycoonY, tycoonW, tycoonH);

    // 🔐 Secret panel sliders (shown when secretPanelVisible)
    // Positioned in the center overlay card
    const int cx = getWidth() / 2;
    const int cy = getHeight() / 2;
    stereoWidthKnob.setBounds (cx - 150, cy - 50, 100, 100);
    outputTrimKnob .setBounds (cx + 50,  cy - 50, 100, 100);
}

void TrapHouseEditor::mouseDown (const juce::MouseEvent& e)
{
    // 🔐 Click the title area 3× within 1.5 s to toggle the secret panel.
    const juce::Rectangle<int> titleArea (getWidth() / 2 - 200, 14, 400, 50);
    if (titleArea.contains (e.getPosition()))
    {
        const int64_t now = juce::Time::currentTimeMillis();
        if (now - lastLogoClickMs < 1500)
            ++logoClickCount;
        else
            logoClickCount = 1;
        lastLogoClickMs = now;

        if (logoClickCount >= 3)
        {
            secretPanelVisible = ! secretPanelVisible;
            logoClickCount = 0;
            stereoWidthKnob.setVisible (secretPanelVisible);
            outputTrimKnob .setVisible (secretPanelVisible);
        }
    }

    // Easter egg: click the mascot (x=28-52, y~=bottom-55..bottom-29)
    const juce::Rectangle<int> mascotArea (26, getHeight() - 57, 26, 26);
    if (mascotArea.contains (e.getPosition()))
    {
        // Trigger a small floating message near the mascot
        stageTransitionTimer = 45;
        stageTransitionText  = "GUWOP!!!";
    }
}

void TrapHouseEditor::timerCallback()
{
    outMeter = juce::jmax (outMeter * 0.88f, processorRef.outputRms.load());
    scope.setCeilingGain (juce::Decibels::decibelsToGain (-1.0f));  // matches new ceiling default

    // 🎮 Feed audio RMS to tycoon (click bonus when audio is flowing)
    tycoon.setAudioActivity (processorRef.outputRms.load() * 3.0f);

    // 🎮 Periodically sync tycoon state back to processor (once per second).
    // Ensures DAW session saves capture the latest game progress.
    if (++tycoonSaveCounter >= 30)
    {
        tycoonSaveCounter = 0;
        processorRef.tycoonState = tycoon.getSaveState();
    }

    updateAnimation();
    repaint();
}
