// LEMONADE — Vocal chain plugin
//
// STUB: this is a working passthrough plugin that already registers the 4
// chain presets + main knobs as AudioProcessorValueTreeState parameters so the
// UI can be wired up immediately. DSP will be implemented next session:
//  - Lead chain : HP 80Hz -> de-ess -> comp 4:1 -> tilt EQ -> saturation
//  - Adlib chain: pitch +3st -> slap delay 120ms -> plate reverb 30%
//  - Ghost chain: LP 6kHz -> stereo widener -> long verb ducked
//  - Zay chain  : pitch -5st -> washy reverb 70% -> sidechain to key

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

namespace lem::PID
{
    inline constexpr auto chain  = "chain";
    inline constexpr auto tune   = "tune";
    inline constexpr auto mix    = "mix";
    inline constexpr auto space  = "space";
    inline constexpr auto color  = "color";
    inline constexpr auto doubler = "doubler";
    inline constexpr auto deEss  = "de_ess";
    inline constexpr auto widen  = "widen";
    inline constexpr auto bypass = "bypass";
}

class LemonadeProcessor : public juce::AudioProcessor
{
public:
    LemonadeProcessor()
        : AudioProcessor (BusesProperties()
            .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
            .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
        , apvts (*this, nullptr, "STATE", createLayout())
    {}

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        using namespace juce;
        std::vector<std::unique_ptr<RangedAudioParameter>> p;

        p.push_back (std::make_unique<AudioParameterChoice> (
            ParameterID { lem::PID::chain, 1 }, "Chain",
            StringArray { "LEAD", "ADLIB", "GHOST", "ZAY" }, 1 /* ADLIB default */));

        p.push_back (std::make_unique<AudioParameterFloat> (
            ParameterID { lem::PID::tune, 1 }, "Tune",
            NormalisableRange<float> (-12.0f, 12.0f, 1.0f), 3.0f));

        p.push_back (std::make_unique<AudioParameterFloat> (
            ParameterID { lem::PID::mix, 1 }, "Mix",
            NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.65f));

        p.push_back (std::make_unique<AudioParameterFloat> (
            ParameterID { lem::PID::space, 1 }, "Space",
            NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.30f));

        p.push_back (std::make_unique<AudioParameterFloat> (
            ParameterID { lem::PID::color, 1 }, "Color",
            NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.55f));

        p.push_back (std::make_unique<AudioParameterBool> (
            ParameterID { lem::PID::doubler, 1 }, "Doubler", false));
        p.push_back (std::make_unique<AudioParameterBool> (
            ParameterID { lem::PID::deEss, 1 }, "De-Ess", true));
        p.push_back (std::make_unique<AudioParameterBool> (
            ParameterID { lem::PID::widen, 1 }, "Widen", true));
        p.push_back (std::make_unique<AudioParameterBool> (
            ParameterID { lem::PID::bypass, 1 }, "Bypass", false));

        return { p.begin(), p.end() };
    }

    void prepareToPlay (double, int) override {}
    void releaseResources() override {}

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override
    {
        const auto& in  = layouts.getMainInputChannelSet();
        const auto& out = layouts.getMainOutputChannelSet();
        return in == out && in == juce::AudioChannelSet::stereo();
    }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        // Passthrough for now. DSP goes here next session.
        juce::ScopedNoDenormals noDenormals;
        juce::ignoreUnused (buffer);
    }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "LEMONADE"; }
    bool acceptsMidi()  const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& dest) override
    {
        if (auto xml = apvts.copyState().createXml())
            copyXmlToBinary (*xml, dest);
    }
    void setStateInformation (const void* data, int size) override
    {
        if (auto xml = getXmlFromBinary (data, size))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
    }

    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts; }

private:
    juce::AudioProcessorValueTreeState apvts;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LemonadeProcessor)
};

// Minimal editor stub so the plugin opens in FL Studio.
class LemonadeEditor : public juce::AudioProcessorEditor
{
public:
    explicit LemonadeEditor (LemonadeProcessor& p) : AudioProcessorEditor (&p)
    {
        setSize (720, 420);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xFF2B1A3D));
        g.setColour (juce::Colour (0xFFD4AF37));
        g.setFont (juce::Font (40.0f, juce::Font::bold));
        g.drawFittedText ("LEMONADE", getLocalBounds().removeFromTop (80),
                          juce::Justification::centred, 1);
        g.setColour (juce::Colour (0xFFF5E9D1));
        g.setFont (juce::Font (14.0f));
        g.drawFittedText ("1017 SERIES / VOCAL CHAIN", getLocalBounds().removeFromTop (120).removeFromBottom (30),
                          juce::Justification::centred, 1);
        g.drawFittedText ("DSP coming next session. Parameters are live for DAW automation tests.",
                          getLocalBounds().reduced (40, 0),
                          juce::Justification::centred, 3);
    }
};

juce::AudioProcessorEditor* LemonadeProcessor::createEditor()
{
    return new LemonadeEditor (*this);
}

// JUCE entry point
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new LemonadeProcessor();
}
