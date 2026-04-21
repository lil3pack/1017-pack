#include "PluginEditor.h"
#include <array>

//==============================================================================
static const LookAndFeel1017::Palette& pal (LookAndFeel1017& lnf) { return lnf.getPalette(); }

//==============================================================================
// 🎮 Pixel sprites — 5×5 and 12×12 patterns rendered as tiny rectangles
namespace sprites
{
    using Pattern5 = std::array<std::array<int, 5>, 5>;
    using Pattern12 = std::array<std::array<int, 12>, 12>;

    static const Pattern5 STAR = {{
        {0,0,1,0,0},{0,1,1,1,0},{1,1,1,1,1},{0,1,1,1,0},{0,0,1,0,0}
    }};
    static const Pattern5 HEART = {{
        {0,1,0,1,0},{1,1,1,1,1},{1,1,1,1,1},{0,1,1,1,0},{0,0,1,0,0}
    }};
    static const Pattern5 COIN = {{
        {0,1,1,1,0},{1,1,0,1,1},{1,0,1,0,1},{1,1,0,1,1},{0,1,1,1,0}
    }};
    static const Pattern12 MASCOT = {{
        {0,0,0,0,1,1,1,1,0,0,0,0},{0,0,0,1,1,0,0,1,1,0,0,0},
        {0,0,0,1,0,0,0,0,1,0,0,0},{0,0,1,1,1,1,1,1,1,1,0,0},
        {0,1,1,1,1,1,1,1,1,1,1,0},{0,1,1,0,1,1,1,1,0,1,1,0},
        {0,1,1,0,1,1,1,1,0,1,1,0},{0,1,1,1,1,0,0,1,1,1,1,0},
        {0,0,1,1,1,1,1,1,1,1,0,0},{0,0,1,0,1,1,1,1,0,1,0,0},
        {0,0,0,0,0,1,1,0,0,0,0,0},{0,0,0,0,0,1,1,0,0,0,0,0},
    }};

    template <typename Pat>
    static void draw (juce::Graphics& g, const Pat& pattern, float x, float y,
                      float pixelSize, juce::Colour colour)
    {
        g.setColour (colour);
        for (int r = 0; r < (int) pattern.size(); ++r)
            for (int c = 0; c < (int) pattern[r].size(); ++c)
                if (pattern[r][c])
                    g.fillRect (x + c * pixelSize, y + r * pixelSize,
                                pixelSize + 0.3f, pixelSize + 0.3f);
    }
}

//==============================================================================
// LED power bar — 10 segments, lit progressively with value, gold→orange→red
static void drawLedPowerBar (juce::Graphics& g, juce::Rectangle<float> bounds,
                             float value01, const LookAndFeel1017::Palette& P)
{
    constexpr int numSegments = 10;
    constexpr float gap = 2.0f;
    const float segW = (bounds.getWidth() - gap * (numSegments - 1)) / numSegments;
    const int active = (int) std::ceil (juce::jlimit (0.0f, 1.0f, value01) * numSegments);

    for (int i = 0; i < numSegments; ++i)
    {
        const float sx = bounds.getX() + i * (segW + gap);
        const auto r = juce::Rectangle<float> (sx, bounds.getY(), segW, bounds.getHeight());
        const bool on = i < active;

        juce::Colour fill;
        if (on)
        {
            if      (i < 6) fill = P.gold;
            else if (i < 8) fill = juce::Colour (0xFFFFA726); // orange
            else            fill = juce::Colour (0xFFE53935); // red
        }
        else
        {
            fill = P.bgDeep;
        }

        g.setColour (fill.withAlpha (on ? 1.0f : 0.3f));
        g.fillRoundedRectangle (r, 1.5f);

        if (on)
        {
            // Inner highlight for LED glow feel
            g.setColour (fill.brighter (0.35f).withAlpha (0.55f));
            g.fillRoundedRectangle (r.withTrimmedLeft (1).withTrimmedRight (1)
                                      .withHeight (std::max (1.0f, bounds.getHeight() / 3.0f))
                                      .translated (0.0f, 1.0f),
                                    1.0f);
        }

        g.setColour (P.purpleLean.withAlpha (on ? 0.4f : 0.55f));
        g.drawRoundedRectangle (r, 1.5f, 0.8f);
    }
}

