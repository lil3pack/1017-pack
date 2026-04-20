#include "PluginEditor.h"

namespace
{
    // 1017 palette (matches the Nano Banana mockup)
    const juce::Colour bgDeep     { 0xFF1A0F2B };
    const juce::Colour bgMid      { 0xFF2B1A3D };
    const juce::Colour purpleLean { 0xFF6B3FA0 };
    const juce::Colour purpleHi   { 0xFF9B6FD9 };
    const juce::Colour gold       { 0xFFD4AF37 };
    const juce::Colour goldHi     { 0xFFF4D03F };
    const juce::Colour cream      { 0xFFF5E9D1 };

    void setupKnob (juce::Slider& k)
    {
        k.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        k.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 18);
        k.setColour (juce::Slider::rotarySliderFillColourId, gold);
        k.setColour (juce::Slider::rotarySliderOutlineColourId, purpleLean);
        k.setColour (juce::Slider::thumbColourId, purpleHi);
        k.setColour (juce::Slider::textBoxTextColourId, cream);
        k.setColour (juce::Slider::textBoxBackgroundColourId, bgDeep);
        k.setColour (juce::Slider::textBoxOutlineColourId, gold.withAlpha (0.4f));
    }

    void setupLabel (juce::Label& l, const juce::String& text, juce::Component& target)
    {
        l.setText (text, juce::dontSendNotification);
        l.setJustificationType (juce::Justification::centred);
        l.setColour (juce::Label::textColourId, gold);
        l.attachToComponent (&target, false);
    }
}

TrapHouseEditor::TrapHouseEditor (TrapHouseProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p)
{
    setSize (720, 420);

    using APVTS = juce::AudioProcessorValueTreeState;
    auto& apvts = processorRef.getAPVTS();

    setupKnob (inputGainKnob); addAndMakeVisible (inputGainKnob);
    setupKnob (ceilingKnob);   addAndMakeVisible (ceilingKnob);
    setupKnob (kneeKnob);      addAndMakeVisible (kneeKnob);
    setupKnob (harmonicsKnob); addAndMakeVisible (harmonicsKnob);

    setupLabel (inputGainLbl, "INPUT GAIN", inputGainKnob);
    setupLabel (ceilingLbl,   "CEILING",    ceilingKnob);
    setupLabel (kneeLbl,      "SOFT KNEE",  kneeKnob);
    setupLabel (harmonicsLbl, "HARMONICS",  harmonicsKnob);

    addAndMakeVisible (characterBox);
    characterBox.addItemList ({ "HARD", "TAPE", "TUBE" }, 1);
    characterBox.setColour (juce::ComboBox::backgroundColourId, bgDeep);
    characterBox.setColour (juce::ComboBox::textColourId, gold);
    characterBox.setColour (juce::ComboBox::arrowColourId, gold);
    characterBox.setColour (juce::ComboBox::outlineColourId, gold.withAlpha (0.5f));

    addAndMakeVisible (autoGainBtn);
    autoGainBtn.setColour (juce::ToggleButton::textColourId, cream);
    autoGainBtn.setColour (juce::ToggleButton::tickColourId, goldHi);

    addAndMakeVisible (bypassBtn);
    bypassBtn.setColour (juce::ToggleButton::textColourId, cream);
    bypassBtn.setColour (juce::ToggleButton::tickColourId, goldHi);

    // Attachments
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
}

void TrapHouseEditor::paint (juce::Graphics& g)
{
    // Background gradient (matches mockup)
    juce::ColourGradient bg (bgDeep, 0.0f, 0.0f,
                             bgMid,  (float) getWidth(), (float) getHeight(), false);
    g.setGradientFill (bg);
    g.fillAll();

    // Title
    g.setColour (gold);
    g.setFont (juce::Font (34.0f, juce::Font::bold));
    g.drawFittedText ("TRAP HOUSE", 0, 10, getWidth(), 40, juce::Justification::centred, 1);

    g.setColour (cream);
    g.setFont (juce::Font (12.0f));
    g.drawFittedText ("1017 SERIES / HARD CLIPPER", 0, 50, getWidth(), 16, juce::Justification::centred, 1);

    // Meter area background
    auto meterArea = juce::Rectangle<int> (40, 90, getWidth() - 80, 40);
    g.setColour (bgDeep);
    g.fillRoundedRectangle (meterArea.toFloat(), 6.0f);
    g.setColour (gold.withAlpha (0.4f));
    g.drawRoundedRectangle (meterArea.toFloat(), 6.0f, 1.0f);

    // Draw meters (horizontal, gold -> red gradient)
    auto drawMeter = [&] (juce::Rectangle<int> r, float level01, const juce::String& label)
    {
        g.setColour (cream);
        g.setFont (10.0f);
        g.drawText (label, r.removeFromLeft (30), juce::Justification::centredLeft);

        const int filled = (int) (r.getWidth() * juce::jlimit (0.0f, 1.0f, level01));
        juce::ColourGradient grad (gold, (float) r.getX(), 0.0f,
                                   juce::Colours::red, (float) r.getRight(), 0.0f, false);
        grad.addColour (0.7, juce::Colours::orange);
        g.setGradientFill (grad);
        g.fillRect (r.withWidth (filled));

        g.setColour (bgDeep.brighter (0.1f));
        g.drawRect (r, 1);
    };

    auto inMeterArea = meterArea.removeFromTop (18).reduced (6, 2);
    drawMeter (inMeterArea, inMeter, "IN");
    auto outMeterArea = meterArea.reduced (6, 2);
    drawMeter (outMeterArea, outMeter, "OUT");

    // Footer
    g.setColour (cream.withAlpha (0.5f));
    g.setFont (10.0f);
    g.drawFittedText ("1017 DSP - MADE IN ZONE 6",
                      0, getHeight() - 18, getWidth(), 14,
                      juce::Justification::centred, 1);
}

void TrapHouseEditor::resized()
{
    const int knobSize = 100;
    const int knobY = 180;
    const int spacing = (getWidth() - 4 * knobSize - 40 /* side panel */) / 5;

    int x = spacing;
    inputGainKnob.setBounds (x, knobY, knobSize, knobSize); x += knobSize + spacing;
    ceilingKnob  .setBounds (x, knobY, knobSize, knobSize); x += knobSize + spacing;
    kneeKnob     .setBounds (x, knobY, knobSize, knobSize); x += knobSize + spacing;
    harmonicsKnob.setBounds (x, knobY, knobSize, knobSize);

    // Right side panel
    const int sideX = getWidth() - 70;
    characterBox.setBounds (sideX - 10, 100, 80, 22);
    autoGainBtn .setBounds (sideX - 20, 140, 100, 22);
    bypassBtn   .setBounds (sideX - 20, 170, 100, 22);
}

void TrapHouseEditor::timerCallback()
{
    // Cheap peak-hold smoothing toward current RMS
    const float inTarget  = processorRef.inputRms.load();
    const float outTarget = processorRef.outputRms.load();

    inMeter  = std::max (inMeter  * 0.82f, inTarget);
    outMeter = std::max (outMeter * 0.82f, outTarget);

    repaint();
}
