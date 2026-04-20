#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"

/**
 * Placeholder editor — lays out the controls in the right slots so we can
 * build & test the DSP end-to-end, then swap in the custom LookAndFeel1017
 * (purple + gold from the Nano Banana mockups) without changing any layout logic.
 */
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

    TrapHouseProcessor& processorRef;

    // Controls
    juce::Slider inputGainKnob, ceilingKnob, kneeKnob, harmonicsKnob;
    juce::Label  inputGainLbl,  ceilingLbl,  kneeLbl,  harmonicsLbl;
    juce::ComboBox characterBox;
    juce::ToggleButton autoGainBtn { "Auto Gain" };
    juce::ToggleButton bypassBtn   { "Bypass" };

    // Meters (simple bars for now, proper skin later)
    float inMeter  { 0.0f };
    float outMeter { 0.0f };

    using APVTS = juce::AudioProcessorValueTreeState;
    std::unique_ptr<APVTS::SliderAttachment>   inputGainAtt, ceilingAtt, kneeAtt, harmonicsAtt;
    std::unique_ptr<APVTS::ComboBoxAttachment> characterAtt;
    std::unique_ptr<APVTS::ButtonAttachment>   autoGainAtt, bypassAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrapHouseEditor)
};
