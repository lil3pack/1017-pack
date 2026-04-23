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
    setSize (780, 520);

    auto& apvts = processorRef.getAPVTS();

    addAndMakeVisible (scope);

    // 🎮 Tycoon game — load saved state from processor's tycoonState ValueTree
    tycoon.loadSaveState (p.tycoonState);
    addAndMakeVisible (tycoon);

    setupKnob (driveKnob);
    setupKnob (subGuardKnob);
    driveKnob   .setDoubleClickReturnValue (true, 0.35);
    subGuardKnob.setDoubleClickReturnValue (true, 0.00);

    characterBox.addItemList ({ "HARD", "TAPE", "TUBE" }, 1);
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

    // Init animation state
    for (auto& s : freqSeed) s = rng.nextFloat();
    particles.reserve (256);

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

    // Screen shake at MAX
    if (drive > 0.85f)
    {
        const float I = (drive - 0.85f) * 60.0f;
        screenShakeX = (rng.nextFloat() - 0.5f) * I;
        screenShakeY = (rng.nextFloat() - 0.5f) * I;
    }
    else { screenShakeX *= 0.7f; screenShakeY *= 0.7f; }

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

    // Apply screen shake to everything inside this save block
    juce::Graphics::ScopedSaveState mainSave (g);
    g.addTransform (juce::AffineTransform::translation (screenShakeX, screenShakeY));

    // ---- Background: red glass gradient (gets hotter at high drive) ----
    const juce::Colour bgD = drive01 > 0.7f ? P.bgDeep.interpolatedWith (P.purpleLean, (drive01 - 0.7f) * 0.4f) : P.bgDeep;
    const juce::Colour bgM = drive01 > 0.7f ? P.bgMid.interpolatedWith  (P.purpleLean, (drive01 - 0.7f) * 0.25f) : P.bgMid;
    juce::ColourGradient bg (bgD, 0.0f, 0.0f, bgM, (float) getWidth(), (float) getHeight(), false);
    g.setGradientFill (bg);
    g.fillAll();

    // ---- 🔥 Procedural flames at bottom (only when DRIVE > 40%) ----
    drawFlames (g, getWidth(), getHeight(), drive01, P, phase);

    // ---- Thin gold frame (pulses with DRIVE) ----
    const float frameAlpha = juce::jlimit (0.0f, 1.0f,
        0.35f + drive01 * 0.35f + std::sin (phase * 2.0f) * 0.07f * drive01);
    g.setColour (P.gold.withAlpha (frameAlpha));
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (8.0f), 14.0f,
                            1.2f + drive01 * 1.8f);

    // ---- Pixel decorations (corners) ----
    sprites::draw (g, sprites::STAR,  22.0f,  22.0f, 3.0f, P.goldHi);
    sprites::draw (g, sprites::STAR,  720.0f, 22.0f, 2.0f, P.goldHi);
    sprites::draw (g, sprites::COIN,  22.0f,  478.0f, 3.0f, P.goldHi);
    sprites::draw (g, sprites::HEART, 740.0f, 478.0f, 3.0f, P.goldHi);

    // 3 LIVES hearts top-left
    const juce::Colour heartRed (0xFFFF5252);
    sprites::draw (g, sprites::HEART, 18.0f, 90.0f, 2.0f, heartRed);
    sprites::draw (g, sprites::HEART, 33.0f, 90.0f, 2.0f, heartRed);
    sprites::draw (g, sprites::HEART, 48.0f, 90.0f, 2.0f, heartRed);

    // Pixel mascot (skull bas-gauche)
    sprites::draw (g, sprites::MASCOT, 29.0f, 441.0f, 2.0f, P.bgDeep);
    sprites::draw (g, sprites::MASCOT, 28.0f, 440.0f, 2.0f, P.goldHi);

    // ---- ⚡ Demonic eyes @ MAX DRIVE ----
    drawDemonEyes (g, getWidth(), drive01, P, phase);

    // ---- Title with chromatic aberration ----
    drawChromaticTitle (g, "3PACK CLIP",
                        juce::Rectangle<int> (0, 14, getWidth(), 46),
                        42.0f, P, drive01);

    g.setColour (P.cream.withAlpha (0.85f));
    g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::plain));
    g.drawFittedText ("1017 DSP  .  INSERT COIN TO CLIP", 0, 62, getWidth(), 14,
                      juce::Justification::centred, 1);

    // ---- COMBO indicator ----
    drawCombo (g, getWidth() / 2, 85, drive01, P, phase);

    // ---- 🎵 FFT freq bars BEHIND scope ----
    auto scopeBounds = juce::Rectangle<int> (30, 90, getWidth() - 130, 150);
    drawFreqBars (g, scopeBounds, drive01, P, freqSeed, phase);

    // ---- Output meter bar under scope ----
    auto meterBar = juce::Rectangle<int> (30, 244, getWidth() - 130, 18);
    g.setColour (P.bgDeep);
    g.fillRoundedRectangle (meterBar.toFloat(), 3.0f);
    const int outW = (int) ((float) meterBar.getWidth() * juce::jlimit (0.0f, 1.0f, outMeter));
    juce::ColourGradient meter (P.gold, (float) meterBar.getX(), 0.0f,
                                juce::Colours::red, (float) meterBar.getRight(), 0.0f, false);
    meter.addColour (0.7, juce::Colours::orange);
    g.setGradientFill (meter);
    g.fillRoundedRectangle (meterBar.withWidth (outW).toFloat(), 2.0f);

    // ---- Knob labels ----
    g.setColour (P.gold);
    g.setFont (juce::Font (13.0f, juce::Font::bold));
    const int knobSize = 150, knobY = 295;
    const int avail = getWidth() - 120;
    const int gap = (avail - 2 * knobSize) / 3;
    g.drawText ("DRIVE",     gap,                knobY - 22, knobSize, 16, juce::Justification::centred);
    g.drawText ("SUB GUARD", gap * 2 + knobSize, knobY - 22, knobSize, 16, juce::Justification::centred);

    // ---- 🌟 Knob auras (below the slider component, above the bg) ----
    const float subGuard = processorRef.getAPVTS().getRawParameterValue (th::PID::subGuard)->load();
    drawKnobAura (g, gap + knobSize * 0.5f,                    knobY + knobSize * 0.5f,
                  (float) knobSize, drive01, P, phase);
    drawKnobAura (g, gap * 2 + knobSize + knobSize * 0.5f,     knobY + knobSize * 0.5f,
                  (float) knobSize, subGuard, P, phase);

    // ---- LED power bars under knobs ----
    const float pbY = knobY + knobSize + 4.0f;
    const float pbW = knobSize - 30.0f;
    drawLedPowerBar (g, juce::Rectangle<float> (gap + (knobSize - pbW) / 2.0f, pbY, pbW, 8.0f), drive01, P);
    drawLedPowerBar (g, juce::Rectangle<float> (gap * 2 + knobSize + (knobSize - pbW) / 2.0f, pbY, pbW, 8.0f),
                     subGuard, P);

    // ---- Right side panel label ----
    g.setFont (juce::Font (10.0f, juce::Font::bold));
    g.drawText ("CHARACTER", getWidth() - 110, 98, 96, 14, juce::Justification::centred);

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

    // ---- ⚡ Lightning white flash overlay ----
    if (lightningFlash > 0.01f)
    {
        g.setColour (P.cream.withAlpha (lightningFlash * 0.25f));
        g.fillAll();
    }
}

void TrapHouseEditor::resized()
{
    presetBox.setBounds (getWidth() - 110, 22, 92, 26);
    scope.setBounds (30, 90, getWidth() - 130, 150);
    characterBox.setBounds (getWidth() - 108, 116, 96, 26);
    autoGainBtn .setBounds (getWidth() - 110, 158, 104, 22);
    bypassBtn   .setBounds (getWidth() - 110, 186, 104, 22);

    const int knobSize = 150, knobY = 295;
    const int available = getWidth() - 120;
    const int gap = (available - 2 * knobSize) / 3;
    driveKnob   .setBounds (gap,                knobY, knobSize, knobSize);
    subGuardKnob.setBounds (gap * 2 + knobSize, knobY, knobSize, knobSize);

    // 🎮 Tycoon game — bottom-right corner, 230×185 px.
    // Fits between the side panel (top) and footer (bottom), right of subGuard knob.
    tycoon.setBounds (getWidth() - 236, getHeight() - 210, 230, 185);
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
