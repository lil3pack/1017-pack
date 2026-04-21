#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"
#include "LookAndFeel1017.h"
#include "ui/ScopeDisplay.h"

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

    // Controls
    juce::Slider inputGainKnob, ceilingKnob, kneeKnob, harmonicsKnob;
    juce::ComboBox characterBox, presetBox;
    juce::ToggleButton autoGainBtn { "AUTO GAIN" };
    juce::ToggleButton bypassBtn   { "BYPASS" };

    // Oscilloscope
    th::ui::ScopeDisplay scope;

    // Meter state
    float inMeter  { 0.0f };
    float outMeter { 0.0f };

    using APVTS = juce::AudioProcessorValueTreeState;
    std::unique_ptr<APVTS::SliderAttachment>   inputGainAtt, ceilingAtt, kneeAtt, harmonicsAtt;
    std::unique_ptr<APVTS::ComboBoxAttachment> characterAtt;
    std::unique_ptr<APVTS::ButtonAttachment>   autoGainAtt, bypassAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrapHouseEditor)
};
