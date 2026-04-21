#include "PluginEditor.h"

//==============================================================================
static const LookAndFeel1017::Palette& pal (LookAndFeel1017& lnf) { return lnf.getPalette(); }

//==============================================================================
void TrapHouseEditor::setupKnob (juce::Slider& k)
{
    k.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    k.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                           juce::MathConstants<float>::pi * 2.75f, true);
    k.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 74, 20);
    k.setVelocityBasedMode (true);
    k.setVelocityModeParameters (0.7, 1, 0.08, false);
    k.setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    addAndMakeVisible (k);
}

//==============================================================================
TrapHouseEditor::TrapHouseEditor (TrapHouseProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p)
{
    setLookAndFeel (&lookAndFeel);
    setSize (820, 480);

    auto& apvts = processorRef.getAPVTS();

    setupKnob (inputGainKnob);
    setupKnob (ceilingKnob);
    setupKnob (kneeKnob);
    setupKnob (harmonicsKnob);

    // Double-click to return to each parameter's stored default
    inputGainKnob.setDoubleClickReturnValue (true, 0.0);
    ceilingKnob  .setDoubleClickReturnValue (true, -0.3);
    kneeKnob     .setDoubleClickReturnValue (true, 0.25);
    harmonicsKnob.setDoubleClickReturnValue (true, 0.40);

    // Character combo (HARD/TAPE/TUBE)
    characterBox.addItemList ({ "HARD", "TAPE", "TUBE" }, 1);
    addAndMakeVisible (characterBox);

    // Preset placeholder (will be wired next session)
    presetBox.addItem ("PRESET", 1);
    presetBox.setSelectedId (1, juce::dontSendNotification);
    addAndMakeVisible (presetBox);

    addAndMakeVisible (autoGainBtn);
    addAndMakeVisible (bypassBtn);

    inputGainAtt = std::make_unique<APVTS::SliderAttachment>   (apvts, th::PID::inputGain, inputGainKnob);
    ceilingAtt   = std::make_unique<APVTS::SliderAttachment>   (apvts, th::PID::ceiling,   ceilingKnob);
    kneeAtt      = std::make_unique<APVTS::SliderAttachment>   (apvts, th::PID::knee,      kneeKnob);
    harmonicsAtt = std::make_unique<APVTS::SliderAttachment>   (apvts, th::PID::harmonics, harmonicsKnob);
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

    // ----- Background: purple glass gradient -----
    juce::ColourGradient bg (P.bgDeep, 0.0f, 0.0f,
                             P.bgMid,  (float) getWidth(), (float) getHeight(), false);
    g.setGradientFill (bg);
    g.fillAll();

    // ----- Thin gold frame -----
    g.setColour (P.gold.withAlpha (0.35f));
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (8.0f), 14.0f, 1.2f);

    // ----- Title -----
    g.setColour (P.gold);
    g.setFont (juce::Font (40.0f, juce::Font::bold));
    g.drawFittedText ("TRAP HOUSE", 0, 18, getWidth(), 44,
                      juce::Justification::centred, 1);

    g.setColour (P.cream.withAlpha (0.8f));
    g.setFont (juce::Font (11.0f, juce::Font::plain));
    g.drawFittedText ("1017 SERIES / HARD CLIPPER", 0, 62, getWidth(), 14,
                      juce::Justification::centred, 1);

    // ----- Display area (meter + ceiling lines, placeholder for scope) -----
    auto displayArea = juce::Rectangle<int> (30, 90, getWidth() - 130, 170);
    g.setColour (P.bgDeep.darker (0.3f));
    g.fillRoundedRectangle (displayArea.toFloat(), 8.0f);
    g.setColour (P.gold.withAlpha (0.4f));
    g.drawRoundedRectangle (displayArea.toFloat(), 8.0f, 1.0f);

    // Faint grid
    g.setColour (P.purpleLean.withAlpha (0.15f));
    for (int i = 1; i < 8; ++i)
    {
        const int gx = displayArea.getX() + displayArea.getWidth() * i / 8;
        g.drawLine ((float) gx, (float) displayArea.getY() + 8,
                    (float) gx, (float) displayArea.getBottom() - 40, 1.0f);
    }
    for (int i = 1; i < 4; ++i)
    {
        const int gy = displayArea.getY() + (displayArea.getHeight() - 40) * i / 4;
        g.drawLine ((float) displayArea.getX() + 8, (float) gy,
                    (float) displayArea.getRight() - 8, (float) gy, 1.0f);
    }

    // Ceiling dashed lines (top & bottom)
    const float ceilingNorm = juce::jlimit (
        0.05f, 1.0f,
        juce::Decibels::decibelsToGain (processorRef.getAPVTS()
            .getRawParameterValue (th::PID::ceiling)->load()));
    const float centerY = (float) displayArea.getCentreY() - 20.0f;
    const float halfH   = (float) (displayArea.getHeight() - 50) * 0.5f * ceilingNorm;
    const float dashes[] = { 6.0f, 4.0f };
    g.setColour (P.gold.withAlpha (0.75f));
    g.drawDashedLine ({ (float) displayArea.getX() + 10, centerY - halfH,
                        (float) displayArea.getRight() - 10, centerY - halfH },
                      dashes, 2, 1.5f);
    g.drawDashedLine ({ (float) displayArea.getX() + 10, centerY + halfH,
                        (float) displayArea.getRight() - 10, centerY + halfH },
                      dashes, 2, 1.5f);

    // Live waveform placeholder — horizontal lines showing input vs output
    const float mid = centerY;
    g.setColour (P.purpleHi.withAlpha (0.65f));
    const float inAmp  = juce::jmin (1.0f, inMeter * 2.0f);
    const float outAmp = juce::jmin (1.0f, outMeter * 2.0f);
    g.fillRect (juce::Rectangle<float> ((float) displayArea.getX() + 10,
                                         mid - halfH * outAmp,
                                         (float) displayArea.getWidth() - 20,
                                         halfH * outAmp * 2.0f + 1.0f));
    juce::ignoreUnused (inAmp);

    // ----- Meter bar (output peak) at bottom of display area -----
    auto meterBar = juce::Rectangle<int> (displayArea.getX() + 8,
                                          displayArea.getBottom() - 26,
                                          displayArea.getWidth() - 16, 18);
    g.setColour (P.bgDeep);
    g.fillRoundedRectangle (meterBar.toFloat(), 3.0f);

    const int maxW = meterBar.getWidth();
    const int outW = (int) ((float) maxW * juce::jlimit (0.0f, 1.0f, outMeter));

    juce::ColourGradient meter (P.gold, (float) meterBar.getX(), 0.0f,
                                juce::Colours::red, (float) meterBar.getRight(), 0.0f, false);
    meter.addColour (0.7, juce::Colours::orange);
    g.setGradientFill (meter);
    g.fillRoundedRectangle (meterBar.withWidth (outW).toFloat(), 2.0f);

    g.setColour (P.cream);
    g.setFont (8.5f);

    // ----- Knob labels -----
    g.setColour (P.gold);
    g.setFont (juce::Font (11.0f, juce::Font::bold));

    const int knobY = 290;
    const int knobW = 130;
    const int knobStart = 40;
    const int knobGap = 20;

    const juce::StringArray knobNames { "INPUT GAIN", "CEILING", "SOFT KNEE", "HARMONICS" };
    for (int i = 0; i < 4; ++i)
    {
        const int x = knobStart + i * (knobW + knobGap);
        g.drawText (knobNames[i], x, knobY - 22, knobW, 16, juce::Justification::centred);
    }

    // ----- Right side panel label -----
    g.setFont (juce::Font (10.0f, juce::Font::bold));
    g.drawText ("CHARACTER", getWidth() - 110, 98, 96, 14, juce::Justification::centred);

    // ----- Footer -----
    g.setColour (P.cream.withAlpha (0.4f));
    g.setFont (9.5f);
    g.drawFittedText ("1017 DSP - MADE IN ZONE 6",
                      0, getHeight() - 22, getWidth(), 14,
                      juce::Justification::centred, 1);
}

