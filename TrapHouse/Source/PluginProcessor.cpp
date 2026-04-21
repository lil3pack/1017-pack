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

    // DRIVE — macro 0-100%, internally maps to (input gain, knee, harmonics).
    params.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { th::PID::drive, 1 }, "Drive",
        NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.35f,
        AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int) {
                return juce::String ((int) std::round (v * 100.0f)) + "%";
            })));

    // SUB GUARD — 0% = standard clip, 100% = sub-bass (<120 Hz) stays untouched.
    params.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { th::PID::subGuard, 1 }, "Sub Guard",
        NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f,
        AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int) {
                return juce::String ((int) std::round (v * 100.0f)) + "%";
            })));

    params.push_back (std::make_unique<AudioParameterChoice> (
        ParameterID { th::PID::character, 1 }, "Character",
        StringArray { "HARD", "TAPE", "TUBE" }, 1)); // TAPE default — warmest, master-friendly

    params.push_back (std::make_unique<AudioParameterBool> (
        ParameterID { th::PID::autoGain, 1 }, "Auto Gain", true /* default ON */));

    params.push_back (std::make_unique<AudioParameterBool> (
        ParameterID { th::PID::bypass, 1 }, "Bypass", false));

    return { params.begin(), params.end() };
}

void TrapHouseProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    clipper.prepare (sampleRate, samplesPerBlock, getTotalNumOutputChannels());
    setLatencySamples (clipper.getLatencySamples());

    // Decimate for the scope so we show ~80 ms in the visible frames.
    const int target = juce::jmax (1, (int) std::round (sampleRate * 0.080 / th::dsp::ScopeBuffer::frameSize));
    scopeBuffer.setDecimation (target);
    scopeBuffer.reset();
}

void TrapHouseProcessor::releaseResources()
{
    clipper.reset();
    scopeBuffer.reset();
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

    // --- DRIVE macro mapping ---
    // Single knob that drives input gain, knee morph, and harmonic richness.
    // Ceiling stays fixed at -0.3 dBFS under the hood (mastering-friendly
    // default; a true peak would need intersample detection which is a
    // future enhancement).
    const float drive = juce::jlimit (0.0f, 1.0f,
        apvts.getRawParameterValue (th::PID::drive)->load());

    // DRIVE macro — pro master-grade mapping (v3.2, "warmth edition"):
    //   - Input gain: exponential (pow 1.6) curve, max +12 dB — gentle on master.
    //   - Harmonics: LINEAR curve (drive × 0.40) — gives consistent warmth
    //     even at low DRIVE (important for master-bus use where 20-30% drive
    //     should already add character, not be invisible).
    //   - Knee: softens more gradually.
    //   - Ceiling: -1.0 dBFS + TP limiter = -1 dBTP true-peak safe.
    //
    //   drive=0    → 0 dB    / knee 0.90 / harm 0.00   — transparent bypass
    //   drive=0.2  → +0.8 dB / knee 0.80 / harm 0.08   — invisible warmth
    //   drive=0.3  → +1.8 dB / knee 0.72 / harm 0.12   — subtle mastering lift
    //   drive=0.5  → +3.8 dB / knee 0.55 / harm 0.20   — audible character
    //   drive=0.7  → +6.5 dB / knee 0.35 / harm 0.28   — pushing
    //   drive=1.0  → +12.0 dB / knee 0.05 / harm 0.40  — destroy mode
    const float driveCurve = std::pow (drive, 1.6f);
    const float inGainDb = driveCurve * 12.0f;
    const float ceilDb   = -1.0f;
    const float knee     = 0.90f - driveCurve * 0.85f;
    const float harm     = drive * 0.40f; // linear → more warmth at low settings

    const float subGuard = juce::jlimit (0.0f, 1.0f,
        apvts.getRawParameterValue (th::PID::subGuard)->load());

    const int  charIdx = (int) apvts.getRawParameterValue (th::PID::character)->load();
    const bool autoG   = apvts.getRawParameterValue (th::PID::autoGain)->load() > 0.5f;

    clipper.setInputGainDb (inGainDb);
    clipper.setCeilingDb   (ceilDb);
    clipper.setKnee        (knee);
    clipper.setHarmonics   (harm);
    clipper.setSubGuard    (subGuard);
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

    // Output RMS + feed scope buffer (mono mixdown of output)
    float outSum = 0.0f;
    for (int ch = 0; ch < totalOut; ++ch)
    {
        const auto* d = buffer.getReadPointer (ch);
        for (int i = 0; i < n; ++i) outSum += d[i] * d[i];
    }
    outputRms.store (std::sqrt (outSum / juce::jmax (1, n * totalOut)));

    if (totalOut > 0)
    {
        const auto* l = buffer.getReadPointer (0);
        const auto* r = (totalOut > 1) ? buffer.getReadPointer (1) : l;
        for (int i = 0; i < n; ++i)
            scopeBuffer.push ((l[i] + r[i]) * 0.5f);
    }
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
