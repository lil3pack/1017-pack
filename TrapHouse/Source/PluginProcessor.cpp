#include "PluginProcessor.h"
#include "PluginEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;

TrapHouseProcessor::TrapHouseProcessor()
    : AudioProcessor (BusesProperties()
        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
    , apvts (*this, nullptr, "STATE", createLayout())
{
}

APVTS::ParameterLayout TrapHouseProcessor::createLayout()
{
    using namespace juce;

    std::vector<std::unique_ptr<RangedAudioParameter>> params;

    params.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { th::PID::inputGain, 1 }, "Input Gain",
        NormalisableRange<float> (-12.0f, 24.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes()
            .withLabel ("dB")
            .withStringFromValueFunction ([] (float v, int) {
                return juce::String (v, 1) + " dB";
            })));

    params.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { th::PID::ceiling, 1 }, "Ceiling",
        NormalisableRange<float> (-12.0f, 0.0f, 0.1f), -0.3f,
        AudioParameterFloatAttributes()
            .withLabel ("dB")
            .withStringFromValueFunction ([] (float v, int) {
                return juce::String (v, 1) + " dB";
            })));

    params.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { th::PID::knee, 1 }, "Soft Knee",
        NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.25f,
        AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int) {
                return juce::String ((int) std::round (v * 100.0f)) + "%";
            })));

    params.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { th::PID::harmonics, 1 }, "Harmonics",
        NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.40f,
        AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int) {
                return juce::String ((int) std::round (v * 100.0f)) + "%";
            })));

    params.push_back (std::make_unique<AudioParameterChoice> (
        ParameterID { th::PID::character, 1 }, "Character",
        StringArray { "HARD", "TAPE", "TUBE" }, 0));

    params.push_back (std::make_unique<AudioParameterBool> (
        ParameterID { th::PID::autoGain, 1 }, "Auto Gain", false));

    params.push_back (std::make_unique<AudioParameterBool> (
        ParameterID { th::PID::bypass, 1 }, "Bypass", false));

    return { params.begin(), params.end() };
}

void TrapHouseProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    clipper.prepare (sampleRate, samplesPerBlock, getTotalNumOutputChannels());
    setLatencySamples (clipper.getLatencySamples());
}

void TrapHouseProcessor::releaseResources()
{
    clipper.reset();
}

bool TrapHouseProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();

    if (in != out) return false;
    if (in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo())
        return false;

    return true;
}

void TrapHouseProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int totalIn  = getTotalNumInputChannels();
    const int totalOut = getTotalNumOutputChannels();

    for (int ch = totalIn; ch < totalOut; ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    if (apvts.getRawParameterValue (th::PID::bypass)->load() > 0.5f)
        return;

    // Pull current parameter values
    const float inGainDb = apvts.getRawParameterValue (th::PID::inputGain)->load();
    const float ceilDb   = apvts.getRawParameterValue (th::PID::ceiling)->load();
    const float knee     = apvts.getRawParameterValue (th::PID::knee)->load();
    const float harm     = apvts.getRawParameterValue (th::PID::harmonics)->load();
    const int   charIdx  = (int) apvts.getRawParameterValue (th::PID::character)->load();
    const bool  autoG    = apvts.getRawParameterValue (th::PID::autoGain)->load() > 0.5f;

    clipper.setInputGainDb (inGainDb);
    clipper.setCeilingDb   (ceilDb);
    clipper.setKnee        (knee);
    clipper.setHarmonics   (harm);
    clipper.setCharacter   (static_cast<th::dsp::ClipperCore::Character> (juce::jlimit (0, 2, charIdx)));
    clipper.setAutoGain    (autoG);

    // Input RMS for the meter
    float inSum = 0.0f;
    const int n = buffer.getNumSamples();
    for (int ch = 0; ch < totalIn; ++ch)
    {
        const auto* d = buffer.getReadPointer (ch);
        for (int i = 0; i < n; ++i) inSum += d[i] * d[i];
    }
    inputRms.store (std::sqrt (inSum / juce::jmax (1, n * totalIn)));

    clipper.process (buffer);

    // Output RMS
    float outSum = 0.0f;
    for (int ch = 0; ch < totalOut; ++ch)
    {
        const auto* d = buffer.getReadPointer (ch);
        for (int i = 0; i < n; ++i) outSum += d[i] * d[i];
    }
    outputRms.store (std::sqrt (outSum / juce::jmax (1, n * totalOut)));
}

juce::AudioProcessorEditor* TrapHouseProcessor::createEditor()
{
    return new TrapHouseEditor (*this);
}

void TrapHouseProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void TrapHouseProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

// --- JUCE entry point ---
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TrapHouseProcessor();
}
