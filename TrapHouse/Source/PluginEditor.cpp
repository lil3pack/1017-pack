#include "PluginEditor.h"

//==============================================================================
static const LookAndFeel1017::Palette& pal (LookAndFeel1017& lnf) { return lnf.getPalette(); }

//==============================================================================
void TrapHouseEditor::setupKnob (juce::Slider& k)
{
    k.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    k.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                           juce::MathConstants<float>::pi * 2.75f, true);
    // Value is drawn INSIDE the disc by LookAndFeel1017::drawRotarySlider
    k.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    k.setVelocityBasedMode (true);
    k.setVelocityModeParameters (0.7, 1, 0.08, false);
    k.setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    k.setPopupDisplayEnabled (true, true, nullptr, 1000); // tooltip while dragging
    addAndMakeVisible (k);
}

//==============================================================================
TrapHouseEditor::TrapHouseEditor (TrapHouseProcessor& p)
    : AudioProcessorEditor (&p),
      processorRef (p),
      scope (p.scopeBuffer, lookAndFeel)
{
    setLookAndFeel (&lookAndFeel);
    setSize (780, 480);

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

    // Background
    juce::ColourGradient bg (P.bgDeep, 0.0f, 0.0f,
                             P.bgMid,  (float) getWidth(), (float) getHeight(), false);
    g.setGradientFill (bg);
    g.fillAll();

    // Thin gold frame
    g.setColour (P.gold.withAlpha (0.35f));
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (8.0f), 14.0f, 1.2f);

    // Title
    g.setColour (P.gold);
    g.setFont (juce::Font (40.0f, juce::Font::bold));
    g.drawFittedText ("TRAP HOUSE", 0, 18, getWidth(), 44,
                      juce::Justification::centred, 1);

    g.setColour (P.cream.withAlpha (0.8f));
    g.setFont (juce::Font (11.0f, juce::Font::plain));
    g.drawFittedText ("1017 SERIES / ONE-KNOB CLIPPER", 0, 62, getWidth(), 14,
                      juce::Justification::centred, 1);

    // Output meter bar under the scope
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

    // Knob labels
    g.setColour (P.gold);
    g.setFont (juce::Font (13.0f, juce::Font::bold));

    // Label positions must match knob positions in resized()
    const int knobSize = 180;
    const int knobY    = 300;
    const int avail    = getWidth() - 120;
    const int gap      = (avail - 2 * knobSize) / 3;
    g.drawText ("DRIVE",     gap,                    knobY - 24, knobSize, 18, juce::Justification::centred);
    g.drawText ("SUB GUARD", gap * 2 + knobSize,     knobY - 24, knobSize, 18, juce::Justification::centred);

    // Right side panel label
    g.setFont (juce::Font (10.0f, juce::Font::bold));
    g.drawText ("CHARACTER", getWidth() - 110, 98, 96, 14, juce::Justification::centred);

    // Footer
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

    // Oscilloscope
    scope.setBounds (30, 90, getWidth() - 130, 150);

    // Right side panel
    characterBox.setBounds (getWidth() - 108, 116, 96, 26);
    autoGainBtn .setBounds (getWidth() - 110, 158, 104, 22);
    bypassBtn   .setBounds (getWidth() - 110, 186, 104, 22);

    // 2 big knobs, centered in the bottom half
    const int knobSize = 180;
    const int knobY = 300;

    // Leave room on the right for the side panel (~120px)
    const int available = getWidth() - 120;
    const int totalKnobWidth = 2 * knobSize;
    const int gap = (available - totalKnobWidth) / 3;

    driveKnob   .setBounds (gap,                         knobY, knobSize, knobSize);
    subGuardKnob.setBounds (gap * 2 + knobSize,          knobY, knobSize, knobSize);
}

void TrapHouseEditor::timerCallback()
{
    outMeter = juce::jmax (outMeter * 0.88f, processorRef.outputRms.load());

    // Ceiling is fixed at -0.3 dB in the DRIVE macro design.
    scope.setCeilingGain (juce::Decibels::decibelsToGain (-0.3f));

    repaint();
}