//==============================================================================
// COMBO indicator — appears above scope when DRIVE crosses thresholds
static void drawCombo (juce::Graphics& g, int centreX, int centreY,
                       float drive01, const LookAndFeel1017::Palette& P,
                       float phase)
{
    if (drive01 < 0.5f) return;

    juce::String text;
    juce::Colour colour;
    if      (drive01 >= 0.9f)  { text = "MAX DRIVE !"; colour = P.purpleHi; }
    else if (drive01 >= 0.75f) { text = "x3 COMBO";    colour = juce::Colour (0xFFFFA726); }
    else                       { text = "x2 COMBO";    colour = P.goldHi; }

    const float pulse = 1.0f + 0.08f * std::sin (phase * 3.0f);
    juce::Graphics::ScopedSaveState saved (g);
    g.addTransform (juce::AffineTransform::scale (pulse)
                        .translated ((float) centreX, (float) centreY));

    const juce::Rectangle<float> box (-52.0f, -10.0f, 104.0f, 18.0f);
    g.setColour (P.bgDeep.withAlpha (0.85f));
    g.fillRoundedRectangle (box, 2.0f);
    g.setColour (colour);
    g.drawRoundedRectangle (box, 2.0f, 1.5f);
    g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::bold));
    g.drawText (text, box, juce::Justification::centred, false);
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
    setSize (780, 520);  // +40 px for LED power bars & game layer at the bottom

    auto& apvts = processorRef.getAPVTS();

    addAndMakeVisible (scope);

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

    startTimerHz (30);
}

TrapHouseEditor::~TrapHouseEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