void TrapHouseEditor::resized()
{
    // Preset dropdown top-right
    presetBox.setBounds (getWidth() - 110, 22, 92, 26);

    // Right side panel: CHARACTER combo + AUTO GAIN + BYPASS
    characterBox.setBounds (getWidth() - 108, 116, 96, 26);
    autoGainBtn .setBounds (getWidth() - 110, 158, 104, 22);
    bypassBtn   .setBounds (getWidth() - 110, 186, 104, 22);

    // 4 knobs row
    const int knobY = 290;
    const int knobW = 130;
    const int knobH = 130;
    const int startX = 40;
    const int gap = 20;

    inputGainKnob.setBounds (startX + 0 * (knobW + gap), knobY, knobW, knobH);
    ceilingKnob  .setBounds (startX + 1 * (knobW + gap), knobY, knobW, knobH);
    kneeKnob     .setBounds (startX + 2 * (knobW + gap), knobY, knobW, knobH);
    harmonicsKnob.setBounds (startX + 3 * (knobW + gap), knobY, knobW, knobH);
}

void TrapHouseEditor::timerCallback()
{
    const float inTarget  = processorRef.inputRms.load();
    const float outTarget = processorRef.outputRms.load();

    inMeter  = juce::jmax (inMeter  * 0.88f, inTarget);
    outMeter = juce::jmax (outMeter * 0.88f, outTarget);

    repaint();
}