//==============================================================================
void TrapHouseEditor::paint (juce::Graphics& g)
{
    const auto& P = pal (lookAndFeel);
    const float drive01 = processorRef.getAPVTS()
        .getRawParameterValue (th::PID::drive)->load();
    const float phase = wavePhase;

    // ---- Background: red glass gradient ----
    juce::ColourGradient bg (P.bgDeep, 0.0f, 0.0f,
                             P.bgMid,  (float) getWidth(), (float) getHeight(), false);
    g.setGradientFill (bg);
    g.fillAll();

    // ---- Thin gold frame (pulses slightly with DRIVE) ----
    const float frameAlpha = juce::jlimit (0.0f, 1.0f,
        0.35f + drive01 * 0.3f + std::sin (phase * 2.0f) * 0.06f * drive01);
    g.setColour (P.gold.withAlpha (frameAlpha));
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (8.0f), 14.0f,
                            1.2f + drive01 * 1.2f);

    // ---- Title: "3PACK CLIP" in gothic gold ----
    g.setColour (P.gold);
    g.setFont (juce::Font (42.0f, juce::Font::bold));
    g.drawFittedText ("3PACK CLIP", 0, 14, getWidth(), 46,
                      juce::Justification::centred, 1);

    g.setColour (P.cream.withAlpha (0.85f));
    g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::plain));
    g.drawFittedText ("1017 DSP  .  INSERT COIN TO CLIP", 0, 62, getWidth(), 14,
                      juce::Justification::centred, 1);

    // ---- Pixel decorations: stars + hearts + diamonds in corners ----
    sprites::draw (g, sprites::STAR, 22.0f, 22.0f, 3.0f, P.goldHi);
    sprites::draw (g, sprites::STAR, 720.0f, 22.0f, 2.0f, P.goldHi);
    sprites::draw (g, sprites::COIN, 22.0f, 478.0f, 3.0f, P.goldHi);
    sprites::draw (g, sprites::HEART, 740.0f, 478.0f, 3.0f, P.goldHi);

    // 3 LIVES hearts top-left
    const juce::Colour heartRed (0xFFFF5252);
    sprites::draw (g, sprites::HEART, 18.0f,  90.0f, 2.0f, heartRed);
    sprites::draw (g, sprites::HEART, 33.0f,  90.0f, 2.0f, heartRed);
    sprites::draw (g, sprites::HEART, 48.0f,  90.0f, 2.0f, heartRed);

    // Pixel mascot (bottom-left, above the footer)
    sprites::draw (g, sprites::MASCOT, 29.0f, 441.0f, 2.0f, P.bgDeep);  // shadow
    sprites::draw (g, sprites::MASCOT, 28.0f, 440.0f, 2.0f, P.goldHi);  // mascot

    // ---- COMBO indicator (conditional) ----
    drawCombo (g, getWidth() / 2, 85, drive01, P, phase);

    // ---- Output meter bar under the scope ----
    auto meterBar = juce::Rectangle<int> (30, 244, getWidth() - 130, 18);
    g.setColour (P.bgDeep);
    g.fillRoundedRectangle (meterBar.toFloat(), 3.0f);
    const int maxW = meterBar.getWidth();
    const int outW = (int) ((float) maxW * juce::jlimit (0.0f, 1.0f, outMeter));
    juce::ColourGradient meter (P.gold, (float) meterBar.getX(), 0.0f,
                                juce::Colours::red, (float) meterBar.getRight(), 0.0f, false);
    meter.addColour (0.7, juce::Colours::orange);
    g.setGradientFill (meter);
    g.fillRoundedRectangle (meterBar.withWidth (outW).toFloat(), 2.0f);

    // ---- Knob labels ----
    g.setColour (P.gold);
    g.setFont (juce::Font (13.0f, juce::Font::bold));
    const int knobSize = 150;
    const int knobY    = 295;
    const int avail    = getWidth() - 120;
    const int gap      = (avail - 2 * knobSize) / 3;
    g.drawText ("DRIVE",     gap,                knobY - 22, knobSize, 16, juce::Justification::centred);
    g.drawText ("SUB GUARD", gap * 2 + knobSize, knobY - 22, knobSize, 16, juce::Justification::centred);

    // ---- LED power bars UNDER each knob ----
    const float pbY = knobY + knobSize + 4.0f;
    const float pbW = knobSize - 30.0f;
    drawLedPowerBar (g,
        juce::Rectangle<float> (gap + (knobSize - pbW) / 2.0f, pbY, pbW, 8.0f),
        drive01, P);
    drawLedPowerBar (g,
        juce::Rectangle<float> (gap * 2 + knobSize + (knobSize - pbW) / 2.0f, pbY, pbW, 8.0f),
        processorRef.getAPVTS().getRawParameterValue (th::PID::subGuard)->load(),
        P);

    // ---- Right side panel label ----
    g.setFont (juce::Font (10.0f, juce::Font::bold));
    g.drawText ("CHARACTER", getWidth() - 110, 98, 96, 14, juce::Justification::centred);

    // ---- Footer ----
    g.setColour (P.cream.withAlpha (0.45f));
    g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 9.5f, juce::Font::plain));
    g.drawFittedText ("1017 DSP  .  MADE IN ZONE 6",
                      0, getHeight() - 22, getWidth(), 14,
                      juce::Justification::centred, 1);
}

void TrapHouseEditor::resized()
{
    // Preset dropdown top-right
    presetBox.setBounds (getWidth() - 110, 22, 92, 26);

    // Oscilloscope
    scope.setBounds (30, 90, getWidth() - 130, 150);

    // Right side panel
    characterBox.setBounds (getWidth() - 108, 116, 96, 26);
    autoGainBtn .setBounds (getWidth() - 110, 158, 104, 22);
    bypassBtn   .setBounds (getWidth() - 110, 186, 104, 22);

    // 2 big knobs (150 px each, with LED power bars below)
    const int knobSize = 150;
    const int knobY    = 295;
    const int available = getWidth() - 120;
    const int totalKnobWidth = 2 * knobSize;
    const int gap = (available - totalKnobWidth) / 3;

    driveKnob   .setBounds (gap,                knobY, knobSize, knobSize);
    subGuardKnob.setBounds (gap * 2 + knobSize, knobY, knobSize, knobSize);
}

void TrapHouseEditor::timerCallback()
{
    outMeter = juce::jmax (outMeter * 0.88f, processorRef.outputRms.load());
    scope.setCeilingGain (juce::Decibels::decibelsToGain (-0.3f));
    wavePhase += 0.08f;
    repaint();
}
